#include <string.h>
#include "ota_session.h"

#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "global_state.h"

#include "guards.h"
#include "http_cors.h"
#include "http_utils.h"
#include "macros.h"
#include "ota_validation.h"

static const char *TAG = "http_ota";

extern bool enter_recovery;

#define OTA_CHUNK_SIZE 2048
#define ESP_APP_IMAGE_MAGIC 0xE9

// Receive up to `len` bytes; tolerate at most two consecutive socket timeouts.
// Returns the number of bytes received, or <= 0 on error/disconnect.
static int recv_retry(httpd_req_t *req, char *dst, int len)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        int n = httpd_req_recv(req, dst, len);
        if (n != HTTPD_SOCK_ERR_TIMEOUT) {
            return n;
        }
    }
    return HTTPD_SOCK_ERR_TIMEOUT;
}

esp_err_t POST_WWW_update(httpd_req_t *req)
{
    // close connection when out of scope
    ConGuard g(http_server, req);
    OtaSession session;
    if (!session) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Another update is in progress");
    }

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // disable OTP when in recovery mode
    if (!enter_recovery && validateOTP(req) != ESP_OK) {
        return ESP_FAIL;
    }

    const esp_partition_t *www_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    if (www_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WWW partition not found");
        return ESP_FAIL;
    }

    // www.bin is always generated with exactly the size of the www partition
    const int total = req->content_len;
    if (total <= 0 || (size_t) total != www_partition->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid www.bin size");
        return ESP_FAIL;
    }

    // Receive the complete image into PSRAM first: the current web UI is only
    // erased once the new image is complete and looks valid.
    uint8_t *image = (uint8_t *) MALLOC((size_t) total);
    MemoryGuard gcImage(image);
    // use internal RAM for the flash write buffer (writes must not come from PSRAM)
    uint8_t *buf = (uint8_t *) malloc(OTA_CHUNK_SIZE);
    MemoryGuard gcBuf(buf);
    if (!image || !buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total) {
        int n = recv_retry(req, (char *) image + received, min(total - received, OTA_CHUNK_SIZE));
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            return ESP_FAIL;
        }
        received += n;
        // let other tasks run while receiving
        taskYIELD();
    }

    if (!www_image_looks_valid(image, (size_t) total, www_partition->size)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not a valid www.bin");
        return ESP_FAIL;
    }

    // Erase the entire www partition before writing
    {
        // lock the power management module
        LockGuard lg(POWER_MANAGEMENT_MODULE);
        ESP_LOGI(TAG, "erasing www partition ...");
        if (esp_partition_erase_range(www_partition, 0, www_partition->size) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erase Error");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "erasing done");
    }

    for (int offset = 0; offset < total; offset += OTA_CHUNK_SIZE) {
        int len = min(total - offset, OTA_CHUNK_SIZE);
        memcpy(buf, image + offset, (size_t) len);
        {
            // lock the power management module
            LockGuard lg(POWER_MANAGEMENT_MODULE);

            // print each 64kb
            if (!(offset & 0xffff)) {
                ESP_LOGI(TAG, "flashing to %08x", (unsigned) offset);
            }
            if (esp_partition_write(www_partition, (size_t) offset, buf, (size_t) len) != ESP_OK ||
                esp_partition_read(www_partition, (size_t) offset, buf, (size_t) len) != ESP_OK ||
                memcmp(buf, image + offset, (size_t) len) != 0) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WWW write or verification failed");
                return ESP_FAIL;
            }
        }

        // switch task to not stall the PID
        taskYIELD();
    }

    httpd_resp_sendstr(req, "WWW update complete\n");
    return ESP_OK;
}

/*
 * Handle OTA file upload
 */
esp_err_t POST_OTA_update(httpd_req_t *req)
{
    // close connection when out of scope
    ConGuard g(http_server, req);
    OtaSession session;
    if (!session) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Another update is in progress");
    }

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (validateOTP(req) != ESP_OK) {
        return ESP_FAIL;
    }

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (!ota_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA partition not found");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    if (remaining <= 0 || (size_t) remaining > ota_partition->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware size");
        return ESP_FAIL;
    }

    // don't put it on the stack
    char *buf = (char *) malloc(OTA_CHUNK_SIZE);
    MemoryGuard gcBuf(buf);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    // Receive the first chunk before touching the hardware, so obviously wrong
    // files are rejected while the miner keeps mining.
    int recv_len = recv_retry(req, buf, min(remaining, OTA_CHUNK_SIZE));
    if (recv_len <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
        return ESP_FAIL;
    }
    if ((uint8_t) buf[0] != ESP_APP_IMAGE_MAGIC) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not a firmware image");
        return ESP_FAIL;
    }

    // lock the power management module
    LockGuard lg(POWER_MANAGEMENT_MODULE);

    // Shut down buck converter before starting OTA.
    // During OTA, I2C conflicts prevent the PID from working correctly.
    // Disabling hardware avoids any risk of overheating.
    // The device will reboot after the update anyway.
    POWER_MANAGEMENT_MODULE.shutdown();

    // From here on the ASICs are off. Any failure must reboot the device,
    // otherwise it would stay up without mining until restarted by hand.
    // The boot partition is only switched on success, so the reboot comes
    // back with the current firmware.
    auto fail_and_restart = [&](const char *msg) -> esp_err_t {
        ESP_LOGE(TAG, "Firmware update failed: %s - restarting to resume mining", msg);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        vTaskDelay(pdMS_TO_TICKS(1000));
        POWER_MANAGEMENT_MODULE.restart();
        return ESP_FAIL; // unreachable
    };

    esp_ota_handle_t ota_handle;
    if (esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
        return fail_and_restart("OTA Begin Error");
    }

    uint32_t offset = 0;
    for (;;) {
        // print each 64kb
        if (!(offset & 0xffff)) {
            ESP_LOGI(TAG, "flashing to %08lx", offset);
        }
        if (esp_ota_write(ota_handle, (const void *) buf, recv_len) != ESP_OK) {
            esp_ota_abort(ota_handle);
            return fail_and_restart("Flash Error");
        }

        remaining -= recv_len;
        offset += recv_len;

        if (remaining <= 0) {
            break;
        }

        // give the scheduler a chance for other tasks
        taskYIELD();

        recv_len = recv_retry(req, buf, min(remaining, OTA_CHUNK_SIZE));
        if (recv_len <= 0) {
            esp_ota_abort(ota_handle);
            return fail_and_restart("Protocol Error");
        }
    }

    // Validate and switch to new OTA image and reboot
    if (esp_ota_end(ota_handle) != ESP_OK) {
        return fail_and_restart("Validation Error");
    }
    if (esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        return fail_and_restart("Activation Error");
    }

    httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");
    ESP_LOGI(TAG, "Restarting System because of Firmware update complete");
    vTaskDelay(pdMS_TO_TICKS(1000));
    POWER_MANAGEMENT_MODULE.restart();

    // unreachable
    return ESP_OK;
}

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

static inline uint16_t ota_read_le16(const uint8_t *p)
{
    return p[0] | (uint16_t(p[1]) << 8);
}

// Validate the ESP-IDF SPIFFS geometry and the generated web entry point.
// This is a structural preflight, not a filesystem scrub or signature check.
static inline bool www_image_looks_valid(const uint8_t *data, size_t len, size_t partition_size)
{
    constexpr size_t block = 4096, page = 256, page_header = 5;
    // Index header: aligned common header (8), size (4), type (1), name (64), metadata (4).
    constexpr size_t index_table = 8 + 4 + 1 + 64 + 4;
    constexpr char entry[] = "/index.html.gz";
    if (!data || len != partition_size || len == 0 || len % block != 0 || len / page > UINT16_MAX) {
        return false;
    }
    const size_t blocks = len / block;
    for (size_t i = 0; i < blocks; ++i) {
        const uint16_t magic = uint16_t(0x20140529u ^ page ^ (blocks - i));
        if (ota_read_le16(data + i * block + page - 4) != magic) {
            return false;
        }
    }

    for (size_t base = 0; base < len; base += block) {
        for (size_t slot = 1; slot < block / page; ++slot) {
            const uint16_t id = ota_read_le16(data + base + (slot - 1) * 2);
            const uint8_t *p = data + base + slot * page;
            if (!(id & 0x8000) || id == 0xFFFF || ota_read_le16(p) != id ||
                ota_read_le16(p + 2) != 0 || p[4] != 0xF8 || p[12] != 1 ||
                memcmp(p + 13, entry, sizeof(entry)) != 0) {
                continue;
            }
            const uint32_t size = ota_read_le16(p + 8) | (uint32_t(ota_read_le16(p + 10)) << 16);
            if (size < 18 || size > ((page - index_table) / 2) * (page - page_header)) {
                return false;
            }
            const size_t count = (size + page - page_header - 1) / (page - page_header);
            const uint16_t data_id = id & 0x7FFF;
            for (size_t span = 0; span < count; ++span) {
                const size_t ref = ota_read_le16(p + index_table + 2 * span);
                const size_t ref_slot = ref % (block / page);
                if (ref >= len / page || ref_slot == 0) {
                    return false;
                }
                const uint8_t *q = data + ref * page;
                const auto *lookup = data + (ref * page / block) * block;
                if (ota_read_le16(lookup + (ref_slot - 1) * 2) != data_id ||
                    ota_read_le16(q) != data_id || ota_read_le16(q + 2) != span || q[4] != 0xFC) {
                    return false;
                }
                if (span == 0 && (q[5] != 0x1F || q[6] != 0x8B || q[7] != 8)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

static inline bool is_allowed_redirect(const char *url)
{
    static const char https[] = "https://";
    if (!url || strncasecmp(url, https, sizeof(https) - 1) != 0) {
        return false;
    }
    for (const char *q = url; *q; ++q) {
        const unsigned char c = *q;
        if (c <= 0x20 || c == 0x7F || c == '\\') {
            return false;
        }
    }
    const char *host = url + sizeof(https) - 1;
    const size_t host_len = strcspn(host, "/?#");
    static const char *const hosts[] = {"github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"};
    for (const char *allowed : hosts) {
        if (strlen(allowed) == host_len && strncasecmp(host, allowed, host_len) == 0) {
            return true;
        }
    }
    return false;
}

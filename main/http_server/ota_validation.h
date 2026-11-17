#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

// Sanity checks for a SPIFFS web UI image before it replaces the current one.
// The build always produces www.bin with exactly the size of the www partition,
// and the image always contains the web UI entry point (index.html[.gz]).
// This is not a cryptographic check; it rejects empty, truncated or obviously
// wrong files so the current web UI is not destroyed by a bad upload.
static inline bool www_image_looks_valid(const uint8_t *data, size_t len, size_t partition_size)
{
    if (!data || len == 0 || len != partition_size) {
        return false;
    }

    bool has_data = false;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] != 0xFF) {
            has_data = true;
            break;
        }
    }
    if (!has_data) {
        return false;
    }

    static const char marker[] = "index.html";
    return memmem(data, len, marker, sizeof(marker) - 1) != nullptr;
}

// --- Helper: only follow redirects to GitHub over HTTPS.
// Release downloads go github.com -> release-assets.githubusercontent.com
// (older: objects.githubusercontent.com), so accept github.com and its
// *.githubusercontent.com download hosts only.
static inline bool is_allowed_redirect(const char *url)
{
    static const char https[] = "https://";
    if (!url || strncasecmp(url, https, sizeof(https) - 1) != 0) {
        return false;
    }

    const char *host = url + sizeof(https) - 1;
    size_t host_len = strcspn(host, "/?#");
    if (host_len == 0 || host_len >= 256) {
        return false;
    }

    // reject userinfo ("user@host") and explicit ports
    for (size_t i = 0; i < host_len; ++i) {
        if (host[i] == '@' || host[i] == ':') {
            return false;
        }
    }

    for (const char *q = url; *q; ++q) {
        unsigned char c = (unsigned char) *q;
        if (c < 0x20 || c == 0x7F || c == '\\') {
            return false;
        }
    }

    static const char github[] = "github.com";
    static const char ghusercontent[] = ".githubusercontent.com";
    const size_t gh_len = sizeof(github) - 1;
    const size_t ghuc_len = sizeof(ghusercontent) - 1;

    if (host_len == gh_len && strncasecmp(host, github, gh_len) == 0) {
        return true;
    }
    if (host_len > ghuc_len && strncasecmp(host + host_len - ghuc_len, ghusercontent, ghuc_len) == 0) {
        return true;
    }
    return false;
}

// Host unit test for main/http_server/ota_validation.h
//   c++ -std=c++17 -D_GNU_SOURCE -Imain/http_server tools/test_ota_validation.cpp -o /tmp/test_ota_validation
//   /tmp/test_ota_validation [path/to/www.bin]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ota_validation.h"

static int failures = 0;
#define EXPECT(cond)                                                    \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

int main(int argc, char **argv)
{
    // --- redirects
    EXPECT(is_allowed_redirect("https://github.com/brunneis/x/releases/download/v1/a.bin"));
    EXPECT(is_allowed_redirect("https://release-assets.githubusercontent.com/github-production-release-asset/1?sp=r&sig=x"));
    EXPECT(is_allowed_redirect("https://objects.githubusercontent.com/a"));
    EXPECT(is_allowed_redirect("HTTPS://GitHub.com/a"));
    EXPECT(!is_allowed_redirect("http://github.com/a"));                       // no TLS
    EXPECT(!is_allowed_redirect("https://evil.com/github.com"));
    EXPECT(!is_allowed_redirect("https://github.com.evil.com/a"));
    EXPECT(!is_allowed_redirect("https://evilgithubusercontent.com/a"));      // no dot boundary
    EXPECT(!is_allowed_redirect("https://githubusercontent.com.evil.com/a"));
    EXPECT(!is_allowed_redirect("https://github.com@evil.com/a"));            // userinfo
    EXPECT(!is_allowed_redirect("https://github.com:444/a"));                 // port
    EXPECT(!is_allowed_redirect("https://github.com/a\r\nX: y"));             // control chars
    EXPECT(!is_allowed_redirect("/relative/path"));
    EXPECT(!is_allowed_redirect(""));
    EXPECT(!is_allowed_redirect(nullptr));

    // --- www images (synthetic)
    const size_t part = 3 * 1024 * 1024;
    std::vector<uint8_t> img(part, 0xFF);
    EXPECT(!www_image_looks_valid(img.data(), img.size(), part));              // erased / empty
    std::memcpy(&img[4096], "/index.html.gz", 14);
    img[0] = 0x01;
    EXPECT(www_image_looks_valid(img.data(), img.size(), part));
    EXPECT(!www_image_looks_valid(img.data(), img.size() - 1, part));          // truncated
    EXPECT(!www_image_looks_valid(img.data(), 0, part));
    EXPECT(!www_image_looks_valid(nullptr, part, part));
    std::vector<uint8_t> noindex(part, 0x00);
    EXPECT(!www_image_looks_valid(noindex.data(), noindex.size(), part));      // wrong content

    // --- real www.bin (optional)
    if (argc > 1) {
        FILE *f = std::fopen(argv[1], "rb");
        if (!f) { std::perror(argv[1]); return 2; }
        std::vector<uint8_t> real(part + 1);
        size_t n = std::fread(real.data(), 1, real.size(), f);
        std::fclose(f);
        EXPECT(n == part);
        EXPECT(www_image_looks_valid(real.data(), n, part));
        // a firmware image must not pass as www.bin
    }

    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("PASS: ota_validation\n");
    return 0;
}

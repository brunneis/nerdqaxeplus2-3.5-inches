/* Host regression test using the actual LVGL lv_color_make implementation.
 * Compile against the managed LVGL headers with the SAME color flags as firmware:
 * cc -DLV_CONF_SKIP -DLV_COLOR_DEPTH=16 -DLV_COLOR_16_SWAP=1 \
 *    -Imanaged_components/lvgl__lvgl/src tools/check_lvgl_palette.c -o /tmp/check_lvgl_palette
 * Inputs: original RGB565 image bytes, indexed palette+pixel bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include "misc/lv_color.h"

static unsigned char *load(const char *path, size_t expected) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    unsigned char *b = malloc(expected);
    if (!b || fread(b, 1, expected, f) != expected || fgetc(f) != EOF) {
        fprintf(stderr, "Wrong input size: %s\n", path); exit(2);
    }
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const size_t pixels = 480 * 320;
    unsigned char *original = load(argv[1], pixels * 2);
    unsigned char *indexed = load(argv[2], 1024 + pixels);
    for (size_t i = 0; i < pixels; ++i) {
        const unsigned char *p = indexed + indexed[1024 + i] * 4;
        lv_color_t color = lv_color_make(p[2], p[1], p[0]);
        /* Match lv_img_decoder.c's read_line output, including byte swapping. */
        unsigned char low = color.full & 255, high = color.full >> 8;
        if (p[3] != 255 || low != original[i*2] || high != original[i*2+1]) {
            fprintf(stderr, "LVGL mismatch at pixel %zu: original=%02x%02x decoded=%02x%02x (SWAP=%d)\n",
                    i, original[i*2], original[i*2+1], low, high, LV_COLOR_16_SWAP);
            return 1;
        }
    }
    printf("PASS: %zu exact pixels through LVGL lv_color_make (SWAP=%d)\n", pixels, LV_COLOR_16_SWAP);
    free(original); free(indexed);
    return 0;
}

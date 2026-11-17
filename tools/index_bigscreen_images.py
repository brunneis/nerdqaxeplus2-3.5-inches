#!/usr/bin/env python3
"""Losslessly index the fork's RGB565 C images; verify exact pixel round trips.

Run once on TRUE_COLOR generated images; --check also accepts indexed output.
No image libraries or color quantization: the original RGB565 values are retained.
LVGL's indexed palette uses BGRA8888 and converts it back with lv_color_make.
The fork's RGB565 assets are MSB-first: sdkconfig.defaults enables
CONFIG_LV_COLOR_16_SWAP=y (lv_conf.h is skipped in the real build).
"""
import argparse
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
DIRECTORY = pathlib.Path('main/displays/images/themes/NerdQaxePlus2')


def data(text):
    array = text.split('= {', 1)[1].split('};', 1)[0]
    return bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', array))


def decode_indexed(raw):
    palette = []
    for i in range(256):
        b, g, r, a = raw[i * 4:i * 4 + 4]
        assert a == 255
        palette.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return b''.join(palette[i].to_bytes(2, 'big') for i in raw[1024:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='compare indexed files to original port commit')
    parser.add_argument('--reference', default='5b0b3507', help='original RGB565 git revision')
    parser.add_argument('--from-reference', action='store_true', help='regenerate directly from original RGB565 git revision')
    args = parser.parse_args()
    assert 'CONFIG_LV_COLOR_16_SWAP=y' in (ROOT / 'sdkconfig.defaults').read_text()
    for path in sorted((ROOT / DIRECTORY).glob('ui_img*.c')):
        text = (subprocess.check_output(['git', 'show', f'{args.reference}:{path.relative_to(ROOT)}'], cwd=ROOT).decode()
                if args.from_reference else path.read_text())
        raw = data(text)
        if args.check:
            original = subprocess.check_output(['git', 'show', f'{args.reference}:{path.relative_to(ROOT)}'], cwd=ROOT).decode()
            assert 'LV_IMG_CF_INDEXED_8BIT' in text
            assert len(raw) == 1024 + 480 * 320
            assert decode_indexed(raw) == data(original), path.name
            print(f'OK exact RGB565 round trip: {path.name}')
            continue
        assert 'LV_IMG_CF_TRUE_COLOR' in text, 'Expected original TRUE_COLOR input'
        assert len(raw) == 480 * 320 * 2
        values = [int.from_bytes(raw[i:i+2], 'big') for i in range(0, len(raw), 2)]
        colors = list(dict.fromkeys(values))
        assert len(colors) <= 256
        lookup = {value: i for i, value in enumerate(colors)}
        palette = bytearray()
        for value in colors + [0] * (256 - len(colors)):
            r, g, b = (value >> 11) & 31, (value >> 5) & 63, value & 31
            palette.extend(((b << 3) | (b >> 2), (g << 2) | (g >> 4), (r << 3) | (r >> 2), 255))
        indexed = bytes(palette) + bytes(lookup[v] for v in values)
        assert decode_indexed(indexed) == raw
        start = text.index('= {') + len('= {')
        end = text.index('};', start)
        lines = ['    ' + ', '.join(f'0x{v:02X}' for v in indexed[i:i+16]) + ',' for i in range(0, len(indexed), 16)]
        text = text[:start] + '\n' + '\n'.join(lines) + '\n' + text[end:]
        text = text.replace('LV_IMG_CF_TRUE_COLOR', 'LV_IMG_CF_INDEXED_8BIT')
        text = text.replace('// IMAGE DATA:', '// Lossless indexed RGB565 palette (tools/index_bigscreen_images.py)\n// IMAGE DATA:')
        path.write_text(text)
        print(f'{path.name}: {len(colors)} colors, {len(raw)} -> {len(indexed)} bytes')


if __name__ == '__main__':
    main()

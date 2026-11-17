#!/usr/bin/env python3
"""Validate fork release packaging without flashing a device (stdlib only)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    build = args.build_dir
    factory = (build / 'esp-miner-factory-NERDQAXEPLUS2.bin').read_bytes()
    pieces = [
        (0x0, 'bootloader/bootloader.bin'),
        (0x8000, 'partition_table/partition-table.bin'),
        (0x10000, 'esp-miner.bin'),
        (0x410000, 'www.bin'),
        (0xf10000, 'ota_data_initial.bin'),
    ]
    for offset, name in pieces:
        content = (build / name).read_bytes()
        assert factory[offset:offset+len(content)] == content, f'Factory mismatch: {name}'
        print(f'OK factory offset {offset:#x}: {name}')
    assert factory[0x9000:0x10000] == b'\xff' * 0x7000, 'Factory includes NVS/config data'
    app = (build / 'esp-miner.bin').read_bytes()
    assert app == (build / 'esp-miner-NerdQAxe++.bin').read_bytes()
    assert app[0] == 0xe9 and struct.unpack_from('<I', app, 32)[0] == 0xabcd5432
    embedded_version = app[48:80].split(b'\0', 1)[0].decode()
    assert embedded_version == args.version, (embedded_version, args.version)
    partitions = {}
    table = (build / 'partition_table/partition-table.bin').read_bytes()
    for offset in range(0, len(table), 32):
        if table[offset:offset+2] != b'\xaa\x50':
            break
        _, kind, subtype, address, size, label, flags = struct.unpack_from('<HBBII16sI', table, offset)
        label = label.split(b'\0', 1)[0].decode()
        partitions[label] = {'type': kind, 'subtype': subtype, 'offset': address, 'size': size}
        if kind == 0:
            assert len(app) <= size, f'App overflows {label}'
    expected = {'nvs': (0x9000, 0x6000), 'factory': (0x10000, 0x400000),
                'www': (0x410000, 0x300000), 'ota_0': (0x710000, 0x400000),
                'ota_1': (0xb10000, 0x400000), 'otadata': (0xf10000, 0x2000)}
    for label, (offset, size) in expected.items():
        assert partitions[label]['offset'] == offset and partitions[label]['size'] == size, label
    assert len(factory) == 0xf12000
    assets = {}
    for name in ['esp-miner-factory-NERDQAXEPLUS2.bin', 'esp-miner-NerdQAxe++.bin', 'www.bin']:
        content = (build / name).read_bytes()
        assets[name] = {'bytes': len(content), 'sha256': hashlib.sha256(content).hexdigest()}
    result = {'version': embedded_version, 'app_free_bytes': 0x400000 - len(app),
              'factory_nvs_blank': True, 'partitions': partitions, 'assets': assets}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 github@nwdxlgzs
"""Regenerate committed brand assets. Dev-only: pip install pillow cairosvg.

Normal CMake builds use the committed icons and do not require these packages.
The small ICO frames use BMP/DIB so IE11 and the Windows 7 shell can read them.
"""
import gzip
import hashlib
import io
import json
import struct
from pathlib import Path

import cairosvg
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
BRAND = ROOT / 'resources/branding'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def save(name, data):
    path = BRAND / name
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def dib(image):
    width, height = image.size
    pixels = image.load()
    bitmap = bytearray()
    mask = bytearray()
    stride = ((width + 31) // 32) * 4
    for y in range(height - 1, -1, -1):
        row = bytearray(stride)
        for x in range(width):
            red, green, blue, alpha = pixels[x, y]
            bitmap.extend((blue, green, red, alpha))
            if alpha < 128:
                row[x // 8] |= 1 << (7 - x % 8)
        mask.extend(row)
    header = struct.pack('<IiiHHIIiiII', 40, width, height * 2, 1, 32,
                         0, len(bitmap), 0, 0, 0, 0)
    return header + bitmap + mask


def main():
    svg = (BRAND / 'logo.svg').read_bytes()
    rendered = cairosvg.svg2png(bytestring=svg, output_width=1024, output_height=1024)
    master = Image.open(io.BytesIO(rendered)).convert('RGBA')

    def png(size):
        out = io.BytesIO()
        master.resize((size, size), Image.Resampling.LANCZOS).save(out, format='PNG', optimize=True)
        return out.getvalue()

    save('logo.png', png(512))
    save('favicon-32.png', png(32))
    # Apple touch icons use an opaque square; the OS adds its own outer mask.
    touch = Image.new('RGBA', (180, 180), '#2766c8')
    touch.alpha_composite(master.resize((180, 180), Image.Resampling.LANCZOS))
    out = io.BytesIO()
    touch.convert('RGB').save(out, format='PNG', optimize=True)
    save('apple-touch-icon.png', out.getvalue())

    sizes = (16, 24, 32, 48, 64, 128, 256)
    frames = [(size, png(size) if size >= 64 else dib(master.resize((size, size), Image.Resampling.LANCZOS))) for size in sizes]
    offset = 6 + 16 * len(frames)
    entries, bodies = bytearray(), bytearray()
    for size, data in frames:
        entries.extend(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset))
        bodies.extend(data)
        offset += len(data)
    save('favicon.ico', struct.pack('<HHH', 0, 1, len(frames)) + entries + bodies)

    assets = {}
    for name in ('logo.svg', 'favicon.ico', 'apple-touch-icon.png'):
        data = (BRAND / name).read_bytes()
        packed = bytearray(gzip.compress(data, compresslevel=9, mtime=0))
        packed[9] = 255
        save(name + '.gz', bytes(packed))
        assets[name] = {'bytes': len(data), 'sha256': digest(data),
                        'gzipBytes': len(packed), 'gzipSha256': digest(packed)}
    files = {name: digest((BRAND / name).read_bytes()) for name in
             ('logo.svg', 'logo-mono.svg', 'logo.png', 'favicon-32.png', 'favicon.ico', 'apple-touch-icon.png')}
    save('manifest.json', (json.dumps({'assets': assets, 'files': files}, indent=2) + '\n').encode())
    print('Brand icons generated:', ', '.join(str(size) for size in sizes), 'px')


if __name__ == '__main__':
    main()

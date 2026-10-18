#!/usr/bin/env python3
"""Losslessly store the original ICO's PNG images as Windows 32-bit DIBs.

Developer asset-generation tool only; normal builds use the saved ICO. Uses
the same installed Pillow dependency as splash_to_h.py. Never changes artwork.
"""
import argparse
import io
from pathlib import Path
import struct
from PIL import Image


def convert(source):
    reserved, kind, count = struct.unpack_from('<HHH', source)
    if (reserved, kind, count) != (0, 1, 6):
        raise ValueError('Expected the original six-size Moonstone icon')
    entries, payloads = [], []
    offset = 6 + count * 16
    for i in range(count):
        entry = bytearray(source[6 + i * 16:22 + i * 16])
        length, start = struct.unpack_from('<II', entry, 8)
        png = source[start:start + length]
        if not png.startswith(b'\x89PNG\r\n\x1a\n') or len(png) != length:
            raise ValueError('Expected complete PNG icon images')
        image = Image.open(io.BytesIO(png)).convert('RGBA')
        width, height = image.size
        if (width, height) != (entry[0] or 256, entry[1] or 256):
            raise ValueError('ICO dimensions disagree with PNG')
        rgba = image.tobytes()
        pixels, mask = bytearray(), bytearray()
        stride = ((width + 31) // 32) * 4
        for y in range(height - 1, -1, -1):
            row_mask = bytearray(stride)
            for x in range(width):
                r, g, b, a = rgba[(y * width + x) * 4:(y * width + x + 1) * 4]
                pixels.extend((b, g, r, a))
                if not a:
                    row_mask[x // 8] |= 128 >> (x % 8)
            mask.extend(row_mask)
        dib = struct.pack('<IiiHHIIiiII', 40, width, height * 2, 1, 32, 0,
                          len(pixels) + len(mask), 0, 0, 0, 0) + pixels + mask
        struct.pack_into('<H', entry, 4, 1)
        struct.pack_into('<II', entry, 8, len(dib), offset)
        entries.append(entry)
        payloads.append(dib)
        offset += len(dib)
    return source[:6] + b''.join(entries) + b''.join(payloads)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    if args.source.resolve() == args.output.resolve():
        ap.error('Keep the original source icon')
    args.output.write_bytes(convert(args.source.read_bytes()))

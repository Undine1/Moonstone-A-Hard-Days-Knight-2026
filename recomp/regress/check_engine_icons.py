#!/usr/bin/env python3
"""Independent pixel/PE proof for release-only uncompressed icon storage.

Requires installed test libraries pefile/Pillow. --before is an untouched public
PNG-icon engine; --after is the normal public build. Only scratch copies change.
"""
import argparse
import io
from pathlib import Path
import struct
import sys
import pefile
from PIL import Image

RECOMP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RECOMP))
from engine_icons import store_icons, validate_icons, verify_payloads
from release_gate import validate_engine


def resource_data(pe):
    return {(a.id, b.id, c.id): pe.get_data(c.data.struct.OffsetToData, c.data.struct.Size)
            for a in pe.DIRECTORY_ENTRY_RESOURCE.entries for b in a.directory.entries
            for c in b.directory.entries}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--before', required=True, type=Path)
    ap.add_argument('--after', type=Path, default=RECOMP / 'build/release/moonstone.exe')
    ap.add_argument('--output', required=True, type=Path)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    original, final = args.before.read_bytes(), args.after.read_bytes()
    a, b = pefile.PE(data=original), pefile.PE(data=final)
    require_clean = lambda pe: not pe.get_warnings()
    assert require_clean(a) and require_clean(b), (a.get_warnings(), b.get_warnings())
    ra, rb = resource_data(a), resource_data(b)
    assert ra.keys() == rb.keys()
    original_ico = Image.open(RECOMP / 'icon/moonstone.ico')
    raw_ico = Image.open(RECOMP / 'icon/moonstone-raw.ico')
    for key, png in ra.items():
        if key[0] != 3:
            if key[0] != 14:
                assert rb[key] == png
            continue
        im = Image.open(io.BytesIO(png)).convert('RGBA')
        width, height = im.size
        dib = rb[key]
        assert struct.unpack_from('<IiiHHI', dib) == (40, width, height * 2, 1, 32, 0)
        # Decode the bitmap rows, alpha and AND mask independently of the generator.
        pixels = bytearray()
        for y in range(height):
            for x in range(width):
                blue, green, red, alpha = dib[40 + ((height - 1 - y) * width + x) * 4:44 + ((height - 1 - y) * width + x) * 4]
                pixels.extend((red, green, blue, alpha))
                stride = ((width + 31) // 32) * 4
                mask = dib[40 + width * height * 4 + (height - 1 - y) * stride + x // 8]
                assert bool(mask & (128 >> (x % 8))) == (alpha == 0)
        assert pixels == im.tobytes()
        assert raw_ico.ico.getimage(im.size).convert('RGBA').tobytes() == im.tobytes()
        assert original_ico.ico.getimage(im.size).convert('RGBA').tobytes() == im.tobytes()
    for left, right in zip(a.sections, b.sections):
        assert left.Name == right.Name and left.Characteristics == right.Characteristics
        if left.Name.rstrip(b'\0') != b'.rsrc':
            assert left.get_data() == right.get_data()
            if left.Name.rstrip(b'\0') != b'.reloc':
                assert left.VirtualAddress == right.VirtualAddress
    for i in range(16):
        if i not in (2, 5):
            assert a.OPTIONAL_HEADER.DATA_DIRECTORY[i].__pack__() == b.OPTIONAL_HEADER.DATA_DIRECTORY[i].__pack__()
    assert b.FILE_HEADER.TimeDateStamp == 0
    assert a.OPTIONAL_HEADER.AddressOfEntryPoint == b.OPTIONAL_HEADER.AddressOfEntryPoint
    assert a.OPTIONAL_HEADER.DllCharacteristics == b.OPTIONAL_HEADER.DllCharacteristics
    verify_payloads(original, final)
    validate_engine(args.after)
    # Identical independent builds and already-converted copies must be stable.
    for name in ('first.exe', 'second.exe'):
        target = args.output / name
        target.write_bytes(original)
        store_icons(target)
        assert target.read_bytes() == final
        store_icons(target)
        assert target.read_bytes() == final
    try:
        validate_icons(args.before)
    except ValueError:
        pass
    else:
        raise AssertionError('Release gate accepted PNG icons returning')
    text, = [s for s in a.sections if s.Name.rstrip(b'\0') == b'.text']
    changed = bytearray(final)
    changed[text.PointerToRawData] ^= 1
    try:
        verify_payloads(original, changed)
    except ValueError:
        pass
    else:
        raise AssertionError('Resource conversion allowed a code change')
    print('PASS six sizes: pixels, alpha, masks, ICO decoding; code/data/imports/TLS/permissions unchanged')
    print('PASS exact reproducibility, idempotence and PNG/code-change rejection guards')


if __name__ == '__main__':
    main()

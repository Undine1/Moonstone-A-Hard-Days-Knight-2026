"""Release-only lossless icon storage, using the Windows resource APIs.

No game code runs. No compiler, imports, gameplay data or icon pixels change.
Normal builds need only Python's standard library and the checked-in raw ICO.
"""
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import shutil
import struct
import tempfile

HERE = Path(__file__).resolve().parent


def require(ok, message):
    if not ok:
        raise ValueError('Engine icon storage: ' + message)


def ico_entries(path, *, raw=False):
    data = Path(path).read_bytes()
    require(len(data) >= 102 and struct.unpack_from('<HHH', data) == (0, 1, 6), 'unexpected ICO header')
    result = []
    next_offset = 102
    for i in range(6):
        entry = data[6 + i * 16:22 + i * 16]
        length, offset = struct.unpack_from('<II', entry, 8)
        require(offset == next_offset and offset + length <= len(data), 'noncontiguous/incomplete icon')
        payload = data[offset:offset + length]
        width, height = entry[0] or 256, entry[1] or 256
        if raw:
            require(len(payload) >= 40, 'incomplete DIB')
            header = struct.unpack_from('<IiiHHIIiiII', payload)
            size = width * height * 4 + ((width + 31) // 32) * 4 * height
            require(header == (40, width, height * 2, 1, 32, 0, size, 0, 0, 0, 0), 'unexpected DIB format')
            require(len(payload) == 40 + size, 'incomplete bitmap or mask')
        else:
            require(payload.startswith(b'\x89PNG\r\n\x1a\n'), 'unexpected source icon format')
        result.append((entry, payload))
        next_offset += length
    require(next_offset == len(data), 'unexpected ICO trailing bytes')
    return result


def api():
    kernel = C.WinDLL('kernel32', use_last_error=True)
    signatures = {
        'LoadLibraryExW': ([W.LPCWSTR, W.HANDLE, W.DWORD], W.HMODULE),
        'FreeLibrary': ([W.HMODULE], W.BOOL),
        'FindResourceExW': ([W.HMODULE, C.c_void_p, C.c_void_p, W.WORD], W.HANDLE),
        'SizeofResource': ([W.HMODULE, W.HANDLE], W.DWORD),
        'LoadResource': ([W.HMODULE, W.HANDLE], W.HANDLE),
        'LockResource': ([W.HANDLE], C.c_void_p),
        'BeginUpdateResourceW': ([W.LPCWSTR, W.BOOL], W.HANDLE),
        'UpdateResourceW': ([W.HANDLE, C.c_void_p, C.c_void_p, W.WORD, C.c_void_p, W.DWORD], W.BOOL),
        'EndUpdateResourceW': ([W.HANDLE, W.BOOL], W.BOOL),
    }
    for name, (args, result) in signatures.items():
        getattr(kernel, name).argtypes = args
        getattr(kernel, name).restype = result
    return kernel


def resources(path):
    kernel = api()
    # Resource/data-file mappings do not resolve imports or run TLS/entrypoints.
    module = kernel.LoadLibraryExW(str(Path(path).resolve()), None, 0x22)
    if not module:
        raise C.WinError(C.get_last_error())
    result = {}
    try:
        for kind, number in [(3, i) for i in range(1, 7)] + [(14, 1)]:
            found = kernel.FindResourceExW(module, C.c_void_p(kind), C.c_void_p(number), 1033)
            require(bool(found), 'missing original icon resource')
            size = kernel.SizeofResource(module, found)
            loaded = kernel.LoadResource(module, found)
            pointer = kernel.LockResource(loaded)
            require(size > 0 and bool(pointer), 'unreadable icon resource')
            result[kind, number] = C.string_at(pointer, size)
    finally:
        kernel.FreeLibrary(module)
    return result


def expected_resources():
    original = ico_entries(HERE / 'icon/moonstone.ico')
    raw = ico_entries(HERE / 'icon/moonstone-raw.ico', raw=True)
    group = bytearray(struct.pack('<HHH', 0, 1, 6))
    result = {}
    for number, ((old_entry, _), (new_entry, payload)) in enumerate(zip(original, raw), 1):
        require(old_entry[:4] == new_entry[:4] and old_entry[6:8] == new_entry[6:8], 'icon dimensions/format metadata changed')
        require(struct.unpack_from('<H', old_entry, 4)[0] in (0, 1) and new_entry[4:6] == b'\1\0', 'unexpected icon planes')
        group += new_entry[:12] + struct.pack('<H', number)
        result[3, number] = payload
    result[14, 1] = bytes(group)
    return result


def validate_icons(path):
    require(resources(path) == expected_resources(), 'release icons differ from checked raw assets')


def layout(data):
    require(data[:2] == b'MZ', 'invalid DOS header')
    pe, = struct.unpack_from('<I', data, 0x3c)
    require(data[pe:pe + 4] == b'PE\0\0', 'invalid PE header')
    machine, count, stamp, symbols, nsym, optsize, flags = struct.unpack_from('<HHIIIHH', data, pe + 4)
    require(machine == 0x8664 and optsize == 240 and not stamp and not symbols and not nsym, 'expected normalized x64 release')
    opt = data[pe + 24:pe + 24 + optsize]
    require(struct.unpack_from('<H', opt)[0] == 0x20b, 'expected PE32+')
    require(opt[112 + 4 * 8:112 + 5 * 8] == bytes(8), 'cannot modify a signed image')
    sections = {}
    for i in range(count):
        header = data[pe + 24 + optsize + 40 * i:pe + 24 + optsize + 40 * (i + 1)]
        name, vsize, rva, size, offset = struct.unpack_from('<8sIIII', header)
        require(name not in sections and offset + size <= len(data), 'invalid section layout')
        sections[name] = (header, data[offset:offset + size])
    return (machine, flags), opt, sections


def verify_payloads(before, after):
    identity_a, opt_a, a = layout(before)
    identity_b, opt_b, b = layout(after)
    require(identity_a == identity_b and a.keys() == b.keys(), 'PE identity or sections changed')
    # Only resource size/image size and resource/relocation directory locations
    # may change. This includes entrypoint, imports, TLS, unwind and protections.
    ignore = set(range(8, 12)) | set(range(56, 60)) | set(range(128, 136)) | set(range(152, 160))
    require(all(x == y for i, (x, y) in enumerate(zip(opt_a, opt_b)) if i not in ignore), 'runtime PE options changed')
    for name, (old_header, old_data) in a.items():
        new_header, new_data = b[name]
        require(old_header[36:] == new_header[36:], 'section permissions changed')
        if name.rstrip(b'\0') != b'.rsrc':
            require(old_data == new_data, 'non-resource payload changed')
            require(old_header[8:12] == new_header[8:12], 'non-resource virtual size changed')
            if name.rstrip(b'\0') != b'.reloc':
                require(old_header[12:16] == new_header[12:16], 'runtime data/code address changed')


def store_icons(path):
    path = Path(path).resolve()
    before = path.read_bytes()
    layout(before)
    existing = resources(path)
    desired = expected_resources()
    if existing == desired:
        return
    original = ico_entries(HERE / 'icon/moonstone.ico')
    require(all(existing[3, i] == item[1] for i, item in enumerate(original, 1)), 'input artwork is not the original')
    expected_group = struct.pack('<HHH', 0, 1, 6) + b''.join(
        # The resource compiler resolves the PNG ICO's zero plane count to one.
        entry[:4] + struct.pack('<H', 1) + entry[6:12] + struct.pack('<H', i)
        for i, (entry, _) in enumerate(original, 1))
    require(existing[14, 1] == expected_group, 'unexpected original icon group')
    kernel = api()
    with tempfile.TemporaryDirectory(prefix='engine-icons-', dir=path.parent) as scratch:
        candidate = Path(scratch) / path.name
        shutil.copyfile(path, candidate)
        handle = kernel.BeginUpdateResourceW(str(candidate), False)
        if not handle:
            raise C.WinError(C.get_last_error())
        try:
            for (kind, number), payload in desired.items():
                buf = C.create_string_buffer(payload)
                if not kernel.UpdateResourceW(handle, C.c_void_p(kind), C.c_void_p(number), 1033, buf, len(payload)):
                    raise C.WinError(C.get_last_error())
        except BaseException:
            kernel.EndUpdateResourceW(handle, True)
            raise
        if not kernel.EndUpdateResourceW(handle, False):
            raise C.WinError(C.get_last_error())
        verify_payloads(before, candidate.read_bytes())
        validate_icons(candidate)
        candidate.replace(path)

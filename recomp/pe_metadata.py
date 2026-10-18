"""Remove the bundled linker's empty REPRO marker from unsigned public EXEs.

The COFF timestamp is left unspecified (zero). No code/data/resource bytes,
loaded addresses or permissions change. Unexpected layouts fail the build.
This uses only the standard library and is never applied to the debug engine.
"""
from pathlib import Path
import struct


def require(ok, message):
    if not ok:
        raise ValueError(f"Public PE metadata: {message}")


def normalize(data):
    original = bytes(data)
    require(len(original) >= 64 and original[:2] == b"MZ", "missing DOS header")
    pe, = struct.unpack_from("<I", original, 0x3c)
    require(pe >= 64 and pe + 24 <= len(original), "invalid PE offset")
    require(original[pe:pe + 4] == b"PE\0\0", "missing PE signature")
    machine, count, _, symbols, symbol_count, optsize, _ = struct.unpack_from("<HHIIIHH", original, pe + 4)
    require(machine == 0x8664 and not symbols and not symbol_count, "expected stripped x64 image")
    opt, table = pe + 24, pe + 24 + optsize
    require(optsize == 240 and table + count * 40 <= len(original), "invalid header sizes")
    require(struct.unpack_from("<H", original, opt)[0] == 0x20b, "expected PE32+")
    align, file_align = struct.unpack_from("<II", original, opt + 32)
    require((align, file_align) == (4096, 512), "unexpected section alignment")
    headers, checksum = struct.unpack_from("<II", original, opt + 60)
    require(table + count * 40 <= headers <= len(original) and checksum == 0, "unexpected headers/checksum")
    require(struct.unpack_from("<I", original, opt + 108)[0] == 16, "unexpected directory count")
    directories = [struct.unpack_from("<II", original, opt + 112 + i * 8) for i in range(16)]
    require(directories[4] == (0, 0), "signed image must not be rewritten")
    sections = []
    for i in range(count):
        start = table + i * 40
        name, vsize, rva, rawsize, raw, reloc, lines, nreloc, nlines, flags = struct.unpack_from("<8sIIIIIIHHI", original, start)
        require(not (reloc or lines or nreloc or nlines), "unexpected COFF section records")
        require(raw >= headers and raw % file_align == 0 and rawsize % file_align == 0,
                "unaligned raw section")
        require(raw + rawsize <= len(original) and rva % align == 0, "section outside image")
        sections.append(dict(name=name.rstrip(b"\0"), vsize=vsize, rva=rva,
                             rawsize=rawsize, raw=raw, flags=flags, start=start))
    names = [s["name"] for s in sections]
    require(len(names) == len(set(names)), "duplicate section names")
    marker_indices = [i for i, s in enumerate(sections) if s["name"] == b".buildid"]
    if not marker_indices:
        require(directories[6] == (0, 0) and struct.unpack_from("<I", original, pe + 8)[0] == 0,
                "missing marker but unexpected timestamp/debug data")
        return original
    require(len(marker_indices) == 1 and marker_indices[0] > 0, "unexpected build marker count")
    index = marker_indices[0]
    marker, previous = sections[index], sections[index - 1]
    require(previous["name"] == b".rdata" and previous["flags"] == marker["flags"] == 0x40000040,
            "marker must follow read-only .rdata")
    require((marker["vsize"], marker["rawsize"]) == (28, 512), "marker contains additional data")
    require(directories[6] == (marker["rva"], 28), "unexpected debug directory")
    record = struct.unpack_from("<IIHHIIII", original, marker["raw"])
    require(record[0] == 0 and record[2:] == (0, 0, 16, 0, 0, 0), "expected empty REPRO record only")
    require(record[1] == struct.unpack_from("<I", original, pe + 8)[0], "inconsistent build hashes")
    require(original[marker["raw"] + 28:marker["raw"] + 512] == bytes(484), "nonempty marker padding")
    for a, b in zip(sections, sections[1:]):
        end = a["rva"] + max(a["vsize"], a["rawsize"])
        require((end + align - 1) // align * align == b["rva"], "nonadjacent virtual sections")
        require(a["raw"] + a["rawsize"] == b["raw"], "noncontiguous file sections")
    require(sections[-1]["raw"] + sections[-1]["rawsize"] == len(original), "unexpected file overlay")
    for i, (rva, size) in enumerate(directories):
        if i != 6 and size:
            require(rva + size <= marker["rva"] or rva >= marker["rva"] + align,
                    "another directory references the marker page")
    # Retain virtual addresses. Extend the previous section's zero-filled tail
    # across the now-unused page so Windows still sees adjacent sections.
    start, size = marker["raw"], marker["rawsize"]
    result = bytearray(original[:start] + original[start + size:])
    retained = sections[:index] + sections[index + 1:]
    for i, section in enumerate(retained):
        header = bytearray(original[section["start"]:section["start"] + 40])
        if section is previous:
            struct.pack_into("<I", header, 8, marker["rva"] + 28 - section["rva"])
        if section["raw"] > start:
            struct.pack_into("<I", header, 20, section["raw"] - size)
        result[table + i * 40:table + (i + 1) * 40] = header
    result[table + len(retained) * 40:table + count * 40] = bytes(40)
    struct.pack_into("<H", result, pe + 6, len(retained))
    struct.pack_into("<I", result, pe + 8, 0)  # Unspecified; not a fabricated build date.
    initialized, = struct.unpack_from("<I", original, opt + 8)
    require(initialized >= size, "invalid initialized-data size")
    struct.pack_into("<I", result, opt + 8, initialized - size)
    struct.pack_into("<II", result, opt + 112 + 6 * 8, 0, 0)
    # Check every retained payload and loaded address before writing anything.
    for i, section in enumerate(retained):
        rva, rawsize, raw = struct.unpack_from("<III", result, table + i * 40 + 12)
        require((rva, rawsize) == (section["rva"], section["rawsize"]), "changed loaded section")
        require(result[raw:raw + rawsize] == original[section["raw"]:section["raw"] + rawsize],
                "changed section payload")
    return bytes(result)


def normalize_file(path):
    path = Path(path)
    original = path.read_bytes()
    result = normalize(original)
    if result != original:
        path.write_bytes(result)

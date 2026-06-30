"""Minimal AmigaDOS Hunk (LoadSeg) loader for the Route-A static-recompile work.

Parses a HUNK_HEADER executable into segments, assigns each segment a base
address in a flat 24-bit-ish address space, and applies 32-bit relocations so
that absolute cross-hunk pointers resolve to real linear addresses. This gives
us a single contiguous memory image we can both analyze and (later) execute
against.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

HUNK_CODE = 1001
HUNK_DATA = 1002
HUNK_BSS = 1003
HUNK_RELOC32 = 1004
HUNK_SYMBOL = 1008
HUNK_DEBUG = 1009
HUNK_END = 1010
HUNK_HEADER = 1011
HUNK_RELOC32SHORT = 1020

TYPE_NAMES = {HUNK_CODE: "code", HUNK_DATA: "data", HUNK_BSS: "bss"}


def be32(d: bytes, o: int) -> int:
    return int.from_bytes(d[o:o + 4], "big")


def be16(d: bytes, o: int) -> int:
    return int.from_bytes(d[o:o + 2], "big")


@dataclass
class Segment:
    index: int
    kind: int            # HUNK_CODE/DATA/BSS
    size_bytes: int      # allocated size (BSS has no file data but real size)
    data: bytearray      # length == size_bytes (BSS zero-filled)
    relocs: list[tuple[int, int]] = field(default_factory=list)  # (src_off, target_hunk)
    base: int = 0        # assigned linear base address


@dataclass
class Program:
    path: Path
    segments: list[Segment]
    image: bytearray          # full flat memory image
    image_base: int           # linear base of first segment
    entry: int                # entry linear address (start of first code hunk)

    def seg_for(self, addr: int) -> Segment | None:
        for s in self.segments:
            if s.base <= addr < s.base + s.size_bytes:
                return s
        return None


def _skip_strings(d: bytes, o: int) -> int:
    while True:
        n = be32(d, o); o += 4
        if n == 0:
            return o
        o += n * 4


def load(path: Path, image_base: int = 0x00021000, align: int = 8) -> Program:
    path = Path(path)
    d = path.read_bytes()
    o = 0
    if be32(d, o) != HUNK_HEADER:
        raise ValueError(f"{path.name}: not a HUNK_HEADER executable")
    o += 4
    o = _skip_strings(d, o)              # resident library names (usually empty)
    table_size = be32(d, o); o += 4
    first = be32(d, o); o += 4
    last = be32(d, o); o += 4
    sizes = [be32(d, o + 4 * i) for i in range(table_size)]
    o += 4 * table_size

    segments: list[Segment] = []
    cur: Segment | None = None
    while o + 4 <= len(d):
        ht = be32(d, o) & 0x0FFFFFFF
        o += 4
        if ht in (HUNK_CODE, HUNK_DATA):
            n = be32(d, o); o += 4
            nbytes = n * 4
            cur = Segment(len(segments), ht, nbytes, bytearray(d[o:o + nbytes]))
            segments.append(cur)
            o += nbytes
        elif ht == HUNK_BSS:
            n = be32(d, o); o += 4
            nbytes = n * 4
            cur = Segment(len(segments), ht, nbytes, bytearray(nbytes))
            segments.append(cur)
        elif ht == HUNK_RELOC32:
            assert cur is not None
            while True:
                cnt = be32(d, o); o += 4
                if cnt == 0:
                    break
                th = be32(d, o); o += 4
                for _ in range(cnt):
                    cur.relocs.append((be32(d, o), th)); o += 4
        elif ht == HUNK_RELOC32SHORT:
            assert cur is not None
            while True:
                cnt = be16(d, o); o += 2
                if cnt == 0:
                    if o % 4:
                        o += 2
                    break
                th = be16(d, o); o += 2
                for _ in range(cnt):
                    cur.relocs.append((be16(d, o), th)); o += 2
                if o % 4:
                    o += 2
        elif ht == HUNK_SYMBOL:
            while True:
                n = be32(d, o); o += 4
                if n == 0:
                    break
                o += n * 4 + 4
        elif ht == HUNK_DEBUG:
            n = be32(d, o); o += 4 + n * 4
        elif ht == HUNK_END:
            cur = None
        else:
            raise ValueError(f"{path.name}: unknown hunk block {ht:#x} at {o-4:#x}")

    # assign bases
    addr = image_base
    for s in segments:
        s.base = addr
        sz = s.size_bytes
        sz = (sz + align - 1) & ~(align - 1)
        addr += sz
    image_end = addr
    image = bytearray(image_end - image_base)
    for s in segments:
        image[s.base - image_base: s.base - image_base + s.size_bytes] = s.data

    # apply relocations into the flat image
    for s in segments:
        for src_off, th in s.relocs:
            if th >= len(segments):
                continue
            target_base = segments[th].base
            pos = s.base - image_base + src_off
            addend = be32(image, pos)
            be = (target_base + addend) & 0xFFFFFFFF
            image[pos:pos + 4] = be.to_bytes(4, "big")

    entry = segments[0].base if segments else image_base
    return Program(path, segments, image, image_base, entry)


if __name__ == "__main__":
    import sys
    for arg in sys.argv[1:]:
        p = load(Path(arg))
        print(f"{p.path.name}: {len(p.segments)} segments, image {len(p.image)} bytes @ {p.image_base:#x}")
        for s in p.segments:
            print(f"  [{s.index}] {TYPE_NAMES.get(s.kind,'?'):4} base={s.base:#08x} size={s.size_bytes:#x} relocs={len(s.relocs)}")

#!/usr/bin/env python3
"""Check public PE metadata cleanup against unprocessed compiler outputs.

Uses installed pefile as an independent parser/oracle. --before contains the
three original public EXEs; all test copies are written to a NEW --output.
Does not run an engine or change the original files.
"""
import argparse
from pathlib import Path
import struct
import sys
import pefile

RECOMP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RECOMP))
from pe_metadata import normalize
from release_gate import ARTIFACTS, inspect_pe


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--before', required=True, type=Path)
    ap.add_argument('--output', required=True, type=Path)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    for name in (n for n in ARTIFACTS if n.endswith('.exe')):
        original = (args.before / name).read_bytes()
        cleaned = normalize(original)
        a, b = pefile.PE(data=original), pefile.PE(data=cleaned)
        assert not b.get_warnings(), b.get_warnings()
        assert b.FILE_HEADER.TimeDateStamp == 0 and not getattr(b, 'DIRECTORY_ENTRY_DEBUG', [])
        assert normalize(cleaned) == cleaned, 'Cleanup is not idempotent'
        marker, = [s for s in a.sections if s.Name.rstrip(b'\0') == b'.buildid']
        assert all(s.Name.rstrip(b'\0') != b'.buildid' for s in b.sections)
        mapped_a, mapped_b = bytearray(a.get_memory_mapped_image()), bytearray(b.get_memory_mapped_image())
        mapped_a[:a.OPTIONAL_HEADER.SizeOfHeaders] = mapped_b[:b.OPTIONAL_HEADER.SizeOfHeaders]
        mapped_a[marker.VirtualAddress:marker.VirtualAddress + 28] = bytes(28)
        assert mapped_a == mapped_b, 'Loaded code, data, resources or zero-filled memory changed'
        for left, right in zip([s for s in a.sections if s is not marker], b.sections):
            assert left.Name == right.Name and left.Characteristics == right.Characteristics
            assert left.VirtualAddress == right.VirtualAddress
            assert left.get_data() == right.get_data()
        for index in range(16):
            if index != 6:
                assert a.OPTIONAL_HEADER.DATA_DIRECTORY[index].__pack__() == b.OPTIONAL_HEADER.DATA_DIRECTORY[index].__pack__()
        for field in ('ImageBase', 'AddressOfEntryPoint', 'DllCharacteristics', 'Subsystem',
                      'SizeOfImage', 'SizeOfStackReserve', 'SizeOfHeapReserve'):
            assert getattr(a.OPTIONAL_HEADER, field) == getattr(b.OPTIONAL_HEADER, field)
        for block in getattr(a, 'DIRECTORY_ENTRY_BASERELOC', []):
            for entry in block.entries:
                assert not marker.VirtualAddress <= entry.rva < marker.VirtualAddress + 4096
                if entry.type == 10:
                    target = a.get_qword_at_rva(entry.rva) - a.OPTIONAL_HEADER.ImageBase
                    assert not marker.VirtualAddress <= target < marker.VirtualAddress + 4096
        dest = args.output / name
        dest.write_bytes(cleaned)
        inspect_pe(dest)
        # Fail closed on additional debug data, unexpected layout and signatures.
        mutations = [
            ('debug type', marker.PointerToRawData + 12, '<I', 2),
            ('debug payload', marker.PointerToRawData + 16, '<I', 32),
            ('nonempty padding', marker.PointerToRawData + 28, '<B', 1),
            ('executable marker', marker.get_field_absolute_offset('Characteristics'), '<I', 0x60000020),
            ('unaligned raw section', marker.get_field_absolute_offset('PointerToRawData'), '<I', marker.PointerToRawData + 1),
            ('signed image', a.OPTIONAL_HEADER.DATA_DIRECTORY[4].get_file_offset(), '<I', 1024),
            ('nonzero checksum', a.OPTIONAL_HEADER.get_field_absolute_offset('CheckSum'), '<I', 1),
        ]
        for label, offset, fmt, value in mutations:
            mutated = bytearray(original)
            struct.pack_into(fmt, mutated, offset, value)
            try:
                normalize(mutated)
            except ValueError:
                pass
            else:
                raise AssertionError(f'Accepted {label}: {name}')
        # The package gate prevents an unnormalized rebuild returning silently.
        try:
            inspect_pe(args.before / name)
        except ValueError:
            pass
        else:
            raise AssertionError(f'Gate accepted original timestamp/marker: {name}')
        print(f'PASS {name}: independent loaded-image equality, permissions, idempotence, seven rejection guards, package gate')


if __name__ == '__main__':
    main()

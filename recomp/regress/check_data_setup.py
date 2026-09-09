#!/usr/bin/env python3
"""Exercise startup data diagnostics with isolated disk copies and scratch logs.

Windows permission checks deny only file creation in this test's private data
directory, and remove that temporary ACL in a finally block. Operator files
are read only. No game data is included in the repository.
"""
import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MODULES = ('nb', 'program', 'mog', 'crystal')
ADF_BYTES = 901120


def put32(data, offset, value):
    struct.pack_into('>I', data, offset, value & 0xffffffff)


def test_disk(files=()):
    """Small OFS directory in a full-size image, with one data block per file."""
    data = bytearray(ADF_BYTES)
    data[:4] = b'DOS\0'
    root = 880 * 512
    put32(data, root, 2)
    put32(data, root + 508, 1)
    for i, name in enumerate(files):
        header, block = (900 + i * 2) * 512, (901 + i * 2) * 512
        put32(data, root + 24 + i * 4, header // 512)
        put32(data, header, 2)
        put32(data, header + 508, -3)
        data[header + 432] = len(name)
        data[header + 433:header + 433 + len(name)] = name.encode()
        put32(data, header + 324, 4)
        put32(data, header + 16, block // 512)
        put32(data, block, 8)
        put32(data, block + 12, 4)
        data[block + 24:block + 28] = b'test'
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=ROOT / 'recomp/build/moonstone.exe')
    ap.add_argument('--host-probe-exe', type=Path)
    args = ap.parse_args()
    exe = args.exe.resolve()
    source = ROOT / 'dist/MoonstoneNative/data'
    disks = []
    for i in range(1, 4):
        candidates = [source / f'Moonstone - A Hard Days Knight_Disk{i}.adf', source / f'Disk{i}.adf']
        disk = next(p for p in candidates if p.is_file())
        assert disk.stat().st_size == ADF_BYTES
        disks.append(disk)
    original_hashes = {p: hashlib.sha256(p.read_bytes()).digest() for p in disks}
    passed = 0
    with tempfile.TemporaryDirectory(prefix='moonstone-data-setup-') as temp:
        scratch = Path(temp)

        def prepare(name, missing=(), synthetic=False):
            data = scratch / name / 'data'
            data.mkdir(parents=True)
            for i, disk in enumerate(disks, 1):
                if synthetic:
                    (data / f'Disk{i}.adf').write_bytes(test_disk())
                else:
                    shutil.copyfile(disk, data / f'Disk{i}.adf')
            for module in MODULES:
                if module not in missing:
                    shutil.copyfile(source / module, data / module)
            return data

        def run(data, expected, exit_code=1, alternate=None, extra=()):
            nonlocal passed
            log = data.parent / 'run.log'
            cmd = [str(alternate or exe), '--os', '--dataset', str(data), '--diskdir', str(data),
                   '--frames', '2', '--log', str(log), *extra]
            result = subprocess.run(cmd, cwd=data.parent, capture_output=True, timeout=15)
            report = log.read_text(errors='replace')
            assert result.returncode == exit_code, (data.parent.name, result.returncode, report)
            if os.name == 'nt':
                host = sys.getwindowsversion()
                assert f'version={host.major}.{host.minor}; build={host.build}' in report
                assert 'PROCESS: x64, 64-bit; platform=Windows' in report
                assert 'OS ARCH:' in report and '64-bit; source=' in report
                assert report.index('OS ARCH:') < report.index('SETUP RESULT:'), 'OS information must survive setup failure'
            for marker in expected:
                assert marker in report, (data.parent.name, marker, report)
            assert 'SETUP RESULT: ' + ('ready' if exit_code == 0 else 'failed') in report
            if exit_code:
                assert 'SETUP ERROR:' in report
                assert str(data).replace('\\', '/') in report.replace('\\', '/')
            passed += 1
            print('PASS:', data.parent.name, flush=True)
            return report

        data = prepare('fresh-extraction', missing=MODULES)
        run(data, ['write access confirmed', 'fnv1a32=', '901120 bytes'], 0)
        for module in MODULES:
            assert (data / module).read_bytes() == (source / module).read_bytes()

        install = scratch / 'no-data-install'
        install.mkdir()
        shutil.copyfile(exe, install / 'moonstone.exe')
        shutil.copyfile(exe.parent / 'SDL2.dll', install / 'SDL2.dll')
        log = install / 'run.log'
        result = subprocess.run([str(install / 'moonstone.exe'), '--os', '--frames', '2', '--log', str(log)],
                                cwd=install, capture_output=True, timeout=15)
        report = log.read_text(errors='replace').replace('\\', '/')
        assert result.returncode == 1 and 'Cannot open Disk 1 for reading' in report
        assert 'dataset=' + install.as_posix() + '/data' in report
        assert '../portable' not in report
        passed += 1
        print('PASS: missing data folder reports the executable-relative location', flush=True)

        data = prepare('missing-disk')
        (data / 'Disk2.adf').unlink()
        run(data, ['Cannot open Disk 2 for reading', 'Disk2.adf', 'errno='])

        data = prepare('incorrect-size')
        (data / 'Disk3.adf').write_bytes(b'DOS\0')
        run(data, ['Disk 3 has 4 bytes; expected exactly 901120', 'Disk3.adf'])

        data = prepare('missing-startup-files', missing=('nb', 'crystal'), synthetic=True)
        run(data, ["file 'nb' is absent", "file 'crystal' is absent", "Required startup file 'nb'", 'fnv1a32='])
        assert not (data / 'nb').exists() and not (data / 'crystal').exists()

        for name, signature, expected in [('not-amigados', b'PK\x03\x04', 'no AmigaDOS signature'),
                                           ('ffs-disk', b'DOS\x01', 'unsupported FFS filesystem')]:
            data = prepare(name, missing=('nb',), synthetic=True)
            disk = test_disk()
            disk[:4] = signature
            (data / 'Disk1.adf').write_bytes(disk)
            run(data, [expected, 'Disk1.adf'])

        for name, mutate, expected in [
            ('invalid-root', lambda d: put32(d, 880 * 512, 0), 'invalid OFS root directory'),
            ('directory-cycle', lambda d: (put32(d, 900 * 512 + 496, 900), d.__setitem__(900 * 512 + 433, ord('x'))), 'cyclic or repeated directory'),
            ('incomplete-module', lambda d: put32(d, 900 * 512 + 324, 8), 'recovered 4 of 8 bytes'),
            ('cyclic-module', lambda d: (put32(d, 900 * 512 + 324, 8), put32(d, 901 * 512 + 16, 901)), 'incomplete or cyclic file chain'),
            ('bad-block-length', lambda d: put32(d, 901 * 512 + 12, 489), 'invalid OFS data block'),
        ]:
            data = prepare(name, missing=('nb',), synthetic=True)
            disk = test_disk(('nb',))
            mutate(disk)
            (data / 'Disk1.adf').write_bytes(disk)
            run(data, [expected])
            assert not (data / 'nb').exists(), 'Invalid extraction left a cached module'

        data = prepare('empty-existing-module')
        (data / 'nb').write_bytes(b'')
        run(data, ['Startup file is empty or unreadable', 'Existing file was preserved'])
        assert (data / 'nb').read_bytes() == b''

        if os.name == 'nt':
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                           ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
            kernel.CreateFileW.restype = ctypes.c_void_p
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            for name, locked in [('disk-read-denied', 'Disk1.adf'), ('module-read-denied', 'nb')]:
                data = prepare(name)
                handle = kernel.CreateFileW(str(data / locked), 0x80000000, 0, None, 3, 0, None)
                assert handle not in (None, ctypes.c_void_p(-1).value)
                try:
                    run(data, ['Cannot open Disk 1 for reading' if locked.endswith('.adf') else 'Cannot read startup file', 'errno=13'])
                finally:
                    kernel.CloseHandle(handle)

            for name, missing, exit_code in [('folder-write-denied', ('nb',), 1),
                                              ('cached-readonly-folder', (), 0)]:
                data = prepare(name, missing=missing)
                # WD denies file creation here, without denying read or cleanup.
                subprocess.run(['icacls', str(data), '/deny', '*S-1-1-0:(WD)'], check=True, capture_output=True)
                try:
                    expected = ['Cannot create startup file', 'errno=13', 'Check write access'] if missing else ['SETUP MODULE: readable']
                    run(data, expected, exit_code)
                    if missing:
                        assert not (data / 'nb').exists()
                finally:
                    subprocess.run(['icacls', str(data), '/remove:d', '*S-1-1-0'], check=True, capture_output=True)

        if args.host_probe_exe:
            for failure in ('write', 'close'):
                data = prepare('failed-output-' + failure, missing=('nb',))
                run(data, ['Cannot finish writing startup file', 'Check free space'],
                    alternate=args.host_probe_exe.resolve(), extra=('--probe-data-failure', failure))
                assert not (data / 'nb').exists(), 'Failed write left a cached module'

    assert all(hashlib.sha256(p.read_bytes()).digest() == digest for p, digest in original_hashes.items())
    print(f'All {passed} startup-data diagnostic checks passed; source disks unchanged.')


if __name__ == '__main__':
    main()

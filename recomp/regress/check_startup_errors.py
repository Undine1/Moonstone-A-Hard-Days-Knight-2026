#!/usr/bin/env python3
"""Verify console-free startup failures against the actual Windows executable.

Only dismisses dialogs owned by each test's child process. Game data is read
from the configured install; invalid inputs and every log/output use a fresh
temporary directory. The operator's saves and log are never written.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, default=ROOT / "recomp/build/moonstone.exe")
    ap.add_argument("--data", type=Path, default=ROOT / "dist/MoonstoneNative/data")
    ap.add_argument("--host-probe-exe", type=Path, help="Optional test-only host_error_probe executable")
    args = ap.parse_args()
    if os.name != "nt":
        ap.error("Native dialog checks require Windows")
    exe, data = args.exe.resolve(), args.data.resolve()
    binary = exe.read_bytes()
    pe = struct.unpack_from("<I", binary, 60)[0]
    assert struct.unpack_from("<H", binary, pe + 92)[0] == 2, "Executable must use GUI subsystem"

    ui = c.WinDLL("user32", use_last_error=True)
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    ui.EnumWindows.argtypes = [callback, w.LPARAM]
    ui.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
    ui.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    ui.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    ui.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    ui.IsWindowVisible.argtypes = [w.HWND]
    ui.IsWindowEnabled.argtypes = [w.HWND]
    ui.GetWindow.argtypes = [w.HWND, w.UINT]
    ui.GetWindow.restype = w.HWND
    ui.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
    ui.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]

    def text(hwnd, class_name=False):
        buf = c.create_unicode_buffer(8192)
        (ui.GetClassNameW if class_name else ui.GetWindowTextW)(hwnd, buf, len(buf))
        return buf.value

    def windows(pid):
        found = []

        @callback
        def visit(hwnd, unused):
            owner = w.DWORD()
            ui.GetWindowThreadProcessId(hwnd, c.byref(owner))
            if owner.value == pid and ui.IsWindowVisible(hwnd):
                found.append(hwnd)
            return True

        ui.EnumWindows(visit, 0)
        return found

    def dialogs(pid):
        found = []
        for hwnd in windows(pid):
            if text(hwnd, True) == "#32770":
                labels, buttons = [], []

                @callback
                def child(control, unused):
                    if text(control, True) == "Static":
                        labels.append(text(control))
                    elif text(control, True) == "Button":
                        buttons.append(control)
                    return True

                ui.EnumChildWindows(hwnd, child, 0)
                found.append((hwnd, "\n".join(labels), buttons))
        return found

    def check_dialog_owner(pid, dialog, title):
        game = next((hwnd for hwnd in windows(pid) if text(hwnd) == title), None)
        assert game, "Missing game window during warning"
        assert ui.GetWindow(dialog, 4) == game, "Warning must be owned by the game window"  # GW_OWNER
        assert not ui.IsWindowEnabled(game), "Game window must be disabled while warning is open"
        # Raising the game used to cover the warning and leave play apparently frozen.
        assert ui.SetWindowPos(game, None, 0, 0, 0, 0, 0x13), "Could not raise test game window"
        time.sleep(0.05)  # HWND_TOP, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        order = windows(pid)
        assert order.index(dialog) < order.index(game), "Warning can be covered by the game window"

    with tempfile.TemporaryDirectory(prefix="moonstone-startup-") as scratch:
        tmp = Path(scratch)
        bad = tmp / "damaged-module.bin"
        bad.write_bytes(b"\x01\x02\x03\x04")
        absent = tmp / "missing-module.bin"
        no_dir = tmp / "does-not-exist"
        base = [str(exe), "--os", "--dataset", str(data), "--diskdir", str(data), "--mod", str(data / "nb")]

        passed = 0

        def run_case(name, extra, expected, reason=None, live=True, env=None, missing_log=False, exit_code=1, log_hint=True, alternate_exe=None, owner_title=None):
            nonlocal passed
            log = no_dir / "unwritable.log" if missing_log else tmp / (name + ".log")
            cmd = base + ["--log", str(log)] + (["--sdl"] if live else []) + extra
            if alternate_exe:
                cmd[0] = str(alternate_exe.resolve())
            child_env = dict(os.environ)
            child_env.update(env or {})
            # File redirection prevents pipe backpressure on diagnostic runs.
            with (tmp / (name + ".stdout")).open("w+b") as out, (tmp / (name + ".stderr")).open("w+b") as err:
                proc = subprocess.Popen(cmd, stdout=out, stderr=err, env=child_env, cwd=tmp)
                seen = []
                dismissed = set()
                deadline = time.monotonic() + 12
                try:
                    while proc.poll() is None and time.monotonic() < deadline:
                        current = dialogs(proc.pid)
                        dismissed.intersection_update((hwnd, body) for hwnd, body, _ in current)
                        for hwnd, body, buttons in current:
                            key = (hwnd, body)
                            if key in dismissed:
                                continue
                            i = len(seen)
                            assert i < len(expected), f"{name}: unexpected dialog: {body}"
                            assert expected[i] in body, f"{name}: wrong dialog: {body}"
                            if missing_log:
                                assert "No diagnostic log could be written" in body
                            else:
                                if log_hint:
                                    assert str(log) in body, f"{name}: wrong log path: {body}"
                                if reason and i == len(expected) - 1:
                                    assert reason in log.read_text(errors="replace"), "Reason must be flushed before dismissal"
                            assert buttons, f"{name}: dialog has no dismissal button"
                            if owner_title:
                                check_dialog_owner(proc.pid, hwnd, owner_title)
                            seen.append(body)
                            dismissed.add(key)
                            assert ui.PostMessageW(buttons[0], 0x00F5, 0, 0), "Could not dismiss test dialog"  # BM_CLICK
                        time.sleep(0.02)
                    assert proc.poll() is not None, f"{name}: process did not exit after dismissal"
                    assert proc.returncode == exit_code, f"{name}: exit={proc.returncode}"
                    assert len(seen) == len(expected), f"{name}: expected {len(expected)} dialogs, saw {len(seen)}"
                    err.seek(0)
                    stderr = err.read().decode(errors="replace")
                    if reason:
                        assert reason in stderr, f"{name}: missing redirected error"
                        if not missing_log:
                            assert reason in log.read_text(errors="replace"), f"{name}: missing logged reason"
                    out.seek(0)
                    stdout = out.read().decode(errors="replace")
                finally:
                    if proc.poll() is None:
                        proc.kill()
                    proc.wait()
            print(f"PASS: {name}", flush=True)
            passed += 1
            return stdout

        startup = "couldn't load its startup data"
        saved = "couldn't load the saved game"
        recording = "couldn't create the audio recording"
        run_case("missing-disk-dialog", ["--diskdir", str(no_dir)], ["Moonstone needs your three original"], "ADF disk images", log_hint=False)
        run_case("corrupt-module-dialog", ["--mod", str(bad)], [startup], "not HUNK_HEADER")
        run_case("missing-module-dialog", ["--mod", str(absent)], [startup], "cannot open")
        run_case("corrupt-save-dialog", ["--loadstate", str(bad)], [saved], "load_state rejected")
        run_case("missing-save-dialog", ["--loadstate", str(absent)], [saved], "load_state rejected")
        run_case("log-unavailable-dialog", ["--mod", str(bad)], ["couldn't create its diagnostic log", startup], "not HUNK_HEADER", missing_log=True)
        run_case("recording-unavailable-dialog", ["--wav", str(no_dir / "audio.wav"), "--mod", str(bad)], [recording, startup], "not HUNK_HEADER")
        run_case("sdl-init-dialog", [], ["Moonstone couldn't start"], "SDL_Init:", env={"SDL_VIDEODRIVER": "moonstone-test-invalid"})
        run_case("sdl-renderer-dialog", [], ["Moonstone couldn't start"], "SDL_CreateRenderer:", env={"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"})
        run_case("headless-corrupt-module", ["--mod", str(bad)], [], "not HUNK_HEADER", live=False)
        run_case("headless-corrupt-save", ["--loadstate", str(bad)], [], "load_state rejected", live=False)
        run_case("headless-warm-save", ["--frames", "2", "--loadstate-at", "1", str(bad)], [], "load_state rejected", live=False)
        run_case("headless-missing-log", ["--frames", "2"], [], "couldn't create its diagnostic log", live=False, missing_log=True, exit_code=0)
        run_case("headless-missing-recording", ["--frames", "2", "--wav", str(no_dir / "audio.wav")], [], recording, live=False, exit_code=0)
        version = run_case("version-stdout", ["--version"], [], live=False, exit_code=0)
        assert "2026 native port" in version
        output = run_case("diagnostic-stdout", ["--frames", "2", "--ftime", "--disasm", "0x21000", "2", "--dump", str(tmp / "frame.ppm"), "--dumpram", str(tmp / "ram.bin"), "--wav", str(tmp / "audio.wav")], [], live=False, exit_code=0)
        for marker in ("ftime:", "dumped frame", "dumped ram", "021000:", "audio:", "done:"):
            assert marker in output, f"Missing diagnostic stdout: {marker}"
        for name in ("frame.ppm", "ram.bin", "audio.wav"):
            assert (tmp / name).stat().st_size > 44
        if args.host_probe_exe:
            run_case("audio-device-warning", [], ["couldn't open an audio device"], "Injected audio-open failure", exit_code=0, env={"SDL_AUDIODRIVER": "dummy"}, alternate_exe=args.host_probe_exe)
            run_case("f12-recording-warning", ["--probe-record-dir", str(no_dir)], [recording], "No such file or directory", exit_code=0, alternate_exe=args.host_probe_exe, owner_title="Moonstone recording warning ownership test")
            probe_log = (tmp / "f12-recording-warning.log").read_text()
            assert "Probe SDL warning routed to file" in probe_log
            assert "Probe game window re-enabled; recording retry succeeded" in probe_log
            assert (no_dir / "capture-1.wav").stat().st_size == 44
    print(f"All {passed} console-free startup/diagnostic checks passed.")


if __name__ == "__main__":
    main()

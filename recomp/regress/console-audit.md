# Console-removal audit (2026-09-05)

Scope: the executable's complete `src/` tree, its linked Musashi translation
units/configuration, SDL's log callback, and build/package entry points.
This is a host/UI change; guest CPU, graphics, audio synthesis, and save formats
are unchanged.

| Former console use | Current handling / verification |
| --- | --- |
| Controller startup, connection, removal | File log plus four-second overlay. Virtual-pad checks cover initial connection, unused-pad removal, failover, held-input release, and 32 rapid reconnect cycles. |
| Hunk loader failure | Caller receives the exact parser/I/O reason; native startup dialog names the file. Reason is flushed to the selected log before dismissal; redirected stderr remains available. Missing and corrupt modules tested. |
| Missing disks / setup failures | Native dialog names the failing file and reason, plus the actual log path. Logs distinguish missing/read-denied files, incorrect byte counts, unsupported filesystems, missing modules, damaged file chains, and create/write/close failures. An actual denied folder write is checked in both headless and native-dialog tests. |
| Failed `--loadstate` / `--loadstate-at` | Logged error and redirected stderr. Live startup also shows a native dialog and exits with failure. Missing/corrupt cold saves and corrupt warm saves tested. |
| Failed `--wav` output | Warning dialog during live startup, detailed log, and stderr. Existing continue-without-recording behavior retained. |
| Log-open failure / stderr fallback | Visible live-startup warning; explains that no diagnostic log is available. Continues without a log. Does not claim that a file was written. |
| SDL startup / renderer errors | Native error dialog and log. Invalid video driver and failed accelerated renderer tested against the real executable. |
| Audio-device open failure | Warning dialog and log; continues without sound. Tested by substituting only the device-open call in a separate probe executable. |
| SDL internal messages | `SDL_LogSetOutputFunction` routes diagnostics to the file. Probe confirms delivery. |
| `printf` version, hashes, timing, disassembly, dumps, audio summary, exit summary | Intentional diagnostic-command output, retained for redirection/pipes. Version, timing, disassembly, file dumps, audio capture, and stdout verified; game harness also consumes the hash output. |
| Four developer memory/blitter watches using `g_log ? g_log : stderr` | Normally write to the initialized log. Unavailable-log fallback is covered by the startup warning. |
| Musashi `68040: unhandled PFLUSH` stderr | Inactive: this build selects a 68000 and disables 020+/040/PMMU emulation. Vendored code unchanged. |
| Build/package shell output and standalone RE tools | Intentional developer console programs; not part of normal game startup. |
| Console input / waits / console allocation | No active runtime stdin reader, terminal prompt, or console-allocation dependency found. |

Adjacent feedback checked: F5/F9 already report results in the window title;
host crashes/core stops already show dialogs. Those dialogs now reference the
actual `--log` path and acknowledge an unavailable log. F12 recording-open
failure previously only wrote to the log; it now also shows a warning, verified
through the real `toggle_record` function in the test probe. Warnings/errors use
the game window as their owner once it exists. The F12 probe creates a real
window and verifies native ownership, disabled game input while the warning is
open, and that raising the game cannot cover the warning. After dismissal it
checks the game window is enabled again and a recording retry succeeds.

## Repeatable checks

From the repository root:

```powershell
python recomp/regress/check.py --exe C:/Users/Ins/Desktop/vscode/moonstone/recomp/build/moonstone.exe
python recomp/regress/check_startup_errors.py
```

The startup suite has 17 checks against the actual executable, including native
dialogs. It creates temporary bad inputs/logs, dismisses only dialogs belonging
to its own child processes, checks logs before dismissal, and verifies exit
codes. It never writes to the operator's log or saves.

For the additional audio-open and F12 warning checks, build the test-only probe
from `recomp/` using the existing compiler, then run from the repository root:

```powershell
$compiler = './tools/zig-x86_64-windows-0.16.0/zig.exe'
$probeSources = @(
  'regress/host_error_probe.c', 'src/loader.c',
  'vendor/Musashi-master/m68kcpu.c', 'vendor/Musashi-master/m68kops.c',
  'vendor/Musashi-master/m68kdasm.c', 'vendor/Musashi-master/softfloat/softfloat.c'
)
& $compiler cc -O2 -g -std=c11 '-Wl,--subsystem,windows' `
  -Ivendor/Musashi-master -Ivendor/Musashi-master/softfloat -Isrc -Ivendor/SDL2/include `
  -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-unused-function -Wno-date-time `
  @probeSources vendor/SDL2/lib/libSDL2.dll.a -o build/host_error_probe.exe
```

```powershell
python recomp/regress/check_startup_errors.py --host-probe-exe recomp/build/host_error_probe.exe
```

This runs 19 checks. Never package the probe executable; the normal build and
release scripts compile only the production source.

`python recomp/regress/check_data_setup.py --host-probe-exe recomp/build/host_error_probe.exe`
runs 19 additional data/extraction checks. Disk-write permission tests apply an
ACL only to private temporary directories and restore it in a finally block.
The probe's write/close failures simulate full-disk errors through the real
extraction path and check that partial outputs are removed. No fault injection
is compiled into the production executable.

C compilation/typechecking and the 13-row game harness are required alongside
these checks. ESLint was attempted, but this C repository has no ESLint config.

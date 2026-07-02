#!/usr/bin/env python3
"""Moonstone Reborn — regression check.

Runs a manifest of golden states against the built engine and flags any drift,
so a fix that quietly comes undone is caught here in seconds instead of mid-playthrough.

Two kinds of golden:
  ramfnv   - cold-boot the engine to frame N, hash all of RAM (FNV-1a). Catches
             ANY change to engine/game behaviour (a stray write, a logic change).
  framesha - load a save, render frame N, SHA-256 the pixels. Catches DISPLAY
             regressions (e.g. an outro/ending screen rendering garbled).

Only the hashes live in the repo (manifest.tsv). The saves / .adf game data the
goldens reference are local copyrighted content and are NOT committed.

The engine log is redirected into a throwaway temp dir, so the player's
dist/MoonstoneNative/moonstone.log is NEVER touched.

Usage:
  bash recomp/build.sh && python recomp/regress/check.py
  python recomp/regress/check.py --exe dist/MoonstoneNative/moonstone.exe
  python recomp/regress/check.py --update     # re-baseline goldens (after an INTENDED change)
"""
import argparse, hashlib, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
def at(*p): return os.path.join(ROOT, *p)

ap = argparse.ArgumentParser()
ap.add_argument("--exe",      default=at("recomp", "build", "moonstone.exe"))
ap.add_argument("--data",     default=at("dist", "MoonstoneNative", "data"))
ap.add_argument("--saves",    default=at("dist", "MoonstoneNative"))
ap.add_argument("--manifest", default=os.path.join(HERE, "manifest.tsv"))
ap.add_argument("--update",   action="store_true", help="rewrite goldens with current values")
ap.add_argument("--no-audio", action="store_true", help="skip the live SDL audio-cushion check (no game window)")
args = ap.parse_args()

if not os.path.exists(args.exe):
    sys.exit(f"FAIL: engine not built: {args.exe}\n  build first: bash recomp/build.sh")
if not os.path.isdir(args.data):
    sys.exit(f"FAIL: game data not found: {args.data}\n  (local copyrighted data; not in the repo)")

tmp = tempfile.mkdtemp(prefix="moonregress_")

def run(extra):
    cmd = [args.exe, "--mod", os.path.join(args.data, "nb"),
           "--diskdir", args.data, "--dataset", args.data,
           "--log", os.path.join(tmp, "engine.log")] + extra
    return subprocess.run(cmd, capture_output=True, text=True)

def sha16(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(65536), b""):
            h.update(b)
    return h.hexdigest()[:16]

def measure(kind, inp, frame):
    if kind == "ramfnv":
        # input "cold" = legacy bare boot (no --os, default 600 frames -- keeps the
        # original golden); input "os" = full HLE boot, runs the real attract.  A
        # frame past 600 extends the run (the long attract golden exists because a
        # hook misfire at frames 2000-3000 was once invisible to the 500-frame one).
        extra = (["--os"] if inp == "os" else []) + ["--frames", str(max(600, int(frame)))]
        r = run(extra + ["--savestate-at", str(frame), os.path.join(tmp, "d.sav")])
        m = re.search(r"ram_fnv=([0-9a-f]+)", r.stdout)
        return m.group(1) if m else "ERR:no-hash"
    if kind == "savefnv":
        # loadstate a frozen fixture save, run <frame> frames with full HLE, hash RAM.
        # Catches behavioural drift in GAMEPLAY paths (combat AI, sound protocol,
        # hooks) that the cold-boot goldens never execute.
        save = os.path.join(args.saves, inp)
        if not os.path.exists(save):
            return "ERR:no-save"
        r = run(["--os", "--loadstate", save, "--frames", str(frame)])
        m = re.search(r"ram_fnv=([0-9a-f]+)", r.stdout)
        return m.group(1) if m else "ERR:no-hash"
    if kind == "framesha":
        save = os.path.join(args.saves, inp)
        if not os.path.exists(save):
            return "ERR:no-save"
        pre = os.path.join(tmp, "f")
        run(["--loadstate", save, "--dumpevery", "9999", "--dump", pre])
        ppm = f"{pre}_{int(frame):04d}.ppm"
        return sha16(ppm) if os.path.exists(ppm) else "ERR:no-frame"
    return "ERR:bad-kind"

def check_audio():
    """Live SDL audio-cushion check — the one thing the golden hashes CANNOT see:
    audio_reprime and the stall-resync top-up only execute with a real audio device
    (headless runs never open one, never tick the SDL queue).  Boots the game
    windowed, auto-skips the intro at frame 250 and simulates a ~0.5 s window-drag
    stall at frame 600 (--skipat / --stallat), then asserts from the scratch log
    that the cushion was primed at boot, re-primed at BOTH clear/stall events, and
    never chronically starved (a regressed build logs hundreds of AQ-UNDERRUN
    lines; a small tolerance absorbs one-off host hiccups).  Takes ~35 s and a game
    window appears + closes by itself; --no-audio skips it.  SKIPs cleanly when no
    audio device exists."""
    logp = os.path.join(tmp, "audio.log")
    cmd = [args.exe, "--sdl", "--os", "--mod", os.path.join(args.data, "nb"),
           "--diskdir", args.data, "--dataset", args.data,
           "--skipat", "250", "--stallat", "600", "--log", logp]
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        p.wait(timeout=34)              # the live loop never exits by itself...
    except subprocess.TimeoutExpired:
        p.kill(); p.wait()              # ...so the kill IS the normal path
    try:
        log = open(logp, encoding="utf-8", errors="replace").read()
    except OSError:
        return ("FAIL", "no engine log written")
    if "AUDIO-DEV" not in log:
        return ("SKIP", "no audio device on this host")
    reprimes  = log.count("AQ-REPRIME")
    underruns = log.count("AQ-UNDERRUN")
    if "AQ-PRIME" not in log:      return ("FAIL", "boot prime missing")
    if "AUDIO-UNPAUSE" not in log: return ("FAIL", "device never unpaused (load/skip path broken?)")
    if reprimes < 2:               return ("FAIL", f"expected >=2 AQ-REPRIME (skip + stall), got {reprimes}")
    if underruns > 10:             return ("FAIL", f"{underruns} AQ-UNDERRUN lines (cushion not holding)")
    return ("PASS", f"reprimes={reprimes} underruns={underruns}")

rows, fails, out = [], 0, []
for raw in open(args.manifest, encoding="utf-8"):
    line = raw.rstrip("\n")
    if not line.strip() or line.lstrip().startswith("#"):
        out.append(raw.rstrip("\n")); rows.append(None); continue
    name, kind, inp, frame, golden = [c.strip() for c in line.split("\t")]
    got = measure(kind, inp, frame)
    ok = (got == golden) and not got.startswith("ERR")
    if args.update and not got.startswith("ERR"):
        golden = got; ok = True
    rows.append((name, kind, inp, frame, golden))
    out.append(f"{name}\t{kind}\t{inp}\t{frame}\t{golden}")
    if not ok:
        fails += 1
    tag = "PASS" if ok else "FAIL"
    print(f"  [{tag}] {name:14} {kind:8} got={got:18} golden={golden}")

audio_ran = False
if not args.update and not args.no_audio:
    tag, detail = check_audio()
    audio_ran = (tag != "SKIP")
    print(f"  [{tag}] {'audio-cushion':14} {'livelog':8} {detail}")
    if tag == "FAIL":
        fails += 1

shutil.rmtree(tmp, ignore_errors=True)

if args.update:
    with open(args.manifest, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("\nmanifest re-baselined.")
    sys.exit(0)

total = sum(1 for r in rows if r) + (1 if audio_ran else 0)
if fails:
    print(f"\n{fails}/{total} REGRESSED. If the change was intentional, eyeball it then: --update")
    sys.exit(1)
print(f"\nall {total} checks passed.")
sys.exit(0)

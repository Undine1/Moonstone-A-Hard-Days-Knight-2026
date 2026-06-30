#!/usr/bin/env bash
# Build Moonstone (native) and assemble a self-contained, double-click-playable
# distributable in dist/MoonstoneNative/.  Reproducible: run from anywhere.
#
#   dist/MoonstoneNative/
#     moonstone.exe          the native game (Musashi 68000 + OCS chipset model)
#     SDL2.dll               window / input / audio
#     Play Moonstone.cmd     convenience launcher (just runs moonstone.exe)
#     README.txt             what it is, how to run, controls
#     data/                  bundled original disk data:
#       nb, program, mog, crystal               boot modules (HLE-loaded by name)
#       Moonstone ... Disk1/2/3.adf             the 3 floppy images (trackdisk)
#
# The exe resolves data/ relative to its OWN location, so the folder runs from
# any path (and the seamless disk-swap means the player never sees a swap prompt).
set -e
cd "$(dirname "$0")"               # recomp/
ROOT=".."                          # repo root
DATASET="../portable/moonstone_hdd"
OUT="$ROOT/dist/MoonstoneNative"

echo "[1/4] building moonstone.exe ..."
bash build.sh moonstone

echo "[2/4] assembling $OUT ..."
# Overwrite in place — do NOT 'rm -rf' the folder: if moonstone.exe is currently
# running, Windows locks it; an rm -rf would delete the rest of the folder then
# abort (set -e) before repopulating, gutting the dist. Copying over a locked exe
# just warns; the support files always stay intact.
mkdir -p "$OUT/data"
cp -f build/moonstone.exe "$OUT/" || echo "  (warning: moonstone.exe busy/running — close the game and re-run to update the exe)"
cp -f build/SDL2.dll       "$OUT/" 2>/dev/null || cp -f vendor/SDL2/bin/SDL2.dll "$OUT/"

# boot modules served by name from the dataset (small)
for f in nb program mog crystal; do
  cp -f "$DATASET/$f" "$OUT/data/$f"
done
# the three floppy images (the real game data; served by the trackdisk model)
for n in 1 2 3; do
  cp -f "$ROOT/Moonstone - A Hard Days Knight_Disk$n.adf" "$OUT/data/"
done

echo "[3/4] writing launcher + README ..."
cat > "$OUT/Play Moonstone.cmd" <<'CMD'
@echo off
rem Launch the native Moonstone (no emulator).  Data is read from .\data\
start "" "%~dp0moonstone.exe"
CMD
cp -f "$ROOT/recomp/dist_README.txt" "$OUT/README.txt"

echo "[4/4] done.  Distributable contents:"
( cd "$OUT" && find . -maxdepth 2 -type f | sort )
echo
echo "Run by double-clicking '$OUT/moonstone.exe' (or 'Play Moonstone.cmd')."

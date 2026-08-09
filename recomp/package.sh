#!/usr/bin/env bash
# Build Moonstone and assemble a clean runtime-only release folder.
#
# Original game data is deliberately NEVER copied. Players place their own
# Disk1.adf, Disk2.adf, and Disk3.adf in data/ after extracting the release.
set -euo pipefail
cd "$(dirname "$0")"

ROOT=".."
OUT="$ROOT/dist/release-staging/Moonstone-Reborn-Windows-x64"

echo "[1/3] building moonstone.exe ..."
bash build.sh moonstone

echo "[2/3] assembling clean runtime-only folder ..."
if [ -e "$OUT" ]; then
  echo "refusing to reuse existing staging folder: $OUT" >&2
  echo "move or remove that generated folder, then run this script again" >&2
  exit 1
fi

mkdir -p "$OUT/data"
cp build/moonstone.exe "$OUT/"
cp build/SDL2.dll "$OUT/"
cp dist_README.txt "$OUT/README.txt"
cp "$ROOT/LICENSE" "$OUT/LICENSE.txt"
cp "$ROOT/THIRD-PARTY-NOTICES.txt" "$OUT/"
cp PLACE_ADF_FILES_HERE.txt "$OUT/data/"

echo "[3/3] release folder ready: $OUT"
echo "Contents:"
( cd "$OUT" && find . -maxdepth 2 -type f | sort )
echo
echo "No ADFs, extracted game modules, saves, logs, or source files were copied."
echo "Compress the Moonstone-Reborn-Windows-x64 folder as Moonstone-Reborn-Windows-x64.zip."

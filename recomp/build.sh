#!/usr/bin/env bash
# Build the Moonstone native harness with zig cc (no MSVC needed).
set -e
cd "$(dirname "$0")"
ZIG="./tools/zig-x86_64-windows-0.16.0/zig.exe"
MUS="vendor/Musashi-master"

# regenerate Musashi opcode tables if missing
if [ ! -f "$MUS/m68kops.c" ]; then
  "$ZIG" cc -O2 -o "$MUS/m68kmake.exe" "$MUS/m68kmake.c"
  ( cd "$MUS" && ./m68kmake.exe )
fi

TARGET="${1:-moonstone}"
SDL="vendor/SDL2"
mkdir -p build

# Embed the application icon (Explorer/taskbar/window) if present.  zig rc compiles
# the .rc (referencing icon/moonstone.ico) to a COFF .res that the linker bakes in.
RES=""
if [ -f icon/app.rc ] && [ -f icon/moonstone.ico ]; then
  ( cd icon && "../$ZIG" rc app.rc app.res )
  RES="icon/app.res"
fi

"$ZIG" cc -O2 -g -std=c11 \
  -I"$MUS" -I"$MUS/softfloat" -Isrc -I"$SDL/include" \
  -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-unused-function \
  -Wno-date-time \
  src/moon.c src/loader.c \
  "$MUS/m68kcpu.c" "$MUS/m68kops.c" "$MUS/m68kdasm.c" "$MUS/softfloat/softfloat.c" \
  "$SDL/lib/libSDL2.dll.a" \
  $RES \
  -o "build/$TARGET.exe"
cp -f "$SDL/bin/SDL2.dll" build/ 2>/dev/null || true
echo "built build/$TARGET.exe (+ SDL2.dll)"

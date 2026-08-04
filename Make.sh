#!/bin/bash
# Build script for the TinyJoypad -> Vircon32 port.
# Assumes compile, assemble, png2vircon, wav2vircon and packrom are on
# PATH (same layout as this author's other Vircon32 ports).

abort_build()
{
    echo
    echo BUILD FAILED
    exit 1
}

# create obj and bin folders if non existing, since the
# development tools will not create them themselves
mkdir -p obj
mkdir -p bin

echo
echo Compile the C code
echo --------------------------
compile src/main.c -o obj/main.asm || abort_build

echo
echo Optimize the ASM code - v32opt, optional
echo --------------------------
# v32opt (https://github.com/wedge1020/v32opt) is a third-party,
# optional post-compile assembly optimizer - not part of the official
# Vircon32 toolchain, and not required to build this project. Skipped
# automatically if not found on PATH, or if SKIP_V32OPT=1 is set
# (e.g. `SKIP_V32OPT=1 ./Make.sh`) - either way, the unoptimized
# assembly from compile.exe is used directly, same as if this step
# didn't exist.
ASM_TO_ASSEMBLE=obj/main.asm
if [ "$SKIP_V32OPT" = "1" ]; then
    echo SKIP_V32OPT=1 set - skipping, using unoptimized assembly
elif command -v v32opt >/dev/null 2>&1; then
    v32opt obj/main.asm obj/main_opt.asm -v -O3 && ASM_TO_ASSEMBLE=obj/main_opt.asm
else
    echo v32opt not found on PATH - skipping, using unoptimized assembly
fi

echo
echo Assemble the ASM code
echo --------------------------
assemble "$ASM_TO_ASSEMBLE" -o obj/main.vbin || abort_build

echo
echo Convert the PNG textures
echo --------------------------
png2vircon assets/columns.png -o obj/columns.vtex || abort_build
png2vircon assets/thumbnails.png -o obj/thumbnails.vtex || abort_build
png2vircon assets/thumbnails2.png -o obj/thumbnails2.vtex || abort_build
png2vircon assets/pixelgrid.png -o obj/pixelgrid.vtex || abort_build

echo
echo Convert the PlayNote wavetable
echo --------------------------
wav2vircon "libs/PlayNote/sounds/wt_saw.wav" -o "obj/wt_saw.vsnd" || abort_build

echo
echo Pack the ROM
echo --------------------------
packrom rom.xml -o "bin/tinyjoypad.v32" || abort_build

echo
echo BUILD SUCCESSFUL

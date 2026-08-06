@echo off
REM Build script for the TinyJoypad -> Vircon32 port.
REM Assumes compile.exe, assemble.exe, png2vircon.exe, wav2vircon.exe and
REM packrom.exe are on PATH (same layout as this author's other Vircon32
REM ports, e.g. crisp-game-lib-portable_vircon32).

REM create obj and bin folders if non existing, since
REM the development tools will not create them themselves
if not exist obj mkdir obj
if not exist bin mkdir bin

echo.
echo Compile the C code
echo --------------------------
compile src\main.c -o obj\main.asm || goto :failed

echo.
echo Optimize the ASM code - v32opt, optional
echo --------------------------
REM v32opt (https://github.com/wedge1020/v32opt) is a third-party,
REM optional post-compile assembly optimizer - not part of the official
REM Vircon32 toolchain, and not required to build this project. Skipped
REM automatically if not found on PATH, or if SKIP_V32OPT is set to 1
REM (e.g. `set SKIP_V32OPT=1` before running this script) - either way,
REM the unoptimized assembly from compile.exe is used directly, same as
REM if this step didn't exist.
set ASM_TO_ASSEMBLE=obj\main.asm
if "%SKIP_V32OPT%"=="1" (
    echo SKIP_V32OPT=1 set - skipping, using unoptimized assembly
) else (
    where v32opt >nul 2>nul
    if %errorlevel% equ 0 (
        v32opt obj\main.asm obj\main_opt.asm -v -O3 && set ASM_TO_ASSEMBLE=obj\main_opt.asm
    ) else (
        echo v32opt not found on PATH - skipping, using unoptimized assembly
    )
)

echo.
echo Assemble the ASM code
echo --------------------------
assemble %ASM_TO_ASSEMBLE% -o obj\main.vbin || goto :failed

echo.
echo Convert the PNG textures
echo --------------------------
png2vircon assets\columns.png -o obj\columns.vtex || goto :failed
png2vircon assets\thumbnails.png -o obj\thumbnails.vtex || goto :failed
png2vircon assets\thumbnails2.png -o obj\thumbnails2.vtex || goto :failed
png2vircon assets\pixelgrid.png -o obj\pixelgrid.vtex || goto :failed
png2vircon assets\thumbnails3.png -o obj\thumbnails3.vtex || goto :failed

echo.
echo Convert the PlayNote wavetable
echo --------------------------
wav2vircon libs\PlayNote\sounds\wt_saw.wav -o obj\wt_saw.vsnd || goto :failed

echo.
echo Pack the ROM
echo --------------------------
packrom rom.xml -o "bin\tinyjoypad.v32" || goto :failed

goto :succeeded

:failed
echo.
echo BUILD FAILED
exit /b %errorlevel%

:succeeded
echo.
echo BUILD SUCCESSFUL
exit /b

@echo on

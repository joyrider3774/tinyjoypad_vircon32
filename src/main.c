// =============================================================================
// TinyJoypad -> Vircon32 port - master build file
// =============================================================================
// Vircon32 has no linker: a program is exactly one translation unit, built
// from a single .c file passed to compile.exe (everything else must be
// pulled in via #include, full implementations and all - see
// VIRCON32_C_DIALECT.md section 11). This file is that single entry point:
// it #includes every module in dependency order, then portVircon32.c last
// (which defines the machine-dependent hooks and main()).
//
// Build: compile.exe main.c -o main.asm && assemble.exe main.asm -o
// main.vbin && packrom.exe rom.xml -o tinyjoypad.v32 (see Make.bat/Make.sh
// at the project root for the exact commands and folder layout).
// =============================================================================

#include "misc.h"
#include "math.h"

#include "machineDependent.h"
#include "menu.h"
#include "menu.c"
#include "menuGameList.h"

#include "avrCompat.h"
#include "tinyJoypadShim.h"
#include "tinyJoypadShim.c"
#include "obonoCoreShim.h"
#include "obonoCoreShim.c"

// ---- Ported games (add more #includes here as you port them) ----
#include "games/gameNumberPlace.c"
#include "games/gameTinyInvaders.c"
#include "games/gameT2048.c"
#include "games/gameHollowSeeker.c"
#include "games/gameTinyPinball.c"
#include "games/gameTinyPacman.c"
#include "games/gameTinyBomber.c"
#include "games/gameTinyDoc.c"
#include "games/gameTinyBert.c"
#include "games/gameTinyTris.c"
#include "games/gameTinyArkanoid.c"
#include "games/gameTinyTrick.c"
#include "games/gameTinyMinez.c"
#include "games/gameTinyMissile.c"
#include "games/gameTinyBike.c"
#include "games/gameTinyArena.c"
#include "games/gameTinyGilbert.c"
#include "games/gameTinyPipe.c"
#include "games/gameTinyMorpion.c"
#include "games/gameTinyPlaque.c"
#include "games/gameTinySQuest.c"
#include "games/gameTinyDDug.c"
#include "games/gameTinyLander.c"
#include "games/gameWrenRollercoaster.c"
#include "games/gameFrogger.c"
#include "games/gamePong.c"
#include "games/gameStacker.c"
#include "games/gameUFO.c"
#include "games/gameTinyDungeon.c"
#include "games/gameOroboros.c"
#include "games/gameRunDudeRun.c"
#include "games/gameFourInRow.c"
#include "games/gameDinoGame.c"
#include "games/gameSnakeGame85.c"
#include "games/gameJumpSlime.c"
#include "games/gameTinyRoG.c"
#include "games/gameTinYFi.c"
#include "games/gameBreakout.c"
#include "games/gameSpaceAttack.c"
#include "games/gameFallingBlocks.c"
#include "games/gameTinyMania.c"
#include "games/gameBlocksGold.c"

#include "menuGameList.c"

// ---- Vircon32-specific machine-dependent layer + main() ----
#include "portVircon32.c"

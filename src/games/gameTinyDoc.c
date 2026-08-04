// =============================================================================
// Tiny Doc - ported from Daniel C's Tiny-Doc.ino (tinyjoypad.com, GPLv3).
// Same tinyJoypadShim lineage as Tiny Invaders/Pinball/Pacman/Bomber
// (FastTinyDriver.h) - button reads reuse that shim as-is. A Dr. Mario-style
// falling-pill puzzle game: two-cell "pills" fall into an 8x10 grid, matching
// runs of >=4 same-colored cells (pill halves or viruses) clear, and clearing
// every virus advances the level.
//
// Button mapping: same ELECTROLIB.h analogRead(A0)/(A3)+digitalRead(1)
// pattern as gameTinyPinball.c/gameTinyBomber.c - TINYJOYPAD_LEFT/RIGHT off
// A0, TINYJOYPAD_DOWN off A3 (no UP used - this game only needs left/right/
// soft-drop), BUTTON_DOWN off digitalRead(1) (rotate). All map directly onto
// isLeftPressed()/isRightPressed()/isDownPressed()/isFirePressed().
//
// Structural changes from upstream:
//  - upstream's loop() is NEWGAME:/NextLevel_TD: labels around nested
//    while(1)s - rewritten as an explicit frame-stepped state machine, same
//    approach as every other tinyJoypadShim port here.
//  - the line-clear/gravity cascade (`do { CheckCompletedLine_TD(); } while
//    (DropPills_TD());`, plus ClearLine_TD()'s own 6-frame clear-animation
//    loop) was a *blocking* multi-frame sequence upstream (each iteration
//    calls TinyFlip_TD()+FPS_Count_TD() itself). Rewritten as an explicit
//    `tdResolveState` sub-state machine (SCAN -> CLEAR_ANIM -> DROP -> loop
//    back to SCAN if anything moved, else resume normal play) that advances
//    one step per real engine frame instead of looping to completion inside
//    a single frame.
//  - dropped the real-time `_delay_ms()`/`FPS_Count_TD()` frame limiter
//    throughout - an early revision of this port just relied on the
//    engine's own 60fps `end_frame()` pacing instead, but a later frame-
//    pacing session (see CLAUDE.md) added a real tick-skip throttle
//    (`TD_TICK_DIVISOR`) so this file's own update() actually reproduces
//    upstream's real ~30fps rate again, same as `FPS_Count_TD()` did. The
//    two meaningful (non-limiter) delays - a ~1000ms pause after a level
//    first appears, and a ~2000ms pause after game over - became explicit
//    `tdWaitFrames` counts, halved (30/60) when the throttle above was
//    added so both keep measuring the same real-world duration they
//    always did, not double it.
//  - `uint8_t` is aliased to plain `int` via avrCompat.h (as for every other
//    port), which breaks one *intentional* upstream trick: `DropPills_TD()`
//    counts `for (uint8_t y = 8; y < 255; y--)`, relying on `y` underflowing
//    from 0 to 255 to end the loop. A real (non-wrapping) `int` would make
//    that condition true forever. Rewritten as a plain signed
//    `for (int y = 8; y >= 0; y--)`.
//  - GCC's `case N ... M:` case-range extension (used by
//    `ReturnScanLineX_TD()`'s column lookup and `SwitchRecupVirus_TD()`) and
//    `switch` itself aren't used anywhere else in this project's ports, so
//    rewritten as plain `if`/`else if` chains rather than risk relying on
//    dialect support that's never been exercised before.
//  - `rand() % n` -> the shared `arand(n)` helper (avrCompat.h), same fix as
//    every other port here.
//  - `SND_TDOC_TD()`'s two real-time tone-sweep cases (a 13-iteration two-
//    tone loop for level-clear, a ~190-iteration descending sweep for game
//    over) are approximated with a handful of representative `Sound()`
//    calls, same simplification already used for Tiny Invaders/Bomber's own
//    busy-wait sound effects.
//  - render loop (`tdTinyFlip`) was written from the start with the row/x-
//    range gating lesson learned from this session's Tiny Invaders/Bomber
//    CPU-load optimization pass (each of the 7 draw layers only ever
//    applies to a narrow, already-known x/y sub-range - gated in the outer
//    loop instead of calling every layer unconditionally on all 1024
//    pixels/frame).
// =============================================================================

#define TD_HORIZONTAL 0
#define TD_VERTICAL   1

#define TD_XPOS_TP 46
#define TD_YPOS_TP 13

struct TdPill
{
    int active;
    int inTab;
    int graphPosX;
    int graphPosY;
    int gridx;
    int gridy;
    int vertical;
    int pillPart1PreviewStat;
    int pillPart2PreviewStat;
    int pillPart1Stat;
    int pillPart2Stat;
};

struct TdElement
{
    int clr;
    int scanHV;
    int elementCount;
    int elementStart;
    int elementStop;
    int elementType;
};

struct TdGameP
{
    int speed;
    int tabRange;
    int level;
    int totalVirusInLevel;
};

TdPill tdPill;
TdElement tdElement;
TdGameP tdGameP;

int tdFullRefresh;
int tdGameOver;
int tdTimerDrop;
int tdStepRight;
int tdStepLeft;
int tdAnimPos;
int tdPillMode;
int tdFrmVirus;
int tdAnimSpeedVirus;
int tdAutoTrigL;
int tdAutoTrigR;
int[3] tdVirusTypeActif;
int[8][10] tdTab;
int[8][10] tdBackCheck;

int tdLastTotalVirusLeft;
int tdTotalVirusLeft;

int tdLvlDisplay1;
int tdLvlDisplay10;
int tdVDisplay1;
int tdVDisplay10;

int tdScore;
int tdM10000;
int tdM1000;
int tdM100;
int tdM10;
int tdM1;

#define TD_STATE_ATTRACT            0
#define TD_STATE_LEVEL_INTRO_WAIT   1
#define TD_STATE_PLAYING            2
#define TD_STATE_LEVEL_CLEARED_WAIT 3
#define TD_STATE_GAMEOVER_WAIT      4

// Upstream (Tiny-Doc.ino's FPS_Count_TD(33) main-loop throttle) ran its
// whole logic+redraw loop at a fixed real ~30fps - this port reproduces
// that by only actually ticking once every TD_TICK_DIVISOR real Vircon32
// frames (see gameTinyDoc_update()). Every small tick-counted timer ported
// verbatim from upstream (tdAutoTrigL/R's movement auto-repeat delay,
// tdAnimSpeedVirus's idle-wiggle animation) was tuned against upstream's
// own genuine 30fps tick, so throttling to match needs no rescaling of
// those thresholds - only the tick rate itself needed fixing.
#define TD_TICK_DIVISOR 2

#define TD_RESOLVE_NONE       0
#define TD_RESOLVE_SCAN       1
#define TD_RESOLVE_CLEAR_ANIM 2
#define TD_RESOLVE_DROP       3

int tdState;
int tdResolveState;
int tdWaitFrames;
int tdClearAnimFrame;
int tdAttractBlk;
bool tdPrevFire;
int tdTickSkipCounter = 0;

// The locked-grid composite (tdCompositeTabIntoBuffer(), see its own
// comment further down) is expensive in proportion to how many cells are
// filled, but the grid itself is static on almost every frame - it only
// actually changes when a pill locks, a line-clear/drop cascade is
// running, or a new level is set up. Every *other* frame (most frames - a
// pill just falling/moving, or the grid sitting untouched) was still
// recomputing the entire composite from scratch, for every locked cell,
// every frame - exactly the "grid is almost full -> slows down a lot"
// cost the user reported.
//
// `tdPageRowDirty[y]` (one flag per page row, 1..7 used) is set wherever a
// grid-mutating function runs; tdTinyFlip() only recomputes a given page
// row's composite (tdCompositeTabIntoBuffer(y)) when that row's own flag is
// set, reusing the cached `tdTabCache` (a plain array read, no
// blitzSprite calls at all) otherwise. Most mutation sites
// (tdSetSinglePill/tdInitRnd/tdDropPills/tdInitPublicVarForNewLevel, the
// inline clear-animation/virus-anim ticks in gameTinyDoc_update()) touch an
// unpredictable/whole-grid range and mark every row dirty via
// tdMarkAllRowsDirty() - safe, same cost as before. tdFixPill() - by far
// the single most frequent mutation during real play, one call per pill
// lock - writes exactly 1-2 known grid cells, so it marks only the
// specific page row(s) that actually cover them via tdMarkGridRowDirty(),
// leaving the other 5-6 rows' cache untouched. This mattered in practice:
// under a stress test with the board pre-filled near-full and pills
// locking rapidly, CPU load was pegged at 100% almost continuously (each
// lock forcing a full 7-page-row recompute over the dense board) even
// though the existing whole-grid dirty flag was already working exactly
// as designed - narrowing the invalidation scope to the row(s) that
// actually changed is what cuts the sustained cost, not a bug fix.
bool[8] tdPageRowDirty;
int[1024] tdTabCache;

void tdReturnScanLineY( int* a, int* b, int y )
{
    if( y == 1 ) { *a = 0; *b = 1; }
    else if( y == 2 ) { *a = 0; *b = 2; }
    else if( y == 3 ) { *a = 2; *b = 3; }
    else if( y == 4 ) { *a = 4; *b = 5; }
    else if( y == 5 ) { *a = 5; *b = 6; }
    else if( y == 6 ) { *a = 7; *b = 8; }
    else if( y == 7 ) { *a = 8; *b = 9; }
}

void tdMarkAllRowsDirty()
{
    int y;
    for( y = 1; y <= 7; y++ )
      tdPageRowDirty[ y ] = true;
}

// Marks only the page row(s) whose grid-row range (tdReturnScanLineY()
// above) actually includes grid row gy, instead of invalidating every
// page row's cache for a change that only ever touches 1-2 of them.
void tdMarkGridRowDirty( int gy )
{
    int y, aa, bb;
    for( y = 1; y <= 7; y++ )
    {
        tdReturnScanLineY( &aa, &bb, y );
        if( gy >= aa && gy <= bb )
          tdPageRowDirty[ y ] = true;
    }
}

int[5] tdConnectionCheck =
{
0x00,0x02,0x01,0x04,0x03,
};

// -----------------------------------------------------------------------------
//   SpriteBank.h data (programmatically extracted from upstream, byte-for-
//   byte, to avoid the kind of manual-transcription slip found in Bomber's
//   own data tables earlier this session)
// -----------------------------------------------------------------------------

int[60] tdLevelCleared =
{
0x1D,0x02,0xFF,0x01,0x01,0x01,0x01,0x01,0x01,0x7D,0x41,0x01,0x7D,0x55,0x01,0x7D,0x41,0x3D,0x01,0x7D,
0x55,0x01,0x7D,0x41,0x01,0x01,0x01,0x01,0x01,0x01,0xFF,0xFF,0x80,0x80,0xBE,0xA2,0x80,0xBE,0xA0,0x80,
0xBE,0xAA,0x80,0xBE,0x8A,0xBC,0x80,0xBE,0x8A,0xB4,0x80,0xBE,0xAA,0x80,0xBE,0xA2,0xBC,0x80,0x80,0xFF,
};

int[9] tdSpriteOrder =
{
0x01,0x02,0x03,0x02,0x03,0x01,0x03,0x01,0x02,
};

int[6] tdRbbrvv =
{
0x00,0x02,0x02,0x00,0x01,0x01,
};

int[32] tdPolice =
{
0x03,0x01,0x1F,0x11,0x1F,0x00,0x00,0x1F,0x1D,0x15,0x17,0x11,0x15,0x1F,0x07,0x04,0x1F,0x17,0x15,0x1D,
0x1F,0x15,0x1D,0x01,0x1D,0x03,0x1F,0x15,0x1F,0x17,0x15,0x1F,
};

int[22] tdAnim =
{
0x61,0x12,0x5D,0x0E,0x5A,0x0B,0x56,0x08,0x52,0x06,0x4F,0x05,0x4B,0x05,0x48,0x05,0x44,0x06,0x41,0x08,
0x3D,0x0A,
};

int[65] tdVirusLoupe =
{
0x07,0x01,0x08,0x0E,0x11,0x11,0x11,0x0E,0x08,0x00,0x0E,0x11,0x11,0x11,0x0E,0x00,0x02,0x0E,0x11,0x11,
0x11,0x0E,0x02,0x08,0x0E,0x15,0x1B,0x15,0x0E,0x08,0x00,0x0E,0x15,0x1B,0x15,0x0E,0x00,0x02,0x0E,0x15,
0x1B,0x15,0x0E,0x02,0x08,0x0E,0x1F,0x1F,0x1F,0x0E,0x08,0x00,0x0E,0x1F,0x1F,0x1F,0x0E,0x00,0x02,0x0E,
0x1F,0x1F,0x1F,0x0E,0x02,
};

int[13] tdShadow =
{
0x0B,0x01,0x1E,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x1E,
};

int[14] tdVirus =
{
0x04,0x01,0x08,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x01,
};

int[74] tdPills =
{
0x04,0x01,0x06,0x09,0x09,0x06,0x06,0x0B,0x0D,0x06,0x06,0x0F,0x0F,0x06,0x0F,0x09,0x09,0x06,0x06,0x09,
0x09,0x0F,0x07,0x09,0x09,0x07,0x0E,0x09,0x09,0x0E,0x0F,0x0B,0x0D,0x06,0x06,0x0B,0x0D,0x0F,0x07,0x0D,
0x0B,0x07,0x0E,0x0D,0x0B,0x0E,0x0F,0x0F,0x0F,0x06,0x06,0x0F,0x0F,0x0F,0x07,0x0F,0x0F,0x07,0x0E,0x0F,
0x0F,0x0E,0x05,0x08,0x01,0x0A,0x0A,0x01,0x08,0x05,0x00,0x00,0x00,0x00,
};

int[1024] tdBackground =
{
0xFF,0x33,0xFD,0x0D,0x07,0x07,0x05,0x45,0xA7,0xA7,0x25,0x05,0xCF,0x2B,0x2B,0x2B,0x0B,0xCB,0x2B,0x2B,
0x2B,0xCB,0x0B,0xEB,0xAB,0xAB,0xCF,0x05,0xE7,0xA7,0x25,0x25,0x07,0x07,0x05,0x0D,0xFB,0x03,0xCD,0xCD,
0x33,0x33,0xCD,0xCD,0x33,0x33,0xCD,0xCD,0x33,0x33,0xDF,0xF0,0x60,0xC7,0x88,0x90,0x10,0x10,0xE0,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xE0,0x10,0x10,0x90,0x88,0xC7,0xE0,0xF0,
0x3F,0x33,0xCD,0xCD,0x33,0x33,0xCD,0xCD,0x33,0x33,0xCD,0xCD,0x33,0x33,0xCD,0xCD,0x33,0x33,0xCD,0xCD,
0x33,0x33,0xCD,0xCD,0x33,0x33,0xCD,0xCD,0x33,0x33,0xCD,0xCD,0x33,0x33,0xCD,0xCD,0x33,0x33,0xCD,0xCD,
0x33,0x33,0xCD,0xCD,0x33,0x33,0xCD,0xFF,0xFF,0x33,0xFF,0x00,0x00,0x00,0x00,0x02,0x02,0x02,0x01,0x00,
0x01,0x02,0x02,0x02,0x00,0x01,0x02,0x02,0x02,0x01,0x00,0x03,0x00,0x01,0x02,0x00,0x03,0x02,0x02,0x02,
0x00,0x00,0x00,0x00,0xFF,0x00,0xCC,0xCC,0x33,0xFF,0x06,0x03,0xF1,0x09,0x09,0x09,0x09,0x09,0x09,0x09,
0x09,0x09,0x09,0x08,0x04,0x04,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x03,0x04,0x04,0x08,0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,0x09,0xF1,0x03,0x07,0xFF,0xCC,0xCC,
0x33,0x33,0xEC,0x3C,0x13,0x13,0x1C,0x1C,0x13,0x13,0x1C,0x1C,0x13,0x13,0x1C,0x1C,0x13,0x13,0x9C,0x9C,
0x93,0x93,0x9C,0x1C,0x13,0x13,0x1C,0x1C,0x33,0xE3,0x0C,0xCC,0x33,0x33,0xCC,0xFF,0xFF,0x33,0xCF,0xD8,
0x10,0x10,0xD0,0xD0,0x10,0x10,0xD0,0xD0,0x10,0x10,0xD0,0xD0,0x10,0x10,0xD0,0xD0,0x10,0x10,0xD0,0xD0,
0x10,0x10,0xD0,0xD0,0x10,0x10,0xD0,0xD0,0x10,0x10,0xD0,0xC8,0x2F,0x20,0xCC,0xCC,0x33,0xFF,0x00,0x00,
0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0xFF,0x00,0x00,0xFF,0xCC,0xCC,0x33,0x33,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x80,0x02,0x87,0x3F,0x46,0x84,0x15,0x04,0x04,0x04,0x85,0x8D,0x4A,0x3C,0x18,0x00,0xFF,0x00,0xCC,
0x33,0x33,0xCC,0xFF,0xFF,0x33,0xCC,0xCC,0x33,0x33,0xCC,0xCC,0x33,0x33,0xCC,0xCC,0xB3,0xF3,0x4C,0x4C,
0x33,0x33,0x2C,0x2C,0x33,0x33,0x2C,0x2C,0x73,0x73,0xCC,0xCC,0x33,0x33,0xCC,0xCC,0x33,0x33,0xCC,0xCC,
0x33,0x33,0xCC,0xCC,0x33,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0xCC,0xCC,0x33,0x33,0x7F,0xC0,
0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x81,0x81,0x81,0x80,0x91,0xBF,0xB7,0xBF,0xBF,0xAF,
0x80,0x80,0x80,0x80,0xC0,0x7F,0x00,0xCC,0x33,0x33,0xCC,0xFF,0xFF,0x33,0xCC,0xCC,0x33,0x33,0xCC,0x7C,
0x1B,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x01,0x03,0x06,0x0C,0x33,0xF3,0x4C,0xCC,0x33,0x33,0xCC,0xCC,0x33,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
0x00,0xFF,0xCC,0xCC,0x33,0x33,0xF4,0x0C,0x0A,0x0A,0x0C,0x0C,0x0A,0x0A,0x0C,0x1C,0x16,0x16,0x14,0x14,
0x16,0x16,0x14,0x14,0x16,0x1E,0x0C,0x0C,0x0A,0x0A,0x0C,0x0C,0x0A,0xF2,0x05,0xCC,0x33,0x33,0xCC,0xFF,
0xFF,0x33,0xCC,0xCC,0x33,0x7F,0xC1,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC1,0x3E,0x80,0x33,0x33,0xCC,0xCC,
0x33,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0xCC,0xCC,0x33,0x33,0xFF,0x00,0x00,0x00,0x00,0x00,
0x3E,0x20,0x20,0x00,0x22,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0xFF,0x00,0xCC,0x33,0x33,0xCC,0xFF,0xFF,0x33,0xCC,0x4C,0x33,0x93,0x4C,0x17,0x24,0x48,0x90,0x60,
0x40,0xC0,0x80,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x80,0x40,0x40,0x20,0x10,0xC8,0xC4,
0x33,0x31,0xCC,0xCC,0x33,0x33,0xCC,0xCC,0x33,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0xCC,0xCC,
0x33,0x33,0xFF,0x00,0x00,0x00,0x00,0x00,0x3C,0x40,0x3C,0x00,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0xCC,0x33,0x33,0xCC,0xFF,0xFF,0xA7,0xC8,0xD2,
0x95,0x90,0xC8,0xC4,0xB2,0xB1,0xCC,0xCC,0xB2,0xB2,0xCC,0xCC,0xB1,0xB1,0xCD,0xCD,0xB1,0xB1,0xCD,0xCD,
0xB0,0xB2,0xCC,0xCC,0xB3,0xB3,0xCC,0xCC,0xB3,0xB3,0xCC,0xCC,0xB3,0xB3,0xCC,0xCC,0xB3,0xFF,0x80,0x00,
0x7F,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
0x80,0x80,0x7F,0x00,0x80,0xFF,0xCC,0xCC,0xB3,0xB3,0xC3,0xC4,0xB4,0xB4,0xC4,0xC4,0xB4,0xB4,0xC4,0xC4,
0xB4,0xB4,0xC4,0xC4,0xB4,0xB4,0xC4,0xC4,0xB4,0xB4,0xC4,0xC4,0xB4,0xB4,0xC4,0xC4,0xB4,0xB3,0xC8,0xCC,
0xB3,0xB3,0xCC,0xFF,
};

int[162] tdIntrogame =
{
0x28,0x04,0xC0,0x30,0x0C,0x28,0xE6,0x24,0x06,0xE4,0x06,0xE4,0xC6,0x84,0xE6,0x04,0x66,0xC4,0x66,0x04,
0xFE,0x00,0x02,0xFA,0x1A,0xDA,0xDA,0x3A,0xFA,0x3A,0xDA,0xDA,0x3A,0xFA,0x3A,0xDA,0xDA,0xF4,0xF4,0xC8,
0x30,0xC0,0x03,0x0C,0x30,0x10,0x67,0x20,0x60,0x27,0x60,0x27,0x60,0x20,0x67,0x20,0x60,0x27,0x60,0x20,
0x7F,0x00,0x40,0x5F,0x58,0x5B,0x5B,0x5C,0x5F,0x5C,0x5B,0x5B,0x5C,0x5F,0x5C,0x5B,0x5B,0x2F,0x2F,0x13,
0x0C,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x5C,0x54,0x74,0x00,0x04,0x7C,0x04,0x00,0x7C,0x14,0x7C,0x00,0x7C,0x14,0x6C,0x00,0x04,
0x7C,0x04,0x00,0x00,0x00,0x38,0x44,0x54,0x74,0x00,0x7C,0x14,0x7C,0x00,0x7C,0x08,0x7C,0x00,0x7C,0x54,
0x44,0x00,
};

// -----------------------------------------------------------------------------
//   Generic sprite-blit primitives (ELECTROLIB.cpp equivalents) - these draw
//   at arbitrary pixel positions (not just tile-aligned), splitting the
//   sprite's bytes across two page rows as needed via Decalage shifting.
// -----------------------------------------------------------------------------

int tdMymap( int x, int inMin, int inMax, int outMin, int outMax )
{
    return ( x - inMin ) * ( outMax - outMin ) / ( inMax - inMin ) + outMin;
}

int tdRecupeLineY( int valeur )
{
    return valeur >> 3;
}

int tdRecupeDecalageY( int valeur )
{
    return valeur - ( ( valeur >> 3 ) << 3 );
}

int tdSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown )
      return ( input << decalage ) & 0xFF;
    return input >> ( 8 - decalage );
}

int tdBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[ 0 ];
    int hSprite = sprites[ 1 ];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = tdRecupeLineY( yPos );

    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos ||
        recupeLineY > yPass || ( recupeLineY + hSprite ) < yPass )
      return 0x00;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = tdRecupeDecalageY( yPos );
    int scanA = ( xPass - xPos ) + ( spriteYLine * wSprite ) + 2;
    int scanB = ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;

    int outByte;
    if( scanA > wMax )
      outByte = 0x00;
    else
      outByte = tdSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = tdSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

int tdSpeedBlitz( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[ 0 ];
    int hSprite = sprites[ 1 ];
    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos ||
        yPass < yPos || yPass > ( yPos + ( hSprite - 1 ) ) )
      return 0x00;
    return sprites[ 2 + ( ( xPass - xPos ) + ( yPass - yPos ) * wSprite ) + ( frame * ( hSprite * wSprite ) ) ];
}

// -----------------------------------------------------------------------------
//   Sound (simplified - see header comment)
// -----------------------------------------------------------------------------

void tdSndTdoc( int snd )
{
    if( snd == 0 ) Sound( 200, 1 );
    else if( snd == 1 )
    {
        Sound( 4, 80 ); Sound( 100, 80 );
        Sound( 4, 80 ); Sound( 100, 80 );
        Sound( 4, 80 ); Sound( 100, 80 );
    }
    else if( snd == 2 ) { Sound( 240, 4 ); Sound( 100, 4 ); }
    else if( snd == 3 ) Sound( 2, 1 );
    else if( snd == 4 ) Sound( 10, 12 );
    else if( snd == 5 )
    {
        Sound( 40, 3 ); Sound( 160, 12 );
        Sound( 80, 3 ); Sound( 120, 12 );
        Sound( 120, 3 ); Sound( 80, 12 );
        Sound( 160, 3 ); Sound( 40, 12 );
    }
    else if( snd == 6 ) { Sound( 100, 255 ); Sound( 60, 255 ); }
}

// -----------------------------------------------------------------------------
//   Score / level display
// -----------------------------------------------------------------------------

void tdCompileLvDisplay( int* a10, int* b1, int number )
{
    *a10 = number / 10;
    *b1 = number % 10;
}

void tdFirstCalculeDisplay()
{
    tdScore = tdScore + ( ( tdLastTotalVirusLeft - tdTotalVirusLeft ) * tdMymap( tdGameP.level, 0, 20, 20, 60 ) );
    tdLastTotalVirusLeft = tdTotalVirusLeft;
    tdFullRefresh = 1;

    tdM10000 = tdScore / 10000;
    tdM1000 = ( tdScore / 1000 ) % 10;
    tdM100 = ( tdScore / 100 ) % 10;
    tdM10 = ( tdScore / 10 ) % 10;
    tdM1 = tdScore % 10;

    tdCompileLvDisplay( &tdLvlDisplay10, &tdLvlDisplay1, tdGameP.level );
    tdCompileLvDisplay( &tdVDisplay10, &tdVDisplay1, tdTotalVirusLeft );
}

void tdCompilScore()
{
    if( tdLastTotalVirusLeft != tdTotalVirusLeft )
      tdFirstCalculeDisplay();
}

void tdAdjustGamePlay()
{
    if( tdGameP.level > 20 ) tdGameP.level = 20;
    tdTotalVirusLeft = tdGameP.totalVirusInLevel = ( tdGameP.level * 2 ) + 4;
    tdLastTotalVirusLeft = tdTotalVirusLeft;
    tdGameP.speed = tdMymap( tdGameP.level, 0, 20, 25, 4 );
    tdGameP.tabRange = tdMymap( tdGameP.level, 0, 20, 3, 8 );
}

void tdCountVirusTypes()
{
    tdVirusTypeActif[ 0 ] = 0;
    tdVirusTypeActif[ 1 ] = 0;
    tdVirusTypeActif[ 2 ] = 0;
    int x, y;
    for( y = 0; y < 10; y++ )
      for( x = 0; x < 8; x++ )
      {
          int v = tdTab[ x ][ y ] & 0x0F;
          if( v == 1 ) tdVirusTypeActif[ 0 ]++;
          else if( v == 2 ) tdVirusTypeActif[ 1 ]++;
          else if( v == 3 ) tdVirusTypeActif[ 2 ]++;
      }
    tdTotalVirusLeft = tdVirusTypeActif[ 0 ] + tdVirusTypeActif[ 1 ] + tdVirusTypeActif[ 2 ];
}

// -----------------------------------------------------------------------------
//   Grid / pill logic
// -----------------------------------------------------------------------------

int tdReturnConectionType( int val )
{
    return val >> 4;
}

int tdReturnType( int val )
{
    int skin = val & 0x0F;
    if( skin < 10 ) return skin;
    return skin - 10;
}

int tdCheckMatch( int x, int y )
{
    if( x >= 0 && x <= 7 && y >= 0 && y <= 9 ) return tdTab[ x ][ y ];
    return 0;
}

int tdReturnCombinedPillDirection( int x, int y, int type )
{
    int dir = type >> 4;
    if( dir == 0 ) return 0;
    int dx = 0;
    if( dir == 1 ) dx = -1; else if( dir == 2 ) dx = 1;
    int dy = 0;
    if( dir == 3 ) dy = -1; else if( dir == 4 ) dy = 1;
    if( tdReturnConectionType( tdCheckMatch( x + dx, y + dy ) ) != tdConnectionCheck[ dir ] ) return 1;
    return 0;
}

void tdSetSinglePill()
{
    tdMarkAllRowsDirty();
    int x, y;
    for( y = 0; y < 10; y++ )
      for( x = 0; x < 8; x++ )
        if( tdReturnCombinedPillDirection( x, y, tdTab[ x ][ y ] ) )
          tdTab[ x ][ y ] = tdTab[ x ][ y ] & 0x0F;
}

int tdCheckColision( int x, int y )
{
    if( x < 0 ) return 1;
    if( x > 7 ) return 1;
    if( y > 9 ) return 1;
    if( tdPill.vertical )
    {
        if( tdTab[ x ][ y ] ) return 1;
        if( ( y - 1 ) < 0 ) return 0;
        else if( tdTab[ x ][ y - 1 ] ) return 1;
    }
    else
    {
        if( tdTab[ x ][ y ] ) return 1;
        if( tdTab[ x + 1 ][ y ] || x == 7 ) return 1;
    }
    return 0;
}

int tdCheckRotateIsPosible()
{
    if( tdPill.vertical )
    {
        tdPill.vertical = 0;
        if( tdCheckColision( tdPill.gridx, tdPill.gridy ) )
        {
            if( !tdCheckColision( tdPill.gridx - 1, tdPill.gridy ) )
            {
                tdPill.gridx -= 1;
                tdPill.vertical = 1;
                return 1;
            }
            tdPill.vertical = 1;
            return 0;
        }
        tdPill.vertical = 1;
        return 1;
    }
    else
    {
        tdPill.vertical = 1;
        if( tdCheckColision( tdPill.gridx, tdPill.gridy ) )
        {
            tdPill.vertical = 0;
            return 0;
        }
        tdPill.vertical = 0;
        return 1;
    }
}

void tdSetPicDirection( int vertical )
{
    if( vertical )
    {
        tdPill.pillPart1Stat = ( tdPill.pillPart1Stat & 0x0F ) | ( 3 << 4 );
        tdPill.pillPart2Stat = ( tdPill.pillPart2Stat & 0x0F ) | ( 4 << 4 );
    }
    else
    {
        tdPill.pillPart1Stat = ( tdPill.pillPart1Stat & 0x0F ) | ( 2 << 4 );
        tdPill.pillPart2Stat = ( tdPill.pillPart2Stat & 0x0F ) | ( 1 << 4 );
    }
}

void tdRotatePill()
{
    if( !tdCheckRotateIsPosible() ) return;
    int mem = tdPill.pillPart1Stat;
    if( tdPill.vertical )
      tdPill.vertical = 0;
    else
    {
        tdPill.vertical = 1;
        tdPill.pillPart1Stat = tdPill.pillPart2Stat;
        tdPill.pillPart2Stat = mem;
    }
    tdSndTdoc( 0 );
    tdSetPicDirection( tdPill.vertical );
}

void tdFixPill()
{
    // Writes exactly 1-2 known grid cells (both always on the same grid
    // row, tdPill.gridy, plus gridy-1 for the vertical case) - mark only
    // the page row(s) that actually cover those, not the whole grid.
    if( tdPill.vertical )
    {
        if( ( tdPill.gridy - 1 ) >= 0 )
        {
            tdTab[ tdPill.gridx ][ tdPill.gridy ] = tdPill.pillPart1Stat;
            tdTab[ tdPill.gridx ][ tdPill.gridy - 1 ] = tdPill.pillPart2Stat;
            tdMarkGridRowDirty( tdPill.gridy );
            tdMarkGridRowDirty( tdPill.gridy - 1 );
        }
        else
        {
            tdTab[ tdPill.gridx ][ tdPill.gridy ] = tdPill.pillPart1Stat & 0x0F;
            tdMarkGridRowDirty( tdPill.gridy );
        }
    }
    else
    {
        tdTab[ tdPill.gridx ][ tdPill.gridy ] = tdPill.pillPart1Stat;
        tdTab[ tdPill.gridx + 1 ][ tdPill.gridy ] = tdPill.pillPart2Stat;
        tdMarkGridRowDirty( tdPill.gridy );
    }
    tdPill.active = 0;
    tdSndTdoc( 4 );
}

int tdGenerateSidePill( int side )
{
    int colPill = ( arand( 3 ) + 1 ) + 10;
    int sidePill = side << 4;
    return colPill | sidePill;
}

void tdGeneratenewPreviewPill()
{
    tdPill.pillPart1PreviewStat = tdGenerateSidePill( 2 );
    tdPill.pillPart2PreviewStat = tdGenerateSidePill( 1 );
}

void tdInitNewPill( int act )
{
    tdPill.active = act;
    tdPill.vertical = 0;
    tdPill.inTab = 0;
    tdPill.graphPosX = -100;
    tdPill.graphPosY = -100;
    tdPill.gridx = 3;
    tdPill.gridy = 0;
    tdPill.pillPart1Stat = tdPill.pillPart1PreviewStat;
    tdPill.pillPart2Stat = tdPill.pillPart2PreviewStat;
}

int tdGetElement( int index )
{
    return tdRbbrvv[ index % 6 ];
}

int tdOrderSelect( int select, int chiffre )
{
    return tdSpriteOrder[ ( select * 3 ) + chiffre ];
}

void tdInitRnd()
{
    tdMarkAllRowsDirty();
    int setSelect = arand( 3 );
    int virusCount = 0;
    int x, y;
    while( true )
    {
        x = arand( 8 );
        y = 10 - ( arand( tdGameP.tabRange ) + 1 );
        if( tdTab[ x ][ y ] == 0 )
        {
            tdTab[ x ][ y ] = tdOrderSelect( setSelect, tdGetElement( x + ( y * 8 ) ) );
            virusCount++;
        }
        if( virusCount == tdGameP.totalVirusInLevel ) break;
    }
}

// -----------------------------------------------------------------------------
//   Line-clear scan (CheckCompletedLine -> ElementCounter -> CopyItem2Delete)
// -----------------------------------------------------------------------------

void tdInitNewBackTab()
{
    tdElement.clr = 0;
    tdElement.scanHV = 0;
    tdElement.elementCount = 0;
    tdElement.elementStart = 0;
    tdElement.elementStop = 0;
    tdElement.elementType = 0xFF;
    int x, y;
    for( y = 0; y < 10; y++ )
      for( x = 0; x < 8; x++ )
        tdBackCheck[ x ][ y ] = 0;
}

void tdCopyItem2Delete( int x, int y )
{
    tdElement.clr = 1;
    int start = tdElement.elementStart;
    int stop = tdElement.elementStop + 1;
    if( tdElement.scanHV == TD_HORIZONTAL )
    {
        while( start < stop ) { tdBackCheck[ start ][ y ] = 20; start++; }
    }
    else
    {
        while( start < stop ) { tdBackCheck[ x ][ start ] = 20; start++; }
    }
}

void tdNewStepLine()
{
    tdElement.elementCount = 0;
    tdElement.elementStart = 0;
    tdElement.elementStop = 0;
    tdElement.elementType = 0xFF;
}

void tdElementCounter( int x, int y, int element )
{
    int pos, endLine;
    if( tdElement.scanHV == TD_HORIZONTAL ) { pos = x; endLine = 7; }
    else { pos = y; endLine = 9; }

    if( tdElement.elementType != element || element == 0 )
    {
        if( tdElement.elementCount > 3 ) tdCopyItem2Delete( x, y );
        tdElement.elementType = element;
        tdElement.elementCount = 1;
        tdElement.elementStart = pos;
    }
    else
    {
        tdElement.elementCount++;
        tdElement.elementStop = pos;
        if( pos == endLine && tdElement.elementCount > 3 )
        {
            tdCopyItem2Delete( x, y );
            tdNewStepLine();
        }
    }
}

void tdCheckCompletedLine()
{
    int x, y;
    tdInitNewBackTab();

    tdElement.scanHV = TD_HORIZONTAL;
    for( y = 0; y < 10; y++ )
    {
        tdNewStepLine();
        for( x = 0; x < 8; x++ )
          tdElementCounter( x, y, tdReturnType( tdTab[ x ][ y ] ) );
    }

    tdElement.scanHV = TD_VERTICAL;
    for( x = 0; x < 8; x++ )
    {
        tdNewStepLine();
        for( y = 0; y < 10; y++ )
          tdElementCounter( x, y, tdReturnType( tdTab[ x ][ y ] ) );
    }
}

int tdDropPills()
{
    tdMarkAllRowsDirty();
    int repeat = 0;
    int x, y;
    for( y = 8; y >= 0; y-- )
      for( x = 0; x < 8; x++ )
      {
          if( ( tdTab[ x ][ y ] & 0x0F ) > 4 )
          {
              int connection = tdReturnConectionType( tdTab[ x ][ y ] );
              if( connection == 0 )
              {
                  if( tdTab[ x ][ y + 1 ] == 0 )
                  {
                      tdTab[ x ][ y + 1 ] = tdTab[ x ][ y ];
                      tdTab[ x ][ y ] = 0;
                      repeat = 1;
                  }
              }
              else if( connection == 2 )
              {
                  if( tdTab[ x ][ y + 1 ] == 0 && tdTab[ x + 1 ][ y + 1 ] == 0 )
                  {
                      tdTab[ x ][ y + 1 ] = tdTab[ x ][ y ];
                      tdTab[ x + 1 ][ y + 1 ] = tdTab[ x + 1 ][ y ];
                      tdTab[ x ][ y ] = 0;
                      tdTab[ x + 1 ][ y ] = 0;
                      repeat = 1;
                  }
              }
              else if( connection == 3 )
              {
                  if( tdTab[ x ][ y + 1 ] == 0 )
                  {
                      tdTab[ x ][ y + 1 ] = tdTab[ x ][ y ];
                      tdTab[ x ][ y ] = tdTab[ x ][ y - 1 ];
                      tdTab[ x ][ y - 1 ] = 0;
                      repeat = 1;
                  }
              }
          }
      }
    return repeat;
}

// -----------------------------------------------------------------------------
//   Rendering layers
// -----------------------------------------------------------------------------

int tdReturncorectPills( int val )
{
    int valLow = val & 0x0F;
    int valHigh = val >> 4;
    if( valHigh == 0 )
    {
        if( valLow == 1 ) return 0;
        if( valLow == 2 ) return 1;
        if( valLow == 3 ) return 2;
        if( valLow == 11 ) return 0;
        if( valLow == 12 ) return 1;
        if( valLow == 13 ) return 2;
        if( valLow == 4 ) return 15;
        if( valLow == 5 ) return 16;
        if( valLow == 6 ) return 15;
        if( valLow == 7 ) return 16;
        if( valLow == 8 ) return 0;
        if( valLow == 9 ) return 17;
        if( valLow == 16 ) return 17;
    }
    else
    {
        if( valLow == 11 ) return 3 + ( valHigh - 1 );
        if( valLow == 12 ) return 7 + ( valHigh - 1 );
        if( valLow == 13 ) return 11 + ( valHigh - 1 );
    }
    return 0;
}

// Shared page buffer for tdCompositeTabIntoBuffer()/tdCompositeNewPillIntoBuffer()
// below - both just OR their contribution into it, matching how their old
// per-pixel return values were OR'd together directly in tdTinyFlip().
int[128] tdSpritePageBuffer;

// Old tdDrawTab(x,y) scanned every one of the ~273 relevant (x,y) pixels
// and, for each, re-derived which grid cell(s) could overlap it before
// calling tdBlitzSprite() - most of those calls immediately bounced off
// blitzSprite's own bounds check since most pixels don't have a non-empty
// cell nearby. This composites once per *page row* instead: walk the
// handful of grid cells that can overlap this row directly (skipping
// empty ones with a single array read), and only call tdBlitzSprite() for
// the up to 4 columns each actually occupies - same restructuring as
// gameTinyBomber.c's bomCompositeSprites()/gameTinyPacman.c's
// pacCompositeSprites() (per-object, not per-pixel).
void tdCompositeTabIntoBuffer( int y )
{
    int rowBase = y * 128;
    int cx;
    for( cx = 0; cx < 128; cx++ )
      tdTabCache[ rowBase + cx ] = 0;

    int aa = 0, bb = 0;
    tdReturnScanLineY( &aa, &bb, y );
    int yy, xx, col;
    for( yy = aa; yy <= bb; yy++ )
      for( xx = 0; xx < 8; xx++ )
      {
          if( tdTab[ xx ][ yy ] == 0 ) continue;

          int cellType = tdTab[ xx ][ yy ] & 0x0F;
          // tdVirus's own "not a virus" frame (1) is all-zero sprite data
          // ({0x00,0x00,0x00,0x00}) - tdBlitzSprite() with that frame is
          // guaranteed to return 0 for every column, every time, so skip
          // the call entirely for non-virus (i.e. locked pill-half) cells
          // instead of paying its full cost just to get 0 back. Locked
          // pill halves only ever accumulate over a level while viruses
          // only ever decrease, so this is exactly the "many pills on
          // screen" cost the user flagged - previously every one of those
          // accumulating cells paid for a virus-layer blit it could never
          // actually need.
          bool isVirusCell = ( cellType >= 1 && cellType <= 3 );

          int pillFrame = tdReturncorectPills( tdTab[ xx ][ yy ] );
          int xPos = TD_XPOS_TP + ( xx * 5 );
          int yPos = TD_YPOS_TP + ( yy * 5 );

          for( col = 0; col < 4; col++ )
          {
              int x = xPos + col;
              if( x < 46 || x > 84 ) continue;
              int v = tdBlitzSprite( xPos, yPos, x, y, pillFrame, tdPills );
              if( isVirusCell )
                v = v | tdBlitzSprite( xPos, yPos, x, y, tdFrmVirus, tdVirus );
              tdSpritePageBuffer[ x ] = tdSpritePageBuffer[ x ] | v;
              tdTabCache[ rowBase + x ] = tdTabCache[ rowBase + x ] | v;
          }
      }
}

// Same restructuring as tdCompositeTabIntoBuffer() above, applied to the
// two halves of the currently-falling pill (old tdDrawNewPill(x,y) scanned
// all 63 columns in 46-108 per row for both halves).
void tdCompositeNewPillIntoBuffer( int y )
{
    if( !tdPill.active ) return;

    int baseX, baseY;
    if( tdPill.inTab )
    {
        baseX = TD_XPOS_TP + ( tdPill.gridx * 5 );
        baseY = TD_YPOS_TP + ( tdPill.gridy * 5 );
    }
    else
    {
        baseX = tdPill.graphPosX;
        baseY = tdPill.graphPosY;
    }

    int x2, y2;
    if( tdPill.vertical ) { x2 = baseX; y2 = baseY - 5; }
    else { x2 = baseX + 5; y2 = baseY; }

    int frame1 = tdReturncorectPills( tdPill.pillPart1Stat );
    int frame2 = tdReturncorectPills( tdPill.pillPart2Stat );

    int col;
    for( col = 0; col < 4; col++ )
    {
        int x = baseX + col;
        if( x >= 46 && x <= 108 )
          tdSpritePageBuffer[ x ] = tdSpritePageBuffer[ x ] | tdBlitzSprite( baseX, baseY, x, y, frame1, tdPills );
    }
    for( col = 0; col < 4; col++ )
    {
        int x = x2 + col;
        if( x >= 46 && x <= 108 )
          tdSpritePageBuffer[ x ] = tdSpritePageBuffer[ x ] | tdBlitzSprite( x2, y2, x, y, frame2, tdPills );
    }
}

int tdDrawPreviewPill( int x, int y )
{
    if( tdPillMode != 4 ) return 0;
    if( y != 2 ) return 0;
    if( x < 95 || x > 108 ) return 0;
    int byteout = tdBlitzSprite( 97, 18, x, y, tdReturncorectPills( tdPill.pillPart1PreviewStat ), tdPills );
    byteout = byteout | tdBlitzSprite( 102, 18, x, y, tdReturncorectPills( tdPill.pillPart2PreviewStat ), tdPills );
    return byteout;
}

int tdDrawShadowPreviewPill( int x, int y )
{
    if( y > 2 ) return 0;
    if( tdPillMode == 3 )
      return tdBlitzSprite( tdPill.graphPosX - 1, tdPill.graphPosY - 1, x, y, 0, tdShadow );
    return 0;
}

int tdDrawLoupe( int x, int y )
{
    if( x < 12 || x > 29 ) return 0;
    if( y < 4 || y > 6 ) return 0;
    int byteout = 0;
    if( tdVirusTypeActif[ 0 ] ) byteout = byteout | tdBlitzSprite( 15, 35, x, y, 0 + tdFrmVirus, tdVirusLoupe );
    if( tdVirusTypeActif[ 1 ] ) byteout = byteout | tdBlitzSprite( 23, 42, x, y, 3 + tdFrmVirus, tdVirusLoupe );
    if( tdVirusTypeActif[ 2 ] ) byteout = byteout | tdBlitzSprite( 12, 45, x, y, 6 + tdFrmVirus, tdVirusLoupe );
    return byteout;
}

int tdDrawScore( int x, int y )
{
    if( x < 10 || x > 28 ) return 0;
    if( y != 1 ) return 0;
    int byteout = tdBlitzSprite( 10, 11, x, y, tdM10000, tdPolice );
    byteout = byteout | tdBlitzSprite( 14, 11, x, y, tdM1000, tdPolice );
    byteout = byteout | tdBlitzSprite( 18, 11, x, y, tdM100, tdPolice );
    byteout = byteout | tdBlitzSprite( 22, 11, x, y, tdM10, tdPolice );
    byteout = byteout | tdBlitzSprite( 26, 11, x, y, tdM1, tdPolice );
    return byteout;
}

int tdDrawDisplayLv( int x, int y )
{
    if( x < 109 || x > 115 ) return 0;
    if( y < 5 || y > 6 ) return 0;
    int byteout = tdBlitzSprite( 109, 41, x, y, tdLvlDisplay10, tdPolice );
    byteout = byteout | tdBlitzSprite( 113, 41, x, y, tdLvlDisplay1, tdPolice );
    byteout = byteout | tdBlitzSprite( 109, 50, x, y, tdVDisplay10, tdPolice );
    byteout = byteout | tdBlitzSprite( 113, 50, x, y, tdVDisplay1, tdPolice );
    return byteout;
}

void tdTinyFlip()
{
    md_beginFrame();
    int x, y;
    for( y = 0; y < 8; y++ )
    {
        // Each layer below only ever draws within an already-known narrow
        // x/y sub-range (see each function's own bounds check, hoisted
        // here) - gating the call on that range avoids paying full call
        // overhead for a guaranteed no-op on the other rows/columns, same
        // lesson learned from this session's Tiny Invaders/Bomber CPU-load
        // optimization pass.
        bool scoreRow = ( y == 1 );
        bool loupeRows = ( y >= 4 && y <= 6 );
        bool previewRow = ( y == 2 && tdPillMode == 4 );
        bool shadowRows = ( y <= 2 );
        bool tabRows = ( y >= 1 );
        bool displayLvRows = ( y >= 5 && y <= 6 );

        for( x = 0; x < 128; x++ )
          tdSpritePageBuffer[ x ] = 0;
        if( tabRows )
        {
            if( tdPageRowDirty[ y ] )
            {
                tdCompositeTabIntoBuffer( y );
                tdPageRowDirty[ y ] = false;
            }
            else
            {
                int cx;
                int rowBase = y * 128;
                for( cx = 46; cx <= 84; cx++ )
                  tdSpritePageBuffer[ cx ] = tdSpritePageBuffer[ cx ] | tdTabCache[ rowBase + cx ];
            }
        }
        tdCompositeNewPillIntoBuffer( y );

        for( x = 0; x < 128; x++ )
        {
            int displayLv = 0;
            if( displayLvRows && x >= 109 && x <= 115 ) displayLv = tdDrawDisplayLv( x, y );

            int score = 0;
            if( scoreRow && x >= 10 && x <= 28 ) score = tdDrawScore( x, y );

            int loupe = 0;
            if( loupeRows && x >= 12 && x <= 29 ) loupe = tdDrawLoupe( x, y );

            int preview = 0;
            if( previewRow && x >= 95 && x <= 108 ) preview = tdDrawPreviewPill( x, y );

            int shadow = 0;
            if( shadowRows ) shadow = tdDrawShadowPreviewPill( x, y );

            int pixel = displayLv | score | loupe | preview | tdSpritePageBuffer[ x ] |
                        ( ( ~shadow ) & tdBackground[ x + ( y * 128 ) ] );
            md_drawColumn( x, y, pixel );
        }
    }
    tdFullRefresh = 0;
}

void tdIntroFlip( int show )
{
    md_beginFrame();
    int x, y;
    for( y = 0; y < 8; y++ )
      for( x = 0; x < 128; x++ )
      {
          int frame1 = 1; if( show ) frame1 = 0;
          int frame2 = 5; if( show ) frame2 = 3;
          int frame3 = 6; if( show ) frame3 = 8;
          int yBg = y; if( !show ) { if( y > 3 ) yBg = 0; else yBg = y; }

          int pixel = tdSpeedBlitz( 6, 2, x, y, frame1, tdVirusLoupe ) |
                      tdSpeedBlitz( 25, 1, x, y, frame2, tdVirusLoupe ) |
                      tdSpeedBlitz( 21, 3, x, y, frame3, tdVirusLoupe ) |
                      tdSpeedBlitz( 46, 2, x, yBg, 0, tdIntrogame );
          md_drawColumn( x, y, pixel );
      }
}

void tdLevelClearedFlip()
{
    md_beginFrame();
    int x, y;
    for( y = 0; y < 8; y++ )
      for( x = 0; x < 128; x++ )
      {
          int pixel = 0;
          if( y >= 5 && y <= 6 && x >= 109 && x <= 115 ) pixel = pixel | tdDrawDisplayLv( x, y );
          if( y == 1 && x >= 10 && x <= 28 ) pixel = pixel | tdDrawScore( x, y );
          if( y >= 4 && y <= 6 && x >= 12 && x <= 29 ) pixel = pixel | tdDrawLoupe( x, y );
          pixel = pixel | tdSpeedBlitz( 51, 2, x, y, 0, tdLevelCleared );
          pixel = pixel | tdBackground[ x + ( y * 128 ) ];
          md_drawColumn( x, y, pixel );
    }
}

// -----------------------------------------------------------------------------
//   Input-driven pill control (only runs while a pill is under player
//   control - PILLMODE 4)
// -----------------------------------------------------------------------------

void tdTinyjoypadUpdate()
{
    // A plain isFirePressed() edge-compare would miss a tap that started
    // and ended between two logic ticks - md_recentlyPressed() catches a
    // press that began within the last TD_TICK_DIVISOR real frames
    // instead of only the exact instant we happen to sample it.
    bool fire = md_recentlyPressed( md_inputFireFrames(), TD_TICK_DIVISOR );
    if( fire && !tdPrevFire )
      tdRotatePill();
    tdPrevFire = fire;

    if( isRightPressed() )
    {
        if( tdAutoTrigR > 0 ) tdAutoTrigR--;
        else { tdAutoTrigR = 1; tdStepRight = 0; }
        if( tdStepRight == 0 )
        {
            if( !tdCheckColision( tdPill.gridx + 1, tdPill.gridy ) )
            {
                tdStepRight = 1;
                tdPill.gridx++;
                tdSndTdoc( 0 );
            }
        }
    }
    else
    {
        tdStepRight = 0;
        tdAutoTrigR = 5;
    }

    if( isLeftPressed() )
    {
        if( tdAutoTrigL > 0 ) tdAutoTrigL--;
        else { tdAutoTrigL = 1; tdStepLeft = 0; }
        if( tdStepLeft == 0 )
        {
            if( !tdCheckColision( tdPill.gridx - 1, tdPill.gridy ) )
            {
                tdStepLeft = 1;
                tdPill.gridx--;
                tdSndTdoc( 0 );
            }
        }
    }
    else
    {
        tdAutoTrigL = 5;
        tdStepLeft = 0;
    }

    int dropThreshold = tdGameP.speed;
    if( isDownPressed() ) dropThreshold = 0;

    if( tdTimerDrop < dropThreshold )
      tdTimerDrop++;
    else
    {
        if( !tdCheckColision( tdPill.gridx, tdPill.gridy + 1 ) )
        {
            tdPill.gridy++;
            tdSndTdoc( 3 );
        }
        else
        {
            tdFixPill();
            tdResolveState = TD_RESOLVE_SCAN;
        }
        tdTimerDrop = 0;
    }
}

void tdAnimPillShoot()
{
    tdPill.graphPosX = tdAnim[ tdAnimPos * 2 ];
    tdPill.graphPosY = tdAnim[ ( tdAnimPos * 2 ) + 1 ];
    if( tdAnimPos < 10 )
      tdAnimPos++;
    else
    {
        if( tdTab[ tdPill.gridx ][ tdPill.gridy ] != 0 || tdTab[ tdPill.gridx + 1 ][ tdPill.gridy ] != 0 )
          tdGameOver = 1;
        tdGeneratenewPreviewPill();
        tdPill.inTab = 1;
        tdAnimPos = 0;
        tdFullRefresh = 1;
        tdPillMode++;
    }
}

void tdDropMode()
{
    if( tdPill.active == 0 ) tdPillMode = 1;
}

void tdPillProcess()
{
    if( tdPillMode == 0 )
    {
        tdGeneratenewPreviewPill();
        tdInitNewPill( 1 );
        tdAnimPillShoot();
        tdPillMode = 3;
    }
    else if( tdPillMode == 1 )
    {
        tdInitNewPill( 1 );
        tdAnimPillShoot();
        tdPillMode = 3;
    }
    else if( tdPillMode == 2 )
      tdPillMode++;
    else if( tdPillMode == 3 )
      tdAnimPillShoot();
    else if( tdPillMode == 4 )
      tdDropMode();
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

void tdInitPublicVarForNewGame()
{
    tdGameP.speed = 0;
    tdGameP.tabRange = 0;
    tdGameP.level = 0;
    tdScore = 0;
    tdGameOver = 0;
    tdM10000 = 0;
    tdM1000 = 0;
    tdM100 = 0;
    tdM10 = 0;
    tdM1 = 0;
}

void tdInitPublicVarForNewLevel()
{
    tdMarkAllRowsDirty();
    int x, y;
    tdFullRefresh = 1;
    tdTimerDrop = 0;
    tdStepRight = 0;
    tdStepLeft = 0;
    tdAnimPos = 0;
    tdPillMode = 0;
    tdFrmVirus = 1;
    tdAnimSpeedVirus = 0;
    tdAutoTrigL = 0;
    tdAutoTrigR = 0;
    for( y = 0; y < 10; y++ )
      for( x = 0; x < 8; x++ )
      {
          tdBackCheck[ x ][ y ] = 0;
          tdTab[ x ][ y ] = 0;
      }
    tdVirusTypeActif[ 0 ] = 0;
    tdVirusTypeActif[ 1 ] = 0;
    tdVirusTypeActif[ 2 ] = 0;
    tdLastTotalVirusLeft = 0;
    tdTotalVirusLeft = 0;
    tdGameP.totalVirusInLevel = 0;
    tdPill.pillPart1PreviewStat = 0;
    tdPill.pillPart2PreviewStat = 0;
}

void tdBeginLevel()
{
    tdInitPublicVarForNewLevel();
    tdAdjustGamePlay();
    tdInitRnd();
    tdCountVirusTypes();
    tdFirstCalculeDisplay();
    tdInitNewPill( 1 );
    tdResolveState = TD_RESOLVE_NONE;
    tdTinyFlip();
    tdState = TD_STATE_LEVEL_INTRO_WAIT;
    tdWaitFrames = 30; // ~1000ms at 30fps (this whole function only ticks every other real frame - see TD_TICK_DIVISOR - halved from 60 to preserve the original real-world duration)
}

void tdBeginAttract()
{
    tdState = TD_STATE_ATTRACT;
    tdAttractBlk = 0;
}

void gameTinyDoc_init()
{
    InitTinyJoypad();
    tdBeginAttract();
}

void gameTinyDoc_update()
{
    // Upstream throttled its whole logic+redraw loop (every state, not
    // just gameplay) to a fixed ~30fps - only tick once every
    // TD_TICK_DIVISOR real frames to match; skipped frames leave the
    // previous frame's image on screen (no draw call happens for them).
    tdTickSkipCounter++;
    if( tdTickSkipCounter < TD_TICK_DIVISOR )
      return;
    tdTickSkipCounter = 0;

    if( tdState == TD_STATE_ATTRACT )
    {
        arand( 1 ); // matches upstream's throwaway rand() call each attract frame
        int show = 0;
        if( tdAttractBlk < 16 ) show = 1;
        tdIntroFlip( show );
        if( tdAttractBlk < 33 ) tdAttractBlk++;
        else tdAttractBlk = 0;

        if( md_recentlyPressed( md_inputFireFrames(), TD_TICK_DIVISOR ) )
        {
            md_armInputFireGate();
            tdSndTdoc( 6 );
            tdInitPublicVarForNewGame();
            tdBeginLevel();
        }
        return;
    }

    if( tdState == TD_STATE_LEVEL_INTRO_WAIT )
    {
        tdWaitFrames--;
        if( tdWaitFrames <= 0 )
          tdState = TD_STATE_PLAYING;
        tdTinyFlip();
        return;
    }

    if( tdState == TD_STATE_PLAYING )
    {
        if( tdResolveState != TD_RESOLVE_NONE )
        {
            if( tdResolveState == TD_RESOLVE_SCAN )
            {
                tdCheckCompletedLine();
                if( tdElement.clr )
                {
                    tdResolveState = TD_RESOLVE_CLEAR_ANIM;
                    tdClearAnimFrame = 4;
                }
                else
                  tdResolveState = TD_RESOLVE_DROP;
            }
            else if( tdResolveState == TD_RESOLVE_CLEAR_ANIM )
            {
                tdMarkAllRowsDirty();
                int x, y;
                for( y = 0; y < 10; y++ )
                  for( x = 0; x < 8; x++ )
                    if( tdBackCheck[ x ][ y ] == 20 )
                      tdTab[ x ][ y ] = tdClearAnimFrame;
                tdSndTdoc( 2 );
                tdClearAnimFrame++;
                if( tdClearAnimFrame >= 10 )
                {
                    for( y = 0; y < 10; y++ )
                      for( x = 0; x < 8; x++ )
                        if( tdBackCheck[ x ][ y ] == 20 )
                          tdTab[ x ][ y ] = 0;
                    tdElement.clr = 0;
                    tdSetSinglePill();
                    tdResolveState = TD_RESOLVE_DROP;
                }
            }
            else if( tdResolveState == TD_RESOLVE_DROP )
            {
                if( tdDropPills() )
                  tdResolveState = TD_RESOLVE_SCAN;
                else
                {
                    tdResolveState = TD_RESOLVE_NONE;
                    tdPillMode = 1;
                }
            }
        }
        else
        {
            if( tdPillMode == 4 )
              tdTinyjoypadUpdate();
            tdPillProcess();
            tdCountVirusTypes();
            tdCompilScore();

            if( tdAnimSpeedVirus < 2 )
              tdAnimSpeedVirus++;
            else
            {
                if( tdFrmVirus < 2 ) tdFrmVirus++; else tdFrmVirus = 0;
                tdAnimSpeedVirus = 0;
                // tdFrmVirus is baked into the locked-grid composite
                // (tdCompositeTabIntoBuffer's virus-layer blit) but that
                // composite only recomputes when a row's dirty flag is set
                // by an actual grid *mutation* - a purely cosmetic
                // animation tick like this one doesn't touch tdTab at all,
                // so without this, viruses only ever visually advance to a
                // new frame whenever some unrelated grid change happens
                // to also refresh the cache, making them look far less
                // animated than before the dirty-flag caching was added.
                // Every row is marked (not just rows with a virus cell)
                // since virus cells can be anywhere on the board.
                tdMarkAllRowsDirty();
            }

            if( tdGameOver == 1 )
            {
                tdSndTdoc( 5 );
                tdInitNewPill( 0 );
                tdState = TD_STATE_GAMEOVER_WAIT;
                tdWaitFrames = 60; // ~2000ms at 30fps (this whole function only ticks every other real frame - see TD_TICK_DIVISOR - halved from 120 to preserve the original real-world duration)
                return;
            }

            if( tdTotalVirusLeft == 0 )
            {
                tdLevelClearedFlip();
                tdSndTdoc( 1 );
                tdState = TD_STATE_LEVEL_CLEARED_WAIT;
                return;
            }
        }

        // Always redraw the full 128x8 screen, never the partial
        // 85-wide/4-tall region upstream used - that optimization skips
        // i2c_write calls for columns/rows that "didn't change" and
        // relies on the real SSD1306's VRAM persisting whatever was
        // drawn there last time (a real hardware behavior this engine's
        // always-clear-then-redraw model via md_beginFrame() can't
        // replicate - see gameTinyPinball.c's own header comment for the
        // same lesson). Skipping those columns here left the right
        // portion of the screen (background art, virus loupe, etc.)
        // black except during the one PILLMODE that happened to redraw
        // full-width - exactly the bug the user reported.
        tdTinyFlip();
        return;
    }

    if( tdState == TD_STATE_LEVEL_CLEARED_WAIT )
    {
        if( md_recentlyPressed( md_inputFireFrames(), TD_TICK_DIVISOR ) )
        {
            tdGameP.level++;
            tdBeginLevel();
        }
        return;
    }

    if( tdState == TD_STATE_GAMEOVER_WAIT )
    {
        tdWaitFrames--;
        if( tdWaitFrames <= 0 )
          tdBeginAttract();
        return;
    }
}

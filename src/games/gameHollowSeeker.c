// =============================================================================
// HollowSeeker - ported from Obono's TinyJoypadWorks/hollowseeker
// (https://github.com/obono/TinyJoypadWorks/tree/main/hollowseeker), MIT
// License, Copyright (c) 2020-2025 OBONO.
//
// Same shim/port pattern as gameNumberPlace.c/gameT2048.c (obonoCoreShim.h).
// Already mode/state-based upstream (STATE_START/PLAYING/OVER inside
// MODE_GAME), no blocking loops to convert.
//
// This game's own core.cpp uses a WIDER font range ('!' through '_') than
// NumberPlace/t2048 needed ('-' through 'Z') - obonoCoreShim.c's shared
// imgFont[] table was widened to hollowseeker's superset (byte-identical
// in the overlapping range) rather than duplicating a second font table,
// same "fix it once in the shim" approach as the SPRITES capacity bump
// and md_drawColumn()'s byte-masking fix.
//
// Structural changes from upstream, beyond the usual dialect fixes (array-
// declaration syntax, no bitfields, no scoped/underlying-type enums, no
// ternary, no switch statement - converted to if/else chains since that
// hasn't been verified supported by this compiler, no function
// overloading):
//  - HS_FPS matches upstream's own real rate (30, not Vircon32's native
//    60fps) via a real-frame tick-skip throttle added in a later session
//    (see this file's own HS_TICK_DIVISOR comment and CLAUDE.md's "Frame-
//    pacing pass" section) - an earlier revision ran every real frame at
//    HS_FPS=60 instead (faster than upstream) before that throttle was
//    added.
//  - Arduino's constrain()/TWO_PI aren't part of Vircon32's math.h - added
//    a small clamp helper and a TWO_PI define built from math.h's own pi.
// =============================================================================

// Upstream (TinyJoypadWorks/hollowseeker/common.h) ran its whole
// logic+redraw loop at a fixed real FPS=30 - this port reproduces that by
// only actually ticking its own logic once every HS_TICK_DIVISOR real
// Vircon32 frames (see gameHollowSeeker_update()); HS_FPS must track that
// LOGIC tick rate, not the engine's 60fps display rate, since every timer
// derived from it (hsCounter) counts in ticks, not real frames.
#define HS_FPS 30
#define HS_TICK_DIVISOR ( 60 / HS_FPS )
#define HS_TWO_PI ( 2.0 * pi )

int hsClamp( int value, int lo, int hi )
{
    if( value < lo ) return lo;
    if( value > hi ) return hi;
    return value;
}

// -----------------------------------------------------------------------------
//   common.h / common.cpp equivalent
// -----------------------------------------------------------------------------

enum HsMode
{
    HS_MODE_LOGO = 0,
    HS_MODE_TITLE,
    HS_MODE_GAME
};

int hsMode = HS_MODE_LOGO;
int hsTickSkipCounter = 0;
int hsCounter;
int hsScore = 0;

// -----------------------------------------------------------------------------
//   data.h
// -----------------------------------------------------------------------------

int[8][8] hsImgPlayer =
{
    { 0x10, 0x83, 0xAC, 0x69, 0x6D, 0xAD, 0x82, 0x10 },
    { 0x00, 0x13, 0x6C, 0x69, 0xED, 0xAD, 0x02, 0x20 },
    { 0x00, 0x23, 0x2C, 0xE9, 0xED, 0x2D, 0x02, 0x20 },
    { 0x20, 0x06, 0xD8, 0xD2, 0x5A, 0x5A, 0xC4, 0x10 },
    { 0x10, 0x82, 0xAD, 0x6D, 0x69, 0xAC, 0x83, 0x10 },
    { 0x20, 0x02, 0xAD, 0xED, 0x69, 0x6C, 0x13, 0x00 },
    { 0x20, 0x02, 0x2D, 0xED, 0xE9, 0x2C, 0x23, 0x00 },
    { 0x10, 0xC4, 0x5A, 0x5A, 0xD2, 0xD8, 0x06, 0x20 }
};

int[8] hsImgCave = { 0x90, 0x06, 0x4E, 0xE4, 0xC0, 0x88, 0x18, 0x99 };

#define HS_IMG_READY_W 50
#define HS_IMG_READY_H 16

int[100] hsImgReady =
{
    0x00, 0x00, 0xF8, 0xF8, 0xF8, 0x98, 0xF8, 0xF8, 0x70, 0x00, 0x00, 0x80, 0xC0, 0xC0, 0xC0, 0xC0,
    0xC0, 0x80, 0x00, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x80, 0x00, 0x00, 0xC0, 0xC0, 0xC0, 0xC0, 0xF8,
    0xF8, 0xF8, 0x40, 0xC0, 0xC0, 0x00, 0x00, 0xC0, 0xC0, 0x00, 0x38, 0x98, 0xD8, 0xF8, 0xF8, 0x70,
    0x00, 0x00, 0x00, 0x00, 0x1F, 0x1F, 0x1F, 0x01, 0x1F, 0x1E, 0x18, 0x00, 0x07, 0x0F, 0x1F, 0x1A,
    0x1A, 0x1B, 0x1B, 0x03, 0x0C, 0x1E, 0x1E, 0x1A, 0x1F, 0x1F, 0x1F, 0x00, 0x0F, 0x1F, 0x1F, 0x18,
    0x08, 0x1F, 0x1F, 0x1F, 0xC0, 0xC1, 0xCF, 0xFF, 0x7C, 0x1F, 0x03, 0x00, 0x00, 0x1D, 0x1D, 0x1D,
    0x00, 0x00, 0x00, 0x00
};

#define HS_IMG_GAMEOVER_W 78
#define HS_IMG_GAMEOVER_H 16

int[156] hsImgGameOver =
{
    0x00, 0x00, 0xC0, 0xF0, 0xF0, 0x38, 0x98, 0x98, 0x98, 0xB8, 0x80, 0x00, 0x00, 0xC0, 0xC0, 0xC0,
    0xC0, 0xC0, 0x80, 0x00, 0xC0, 0xC0, 0xC0, 0x80, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x80, 0x00,
    0x00, 0x80, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x80, 0x00, 0x00, 0x00, 0xC0, 0xF0, 0xF0, 0x38, 0x18,
    0x18, 0x38, 0xF0, 0xF0, 0xC0, 0x00, 0xC0, 0xC0, 0xC0, 0x00, 0x00, 0xC0, 0xC0, 0x40, 0x00, 0x80,
    0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x80, 0x00, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x0F, 0x0F, 0x1C, 0x19, 0x19, 0x1F, 0x1F, 0x0F, 0x00, 0x0C, 0x1E, 0x1E, 0x1A, 0x1F, 0x1F,
    0x1F, 0x00, 0x1F, 0x1F, 0x1F, 0x00, 0x1F, 0x1F, 0x1F, 0x00, 0x1F, 0x1F, 0x1F, 0x00, 0x07, 0x0F,
    0x1F, 0x1A, 0x1A, 0x1B, 0x1B, 0x03, 0x00, 0x00, 0x00, 0x03, 0x0F, 0x0F, 0x1C, 0x18, 0x18, 0x1C,
    0x0F, 0x0F, 0x03, 0x00, 0x00, 0x07, 0x1F, 0x1F, 0x1E, 0x1F, 0x03, 0x00, 0x07, 0x0F, 0x1F, 0x1A,
    0x1A, 0x1B, 0x1B, 0x03, 0x00, 0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00
};

#define HS_IMG_FIGURE_W 8
#define HS_IMG_FIGURE_H 8

int[10][8] hsImgFigure =
{
    { 0x3C, 0x7E, 0x43, 0x41, 0x61, 0x7F, 0x3E, 0x00 },
    { 0x00, 0x02, 0x02, 0x7F, 0x7F, 0x00, 0x00, 0x00 },
    { 0x42, 0x63, 0x71, 0x51, 0x59, 0x4F, 0x4E, 0x00 },
    { 0x32, 0x73, 0x41, 0x49, 0x69, 0x7F, 0x36, 0x00 },
    { 0x30, 0x28, 0x24, 0x22, 0x7F, 0x7F, 0x20, 0x00 },
    { 0x0E, 0x4F, 0x49, 0x49, 0x69, 0x79, 0x31, 0x00 },
    { 0x3C, 0x7E, 0x4B, 0x49, 0x69, 0x78, 0x30, 0x00 },
    { 0x02, 0x03, 0x01, 0x71, 0x7D, 0x0F, 0x03, 0x00 },
    { 0x36, 0x7F, 0x49, 0x49, 0x6D, 0x7F, 0x36, 0x00 },
    { 0x06, 0x4F, 0x49, 0x49, 0x69, 0x7F, 0x3E, 0x00 }
};

int[11] hsSoundStart = { 72, 12, 74, 12, 76, 12, 77, 12, 79, 36, 0xFF };
int[17] hsSoundOver  = { 55, 13, 54, 16, 53, 19, 52, 22, 51, 25, 50, 28, 49, 31, 48, 34, 0xFF };

int[70] hsImgLogo =
{
    0x00, 0x80, 0xC0, 0x60, 0x20, 0xE0, 0x20, 0x20, 0x40, 0x80, 0x00, 0x00, 0xFF, 0x57, 0xAB, 0x57,
    0x01, 0xFF, 0x20, 0x20, 0x40, 0x80, 0x00, 0x00, 0x00, 0x80, 0xC0, 0x60, 0x20, 0xE0, 0x20, 0x20,
    0x40, 0x80, 0x00, 0x1F, 0x37, 0x6A, 0x95, 0xA8, 0x97, 0xAC, 0x94, 0x4C, 0x24, 0x1F, 0x00, 0x1F,
    0x35, 0x6A, 0x95, 0xA8, 0x97, 0xAC, 0x94, 0x4C, 0x24, 0x1F, 0x00, 0xFF, 0x97, 0xAA, 0x95, 0x80,
    0xBF, 0xAC, 0x94, 0xAC, 0x84, 0xFF
};

#define HS_IMG_TITLE1_W 64
#define HS_IMG_TITLE2_W 61
#define HS_IMG_TITLE_H  16
#define HS_IMG_GUY_W    39
#define HS_IMG_GUY_H    32

int[128] hsImgTitle1 =
{
    0xFE, 0xFE, 0xFE, 0xFE, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0xFE, 0xFE, 0xFE, 0x00, 0x00, 0xC0,
    0xC0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xC0, 0xC0, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xC0, 0xC0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xC0, 0xC0, 0x00, 0x60,
    0xE0, 0xE0, 0xE0, 0xE0, 0x00, 0x00, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0x00, 0x00, 0xE0, 0xE0, 0x60,
    0x7F, 0x7F, 0x7F, 0x7F, 0x01, 0x01, 0x01, 0x01, 0x01, 0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x0F, 0x3F,
    0x3F, 0x7F, 0x70, 0x70, 0x70, 0x7F, 0x3F, 0x3F, 0x0F, 0x00, 0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x7F,
    0x7F, 0x7F, 0x7F, 0x00, 0x0F, 0x3F, 0x3F, 0x7F, 0x70, 0x70, 0x70, 0x7F, 0x3F, 0x3F, 0x0F, 0x00,
    0x07, 0x7F, 0x7F, 0x7F, 0x78, 0x7F, 0x07, 0x00, 0x07, 0x7F, 0x7F, 0x7E, 0x7F, 0x3F, 0x07, 0x00
};

int[122] hsImgTitle2 =
{
    0x78, 0xFC, 0xFE, 0xFE, 0xCE, 0xCE, 0x8E, 0x1E, 0x00, 0x00, 0x00, 0xC0, 0xC0, 0xE0, 0x60, 0x60,
    0xE0, 0xE0, 0xC0, 0x80, 0x00, 0x00, 0xC0, 0xC0, 0xE0, 0x60, 0x60, 0xE0, 0xE0, 0xC0, 0x80, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xC0, 0xE0, 0xE0, 0xE0, 0x60, 0x20, 0x00, 0xC0, 0xC0, 0xE0, 0x60,
    0x60, 0xE0, 0xE0, 0xC0, 0x80, 0x00, 0xE0, 0xE0, 0xE0, 0xE0, 0xC0, 0xE0, 0xE0, 0x78, 0x71, 0x71,
    0x73, 0x73, 0x7F, 0x3F, 0x3F, 0x1E, 0x00, 0x1F, 0x3F, 0x3F, 0x7F, 0x76, 0x76, 0x77, 0x77, 0x77,
    0x07, 0x00, 0x1F, 0x3F, 0x3F, 0x7F, 0x76, 0x76, 0x77, 0x77, 0x77, 0x07, 0x00, 0x7F, 0x7F, 0x7F,
    0x7F, 0x06, 0x1F, 0x7F, 0x7F, 0x7D, 0x70, 0x40, 0x1F, 0x3F, 0x3F, 0x7F, 0x76, 0x76, 0x77, 0x77,
    0x77, 0x07, 0x00, 0x7F, 0x7F, 0x7F, 0x7F, 0x01, 0x00, 0x00
};

int[156] hsImgGuy =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC0, 0xE0, 0xF0, 0x70, 0x78, 0x78, 0x00, 0xF8, 0xFC,
    0x06, 0xF3, 0xF9, 0xFD, 0xFD, 0xFD, 0xF9, 0xF2, 0x04, 0xF8, 0x00, 0x60, 0x40, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0xD8, 0xD6, 0xCB, 0xD4, 0xEA, 0xE4,
    0xEA, 0xF5, 0xF2, 0xF5, 0xF2, 0xF9, 0xF7, 0xF6, 0xEC, 0xED, 0xEB, 0xEB, 0xEB, 0xE9, 0xF4, 0xFA,
    0xF9, 0xF8, 0xFA, 0x79, 0x7A, 0x7B, 0x7C, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x07, 0x07, 0x3F, 0xC7, 0x07, 0x03, 0xAB, 0x53, 0xFB, 0xFB, 0xFB, 0xFD, 0xFD, 0x81, 0x81,
    0xFD, 0xFD, 0xFD, 0xFE, 0xC0, 0xC0, 0xFE, 0xFE, 0xFE, 0xFE, 0xFF, 0x1F, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x20, 0x10, 0x28, 0x48, 0x84, 0x04, 0xA5, 0x56, 0xAC, 0x59,
    0xAA, 0x5D, 0xB7, 0x57, 0xB7, 0x6F, 0xAF, 0x69, 0xA8, 0x28, 0xE8, 0x6E, 0xAF, 0x57, 0xB7, 0x57,
    0xAB, 0x54, 0xAA, 0x56, 0xEA, 0x42, 0xE2, 0x16, 0x0C, 0x04, 0x08, 0xF0
};

// -----------------------------------------------------------------------------
//   logo.cpp
// -----------------------------------------------------------------------------

#define HS_IMG_LOGO_W 35
#define HS_IMG_LOGO_H 16

void hsInitLogo()
{
    setSprite( 0, ( WIDTH - HS_IMG_LOGO_W ) / 2, ( HEIGHT - HS_IMG_LOGO_H ) / 2, hsImgLogo,
               HS_IMG_LOGO_W, HS_IMG_LOGO_H, DIRECT );
    setString( 8, 86, "OBN-T01", WHITE );
    setString( 9, 104, "V0.1", WHITE );
    hsCounter = HS_FPS * 2;
    isInvalid = true;
}

int hsUpdateLogo()
{
    hsCounter--;
    if( hsCounter > 0 )
      return HS_MODE_LOGO;
    return HS_MODE_TITLE;
}

// -----------------------------------------------------------------------------
//   game.cpp forward decl (title screen shows the score if replaying)
// -----------------------------------------------------------------------------

void hsUpdateScore();

// -----------------------------------------------------------------------------
//   title.cpp
// -----------------------------------------------------------------------------

void hsInitTitle()
{
    setSprite( 0, 24, 8, hsImgTitle1, HS_IMG_TITLE1_W, HS_IMG_TITLE_H, DIRECT );
    setSprite( 1, 43, 24, hsImgTitle2, HS_IMG_TITLE2_W, HS_IMG_TITLE_H, DIRECT );
    if( hsScore > 0 )
      hsUpdateScore();
    setSprite( 6, 1, 32, hsImgGuy, HS_IMG_GUY_W, HS_IMG_GUY_H, DIRECT );
    setString( 8, 54, "PRESS BUTTON", WHITE );
    isInvalid = true;
}

int hsUpdateTitle()
{
    srand( rand() ); // shuffle random, matches upstream's stirring of the RNG

    if( isButtonDown( A_BUTTON ) )
      return HS_MODE_GAME;
    return HS_MODE_TITLE;
}

// -----------------------------------------------------------------------------
//   game.cpp
// -----------------------------------------------------------------------------

#define HS_COLUMNS         18
#define HS_COLUMN_W        8
#define HS_CAVE_WIDTH      ( HS_COLUMNS * HS_COLUMN_W )
#define HS_CAVE_BOTTOM_MIN 16
#define HS_CAVE_BOTTOM_MAX 56
#define HS_CAVE_BOTTOM_ADJ 34
#define HS_CAVE_GAPMAX_MIN 32
#define HS_CAVE_GAPMAX_MAX 255
#define HS_CAVE_PHASE_MAX  256

#define HS_PLAYER_X_MAX 56
#define HS_PLAYER_W     8
#define HS_PLAYER_H     8
#define HS_PLAYER_SPEED 2

#define HS_STATE_START   0
#define HS_STATE_PLAYING 1
#define HS_STATE_OVER    2

struct HsColumn
{
    int top;
    int bottom;
};

// Vircon32's `>>` is a *logical* (zero-fill) shift, not arithmetic (see
// VIRCON32_C_DIALECT.md) - unlike AVR-GCC's codegen, which sign-extends a
// negative int's right shift. A plain `val >> 3` here breaks the moment
// `val` is negative: `hsUpdatePlayer()`'s `hsPlayerJump * hsPlayerMove`
// term is negative whenever the player moves into a column where the
// cave floor drops, and the resulting huge *positive* garbage value (from
// zero-filling instead of sign-extending) got added into `playerY`, which
// the very next line's own clamp (`if (playerY > HEIGHT-HS_PLAYER_H)
// playerY = HEIGHT-HS_PLAYER_H;`) then pinned to the bottom of the
// screen for that one frame - reported by the user as the player sprite
// briefly flashing at the bottom of the screen "over" the cave when
// moving right. Fixed by handling the negative case explicitly (same
// "explicit clamp, don't trust the shift instruction outside AVR's
// implicit range" fix already applied to this file's cave-wall edge-
// pattern shift math - see CLAUDE.md's own write-up of that bug), rather
// than relying on `>>` alone: this computes the same floor-division-by-8
// result AVR's arithmetic shift did.
int hsDivByColumnW( int val )
{
    if( val >= 0 )
      return val >> 3;
    return -( ( -val + 7 ) >> 3 );
}

#define hsModByColumnW(val) ( (val) & 7 )
#define hsGetColumnOfPos(pos) ( hsDivByColumnW(pos) % HS_COLUMNS )

HsColumn[HS_COLUMNS] hsCaveColumn;
int hsState;
int hsPlayerX, hsCavePhase, hsCaveOffset, hsCaveGapMax, hsCaveGapCurrent;
int hsPlayerMove, hsPlayerJump, hsCaveHollowDistance, hsCaveBaseTop, hsCaveBaseBottom;
bool hsIsPlayerRight;

void hsGrowCave()
{
    int col = hsGetColumnOfPos( hsCaveOffset + WIDTH + HS_COLUMN_W );
    HsColumn* pNewColumn = &hsCaveColumn[ col ];
    HsColumn* pLastColumn = &hsCaveColumn[ circulate( col, -1, HS_COLUMNS ) ];
    int lastDiff = pLastColumn->bottom - pLastColumn->top;
    int newDiff = arand( HS_PLAYER_H / 2 - 1 );

    hsCaveHollowDistance--;
    if( hsCaveHollowDistance <= 0 )
    {
        newDiff = HS_PLAYER_H - newDiff;
        // Upstream: ((rand() + 32768) * score >> 22) + 2 - relies on AVR's
        // rand() being a non-negative ~16-bit value (see avrCompat.h's own
        // comment on arand() for why that formula breaks on Vircon32's
        // rand()). This is an approximation of the same *intent* (a bounded
        // "how many columns until the next hollow" distance that grows
        // slowly with score) built on arand() instead, since the exact
        // scaling constant is a gameplay-feel tuning value, not something
        // that needs to match bit-for-bit.
        hsCaveHollowDistance = arand( hsScore / 32 + 4 ) + 2;
    }

    int adjust = ( pLastColumn->bottom - HS_CAVE_BOTTOM_ADJ ) / 5;
    int absAdjust = adjust;
    if( absAdjust < 0 ) absAdjust = -absAdjust;
    int diffDelta = newDiff - lastDiff;
    int absDiffDelta = diffDelta;
    if( absDiffDelta < 0 ) absDiffDelta = -absDiffDelta;

    int change = arand( HS_PLAYER_H * 2 + 1 - absDiffDelta - absAdjust ) - HS_PLAYER_H;
    if( newDiff > lastDiff )
      change += newDiff - lastDiff;
    if( adjust < 0 )
      change -= adjust;

    pNewColumn->bottom = pLastColumn->bottom + change;
    pNewColumn->bottom = hsClamp( pNewColumn->bottom, HS_CAVE_BOTTOM_MIN, HS_CAVE_BOTTOM_MAX );
    pNewColumn->top = pNewColumn->bottom - newDiff;
}

void hsInitCave()
{
    hsCaveOffset = 0;
    int lastIdx = hsDivByColumnW( HS_PLAYER_X_MAX + HS_COLUMN_W );
    for( int i = 0; i <= lastIdx; i++ )
    {
        hsCaveColumn[ i ].top = HEIGHT / 2;
        hsCaveColumn[ i ].bottom = HEIGHT / 2 + HS_PLAYER_H;
    }

    hsCaveHollowDistance = 2;
    hsCaveOffset = HS_PLAYER_X_MAX + HS_COLUMN_W * 3;
    while( hsCaveOffset <= WIDTH + HS_COLUMN_W * 2 )
    {
        hsGrowCave();
        hsCaveOffset += HS_COLUMN_W;
    }

    hsCaveOffset = HS_COLUMN_W;
    hsCavePhase = 0;
    hsCaveGapMax = HS_CAVE_GAPMAX_MIN;
}

void hsUpdateCave()
{
    hsCaveGapCurrent = (int)( ( 1.0 - cos( hsCavePhase * HS_TWO_PI / (float)HS_CAVE_PHASE_MAX ) ) * hsCaveGapMax / 2.0 );
    hsCaveBaseTop = -( hsCaveGapCurrent + 1 ) / 2;
    hsCaveBaseBottom = hsCaveGapCurrent / 2;

    if( hsCavePhase >= HS_CAVE_PHASE_MAX - 12 )
    {
        md_playTone( (float)( arand( 16 ) + 16 ), 0.05 );
        if( ( hsCavePhase & 1 ) != 0 )
        {
            hsCaveBaseTop++;
            hsCaveBaseBottom++;
        }
    }
}

void hsInitPlayer()
{
    hsPlayerX = 0;
    hsPlayerMove = 0;
    hsIsPlayerRight = true;
}

int hsIsPlayerRightInt()
{
    if( hsIsPlayerRight )
      return 1;
    return 0;
}

int hsUpdatePlayer()
{
    int col = hsGetColumnOfPos( hsPlayerX + hsPlayerMove * hsIsPlayerRightInt() + hsCaveOffset );
    HsColumn* pPlayerColumn = &hsCaveColumn[ col ];

    if( hsPlayerMove == 0 )
    {
        int vx;
        if( hsState == HS_STATE_PLAYING )
        {
            vx = isButtonPressed( RIGHT_BUTTON ) - isButtonPressed( LEFT_BUTTON );
        }
        else
        {
            vx = 0;
            if( hsState == HS_STATE_START )
              vx = 1;
        }

        if( vx != 0 )
        {
            hsIsPlayerRight = ( vx > 0 );
            HsColumn* pNextColumn = &hsCaveColumn[ circulate( col, vx, HS_COLUMNS ) ];

            int gapA = pPlayerColumn->bottom;
            if( pNextColumn->bottom < gapA ) gapA = pNextColumn->bottom;
            int gapB = pPlayerColumn->top;
            if( pNextColumn->top > gapB ) gapB = pNextColumn->top;
            int nextGap = gapA - gapB;

            if( hsPlayerX + vx >= 0 && nextGap + hsCaveGapCurrent >= HS_PLAYER_H )
            {
                hsPlayerJump = pPlayerColumn->bottom - pNextColumn->bottom;
                hsPlayerMove = HS_COLUMN_W;
                pPlayerColumn = pNextColumn;
                if( hsState == HS_STATE_PLAYING )
                  md_playTone( 660.0, 0.02 );
            }
        }
    }

    if( hsPlayerMove > 0 )
    {
        if( hsIsPlayerRight && hsPlayerX == HS_PLAYER_X_MAX )
        {
            if( hsPlayerMove == HS_COLUMN_W )
            {
                hsGrowCave();
                hsScore++;
                hsUpdateScore();
            }
            hsCaveOffset = ( hsCaveOffset + HS_PLAYER_SPEED ) % HS_CAVE_WIDTH;
        }
        else
        {
            if( hsIsPlayerRight )
              hsPlayerX += HS_PLAYER_SPEED;
            else
              hsPlayerX -= HS_PLAYER_SPEED;
        }
        hsPlayerMove -= HS_PLAYER_SPEED;
    }

    int playerY;
    if( hsState == HS_STATE_OVER )
    {
        playerY = pPlayerColumn->top + hsCaveBaseBottom;
    }
    else
    {
        playerY = pPlayerColumn->bottom + hsCaveBaseBottom - HS_PLAYER_H
                + hsDivByColumnW( hsPlayerJump * hsPlayerMove );
        if( playerY > HEIGHT - HS_PLAYER_H ) playerY = HEIGHT - HS_PLAYER_H;
        if( playerY < pPlayerColumn->top + hsCaveBaseTop ) playerY = pPlayerColumn->top + hsCaveBaseTop;
    }

    setSprite( 0, hsPlayerX, playerY, hsImgPlayer[ hsIsPlayerRightInt() * 4 + hsPlayerMove / 2 ],
               HS_PLAYER_W, HS_PLAYER_H, WHITE );

    return pPlayerColumn->bottom - pPlayerColumn->top;
}

void hsUpdateScore()
{
    int pos = 0;
    if( hsScore >= 10 ) pos++;
    if( hsScore >= 100 ) pos++;
    if( hsScore >= 1000 ) pos++;

    int val = hsScore;
    while( pos >= 0 )
    {
        setSprite( pos + 2, pos * HS_IMG_FIGURE_W, 0, hsImgFigure[ val % 10 ],
                   HS_IMG_FIGURE_W, HS_IMG_FIGURE_H, DIRECT );
        pos--;
        val /= 10;
    }
}

void hsInitGame()
{
    hsScore = 0;
    hsInitCave();
    hsInitPlayer();
    initSprites();
    setSprite( 1, ( WIDTH - HS_IMG_READY_W ) / 2, 8, hsImgReady, HS_IMG_READY_W, HS_IMG_READY_H, DIRECT );
    playScore( hsSoundStart );
    hsState = HS_STATE_START;
    hsCounter = HS_FPS * 2;
}

int hsUpdateGame()
{
    int ret = HS_MODE_GAME;

    hsUpdateCave();
    int diff = hsUpdatePlayer();

    if( hsState == HS_STATE_START )
    {
        hsCounter--;
        if( hsCounter == 0 )
        {
            clearSprite( 1 );
            hsUpdateScore();
            hsState = HS_STATE_PLAYING;
        }
    }
    else if( hsState == HS_STATE_PLAYING )
    {
        int phaseBefore = hsCavePhase;
        // hsCavePhase must wrap at 256 like upstream's uint8_t (relied on
        // for the implicit 8-bit overflow) - a plain int here would just
        // grow forever, so "phaseBefore == 0" (once-per-lap death check /
        // difficulty ramp) would only ever fire on the very first frame,
        // and hsUpdateCave()'s "last 12 frames of the lap" warning-tone
        // condition would go permanently true forever the first time phase
        // crosses 244 instead of pulsing once per lap.
        hsCavePhase = ( hsCavePhase + 1 ) & 0xFF;
        if( phaseBefore == 0 )
        {
            if( diff < HS_PLAYER_H / 2 )
            {
                setSprite( 1, ( WIDTH - HS_IMG_GAMEOVER_W ) / 2, HEIGHT - HS_IMG_GAMEOVER_H, hsImgGameOver,
                           HS_IMG_GAMEOVER_W, HS_IMG_GAMEOVER_H, DIRECT );
                playScore( hsSoundOver );
                hsState = HS_STATE_OVER;
                hsCounter = HS_FPS * 8;
            }
            if( hsCaveGapMax < HS_CAVE_GAPMAX_MAX )
              hsCaveGapMax++;
        }
    }
    else if( hsState == HS_STATE_OVER )
    {
        hsCavePhase = ( hsCavePhase + ( hsCounter & 1 ) ) & 0xFF;
        if( isButtonDown( A_BUTTON ) )
        {
            hsInitGame();
        }
        else
        {
            hsCounter--;
            if( hsCounter == 0 )
              ret = HS_MODE_TITLE;
        }
    }

    isInvalid = true;
    return ret;
}

void hsDrawGame( int y, int* pBuffer )
{
    int col = hsDivByColumnW( hsCaveOffset );
    int odd = hsModByColumnW( hsCaveOffset );
    int shiftTop = hsCaveBaseTop & 7;
    int shiftBottom = hsCaveBaseBottom & 7;
    int caveTop = 0;
    int caveBottom = 0;
    int maskTop = 0;
    int maskBottom = 0;

    for( int x = 0; x < WIDTH; x++ )
    {
        if( x == 0 || odd == 0 )
        {
            caveTop = hsCaveColumn[ col ].top + hsCaveBaseTop - y;
            caveBottom = hsCaveColumn[ col ].bottom + hsCaveBaseBottom - y;

            if( caveTop < 8 )
              maskTop = 0xFF >> ( 8 - caveTop );
            else
              maskTop = 0xFF;

            if( caveBottom > 0 )
              maskBottom = ( 0xFF << caveBottom ) & 0xFF;
            else
              maskBottom = 0xFF;
        }

        HsColumn* pNeighbor = NULL;
        if( odd == 0 )
          pNeighbor = &hsCaveColumn[ circulate( col, -1, HS_COLUMNS ) ];
        else if( odd == 7 )
          pNeighbor = &hsCaveColumn[ circulate( col, 1, HS_COLUMNS ) ];

        int ptn = hsImgCave[ odd ];
        int destPixel = 0;

        // Top
        if( caveTop <= 0 )
        {
            destPixel = 0;
        }
        else
        {
            int edgeTop;
            if( pNeighbor != NULL )
            {
                edgeTop = pNeighbor->top + hsCaveBaseTop - y;
                if( caveTop < edgeTop ) edgeTop = caveTop;
                edgeTop--;
            }
            else
            {
                edgeTop = caveTop - 1;
            }

            // Vircon32's shift instruction wraps the shift count modulo 32
            // (confirmed empirically: (0xFF << 32) & 0xFF == 255, not 0) -
            // unlike AVR-GCC's codegen for a variable-count shift (a bit-by-
            // bit loop, which correctly saturates to 0 once the count
            // exceeds the value's width, no wraparound possible). edgeTop
            // routinely exceeds 8 here (a column's wall can be 30-40+ pixels
            // thick), so trusting the raw shift for out-of-range counts
            // produced a solid/near-solid byte instead of 0 whenever edgeTop
            // landed in the [32,39] (mod 32 = [0,7]) window - the reported
            // "white garbage blocks". Clamp explicitly instead of relying
            // on hardware behavior outside the 8-bit-meaningful range.
            int edgeTopPtn;
            if( edgeTop <= 0 )
              edgeTopPtn = 0xFF;
            else if( edgeTop >= 8 )
              edgeTopPtn = 0;
            else
              edgeTopPtn = ( 0xFF << edgeTop ) & 0xFF;

            int shiftedUp = ( ptn << shiftTop ) & 0xFF;
            int shiftedDown = ( ptn >> ( 8 - shiftTop ) ) & 0xFF;
            destPixel = ( shiftedUp | shiftedDown | edgeTopPtn ) & maskTop;
        }

        // Bottom
        if( caveBottom < 8 )
        {
            int edgeBottom;
            if( pNeighbor != NULL )
            {
                edgeBottom = pNeighbor->bottom + hsCaveBaseBottom - y;
                if( caveBottom > edgeBottom ) edgeBottom = caveBottom;
                edgeBottom++;
            }
            else
            {
                edgeBottom = caveBottom + 1;
            }

            // Same shift-wraparound hazard as edgeTopPtn above, mirrored for
            // the right-shift case (amount = 8-edgeBottom, which is >=8
            // whenever edgeBottom<=0 - i.e. this row-band is nowhere near
            // the bottom wall's edge, so the pattern should be all-zero).
            int edgeBottomPtn;
            if( edgeBottom >= 8 )
              edgeBottomPtn = 0xFF;
            else if( edgeBottom <= 0 )
              edgeBottomPtn = 0;
            else
              edgeBottomPtn = 0xFF >> ( 8 - edgeBottom );

            int shiftedUp = ( ptn << shiftBottom ) & 0xFF;
            int shiftedDown = ( ptn >> ( 8 - shiftBottom ) ) & 0xFF;
            destPixel |= ( shiftedUp | shiftedDown | edgeBottomPtn ) & maskBottom;
        }

        pBuffer[ x ] = destPixel & 0xFF;

        odd++;
        if( odd == HS_COLUMN_W )
        {
            col = ( col + 1 ) % HS_COLUMNS;
            odd = 0;
        }
    }
}

// -----------------------------------------------------------------------------
//   Top-level dispatch (replaces common.cpp's setup()/loop())
// -----------------------------------------------------------------------------

void gameHollowSeeker_init()
{
    initCore();
    hsMode = HS_MODE_LOGO;
    hsInitLogo();
}

void gameHollowSeeker_update()
{
    // Upstream ran its whole logic+redraw loop at a fixed 30fps (see
    // HS_FPS's own comment) - only tick once every HS_TICK_DIVISOR real
    // frames to match; the skipped frames just leave the previous frame's
    // image on screen (no draw call happens for them).
    hsTickSkipCounter++;
    if( hsTickSkipCounter < HS_TICK_DIVISOR )
      return;
    hsTickSkipCounter = 0;

    updateButtonState( HS_TICK_DIVISOR );

    int nextMode = HS_MODE_LOGO;
    if( hsMode == HS_MODE_LOGO )
      nextMode = hsUpdateLogo();
    else if( hsMode == HS_MODE_TITLE )
      nextMode = hsUpdateTitle();
    else
      nextMode = hsUpdateGame();

    DrawFunc* drawFn = NULL;
    if( hsMode == HS_MODE_GAME )
      drawFn = &hsDrawGame;
    refreshScreen( drawFn );

    if( hsMode != nextMode )
    {
        hsMode = nextMode;
        initSprites();
        initStrings();

        if( hsMode == HS_MODE_LOGO )
          hsInitLogo();
        else if( hsMode == HS_MODE_TITLE )
          hsInitTitle();
        else
          hsInitGame();
    }
}

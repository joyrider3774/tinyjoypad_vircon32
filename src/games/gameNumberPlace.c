// =============================================================================
// NumberPlace (Sudoku) - ported from Obono's TinyJoypadWorks/numberplace
// (https://github.com/obono/TinyJoypadWorks/tree/main/numberplace), MIT
// License, Copyright (c) 2020-2025 OBONO.
//
// Merges the upstream common.h/common.cpp/logo.cpp/title.cpp/game.cpp/
// data.h into one file (Vircon32 has no linker - see VIRCON32_C_DIALECT.md
// section 11) and ports it onto obonoCoreShim.h (which reproduces the
// original core.h/core.cpp sprite/string engine on top of Vircon32's real
// video/input/audio - see that file). Every file-scope name is prefixed
// np*/NP_* to avoid collisions with other games sharing this cartridge's
// single translation unit (VIRCON32_C_DIALECT.md section 17.1).
//
// Structural changes from upstream, beyond dialect fixes (array-declaration
// syntax, no bitfields, no scoped/underlying-type enums, no ternary, no
// function overloading - setString's two overloads collapse to one since
// avrCompat.h aliases __FlashStringHelper to plain int):
//  - NP_FPS matches upstream's own real rate (20, not Vircon32's native
//    60fps) - gameNumberPlace_update() only actually ticks once every
//    NP_TICK_DIVISOR (3) real Vircon32 frames, so every frame-counted
//    duration (press-and-hold thresholds, dpad repeat delay) still
//    measures the same real-world time it did upstream. An earlier
//    revision of this port ran every real frame at NP_FPS=60 instead
//    (faster than upstream) before this session's frame-pacing pass
//    added the throttle - see CLAUDE.md's "Frame-pacing pass" section.
//  - Memory-card (EEPROM) persistence is not wired up yet - see
//    obonoCoreShim.h. Nothing in this game currently calls
//    loadRecord()/storeRecord() itself, so this only matters if that's
//    added later.
// =============================================================================

// avrCompat.h/obonoCoreShim.h/misc.h are already included earlier by
// main.c before this file - see VIRCON32_C_DIALECT.md section 11 (single
// translation unit, no linker).

// Upstream (TinyJoypadWorks/numberplace/common.h) ran its own fixed-
// timestep loop at a real FPS=20 - this port now reproduces that by only
// actually ticking its own logic once every NP_TICK_DIVISOR real Vircon32
// frames (see gameNumberPlace_update()), so NP_FPS must track the LOGIC
// tick rate (20), not the engine's 60fps display rate, or every timer here
// derived from it (npCounter, NP_DPAD_REPEAT_DELAY, NP_SHORT_PRESS,
// NP_LONG_PRESS) would silently run 3x too long in real time.
#define NP_FPS 20
#define NP_TICK_DIVISOR ( 60 / NP_FPS )

// -----------------------------------------------------------------------------
//   common.h / common.cpp
// -----------------------------------------------------------------------------

enum NpMode
{
    NP_MODE_LOGO = 0,
    NP_MODE_TITLE,
    NP_MODE_GAME
};

enum NpLevel
{
    NP_LEVEL_EASY = 0,
    NP_LEVEL_MEDIUM,
    NP_LEVEL_HARD,
    NP_LEVEL_EXPERT,
    NP_LEVEL_MAX
};

int npMode = NP_MODE_LOGO;
int npTickSkipCounter = 0;
int npCounter;
int npPuzzleSeed;
int npDpadX, npDpadY;
int npLevel;
int npDpadCounter = 0;

#define NP_DPAD_BUTTONS ( LEFT_BUTTON | RIGHT_BUTTON | DOWN_BUTTON | UP_BUTTON )
#define NP_DPAD_REPEAT_DELAY ( NP_FPS / 4 )

// Arduino-style random()/randomSeed() built on Vircon32's real rand()/
// srand() (misc.h) - only the single-argument random(n) form is used
// anywhere in this port.
int npRandom( int n )
{
    return arand( n );
}

void npHandleDpad()
{
    npDpadX = isButtonPressed( RIGHT_BUTTON ) - isButtonPressed( LEFT_BUTTON );
    npDpadY = isButtonPressed( DOWN_BUTTON ) - isButtonPressed( UP_BUTTON );

    if( isButtonPressed( NP_DPAD_BUTTONS ) )
    {
        if( npDpadCounter < NP_DPAD_REPEAT_DELAY )
        {
            npDpadCounter++;
            if( npDpadCounter > 1 )
            {
                npDpadX = 0;
                npDpadY = 0;
            }
        }
        else
        {
            npDpadX = 0;
            npDpadY = 0;
        }
    }
    else
    {
        npDpadCounter = 0;
    }
}

void npPlaySoundTick()
{
    playTone( 440, 10 );
}

void npPlaySoundClick()
{
    playTone( 587, 20 );
}

// -----------------------------------------------------------------------------
//   data.h
// -----------------------------------------------------------------------------

#define NP_IMG_DIGIT_W 4
#define NP_IMG_DIGIT_H 7

int[10][4] npImgDigit =
{
    { 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x02, 0x7E, 0x00 }, { 0x44, 0x62, 0x52, 0x4C }, { 0x42, 0x4A, 0x4A, 0x34 },
    { 0x30, 0x28, 0x24, 0x7E }, { 0x4E, 0x4A, 0x4A, 0x32 }, { 0x3C, 0x4A, 0x4A, 0x30 },
    { 0x06, 0x02, 0x72, 0x0E }, { 0x34, 0x4A, 0x4A, 0x34 }, { 0x0C, 0x12, 0x52, 0x3C }
};

#define NP_IMG_CURSOR_W 8
#define NP_IMG_CURSOR_H 8

int[8] npImgCursor = { 0xff, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xff };

#define NP_IMG_STATE_W 12
#define NP_IMG_STATE_H 16

int[2][24] npImgState =
{
    {
        0xC0, 0x30, 0x08, 0xC8, 0x84, 0x04, 0x84, 0xC4, 0xE0, 0x30, 0x18, 0xC4,
        0x03, 0x0C, 0x10, 0x10, 0x21, 0x27, 0x27, 0x23, 0x10, 0x10, 0x0C, 0x03
    },
    {
        0x00, 0x00, 0x80, 0xE0, 0xF8, 0x0C, 0x0C, 0xF8, 0xE0, 0x80, 0x00, 0x00,
        0x18, 0x3E, 0x3F, 0x3F, 0x3F, 0x24, 0x24, 0x3F, 0x3F, 0x3F, 0x3E, 0x18
    }
};

#define NP_IMG_TOOL_W 13
#define NP_IMG_TOOL_H 16

int[2][26] npImgTool =
{
    {
        0x00, 0x00, 0xFF, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xFF, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x07, 0x0E, 0x1E, 0x2E, 0x2E, 0x1E, 0x0E, 0x07, 0x00, 0x00, 0x00
    },
    {
        0x00, 0x80, 0xC0, 0x60, 0xB0, 0x58, 0xAC, 0x56, 0xAC, 0x58, 0x30, 0xA0, 0x40,
        0x03, 0x07, 0x0F, 0x1F, 0x3E, 0x3D, 0x1A, 0x09, 0x04, 0x02, 0x01, 0x00, 0x00
    }
};

#define NP_IMG_LOCKED_W 8
#define NP_IMG_LOCKED_H 8

int[8] npImgLocked = { 0xF8, 0xFE, 0xF9, 0x99, 0xD9, 0xF9, 0xFE, 0xF8 };

#define NP_IMG_SELECTING_W 20
#define NP_IMG_SELECTING_H 8

int[20] npImgSelecting =
{
    0x40, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x40
};

int[11] npSoundStart  = { 72, 12, 74, 12, 76, 12, 77, 12, 79, 36, 0xFF };
int[11] npSoundPlace  = { 90, 1, 91, 1, 92, 1, 93, 1, 94, 1, 0xFF };
int[11] npSoundRemove = { 90, 1, 89, 1, 88, 1, 87, 1, 86, 1, 0xFF };
int[13] npSoundFailure = { 87, 3, 75, 3, 63, 3, 87, 3, 75, 3, 63, 3, 0xFF };
int[19] npSoundComplete =
{
    104, 3, 92, 12, 104, 6, 92, 9, 104, 9, 92, 6, 104, 12, 92, 3, 104, 15, 0xFF
};

#define NP_PUZZLES_PER_LEVEL 10

// Difficulty-level labels, packed 7 units per entry (6 characters + a
// terminator) so `&npLevelString[level * 7]` addresses the right one
// directly - identical layout to upstream's levelString[].
int[28] npLevelString =
{
    ' ','E','A','S','Y',' ',0,
    'M','E','D','I','U','M',0,
    ' ','H','A','R','D',' ',0,
    'E','X','P','E','R','T',0
};

// Sudoku puzzle definitions (10 per difficulty x 4 difficulties), each row
// packed as base-10 digits - functional puzzle data, ported unchanged from
// data.h's puzzleData[].
int[40][9] npPuzzleData =
{
    {        36,   500000,980100000, 20307004,400912870,103608095,506030048,  2895361, 10476952 },
    {      5003,610040000,    80070, 61200540,705009000,392408160,  6513794, 79820035,403796801 },
    {     60500, 93024000, 80000000,  7600020,806010370,  9450108,948271603,765839041,321540709 },
    {    340000,  5260480,  3000000,  1802304,709001568,800070019,290150607,357920841,406703925 },
    {   1000000,   906000, 40000082,706508423,  9710060,450023090,893460271,527180300,164007908 },
    {   3090800,704603509,100400000,805000492,200050030, 36074185,360700910,497361208,512008376 },
    {   6380900, 90004000, 12000530,  9207061,200068003,687150400,720605004,105439280,934870015 },
    {   9058000, 30006004,  2100000, 40307605, 20800040,300001789,861572090,573409162,294013008 },
    {  20900000,600040010,    10079, 98720001,347180025,  6530784,  3050196,462300857,951608243 },
    {  45000000,   900207,    68000,208000003,379054160,504021000,407519020,903276810,601483759 },
    {         0,   500012,508006000,210000600,300000000,     4000,  4300000,  9000740,    20000 },
    {         1, 80000027,   304000,  6000000,    70000,309000500,  4000000, 70020000,   900360 },
    {       600, 98002000,  3000700,     8009,650010000,        0,700560000,      138,        0 },
    {      5100,  7190050,  6024000,800001204,600000035,   300010, 41000009,290070000,     9070 },
    {     30090,        0, 28000000,900000000,   804000,600007050,500390000,    60000,  4000802 },
    {     98200,700005106, 50100008,807300000,     1085, 10000600, 70520000, 91700020,  2004800 },
    {    408030,  2060000, 70000000,    20007,        0,850000000,   570000,300000089,  9010000 },
    {    830004,  7000000, 51000000,      300,   301700,600002000,800000090,        6,    75002 },
    {   3402000,800000750,   900000,610070000,900000024,        0,  2000000,    60100,     5000 },
    {   7000000, 63000000,   920004,500008000,       90,    93070,200000100,        5,   706008 },
    {         0, 70530000,  1000090,600000503, 40208000,        7,500000000,  6009000,     4020 },
    {        31,507090000,  8000000,    80700,260000000,    30040,  9000500,   600004,   201000 },
    {       900,  9014000,500000060,    80000,   560007,  2000300,600000000,     2409,800000000 },
    {      7208,   500900, 70901600,  8402090, 40170800,200000005,  3600000,  6710020,105090000 },
    {     60004,302007001,800000002,        0,290003780,  3400000,601500000,  8000010,905000230 },
    {    270900,100000000,406000080,   920000,        0,800006004, 70000000, 50000200,     4001 },
    {    705900,700000000,340000020,     9000,200000003, 17008000,    30000,    40000,  9000800 },
    {   3060000,       95, 90000004, 70080000,    30800, 40000000, 50900000,  2004000,  8000600 },
    {   6400000,  3000021,        9,100000000,   600800,500800007,  8000400,900021000,        0 },
    {  10008000, 40000000,   500002,     4100,  7000006,  2009000, 80000500,  9200000,   700400 },
    {       800,  3154006,210908040,500000001,   605000,   783000, 30071062,  6000000,900236700 },
    {      9050,        0,410600000,     3000,   400200,  7000080,  9057000,  2000406,        1 },
    {     80000, 31000058, 54300700, 10900670,795003800,    70000, 80001204,    20000,602000305 },
    {    490000,503000700,        0,     5300,940080000,700000000,    60040,200003000,       98 },
    {    904003,     1250,100072009, 19000506,700200000,     3090,507600000, 80710060, 60305000 },
    {   4000000,  8001000,    30500, 50097000,     8040, 20000000, 90050000,    20060,       81 },
    {   6801000, 27905600, 85070000,        9, 90000000,608000732,   700405,  4019206,    48900 },
    {   9000001,100024060, 68300700,800092050, 91035008,500078090,910543080, 40280500, 85000004 },
    {  20000006,  8030200,100702040,800070020, 70148005,400060070, 50000002,  4020900,200907010 },
    {  40000050,    80000,   700000, 30004000,     5809,      600,820000030,   409070,  6000000 }
};

// -----------------------------------------------------------------------------
//   logo.cpp
// -----------------------------------------------------------------------------

#define NP_IMG_LOGO_W 35
#define NP_IMG_LOGO_H 16

int[70] npImgLogo =
{
    0x00, 0x80, 0xC0, 0x60, 0x20, 0xE0, 0x20, 0x20, 0x40, 0x80, 0x00, 0x00, 0xFF, 0x57, 0xAB, 0x57,
    0x01, 0xFF, 0x20, 0x20, 0x40, 0x80, 0x00, 0x00, 0x00, 0x80, 0xC0, 0x60, 0x20, 0xE0, 0x20, 0x20,
    0x40, 0x80, 0x00, 0x1F, 0x37, 0x6A, 0x95, 0xA8, 0x97, 0xAC, 0x94, 0x4C, 0x24, 0x1F, 0x00, 0x1F,
    0x35, 0x6A, 0x95, 0xA8, 0x97, 0xAC, 0x94, 0x4C, 0x24, 0x1F, 0x00, 0xFF, 0x97, 0xAA, 0x95, 0x80,
    0xBF, 0xAC, 0x94, 0xAC, 0x84, 0xFF
};

void npInitLogo()
{
    setSprite( 0, ( WIDTH - NP_IMG_LOGO_W ) / 2, ( HEIGHT - NP_IMG_LOGO_H ) / 2, npImgLogo,
               NP_IMG_LOGO_W, NP_IMG_LOGO_H, DIRECT );
    setString( 8, 86, "OBN-T01", WHITE );
    setString( 9, 104, "V0.1", WHITE );
    npCounter = NP_FPS * 2;
    isInvalid = true;
}

int npUpdateLogo()
{
    npCounter--;
    if( npCounter > 0 )
      return NP_MODE_LOGO;
    return NP_MODE_TITLE;
}

// -----------------------------------------------------------------------------
//   title.cpp
// -----------------------------------------------------------------------------

#define NP_TITLE_IMG_CURSOR_W 5
#define NP_TITLE_IMG_CURSOR_H 8
#define NP_TITLE_IMG_CURSOR_TOP 48

#define NP_TITLE_SPR_ID_TITLE 0
#define NP_TITLE_SPR_ID_CURSOR 1

int[128] npTitleImgTitle =
{
    0xFF, 0xFF, 0xF8, 0xC0, 0xFF, 0xFF, 0x00, 0xF8, 0xF8, 0x00, 0xF8, 0xF8, 0x00, 0xF8, 0xF8, 0x18,
    0xF8, 0xF8, 0x18, 0xF8, 0xF0, 0x00, 0xFF, 0xFF, 0x18, 0xF8, 0xF0, 0x00, 0xF0, 0xF8, 0x18, 0xF8,
    0xF0, 0x00, 0xF8, 0xF8, 0x38, 0x00, 0xFF, 0xFF, 0x03, 0xFF, 0xFE, 0x00, 0xFF, 0xFF, 0x00, 0x70,
    0x78, 0x18, 0xF8, 0xF0, 0x00, 0xF0, 0xF8, 0x18, 0xF8, 0xF0, 0x00, 0xF0, 0xF8, 0x18, 0xF8, 0xF0,
    0xFF, 0xFF, 0x03, 0x1F, 0xFF, 0xFF, 0x00, 0x7F, 0xFF, 0xC0, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x00,
    0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xC0, 0xFF, 0x7F, 0x00, 0x7F, 0xFF, 0xC3, 0xF3,
    0x73, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x06, 0x07, 0x03, 0x00, 0xFF, 0xFF, 0x00, 0x7C,
    0xFE, 0xC3, 0xFF, 0xFF, 0x00, 0x7F, 0xFF, 0xC0, 0xF0, 0x70, 0x00, 0x7F, 0xFF, 0xC3, 0xF3, 0x73
};

int[5] npTitleImgCursor = { 0x38, 0x7C, 0x7C, 0x7C, 0x38 };

void npInitTitle()
{
    npLevel = NP_LEVEL_MEDIUM;
    setSprite( NP_TITLE_SPR_ID_TITLE, 32, 24, npTitleImgTitle, 64, 16, DIRECT );
    setSprite( NP_TITLE_SPR_ID_CURSOR, 43, NP_TITLE_IMG_CURSOR_TOP, npTitleImgCursor,
               NP_TITLE_IMG_CURSOR_W, NP_TITLE_IMG_CURSOR_H, DIRECT );
    setString( 8, 13, "EASY  MEDIUM  HARD", WHITE );
    isInvalid = true;
}

int npUpdateTitle()
{
    int ret = NP_MODE_TITLE;
    npHandleDpad();

    if( npDpadX != 0 )
    {
        npLevel = circulate( npLevel, npDpadX, ( NP_LEVEL_MAX - 1 ) );
        int notZero = 1;
        if( npLevel == 0 )
          notZero = 0;
        int cursorX = npLevel * 48 + ( 1 - notZero ) * 12 - 5;
        moveSprite( NP_TITLE_SPR_ID_CURSOR, cursorX, NP_TITLE_IMG_CURSOR_TOP );
        npPlaySoundTick();
        isInvalid = true;
        rand(); // shuffle random, matches upstream's stirring of the RNG
    }

    if( isButtonDown( A_BUTTON ) )
    {
        if( npLevel == NP_LEVEL_HARD && isButtonPressed( UP_BUTTON ) )
          npLevel = NP_LEVEL_EXPERT; // secret!

        ret = NP_MODE_GAME;
    }

    npPuzzleSeed = rand();
    srand( npPuzzleSeed ); // shuffle random

    return ret;
}

// -----------------------------------------------------------------------------
//   game.cpp
// -----------------------------------------------------------------------------

#define NP_BOARD_SIZE   9
#define NP_SECTION_SIZE 3
#define NP_SECTIONS     ( NP_BOARD_SIZE / NP_SECTION_SIZE )
#define NP_DEFAULT_POS  ( NP_BOARD_SIZE / 2 )

#define NP_DIGITS         ( NP_BOARD_SIZE + 1 )
#define NP_DEFAULT_NUMBER 1

#define NP_CELL_PX    7
#define NP_SECTION_PX ( NP_SECTION_SIZE * NP_CELL_PX )
#define NP_BOARD_PX   ( NP_BOARD_SIZE * NP_CELL_PX )
#define NP_BOARD_LEFT 28

#define NP_SHORT_PRESS ( NP_FPS / 2 )
#define NP_LONG_PRESS  ( NP_FPS * 3 )

#define NP_SPR_ID_STATE  0
#define NP_SPR_ID_TOOL   1
#define NP_SPR_ID_NUMBER 2
#define NP_SPR_ID_CURSOR 3
#define NP_SPR_ID_LOCKED 4

#define NP_SEGMENT_COLUMN  0
#define NP_SEGMENT_ROW     1
#define NP_SEGMENT_SECTION 2
#define NP_SEGMENT_MAX     3

struct NpCell
{
    int number;
    int locked;
};

#define npGetUnitIndex(x, y) ( (y) / NP_SECTION_SIZE * NP_SECTION_SIZE + (x) / NP_SECTION_SIZE )

NpCell[NP_BOARD_SIZE][NP_BOARD_SIZE] npBoard;
int[NP_SEGMENT_MAX][NP_BOARD_SIZE] npBoardState;
int npBoardFailure;
int npSelectedNumber = NP_DEFAULT_NUMBER;
int npCursorX = NP_DEFAULT_POS;
int npCursorY = NP_DEFAULT_POS;
int npPlacedCount, npPresetCount;
bool npIsSelecting;

int npPowDigits( int n )
{
    int q = 1;
    while( --n > 0 )
      q *= NP_DIGITS;
    return q;
}

void npUpdateState( int x, int y, int n, bool isPlace )
{
    int q = npPowDigits( n );

    for( int segment = 0; segment < NP_SEGMENT_MAX; segment++ )
    {
        int idx = x;
        if( segment == NP_SEGMENT_SECTION )
          idx = npGetUnitIndex( x, y );
        else if( segment == NP_SEGMENT_ROW )
          idx = y;

        if( isPlace )
          npBoardState[ segment ][ idx ] += q;
        else
          npBoardState[ segment ][ idx ] -= q;

        int a = npBoardState[ segment ][ idx ] / q % NP_DIGITS;
        int b = segment * NP_BOARD_SIZE + idx;

        if( isPlace && a >= 2 )
        {
            playScore( npSoundFailure );
            bitSet( npBoardFailure, b );
        }
        else if( !isPlace && a <= 1 )
        {
            bitClear( npBoardFailure, b );
        }
    }
}

void npRemoveNumber( int x, int y )
{
    int n = npBoard[ y ][ x ].number;
    if( n == 0 )
      return;

    npBoard[ y ][ x ].number = 0;
    npPlacedCount--;
    npUpdateState( x, y, n, false );
}

void npPlaceNumber( int x, int y, int n )
{
    npRemoveNumber( x, y );
    npBoard[ y ][ x ].number = n;
    npPlacedCount++;
    npUpdateState( x, y, n, true );
}

void npInitPuzzle()
{
    srand( npPuzzleSeed );

    int[NP_SEGMENT_MAX][NP_BOARD_SIZE] table;

    for( int segment = 0; segment < NP_SEGMENT_MAX; segment++ )
    {
        int n1 = npRandom( NP_SECTIONS ) + NP_SECTIONS;
        int v1 = npRandom( 2 ) * 2 - 1;

        for( int j = 0; j < NP_SECTIONS; j++ )
        {
            int n2 = npRandom( NP_SECTION_SIZE ) + NP_SECTION_SIZE;
            int v2 = npRandom( 2 ) * 2 - 1;

            for( int k = 0; k < NP_SECTION_SIZE; k++ )
            {
                table[ segment ][ j * NP_SECTION_SIZE + k ] =
                    n1 % NP_SECTIONS * NP_SECTION_SIZE + n2 % NP_SECTION_SIZE;
                n2 += v2;
            }

            n1 += v1;
        }
    }

    for( int y = 0; y < NP_BOARD_SIZE; y++ )
      for( int x = 0; x < NP_BOARD_SIZE; x++ )
      {
          npBoard[ y ][ x ].number = 0;
          npBoard[ y ][ x ].locked = 0;
      }

    for( int segment = 0; segment < NP_SEGMENT_MAX; segment++ )
      for( int i = 0; i < NP_BOARD_SIZE; i++ )
        npBoardState[ segment ][ i ] = 0;

    npPlacedCount = 0;
    npBoardFailure = 0;

    int puzzleIndex = npRandom( NP_PUZZLES_PER_LEVEL ) + npLevel * NP_PUZZLES_PER_LEVEL;

    for( int y = 0; y < NP_BOARD_SIZE; y++ )
    {
        int rowData = npPuzzleData[ puzzleIndex ][ y ];

        for( int x = 0; x < NP_BOARD_SIZE; x++ )
        {
            int n = rowData % NP_DIGITS;
            if( n > 0 )
            {
                int bx = table[ NP_SEGMENT_COLUMN ][ x ];
                int by = table[ NP_SEGMENT_ROW ][ y ];
                npPlaceNumber( bx, by, table[ NP_SEGMENT_SECTION ][ n - 1 ] + 1 );
                npBoard[ by ][ bx ].locked = true;
            }
            rowData /= NP_DIGITS;
        }
    }

    npPresetCount = npPlacedCount;
}

void npSetToolSprite()
{
    int toolIndex = 1;
    if( npSelectedNumber != 0 )
      toolIndex = 0;

    setSprite( NP_SPR_ID_TOOL, 104, 24, npImgTool[ toolIndex ], NP_IMG_TOOL_W, NP_IMG_TOOL_H, DIRECT );
    setSprite( NP_SPR_ID_NUMBER, 108, 25, npImgDigit[ npSelectedNumber ], NP_IMG_DIGIT_W, NP_IMG_DIGIT_H, WHITE );
}

void npSetCursorSprite()
{
    setSprite( NP_SPR_ID_CURSOR, NP_BOARD_LEFT + npCursorX * NP_CELL_PX, npCursorY * NP_CELL_PX,
               npImgCursor, NP_IMG_CURSOR_W, NP_IMG_CURSOR_H, WHITE );

    int* lockedImg = NULL;
    if( npBoard[ npCursorY ][ npCursorX ].locked )
      lockedImg = npImgLocked;

    setSprite( NP_SPR_ID_LOCKED, 106, 40, lockedImg, NP_IMG_LOCKED_W, NP_IMG_LOCKED_H, DIRECT );
}

void npSetSelectingSprite()
{
    setSprite( NP_SPR_ID_CURSOR, 100, 24, npImgSelecting, NP_IMG_SELECTING_W, NP_IMG_SELECTING_H, WHITE );
}

void npInitGame()
{
    npInitPuzzle();
    npIsSelecting = false;
    npCounter = NP_LONG_PRESS;

    npSetToolSprite();
    npSetCursorSprite();
    setString( 1, 93, &npLevelString[ npLevel * 7 ], WHITE );

    playScore( npSoundStart );
    isInvalid = true;
}

int npUpdateGame()
{
    npHandleDpad();
    bool isShortPress = false;

    if( isButtonPressed( A_BUTTON ) )
    {
        if( npCounter < NP_LONG_PRESS )
        {
            npCounter++;
            if( npCounter == NP_LONG_PRESS && npPlacedCount > npPresetCount )
            {
                clearSprite( NP_SPR_ID_STATE );
                npInitGame();
            }
        }
    }
    else
    {
        if( npCounter > 0 && npCounter < NP_SHORT_PRESS )
          isShortPress = true;
        npCounter = 0;
    }

    if( npIsSelecting )
    {
        if( npDpadX != 0 )
        {
            npSelectedNumber = circulate( npSelectedNumber, npDpadX, NP_DIGITS );
            npSetToolSprite();
            npPlaySoundTick();
            isInvalid = true;
        }
        if( isButtonDown( A_BUTTON ) )
        {
            npIsSelecting = false;
            npCounter = NP_LONG_PRESS;
            npPlaySoundClick();
            isInvalid = true;
        }
    }
    else
    {
        if( npDpadX != 0 || npDpadY != 0 )
        {
            npCursorX = circulate( npCursorX, npDpadX, NP_BOARD_SIZE );
            npCursorY = circulate( npCursorY, npDpadY, NP_BOARD_SIZE );
            npPlaySoundTick();
            isInvalid = true;
        }

        if( isShortPress )
        {
            if( npBoard[ npCursorY ][ npCursorX ].locked )
            {
                npPlaySoundClick();
            }
            else
            {
                int n = npBoard[ npCursorY ][ npCursorX ].number;
                if( npSelectedNumber > 0 && n != npSelectedNumber )
                {
                    playScore( npSoundPlace );
                    npPlaceNumber( npCursorX, npCursorY, npSelectedNumber );
                }
                else if( ( npSelectedNumber == 0 && n > 0 ) ||
                         ( npSelectedNumber > 0 && n == npSelectedNumber ) )
                {
                    playScore( npSoundRemove );
                    npRemoveNumber( npCursorX, npCursorY );
                }

                int* stateImg = NULL;
                if( npBoardFailure != 0 )
                {
                    stateImg = npImgState[ 1 ];
                }
                else if( npPlacedCount == NP_BOARD_SIZE * NP_BOARD_SIZE )
                {
                    stateImg = npImgState[ 0 ];
                    playScore( npSoundComplete );
                }
                setSprite( NP_SPR_ID_STATE, 8, 24, stateImg, NP_IMG_STATE_W, NP_IMG_STATE_H, DIRECT );
                isInvalid = true;
            }
        }

        if( npCounter == NP_SHORT_PRESS )
        {
            npIsSelecting = true;
            npSetSelectingSprite();
            npPlaySoundClick();
            isInvalid = true;
        }
    }

    if( !npIsSelecting )
      npSetCursorSprite();

    return NP_MODE_GAME;
}

void npDrawGame( int y, int* pBuffer )
{
    clearScreenBuffer();

    int b = 7 - ( y + 7 ) % NP_SECTION_PX;
    if( b >= 0 )
    {
        int fillValue = bit( b );
        for( int i = 0; i < NP_BOARD_PX - 1; i++ )
          pBuffer[ NP_BOARD_LEFT + 1 + i ] = fillValue;
    }
    for( int x = NP_BOARD_LEFT; x <= NP_BOARD_LEFT + NP_BOARD_PX; x += NP_SECTION_PX )
      pBuffer[ x ] = 0xFF;

    int by = y >> 3;
    for( int bx = 0; bx < NP_BOARD_SIZE; bx++ )
    {
        int n1 = npBoard[ by ][ bx ].number;
        int n2 = npBoard[ by + 1 ][ bx ].number;
        int destBase = NP_BOARD_LEFT + 2 + bx * NP_CELL_PX;
        int* pImg1 = npImgDigit[ n1 ];
        int* pImg2 = npImgDigit[ n2 ];

        for( int w = 0; w < NP_IMG_DIGIT_W; w++ )
        {
            pBuffer[ destBase ] |= ( pImg1[ w ] >> by ) | ( pImg2[ w ] << ( NP_CELL_PX - by ) );
            destBase++;
        }
    }
}

// -----------------------------------------------------------------------------
//   Top-level dispatch (replaces common.cpp's setup()/loop())
// -----------------------------------------------------------------------------

void gameNumberPlace_init()
{
    initCore();
    npMode = NP_MODE_LOGO;
    npInitLogo();
}

void gameNumberPlace_update()
{
    // Upstream ran its whole logic+redraw loop at a fixed 20fps (see
    // NP_FPS's own comment) - only tick once every NP_TICK_DIVISOR real
    // frames to match, leaving the previous frame's image on screen for
    // the skipped ones (the same "just don't call the draw path" trick
    // this project's dirty-flag caching already relies on elsewhere).
    npTickSkipCounter++;
    if( npTickSkipCounter < NP_TICK_DIVISOR )
      return;
    npTickSkipCounter = 0;

    updateButtonState( NP_TICK_DIVISOR );

    int nextMode = NP_MODE_LOGO;
    if( npMode == NP_MODE_LOGO )
      nextMode = npUpdateLogo();
    else if( npMode == NP_MODE_TITLE )
      nextMode = npUpdateTitle();
    else
      nextMode = npUpdateGame();

    DrawFunc* drawFn = NULL;
    if( npMode == NP_MODE_GAME )
      drawFn = &npDrawGame;
    refreshScreen( drawFn );

    if( npMode != nextMode )
    {
        npMode = nextMode;
        initSprites();
        initStrings();

        if( npMode == NP_MODE_LOGO )
          npInitLogo();
        else if( npMode == NP_MODE_TITLE )
          npInitTitle();
        else
          npInitGame();
    }
}

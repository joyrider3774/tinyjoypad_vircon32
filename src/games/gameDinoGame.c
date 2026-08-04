// =============================================================================
// Dino Game - ported from dino-game.ino, an ORIGINAL creation of the
// Yevgeniy-Olexandrenko/tiny-handheld repo itself (not a port of an
// existing TinyJoypad/AttinyArcade game) - a Chrome-offline-dino-style
// endless runner: jump over cacti and dodge scattered "rocks" on the
// ground line, score climbs over time and the game speeds up every
// level. Fourth beyond-scope addition, and the first game in this whole
// project built on the real `lexus2k/ssd1306` Arduino library (sprite +
// text APIs) instead of either tinyJoypadShim/obonoCoreShim or a hand-
// rolled raw-byte driver - the library itself isn't bundled in this
// repo, so its own internal implementation was never read; instead this
// port infers exact pixel output directly from the game's own draw
// calls (positions, widths, per-column byte data), which turned out to
// be entirely sufficient - see the structural notes below.
//
// Structural notes:
//  - `drawGround()` already streams one raw byte per column via
//    `ssd1306_lcd.send_pixels1(bits)` - the exact "one byte per (column,
//    page)" model this whole project's `md_drawColumn()` already
//    handles, so the ground/rock layer needed no reinterpretation at all.
//  - The sprite API (`ssd1306_createSprite`/`replaceSprite`/`draw`/
//    `eraseTrace`) is a real-hardware optimization (track a sprite's
//    last-drawn position, erase just that rectangle before drawing the
//    new one, instead of clearing the whole screen) - this project's own
//    always-`clear_screen()`-then-redraw-everything model via
//    `md_beginFrame()` doesn't need any of that erase-tracking, so
//    `eraseTrace()` has no equivalent here at all - every sprite is just
//    composited fresh into the current frame from its own current
//    position, matching this project's own standing treatment of any
//    upstream "only redraw what changed" optimization.
//  - Both sprites (`dino_images[2][9]`, `cactus_image[8]`) are one page
//    (8 pixel rows) tall, stored one byte per column (standard SSD1306
//    convention, bit0=top) - but `dino.sprite.y` is a genuinely
//    arbitrary *pixel* Y (jump physics), not page-aligned, unlike every
//    cactus (`CACTUS_Y` is a fixed, page-aligned constant). Composited
//    with `dinoSpriteColByte()` - a single sign-branched shift of the
//    sprite's own column byte, the same safe sub-page-slicing technique
//    already proven in Run Dude Run's own `rddBombColByte()`, just
//    simpler here (one 8-bit source byte instead of two combined into
//    16 bits, since these sprites are only 8 px tall not 16).
//  - Text (`ssd1306_printFixed`, upstream's own `ssd1306xled_font5x7`)
//    reuses this project's own already-extracted standard 95-char
//    ssd1306xled font table (the identical font already ported for
//    Oroboros's/Run Dude Run's own text - confirmed the SAME file,
//    `font6x8.h`, is bundled in this repo too) rather than needing a
//    fresh extraction - each game keeps its own self-contained copy,
//    matching this project's own standing precedent (no cross-game-file
//    sharing mechanism exists here). `ssd1306_printFixed`'s own Y
//    parameter is pixel-based too (`25` for the mid-screen game-over/
//    start text), so text glyphs are composited with the exact same
//    `dinoSpriteColByte()` sub-page technique as the dino sprite.
//  - EEPROM high-score persistence dropped (session-in-memory only),
//    matching every other port's own precedent.
//  - Upstream's own `millis()`-based real-time tick throttle
//    (`FRAME_DURATION=14ms`, ~71fps - genuinely *faster* than this
//    engine's native 60fps, unlike every other port's own throttle
//    which has always been slower) was dropped entirely - the tick body
//    just runs once per real engine frame instead, a deliberate,
//    documented simplification (a ~15% pace difference, not the ~8x
//    seen in Arkanoid's own uncapped-loop bug) rather than trying to
//    build an accumulator for a throttle that would only ever ask for
//    *more* real ticks than this engine can natively provide anyway.
//  - `sampleFromExpDistribution()`'s own scan loop has a latent
//    uninitialized-read risk found by inspection: if `value` ever lands
//    exactly on the highest bucket edge, the loop completes without ever
//    setting `lg` - harmless-ish on real AVR (reads whatever garbage was
//    left on the stack), but not a read this project should trust on
//    Vircon32 - fixed with an explicit default (`lg=9`, the highest
//    bucket) before the scan starts, so it's always well-defined.
//  - `random(0,EXP10)` switched to the shared `arand(n)` helper, same
//    fix as every other port's own RNG replacement.
// =============================================================================

int[570] dinoFont =
{
0,0,0,0,0,0,0,0,0,47,0,0,0,0,7,0,
7,0,0,20,127,20,127,20,0,36,42,127,42,18,0,98,
100,8,19,35,0,54,73,85,34,80,0,0,5,3,0,0,
0,0,28,34,65,0,0,0,65,34,28,0,0,20,8,62,
8,20,0,8,8,62,8,8,0,0,0,160,96,0,0,8,
8,8,8,8,0,0,96,96,0,0,0,32,16,8,4,2,
0,62,81,73,69,62,0,0,66,127,64,0,0,66,97,81,
73,70,0,33,65,69,75,49,0,24,20,18,127,16,0,39,
69,69,69,57,0,60,74,73,73,48,0,1,113,9,5,3,
0,54,73,73,73,54,0,6,73,73,41,30,0,0,54,54,
0,0,0,0,86,54,0,0,0,8,20,34,65,0,0,20,
20,20,20,20,0,0,65,34,20,8,0,2,1,81,9,6,
0,50,73,89,81,62,0,124,18,17,18,124,0,127,73,73,
73,54,0,62,65,65,65,34,0,127,65,65,34,28,0,127,
73,73,73,65,0,127,9,9,9,1,0,62,65,73,73,122,
0,127,8,8,8,127,0,0,65,127,65,0,0,32,64,65,
63,1,0,127,8,20,34,65,0,127,64,64,64,64,0,127,
2,12,2,127,0,127,4,8,16,127,0,62,65,65,65,62,
0,127,9,9,9,6,0,62,65,81,33,94,0,127,9,25,
41,70,0,70,73,73,73,49,0,1,1,127,1,1,0,63,
64,64,64,63,0,31,32,64,32,31,0,63,64,56,64,63,
0,99,20,8,20,99,0,7,8,112,8,7,0,97,81,73,
69,67,0,0,127,65,65,0,0,2,4,8,16,32,0,0,
65,65,127,0,0,4,2,1,2,4,0,64,64,64,64,64,
0,0,1,2,4,0,0,32,84,84,84,120,0,127,72,68,
68,56,0,56,68,68,68,32,0,56,68,68,72,127,0,56,
84,84,84,24,0,8,126,9,1,2,0,24,164,164,164,124,
0,127,8,4,4,120,0,0,68,125,64,0,0,64,128,132,
125,0,0,127,16,40,68,0,0,0,65,127,64,0,0,124,
4,24,4,120,0,124,8,4,4,120,0,56,68,68,68,56,
0,252,36,36,36,24,0,24,36,36,24,252,0,124,8,4,
4,8,0,72,84,84,84,32,0,4,63,68,64,32,0,60,
64,64,32,124,0,28,32,64,32,28,0,60,64,48,64,60,
0,68,40,16,40,68,0,28,160,160,160,124,0,68,100,84,
76,68,0,8,54,65,65,0,0,0,0,127,0,0,0,0,
65,65,54,8,0,8,4,8,16,8,
};

int[18] dinoImage0 =
{
24,48,248,124,127,157,39,7,3,
24,48,248,124,255,29,23,7,3,
};

int[8] dinoCactusImage =
{
7,15,24,254,255,24,24,12,
};

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

#define DINO_STATE_GAMEOVER 0
#define DINO_STATE_PLAYING 1
#define DINO_STATE_START 2

#define DINO_GROUND_ROW 7
#define DINO_X_POS 25       // WIDTH/5
#define DINO_Y_POS 48       // HEIGHT-16
#define DINO_JUMP_VELOCITY 500
#define DINO_ACCEL 50
#define DINO_CHEAT_SIZE 2
#define DINO_REPLACE_SPRITE_TICKS 16

#define DINO_CACTUS_Y 48
#define DINO_CACTUSES_COUNT 6
#define DINO_CACTUS_VELOCITY 15

#define DINO_NORM_VALUE 10
#define DINO_MULTIPLIER_VALUE 11
#define DINO_MAX_LEVEL 20
#define DINO_DELAY_TICKS_GAME_OVER 35   // 500/14, matching upstream's own compile-time constant
#define DINO_EXP10 22026

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

int dinoState;
int dinoTickNum;
int dinoLevelNum;
int dinoDelayToGameOver;
int dinoMaxScore;
int dinoScore;
int dinoGameSpeed;

int[3] dinoRockX;
int[3] dinoRockY;
int[3] dinoRockWidth;
int[3] dinoRockVelocity;
int[3] dinoRockDelayTicks;
int[3] dinoRockNextUpdateAt;

int[6] dinoCactusX;        // fixed-point, DINO_NORM_VALUE-scaled
int[6] dinoCactusSpriteX;  // real pixel x
int[6] dinoCactusVelocity;
int[6] dinoCactusNextAppearAt;
int[6] dinoCactusStartDelay;

int dinoSpriteX;
int dinoSpriteY;
int dinoVelocity;
int dinoIsJumping;
int dinoIsLanded;

int[24] dinoScoreText;
int dinoScoreTextLen;

int[128] dinoCactusPageBuf;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

int dinoFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return dinoFont[ ( ch - 32 ) * 6 + col ];
}

// Composites one column of an 8-px-tall sprite/glyph (srcByte, bit0=top)
// whose own top-left pixel row is spriteY, for real hardware page 'page'.
// Caller must only invoke this when the page genuinely overlaps
// [spriteY, spriteY+7] - otherwise the shift amount can exceed the safe
// range (VIRCON32_C_DIALECT.md documents `>>`/`<<` as wrapping the shift
// amount modulo 32, the same class of bug this project has hit before).
int dinoSpriteColByte( int srcByte, int spriteY, int page )
{
    int shift = page * 8 - spriteY;
    if( shift >= 0 ) return ( srcByte >> shift ) & 0xFF;
    return ( srcByte << ( -shift ) ) & 0xFF;
}

void dinoAppendNumber( int value )
{
    int[8] digits;
    int n = 0;
    if( value == 0 )
    {
        dinoScoreText[ dinoScoreTextLen ] = 48;
        dinoScoreTextLen = dinoScoreTextLen + 1;
        return;
    }
    while( ( value > 0 ) && ( n < 8 ) )
    {
        digits[ n ] = value % 10;
        value = value / 10;
        n = n + 1;
    }
    int i;
    for( i = 0; i < n; i++ )
    {
        dinoScoreText[ dinoScoreTextLen ] = 48 + digits[ n - 1 - i ];
        dinoScoreTextLen = dinoScoreTextLen + 1;
    }
}

void dinoBuildScoreText()
{
    dinoScoreTextLen = 0;
    dinoScoreText[0]=115; dinoScoreText[1]=99; dinoScoreText[2]=111;
    dinoScoreText[3]=114; dinoScoreText[4]=101; dinoScoreText[5]=58;
    dinoScoreText[6]=32;
    dinoScoreTextLen = 7;

    int scoreNorm = dinoScore / DINO_NORM_VALUE;
    dinoAppendNumber( scoreNorm );
    dinoScoreText[ dinoScoreTextLen ] = 92; // backslash
    dinoScoreTextLen = dinoScoreTextLen + 1;
    dinoAppendNumber( max( dinoMaxScore, scoreNorm ) );
}

// -----------------------------------------------------------------------------
// Game logic
// -----------------------------------------------------------------------------

int dinoExpValue( int i )
{
    if( i == 0 ) return 3;
    if( i == 1 ) return 7;
    if( i == 2 ) return 20;
    if( i == 3 ) return 55;
    if( i == 4 ) return 148;
    if( i == 5 ) return 403;
    if( i == 6 ) return 1097;
    if( i == 7 ) return 2981;
    if( i == 8 ) return 8103;
    return 22026;
}

int dinoGenerateNextAppearAt()
{
    int value = DINO_EXP10 - arand( DINO_EXP10 );
    int lg = 9;
    int i;
    for( i = 0; i < 10; i++ )
    {
        if( value < dinoExpValue( i ) ) { lg = i; break; }
    }
    lg = -( lg - 10 ) * 8;

    int s = dinoGameSpeed / DINO_NORM_VALUE;
    return DINO_JUMP_VELOCITY * 2 * s / DINO_ACCEL + lg;
}

void dinoResetGame()
{
    int i;
    dinoTickNum = 0;
    dinoLevelNum = 0;
    dinoScore = 0;
    dinoGameSpeed = 10;

    dinoSpriteX = DINO_X_POS;
    dinoSpriteY = DINO_Y_POS;
    dinoVelocity = 0;
    dinoIsJumping = 0;
    dinoIsLanded = 1;

    dinoRockX[0] = 127; dinoRockY[0] = 1; dinoRockWidth[0] = 7; dinoRockVelocity[0] = 3; dinoRockDelayTicks[0] = 120; dinoRockNextUpdateAt[0] = 50;
    dinoRockX[1] = 127; dinoRockY[1] = 3; dinoRockWidth[1] = 4; dinoRockVelocity[1] = 2; dinoRockDelayTicks[1] = 100; dinoRockNextUpdateAt[1] = 20;
    dinoRockX[2] = 127; dinoRockY[2] = 6; dinoRockWidth[2] = 3; dinoRockVelocity[2] = 1; dinoRockDelayTicks[2] = 30;  dinoRockNextUpdateAt[2] = 10;

    for( i = 0; i < DINO_CACTUSES_COUNT; i++ )
    {
        dinoCactusX[ i ] = 127 * DINO_NORM_VALUE;
        dinoCactusSpriteX[ i ] = 127;
        dinoCactusVelocity[ i ] = DINO_CACTUS_VELOCITY;
        dinoCactusNextAppearAt[ i ] = dinoGenerateNextAppearAt();
        if( i == 0 ) dinoCactusStartDelay[ i ] = 1;
        else dinoCactusStartDelay[ i ] = 0;
    }
}

void dinoUpdateRocks()
{
    int i;
    for( i = 0; i < 3; i++ )
    {
        if( dinoRockNextUpdateAt[ i ] )
        {
            dinoRockNextUpdateAt[ i ] = dinoRockNextUpdateAt[ i ] - 1;
            continue;
        }

        if( dinoRockX[ i ] < dinoRockVelocity[ i ] )
        {
            dinoRockNextUpdateAt[ i ] = dinoRockDelayTicks[ i ];
            dinoRockX[ i ] = 128 - dinoRockWidth[ i ];
        }
        else
        {
            dinoRockX[ i ] = dinoRockX[ i ] - dinoRockVelocity[ i ];
        }
    }
}

void dinoUpdateCactuses()
{
    int i;
    for( i = 0; i < DINO_CACTUSES_COUNT; i++ )
    {
        if( !dinoCactusStartDelay[ i ] ) continue;

        if( dinoCactusNextAppearAt[ i ] )
        {
            dinoCactusNextAppearAt[ i ] = dinoCactusNextAppearAt[ i ] - 1;
            continue;
        }
        dinoCactusStartDelay[ ( i + 1 ) % DINO_CACTUSES_COUNT ] = 1;

        if( dinoCactusX[ i ] <= dinoCactusVelocity[ i ] )
        {
            dinoCactusNextAppearAt[ i ] = dinoGenerateNextAppearAt();
            dinoCactusX[ i ] = ( 128 - 8 ) * DINO_NORM_VALUE;
            dinoCactusSpriteX[ i ] = 127;
            dinoCactusStartDelay[ i ] = 0;
        }
        else
        {
            dinoCactusX[ i ] = dinoCactusX[ i ] - dinoCactusVelocity[ i ];
            dinoCactusSpriteX[ i ] = dinoCactusX[ i ] / DINO_NORM_VALUE;
        }
    }
}

int dinoIsOverlapping( int x1, int x2, int y1, int y2 )
{
    return max( x1, y1 ) <= min( x2, y2 );
}

int dinoIsCollided()
{
    if( dinoSpriteY < DINO_Y_POS - 9 ) return 0;

    int i;
    for( i = 0; i < DINO_CACTUSES_COUNT; i++ )
    {
        if( dinoCactusNextAppearAt[ i ] ) continue;

        int dinoStart = dinoSpriteX;
        int dinoEnd = dinoStart + 9;
        int cactusStart = dinoCactusSpriteX[ i ] + DINO_CHEAT_SIZE;
        int cactusEnd = cactusStart + 8 - DINO_CHEAT_SIZE;
        if( dinoIsOverlapping( dinoStart, dinoEnd, cactusStart, cactusEnd ) ) return 1;
    }
    return 0;
}

void dinoUpdateDino()
{
    if( dinoIsJumping )
    {
        dinoVelocity = DINO_JUMP_VELOCITY;
        dinoIsJumping = 0;
    }

    dinoSpriteY = dinoSpriteY - dinoVelocity / ( DINO_NORM_VALUE * DINO_NORM_VALUE );
    dinoVelocity = dinoVelocity - DINO_ACCEL;

    if( dinoSpriteY >= DINO_Y_POS )
    {
        dinoSpriteY = DINO_Y_POS;
        dinoVelocity = 0;
        dinoIsLanded = 1;
    }
}

void dinoUpdateGameState()
{
    if( ( dinoTickNum % 5 ) == 0 ) dinoScore = dinoScore + dinoGameSpeed;

    int currentLevel = dinoScore / ( DINO_NORM_VALUE * 100 );
    if( ( dinoLevelNum < DINO_MAX_LEVEL ) && ( currentLevel > dinoLevelNum ) )
    {
        int i;
        for( i = 0; i < 3; i++ )
        {
            dinoRockVelocity[ i ] = dinoRockVelocity[ i ] * DINO_MULTIPLIER_VALUE / DINO_NORM_VALUE;
            dinoRockDelayTicks[ i ] = dinoRockDelayTicks[ i ] * DINO_NORM_VALUE / DINO_MULTIPLIER_VALUE;
        }
        for( i = 0; i < DINO_CACTUSES_COUNT; i++ )
        {
            dinoCactusVelocity[ i ] = dinoCactusVelocity[ i ] * DINO_MULTIPLIER_VALUE / DINO_NORM_VALUE;
        }
        dinoGameSpeed = dinoGameSpeed * DINO_MULTIPLIER_VALUE / DINO_NORM_VALUE;
        dinoLevelNum = dinoLevelNum + 1;
    }
}

void dinoUpdateState()
{
    int buttonState = isUpPressed() || isFirePressed();

    if( ( dinoState == DINO_STATE_GAMEOVER ) || ( dinoState == DINO_STATE_START ) )
    {
        if( ( dinoState == DINO_STATE_GAMEOVER ) && ( dinoDelayToGameOver > 0 ) ) dinoDelayToGameOver = dinoDelayToGameOver - 1;
        if( buttonState && ( ( dinoState != DINO_STATE_GAMEOVER ) || !dinoDelayToGameOver ) )
        {
            dinoState = DINO_STATE_PLAYING;
            dinoResetGame();
        }
        return;
    }

    if( buttonState && dinoIsLanded )
    {
        dinoIsJumping = 1;
        dinoIsLanded = 0;
    }

    dinoTickNum = dinoTickNum + 1;
    dinoUpdateRocks();
    dinoUpdateDino();
    dinoUpdateCactuses();
    dinoUpdateGameState();

    if( dinoIsCollided() )
    {
        dinoState = DINO_STATE_GAMEOVER;
        int scoreNorm = dinoScore / DINO_NORM_VALUE;
        if( scoreNorm > dinoMaxScore ) dinoMaxScore = scoreNorm;
        dinoDelayToGameOver = DINO_DELAY_TICKS_GAME_OVER;
    }
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

int dinoTextColByte( int* text, int textLen, int startX, int x, int textY, int page )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= textLen ) return 0;
    int src = dinoFontByte( text[ charIdx ], rel % 6 );
    return dinoSpriteColByte( src, textY, page );
}

void dinoBuildCactusPageBuf()
{
    int x;
    for( x = 0; x < 128; x++ ) dinoCactusPageBuf[ x ] = 0;

    int i, col;
    for( i = 0; i < DINO_CACTUSES_COUNT; i++ )
    {
        if( dinoCactusNextAppearAt[ i ] ) continue;
        int cx = dinoCactusSpriteX[ i ];
        for( col = 0; col < 8; col++ )
        {
            int px = cx + col;
            if( ( px >= 0 ) && ( px < 128 ) )
            {
                dinoCactusPageBuf[ px ] = dinoCactusPageBuf[ px ] | dinoCactusImage[ col ];
            }
        }
    }
}

int dinoComputeByte( int x, int page )
{
    if( dinoState == DINO_STATE_GAMEOVER )
    {
        // "Game over :(" at pixel (32,25), page range [25/8..(25+7)/8] = [3,4]
        if( ( page == 3 ) || ( page == 4 ) )
        {
            return dinoTextColByte( "Game over :(", 12, 32, x, 25, page );
        }
        return 0;
    }

    if( dinoState == DINO_STATE_START )
    {
        // "Start" at pixel (52,25)
        if( ( page == 3 ) || ( page == 4 ) )
        {
            return dinoTextColByte( "Start", 5, 52, x, 25, page );
        }
        return 0;
    }

    // DINO_STATE_PLAYING
    int pixels = 0;

    // Ground/rocks - page-aligned, one byte per column.
    if( page == DINO_GROUND_ROW )
    {
        int bits = 1;
        int r;
        for( r = 0; r < 3; r++ )
        {
            if( dinoRockNextUpdateAt[ r ] ) continue;
            if( ( x > dinoRockX[ r ] ) && ( x < dinoRockX[ r ] + dinoRockWidth[ r ] ) )
            {
                bits = bits | ( 1 << ( 8 - dinoRockY[ r ] ) );
            }
        }
        pixels = pixels | ( bits & 0xFF );
    }

    // Score text - fixed at pixel (1,1), page range [0,1].
    if( ( page == 0 ) || ( page == 1 ) )
    {
        pixels = pixels | dinoTextColByte( dinoScoreText, dinoScoreTextLen, 1, x, 1, page );
    }

    // Dino sprite - 9px wide, arbitrary pixel Y (jump physics).
    if( ( x >= dinoSpriteX ) && ( x < dinoSpriteX + 9 ) )
    {
        int pageTop = page * 8;
        if( ( pageTop + 7 >= dinoSpriteY ) && ( pageTop <= dinoSpriteY + 7 ) )
        {
            int frame = 0;
            if( dinoIsLanded ) frame = ( ( dinoTickNum & ( DINO_REPLACE_SPRITE_TICKS - 1 ) ) > ( DINO_REPLACE_SPRITE_TICKS >> 1 ) );
            int src = dinoImage0[ frame * 9 + ( x - dinoSpriteX ) ];
            pixels = pixels | dinoSpriteColByte( src, dinoSpriteY, page );
        }
    }

    // Cactus sprites - 8px wide, always page-aligned (CACTUS_Y fixed) -
    // composited once per frame into dinoCactusPageBuf (see
    // dinoBuildCactusPageBuf()) instead of rescanning all 6 cactuses at
    // every one of 128 columns on this page (768 checks/frame down to
    // 6*8=48) - the same per-object-footprint compositing lesson this
    // project has applied repeatedly elsewhere, checked here proactively
    // rather than waiting for a CPU report.
    if( page == DINO_CACTUS_Y / 8 )
    {
        pixels = pixels | dinoCactusPageBuf[ x ];
    }

    return pixels;
}

void dinoRenderImage()
{
    md_beginFrame();

    if( dinoState == DINO_STATE_PLAYING )
    {
        dinoBuildScoreText();
        dinoBuildCactusPageBuf();
    }

    int x, page;
    for( page = 0; page < 8; page++ )
    {
        for( x = 0; x < 128; x++ )
        {
            md_drawColumn( x, page, dinoComputeByte( x, page ) );
        }
    }
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

void gameDinoGame_init()
{
    InitTinyJoypad();
    dinoMaxScore = 0;
    dinoState = DINO_STATE_START;
    dinoResetGame();
}

void gameDinoGame_update()
{
    dinoUpdateState();
    dinoRenderImage();
}

// =============================================================================
// Oroboros ("UFO Escape") - ported from attiny_oroboros_vcc_gnd_scl_sda.ino
// (Ilya Titov / webboggles.com, 2015; webboggles/AttinyArcade on GitHub).
// Non-commercial, with attribution - same license family as this project's
// other Ilya-Titov-credited port (UFO), and the same author.
//
// A classic Snake clone on a 32x16 logical grid: the snake wraps at the
// playfield edges, eats bait to grow, and dies on self-collision.
//
// Not tinyJoypadShim/obonoCoreShim lineage by name (genuine #AttinyArcade
// hardware - two discrete buttons on real interrupt pins, not TinyJoypad's
// analog ladder), and unlike this project's other AttinyArcade-lineage
// ports (Wren/Frogger/Bat Bonanza/Stacker/UFO, all remapped to TinyJoypad's
// control scheme by Billy Cheung before this project ever saw them), this
// is the untouched original - it only ever reads two logical inputs (turn
// left / turn right, relative to current heading), so isLeftPressed()/
// isRightPressed() cover its whole input surface with no new shim needed.
//
// Structural notes:
//  - Upstream's screenBuffer[16] is a 32-bit-per-row occupancy bitmask,
//    rebuilt from scratch every tick and read back via `>> (31-col) & 1`
//    bit tests during rendering. Ported as a plain flat occupancy array
//    (`orbGrid[16*32]`, one int per cell) instead - avoids needing 32-bit
//    shifts up to the sign bit (bit 31) on this dialect's signed ints,
//    which was never tested anywhere else in this project and easy to
//    sidestep entirely with a plain array.
//  - Upstream reads its two buttons via hardware pin-change interrupts
//    that set a "pending turn" flag, consumed once per tick - a bare
//    `isLeftPressed()`/`isRightPressed()` level read *at tick time* would
//    behave differently (spinning in circles if held across several
//    ticks, since a snake tick is much faster than a human can release a
//    button). Ported as an edge-detect checked every real engine frame
//    (independent of the game's own slower tick rate) that queues a
//    pending turn, consumed by the next tick - this reproduces upstream's
//    real "one press, one turn" behavior far more faithfully than a naive
//    per-tick level read would.
//  - `nextDir` and `selfCollision` are set upstream but never actually
//    read anywhere afterward - confirmed dead by inspection, dropped
//    rather than ported (matching this project's own established
//    practice for confirmed-dead upstream state).
//  - The render loop upstream only ever streams 124 of 128 real columns
//    (31 logical cells x 4 px), relying on real SSD1306 VRAM to keep
//    showing whatever was in the last 4 columns (always black, since
//    nothing else ever draws there) - the same VRAM-persistence
//    assumption already found and fixed in several other ports here,
//    applied proactively from the start this time instead of needing a
//    later fix.
//  - `system_sleep()`'s real AVR power-down has no Vircon32 equivalent
//    (no battery to conserve) - its two call sites are ported as their
//    *observable* gameplay effects instead: the 40-second idle-turn
//    timeout still auto-advances the heading once (so walking away from
//    the game doesn't leave it going dead-straight forever), and the
//    post-game-over "sleep until woken by a button" becomes an explicit
//    wait-for-Fire gate before the next game starts, matching this
//    project's own standing convention of waiting for a confirming press
//    at a game-over screen rather than auto-restarting.
//  - Upstream's game-over sound is a 1000-call synchronous `beep()` sweep
//    with a randomized, slowly-growing delay bound - Vircon32's audio
//    channel has no queue (confirmed repeatedly elsewhere in this
//    project), so 1000 near-instant calls would only ever be audible as
//    the very last one. Downsampled to a short frame-stepped descending
//    sweep instead, matching the established fix for this exact bug
//    shape (e.g. Tiny Missile's/Tiny Pipe's own oversized sweeps).
// =============================================================================

int[570] orbFont =
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

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

#define ORB_GRID_W 32
#define ORB_GRID_H 16
#define ORB_MAX_LEN 100

#define ORB_STATE_INTRO 0
#define ORB_STATE_PLAYING 1
#define ORB_STATE_GAMEOVER 2

#define ORB_IDLE_TIMEOUT_FRAMES 2400 // 40s @ 60fps
#define ORB_INTRO_WAIT_FRAMES 130

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

int orbState;
int orbStateTimer;

int[100] orbSnakeX;
int[100] orbSnakeY;
int orbLen;
int orbDir; // 0 up, 1 right, 2 down, 3 left

int orbPendingMinus;
int orbPendingPlus;
int orbPrevLeftHeld;
int orbPrevRightHeld;
int orbIdleFrames;

int orbFrameDelay; // ticks, frame units
int orbTickCounter;

int orbBaitX;
int orbBaitY;
int orbBaitDropped;

int orbScore;
int orbTopScore;

int[512] orbGrid; // ORB_GRID_H * ORB_GRID_W, 1 = snake segment here

// intro jingle (bDelay-style values from upstream's own resetGame(), mapped
// to representative frequencies - see this file's own header comment on
// why an exact NOP-timing-to-Hz conversion isn't attempted)
int[13] orbIntroFreqs =
{
800,800,229,533,320,533,320,533,320,533,123,533,533,
};
int orbIntroIdx;
int orbIntroTimer;

// eat-bait jingle
int[4] orbEatFreqs =
{
533,533,533,800,
};
int orbEatIdx;
int orbEatTimer;

// game-over sweep (downsampled from upstream's own 1000-call sweep)
int orbGameOverIdx;
int orbGameOverTimer;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

int orbFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return orbFont[ ( ch - 32 ) * 6 + col ];
}

int orbTextByteAt( int* text, int textLen, int startX, int x )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= textLen ) return 0;
    return orbFontByte( text[ charIdx ], rel % 6 );
}

void orbBeep( int freqHz, float durSec )
{
    md_playTone( freqHz, durSec );
}

void orbStartIntroJingle()
{
    orbIntroIdx = 0;
    orbIntroTimer = 0;
}

void orbAdvanceIntroJingle()
{
    if( orbIntroIdx >= 13 ) return;
    orbIntroTimer = orbIntroTimer - 1;
    if( orbIntroTimer <= 0 )
    {
        orbBeep( orbIntroFreqs[ orbIntroIdx ], 0.08 );
        orbIntroIdx = orbIntroIdx + 1;
        orbIntroTimer = 6;
    }
}

void orbStartEatJingle()
{
    orbEatIdx = 0;
    orbEatTimer = 0;
}

void orbAdvanceEatJingle()
{
    if( orbEatIdx >= 4 ) return;
    orbEatTimer = orbEatTimer - 1;
    if( orbEatTimer <= 0 )
    {
        orbBeep( orbEatFreqs[ orbEatIdx ], 0.05 );
        orbEatIdx = orbEatIdx + 1;
        orbEatTimer = 3;
    }
}

void orbStartGameOverSweep()
{
    orbGameOverIdx = 0;
    orbGameOverTimer = 0;
}

void orbAdvanceGameOverSweep()
{
    if( orbGameOverIdx >= 16 ) return;
    orbGameOverTimer = orbGameOverTimer - 1;
    if( orbGameOverTimer <= 0 )
    {
        int freq = 700 - orbGameOverIdx * 40 + arand( 80 );
        if( freq < 80 ) freq = 80;
        orbBeep( freq, 0.04 );
        orbGameOverIdx = orbGameOverIdx + 1;
        orbGameOverTimer = 3;
    }
}

// -----------------------------------------------------------------------------
// Game logic
// -----------------------------------------------------------------------------

void orbResetGame()
{
    orbLen = 3;
    orbScore = 0;
    orbDir = 2; // matches upstream's initial dir=2 (down)

    orbSnakeX[ 0 ] = 16;
    orbSnakeY[ 0 ] = 7;
    orbSnakeX[ 1 ] = 15;
    orbSnakeY[ 1 ] = 7;
    orbSnakeX[ 2 ] = 14;
    orbSnakeY[ 2 ] = 7;

    orbFrameDelay = 18; // 300ms @ 60fps
    orbTickCounter = 0;
    orbBaitDropped = 0;
    orbIdleFrames = 0;

    orbPendingMinus = 0;
    orbPendingPlus = 0;
    orbPrevLeftHeld = 0;
    orbPrevRightHeld = 0;

    orbState = ORB_STATE_INTRO;
    orbStateTimer = ORB_INTRO_WAIT_FRAMES;
    orbStartIntroJingle();
}

void orbDropBait()
{
    int tries = 0;
    orbBaitDropped = 0;
    while( ( orbBaitDropped == 0 ) && ( tries < 50 ) )
    {
        orbBaitX = 1 + arand( 29 );
        orbBaitY = 1 + arand( 13 );
        int free = 1;
        int i;
        for( i = orbLen - 1; i >= 0; i = i - 1 )
        {
            if( ( orbSnakeY[ i ] == orbBaitY ) && ( orbSnakeX[ i ] == orbBaitX ) ) free = 0;
        }
        if( free ) orbBaitDropped = 1;
        tries = tries + 1;
    }
}

void orbTick()
{
    // move body
    int uro;
    for( uro = orbLen - 1; uro > 0; uro = uro - 1 )
    {
        orbSnakeX[ uro ] = orbSnakeX[ uro - 1 ];
        orbSnakeY[ uro ] = orbSnakeY[ uro - 1 ];
    }

    if( orbPendingMinus )
    {
        if( orbDir > 0 ) orbDir = orbDir - 1; else orbDir = 3;
        orbPendingMinus = 0;
        orbIdleFrames = 0;
    }
    if( orbPendingPlus )
    {
        if( orbDir < 3 ) orbDir = orbDir + 1; else orbDir = 0;
        orbPendingPlus = 0;
        orbIdleFrames = 0;
    }

    if( orbDir == 0 )
    {
        if( orbSnakeY[ 0 ] > 0 ) orbSnakeY[ 0 ] = orbSnakeY[ 0 ] - 1; else orbSnakeY[ 0 ] = 15;
    }
    else if( orbDir == 1 )
    {
        if( orbSnakeX[ 0 ] < 31 ) orbSnakeX[ 0 ] = orbSnakeX[ 0 ] + 1; else orbSnakeX[ 0 ] = 1;
    }
    else if( orbDir == 2 )
    {
        if( orbSnakeY[ 0 ] < 15 ) orbSnakeY[ 0 ] = orbSnakeY[ 0 ] + 1; else orbSnakeY[ 0 ] = 0;
    }
    else
    {
        if( orbSnakeX[ 0 ] > 1 ) orbSnakeX[ 0 ] = orbSnakeX[ 0 ] - 1; else orbSnakeX[ 0 ] = 31;
    }

    // rebuild occupancy grid
    int i;
    for( i = 0; i < 512; i++ ) orbGrid[ i ] = 0;

    if( orbBaitDropped == 0 ) orbDropBait();

    if( ( orbSnakeY[ 0 ] == orbBaitY ) && ( orbSnakeX[ 0 ] == orbBaitX ) )
    {
        orbStartEatJingle();
        orbBaitDropped = 0;
        orbScore = orbScore + 1;
        orbLen = orbLen + 1;
        if( orbLen > ORB_MAX_LEN ) orbLen = ORB_MAX_LEN;
    }

    // mark the bait's own cell in the occupancy grid too - separate from
    // the snake segments below, matching upstream's own
    // `screenBuffer[baitY] |= (bit at baitX)` step. Missing this meant the
    // bait's render condition (itself gated behind the grid bit already
    // being set) could never trigger, since only snake segments ever set
    // the grid - the direct cause of a live user report ("no food spawned
    // for the snake").
    if( orbBaitDropped )
    {
        orbGrid[ orbBaitY * ORB_GRID_W + orbBaitX ] = 1;
    }

    int collided = 0;
    for( uro = orbLen - 1; uro >= 0; uro = uro - 1 )
    {
        int gx = orbSnakeX[ uro ];
        int gy = orbSnakeY[ uro ];
        if( ( gx >= 0 ) && ( gx < ORB_GRID_W ) && ( gy >= 0 ) && ( gy < ORB_GRID_H ) )
        {
            if( orbGrid[ gy * ORB_GRID_W + gx ] ) collided = 1;
            orbGrid[ gy * ORB_GRID_W + gx ] = 1;
        }
    }

    if( collided )
    {
        if( orbScore > orbTopScore ) orbTopScore = orbScore;
        orbState = ORB_STATE_GAMEOVER;
        orbStateTimer = 999999; // waits for Fire, see update()
        orbStartGameOverSweep();
        return;
    }

    orbBeep( 400, 0.02 );

    // idle-turn timeout: matches upstream's own "auto-nudge if left alone"
    // effect (see this file's own header comment on why the real sleep
    // itself isn't ported)
    orbIdleFrames = orbIdleFrames + 1;
    if( orbIdleFrames > ORB_IDLE_TIMEOUT_FRAMES )
    {
        orbIdleFrames = 0;
        if( orbDir < 3 ) orbDir = orbDir + 1; else orbDir = 0;
    }

    if( orbScore < 100 ) orbFrameDelay = 18 - ( orbScore * 18 ) / 100;
    if( orbFrameDelay < 1 ) orbFrameDelay = 1;
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

int[4] orbScoreDigits;
int orbScoreDigitCount;
int[4] orbTopScoreDigits;
int orbTopScoreDigitCount;

int orbComputeByte( int x, int page )
{
    if( orbState == ORB_STATE_INTRO )
    {
        if( page == 2 ) return orbTextByteAt( "O R O B O R O S", 15, 16, x );
        if( page == 4 ) return orbTextByteAt( "by webboggles.com", 17, 10, x );
        if( page == 6 ) return orbTextByteAt( "Tweet @webboggles", 17, 12, x );
        if( page == 7 ) return orbTextByteAt( "#AttinyArcade", 13, 22, x );
        return 0;
    }

    if( orbState == ORB_STATE_GAMEOVER )
    {
        if( page == 3 ) return orbTextByteAt( "Game Over", 9, 32, x );
        if( page == 5 )
        {
            int pixels = orbTextByteAt( "score:", 6, 32, x );
            pixels = pixels | orbTextByteAt( orbScoreDigits, orbScoreDigitCount, 70, x );
            return pixels;
        }
        if( page == 6 )
        {
            int pixels = orbTextByteAt( "top score:", 10, 32, x );
            pixels = pixels | orbTextByteAt( orbTopScoreDigits, orbTopScoreDigitCount, 90, x );
            return pixels;
        }
        return 0;
    }

    // ORB_STATE_PLAYING
    if( x >= 124 ) return 0; // upstream never draws the last 4 columns either

    int col = x / 4 + 1;
    int box = x % 4;
    int upperRow = page * 2;
    int lowerRow = page * 2 + 1;

    int upperBit = orbGrid[ upperRow * ORB_GRID_W + col ];
    int lowerBit = orbGrid[ lowerRow * ORB_GRID_W + col ];

    int pixels = 0;
    if( upperBit )
    {
        if( ( box == 1 || box == 2 ) && ( orbBaitX == col ) && ( orbBaitY == upperRow ) && orbBaitDropped ) pixels = pixels | 9;
        else pixels = pixels | 15;
    }
    if( lowerBit )
    {
        if( ( box == 1 || box == 2 ) && ( orbBaitX == col ) && ( orbBaitY == lowerRow ) && orbBaitDropped ) pixels = pixels | 144;
        else pixels = pixels | 240;
    }
    if( page == 0 ) pixels = pixels | 1;
    if( page == 7 ) pixels = pixels | 128;
    if( ( col == 1 && box == 0 ) || ( col == 31 && box == 3 ) ) pixels = 255;

    return pixels;
}

// outIndexBase selects which of the two digit-buffer globals to fill (0 or
// 1), since only orbScoreDigits/orbTopScoreDigits are ever the target.
void orbDigitsOf( int value, int outIndexBase )
{
    if( value < 0 ) value = 0;
    if( value > 9999 ) value = 9999;

    if( value == 0 )
    {
        if( outIndexBase == 0 ) { orbScoreDigits[ 0 ] = 48; orbScoreDigitCount = 1; }
        else { orbTopScoreDigits[ 0 ] = 48; orbTopScoreDigitCount = 1; }
        return;
    }

    int[4] digits;
    int n = 0;
    int tmp = value;
    while( ( tmp > 0 ) && ( n < 4 ) )
    {
        digits[ n ] = tmp % 10;
        tmp = tmp / 10;
        n = n + 1;
    }
    int i;
    if( outIndexBase == 0 )
    {
        for( i = 0; i < n; i++ ) orbScoreDigits[ i ] = 48 + digits[ n - 1 - i ];
        orbScoreDigitCount = n;
    }
    else
    {
        for( i = 0; i < n; i++ ) orbTopScoreDigits[ i ] = 48 + digits[ n - 1 - i ];
        orbTopScoreDigitCount = n;
    }
}

void orbRenderImage()
{
    md_beginFrame();

    if( orbState == ORB_STATE_GAMEOVER )
    {
        orbDigitsOf( orbScore, 0 );
        orbDigitsOf( orbTopScore, 1 );
    }

    int x, page;
    for( x = 0; x < 128; x++ )
    {
        for( page = 0; page < 8; page++ )
        {
            md_drawColumn( x, page, orbComputeByte( x, page ) );
        }
    }
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

// Quit-confirmation-dialog resume hook (see menuGameList.c's own comment
// on this pattern). Every state in gameOroboros_update() already calls
// orbRenderImage() unconditionally every frame (no dirty-flag skipping
// anywhere in this port), so there is nothing to actually force back on -
// wired anyway for consistency with every other port's own onResume audit,
// matching the standing project practice of never leaving this hook
// unaudited just because a game "looks like" it always redraws.
void gameOroboros_forceRedraw()
{
}

void gameOroboros_init()
{
    InitTinyJoypad();
    orbTopScore = 0;
    orbResetGame();
}

void gameOroboros_update()
{
    if( orbState == ORB_STATE_INTRO )
    {
        orbAdvanceIntroJingle();
        orbStateTimer = orbStateTimer - 1;
        if( orbStateTimer <= 0 )
        {
            orbState = ORB_STATE_PLAYING;
            orbTickCounter = 0;
        }
    }
    else if( orbState == ORB_STATE_PLAYING )
    {
        orbAdvanceEatJingle();

        // edge-detect every real frame, independent of the tick rate -
        // see this file's own header comment on why
        if( isLeftPressed() && !orbPrevLeftHeld ) orbPendingMinus = 1;
        if( isRightPressed() && !orbPrevRightHeld ) orbPendingPlus = 1;
        orbPrevLeftHeld = isLeftPressed();
        orbPrevRightHeld = isRightPressed();

        orbTickCounter = orbTickCounter - 1;
        if( orbTickCounter <= 0 )
        {
            orbTickCounter = orbFrameDelay;
            orbTick();
        }
    }
    else if( orbState == ORB_STATE_GAMEOVER )
    {
        orbAdvanceGameOverSweep();
        if( isFirePressed() ) orbResetGame();
    }

    orbRenderImage();
}


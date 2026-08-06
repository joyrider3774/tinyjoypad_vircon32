// =============================================================================
// Run Dude Run - ported from attiny_run_vcc_gnd_scl_sda.ino (Ilya Titov /
// webboggles.com, 2017; webboggles/AttinyArcade on GitHub). Non-commercial,
// with attribution - same license family and author as this project's own
// UFO and Oroboros.
//
// Dodge falling bombs: move left/right along the bottom row, survive as
// long as possible. Bomb spawn rate and count both scale up with score.
//
// Not tinyJoypadShim/obonoCoreShim lineage by name (genuine #AttinyArcade
// hardware - two discrete buttons on real interrupt pins), and like this
// project's other Ilya-Titov-credited ports, needs no new shim -
// isLeftPressed()/isRightPressed() cover the whole input surface. Unlike
// Oroboros (this game's own sibling port, which needed edge-detection to
// replicate a "one press, one turn" gesture), upstream's own button
// handling here is a genuine LEVEL read
// (`if (digitalRead(0)==1||btn1==1) ...`) - holding a direction moves the
// player every tick, matching a "run" game's real intent - so
// isLeftPressed()/isRightPressed() are read directly at tick time with no
// edge-detection needed.
//
// Structural notes:
//  - Upstream's bomb rendering is the most intricate part: bombs fall at
//    an arbitrary (non-page-aligned) pixel Y, so upstream slices its 7x16
//    sprite across up to 3 real hardware pages using `~byte >> offset ^
//    0xFF << offset2`-style expressions that rely on AVR's implicit
//    uint8_t narrowing to stay within a real byte - the exact class of
//    bug this project has hit and fixed repeatedly elsewhere (byte
//    truncation, shift wraparound). Rather than porting that bit
//    arithmetic literally, `rddBombColByte()` below re-derives the same
//    visible result with a single sign-branched shift of the sprite's
//    16-bit column value (top+bottom byte combined) - independently
//    correct by construction, no truncation risk, and (after an initial
//    version using an 8-iteration-per-bit nested-function-call bit-scan
//    measurably cost real CPU% once RDD_MAX_BOMBS(16) bombs were live at
//    once - see rddBuildBombPageBuffer()'s own comment) far cheaper too.
//  - Upstream's ~30-second all-input idle timeout goes to real AVR sleep
//    with no gameplay-visible effect if skipped (unlike Oroboros's own
//    idle-turn timeout, which visibly auto-nudges the snake's heading) -
//    not ported at all, matching this project's own established practice
//    of only replicating a sleep call's *observable* gameplay effect, not
//    the power-management mechanism itself.
//  - Upstream's game-over sound (a 1000-call synchronous `beep()` sweep)
//    was downsampled to a short frame-stepped descending sweep, matching
//    the established fix for this exact bug shape (Vircon32's audio
//    channel has no queue).
//  - `bottle1` is declared upstream but only ever referenced from a
//    commented-out call site - confirmed dead by inspection, dropped
//    rather than ported.
// =============================================================================

int[570] rddFont =
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

int[5] rddPlayerIdle1 =
{
60,219,194,219,60,
};

int[5] rddPlayerIdle2 =
{
59,215,197,215,59,
};

int[5] rddPlayerRight1 =
{
247,123,155,2,111,
};

int[5] rddPlayerRight2 =
{
63,211,226,27,255,
};

int[5] rddPlayerLeft1 =
{
111,2,155,123,247,
};

int[5] rddPlayerLeft2 =
{
255,27,226,211,63,
};

// 7 columns wide x 16 rows tall (2 pages) - column c's top-half byte is
// index c, bottom-half byte is index c+7. Stored inverted (matches
// upstream's own `~bomb1[...]` reads) - see rddBombByteAt().
int[14] rddBomb1 =
{
255,143,127,31,127,143,255,199,185,126,124,16,129,199,
};

// small 7x8 explosion glyph, drawn directly (not inverted) - matches the
// literal B-prefixed byte values inline in upstream's own drawBombs()
int[7] rddExplosion =
{
60,90,165,85,170,90,60,
};

int[1024] rddSplash =
{
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,
62,254,226,194,254,128,0,0,0,192,128,0,0,192,192,128,
0,32,112,192,224,48,112,224,128,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,128,128,128,128,0,0,0,0,0,0,0,0,0,
0,0,192,192,192,96,30,254,240,248,247,251,252,248,248,254,
253,252,247,252,252,254,253,255,252,252,244,252,244,254,254,254,
220,248,255,255,248,96,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,1,7,239,224,97,97,115,118,112,115,119,246,230,231,227,
230,192,192,1,195,179,224,129,3,3,0,0,16,248,240,224,
132,28,124,252,254,198,6,14,10,28,116,104,176,224,134,127,
251,243,179,51,27,26,24,8,0,0,0,0,0,0,0,0,
131,0,255,63,255,255,126,63,15,7,3,1,1,1,0,0,
0,4,4,4,132,7,7,7,7,15,15,31,31,30,123,127,
255,255,255,127,127,255,0,0,0,16,6,12,0,0,0,0,
0,0,96,224,160,96,192,128,0,0,0,0,0,240,252,248,
0,14,60,127,252,240,224,144,16,0,0,0,0,0,0,1,
3,15,255,255,252,113,7,15,63,252,240,224,128,224,63,1,
3,15,60,248,99,79,30,124,224,192,224,96,63,30,15,0,
1,7,7,15,14,12,28,26,26,14,132,0,0,0,32,255,
255,254,127,63,31,11,12,0,0,0,0,0,0,0,128,128,
128,192,192,192,128,193,193,129,129,3,1,2,2,6,12,12,
8,0,1,16,144,160,191,0,0,0,24,60,124,8,0,0,
128,128,0,3,15,62,126,243,195,134,12,184,224,255,129,255,
128,128,128,128,129,3,15,63,123,252,248,240,240,248,120,124,
54,31,13,7,0,224,240,240,240,48,113,99,193,0,0,0,
0,0,192,224,192,0,0,0,224,240,224,128,0,0,0,60,
248,248,112,224,192,128,6,254,254,0,7,60,224,0,0,31,
7,0,224,236,252,0,0,0,0,0,0,0,0,1,1,1,
3,3,15,63,59,63,63,63,63,63,31,7,7,14,254,254,
255,255,191,252,252,248,240,62,0,0,0,0,0,0,8,0,
15,31,63,62,62,94,254,255,255,255,255,255,255,255,255,251,
243,131,3,3,3,3,131,195,54,28,159,187,177,224,192,224,
32,32,32,64,64,99,39,255,191,252,224,252,63,63,48,112,
112,112,225,227,231,94,124,112,96,63,31,7,15,31,28,56,
1,31,63,60,3,7,15,15,15,4,0,0,13,12,132,160,
252,255,255,255,250,224,224,192,224,224,240,248,248,252,252,252,
254,254,254,254,255,255,241,243,226,224,192,192,224,224,193,249,
243,227,195,131,131,3,7,252,0,0,16,48,56,4,4,12,
66,70,132,204,126,38,255,255,255,255,255,127,127,63,191,61,
127,253,19,222,156,14,9,28,6,195,241,80,220,4,5,7,
15,94,248,240,240,144,32,96,195,134,143,111,57,12,6,14,
8,4,196,124,240,140,4,0,0,0,56,46,118,248,216,248,
204,228,100,36,0,24,28,14,6,2,0,3,1,192,251,255,
255,255,255,255,255,255,255,255,255,255,255,127,63,31,143,207,
231,231,247,243,243,243,243,195,231,199,135,143,15,31,127,255,
255,255,255,255,255,240,24,15,128,196,136,0,48,48,112,224,
40,44,39,33,35,69,73,115,3,247,239,232,99,159,223,159,
126,125,60,207,243,249,242,199,199,207,196,130,143,239,178,243,
115,201,219,79,95,125,223,194,195,243,127,255,255,255,252,190,
42,55,156,142,27,25,89,179,252,221,90,238,231,107,21,145,
128,193,195,192,128,128,128,0,0,0,0,0,0,0,143,255,
255,255,255,255,255,255,255,255,255,255,240,240,226,239,207,143,
143,15,7,63,63,63,255,127,7,243,249,252,254,254,255,255,
255,255,127,247,243,225,240,216,216,252,255,255,254,254,254,127,
24,47,109,225,62,1,129,217,243,227,225,228,230,235,255,255,
255,255,255,255,255,31,103,231,143,159,231,249,252,253,255,252,
249,242,243,252,252,254,126,126,255,255,248,243,250,248,252,248,
253,241,198,206,14,255,255,255,255,255,255,255,252,250,249,249,
254,189,123,254,255,255,255,255,254,126,126,252,254,254,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,254,254,254,252,252,252,255,127,63,31,31,31,31,
127,255,255,255,255,255,247,247,231,227,255,255,255,253,252,108,
};

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

#define RDD_MAX_BOMBS 16

#define RDD_STATE_INTRO 0
#define RDD_STATE_PLAYING 1
#define RDD_STATE_GAMEOVER 2

#define RDD_INTRO_TEXT_FRAMES 60
#define RDD_INTRO_SPLASH_FRAMES 120

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

int rddState;
int rddStateTimer;

int rddPlayerX;
int rddPlayerStatus; // 0 idle, 1 right, 2 left
int rddQuadFrameCount;
int rddPlayerStep;

int[16] rddBombX;
int[16] rddBombY;
int[16] rddBombStatus;
int rddTotalBombs;

int rddScore;
int rddTopScore;

int rddFrameDelay; // ticks between logic updates, frame units
int rddTickCounter;
int rddBombDelay; // frames between spawn-rate escalation checks
int rddBombTimer;

int rddStopAnimate;

int rddGameOverIdx;
int rddGameOverTimer;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

int rddFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return rddFont[ ( ch - 32 ) * 6 + col ];
}

int rddTextByteAt( int* text, int textLen, int startX, int x )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= textLen ) return 0;
    return rddFontByte( text[ charIdx ], rel % 6 );
}

int[4] rddScoreDigits;
int rddScoreDigitCount;
int[4] rddTopScoreDigits;
int rddTopScoreDigitCount;
int[4] rddLiveScoreDigits;
int rddLiveScoreDigitCount;

void rddDigitsInto( int value, int* outBuf, int* outCount )
{
    if( value < 0 ) value = 0;
    if( value > 9999 ) value = 9999;

    if( value == 0 )
    {
        outBuf[ 0 ] = 48;
        *outCount = 1;
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
    for( i = 0; i < n; i++ ) outBuf[ i ] = 48 + digits[ n - 1 - i ];
    *outCount = n;
}

void rddStartGameOverSweep()
{
    rddGameOverIdx = 0;
    rddGameOverTimer = 0;
}

void rddAdvanceGameOverSweep()
{
    if( rddGameOverIdx >= 16 ) return;
    rddGameOverTimer = rddGameOverTimer - 1;
    if( rddGameOverTimer <= 0 )
    {
        int freq = 700 - rddGameOverIdx * 40 + arand( 80 );
        if( freq < 80 ) freq = 80;
        md_playTone( freq, 0.04 );
        rddGameOverIdx = rddGameOverIdx + 1;
        rddGameOverTimer = 3;
    }
}

// Returns the byte for one column of the player sprite (5 wide, page-
// aligned - unlike the bombs, no sub-page slicing needed at all).
int rddPlayerByteAt( int col )
{
    if( ( rddPlayerStatus == 1 ) && ( rddQuadFrameCount < 2 ) ) return rddPlayerRight1[ col ];
    if( rddPlayerStatus == 1 ) return rddPlayerRight2[ col ];
    if( ( rddPlayerStatus == 2 ) && ( rddQuadFrameCount < 2 ) ) return rddPlayerLeft1[ col ];
    if( rddPlayerStatus == 2 ) return rddPlayerLeft2[ col ];
    if( rddQuadFrameCount < 2 ) return rddPlayerIdle1[ col ];
    return rddPlayerIdle2[ col ];
}

// Returns the composited byte for one column (0-6) of a bomb sprite within
// a given real hardware page. The bomb's own bottom edge sits at pixel row
// bombY (falls in from the top, revealing bottom-first) - a page's own
// 8-pixel window only ever needs a *shift* of the sprite's 16-bit column
// (top+bottom byte combined) to land in the right place, not an 8-
// iteration per-bit scan through a second function - a real, measured CPU
// cost found via the perf overlay once RDD_MAX_BOMBS(16) bombs were live
// simultaneously (see rddBuildBombPageBuffer()'s own comment): the
// original bit-scan version cost 2 nested function calls per bit x 8 bits
// x up to 16 bombs x 7 columns x ~2 matching pages/frame, easily thousands
// of real function calls/frame just to resolve bomb pixels.
//
// The caller (rddBuildBombPageBuffer) already verified this bomb's row
// range overlaps this page - VIRCON32_C_DIALECT.md documents `>>` as a
// *logical*, not arithmetic, shift, so a shift amount landing negative
// (this project's own repeatedly-found bug class - see HollowSeeker's
// hsDivByColumnW/Tiny DDug's tddugSafeShiftDiv* for the same fix shape)
// is handled by branching on sign and only ever shifting a non-negative
// amount, rather than trusting a negative shift to behave like AVR would.
int rddBombColByte( int bombIdx, int col, int page )
{
    int topByte = ( ~rddBomb1[ col ] ) & 0xFF;     // stored inverted, matching upstream's own `~bomb1[...]`
    int botByte = ( ~rddBomb1[ col + 7 ] ) & 0xFF;
    int src16 = topByte | ( botByte << 8 ); // bit r (0-15) = sprite pixel at row r

    int by = rddBombY[ bombIdx ];
    int pageTop = page * 8;
    int shift = pageTop - by + 15; // output bit b = src16 bit (shift + b)

    if( shift >= 0 ) return ( src16 >> shift ) & 0xFF;
    return ( src16 << ( -shift ) ) & 0xFF;
}

// -----------------------------------------------------------------------------
// Game logic
// -----------------------------------------------------------------------------

void rddResetGame()
{
    int i;
    for( i = 0; i < RDD_MAX_BOMBS; i++ )
    {
        rddBombX[ i ] = 0;
        rddBombY[ i ] = 0;
        rddBombStatus[ i ] = 0;
    }
    rddTotalBombs = 1;
    rddBombDelay = 90;  // 1500ms @ 60fps
    rddBombTimer = rddBombDelay;
    rddFrameDelay = 2;  // 40ms @ 60fps
    rddTickCounter = rddFrameDelay;
    rddScore = 0;
    rddStopAnimate = 0;

    rddPlayerX = 62;
    rddPlayerStatus = 0;
    rddQuadFrameCount = 0;
    rddPlayerStep = 2;

    rddBombY[ 0 ] = arand( 64 );

    rddState = RDD_STATE_INTRO;
    rddStateTimer = RDD_INTRO_TEXT_FRAMES;
}

void rddUpdateBombs()
{
    rddBombTimer = rddBombTimer - 1;
    if( rddBombTimer <= 0 )
    {
        rddBombTimer = rddBombDelay;
        if( ( rddScore < 10 ) && ( rddScore > 0 ) )
        {
            rddTotalBombs = rddTotalBombs + 1;
        }
        else if( ( rddScore > 100 ) && ( rddScore < 1000 ) )
        {
            rddBombDelay = 180; // 3000ms @ 60fps
            if( rddFrameDelay > 0 ) rddFrameDelay = rddFrameDelay - 1;
        }
        else if( rddScore > 1000 )
        {
            if( rddTotalBombs < RDD_MAX_BOMBS ) rddTotalBombs = rddTotalBombs + 1;
        }
    }

    int bi;
    for( bi = 0; bi < rddTotalBombs; bi++ )
    {
        if( rddBombStatus[ bi ] != 1 )
        {
            rddBombStatus[ bi ] = 1;
            rddBombX[ bi ] = arand( 120 );
            rddBombY[ bi ] = 0;
        }
    }

    int tb;
    for( tb = 0; tb < rddTotalBombs; tb++ )
    {
        if( rddBombStatus[ tb ] == 1 )
        {
            rddBombY[ tb ] = rddBombY[ tb ] + 1;
            if( rddBombY[ tb ] >= 63 ) rddBombY[ tb ] = 0;

            if( ( rddBombY[ tb ] > 56 ) && ( rddPlayerX < rddBombX[ tb ] + 7 ) && ( rddPlayerX + 5 > rddBombX[ tb ] ) )
            {
                rddStopAnimate = 1;
            }

            if( rddBombY[ tb ] == 62 )
            {
                rddBombStatus[ tb ] = 0;
                rddScore = rddScore + 1;
            }
        }
    }
}

void rddTick()
{
    rddPlayerStatus = 0;
    if( isLeftPressed() )
    {
        rddPlayerStatus = 2;
        if( rddPlayerX > 0 ) rddPlayerX = rddPlayerX - rddPlayerStep;
    }
    if( isRightPressed() )
    {
        rddPlayerStatus = 1;
        if( rddPlayerX < 121 ) rddPlayerX = rddPlayerX + rddPlayerStep;
    }

    rddQuadFrameCount = rddQuadFrameCount + 1;
    if( rddQuadFrameCount >= 3 ) rddQuadFrameCount = 0;

    rddUpdateBombs();

    if( rddStopAnimate )
    {
        // Direct translation of upstream's own 2-byte big-endian
        // EEPROM.write(1,...)/EEPROM.write(0,...) topScore save.
        if( rddScore > rddTopScore ) { rddTopScore = rddScore; eeprom_write_word( 0, rddTopScore ); }
        rddState = RDD_STATE_GAMEOVER;
        rddStartGameOverSweep();
    }
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

// Composited once per page/row instead of rescanned per pixel - see
// rddBuildBombPageBuffer(). With rddTotalBombs able to climb as high as
// RDD_MAX_BOMBS (16) once score passes 1000 (rddUpdateBombs() keeps
// incrementing it every rddBombDelay-frame cycle with no upper check
// beyond the array bound), the old per-pixel bomb loop cost up to
// 128 columns x 8 pages x 16 bombs = 16384 status/range checks/frame -
// reported by the user as a 100% CPU spike specifically once score
// crossed 1000, exactly where rddTotalBombs starts climbing.
int[128] rddBombPageBuf;

void rddBuildBombPageBuffer( int page )
{
    int x;
    for( x = 0; x < 128; x++ ) rddBombPageBuf[ x ] = 0;

    int bi;
    for( bi = 0; bi < rddTotalBombs; bi++ )
    {
        if( rddBombStatus[ bi ] == 1 )
        {
            int bx = rddBombX[ bi ];
            int by = rddBombY[ bi ];

            if( by > 59 )
            {
                if( page == 7 )
                {
                    int col;
                    for( col = 0; col < 7; col++ )
                    {
                        int x2 = bx + col;
                        if( ( x2 >= 0 ) && ( x2 < 128 ) )
                            rddBombPageBuf[ x2 ] = rddBombPageBuf[ x2 ] | rddExplosion[ col ];
                    }
                }
            }
            else
            {
                int pageTop = page * 8;
                if( ( pageTop + 7 >= by - 15 ) && ( pageTop <= by ) )
                {
                    int col;
                    for( col = 0; col < 7; col++ )
                    {
                        int x2 = bx + col;
                        if( ( x2 >= 0 ) && ( x2 < 128 ) )
                            rddBombPageBuf[ x2 ] = rddBombPageBuf[ x2 ] | rddBombColByte( bi, col, page );
                    }
                }
            }
        }
    }
}

int rddComputeByte( int x, int page )
{
    if( rddState == RDD_STATE_INTRO )
    {
        if( rddStateTimer > RDD_INTRO_SPLASH_FRAMES )
        {
            if( page == 4 ) return rddTextByteAt( "webboggles.com", 14, 18, x );
            if( page == 6 ) return rddTextByteAt( "Tweet @webboggles", 17, 12, x );
            if( page == 7 ) return rddTextByteAt( "#AttinyArcade", 13, 22, x );
            return 0;
        }
        // stored inverted (matches upstream's own draw_bmp ~byte read) -
        // a real bitwise NOT, not a logical one, so mask back to a byte
        // afterward (this dialect's ints are full 32-bit, no implicit
        // narrowing the way AVR's uint8_t gave upstream - same class of
        // fix as this project's very first documented bug).
        return ( ~rddSplash[ page * 128 + x ] ) & 0xFF;
    }

    if( rddState == RDD_STATE_GAMEOVER )
    {
        if( page == 3 ) return rddTextByteAt( "Game Over", 9, 32, x );
        if( page == 5 )
        {
            int pixels = rddTextByteAt( "score:", 6, 32, x );
            pixels = pixels | rddTextByteAt( rddScoreDigits, rddScoreDigitCount, 70, x );
            return pixels;
        }
        if( page == 6 )
        {
            int pixels = rddTextByteAt( "top score:", 10, 32, x );
            pixels = pixels | rddTextByteAt( rddTopScoreDigits, rddTopScoreDigitCount, 90, x );
            return pixels;
        }
        return 0;
    }

    // RDD_STATE_PLAYING
    int pixels = 0;

    if( page == 0 )
    {
        pixels = pixels | rddTextByteAt( rddLiveScoreDigits, rddLiveScoreDigitCount, 92, x );
    }

    if( page == 7 )
    {
        if( ( x >= rddPlayerX ) && ( x < rddPlayerX + 5 ) )
        {
            pixels = pixels | rddPlayerByteAt( x - rddPlayerX );
        }
    }

    // rddBombPageBuf is rebuilt once per page by rddRenderImage() before
    // this function is called for any column in that page - see
    // rddBuildBombPageBuffer()'s own comment for why.
    pixels = pixels | rddBombPageBuf[ x ];

    return pixels;
}

void rddRenderImage()
{
    md_beginFrame();

    if( rddState == RDD_STATE_GAMEOVER )
    {
        rddDigitsInto( rddScore, rddScoreDigits, &rddScoreDigitCount );
        rddDigitsInto( rddTopScore, rddTopScoreDigits, &rddTopScoreDigitCount );
    }
    else if( rddState == RDD_STATE_PLAYING )
    {
        rddDigitsInto( rddScore, rddLiveScoreDigits, &rddLiveScoreDigitCount );
    }

    int x, page;
    for( page = 0; page < 8; page++ )
    {
        // Only PLAYING actually has bombs on screen - skip the rebuild
        // entirely for the INTRO/GAMEOVER states (rddBombPageBuf is left
        // however it last was, but rddComputeByte only ever reads it in
        // the PLAYING branch, so a stale buffer is never observed).
        if( rddState == RDD_STATE_PLAYING ) rddBuildBombPageBuffer( page );

        for( x = 0; x < 128; x++ )
        {
            md_drawColumn( x, page, rddComputeByte( x, page ) );
        }
    }
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

void gameRunDudeRun_forceRedraw()
{
}

void gameRunDudeRun_init()
{
    InitTinyJoypad();
    // Direct translation of upstream's own topScore = EEPROM.read(0)<<8 |
    // EEPROM.read(1). Upstream also has its own explicit "if(topScore<0)
    // reset to 0" guard here - on real AVR hardware, a virgin (0xFF,0xFF)
    // read composes into a *negative* 16-bit int (0xFF00 has its sign bit
    // set), not a large positive one; Vircon32's own wider 32-bit int
    // arithmetic instead composes the exact same virgin bytes into a
    // large *positive* 65535, so the equivalent guard here checks for
    // that value instead, matching every other game in this pass.
    rddTopScore = eeprom_read_word( 0 );
    if( rddTopScore == 65535 ) rddTopScore = 0;
    rddResetGame();
}

void gameRunDudeRun_update()
{
    if( rddState == RDD_STATE_INTRO )
    {
        rddStateTimer = rddStateTimer - 1;
        if( rddStateTimer <= 0 )
        {
            if( rddStateTimer <= -RDD_INTRO_SPLASH_FRAMES )
            {
                rddState = RDD_STATE_PLAYING;
                rddTickCounter = rddFrameDelay;
            }
        }
    }
    else if( rddState == RDD_STATE_PLAYING )
    {
        rddTickCounter = rddTickCounter - 1;
        if( rddTickCounter <= 0 )
        {
            rddTickCounter = rddFrameDelay;
            rddTick();
        }
    }
    else if( rddState == RDD_STATE_GAMEOVER )
    {
        rddAdvanceGameOverSweep();
        if( isFirePressed() ) rddResetGame();
    }

    rddRenderImage();
}

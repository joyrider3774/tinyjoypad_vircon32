// =============================================================================
// SnakeGame85 - ported from snakeGame85.ino (github.com/terezaza/SnakeGame85,
// GPLv3). No individual author name appears anywhere in the upstream source
// (.ino, oled85.h/.cpp, README) - credited in the menu by the repo's own
// GitHub handle, "TEREZAZA", the only identifying name available anywhere in
// the project.
//
// A classic 4-directional Nokia-style Snake on a 16x8 grid (genuinely
// different control feel from this project's already-shipped Oroboros, whose
// own upstream only ever supports relative turn-left/turn-right - this game
// reads absolute up/down/left/right instead).
//
// Not tinyJoypadShim/obonoCoreShim lineage by name - genuine bespoke
// #AttinyArcade-style hardware (a real SSD1306 at I2C 0x3C, exact hardware
// match to every other game in this project, confirmed via oled85.h) but its
// own two-analog-pin, four-button ladder (LEFT_RIGHT/A3 and UP_DOWN/A2, each
// wired to two buttons at two different thresholds) has no fixed "left/
// right/up/down" meaning of its own - upstream's own "button 1"/"button 2"/
// "button 3"/"button 4" labels don't map onto isLeftPressed()/etc in any
// single obviously-correct way. Mapped instead to whatever produces natural
// d-pad movement (isLeftPressed() -> -X, isRightPressed() -> +X,
// isUpPressed() -> -Y, isDownPressed() -> +Y) - a deliberate remap for
// gamepad naturalness, the same kind of call already made for Tiny Lander's
// thrust controls and Tiny Arkanoid's paddle axis. isFirePressed() is never
// read at all - this game's own native input surface has no fire button,
// only the four directions (which also double as "any button" to start/
// restart, exactly as upstream's own checkButtonStateChange() attract-wait
// treats them).
//
// A real, non-obvious discovery from reading oled85.cpp's own constructor:
// OLED85::OLED85() calls sendCommand(INVERT_DISPLAY) once, permanently,
// right after the initial fillScreen(0) - meaning this game runs its ENTIRE
// real display in SSD1306 hardware-inverted mode from boot onward (0=lit,
// 1=dark), not the normal polarity every other game in this project uses.
// blinkScreen()'s own NORMAL_DISPLAY/INVERT_DISPLAY toggle pairs (used once,
// for the game-over flash) are relative to that same permanently-inverted
// baseline, and its own 3-iteration loop happens to end back on
// INVERT_DISPLAY, restoring the baseline exactly. Reproduced here with a
// single `snkInvertFrame` flag applied uniformly to every rendered byte in
// snkRenderImage() (default 1, i.e. "hardware-inverted", the correct
// baseline for every state; only the game-over blink's own NORMAL-phase
// half-steps briefly clear it to 0) - this is why the grid-dot pattern
// (raw byte 0x10, a single lit-bit dot) actually reads as a mostly-white
// cell with a single black dot once inverted, and why an occupied cell
// (raw 0x7E) reads as a mostly-black block with a thin white border - both
// verified by working through the real hardware polarity rather than
// assumed from the raw byte values alone.
//
// Rendering model: oled85.cpp's own drawBlock()/removeBlock() functions
// operate on the exact same byte-per-(column,page) primitive this whole
// project already uses (`x *= NUM_PAGES` in drawBlock() confirms x/y really
// are 16x8 grid units, one 8px-wide, 1-page-tall cell each - matching
// md_drawColumn()'s own convention exactly) - no new shim needed. Rather
// than replicate oled85's own incremental draw/erase-a-single-cell calls,
// this port (like most others here) just rebuilds the whole 16x8 occupancy
// grid from scratch every tick and redraws the complete screen every real
// engine frame - avoids any VRAM-persistence risk from the start, matching
// this project's own established, repeatedly-necessary lesson from several
// other ports (Pinball/Doc/Bert/Tris/Pipe/Plaque all needed this fix
// retroactively; applied proactively here instead).
//
// Upstream's own tick pacing is real, variable analogRead()-plus-delay()
// wall-clock time (moveSnake()'s own delay(150), plus changeNextMove()'s own
// delay(150) on an actual direction change, plus the main loop's trailing
// delay(100) - roughly 250-400ms/tick depending on whether a turn happened
// that tick). Approximated with a single flat whole-function tick divisor
// (SNK_TICK_DIVISOR, ~4 ticks/sec) rather than replicating the variable
// real-delay timing exactly - the same kind of single-representative-rate
// simplification already used for several other ports here (e.g. Tiny Pipe/
// Doc/Bike) rather than modeling upstream's own real-time jitter.
//
// changeNextMove() is a genuine synchronous level-read (analogRead()) called
// directly inside the tick body upstream, not an interrupt-latched pending
// flag the way Oroboros's own hardware works - so this port reads input
// once per discrete engine tick (not edge-detected/queued across real
// frames the way Oroboros needed), matching upstream's own polling
// structure directly. Upstream calls changeNextMove() twice per loop
// iteration (once before moveSnake(), once after) purely to give the real
// analog hardware a second chance to catch a direction change before the
// loop's own delay(100) - with an instantaneous digital gamepad read there
// is nothing extra a second call within the same tick could catch, so only
// one call per tick is made here.
//
// Upstream's oversized synchronous tune loops (tinyTune(), a busy real-time
// PWM buzz) are converted to short frame-stepped note sequencers, matching
// the established pattern from every other port's own oversized sound-sweep
// fix - frequencies/durations derived directly from tinyTune(down,up,times)'s
// own real PWM period (freqHz = 1000/(down+up), durSec = (down+up)*times/
// 1000), not guessed.
// =============================================================================

#define SNK_GRID_W 16
#define SNK_GRID_H 8

#define SNK_STATE_ATTRACT 0
#define SNK_STATE_START_TUNE 1
#define SNK_STATE_RESET_WAIT 2
#define SNK_STATE_PLAYING 3
#define SNK_STATE_GAMEOVER_WAIT1 4
#define SNK_STATE_GAMEOVER_TUNE 5
#define SNK_STATE_GAMEOVER_BLINK 6
#define SNK_STATE_GAMEOVER_WAIT2 7
#define SNK_STATE_GAMEOVER_SCORE 8

#define SNK_TICK_DIVISOR 15          // ~4 ticks/sec @60fps, approximating upstream's ~250-400ms real tick period
#define SNK_ATTRACT_BLINK_FRAMES 30  // 500ms @ 60fps, matches upstream's own blink interval exactly
#define SNK_RESET_WAIT_FRAMES 21     // ~350ms: reset()'s own delay(50) + the pre-placeDot() delay(300)
#define SNK_GAMEOVER_WAIT1_FRAMES 18 // 300ms, matches upstream's own delay(300) before the game-over tune
#define SNK_GAMEOVER_BLINK_HALFSTEP_FRAMES 12 // 200ms, matches blinkScreen()'s own delay(200) per half-step
#define SNK_GAMEOVER_WAIT2_FRAMES 6  // 100ms, matches upstream's own delay(100) before the score screen

// -----------------------------------------------------------------------------
// Raw SSD1306 bitmaps (extracted byte-for-byte from oled85.h's own
// LOAD_SCREEN[]/SCORE[] PROGMEM tables via a small Python script, not
// hand-transcribed - matching this project's own established "byte-diff
// extracted tables" discipline). Page-major, 128 columns x 8 pages, same
// layout drawImage()'s own loop streams them in.
// -----------------------------------------------------------------------------

int[1024] snkLoadScreenData =
{
0,0,0,0,0,192,192,0,0,0,192,192,0,0,0,128,192,192,192,192,
128,0,192,192,192,128,0,0,192,192,192,128,0,0,0,192,128,128,192,192,
192,0,0,0,128,192,192,192,128,192,192,0,0,0,0,0,0,0,192,240,
184,24,24,0,0,192,192,0,0,0,192,192,0,0,192,192,128,192,192,192,
128,0,0,0,0,0,128,192,192,192,192,128,0,128,192,192,192,192,128,0,
128,192,192,192,192,128,0,0,0,0,128,192,128,0,0,128,192,192,192,192,
248,248,0,0,0,0,0,0,0,0,0,0,0,31,63,48,48,48,63,63,
0,0,31,63,49,48,48,63,31,6,32,48,255,255,48,32,48,48,255,255,
48,0,0,63,63,0,0,1,63,63,0,14,31,63,48,48,48,255,255,0,
0,0,0,0,56,63,7,1,15,62,48,0,0,63,63,48,48,16,63,63,
0,0,31,63,52,36,54,51,3,0,0,0,0,1,51,55,38,60,60,0,
0,51,55,38,60,60,24,0,28,60,52,36,53,63,15,32,48,48,31,63,
63,0,14,63,63,48,48,48,63,63,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,192,96,32,96,64,192,128,128,0,0,0,0,
0,0,192,96,32,96,64,192,128,128,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,128,128,192,64,96,32,96,192,0,0,
0,0,0,0,128,128,192,64,96,32,96,192,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,129,195,66,102,
36,52,28,153,153,195,66,102,36,60,129,195,66,102,36,52,28,153,153,195,
66,102,36,60,60,38,34,34,34,34,34,62,60,38,34,34,34,34,34,62,
60,38,34,34,34,34,34,62,60,38,34,34,34,34,34,62,60,38,34,34,
34,34,34,62,60,38,34,34,34,34,34,62,60,36,102,66,195,153,153,28,
52,36,102,66,195,129,60,36,102,66,195,153,153,28,52,36,102,66,195,129,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,3,6,4,6,2,3,1,1,128,192,192,192,192,192,195,198,
196,198,194,195,193,129,128,192,192,192,192,192,192,128,0,0,0,0,0,128,
192,192,192,192,128,0,128,192,192,192,192,128,0,0,0,0,0,0,0,128,
192,192,192,192,192,128,0,128,192,192,192,192,192,128,0,0,0,0,128,192,
192,192,192,128,0,0,1,1,3,130,198,196,198,195,192,192,192,192,192,192,
129,1,3,2,6,4,6,3,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,
159,240,120,120,120,120,112,120,112,0,0,0,0,255,255,6,12,24,48,96,
192,129,3,6,12,30,63,33,0,0,0,0,255,255,31,248,224,0,0,1,
7,6,134,134,134,134,7,7,0,0,128,240,124,15,255,255,0,0,0,0,
128,1,7,30,120,255,255,0,0,0,0,255,255,112,252,142,3,1,0,0,
48,120,120,120,120,216,240,176,97,63,30,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,199,239,56,56,56,56,56,56,56,56,0,0,0,
0,255,255,0,128,192,96,48,24,13,7,130,192,224,240,16,0,0,0,0,
255,255,0,0,7,31,248,224,0,0,1,15,7,0,0,128,240,124,15,3,
0,0,255,255,0,0,0,0,255,254,56,240,192,1,7,0,0,0,0,255,
255,0,193,227,54,30,60,60,56,56,56,56,48,0,0,1,131,254,124,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,7,12,12,12,
12,12,12,12,12,12,12,12,12,7,3,7,13,12,12,12,12,6,3,1,
0,1,3,6,12,12,12,12,7,3,0,0,0,0,0,3,7,12,12,12,
12,12,6,3,1,0,0,0,0,1,3,6,12,12,12,6,3,1,0,0,
1,3,6,12,12,12,12,7,3,0,0,1,3,6,6,12,12,12,12,12,
12,6,6,3,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,
};

int[1024] snkScoreBgData =
{
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,192,64,96,32,176,144,216,72,104,40,248,
64,64,64,64,64,192,64,64,64,64,64,192,64,64,64,64,
64,192,64,64,64,64,64,192,64,64,64,64,64,192,64,64,
64,64,64,192,192,96,32,48,144,152,200,72,104,56,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,3,2,6,4,12,9,25,19,18,22,29,
3,2,2,2,2,3,3,2,2,2,2,3,3,2,2,2,
2,3,3,2,2,2,2,3,2,2,2,2,2,3,2,2,
2,2,2,3,3,2,6,4,13,9,27,18,22,28,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,56,236,196,196,196,196,4,4,12,
248,60,228,132,12,24,112,248,12,4,4,252,192,112,24,8,
140,196,100,68,204,8,24,48,224,120,76,68,100,68,196,140,
8,24,112,192,248,8,12,68,228,228,196,204,72,120,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,112,223,140,204,140,140,128,128,192,
127,0,62,99,64,64,204,140,128,128,128,255,15,56,96,64,
199,140,152,136,207,64,96,48,31,120,200,136,152,136,140,199,
64,96,56,15,123,78,204,140,156,136,192,65,99,62,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,192,64,96,32,176,144,152,200,72,104,248,
64,64,64,64,64,192,64,64,64,64,64,192,64,64,64,64,
64,192,64,64,64,64,64,192,64,64,64,64,64,192,64,64,
64,64,64,192,192,96,32,48,144,144,216,72,104,56,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,3,2,6,4,13,9,25,19,18,22,29,
3,2,2,2,2,3,3,2,2,2,2,3,2,2,2,2,
2,3,2,2,2,2,2,3,2,2,2,2,2,3,2,2,
2,2,2,3,3,6,4,12,9,25,19,18,22,28,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// -----------------------------------------------------------------------------
// displayScore()'s own 13-segment digit table, extracted byte-for-byte from
// oled85.cpp - full[] gives each segment's own (col,row) grid position,
// numbers[10][13] which segments are lit for each digit 0-9.
// -----------------------------------------------------------------------------

int[26] snkDigitFull =
{
10,1, 11,1, 12,1, 10,2, 12,2, 10,3, 11,3, 12,3, 10,4, 12,4, 10,5, 11,5, 12,5,
};

int[130] snkDigitPattern =
{
1,1,1,1,1,1,0,1,1,1,1,1,1, // 0
0,0,1,0,1,0,0,1,0,1,0,0,1, // 1
1,1,1,0,1,1,1,1,1,0,1,1,1, // 2
1,1,1,0,1,1,1,1,0,1,1,1,1, // 3
1,0,1,1,1,1,1,1,0,1,0,0,1, // 4
1,1,1,1,0,1,1,1,0,1,1,1,1, // 5
1,1,1,1,0,1,1,1,1,1,1,1,1, // 6
1,1,1,0,1,0,0,1,0,1,0,0,1, // 7
1,1,1,1,1,1,1,1,1,1,1,1,1, // 8
1,1,1,1,1,1,1,1,0,1,1,1,1, // 9
};

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

int snkState;
int snkStateTimer;

int[50] snkSnakeX;
int[50] snkSnakeY;
int snkSnakeLen;
int snkNextMoveX;
int snkNextMoveY;

int snkDotX;
int snkDotY;

int[128] snkOccupied; // SNK_GRID_H * SNK_GRID_W, 1 = snake segment or dot here

int snkTickCounter;

int snkAttractBlinkState;
int snkAttractBlinkTimer;

int snkInvertFrame; // see this file's own header comment on the permanent hardware INVERT_DISPLAY finding

int snkBlinkHalfStep;
int snkBlinkHalfTimer;

int[128] snkScoreLit; // SNK_GRID_H * SNK_GRID_W, built once entering the score screen

// start-of-game tune (4 notes, ascending - matches
// `for(i=4;i>=1;i--) tinyTune(i,1,25)`, freqHz = 1000/(down+up),
// durSec = (down+up)*25/1000)
int[4] snkStartFreq = { 200, 250, 333, 500, };
int[4] snkStartDurFrames = { 8, 6, 5, 3, }; // (down+up)*25ms @60fps, rounded
int snkStartIdx;
int snkStartTimer;

// game-over tune (4 notes, descending - matches `for(i=5;i<9;i++) tinyTune(i,1,25)`)
int[4] snkGoFreq = { 167, 143, 125, 111, };
int[4] snkGoDurFrames = { 9, 11, 12, 14, }; // (down+up)*25ms @60fps, rounded
int snkGoIdx;
int snkGoTimer;

// -----------------------------------------------------------------------------
// Sound
// -----------------------------------------------------------------------------

void snkPlayStartTune()
{
    snkStartIdx = 0;
    snkStartTimer = 0;
}

// returns 1 while still playing, 0 once the sequence (including its final
// note's own wait) has finished - same shape as every other per-game note
// sequencer in this project (see gameOroboros.c's own orbAdvanceIntroJingle)
int snkAdvanceStartTune()
{
    if( snkStartIdx >= 4 ) return 0;
    snkStartTimer = snkStartTimer - 1;
    if( snkStartTimer <= 0 )
    {
        md_playTone( snkStartFreq[ snkStartIdx ], snkStartDurFrames[ snkStartIdx ] / 60.0 );
        snkStartTimer = snkStartDurFrames[ snkStartIdx ];
        snkStartIdx = snkStartIdx + 1;
    }
    return 1;
}

void snkPlayGameOverTune()
{
    snkGoIdx = 0;
    snkGoTimer = 0;
}

int snkAdvanceGameOverTune()
{
    if( snkGoIdx >= 4 ) return 0;
    snkGoTimer = snkGoTimer - 1;
    if( snkGoTimer <= 0 )
    {
        md_playTone( snkGoFreq[ snkGoIdx ], snkGoDurFrames[ snkGoIdx ] / 60.0 );
        snkGoTimer = snkGoDurFrames[ snkGoIdx ];
        snkGoIdx = snkGoIdx + 1;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// Game logic
// -----------------------------------------------------------------------------

void snkEnterAttract()
{
    snkState = SNK_STATE_ATTRACT;
    snkAttractBlinkState = 0;
    snkAttractBlinkTimer = SNK_ATTRACT_BLINK_FRAMES;
}

void snkPlaceDot()
{
    int tries = 0;
    int placed = 0;
    while( ( placed == 0 ) && ( tries < 50 ) )
    {
        int x = 1 + arand( 14 );
        int y = 1 + arand( 6 );
        int free = 1;
        int i;
        for( i = 0; i < snkSnakeLen; i++ )
        {
            if( ( snkSnakeX[ i ] == x ) && ( snkSnakeY[ i ] == y ) ) free = 0;
        }
        if( free )
        {
            snkDotX = x;
            snkDotY = y;
            placed = 1;
        }
        tries = tries + 1;
    }
}

// matches reset(): fresh 3-segment snake at grid column 1, rows 1-3, moving
// in the +X direction - the true initial spawn position is never actually
// rendered on screen (see this file's own header note on moveSnake()'s
// first-call behavior), so no render needs to happen here.
void snkResetGame()
{
    snkSnakeLen = 3;
    snkNextMoveX = 1;
    snkNextMoveY = 0;

    snkSnakeX[ 0 ] = 1; snkSnakeY[ 0 ] = 1;
    snkSnakeX[ 1 ] = 1; snkSnakeY[ 1 ] = 2;
    snkSnakeX[ 2 ] = 1; snkSnakeY[ 2 ] = 3;

    int i;
    for( i = 0; i < 128; i++ ) snkOccupied[ i ] = 0;
}

// matches changeNextMove()'s own if/else-if priority chain and anti-reversal
// guards, remapped onto natural d-pad directions (see this file's own header
// comment on why isLeftPressed()/etc rather than upstream's own raw
// button-1..4 labels).
//
// The raw snkSnakeX/Y deltas here are in SOURCE grid space, not physical
// screen space - and per snkRenderImage()'s own header comment, physical
// screen position is a full point-reflection of source space (physical col
// = 15-rawCol, physical row = 7-rawRow, at grid scale). So a raw +X delta
// (the same "+1" upstream's own reset() comment calls "left") actually
// moves the snake toward smaller physical columns - visually LEFT, exactly
// matching upstream's own comment - and a raw +Y delta moves toward a
// smaller physical page - visually UP. Both axes are therefore inverted
// here relative to a naive "Right press -> +X" mapping, confirmed needed
// via a live user report right after the display-orientation fix shipped
// ("controls are inverted now also").
void snkChangeNextMove()
{
    if( isLeftPressed() && ( snkNextMoveX != -1 ) )
    {
        snkNextMoveX = 1;
        snkNextMoveY = 0;
    }
    else if( isRightPressed() && ( snkNextMoveX != 1 ) )
    {
        snkNextMoveX = -1;
        snkNextMoveY = 0;
    }
    else if( isUpPressed() && ( snkNextMoveY != -1 ) )
    {
        snkNextMoveX = 0;
        snkNextMoveY = 1;
    }
    else if( isDownPressed() && ( snkNextMoveY != 1 ) )
    {
        snkNextMoveX = 0;
        snkNextMoveY = -1;
    }
}

// matches moveSnake(): shift every segment toward the tail, then advance
// the head by the current direction
void snkMoveSnake()
{
    int i;
    for( i = snkSnakeLen - 1; i > 0; i = i - 1 )
    {
        snkSnakeX[ i ] = snkSnakeX[ i - 1 ];
        snkSnakeY[ i ] = snkSnakeY[ i - 1 ];
    }
    snkSnakeX[ 0 ] = snkSnakeX[ 0 ] + snkNextMoveX;
    snkSnakeY[ 0 ] = snkSnakeY[ 0 ] + snkNextMoveY;
}

// matches gameOver(): self-collision against every body segment but the
// head, plus the fixed 16x8 border
int snkIsGameOver()
{
    int i;
    for( i = 1; i < snkSnakeLen; i++ )
    {
        if( ( snkSnakeX[ i ] == snkSnakeX[ 0 ] ) && ( snkSnakeY[ i ] == snkSnakeY[ 0 ] ) ) return 1;
    }
    if( ( snkSnakeX[ 0 ] <= 0 ) || ( snkSnakeX[ 0 ] >= 15 ) || ( snkSnakeY[ 0 ] <= 0 ) || ( snkSnakeY[ 0 ] >= 7 ) ) return 1;
    return 0;
}

void snkRebuildOccupied()
{
    int i;
    for( i = 0; i < 128; i++ ) snkOccupied[ i ] = 0;
    for( i = 0; i < snkSnakeLen; i++ )
    {
        int gx = snkSnakeX[ i ];
        int gy = snkSnakeY[ i ];
        if( ( gx >= 0 ) && ( gx < SNK_GRID_W ) && ( gy >= 0 ) && ( gy < SNK_GRID_H ) )
        {
            snkOccupied[ gy * SNK_GRID_W + gx ] = 1;
        }
    }
    snkOccupied[ snkDotY * SNK_GRID_W + snkDotX ] = 1;
}

void snkBuildScoreLit( int score )
{
    int i;
    for( i = 0; i < 128; i++ ) snkScoreLit[ i ] = 0;

    int tens = ( score / 10 ) % 10;
    int units = score % 10;
    if( tens < 0 ) tens = 0;
    if( units < 0 ) units = 0;

    for( i = 0; i < 13; i++ )
    {
        int col = snkDigitFull[ i * 2 ];
        int row = snkDigitFull[ i * 2 + 1 ];
        int tensGx = 16 - col + 1;
        int tensGy = 8 - row - 2;
        int unitsGx = tensGx - 4;
        int unitsGy = tensGy;

        if( snkDigitPattern[ tens * 13 + i ] )
        {
            if( ( tensGx >= 0 ) && ( tensGx < SNK_GRID_W ) && ( tensGy >= 0 ) && ( tensGy < SNK_GRID_H ) )
                snkScoreLit[ tensGy * SNK_GRID_W + tensGx ] = 1;
        }
        if( snkDigitPattern[ units * 13 + i ] )
        {
            if( ( unitsGx >= 0 ) && ( unitsGx < SNK_GRID_W ) && ( unitsGy >= 0 ) && ( unitsGy < SNK_GRID_H ) )
                snkScoreLit[ unitsGy * SNK_GRID_W + unitsGx ] = 1;
        }
    }
}

// matches one full iteration of upstream's `while(!gameOver()) {...}` body
void snkDoTick()
{
    snkChangeNextMove();
    snkMoveSnake();

    if( ( snkSnakeX[ 0 ] == snkDotX ) && ( snkSnakeY[ 0 ] == snkDotY ) )
    {
        md_playTone( 250, 0.04 );
        // Growing by simply incrementing snkSnakeLen would leave the new
        // tail slot's snkSnakeX/Y at whatever zero-initialized (or stale,
        // from a previous game) value was already sitting in the array -
        // a real, reachable grid cell (0,0), the board's own corner.
        // snkMoveSnake()'s own shift loop only starts writing that slot on
        // the *next* tick; snkRebuildOccupied() below runs THIS tick,
        // against the just-grown snkSnakeLen - so without this, the corner
        // cell would flash "occupied" for exactly one frame every time the
        // snake eats. Upstream doesn't have this gap (moveSnake() there
        // draws using the pre-growth length, since snakeLen++ happens
        // after it returns - the shift on the next real tick overwrites
        // the same stale slot before it's ever rendered), so this is a
        // genuine port-introduced bug, found via a direct user report
        // asking whether upstream really flashes a food item in a corner
        // (it doesn't, confirmed by re-reading placeDot()/moveSnake()).
        // Fixed by explicitly seeding the new tail at the old tail's own
        // position instead of leaving it at whatever was already there.
        if( snkSnakeLen < 50 )
        {
            int tailX = snkSnakeX[ snkSnakeLen - 1 ];
            int tailY = snkSnakeY[ snkSnakeLen - 1 ];
            snkSnakeLen = snkSnakeLen + 1;
            snkSnakeX[ snkSnakeLen - 1 ] = tailX;
            snkSnakeY[ snkSnakeLen - 1 ] = tailY;
        }
        snkPlaceDot();
    }

    snkRebuildOccupied();

    if( snkIsGameOver() )
    {
        snkState = SNK_STATE_GAMEOVER_WAIT1;
        snkStateTimer = SNK_GAMEOVER_WAIT1_FRAMES;
    }
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

int snkAttractByte( int x, int page )
{
    int idx = page * 128 + x;
    int data = snkLoadScreenData[ idx ];
    // matches drawImage()'s own `if (counter < 265 && blank_half) data = 0x00;`
    // (counter is checked post-increment there, so this is idx+1 < 265)
    if( snkAttractBlinkState && ( idx < 264 ) ) data = 0;
    return data;
}

int snkGridByte( int x, int page )
{
    int gx = x / 8;
    int gy = page;
    int cellOffset = x % 8;
    if( snkOccupied[ gy * SNK_GRID_W + gx ] )
    {
        if( cellOffset == 0 ) return 0;
        return 0x7E;
    }
    if( cellOffset == 4 ) return 0x10;
    return 0;
}

int snkScoreByte( int x, int page )
{
    int gx = x / 8;
    int gy = page;
    if( snkScoreLit[ gy * SNK_GRID_W + gx ] ) return 0xFF;
    return snkScoreBgData[ page * 128 + x ];
}

int snkComputeByte( int x, int page )
{
    if( ( snkState == SNK_STATE_ATTRACT ) || ( snkState == SNK_STATE_START_TUNE ) )
        return snkAttractByte( x, page );

    if( ( snkState == SNK_STATE_GAMEOVER_WAIT2 ) || ( snkState == SNK_STATE_GAMEOVER_SCORE ) )
        return snkScoreByte( x, page );

    // SNK_STATE_RESET_WAIT, SNK_STATE_PLAYING, SNK_STATE_GAMEOVER_WAIT1/
    // TUNE/BLINK all show the game field (live during RESET_WAIT/PLAYING,
    // frozen at the fatal frame during the game-over wait/tune/blink)
    return snkGridByte( x, page );
}

// oled85.h's own init_commands_list sends SEGMENT_REMAP 0xA1 and
// COM_SCAN_DECREASING - real hardware commands that reverse both the
// column-address-to-physical-column mapping AND the page/row scan
// direction, so what a real user actually sees is every byte's own source
// (col, page) physically placed at (127-col, 7-page) - AND, since
// COM_SCAN_DECREASING reverses the full 64-row scan order (not just which
// 8-row page-block lands where), each byte's own 8 bits are individually
// reversed too (bit b of the source byte lands on physical row 7-b within
// its new page). Net effect: every rendered byte needs a full point-
// reflection (reverse columns, reverse pages, reverse bits-within-byte) -
// confirmed by comparing an unreflected first attempt's screenshot (showed
// "SNAKE" upside-down and mirrored, i.e. exactly a 180-degree rotation)
// against this fix.
//
// A precomputed 256-entry lookup table (every byte's own bit-reversal is a
// pure function of its 8-bit value) rather than an 8-iteration shift/or
// loop run for every one of the 1024 pixels/frame - the loop version
// measurably pushed a real frame over Vircon32's 250,000-cycle/frame
// budget (confirmed via a live user report of a rendering glitch - a
// partial black region cut into the right side of the screen, exactly
// this project's own well-documented "frame truncated mid-instruction-
// stream" failure signature - immediately after this fix first shipped),
// matching the standing lesson that even a small per-pixel loop adds up
// at 1024 calls/frame. Same "bake the 256 possible byte values into a
// table" approach this project's own column atlas already uses.
int[256] snkBitReverseTable =
{
0,128,64,192,32,160,96,224,16,144,80,208,48,176,112,240,8,136,72,200,
40,168,104,232,24,152,88,216,56,184,120,248,4,132,68,196,36,164,100,228,
20,148,84,212,52,180,116,244,12,140,76,204,44,172,108,236,28,156,92,220,
60,188,124,252,2,130,66,194,34,162,98,226,18,146,82,210,50,178,114,242,
10,138,74,202,42,170,106,234,26,154,90,218,58,186,122,250,6,134,70,198,
38,166,102,230,22,150,86,214,54,182,118,246,14,142,78,206,46,174,110,238,
30,158,94,222,62,190,126,254,1,129,65,193,33,161,97,225,17,145,81,209,
49,177,113,241,9,137,73,201,41,169,105,233,25,153,89,217,57,185,121,249,
5,133,69,197,37,165,101,229,21,149,85,213,53,181,117,245,13,141,77,205,
45,173,109,237,29,157,93,221,61,189,125,253,3,131,67,195,35,163,99,227,
19,147,83,211,51,179,115,243,11,139,75,203,43,171,107,235,27,155,91,219,
59,187,123,251,7,135,71,199,39,167,103,231,23,151,87,215,55,183,119,247,
15,143,79,207,47,175,111,239,31,159,95,223,63,191,127,255,
};

void snkRenderImage()
{
    md_beginFrame();
    int x, page;
    for( x = 0; x < 128; x++ )
    {
        for( page = 0; page < 8; page++ )
        {
            int raw = snkComputeByte( 127 - x, 7 - page );
            raw = snkBitReverseTable[ raw & 0xFF ];
            int final;
            if( snkInvertFrame ) final = ( ~raw ) & 0xFF; else final = raw & 0xFF;
            md_drawColumn( x, page, final );
        }
    }
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

// Every state in gameSnakeGame85_update() calls snkRenderImage()
// unconditionally at the end (no dirty-flag skip anywhere in this port), so
// NULL is the correct addGame() hook, matching Four in a Row/Dino Game's
// own confirmed-correct precedent - no forceRedraw function needed.

void gameSnakeGame85_init()
{
    InitTinyJoypad();
    snkInvertFrame = 1;
    snkEnterAttract();
}

void gameSnakeGame85_update()
{
    if( snkState != SNK_STATE_GAMEOVER_BLINK ) snkInvertFrame = 1;

    if( snkState == SNK_STATE_ATTRACT )
    {
        snkAttractBlinkTimer = snkAttractBlinkTimer - 1;
        if( snkAttractBlinkTimer <= 0 )
        {
            snkAttractBlinkState = !snkAttractBlinkState;
            snkAttractBlinkTimer = SNK_ATTRACT_BLINK_FRAMES;
        }
        if( isLeftPressed() || isRightPressed() || isUpPressed() || isDownPressed() )
        {
            snkState = SNK_STATE_START_TUNE;
            snkPlayStartTune();
        }
    }
    else if( snkState == SNK_STATE_START_TUNE )
    {
        if( !snkAdvanceStartTune() )
        {
            snkResetGame();
            snkState = SNK_STATE_RESET_WAIT;
            snkStateTimer = SNK_RESET_WAIT_FRAMES;
        }
    }
    else if( snkState == SNK_STATE_RESET_WAIT )
    {
        snkStateTimer = snkStateTimer - 1;
        if( snkStateTimer <= 0 )
        {
            snkPlaceDot();
            snkState = SNK_STATE_PLAYING;
            snkTickCounter = SNK_TICK_DIVISOR;
        }
    }
    else if( snkState == SNK_STATE_PLAYING )
    {
        snkTickCounter = snkTickCounter - 1;
        if( snkTickCounter <= 0 )
        {
            snkTickCounter = SNK_TICK_DIVISOR;
            snkDoTick();
        }
    }
    else if( snkState == SNK_STATE_GAMEOVER_WAIT1 )
    {
        snkStateTimer = snkStateTimer - 1;
        if( snkStateTimer <= 0 )
        {
            snkPlayGameOverTune();
            snkState = SNK_STATE_GAMEOVER_TUNE;
        }
    }
    else if( snkState == SNK_STATE_GAMEOVER_TUNE )
    {
        if( !snkAdvanceGameOverTune() )
        {
            snkState = SNK_STATE_GAMEOVER_BLINK;
            snkBlinkHalfStep = 0;
            snkBlinkHalfTimer = SNK_GAMEOVER_BLINK_HALFSTEP_FRAMES;
            snkInvertFrame = 0; // half-step 0 = NORMAL_DISPLAY
        }
    }
    else if( snkState == SNK_STATE_GAMEOVER_BLINK )
    {
        snkBlinkHalfTimer = snkBlinkHalfTimer - 1;
        if( snkBlinkHalfTimer <= 0 )
        {
            snkBlinkHalfStep = snkBlinkHalfStep + 1;
            if( snkBlinkHalfStep >= 6 )
            {
                snkState = SNK_STATE_GAMEOVER_WAIT2;
                snkStateTimer = SNK_GAMEOVER_WAIT2_FRAMES;
                snkInvertFrame = 1;
            }
            else
            {
                if( snkBlinkHalfStep % 2 == 0 ) snkInvertFrame = 0; else snkInvertFrame = 1;
                snkBlinkHalfTimer = SNK_GAMEOVER_BLINK_HALFSTEP_FRAMES;
            }
        }
    }
    else if( snkState == SNK_STATE_GAMEOVER_WAIT2 )
    {
        snkStateTimer = snkStateTimer - 1;
        if( snkStateTimer <= 0 )
        {
            snkBuildScoreLit( snkSnakeLen - 3 );
            snkState = SNK_STATE_GAMEOVER_SCORE;
        }
    }
    else if( snkState == SNK_STATE_GAMEOVER_SCORE )
    {
        if( isLeftPressed() || isRightPressed() || isUpPressed() || isDownPressed() )
        {
            snkEnterAttract();
        }
    }

    snkRenderImage();
}

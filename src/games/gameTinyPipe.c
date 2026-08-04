// =============================================================================
// Tiny Pipe - ported from Daniel C's Tiny-Pipe.ino (tinyjoypad.com, GPLv3).
// Same tinyJoypadShim lineage as every other Daniel-C game here
// (FastTinyDriver.h) - Sound()/isXPressed() reuse the existing shim as-is.
//
// A Mappy/Pengo-style single-screen platformer: bounce on the pipework to
// knock turtles over from below (via a bump indicator or an "earthquake"
// stomp), then walk into a stunned turtle to kick it off screen for
// points; touching an un-stunned turtle costs a life. Clear all the
// turtles in a level to advance; 3 lives, 20 levels (capped).
//
// Button mapping (matches every other Daniel-C game's own established A0
// thresholds, confirmed directly against this game's own ELECTROLIB.h):
//   analogRead(A0) in (500,750) = isRightPressed()
//   analogRead(A0) in [750,950) = isLeftPressed()
//   digitalRead(1) (active low) = isFirePressed() - jump, and confirm on
//     the title screen. No up/down input exists in this game at all.
//
// Structural changes from upstream:
//  - `CLASS_TPIPE.h` declares two real C++ classes with a small, flat
//    inheritance step (`PASIVE_SPRITE_TPIPE`, and `SPRITE_TPIPE : public
//    PASIVE_SPRITE_TPIPE`) - the same shape already solved for Tiny
//    Missile's own `CLASS_TMISSILE.h`, not the harder full-hierarchy
//    complexity that keeps TinyDungeon/SQuest/DDug deferred. Flattened
//    into one `TpipeSprite` struct (every field from both classes inlined
//    together - the base class's fields are simply unused by the
//    "Target" bump-indicator instance, which only ever needed
//    `PASIVE_SPRITE_TPIPE`'s own subset) with plain `tpipe*` functions
//    taking an explicit pointer instead of methods.
//  - upstream's loop() is a `New_Game:`/`Next_Level:` goto-chain around
//    one big `while(1)`, with several genuinely blocking pieces beyond
//    the usual goto-chain shape - rewritten as an explicit frame-stepped
//    state machine (tpipeState), same approach as every other
//    tinyJoypadShim port here, just with more states than usual to cover
//    each blocking piece individually:
//     - `Intro_TPIPE()`'s own blocking blink-and-wait-for-press-then-
//       release loop -> TPIPE_STATE_INTRO_WAIT.
//     - `FADE_TPIPE()` (a real 9-step screen-wide fade transition, each
//       step a real `_delay_ms(20)`) -> a shared `tpipeAdvanceFade()`
//       helper (one step per real engine frame, not per 20ms - the
//       whole transition was already only ~180ms real time upstream, so
//       this is a close, imperceptible-difference match) reused by every
//       fade-in/fade-out call site via its own dedicated state.
//     - `NEXT_LEVEL_TPIPE()`'s own two `_delay_ms(250)` calls (bracketing
//       the level-number digit draw and the level-start music) ->
//       TPIPE_STATE_LEVEL_LOAD_WAIT1/2, plain frame countdowns.
//  - `SND_TPIPE(1)` (played when the player dies) is a genuinely
//    startling find: a *nested* loop (`for(e=0;e<100;e+=20){for(r=e;
//    r<e+100;r++){Sound(255-r,2);}}`) firing **~500** `Sound()` calls
//    synchronously in one go - the largest computed sweep found in this
//    project yet (bigger than Tiny Missile's 508-note sweeps, in the same
//    ballpark). Same root cause and fix as every other one of these
//    (Tiny Arena's death sweep, Tiny Missile's win/lose sweeps): harmless
//    on real AVR hardware (each call blocks for a a fraction of a
//    millisecond, so ~500 calls still finish in well under a second) but
//    Vircon32's async audio channel has no queue, so synchronous calls
//    with no real time between them would only ever be *heard* as the
//    last call's tone. Converted proactively, before ever compiling, to
//    a small frame-stepped, downsampled descending sweep (~15 notes,
//    step -12) - preserves the audible "warble down" character without
//    reproducing the literal overlapping-block structure or the note
//    count.
//  - `switch`/`case` avoided proactively (matching Tiny Doc/Bike's own
//    established caution, never yet tested in this dialect) -
//    `SND_TPIPE`'s dispatch, `LOAD_LEVEL_TPIPE`'s multi-case fall-through
//    bonus-life levels (2/5/8/11/14/17, rewritten as one `||`-chained
//    `if`), `INVERT_TURTLE_TPIPE`'s dispatch, and the main loop's
//    `GP.STATE` dispatch are all `if`/`else if` chains instead.
//    `SND_TPIPE`'s own case 4 is never actually called from anywhere in
//    the source - confirmed by grep before dropping it as dead code.
//  - Every intra-function `goto` used purely as a structured-control-flow
//    shortcut (a "continue" via `goto SKIPP_`, an "early exit past a
//    trailing cleanup line" via `goto EnD_`, etc.) was rewritten with
//    plain `if`/`else`/`continue` rather than porting the labels
//    verbatim - lower-risk than being the first port in this project to
//    test intra-function `goto`/label control flow (as opposed to the
//    already-proven outer-loop `goto NEWGAME:`-style state dispatch every
//    other port here already uses safely).
//  - `Trace_LINE`/`DIRECTION_LINE`/`Return_Full_Byte`/`RECONSTRUCT_BYTE`
//    (a Bresenham-style line-drawing primitive) and the `DEBOUNCE` macro
//    are declared in `ELECTROLIB.h` but never actually called anywhere
//    in the game logic - confirmed by grep before dropping them, the
//    same "provably dead, don't port verbatim" treatment as other finds
//    in this project (e.g. Tiny Bike's own dead-function cleanup).
//  - `Tiny_Flip_TPIPE()`'s own `FLIP_MODE_` parameter maps to a column
//    width of 128 (full) or 110 (partial) - but the *only* real call site
//    in the whole game (`Tiny_Flip_TPIPE(0)`, every real gameplay frame)
//    always resolves to the **110** case, meaning columns 110-127 (18 of
//    128) were *never* redrawn during actual play - the exact same "real
//    SSD1306 VRAM persistence" assumption already found and fixed in
//    Pinball/Doc/Bert/Trick, just newly discovered here rather than
//    retrofitted after a report. Fixed proactively: `tpipeTinyFlip()`
//    always redraws the full 128 columns.
//  - The above fix also exposed a genuine latent out-of-bounds risk in
//    the earthquake screen-shake effect (`x2 = (x>16) ? x+EARTQUAKE_INVERT
//    : x;`, in the original the shifted column could reach 128 for
//    x=127 - past `BACKGROUND_TPIPE[1024]`'s real bound of 127 within a
//    given row) - harmless on AVR's forgiving flat memory (reads one
//    byte into the next row/array), a real invalid-read risk on
//    Vircon32. Fixed with an explicit clamp to 127.
//  - `FADE_TPIPE()`'s own mask computation (`0xff << (8-l)` for fade-in,
//    `0xff << l` for fade-out) relies on AVR's implicit `uint8_t`
//    narrowing to keep the shifted value within a real byte - on
//    Vircon32's full-width `int` shifts, `0xff << 8` is `0xFF00`, not 0,
//    which would silently zero out every masked pixel (`wideMask &
//    byteValue` is always 0 when the mask's only set bits are above bit
//    7). Fixed with an explicit `& 0xFF` on the computed mask itself -
//    the same byte-truncation-reliant-trick class of bug as Tiny Arena's
//    VSlide fix, just caught here by inspection before ever compiling
//    rather than via a runtime crash.
//  - **Frame-pacing, added later by direct request**: upstream itself has
//    no genuine real-time throttle at all (the "no timing model" category,
//    same as Trick/Invaders/Pinball/Bert/Tris/Arkanoid), so this game
//    originally shipped running at Vircon32's native 60fps throughout.
//    Capped to 30fps, including its own logic (not just the redraw), via
//    a whole-function `TPIPE_TICK_DIVISOR=2` tick-skip gating the entire
//    top of `gameTinyPipe_update()` - the same shape already used for
//    NumberPlace/HollowSeeker/t2048/Doc/Pacman (not the movement-only/
//    redraw-stays-60fps shape used for Trick/Invaders/Pinball/Bert).
//    Every existing tick-counted wait constant in this file
//    (`tpipeWaitFrames`, `tpipeIntroBlinkT`, the fade-step counters) is
//    left unrescaled, matching this project's own standing "one divisor,
//    no dual bookkeeping" practice - they simply now take twice as long
//    in real time, which is the intended effect of halving the tick rate.
// =============================================================================

int[55] tpipeMusic1 =
{
54,102,255,0,255,90,255,0,255,80,255,0,255,72,50,62,50,72,50,62,
50,72,50,62,50,72,50,62,50,72,50,62,50,72,50,62,50,72,50,62,
50,72,50,62,50,72,50,62,50,72,50,62,50,72,50,
};

int[69] tpipeMusic2 =
{
68,96,255,0,50,90,255,0,50,50,50,100,50,50,50,100,50,50,50,0,
200,50,50,100,50,50,50,100,50,50,50,0,200,50,50,100,50,50,50,100,
50,50,50,0,200,90,255,0,200,96,50,116,50,96,50,116,50,96,50,96,
50,116,50,96,50,116,50,96,50,
};

int[42] tpipePolice =
{
4,1,124,68,124,0,0,124,0,0,116,84,92,0,68,84,124,0,28,16,
124,0,92,84,116,0,124,84,116,0,4,116,12,0,124,84,124,0,92,84,
124,0,
};

int[30] tpipeLevelText =
{
28,1,124,64,64,0,124,84,84,0,60,64,60,0,124,84,84,0,124,64,64,0,
92,84,116,0,0,0,108,108,
};

int[44] tpipeMainSprite =
{
7,1,14,159,255,127,219,14,2,28,190,126,254,54,28,4,28,62,254,126,
182,28,4,2,14,219,127,255,159,14,4,28,54,254,126,190,28,4,28,182,
126,254,62,28,
};

int[51] tpipeTurtle =
{
7,1,0,192,96,224,64,48,16,0,64,224,96,160,24,8,0,192,96,96,192,48,
16,16,48,64,224,96,192,0,8,24,160,96,224,64,0,16,48,192,96,96,
192,0,0,64,192,192,192,64,0,
};

int[30] tpipeRnd =
{
0,0,1,0,1,0,0,1,1,0,1,0,0,0,1,0,1,1,0,1,0,1,1,1,1,0,1,0,1,0,
};

int[20] tpipePower =
{
6,1,64,224,160,160,224,64,96,240,144,208,240,96,112,248,136,200,248,112,
};

int[1024] tpipeBackground =
{
255,255,255,0,255,255,1,0,254,255,255,255,1,72,121,255,0,255,128,188,
190,190,142,54,150,22,22,22,0,47,47,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,47,47,0,
22,22,22,150,54,142,190,190,188,128,255,0,255,121,72,1,255,255,255,254,
0,1,255,255,0,255,255,255,255,255,15,240,255,63,0,240,255,255,255,1,
146,146,158,255,0,255,169,173,173,173,177,156,129,128,128,128,128,128,128,128,
128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
128,128,128,128,128,128,128,0,0,0,0,0,0,0,0,0,0,128,128,128,
128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,
128,128,128,128,128,128,128,128,128,128,128,129,156,177,173,173,173,169,255,0,
255,158,146,146,1,255,255,255,240,0,63,255,240,15,255,255,255,255,252,255,
15,0,252,255,255,127,1,60,36,36,231,255,0,255,3,2,3,2,3,2,
3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,
3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,0,0,0,0,0,
0,0,0,0,0,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,
2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,2,3,
2,3,2,3,2,3,255,0,255,231,36,36,60,1,127,255,255,252,0,15,
255,252,255,255,255,255,255,255,252,126,63,31,67,72,79,201,73,73,121,255,
0,255,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,224,160,224,160,224,160,224,160,224,160,224,160,224,160,224,160,224,160,
224,160,224,160,224,160,224,160,160,224,160,224,160,224,160,224,160,224,160,224,
160,224,160,224,160,224,160,224,160,224,160,224,160,224,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,0,255,121,73,73,
201,79,72,67,31,63,126,252,255,255,255,255,231,243,243,121,248,224,130,18,
158,146,242,147,146,146,158,255,0,255,28,20,28,20,28,20,28,20,28,20,
28,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,28,20,28,20,28,20,28,20,28,
20,28,255,0,255,158,146,146,147,242,146,158,18,130,224,248,121,243,243,231,
255,255,255,252,227,31,255,254,240,4,36,60,36,36,231,255,0,255,192,64,
192,64,192,64,192,64,192,64,192,64,192,64,192,64,192,64,192,64,192,64,
192,64,192,64,192,64,192,64,192,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,192,
64,192,64,192,64,192,64,192,64,192,64,192,64,192,64,192,64,192,64,192,
64,192,64,192,64,192,64,192,64,192,255,0,255,231,36,36,60,36,4,240,
254,255,31,227,252,255,255,255,255,255,31,255,255,254,1,255,255,252,1,201,
73,73,121,255,0,255,193,193,193,193,1,225,225,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,225,225,1,193,193,193,193,255,0,
255,121,73,73,201,1,252,255,255,1,254,255,255,31,255,255,255,255,31,224,
255,255,0,255,255,255,0,243,146,146,158,255,0,255,37,165,133,229,32,171,
139,224,32,160,128,224,32,160,128,224,32,160,128,224,32,160,128,224,32,160,
128,224,32,160,128,224,32,160,128,224,32,160,128,224,32,160,128,224,32,160,
128,224,32,160,128,224,32,160,128,224,32,160,128,224,32,160,128,224,32,160,
128,224,32,160,128,224,32,160,128,224,32,160,128,224,32,160,128,224,32,171,
139,224,37,165,133,229,255,0,255,158,146,146,243,0,255,255,255,0,255,255,
224,31,255,255,
};

int[1024] tpipeTitle =
{
255,255,121,73,73,207,73,73,121,73,73,207,73,73,121,73,73,207,73,73,
73,255,7,3,121,125,125,28,108,44,44,44,44,0,94,94,0,0,0,0,
0,0,0,0,0,0,8,24,160,96,224,64,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,64,224,96,160,24,8,
0,0,0,0,0,0,0,0,0,0,0,0,94,94,0,44,44,44,44,108,
28,125,125,121,3,7,255,73,73,73,207,73,73,121,73,73,207,73,73,121,
73,73,207,73,73,121,255,255,255,255,158,146,146,243,146,146,158,146,146,243,
146,146,158,146,146,243,146,146,146,255,0,3,251,251,251,3,248,3,0,0,
0,0,0,254,255,7,51,123,123,51,3,3,3,3,3,3,3,3,3,3,
3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
3,3,3,3,3,3,3,3,3,3,3,3,51,123,123,51,5,171,254,0,
0,0,0,0,0,0,3,248,3,251,251,251,3,0,255,146,146,146,243,146,
146,158,146,146,243,146,146,158,146,146,243,146,146,158,255,255,255,255,231,36,
36,60,36,36,231,36,36,60,36,36,231,36,36,60,36,36,36,255,0,0,
255,255,255,0,255,0,0,0,0,0,0,255,255,0,0,0,0,0,0,2,
2,254,2,2,0,250,0,0,248,8,8,240,0,8,48,192,120,8,32,32,
32,0,254,18,18,12,0,0,250,0,0,248,136,136,112,0,112,168,168,176,
0,0,0,0,0,170,255,0,0,0,0,0,0,0,0,255,0,255,255,255,
0,0,255,36,36,36,60,36,36,231,36,36,60,36,36,231,36,36,60,36,
36,231,255,255,255,255,121,73,73,207,73,73,121,73,73,207,73,73,121,73,
73,207,73,73,73,255,0,0,255,255,255,0,255,0,0,0,0,0,0,255,
255,0,96,240,240,96,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,
0,0,0,0,0,0,0,0,96,240,240,96,0,170,255,0,0,0,0,0,
0,0,0,255,0,255,255,255,0,0,255,73,73,73,207,73,73,121,73,73,
207,73,73,121,73,73,207,73,73,121,255,255,255,255,126,50,50,51,50,50,
62,50,50,51,50,50,62,50,50,51,50,50,26,15,224,240,247,119,183,176,
183,176,176,0,120,120,0,3,6,5,6,4,6,4,6,4,6,4,6,4,
6,4,6,4,6,4,6,4,6,4,6,4,6,4,6,4,6,4,6,4,
6,4,6,4,6,4,6,4,6,4,6,4,6,4,6,4,6,4,6,4,
7,6,3,0,0,0,120,120,0,176,176,183,176,183,119,247,240,224,15,26,
50,50,51,50,50,62,50,50,51,50,50,62,50,50,51,50,50,126,255,255,
255,255,112,96,96,98,102,120,108,108,120,96,96,96,96,96,96,96,96,96,
96,108,109,109,109,140,225,12,0,0,0,0,1,1,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,
12,225,140,109,109,109,108,96,96,96,96,96,96,96,96,120,108,108,120,102,
98,96,96,96,96,112,255,255,255,255,3,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,128,128,0,192,
64,0,0,24,192,24,216,216,216,24,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,24,192,24,216,216,
216,24,0,0,64,192,0,128,128,0,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,3,255,255,39,39,62,36,
36,228,36,36,60,36,36,228,36,36,60,36,36,228,36,36,60,36,36,228,
36,36,60,39,37,231,37,36,60,36,36,228,39,36,63,39,39,228,36,36,
60,36,36,228,36,36,60,36,36,228,36,36,60,36,36,228,36,36,60,36,
36,228,36,36,60,36,36,228,36,36,60,36,36,228,36,36,60,36,36,228,
36,36,60,36,39,228,39,39,63,36,36,228,36,36,61,39,37,231,36,36,
60,36,36,228,36,36,60,36,36,228,36,36,60,36,36,228,36,36,60,36,
36,230,39,39,
};

int[52] tpipeStart =
{
50,1,76,146,146,98,0,8,254,8,0,104,168,168,240,0,240,8,8,0,8,254,
136,128,0,0,0,0,56,68,130,178,146,116,0,104,168,168,240,0,248,8,
16,48,8,248,0,112,248,168,184,176,
};

// -----------------------------------------------------------------------------
//   State
// -----------------------------------------------------------------------------

#define TPIPE_NUM_SPRITES 4

struct TpipeSprite
{
    int x;
    int y;
    int frame;
    int killed;
    int active;
    int width;
    int height;
    int xSpeed;
    int xAdd;
    int ySpeed;
    int yAdd;
    int animDirection;
    int cancelJump;
    int noMoveTimer;
};

TpipeSprite tpipeMain;
TpipeSprite[4] tpipeSprite;
TpipeSprite tpipeTarget;

struct TpipeGamePlay
{
    int firstTime;
    int state;
    int level;
    int lives;
    int timerRenew;
    int levelXSpeed;
    int totalTurtleLevel;
    int speedPopTurtle;
    int power;
    int earthquake;
    int earthquakeInvert;
    int noMoveTime;
    int digit1;
    int digit2;
};

TpipeGamePlay tpipeGP;

int tpipeSequencialCheck;
int tpipeBlink;
int tpipeDChange;
int tpipeDChangeB;
int tpipeRndCounter;

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

int tpipeSweepActive;
int tpipeSweepT;
int tpipeBurstActive;

// SND_TPIPE(0) - a short 5-note ascending confirm chime, harmless as a
// synchronous burst (only 5 calls).
void tpipeSound0()
{
    Sound( 10, 40 ); Sound( 60, 40 ); Sound( 110, 40 ); Sound( 170, 40 ); Sound( 220, 40 );
}

// SND_TPIPE(1) - see this file's own header comment: upstream fires
// ~500 Sound() calls synchronously here. Converted to a frame-stepped,
// downsampled descending sweep instead.
void tpipeStartDeathSweep()
{
    tpipeSweepActive = 1;
    tpipeSweepT = 255;
}

void tpipeAdvanceSweep()
{
    if( !tpipeSweepActive ) return;
    Sound( tpipeSweepT, 2 );
    tpipeSweepT -= 12;
    if( tpipeSweepT <= 76 )
      tpipeSweepActive = 0;
}

int* tpipeMusicTable;
int tpipeMusicIndex;
int tpipeMusicWaitFrames;

void tpipeStartMusic( int* music )
{
    tpipeMusicTable = music;
    tpipeMusicIndex = 1;
    tpipeMusicWaitFrames = 0;
    tpipeBurstActive = 1;
}

// Returns true once the whole tune has finished playing.
int tpipeAdvanceMusic()
{
    if( !tpipeBurstActive )
      return 1;

    if( tpipeMusicWaitFrames > 0 )
    {
        tpipeMusicWaitFrames--;
        return 0;
    }

    if( tpipeMusicIndex >= tpipeMusicTable[0] )
    {
        tpipeBurstActive = 0;
        return 1;
    }

    int freq = tpipeMusicTable[ tpipeMusicIndex ];
    int dur = tpipeMusicTable[ tpipeMusicIndex + 1 ];
    Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;

    tpipeMusicWaitFrames = waitFrames;
    tpipeMusicIndex += 2;
    return 0;
}

int tpipeSoundBusy()
{
    return tpipeSweepActive || tpipeBurstActive;
}

// -----------------------------------------------------------------------------
//   Helpers
// -----------------------------------------------------------------------------

int tpipeMymap( int x, int inMin, int inMax, int outMin, int outMax )
{
    return ( x - inMin ) * ( outMax - outMin ) / ( inMax - inMin ) + outMin;
}

int tpipeCollision1v1( int x1, int x2, int y1, int y2, int sx1, int sx2, int sy1, int sy2 )
{
    if( x1 > sx2 ) return 0;
    if( x2 < sx1 ) return 0;
    if( y1 > sy2 ) return 0;
    if( y2 < sy1 ) return 0;
    return 1;
}

int tpipeFloorsVsSprite( int killed, int x1, int x2, int y1, int y2 )
{
    if( killed ) return 0;
    if( tpipeCollision1v1( x1, x2, y1, y2, 0, 58, 15, 17 ) ) return 1;
    if( tpipeCollision1v1( x1, x2, y1, y2, 69, 127, 15, 17 ) ) return 1;
    if( tpipeCollision1v1( x1, x2, y1, y2, 38, 89, 29, 31 ) ) return 1;
    if( tpipeCollision1v1( x1, x2, y1, y2, 0, 28, 34, 36 ) ) return 1;
    if( tpipeCollision1v1( x1, x2, y1, y2, 99, 127, 34, 36 ) ) return 1;
    if( tpipeCollision1v1( x1, x2, y1, y2, 0, 48, 46, 48 ) ) return 1;
    if( tpipeCollision1v1( x1, x2, y1, y2, 79, 127, 46, 48 ) ) return 1;
    if( tpipeCollision1v1( x1, x2, y1, y2, 0, 127, 61, 61 ) ) return 1;
    return 0;
}

int tpipeCollisionSimplified( TpipeSprite* mainS, TpipeSprite* other )
{
    return tpipeCollision1v1( mainS->x, mainS->x + mainS->width, mainS->y, mainS->y + mainS->height,
                               other->x + 1, other->x + other->width - 2, other->y + 5, other->y + other->height );
}

void tpipeRndMixer()
{
    if( tpipeRndCounter < 29 ) tpipeRndCounter++;
    else tpipeRndCounter = 0;
}

int tpipePseudoRnd()
{
    tpipeRndMixer();
    return tpipeRnd[ tpipeRndCounter ];
}

void tpipeSpriteInit( TpipeSprite* s, int active, int x, int y )
{
    s->x = x;
    s->y = y;
    s->frame = 0;
    s->active = active;
    s->killed = 0;
    s->width = 7;
    s->height = 7;
}

void tpipeSpriteAnim( TpipeSprite* s )
{
    if( s->frame < 2 ) s->frame++;
    else s->frame = 0;
}

// -----------------------------------------------------------------------------
//   Sprite blitters (ELECTROLIB.h's own blitzSprite/SPEED_BLITZ - not part
//   of the shared shim, ported here as game-local functions like every
//   other Daniel-C game's own equivalent, e.g. gilbSpeedBlitz).
// -----------------------------------------------------------------------------

// Was `RecupeLineY(int8_t Valeur){ return (Valeur>>3); }` upstream - a
// genuine bug found by inspection before ever compiling: `yPos` can be
// negative (turtles spawn at y=-3), and AVR-GCC's `int8_t >>` sign-
// extends (arithmetic shift), giving the correct floor-division result,
// but Vircon32's `>>` is documented as a *logical* (zero-fill) shift -
// the same class of bug already found and fixed in HollowSeeker
// (`hsDivByColumnW`). Fixed the same way: branch on sign and only ever
// shift a non-negative operand.
int tpipeRecupeLineY( int val )
{
    if( val >= 0 ) return val >> 3;
    return -( ( -val + 7 ) >> 3 );
}

// Was `RecupeDecalageY(uint8_t Valeur){ return (Valeur-((Valeur>>3)<<3)); }`
// - also fed negative `yPos` values upstream, relying on the same
// arithmetic-shift behavior. Derived directly from the already-fixed
// tpipeRecupeLineY() instead of its own separate shift trick - this is
// just the true mathematical remainder (Valeur - 8*floor(Valeur/8)),
// which is correct for any sign once the floor-division part is safe.
int tpipeRecupeDecalageY( int val )
{
    return val - ( tpipeRecupeLineY( val ) * 8 );
}

int tpipeSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown ) return input << decalage;
    return input >> ( 8 - decalage );
}

// Sub-page-Y-positioned sprite blitter (was blitzSprite). Every
// intermediate OUTBYTE/OUTBYTE2 value here is only ever combined with
// bitwise OR (or returned as a bare 0x00) all the way up through
// tpipeMainBlitz()/tpipeSpritesTurtle()/tpipeRecupe() to the final
// md_drawColumn() call, which already masks to a real byte centrally
// (the same "fix it once, centrally" precedent as md_drawColumn's own
// established byte-truncation fix) - safe to defer masking there rather
// than at every intermediate shift site, since OR-combination and
// masking commute.
int tpipeBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = tpipeRecupeLineY( yPos );

    if( xPass > ( xPos + wSprite - 1 ) || xPass < xPos ||
        recupeLineY > yPass || ( recupeLineY + hSprite ) < yPass )
      return 0x00;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = tpipeRecupeDecalageY( yPos );
    int scanA = ( xPass - xPos ) + ( spriteYLine * wSprite ) + 2;
    int scanB = ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;

    int outByte;
    if( scanA > wMax ) outByte = 0x00;
    else outByte = tpipeSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = tpipeSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

// Simple page-aligned blitter (was SPEED_BLITZ) - matches every other
// Daniel-C game's own equivalent (e.g. gilbSpeedBlitz).
int tpipeSpeedBlitz( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    if( xPass > ( xPos + wSprite - 1 ) || xPass < xPos ||
        yPass < yPos || yPass > ( yPos + hSprite - 1 ) )
      return 0x00;
    return sprites[ 2 + ( xPass - xPos ) + ( yPass - yPos ) * wSprite + frame * ( hSprite * wSprite ) ];
}

// -----------------------------------------------------------------------------
//   Player/turtle physics (was SPRITE_TPIPE's own methods)
// -----------------------------------------------------------------------------

void tpipeRunR( TpipeSprite* s );
void tpipeRunL( TpipeSprite* s );

void tpipeDecel( TpipeSprite* s )
{
    if( s->xSpeed < 0 ) { s->xSpeed++; tpipeRunL( s ); }
    else if( s->xSpeed > 0 ) { s->xSpeed--; tpipeRunR( s ); }
}

void tpipeResetXSpeed( TpipeSprite* s )
{
    s->xSpeed = 0;
    s->xAdd = 0;
}

void tpipeRunR( TpipeSprite* s )
{
    int absSpeed = s->xSpeed;
    if( absSpeed < 0 ) absSpeed = -absSpeed;
    s->xAdd = absSpeed + s->xAdd;
    if( s->xAdd > 10 )
    {
        s->xAdd -= 10;
        if( s->x < 114 )
        {
            s->x = s->x + 1;
            tpipeSpriteAnim( s );
            if( tpipeFloorsVsSprite( s->killed, s->x + 1, s->x + s->width - 1, s->y, s->y + s->height ) )
              s->x = s->x - 1;
        }
        else s->x = 8;
    }
}

void tpipeRunL( TpipeSprite* s )
{
    int absSpeed = s->xSpeed;
    if( absSpeed < 0 ) absSpeed = -absSpeed;
    s->xAdd = absSpeed + s->xAdd;
    if( s->xAdd > 10 )
    {
        s->xAdd -= 10;
        if( s->x > 8 )
        {
            s->x = s->x - 1;
            tpipeSpriteAnim( s );
            if( tpipeFloorsVsSprite( s->killed, s->x + 1, s->x + s->width - 1, s->y, s->y + s->height ) )
              s->x = s->x + 1;
        }
        else s->x = 114;
    }
}

void tpipeResetGravity( TpipeSprite* s )
{
    s->yAdd = 0;
    s->ySpeed = 0;
}

void tpipeGravityRefresh( TpipeSprite* s )
{
    if( s->ySpeed < 30 ) s->ySpeed = s->ySpeed + 2;
    else s->ySpeed = 30;
    int absSpeed = s->ySpeed;
    if( absSpeed < 0 ) absSpeed = -absSpeed;
    s->yAdd = absSpeed + s->yAdd;
}

void tpipeGravity( TpipeSprite* s, int isMain )
{
    if( !s->active ) return;
    tpipeGravityRefresh( s );

    if( s->ySpeed >= 0 )
    {
        while( s->yAdd > 10 ) { s->y = s->y + 1; s->yAdd -= 10; }
        while( 1 )
        {
            if( tpipeFloorsVsSprite( s->killed, s->x + 1, s->x + s->width - 1, s->y, s->y + s->height ) )
            {
                s->y = s->y - 1;
                tpipeResetGravity( s );
                if( s->cancelJump == 1 ) s->cancelJump = 2;
            }
            else
            {
                if( s->y > 63 ) s->active = 0;
                break;
            }
        }
    }
    else
    {
        while( s->yAdd > 10 )
        {
            if( s->y > -10 ) { s->y = s->y - 1; s->yAdd -= 10; }
            else tpipeResetGravity( s );
        }
        if( tpipeFloorsVsSprite( s->killed, s->x + 1, s->x + s->width - 1, s->y, s->y + s->height ) )
        {
            while( 1 )
            {
                if( tpipeFloorsVsSprite( s->killed, s->x + 1, s->x + s->width - 1, s->y, s->y + s->height ) )
                {
                    s->y = s->y + 1;
                    tpipeResetGravity( s );
                }
                else
                {
                    if( tpipeTarget.active == 0 && isMain == 1 )
                      tpipeSpriteInit( &tpipeTarget, 1, s->x, s->y - 8 );
                    break;
                }
            }
        }
    }
}

void tpipeJump( TpipeSprite* s )
{
    if( s->cancelJump == 0 )
    {
        s->cancelJump = 1;
        if( tpipeFloorsVsSprite( s->killed, s->x + 1, s->x + s->width - 1, s->y + 1, s->y + s->height + 1 ) )
        {
            s->ySpeed = -25;
            s->yAdd = 10;
        }
    }
}

void tpipeRightMove( TpipeSprite* s )
{
    s->animDirection = 0;
    if( s->xSpeed < 0 ) { tpipeDecel( s ); tpipeRunL( s ); }
    else
    {
        if( s->xSpeed < 10 ) s->xSpeed = s->xSpeed + 1;
        tpipeRunR( s );
    }
}

void tpipeLeftMove( TpipeSprite* s )
{
    s->animDirection = 3;
    if( s->xSpeed > 0 ) { tpipeDecel( s ); tpipeRunR( s ); }
    else
    {
        if( s->xSpeed > -10 ) s->xSpeed = s->xSpeed - 1;
        tpipeRunL( s );
    }
}

void tpipeDirectRMove( TpipeSprite* s )
{
    if( s->x > 109 && s->y > 52 )
    {
        s->x = 24; s->y = -3;
        tpipeResetGravity( s );
    }
    else
    {
        s->animDirection = 0;
        tpipeRunR( s );
    }
}

void tpipeDirectLMove( TpipeSprite* s )
{
    if( s->x < 12 && s->y > 52 )
    {
        s->x = 97; s->y = -3;
        tpipeResetGravity( s );
    }
    else
    {
        s->animDirection = 3;
        tpipeRunL( s );
    }
}

void tpipeAutoMove( TpipeSprite* s )
{
    if( s->xSpeed > 0 ) tpipeDirectRMove( s );
    else tpipeDirectLMove( s );
}

// -----------------------------------------------------------------------------
//   Game logic
// -----------------------------------------------------------------------------

void tpipeAdjGP()
{
    if( tpipeGP.lives > 0 )
    {
        tpipeMain.animDirection = 0;
        tpipeMain.width = 7;
        tpipeMain.height = 7;
        tpipeResetGravity( &tpipeMain );
        tpipeMain.xSpeed = 0;
        tpipeSpriteInit( &tpipeTarget, 0, 0, 0 );
        if( tpipeGP.firstTime == 1 )
        {
            tpipeSpriteInit( &tpipeMain, 1, 42, 53 );
            tpipeGP.firstTime = 0;
        }
        else
        {
            tpipeSpriteInit( &tpipeMain, 2, 53, 7 );
        }
    }
    else tpipeGP.state = 2;
}

void tpipeShieldRemove()
{
    if( tpipeMain.active == 2 ) tpipeMain.active = 1;
    tpipeRndMixer();
}

void tpipeCheckLevelCompleted()
{
    int t;
    int anyActive = 0;
    for( t = 0; t < TPIPE_NUM_SPRITES; t++ )
      if( tpipeSprite[t].active != 0 ) anyActive = 1;
    if( !anyActive ) tpipeGP.state = 1;
}

void tpipeNewTurtle()
{
    int t;
    int slot = -1;
    for( t = 0; t < TPIPE_NUM_SPRITES; t++ )
      if( tpipeSprite[t].active == 0 ) { slot = t; break; }
    if( slot == -1 ) return;

    tpipeSprite[ slot ].active = 1;
    tpipeSprite[ slot ].killed = 0;
    int r = tpipePseudoRnd();
    if( r == 0 )
    {
        tpipeSprite[ slot ].x = 24; tpipeSprite[ slot ].y = -3;
        tpipeSprite[ slot ].xSpeed = tpipeGP.levelXSpeed;
        tpipeGP.totalTurtleLevel--;
    }
    else if( r == 1 )
    {
        tpipeSprite[ slot ].x = 97; tpipeSprite[ slot ].y = -3;
        tpipeSprite[ slot ].xSpeed = -tpipeGP.levelXSpeed;
        tpipeGP.totalTurtleLevel--;
    }
}

void tpipeCheckForNewTurtle()
{
    if( tpipeGP.totalTurtleLevel > 0 ) tpipeNewTurtle();
    else tpipeCheckLevelCompleted();
}

void tpipeTimerForNewTurtle()
{
    if( tpipeGP.timerRenew < tpipeGP.speedPopTurtle ) tpipeGP.timerRenew++;
    else
    {
        tpipeGP.timerRenew = 0;
        tpipeCheckForNewTurtle();
    }
}

void tpipeKillSprite()
{
    if( !tpipeSprite[ tpipeSequencialCheck ].killed )
    {
        Sound( 100, 2 ); Sound( 240, 2 );
        int a = tpipeSprite[ tpipeSequencialCheck ].active;
        if( a == 1 )
        {
            tpipeStartDeathSweep();
            tpipeGP.lives--;
            tpipeMain.killed = 1;
            tpipeMain.ySpeed = -18;
        }
        else if( a == 2 )
        {
            tpipeSprite[ tpipeSequencialCheck ].killed = 1;
            tpipeSprite[ tpipeSequencialCheck ].ySpeed = -18;
        }
    }
}

void tpipeCheckCycleCollision()
{
    if( tpipeMain.active != 1 ) return;
    if( !tpipeMain.killed )
    {
        if( tpipeSequencialCheck < TPIPE_NUM_SPRITES - 1 ) tpipeSequencialCheck++;
        else tpipeSequencialCheck = 0;
        if( tpipeSprite[ tpipeSequencialCheck ].active )
        {
            if( tpipeCollisionSimplified( &tpipeMain, &tpipeSprite[ tpipeSequencialCheck ] ) )
              tpipeKillSprite();
        }
    }
}

void tpipeInvertTurtle( int no, int val )
{
    if( val == 1 )
    {
        tpipeSprite[ no ].noMoveTimer = tpipeGP.noMoveTime;
        tpipeSprite[ no ].active = 2;
        tpipeSprite[ no ].animDirection = 6;
        tpipeSprite[ no ].frame = 0;
        tpipeSprite[ no ].ySpeed = -14;
    }
    else if( val == 2 )
    {
        tpipeSprite[ no ].noMoveTimer = 0;
        tpipeSprite[ no ].active = 1;
        tpipeSprite[ no ].ySpeed = -14;
    }
}

void tpipeAllInvertTurtle()
{
    int t;
    for( t = 0; t < TPIPE_NUM_SPRITES; t++ )
      if( tpipeSprite[t].ySpeed < 11 )
        if( tpipeSprite[t].y > 0 )
          tpipeInvertTurtle( t, tpipeSprite[t].active );
}

int tpipeHitBumpCheck()
{
    int offsetX = 3;
    if( tpipeTarget.active == 1 )
    {
        int t;
        int hit = 0;
        for( t = 0; t < TPIPE_NUM_SPRITES && !hit; t++ )
        {
            if( tpipeSprite[t].active )
            {
                if( tpipeCollision1v1( tpipeTarget.x + offsetX, tpipeTarget.x + offsetX,
                                       tpipeTarget.y + offsetX, tpipeTarget.y + tpipeTarget.height,
                                       tpipeSprite[t].x + 1, tpipeSprite[t].x + tpipeSprite[t].width - 2,
                                       tpipeSprite[t].y, tpipeSprite[t].y + tpipeSprite[t].height ) )
                {
                    tpipeInvertTurtle( t, tpipeSprite[t].active );
                    Sound( 240, 10 );
                    hit = 1;
                }
            }
        }
        tpipeTarget.active = 2;
        return 0;
    }
    else
    {
        if( tpipeGP.power < 0 ) return 0;
        if( tpipeMain.killed == 0 )
        {
            if( tpipeMain.ySpeed < 0 )
            {
                if( tpipeCollision1v1( 63, 63, 46, 46, tpipeMain.x + 1, tpipeMain.x + tpipeMain.width - 2,
                                       tpipeMain.y, tpipeMain.y + 1 ) )
                {
                    tpipeMain.y = tpipeMain.y + 1;
                    tpipeResetGravity( &tpipeMain );
                    if( tpipeGP.power != -1 ) tpipeGP.power--;
                    tpipeGP.earthquake = 8;
                    tpipeAllInvertTurtle();
                }
            }
        }
        return 0;
    }
}

void tpipeNoMoveTimer( int i )
{
    if( tpipeSprite[i].noMoveTimer == 1 )
    {
        tpipeSprite[i].noMoveTimer = 0;
        tpipeSprite[i].active = 1;
    }
    else if( tpipeSprite[i].noMoveTimer > 1 )
      tpipeSprite[i].noMoveTimer = tpipeSprite[i].noMoveTimer - 1;
}

void tpipeChangeDirection()
{
    if( tpipeDChange < 160 ) tpipeDChange++;
    else tpipeDChange = 0;
    if( tpipeDChange == 0 )
    {
        tpipeSprite[ tpipeDChangeB ].xSpeed = -tpipeSprite[ tpipeDChangeB ].xSpeed;
        if( tpipeDChangeB < TPIPE_NUM_SPRITES - 1 ) tpipeDChangeB++;
        else tpipeDChangeB = 0;
    }
}

void tpipeRefreshTurtle()
{
    tpipeChangeDirection();
    int t;
    for( t = 0; t < TPIPE_NUM_SPRITES; t++ )
    {
        if( tpipeSprite[t].active == 0 ) continue;
        if( tpipeSprite[t].active == 1 )
        {
            if( !tpipeSprite[t].killed ) tpipeAutoMove( &tpipeSprite[t] );
        }
        else tpipeNoMoveTimer( t );

        if( tpipeSprite[t].y == -3 )
        {
            if( tpipeSprite[t].x < 30 ) continue;
            if( tpipeSprite[t].x > 91 ) continue;
        }
        tpipeGravity( &tpipeSprite[t], 0 );
    }
}

void tpipeUpdateDigital()
{
    if( tpipeGP.digit1 < 9 ) tpipeGP.digit1++;
    else
    {
        tpipeGP.digit1 = 0;
        if( tpipeGP.digit2 < 9 ) tpipeGP.digit2++;
    }
}

void tpipeLoadLevel( int level )
{
    if( level == 2 || level == 5 || level == 8 || level == 11 || level == 14 || level == 17 )
    {
        tpipeGP.lives++;
        Sound( 200, 255 ); Sound( 0, 255 ); Sound( 200, 255 ); Sound( 0, 255 ); Sound( 200, 255 ); Sound( 0, 255 );
    }
    if( level > 20 ) level = 20;
    tpipeGP.levelXSpeed = tpipeMymap( level, 0, 20, 3, 10 );
    tpipeGP.totalTurtleLevel = tpipeMymap( level, 0, 20, 8, 40 );
    tpipeGP.speedPopTurtle = tpipeMymap( level, 0, 20, 80, 40 );
    tpipeGP.noMoveTime = tpipeMymap( level, 0, 20, 255, 60 );
    tpipeAdjGP();
}

void tpipeGPInit()
{
    tpipeGP.timerRenew = 0;
    tpipeGP.power = 2;
    tpipeGP.earthquake = 0;
    tpipeGP.earthquakeInvert = 0;
}

void tpipeInitTurtle()
{
    int t;
    for( t = 0; t < TPIPE_NUM_SPRITES; t++ )
      tpipeSpriteInit( &tpipeSprite[t], 0, 0, 0 );
}

// -----------------------------------------------------------------------------
//   Display
// -----------------------------------------------------------------------------

int tpipeAnimIndex( TpipeSprite* s )
{
    return s->frame + s->animDirection;
}

// Requested optimization pass, confirmed necessary by measurement (perf
// overlay showed a steady 100% CPU during normal play, and a soak-test
// screenshot caught a genuinely truncated frame - only the top page rows
// drawn before the frame ran out of budget, exactly this project's own
// documented over-budget failure mode). Root cause: the player and all 4
// turtles were each composited via a full per-pixel call
// (`tpipeMainBlitz`/`tpipeSpritesTurtle`, called once per one of 1024
// pixels/frame, with the turtle version additionally looping over all 4
// sprites internally every single call) - the same O(pixels x objects)
// shape already found and fixed in Bomber/Pacman/Bert/Doc. Fixed the same
// way: composite each sprite once per *page row* (8 times/frame) into a
// shared buffer, over only its own real column footprint, rather than
// scanning all 128 columns per sprite.
//
// The row-membership check below (`recupeLineY <= y <= recupeLineY+1`)
// is not an approximation of the original per-pixel gate - it's the
// exact same range `tpipeBlitzSprite()` already checks internally (its
// own HSPRITE field is 1 for both sprites here, so it can only ever
// produce a nonzero result within that 2-row span regardless of what
// gate calls it) - so this cuts wasted calls without changing output,
// the same "self-gating still costs a full call every time it's
// invoked" lesson already applied elsewhere in this project.
int[128] tpipeSpritePageBuffer;

void tpipeCompositeSpritesRow( int y )
{
    int x;
    for( x = 0; x < 128; x++ ) tpipeSpritePageBuffer[x] = 0;

    if( !( tpipeMain.active == 2 && tpipeBlink != 0 ) )
    {
        int lineY = tpipeRecupeLineY( tpipeMain.y );
        if( y >= lineY && y <= lineY + 1 )
        {
            int xs = tpipeMain.x;
            if( xs < 18 ) xs = 18;
            int xe = tpipeMain.x + 7 - 1;
            if( xe > 109 ) xe = 109;
            for( x = xs; x <= xe; x++ )
              tpipeSpritePageBuffer[x] = tpipeSpritePageBuffer[x] | tpipeBlitzSprite( tpipeMain.x, tpipeMain.y, x, y, tpipeAnimIndex( &tpipeMain ), tpipeMainSprite );
        }
    }

    int t;
    for( t = 0; t < TPIPE_NUM_SPRITES; t++ )
    {
        if( !tpipeSprite[t].active ) continue;
        int lineY = tpipeRecupeLineY( tpipeSprite[t].y );
        if( y < lineY || y > lineY + 1 ) continue;
        int xs = tpipeSprite[t].x;
        if( xs < 18 ) xs = 18;
        int xe = tpipeSprite[t].x + tpipeSprite[t].width - 1;
        if( xe > 109 ) xe = 109;
        for( x = xs; x <= xe; x++ )
          tpipeSpritePageBuffer[x] = tpipeSpritePageBuffer[x] | tpipeBlitzSprite( tpipeSprite[t].x, tpipeSprite[t].y, x, y, tpipeAnimIndex( &tpipeSprite[t] ), tpipeTurtle );
    }
}

int tpipePowerBlitz( int xPass, int yPass )
{
    if( tpipeGP.power < 0 ) return 0x00;
    if( yPass == 5 ) return tpipeSpeedBlitz( 61, 5, xPass, yPass, tpipeGP.power, tpipePower );
    return 0x00;
}

int tpipeBack( int xPass, int yPass )
{
    return tpipeBackground[ xPass + ( yPass * 128 ) ];
}

int tpipeRecupe( int xPass, int yPass )
{
    return tpipeSpritePageBuffer[ xPass ] | tpipeBack( xPass, yPass ) | tpipePowerBlitz( xPass, yPass );
}

void tpipeTinyFlip()
{
    if( tpipeGP.earthquake != 0 )
    {
        Sound( 10, 1 );
        tpipeGP.earthquake--;
        if( tpipeGP.earthquakeInvert == 0 ) tpipeGP.earthquakeInvert = 1;
        else tpipeGP.earthquakeInvert = 0;
    }
    else tpipeGP.earthquakeInvert = 0;

    md_beginFrame();
    int y, x, x2;
    for( y = 0; y < 8; y++ )
    {
        tpipeCompositeSpritesRow( y );
        for( x = 0; x < 128; x++ )
        {
            if( x > 16 ) x2 = x + tpipeGP.earthquakeInvert;
            else x2 = x;
            if( x2 > 127 ) x2 = 127;
            md_drawColumn( x, y, tpipeRecupe( x2, y ) );
        }
    }
}

void tpipeIntroDraw( int showStart )
{
    md_beginFrame();
    int y, x;
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
        {
            int startBit = 0;
            if( showStart ) startBit = tpipeSpeedBlitz( 38, 5, x, y, 0, tpipeStart );
            md_drawColumn( x, y, startBit | tpipeTitle[ x + ( y * 128 ) ] );
        }
    }
}

// Draw-only half of upstream's DRAW_LEVEL_TPIPE - kept separate from the
// digit-counter advance below so a quit-dialog forced redraw (which must
// redraw this same screen again, unchanged) can't accidentally advance
// the level-number digits a second time.
// Upstream's own DRAW_LEVEL_TPIPE draws the *current* digits, then
// immediately advances them (preparing next level's display) - a real,
// one-shot "prepare next value" pattern, not a bug. But `tpipeGP.digit1/2`
// are therefore already the *next* level's values by the time a later
// forced redraw (see gameTinyPipe_forceRedraw()) could fire during
// TPIPE_STATE_LEVEL_LOAD_MUSIC/WAIT2 - reading them live there redraws
// the wrong ("02" instead of the still-on-screen "01") digits. Fixed by
// freezing what was actually just drawn into its own pair of globals,
// used by every redraw (real or forced) instead of the live, already-
// advanced GP fields.
int tpipeShownDigit1;
int tpipeShownDigit2;

void tpipeDrawLevelDisplay()
{
    md_beginFrame();
    int y, x;
    for( y = 0; y < 8; y++ )
      for( x = 0; x < 128; x++ )
        md_drawColumn( x, y, tpipeSpeedBlitz( 44, 3, x, y, 0, tpipeLevelText ) |
                              tpipeSpeedBlitz( 75, 3, x, y, tpipeShownDigit2, tpipePolice ) |
                              tpipeSpeedBlitz( 80, 3, x, y, tpipeShownDigit1, tpipePolice ) );
}

void tpipeDrawLevel()
{
    tpipeShownDigit1 = tpipeGP.digit1;
    tpipeShownDigit2 = tpipeGP.digit2;
    tpipeDrawLevelDisplay();
    tpipeUpdateDigital();
}

// -----------------------------------------------------------------------------
//   Fade transition (was FADE_TPIPE's own 9-step blocking loop)
// -----------------------------------------------------------------------------

int tpipeFadeStep;
int tpipeFadeDir;
int* tpipeFadePic;

void tpipeStartFade( int* pic, int dir )
{
    tpipeFadePic = pic;
    tpipeFadeDir = dir;
    tpipeFadeStep = 0;
}

// Returns true once the fade has fully completed (the final frame was
// just drawn this call).
int tpipeAdvanceFade()
{
    int shiftAmount;
    if( tpipeFadeDir == 0 ) shiftAmount = 8 - tpipeFadeStep;
    else shiftAmount = tpipeFadeStep;

    // See this file's own header comment: AVR's uint8_t narrowing kept
    // `0xff << 8` at 0, but Vircon32's full-width int shift needs an
    // explicit mask to match.
    int mask = ( 0xff << shiftAmount ) & 0xFF;

    md_beginFrame();
    int y, x;
    for( y = 0; y < 8; y++ )
      for( x = 0; x < 128; x++ )
        md_drawColumn( x, y, mask & tpipeFadePic[ x + ( y * 128 ) ] );

    tpipeFadeStep++;
    if( tpipeFadeStep > 8 ) return 1;
    return 0;
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define TPIPE_STATE_INTRO_FADEIN           0
#define TPIPE_STATE_INTRO_WAIT             1
#define TPIPE_STATE_INTRO_WAIT_RELEASE     2
#define TPIPE_STATE_INTRO_FADEOUT          3
#define TPIPE_STATE_LEVEL_LOAD_WAIT1       4
#define TPIPE_STATE_LEVEL_LOAD_MUSIC       5
#define TPIPE_STATE_LEVEL_LOAD_WAIT2       6
#define TPIPE_STATE_LEVEL_FADEIN           7
#define TPIPE_STATE_PLAYING               8
#define TPIPE_STATE_LEVEL_COMPLETE_FADEOUT 9
#define TPIPE_STATE_GAMEOVER_FADEOUT       10

// Requested directly: cap Tiny Pipe at 30fps, including its own logic (not
// just the redraw) - upstream itself has no genuine real-time throttle of
// its own (the "no timing model at all" category, same as Trick/Invaders/
// Pinball/Bert/Tris/Arkanoid in this project's own frame-pacing survey),
// so this is a deliberate slowdown rather than restoring an original rate.
// Whole-function tick-skip (matching NumberPlace/HollowSeeker/t2048/Doc/
// Pacman's own shape, not the movement-only/redraw-stays-60fps shape used
// for Trick/Invaders/Pinball/Bert) - every tick-counted wait constant in
// this file (tpipeWaitFrames, tpipeIntroBlinkT, etc.) is left unrescaled,
// matching this project's own standing "one divisor, no dual bookkeeping"
// practice - they'll simply now take twice as long in real time, which is
// the intended effect of halving the whole tick rate.
#define TPIPE_TICK_DIVISOR 2
int tpipeTickSkipCounter;

int tpipeState;
int tpipeWaitFrames;
int tpipeIntroBlinkT;
int tpipeForceRedraw;

// Quit-confirmation-dialog resume hook (see menuGameList.c's own comment
// on this pattern, and CLAUDE.md's own writeup of the same fix applied
// to Tiny Bike/Arena/Gilbert). TPIPE_STATE_INTRO_WAIT_RELEASE has no
// timer of its own (waits for Fire to be released) - real, indefinite-
// persistence risk. The LEVEL_LOAD_WAIT1/MUSIC/WAIT2 states are bounded
// but can still last a couple of real seconds (the level-start music) -
// fixed for consistency too, checked proactively this time rather than
// waiting for another report.
void gameTinyPipe_forceRedraw()
{
    tpipeForceRedraw = 1;
}

void tpipeBeginNewGame()
{
    tpipeGP.lives = 3;
    tpipeGP.level = 0;
    tpipeGP.digit1 = 1;
    tpipeGP.digit2 = 0;
    tpipeInitTurtle();
    tpipeIntroBlinkT = 0;
    tpipeStartFade( tpipeTitle, 0 );
    tpipeState = TPIPE_STATE_INTRO_FADEIN;
}

void tpipeBeginNextLevel()
{
    tpipeGP.firstTime = 1;
    tpipeGP.state = 0;
    tpipeGPInit();
    tpipeLoadLevel( tpipeGP.level );
    tpipeWaitFrames = 15;
    tpipeState = TPIPE_STATE_LEVEL_LOAD_WAIT1;
}

void gameTinyPipe_init()
{
    InitTinyJoypad();
    tpipeTickSkipCounter = 0;
    tpipeBeginNewGame();
}

void gameTinyPipe_update()
{
    tpipeTickSkipCounter++;
    if( tpipeTickSkipCounter < TPIPE_TICK_DIVISOR ) return;
    tpipeTickSkipCounter = 0;

    if( tpipeState == TPIPE_STATE_INTRO_FADEIN )
    {
        if( tpipeAdvanceFade() )
        {
            tpipeIntroBlinkT = 0;
            tpipeState = TPIPE_STATE_INTRO_WAIT;
        }
        return;
    }

    if( tpipeState == TPIPE_STATE_INTRO_WAIT )
    {
        tpipeIntroDraw( tpipeIntroBlinkT < 20 );
        if( isFirePressed() )
        {
            tpipeSound0();
            tpipeState = TPIPE_STATE_INTRO_WAIT_RELEASE;
            return;
        }
        if( tpipeIntroBlinkT < 40 ) tpipeIntroBlinkT++;
        else tpipeIntroBlinkT = 0;
        return;
    }

    if( tpipeState == TPIPE_STATE_INTRO_WAIT_RELEASE )
    {
        if( tpipeForceRedraw )
        {
            tpipeIntroDraw( 0 );
            tpipeForceRedraw = 0;
        }
        if( !isFirePressed() )
        {
            tpipeStartFade( tpipeTitle, 1 );
            tpipeState = TPIPE_STATE_INTRO_FADEOUT;
        }
        return;
    }

    if( tpipeState == TPIPE_STATE_INTRO_FADEOUT )
    {
        if( tpipeAdvanceFade() )
          tpipeBeginNextLevel();
        return;
    }

    if( tpipeState == TPIPE_STATE_LEVEL_LOAD_WAIT1 )
    {
        if( tpipeWaitFrames > 0 ) { tpipeWaitFrames--; return; }
        tpipeDrawLevel();
        tpipeStartMusic( tpipeMusic2 );
        tpipeState = TPIPE_STATE_LEVEL_LOAD_MUSIC;
        return;
    }

    if( tpipeState == TPIPE_STATE_LEVEL_LOAD_MUSIC )
    {
        if( tpipeForceRedraw )
        {
            tpipeDrawLevelDisplay();
            tpipeForceRedraw = 0;
        }
        if( tpipeAdvanceMusic() )
        {
            tpipeWaitFrames = 15;
            tpipeState = TPIPE_STATE_LEVEL_LOAD_WAIT2;
        }
        return;
    }

    if( tpipeState == TPIPE_STATE_LEVEL_LOAD_WAIT2 )
    {
        if( tpipeForceRedraw )
        {
            tpipeDrawLevelDisplay();
            tpipeForceRedraw = 0;
        }
        if( tpipeWaitFrames > 0 ) { tpipeWaitFrames--; return; }
        tpipeStartFade( tpipeBackground, 0 );
        tpipeState = TPIPE_STATE_LEVEL_FADEIN;
        return;
    }

    if( tpipeState == TPIPE_STATE_LEVEL_FADEIN )
    {
        if( tpipeAdvanceFade() )
          tpipeState = TPIPE_STATE_PLAYING;
        return;
    }

    if( tpipeState == TPIPE_STATE_LEVEL_COMPLETE_FADEOUT )
    {
        if( tpipeAdvanceFade() )
          tpipeBeginNextLevel();
        return;
    }

    if( tpipeState == TPIPE_STATE_GAMEOVER_FADEOUT )
    {
        tpipeAdvanceMusic();
        if( tpipeAdvanceFade() )
          tpipeBeginNewGame();
        return;
    }

    // TPIPE_STATE_PLAYING
    tpipeAdvanceSweep();

    if( !tpipeMain.killed )
    {
        if( isRightPressed() ) { tpipeShieldRemove(); tpipeRightMove( &tpipeMain ); }
        else if( isLeftPressed() ) { tpipeShieldRemove(); tpipeLeftMove( &tpipeMain ); }
        else tpipeDecel( &tpipeMain );
    }
    else
    {
        if( tpipeMain.active == 0 ) tpipeAdjGP();
    }

    tpipeRefreshTurtle();
    tpipeGravity( &tpipeMain, 1 );
    tpipeHitBumpCheck();

    if( isFirePressed() ) { tpipeShieldRemove(); tpipeJump( &tpipeMain ); }
    else
    {
        if( tpipeMain.cancelJump == 2 )
        {
            tpipeMain.cancelJump = 0;
            if( tpipeTarget.active == 2 ) tpipeTarget.active = 0;
        }
    }

    tpipeCheckCycleCollision();
    tpipeRndMixer();
    tpipeTimerForNewTurtle();
    tpipeTinyFlip();

    if( tpipeBlink < 1 ) tpipeBlink = tpipeBlink + 1;
    else tpipeBlink = 0;

    if( tpipeGP.state == 1 )
    {
        if( tpipeGP.level < 97 ) tpipeGP.level++;
        tpipeStartFade( tpipeBackground, 1 );
        tpipeState = TPIPE_STATE_LEVEL_COMPLETE_FADEOUT;
    }
    else if( tpipeGP.state == 2 )
    {
        tpipeStartMusic( tpipeMusic1 );
        tpipeStartFade( tpipeBackground, 1 );
        tpipeState = TPIPE_STATE_GAMEOVER_FADEOUT;
    }
}

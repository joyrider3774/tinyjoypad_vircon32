// =============================================================================
// Tiny Lander v1.0 (Roger Buehler / tscha70, 2020, GPLv3) - a Lunar-Lander
// style game: thrust left/right/up to guide a ship down onto a landing pad
// carved into a scrolling terrain silhouette, across 10 hand-authored
// levels, before running out of fuel or crashing.
//
// Structural notes:
// - Not tinyJoypadShim/FastTinyDriver.h or obonoCoreShim/TinyJoypadWorks
//   lineage - its own `gameinterface.h/.cpp` - but investigation before
//   porting showed this doesn't actually need a new shim at all: its
//   `JOYPAD_LEFT/RIGHT/UP/DOWN/FIRE` macros use the exact same
//   `analogRead(A0)`/`analogRead(A3)`/`digitalRead(1)` thresholds as every
//   Daniel-C game already ported here, and its own `SOUND(freq,dur)` is a
//   byte-for-byte identical PORTB bit-bang formula to ELECTROLIB.h's
//   shared Sound() - so button reads and tone playback are ported straight
//   onto the existing `isLeftPressed()`/etc and `Sound()` from
//   `tinyJoypadShim`, the same as every other game in this family.
// - No C++ classes (`typedef struct GAME{...}`/`typedef struct DIGITAL{...}`
//   are plain C structs, not classes) - the "DIGITAL" struct's `uint8_t
//   D[5]` array member was ported as 5 separate named fields
//   (`d0`..`d4`, read via a `tlandDigitAt()` index helper) rather than an
//   array-typed struct member, since no existing struct in this project
//   had one and there was no need to be the first to find out whether
//   it's supported.
// - No genuine real-time throttle anywhere upstream (no `_delay_ms()`/
//   `FPS_Control` gating the main loop) - the "no rate to match" category,
//   same as Trick/Invaders/Pinball/Bert/Tris - runs at native Vircon32
//   60fps with no tick divisor.
// - `GameDisplay()`'s own per-pixel collision detection is genuinely
//   embedded inside what's otherwise a "compute this column's byte"
//   render function (a classic overlap-via-OR-vs-ADD bit trick) - ported
//   exactly as structured, including the fact that a colliding pixel
//   re-invokes `tlandLanderDisplay()`/`tlandGetLanderSprite()` a second
//   time within the same call (which, since `shipExplode` was *just* set
//   to 3 by the same collision check, immediately renders that pixel via
//   the explosion-sprite branch instead of the normal one) - this dual-
//   call structure is the actual mechanism upstream uses to make the
//   explosion animation begin visibly on the very frame the crash is
//   detected, not a redundant call to simplify away.
// - `showAllScoresAndBonuses()`'s two blocking loops (a per-bonus-star
//   flash-and-chime, and a "count the score up one point at a time with a
//   chime each point" tally) are both converted to explicit frame-stepped
//   sub-states, the usual "blocking loop -> resumable state" treatment.
//   The point-by-point tally is uncapped upstream (could be up to
//   `LevelScore * 4`, and `LevelScore` reaches 240 on the last level - up
//   to 960 individual +1 steps) - downsampled to finish in about half a
//   second regardless of magnitude (a computed step size), the same
//   "downsample a computed sweep rather than reproducing every literal
//   step" treatment already used for Missile/Arena's own oversized sound
//   sweeps, just applied to a *visual* count-up loop instead of audio.
// - `SetLandingMap()`'s local `prev` variable is read (`prev==0`) before
//   ever being assigned on its very first loop iteration, upstream relying
//   on whatever garbage happened to be on the AVR stack (very likely zero
//   in practice on real hardware, but formally undefined behavior, not a
//   documented AVR-vs-Vircon32 semantic difference like this project's
//   other found bugs) - initialized explicitly to 0 in the port rather
//   than risking Vircon32's own uninitialized-stack-value behavior
//   differing from AVR's, since 0 is clearly what the surrounding logic
//   already assumes as the starting condition.
// - `moveShip()` clamps `ShipPosY` against an *upper* bound (`>55`) but
//   has no corresponding lower-bound clamp at all - upstream's own
//   `ShipPosY` is `uint8_t`, so flying up aggressively enough to push it
//   negative would AVR-wrap to a large positive value (near the bottom of
//   the screen) rather than go negative - this reads as a plain missing
//   bounds check upstream (nothing else in the game treats a negative/
//   wrapped Y as a deliberate mechanic, unlike Tiny SQuest's or Tiny
//   DDug's own genuine wraparound-reliant tricks), not a designed
//   behavior worth reproducing - fixed with an explicit lower clamp
//   (`shipPosY < 0 -> 0`) instead, avoiding relying on Vircon32's own
//   unspecified negative-value handling in the downstream `/8`/`%8` page
//   arithmetic.
// - EEPROM/high-score persistence: none exists upstream for this game at
//   all (score is already always session-local), so nothing was dropped.
// =============================================================================

#define TLAND_MAX_LEVEL 10
#define TLAND_VLIMIT 100
#define TLAND_MOVE_Y 35
#define TLAND_MOVE_X 35
#define TLAND_TRUST_Y 1
#define TLAND_TRUST_X 1
#define TLAND_GRAVITY_DEC_Y 1
#define TLAND_FULLTHRUST 18
#define TLAND_ACCELERATOR 45
#define TLAND_LANDING_SPEED 35
#define TLAND_BONUS_SPEED1 13
#define TLAND_BONUS_SPEED2 24
#define TLAND_DIGIT_SIZE 4
#define TLAND_SCORE_OFFSET 1
#define TLAND_SCORE_DIGITS 5
#define TLAND_VELO_DIGITS 4
#define TLAND_VELO_OFFSET 5

// -----------------------------------------------------------------------------
//   Data tables (extracted + byte-diff verified against spritebank.h /
//   gameinterface.h)
// -----------------------------------------------------------------------------

int[56] tlandLANDER =
{
0,14,9,9,9,14,0,240,16,16,16,16,16,240,240,16,
48,112,48,16,240,4,14,9,9,9,14,0,0,14,9,9,
9,14,4,8,42,20,107,20,42,8,0,34,8,85,8,34,
0,0,65,0,8,0,65,0,
};

int[184] tlandDASHBOARD =
{
0,184,168,232,0,248,136,136,0,248,136,248,0,248,72,184,
0,248,168,136,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,40,
16,40,16,40,16,40,16,40,16,40,16,40,16,40,16,40,
16,40,0,0,0,0,184,168,232,0,248,40,56,0,248,168,
168,0,248,168,136,0,248,136,112,0,0,0,0,248,32,248,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,120,192,120,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,248,40,8,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,
};

int[48] tlandDIGITS =
{
248,136,248,0,0,248,0,0,232,168,184,0,136,168,248,0,
56,32,248,0,184,168,232,0,248,168,232,0,8,232,24,0,
248,168,248,0,184,168,248,0,32,112,32,0,32,32,32,0,
};

int[72] tlandSTARFULL =
{
0,0,0,0,0,0,0,0,0,128,224,248,224,128,0,0,
0,0,0,0,0,0,0,0,0,0,0,2,6,6,14,158,
254,255,255,255,255,255,254,254,158,14,6,2,2,0,0,0,
0,0,0,0,0,0,12,7,3,1,1,0,0,1,3,3,
7,12,0,0,0,0,0,0,
};

int[72] tlandSTAROUTLINE =
{
0,0,0,0,0,0,0,0,0,64,16,4,16,64,0,0,
0,0,0,0,0,0,0,0,0,0,2,5,8,1,144,33,
0,0,0,0,0,0,0,33,144,9,0,5,2,0,0,0,
0,0,0,0,0,8,18,8,4,2,0,1,0,2,4,8,
18,8,0,0,0,0,0,0,
};

int[5] tlandLIVE =
{
96,24,24,96,0,
};

int[1024] tlandINTRO =
{
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,128,0,0,0,0,0,0,0,0,0,0,
0,0,0,30,48,30,0,0,62,0,32,0,62,34,62,0,
0,0,128,128,128,128,128,128,128,128,128,0,128,128,128,128,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,184,252,252,252,252,184,128,
128,128,128,128,128,255,130,128,128,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,3,3,3,255,255,255,3,3,3,0,253,253,253,253,
0,0,252,252,252,28,28,28,248,248,240,0,28,252,252,248,
0,192,252,252,60,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,224,240,248,252,254,255,255,127,63,63,63,63,63,63,
127,255,255,255,255,255,255,255,255,255,254,252,248,240,224,192,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,255,255,255,0,0,0,0,255,255,255,255,
0,0,255,255,255,0,0,0,255,255,255,0,0,3,63,255,
240,255,127,3,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,255,255,255,255,255,129,0,0,0,0,0,0,0,0,
0,0,129,255,255,255,255,255,129,129,129,135,255,255,255,255,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,7,7,7,
3,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,3,7,15,31,63,127,255,254,252,252,252,252,124,124,
126,255,255,127,127,127,127,255,255,255,127,63,31,15,7,3,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,248,248,248,248,0,0,0,0,0,0,128,192,192,
192,192,192,192,128,0,0,192,192,192,128,128,192,192,192,128,
0,0,0,128,128,192,192,192,248,248,248,0,0,0,128,192,
192,192,192,192,128,0,0,0,192,192,192,0,128,192,192,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,128,224,120,
62,15,7,31,127,255,255,255,255,255,255,255,255,255,175,223,
175,223,175,223,175,255,255,255,255,255,255,255,255,127,31,7,
15,30,120,224,128,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,255,255,255,255,0,0,0,0,0,192,227,243,243,
56,28,255,255,255,0,0,255,255,255,255,1,1,255,255,255,
0,0,255,255,255,3,1,1,255,255,255,0,0,255,255,255,
25,24,25,159,159,159,0,0,255,255,255,7,3,3,3,0,
0,0,0,0,0,0,0,0,0,128,224,248,62,31,13,12,
4,6,6,3,3,1,1,1,1,1,1,1,193,241,255,255,
255,255,255,255,241,193,1,1,1,1,1,1,1,3,3,6,
6,4,12,13,31,62,248,224,128,0,0,0,0,0,0,0,
0,0,0,15,15,15,15,14,14,14,14,0,3,7,15,15,
12,4,15,15,15,0,0,15,15,15,15,0,0,15,15,15,
0,0,3,7,15,14,12,12,15,15,15,0,0,3,7,15,
14,12,14,15,7,3,0,0,15,15,15,0,0,0,0,0,
0,0,0,0,4,12,12,28,30,31,31,12,12,4,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,4,12,12,31,31,30,28,12,12,4,0,0,
};

int[50] tlandGAMELEVEL =
{
44,33,150,10,75,110,33,150,20,75,34,30,150,20,50,42,
25,100,40,20,29,40,100,40,20,35,40,75,60,10,113,30,
120,60,10,28,10,150,120,10,30,5,150,120,10,26,49,150,
240,5,
};

int[540] tlandGAMEMAP =
{
63,44,32,22,12,6,4,6,10,18,16,20,12,36,38,30,
28,26,10,2,0,0,0,2,10,28,40,63,63,63,63,63,
63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,
63,63,63,63,63,63,63,10,0,0,0,14,18,14,30,20,
16,19,12,25,30,30,27,28,26,4,1,1,4,4,10,15,
63,63,63,63,45,40,50,63,63,63,63,55,63,63,63,63,
50,55,45,50,63,63,63,63,55,60,60,63,1,1,9,15,
18,20,22,20,18,15,9,1,1,9,15,18,20,22,20,18,
15,9,1,0,0,0,0,38,38,38,63,63,63,63,58,53,
48,43,38,43,48,53,58,63,63,63,63,38,38,38,38,38,
38,38,63,40,30,20,15,12,10,12,15,20,30,40,45,46,
45,36,30,28,15,10,5,0,0,0,0,4,63,63,63,63,
63,55,63,60,63,63,63,63,63,63,63,63,63,63,63,55,
45,40,30,30,63,63,63,63,30,2,3,3,2,12,12,1,
20,20,15,10,7,5,5,7,12,20,22,22,22,22,18,0,
0,0,0,30,54,60,50,50,40,59,40,48,40,45,50,53,
55,55,53,47,40,38,38,38,60,38,60,34,32,25,5,3,
2,2,5,12,10,12,15,40,42,40,0,0,0,40,45,40,
10,8,5,2,2,2,7,8,9,63,55,63,63,55,63,55,
63,63,60,63,63,63,63,63,63,63,63,55,55,57,57,63,
63,63,63,63,0,0,0,0,20,23,1,23,22,1,20,19,
18,1,16,15,1,13,12,11,10,1,8,7,6,5,4,20,
30,36,36,62,36,36,37,63,39,40,41,42,63,44,63,46,
47,63,49,50,62,52,63,54,55,56,35,35,25,5,5,5,
5,10,10,5,5,30,40,45,45,40,30,20,40,20,20,48,
0,0,0,7,40,63,63,63,63,63,35,30,30,63,63,63,
63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,
45,45,45,20,20,20,1,1,1,1,1,1,1,1,1,1,
20,1,1,1,1,1,35,0,0,0,0,63,63,63,62,62,
62,63,40,40,63,60,20,40,63,63,63,63,40,40,63,63,
63,63,61,61,61,61,1,1,1,5,41,44,41,5,1,1,
1,1,1,1,1,5,41,44,41,5,1,1,2,1,0,0,
0,45,63,55,63,63,60,63,63,63,59,34,17,34,59,63,
63,63,59,63,63,63,59,23,20,55,23,25,
};

// -----------------------------------------------------------------------------
//   Sound note sequences (freq, dur, extraMs triples - extraMs adds a
//   real-time gap after this note's own natural duration, matching the
//   inter-note _delay_ms() calls interspersed between only some notes
//   upstream)
// -----------------------------------------------------------------------------

// INTROSOUND(): SOUND(80,55);delay(20);SOUND(90,55);delay(20);SOUND(100,55);SOUND(115,255);SOUND(115,255);
int[15] tlandIntroNotes =
{
80,55,20, 90,55,20, 100,55,0, 115,255,0, 115,255,0,
};
#define TLAND_INTRO_COUNT 5

// VICTORYSOUND(): SOUND(111,100);delay(20);SOUND(111,90);delay(20);SOUND(144,255);SOUND(144,255);SOUND(144,255);
int[15] tlandVictoryNotes =
{
111,100,20, 111,90,20, 144,255,0, 144,255,0, 144,255,0,
};
#define TLAND_VICTORY_COUNT 5

// ALERTSOUND(): SOUND(150,100);delay(100);SOUND(150,90);delay(100);SOUND(150,100);
int[9] tlandAlertNotes =
{
150,100,100, 150,90,100, 150,100,0,
};
#define TLAND_ALERT_COUNT 3

// HAPPYSOUND(): SOUND(75,90);delay(10);SOUND(114,90);SOUND(121,90);
int[9] tlandHappyNotes =
{
75,90,10, 114,90,0, 121,90,0,
};
#define TLAND_HAPPY_COUNT 3

// The attract screen's own "fire with no direction held" cue: SOUND(100,125);SOUND(50,125);
int[6] tlandStartNotes2 =
{
100,125,0, 50,125,0,
};
#define TLAND_START2_COUNT 2

// -----------------------------------------------------------------------------
//   Game state structs
// -----------------------------------------------------------------------------

struct TlandGame
{
    int score;
    int stars;
    int shipPosX;
    int shipPosY;
    int thrustUp;
    int thrustLeft;
    int thrustRight;
    int fuel;
    int fuelBonus;
    int velocityY;
    int velocityX;
    int velXCounter;
    int velYCounter;
    int toggle;
    int shipExplode;
    int collision;
    int hasLanded;
    int endCounter;
    int level;
    int levelScore;
    int landingPadLeft;
    int landingPadRight;
    int lives;
};

struct TlandDigital
{
    int d0;
    int d1;
    int d2;
    int d3;
    int d4;
    int isNegative;
};

TlandGame tlandGame;
TlandDigital tlandScore;
TlandDigital tlandVelX;
TlandDigital tlandVelY;

int tlandDigitAt( TlandDigital* data, int i )
{
    if( i == 0 ) return data->d0;
    if( i == 1 ) return data->d1;
    if( i == 2 ) return data->d2;
    if( i == 3 ) return data->d3;
    return data->d4;
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define TLAND_STATE_ATTRACT 0
#define TLAND_STATE_PLAYING 1
#define TLAND_STATE_LEVEL_CLEAR_WAIT1 2
#define TLAND_STATE_LEVEL_CLEAR_STARS 3
#define TLAND_STATE_LEVEL_CLEAR_TALLY 4
#define TLAND_STATE_DEATH_WAIT 5

int tlandState;
int tlandWaitFrames;
int tlandBonusPoints;
int tlandScoreTallyTarget;
int tlandScoreTallyStep;

// Originally uncapped (native 60fps, no genuine upstream rate to match -
// see the header comment) - a whole-tick throttle added per direct user
// request to run at half speed, including logic (not just render). Gates
// the entire state-dispatch body below, matching the established "one
// divisor, no dual bookkeeping" philosophy from Tiny SQuest/Tiny DDug's
// own throttles - every tick-counted constant (tlandWaitFrames etc) is
// left unrescaled, so all waits are now proportionally twice as long in
// real time too, uniformly. The sound sequencer is deliberately kept
// OUTSIDE this gate (advanced every real frame) so audio timing stays in
// real wall-clock time rather than also being halved.
#define TLAND_TICK_DIVISOR 2
int tlandTickSkipCounter;

// -----------------------------------------------------------------------------
//   Sound sequencer (shared, one active sequence at a time)
// -----------------------------------------------------------------------------

int tlandSeqActive;
int* tlandSeqNotes;
int tlandSeqCount;
int tlandSeqIndex;
int tlandSeqWaitFrames;

void tlandStartNoteSeq( int* notes, int count )
{
    tlandSeqNotes = notes;
    tlandSeqCount = count;
    tlandSeqIndex = 0;
    tlandSeqActive = 1;
    tlandSeqWaitFrames = 0;
}

void tlandAdvanceNoteSeq()
{
    if( !tlandSeqActive )
        return;

    if( tlandSeqWaitFrames > 0 )
    {
        tlandSeqWaitFrames--;
        return;
    }

    if( tlandSeqIndex >= tlandSeqCount )
    {
        tlandSeqActive = 0;
        return;
    }

    int freq = tlandSeqNotes[ tlandSeqIndex * 3 ];
    int dur = tlandSeqNotes[ tlandSeqIndex * 3 + 1 ];
    int extraMs = tlandSeqNotes[ tlandSeqIndex * 3 + 2 ];
    Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    waitFrames += ( extraMs * 60 ) / 1000;

    tlandSeqWaitFrames = waitFrames;
    tlandSeqIndex++;
}

// -----------------------------------------------------------------------------
//   Level / landing-pad setup
// -----------------------------------------------------------------------------

void tlandSetLandingMap( int level, TlandGame* game )
{
    int i;
    int prev = 0;
    game->landingPadLeft = 0;
    game->landingPadRight = 255;
    for( i = 0; i < 27; i++ )
    {
        int val = tlandGAMEMAP[ ( level - 1 ) * 2 * 27 + i ];

        if( ( prev == 0 && ( val != 0 || i == 26 ) ) && game->landingPadRight == 0 )
            game->landingPadRight = i * 4;

        if( val == 0 && game->landingPadLeft == 0 )
        {
            game->landingPadLeft = i * 4;
            game->landingPadRight = 0;
        }

        prev = val;
    }
}

void tlandSetNextLevel( int level, TlandGame* game )
{
    if( level > TLAND_MAX_LEVEL )
        level = 1;
    game->level = level;
    tlandSetLandingMap( level, game );
    game->shipPosX = tlandGAMELEVEL[ ( level - 1 ) * 5 + 0 ];
    game->shipPosY = tlandGAMELEVEL[ ( level - 1 ) * 5 + 1 ];
    game->fuel = 100 * tlandGAMELEVEL[ ( level - 1 ) * 5 + 2 ];
    game->levelScore = tlandGAMELEVEL[ ( level - 1 ) * 5 + 3 ];
    game->fuelBonus = 100 * tlandGAMELEVEL[ ( level - 1 ) * 5 + 4 ];
}

void tlandInitGame( TlandGame* game )
{
    tlandSetNextLevel( game->level, game );
    game->velocityY = 0;
    game->velocityX = 0;
    game->velXCounter = 0;
    game->velYCounter = 0;
    game->shipExplode = 0;
    game->toggle = 1;
    game->collision = 0;
    game->hasLanded = 0;
    game->endCounter = 0;
    game->stars = 0;
}

void tlandSplitDigits( int val, TlandDigital* data )
{
    data->d4 = val / 10000;
    data->d3 = ( val - ( data->d4 * 10000 ) ) / 1000;
    data->d2 = ( val - ( data->d3 * 1000 ) - ( data->d4 * 10000 ) ) / 100;
    data->d1 = ( val - ( data->d2 * 100 ) - ( data->d3 * 1000 ) - ( data->d4 * 10000 ) ) / 10;
    data->d0 = val - ( data->d1 * 10 ) - ( data->d2 * 100 ) - ( data->d3 * 1000 ) - ( data->d4 * 10000 );
}

void tlandFillData( int myValue, TlandDigital* data )
{
    int absVal = myValue;
    if( absVal < 0 ) absVal = -absVal;
    tlandSplitDigits( absVal, data );
    data->isNegative = ( myValue < 0 );
}

// -----------------------------------------------------------------------------
//   Physics
// -----------------------------------------------------------------------------

void tlandChangeSpeed( TlandGame* game )
{
    // thrustLeft increases velocityX (ship moves RIGHT on screen, per
    // tlandMoveShip below) and thrustRight decreases it (moves LEFT) -
    // upstream's own naming reflects which thruster physically fires
    // (a left-mounted thruster pushes the ship right, like a real
    // rocket), not the resulting screen direction, so a faithful
    // isLeftPressed()->thrustLeft mapping reads backwards on a real
    // gamepad. Swapped which physical button sets which flag (not the
    // flags' own meaning elsewhere) so LEFT visually moves the ship
    // left - matches the same class of deliberate control remap already
    // done for Tiny Arkanoid's own faithfully-inverted-but-confusing
    // upstream mapping.
    game->thrustLeft = isRightPressed();
    game->thrustRight = isLeftPressed();
    game->thrustUp = isFirePressed();
    game->toggle = !game->toggle;

    if( game->thrustLeft && game->fuel > 0 )
    {
        game->fuel -= ( TLAND_FULLTHRUST / 2 );
        game->velocityX += TLAND_TRUST_X;
        if( game->velocityX > TLAND_VLIMIT ) game->velocityX = TLAND_VLIMIT;
    }
    else if( game->thrustRight && game->fuel > 0 )
    {
        game->fuel -= ( TLAND_FULLTHRUST / 2 );
        game->velocityX -= TLAND_TRUST_X;
        if( game->velocityX < -TLAND_VLIMIT ) game->velocityX = -TLAND_VLIMIT;
    }

    if( game->thrustUp && game->fuel > 0 )
    {
        game->fuel -= ( TLAND_FULLTHRUST * 2 );
        game->velocityY += TLAND_TRUST_Y;
        if( game->velocityY > TLAND_VLIMIT ) game->velocityY = TLAND_VLIMIT;
    }
    else
    {
        game->velocityY -= TLAND_GRAVITY_DEC_Y;
        if( game->velocityY < -TLAND_VLIMIT ) game->velocityY = -TLAND_VLIMIT;
    }

    if( game->fuel <= 0 ) game->fuel = 0;
}

void tlandMoveShip( TlandGame* game )
{
    if( game->shipExplode > 0 || game->collision || game->hasLanded )
        return;

    int absVX = game->velocityX; if( absVX < 0 ) absVX = -absVX;
    int absVY = game->velocityY; if( absVY < 0 ) absVY = -absVY;
    game->velXCounter += absVX;
    game->velYCounter += absVY;

    if( game->velXCounter >= TLAND_MOVE_X )
    {
        game->velXCounter = 0;
        if( game->velocityX > 0 ) game->shipPosX += 1;
        if( game->velocityX < 0 ) game->shipPosX -= 1;
    }

    if( game->velYCounter >= TLAND_MOVE_Y )
    {
        int absVYnow = game->velocityY; if( absVYnow < 0 ) absVYnow = -absVYnow;
        int inc = ( absVYnow / TLAND_ACCELERATOR ) + 1;
        game->velYCounter = 0;
        if( game->velocityY > 0 ) game->shipPosY -= inc;
        if( game->velocityY < 0 ) game->shipPosY += inc;
    }

    if( game->shipPosX > 121 ) game->shipPosX = 121;
    else if( game->shipPosX < 23 ) game->shipPosX = 23;
    if( game->shipPosY > 55 ) game->shipPosY = 55;
    if( game->shipPosY < 0 ) game->shipPosY = 0;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int tlandGetLandscape( int x, int y, int level, TlandGame* game )
{
    // game unused (matches upstream's own GETLANDSCAPE, which never uses
    // its GAME* param either) - self-assigned to silence the warning.
    game = game;
    int height = 63;
    int frame = 0;
    int t = x % 4;
    int ind = x / 4;
    int val = height - tlandGAMEMAP[ level * 27 + ind ];
    int valT = height - tlandGAMEMAP[ ( level + 1 ) * 27 + ind ];
    if( x > 0 && t != 0 )
    {
        if( ( ind + 1 ) < 27 )
        {
            if( val < height )
            {
                int val2 = height - tlandGAMEMAP[ level * 27 + ind + 1 ];
                val += ( ( val2 - val ) / 4 ) * t;
            }
            int valT2 = height - tlandGAMEMAP[ ( level + 1 ) * 27 + ind + 1 ];
            valT += ( ( valT2 - valT ) / 4 ) * t;
        }
    }

    int b = val / 8;
    int bT = valT / 8;
    if( b == y )
    {
        if( val == height )
        {
            if( x % 2 == 0 ) frame = frame | 0xB8;
            else frame = frame | 0x58;
        }
        else
            frame = frame | ( 0xFF << ( val - ( b * 8 ) ) );
    }
    if( bT == y )
        frame = frame | ( 0xFF >> ( 7 - ( valT - ( bT * 8 ) ) ) );
    if( y > b || y < bT )
        frame = frame | 0xFF;

    return frame;
}

int tlandGetLanderSprite( int x, int y, TlandGame* game )
{
    // y unused (the sprite's own row placement is handled entirely by
    // tlandLanderDisplay's own shift-based split) - self-assigned to
    // silence the warning.
    y = y;
    int sprite = 0;

    if( game->shipExplode > 0 )
    {
        sprite = tlandLANDER[ ( x - game->shipPosX ) + ( ( 8 - game->shipExplode ) * 7 ) ];
        Sound( 20 * game->shipExplode, 10 );
        game->shipExplode--;
        if( game->shipExplode < 1 ) game->shipExplode = 3;
        return sprite;
    }

    if( game->thrustLeft )
        sprite = tlandLANDER[ ( x - game->shipPosX ) + 21 ];
    else if( game->thrustRight )
        sprite = tlandLANDER[ ( x - game->shipPosX ) + 28 ];
    else
        sprite = tlandLANDER[ ( x - game->shipPosX ) ];

    if( game->thrustUp && game->toggle && game->fuel > 0 )
        return sprite | tlandLANDER[ ( x - game->shipPosX ) + 14 ];
    return sprite | tlandLANDER[ ( x - game->shipPosX ) + 7 ];
}

int tlandLanderDisplay( int x, int y, TlandGame* game )
{
    int line = game->shipPosY / 8;
    int offset = game->shipPosY % 8;
    if( y == line || ( y == line + 1 && offset > 0 ) )
    {
        if( ( x - game->shipPosX ) >= 0 && ( x - game->shipPosX ) < 7 )
        {
            int sprite = tlandGetLanderSprite( x, y, game );
            if( offset == 0 && y == line ) return sprite;
            if( offset > 0 && y == line ) return sprite << offset;
            if( offset > 0 && y == ( line + 1 ) ) return sprite >> ( 8 - offset );
        }
    }
    return 0;
}

int tlandScoreDisplay( int x, int y, TlandDigital* score )
{
    if( y != 1 || x < TLAND_SCORE_OFFSET || x > ( TLAND_SCORE_OFFSET + ( TLAND_SCORE_DIGITS * TLAND_DIGIT_SIZE ) - 1 ) )
        return 0;
    int part = ( x - TLAND_SCORE_OFFSET ) / TLAND_DIGIT_SIZE;
    return tlandDIGITS[ x - TLAND_SCORE_OFFSET - ( TLAND_DIGIT_SIZE * part ) + ( tlandDigitAt( score, ( TLAND_SCORE_DIGITS - 1 ) - part ) * TLAND_DIGIT_SIZE ) ];
}

int tlandVelocityDisplay( int x, int y, TlandDigital* velocity, int horizontal )
{
    if( ( horizontal == 1 && y != 4 ) || ( horizontal == 0 && y != 5 ) )
        return 0;
    if( x < TLAND_VELO_OFFSET || x > ( TLAND_VELO_OFFSET + ( TLAND_VELO_DIGITS * TLAND_DIGIT_SIZE ) ) - 1 )
        return 0;
    if( x >= TLAND_VELO_OFFSET && x < ( TLAND_VELO_OFFSET + TLAND_DIGIT_SIZE ) )
        return tlandDIGITS[ x - TLAND_VELO_OFFSET + ( ( 10 + velocity->isNegative ) * TLAND_DIGIT_SIZE ) ];
    int part = ( x - TLAND_VELO_OFFSET ) / TLAND_DIGIT_SIZE;
    return tlandDIGITS[ x - TLAND_VELO_OFFSET - ( TLAND_DIGIT_SIZE * part ) + ( tlandDigitAt( velocity, ( TLAND_VELO_DIGITS - 1 ) - part ) * TLAND_DIGIT_SIZE ) ];
}

int tlandDashboardDisplay( int x, int y )
{
    if( x >= 0 && x <= 22 )
        return tlandDASHBOARD[ x + y * 23 ];
    return 0;
}

int tlandFuelDisplay( int x, int y, TlandGame* game )
{
    if( y != 6 ) return 0;
    if( x > 4 && x <= 19 )
    {
        if( ( game->fuel / 1000 ) + 1 > x - 4 || ( ( x - 4 == 1 ) && game->fuel > 0 ) )
            return 0xF8;
        return 0;
    }
    return 0;
}

// Precomputed once per frame (shipPosY is unchanged for the whole render
// pass) rather than re-derived - and, more importantly, used to skip
// calling tlandLanderDisplay() entirely for the ~826 of 840 game-area
// pixels/frame outside the ship's own real ~7x2 footprint, instead of
// calling it and letting its own internal bounds check return 0 - the
// same "self-gated call still costs a full call every time it's invoked"
// lesson applied throughout this project. The pre-check below is a
// literal duplicate of tlandLanderDisplay's own bounds check, not an
// approximation, so this can't change what gets drawn.
int tlandShipLine;
int tlandShipOffset;

int tlandGameDisplay( int x, int y, TlandGame* game )
{
    int offset = 23;
    if( x >= offset )
    {
        int frame;
        if( x == offset || x == 127 )
            frame = 0xFF;
        else
            frame = tlandGetLandscape( x - offset, y, ( game->level - 1 ) * 2, game );

        int ship = 0;
        if( ( y == tlandShipLine || ( y == tlandShipLine + 1 && tlandShipOffset > 0 ) ) && ( x - game->shipPosX ) >= 0 && ( x - game->shipPosX ) < 7 )
            ship = tlandLanderDisplay( x, y, game );

        if( y == 7 && x >= ( game->landingPadLeft + offset ) && x <= ( game->landingPadRight + offset ) )
        {
            if( ship != 0 && ( 0xFC | ship ) != ( 0xFC + ship ) )
            {
                int absVY = game->velocityY; if( absVY < 0 ) absVY = -absVY;
                if( absVY <= TLAND_LANDING_SPEED && ( game->shipPosX >= game->landingPadLeft + offset ) && ( game->shipPosX + 7 <= game->landingPadRight + offset ) )
                {
                    game->hasLanded = 1;
                    return frame | ship;
                }
                else
                {
                    if( !game->collision ) game->lives--;
                    game->shipExplode = 3;
                    game->collision = 1;
                    return frame | tlandLanderDisplay( x, y, game );
                }
            }
        }
        else if( ( frame != 0 && ship != 0 ) && ( frame | ship ) != ( frame + ship ) )
        {
            if( !game->collision ) game->lives--;
            game->shipExplode = 3;
            game->collision = 1;
            return frame | tlandLanderDisplay( x, y, game );
        }

        return frame | ship;
    }
    return 0;
}

int tlandStarsDisplay( int x, int y, TlandGame* game )
{
    int o1 = 23;
    int bg = 0;
    if( y == 0 && x > o1 ) bg = bg | 0x01;
    if( x == o1 ) bg = bg | 0xFF;
    if( x == 127 ) bg = bg | 0xFF;
    if( y == 7 && x > o1 ) bg = bg | 0x80;

    int offset = 40;
    if( y > 1 && y < 5 )
    {
        if( x > offset && x < ( offset + 72 ) )
        {
            if( game->stars > ( x - offset ) / 24 )
                return tlandSTARFULL[ ( ( x - offset ) % 24 ) + ( ( y - 2 ) * 24 ) ];
            else
                return tlandSTAROUTLINE[ ( ( x - offset ) % 24 ) + ( ( y - 2 ) * 24 ) ];
        }
    }
    return bg;
}

int tlandLivesDisplay( int x, int y, TlandGame* game )
{
    int offset = 1;
    if( y == 7 && x >= offset && x < ( 4 * 5 ) + offset )
    {
        if( game->lives > ( x - offset ) / 5 )
            return tlandLIVE[ ( x - offset ) % 5 ];
    }
    return 0;
}

void tlandRenderFrame( int mode )
{
    // Every dashboard/UI layer (Dashboard/Score/Velocity/Fuel/Lives) has
    // its own real footprint entirely within x<=22 (the left instrument
    // panel), and GameDisplay/StarsDisplay's own real footprint is
    // entirely within x>=23 (confirmed by their own internal bounds
    // checks) - these never overlap, so gating the call site on this
    // fixed, static x boundary (rather than calling every self-gated
    // layer for all 128 columns, matching the established "self-gated
    // call still costs a full call" lesson) is a safe, exact match for
    // what each layer's own bounds check already computes, not an
    // approximation.
    int x, y, val;
    tlandShipLine = tlandGame.shipPosY / 8;
    tlandShipOffset = tlandGame.shipPosY % 8;
    md_beginFrame();
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
        {
            if( mode == 1 )
                val = tlandINTRO[ x + y * 128 ];
            else if( x <= 22 )
            {
                // Dashboard is the panel's own background, spanning all
                // 8 rows - but Score/Velocity/Fuel/Lives each only ever
                // match exactly one specific row (their own internal
                // bounds check), so gating them to that row at the call
                // site (rather than calling all 5 for every row) is
                // another instance of the same lesson as the x<=22 split
                // above, just one level narrower.
                val = tlandDashboardDisplay( x, y );
                if( y == 1 ) val = val | tlandScoreDisplay( x, y, &tlandScore );
                else if( y == 4 ) val = val | tlandVelocityDisplay( x, y, &tlandVelX, 1 );
                else if( y == 5 ) val = val | tlandVelocityDisplay( x, y, &tlandVelY, 0 );
                else if( y == 6 ) val = val | tlandFuelDisplay( x, y, &tlandGame );
                else if( y == 7 ) val = val | tlandLivesDisplay( x, y, &tlandGame );
            }
            else if( mode == 0 )
                val = tlandGameDisplay( x, y, &tlandGame );
            else
                val = tlandStarsDisplay( x, y, &tlandGame );
            md_drawColumn( x, y, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   Top-level state machine
// -----------------------------------------------------------------------------

void tlandBeginAttract()
{
    tlandGame.level = 1;
    tlandGame.score = 0;
    tlandGame.lives = 4;
    tlandState = TLAND_STATE_ATTRACT;
}

void tlandBeginLevelIntro()
{
    tlandInitGame( &tlandGame );
    tlandStartNoteSeq( tlandIntroNotes, TLAND_INTRO_COUNT );
    tlandState = TLAND_STATE_PLAYING;
}

void tlandBeginDeathWait()
{
    tlandWaitFrames = 120;
    tlandState = TLAND_STATE_DEATH_WAIT;
}

void tlandBeginLevelClear()
{
    tlandStartNoteSeq( tlandVictoryNotes, TLAND_VICTORY_COUNT );
    tlandGame.level++;

    int bonusPoints = 0;
    int absVY = tlandGame.velocityY; if( absVY < 0 ) absVY = -absVY;
    if( absVY <= TLAND_BONUS_SPEED2 ) bonusPoints++;
    if( absVY <= TLAND_BONUS_SPEED1 ) bonusPoints++;
    if( tlandGame.fuel >= tlandGame.fuelBonus ) bonusPoints++;
    tlandBonusPoints = bonusPoints;
    tlandGame.stars = 0;

    tlandScoreTallyTarget = tlandGame.score + tlandGame.levelScore + ( tlandGame.levelScore * bonusPoints );

    tlandWaitFrames = 60;
    tlandState = TLAND_STATE_LEVEL_CLEAR_WAIT1;
}

void gameTinyLander_init()
{
    tlandSeqActive = 0;
    tlandTickSkipCounter = 0;
    tlandBeginAttract();
}

void gameTinyLander_forceRedraw()
{
    if( tlandState == TLAND_STATE_ATTRACT )
        tlandRenderFrame( 1 );
    else if( tlandState == TLAND_STATE_LEVEL_CLEAR_STARS || tlandState == TLAND_STATE_LEVEL_CLEAR_TALLY )
        tlandRenderFrame( 2 );
    else
        tlandRenderFrame( 0 );
}

void gameTinyLander_update()
{
    tlandAdvanceNoteSeq();

    tlandTickSkipCounter++;
    if( tlandTickSkipCounter < TLAND_TICK_DIVISOR )
        return;
    tlandTickSkipCounter = 0;

    if( tlandState == TLAND_STATE_ATTRACT )
    {
        tlandRenderFrame( 1 );
        if( isFirePressed() )
        {
            if( isUpPressed() )
            {
                tlandGame.level = 10;
                tlandStartNoteSeq( tlandAlertNotes, TLAND_ALERT_COUNT );
            }
            else if( isDownPressed() )
            {
                tlandGame.lives = 255;
                tlandStartNoteSeq( tlandAlertNotes, TLAND_ALERT_COUNT );
            }
            else
            {
                tlandStartNoteSeq( tlandStartNotes2, TLAND_START2_COUNT );
            }
            tlandBeginLevelIntro();
        }
    }
    else if( tlandState == TLAND_STATE_PLAYING )
    {
        tlandFillData( tlandGame.score, &tlandScore );
        tlandFillData( tlandGame.velocityX, &tlandVelX );
        tlandFillData( tlandGame.velocityY, &tlandVelY );
        tlandMoveShip( &tlandGame );
        tlandChangeSpeed( &tlandGame );
        tlandRenderFrame( 0 );

        if( tlandGame.endCounter > 8 )
        {
            if( tlandGame.hasLanded )
                tlandBeginLevelClear();
            else
                tlandBeginDeathWait();
        }
        else
        {
            if( tlandGame.shipExplode > 0 || tlandGame.collision ) tlandGame.endCounter++;
            if( tlandGame.hasLanded ) tlandGame.endCounter = 10;
        }
    }
    else if( tlandState == TLAND_STATE_LEVEL_CLEAR_WAIT1 )
    {
        if( tlandWaitFrames > 0 )
            tlandWaitFrames--;
        else
        {
            tlandWaitFrames = 0;
            tlandState = TLAND_STATE_LEVEL_CLEAR_STARS;
        }
    }
    else if( tlandState == TLAND_STATE_LEVEL_CLEAR_STARS )
    {
        if( tlandWaitFrames > 0 )
        {
            tlandWaitFrames--;
        }
        else if( tlandGame.stars >= tlandBonusPoints )
        {
            int delta = tlandScoreTallyTarget - tlandGame.score;
            tlandScoreTallyStep = delta / 30;
            if( tlandScoreTallyStep < 1 ) tlandScoreTallyStep = 1;
            tlandState = TLAND_STATE_LEVEL_CLEAR_TALLY;
        }
        else
        {
            tlandGame.stars++;
            tlandRenderFrame( 2 );
            tlandStartNoteSeq( tlandHappyNotes, TLAND_HAPPY_COUNT );
            tlandWaitFrames = 30;
        }
    }
    else if( tlandState == TLAND_STATE_LEVEL_CLEAR_TALLY )
    {
        if( tlandGame.score < tlandScoreTallyTarget )
        {
            tlandGame.score += tlandScoreTallyStep;
            if( tlandGame.score > tlandScoreTallyTarget ) tlandGame.score = tlandScoreTallyTarget;
            tlandFillData( tlandGame.score, &tlandScore );
            tlandRenderFrame( 2 );
            Sound( 129, 2 );
        }
        else
        {
            tlandBeginLevelIntro();
        }
    }
    else if( tlandState == TLAND_STATE_DEATH_WAIT )
    {
        if( tlandWaitFrames > 0 )
            tlandWaitFrames--;
        else
        {
            if( tlandGame.lives > 0 )
                tlandBeginLevelIntro();
            else
                tlandBeginAttract();
        }
    }
}

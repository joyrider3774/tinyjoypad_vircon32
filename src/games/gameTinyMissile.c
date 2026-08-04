// =============================================================================
// Tiny Missile - ported from Daniel C's Tiny-Missile.ino (tinyjoypad.com,
// GPLv3). Same tinyJoypadShim lineage as Pinball/Pacman/Bomber/Doc/Bert/Tris/
// Arkanoid/Trick/Invaders (FastTinyDriver.h) - button reads/Sound() reuse
// that shim as-is, no shim changes needed. A Missile-Command-style game: a
// crosshair moves around the screen, launching interceptor rockets to
// destroy incoming missiles before they hit one of 6 domes/cities on the
// ground; clearing every incoming missile advances the level, losing every
// dome ends the game.
//
// Button mapping: ordinary, no remap needed - isLeftPressed()/isRightPressed()
// (A0) and isUpPressed()/isDownPressed() (A3) move the crosshair,
// isFirePressed() (digitalRead(1)) launches an interceptor. Same axis/
// threshold mapping as every other Daniel-C game here.
//
// Structural changes from upstream, beyond the usual dialect fixes:
//  - upstream's classes (CLASS_TMISSILE.h/.cpp: ARMY_TMISSILE,
//    STATIC_SPRITE_TMISSILE, STATIC_SPRITE_ANIM_TMISSILE, CROSS,
//    LINE_TMISSILE, DEFENCE, CLK - the last three with real single
//    inheritance from STATIC_SPRITE_TMISSILE, not just plain structs) are
//    flattened into plain structs with the base class's X/Y/ACTIVE fields
//    inlined directly into each derived type, and every method becomes a
//    `tmis*` function taking an explicit pointer instead of `this` - the
//    same treatment TinyMinez's `Game`/`Selection` classes needed, just a
//    handful of much smaller classes here (no deep hierarchies, no virtual
//    functions - closer in spirit to Tiny Bert's plain `Sprite` struct than
//    to TinyMinez's own bigger classes, despite the C++ `class` keyword
//    being used throughout instead of `struct`).
//  - upstream's `loop()` is `NEWGAME:`/`START_:`/`NEXTLEVEL:` labels around
//    two `while(1)` loops (title-screen poll, then the main play loop) -
//    rewritten as an explicit frame-stepped state machine, the same
//    approach every other tinyJoypadShim port here uses.
//  - the title screen's inner `while(1){if(BUTTON_UP){...goto START_;}}`
//    (busy-waits for the confirming Fire press to *release* before
//    starting, so that release doesn't double as the play loop's own
//    first "place an interceptor" gesture) needed the same local fire-gate
//    treatment TinyMinez's difficulty-confirm needed (`tmisFireGateActive`,
//    forced false on the same tick it's armed, not just later ticks -
//    otherwise the forced-false read itself looks like a fresh release
//    edge one tick later and the bug isn't actually fixed) - without it,
//    the very first frame of gameplay would immediately fire an
//    interceptor rocket the player never asked for.
//  - `Score_Dome_Munition_TMISSILE()` (the end-of-wave scoring sequence:
//    drain remaining ammo for +1 each, pause, award +5 per surviving dome,
//    pause, decide game-over-vs-next-level) is a genuinely blocking
//    multi-phase sequence upstream (`_delay_ms(500)` x3 plus an inner
//    `while(1)` ammo-drain loop) - converted to an explicit
//    `TMIS_STATE_LEVEL_CLEAR_*` sub-state sequence, one step per real
//    frame, the usual "blocking loop -> resumable state" treatment. The
//    per-dome bonus award loop itself has no real per-dome delay upstream
//    (just a redraw call per dome, no `_delay_ms`) so it's processed as a
//    single instantaneous pass between the two real-timed pauses, not its
//    own frame-stepped state.
//  - `ARMY_TMISSILE::ATTACK_WEAPON()` (the "a missile got through to the
//    crosshair" auto-return-fire) also busy-loops, rapid-firing every
//    remaining rocket in one blocking burst - converted to a
//    `tmisAttackBurstActive` flag ticking down one rocket per real frame;
//    the main engine update is skipped entirely while a burst is active,
//    matching upstream's own complete freeze during the burst.
//  - `SNDBOX_TMISSILE()`'s cases 1 and 2 are genuinely long blocking tone
//    sequences (`for(t=1;t<255;t++){Sound(50,2);Sound(t,2);}` - 254 pairs;
//    `for(t=200;t>10;t--){Sound(200-t,3);Sound(t,12);}` - 190 pairs) -
//    rather than hand-transcribe several hundred numbers, both tables are
//    built once at init time via the exact same loop upstream uses, then
//    played through a small shared frame-stepped note sequencer
//    (`tmisStartNoteSeq`/`tmisAdvanceNoteSeq`, modeled directly on Tiny
//    Arkanoid's/Tiny Minez's own of the same name - this is the third port
//    needing this exact pattern, worth promoting to a shared shim
//    primitive if a fourth ever needs it). Every *other* SNDBOX case (0,
//    3, 4, 5) is also 2 back-to-back Sound() calls, not a single tone -
//    also routed through the sequencer, since Vircon32's Sound() triggers
//    a real hardware channel and returns immediately (unlike AVR's
//    bit-banged version, which genuinely blocked for each call's own
//    duration) - calling it twice with no wait between would just cut the
//    first note off before it's audible, the same reasoning already
//    applied to TinyMinez's own short "blip" sounds.
//  - `Tiny_Flip_TMISSILE(1,7,126)` (the main play loop's own per-frame
//    redraw call) only redraws rows 1-6 and columns 0-125 each frame -
//    rows 0 and 7 are redrawn separately, only when something on them
//    actually changes (`REFRESH_TOPBAR_TMISSILE()`/`UPDATE_DOME_TMISSILE()`
//    call their own narrower `Tiny_Flip_TMISSILE(0,1,126)`/`(7,8,126)`).
//    This is the exact same VRAM-persistence assumption already found and
//    fixed in Pinball/Doc/Bert (upstream relies on the real SSD1306's
//    VRAM keeping whatever was drawn there last frame) - this port's
//    always-`clear_screen()`-then-redraw model can't replicate that, so
//    `tmisTinyFlip()` always redraws the full 128x8 screen every frame
//    instead of taking row/column-range parameters at all.
//  - Frame pacing: upstream's `CONTROL_FRAMERATE(46)` throttles the
//    *entire* loop iteration (logic+redraw together, no separate
//    tick-vs-redraw split the way Arkanoid's own loop had) to a genuine
//    ~1000/46 = 21.7fps - per this project's own standing rule (added
//    after several earlier ports had to retrofit this same fix), this is
//    a real fixed-rate throttle and gets a whole-function tick-skip divisor
//    from the start rather than shipping at 60fps: `TMIS_TICK_DIVISOR = 3`
//    (~20fps, the closest clean divisor of 60 - matching the same rounding
//    tolerance NumberPlace's own FPS=20 conversion already used for a
//    similarly-shaped non-round rate).
//  - Checked every `>>`/`<<` site inherited from `ELECTROLIB.cpp`
//    (`RecupeLineY`/`RecupeDecalageY`/`RECONSTRUCT_BYTE`) against this
//    project's own established shift-related bug classes (arithmetic vs
//    logical shift on a negative operand; shift-count wraparound past 32)
//    before porting: `RecupeLineY(int8_t Valeur){return Valeur>>3;}` is
//    typed to accept a signed value, but every actual call site in this
//    game only ever passes an already-non-negative position/track value
//    (missile Track, dome/intercept Y positions, line-endpoint Y
//    coordinates) - confirmed by tracing every call site, so the
//    sign-extension mismatch that broke HollowSeeker's own equivalent
//    helper doesn't actually manifest here. `SplitSpriteDecalageY`'s shift
//    amount is `RecupeDecalageY(...)` (a value%8 result, always 0-7) or
//    `8-decalage` (always 1-8) - both safely bounded well under 32, no
//    wraparound risk either.
// =============================================================================

#define TMIS_NUM_DOME 6
#define TMIS_NUM_MISSILE 4
#define TMIS_NUM_INTERCEPT 3
#define TMIS_NUM_DEFENCE 3

#define TMIS_STATE_TITLE 0
#define TMIS_STATE_PLAYING 1
#define TMIS_STATE_LEVEL_CLEAR_PAUSE1 2
#define TMIS_STATE_LEVEL_CLEAR_DRAIN_AMMO 3
#define TMIS_STATE_LEVEL_CLEAR_PAUSE2 4
#define TMIS_STATE_LEVEL_CLEAR_PAUSE3 5

// Genuine upstream whole-loop real-FPS throttle (CONTROL_FRAMERATE(46) ~=
// 21.7fps) - ported as a real tick-skip divisor from day one rather than
// shipping at 60fps and retrofitting later (see this project's own standing
// rule in memory). 60/3 = 20fps, the closest clean divisor.
#define TMIS_TICK_DIVISOR 3
int tmisTickSkipCounter;

// ~500ms at 20fps (this game's own throttled rate, not 60fps) - matches
// each of Score_Dome_Munition_TMISSILE()'s three _delay_ms(500) pauses.
#define TMIS_PAUSE_FRAMES 10

int tmisState;
int tmisPrevUp, tmisPrevDown, tmisPrevFire;
int tmisFireGateActive;
int tmisPauseFrames;
int tmisLevelClearSurvivors;

int tmisReverse;
int tmisM10000, tmisM1000, tmisM100, tmisM10, tmisM1;
int tmisScores;
int tmisShotAdj;
int tmisLevel;
int tmisRdlp;
int tmisStartRdlp;
int tmisOneDrop;

int tmisAttackBurstActive;

// ---- CLK (a small repeating/one-shot frame counter+trigger) ----
struct TmisClk
{
    int start;
    int end;
    int pos;
    int trig;
    int loop;
};

TmisClk tmisBlink;
TmisClk tmisSpeedMissile;
TmisClk tmisRenew;

void tmisClkInit( TmisClk* c, int start, int end, int loop )
{
    c->start = start;
    c->end = end;
    c->pos = start;
    c->trig = 0;
    c->loop = loop;
}

int tmisClkProgress( TmisClk* c )
{
    if( ( c->loop == 0 ) && ( c->trig == 1 ) )
      return c->trig;

    if( c->pos < c->end )
    {
        c->pos++;
    }
    else
    {
        c->trig = !c->trig;
        if( c->loop == 1 )
          c->pos = c->start;
        else
          c->pos = c->end;
    }
    return c->trig;
}

void tmisClkReset( TmisClk* c )
{
    c->pos = c->start;
    c->trig = 0;
}

// ---- ARMY (ammo: rocket clip + spare clips) ----
int tmisArmySpare;
int tmisArmyRocket;

void tmisArmyResetWeapon( int level )
{
    if( level > 4 )
      tmisArmySpare = 4;
    else
      tmisArmySpare = 3;
    tmisArmyRocket = 10;
}

int tmisArmyUseWeapon()
{
    if( tmisArmyRocket > 0 )
    {
        tmisArmyRocket--;
    }
    else
    {
        if( tmisArmySpare > 0 )
        {
            tmisArmySpare--;
            tmisArmyRocket = 9;
        }
        else
        {
            return 0;
        }
    }
    return 1;
}

// ---- DOME (a city that can be destroyed - explosion anim + revive state) ----
struct TmisDome
{
    int x, y, active, frame;
};

TmisDome[6] tmisDome;

void tmisDomeInit( TmisDome* d, int x, int y, int active )
{
    d->x = x;
    d->y = y;
    d->active = active;
    d->frame = 0;
}

int tmisDomeProgressAnim( TmisDome* d )
{
    if( d->frame < 6 )
    {
        d->frame++;
    }
    else
    {
        d->frame = 0;
        d->active = 0;
    }
    return d->frame;
}

// ---- INTERCEPT (an interceptor's own detonation-flash animation) ----
struct TmisIntercept
{
    int x, y, active, frame;
};

TmisIntercept[3] tmisIntercept;

// ---- MISSILE (an incoming diagonal line-missile) ----
struct TmisMissile
{
    int startX, posX, posY, endX, yPass, yDeca, track, active;
};

TmisMissile[4] tmisMissile;

// ---- DEFENCE (a fired interceptor rocket travelling toward its target) ----
struct TmisDefence
{
    int x, y, active;
    float xf, yf, xcf, ycf;
    int count;
};

TmisDefence[3] tmisDefence;

// ---- CROSS (the player's crosshair) ----
int tmisCrossX, tmisCrossY, tmisCrossActive;

// -----------------------------------------------------------------------------
//   Data tables (extracted from PIC_TMISSILE.h - see extract_tinymissile.js)
// -----------------------------------------------------------------------------

int[6] tmisDomeOrder =
{
0x02,0x11,0x20,0x51,0x60,0x6F,
};

int[107] tmisDomeSprite =
{
0x0F,0x01,0x00,0x00,0x08,0x1C,0x1E,0x3E,0x3F,0x2F,0x3D,0x36,0x1A,0x14,0x08,0x00,0x00,0x00,0x00,0x08,
0x1C,0x1E,0x3E,0x3E,0x2C,0x3C,0x36,0x1A,0x14,0x08,0x00,0x00,0x00,0x00,0x08,0x1C,0x1E,0x38,0x30,0x20,
0x38,0x30,0x1A,0x14,0x08,0x00,0x00,0x00,0x00,0x0A,0x1D,0x11,0x20,0x21,0x00,0x21,0x20,0x11,0x15,0x0A,
0x00,0x00,0x00,0x00,0x06,0x11,0x01,0x21,0x01,0x02,0x00,0x21,0x01,0x11,0x06,0x00,0x00,0x00,0x00,0x00,
0x00,0x01,0x00,0x03,0x28,0x03,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x28,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

int[4] tmisRocket =
{
0x02,0x01,0x03,0x03,
};

int[5] tmisCross =
{
0x03,0x01,0x02,0x07,0x02,
};

int[42] tmisPolice =
{
0x04,0x01,0xF8,0x88,0xF8,0x00,0x00,0xF8,0x00,0x00,0xE8,0xA8,0xB8,0x00,0x88,0xA8,0xF8,0x00,0x38,0x20,
0xF8,0x00,0xB8,0xA8,0xE8,0x00,0xF8,0xA8,0xE8,0x00,0x08,0xE8,0x18,0x00,0xF8,0xA8,0xF8,0x00,0xB8,0xA8,
0xF8,0x00,
};

int[142] tmisInterceptSprite =
{
0x0A,0x02,0x00,0x00,0x00,0x80,0x40,0x40,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x02,0x01,0x00,
0x00,0x00,0x00,0x00,0xC0,0x20,0x20,0x20,0x20,0xC0,0x00,0x00,0x00,0x00,0x03,0x04,0x04,0x04,0x04,0x03,
0x00,0x00,0x00,0xC0,0x20,0x10,0x10,0x10,0x10,0x20,0xC0,0x00,0x00,0x03,0x04,0x08,0x08,0x08,0x08,0x04,
0x03,0x00,0xC0,0x20,0x10,0x88,0xC8,0xC8,0x88,0x10,0x20,0xC0,0x03,0x04,0x08,0x11,0x13,0x13,0x11,0x08,
0x04,0x03,0xC0,0x20,0xD0,0xA8,0x68,0x68,0xA8,0xD0,0x20,0xC0,0x03,0x04,0x0B,0x15,0x16,0x16,0x15,0x0B,
0x04,0x03,0xE0,0xF0,0x58,0x38,0x18,0x78,0xB8,0x58,0xF0,0xE0,0x07,0x0F,0x1B,0x1D,0x1C,0x1A,0x1C,0x1B,
0x0F,0x07,0x48,0xA0,0x08,0x20,0x04,0x00,0x10,0x48,0x00,0x24,0x24,0x02,0x18,0x04,0x10,0x00,0x00,0x08,
0x01,0x22,
};

int[128] tmisTopPanel =
{
0xFE,0x07,0x03,0xEB,0x9B,0xB3,0xD3,0x9B,0xEB,0x03,0x03,0xEB,0x9B,0xB3,0xD3,0x9B,0xEB,0x03,0x03,0xEB,
0x9B,0xB3,0xD3,0x9B,0xEB,0x03,0x03,0xEB,0x9B,0xB3,0xD3,0x9B,0xEB,0x03,0x07,0xFF,0xFF,0x03,0x01,0x01,
0x01,0x99,0x7D,0x99,0x01,0x01,0x01,0x99,0x7D,0x99,0x01,0x01,0x01,0x99,0x7D,0x99,0x01,0x01,0x01,0x99,
0x7D,0x99,0x01,0x01,0x01,0x99,0x7D,0x99,0x01,0x01,0x01,0x99,0x7D,0x99,0x01,0x01,0x01,0x99,0x7D,0x99,
0x01,0x01,0x01,0x99,0x7D,0x99,0x01,0x01,0x01,0x99,0x7D,0x99,0x01,0x01,0x01,0x99,0x7D,0x99,0x01,0x01,
0x01,0x03,0xFF,0xFF,0x07,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,
0x03,0x03,0x03,0x03,0x03,0x03,0x07,0xFE,
};

int[698] tmisIntro =
{
0x57,0x08,0x00,0xC0,0xFE,0xB6,0xDE,0xFA,0x6E,0xFA,0xD2,0x06,0x02,0x0A,0xC6,0x22,0xCA,0x26,0x96,0xC6,
0x4E,0x16,
0x44,0x04,0x0C,0x88,0x18,0x10,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0xF8,0xB8,0x38,0xB8,0xF8,
0x58,0xF8,0x78,0xF8,0xF8,0xF8,0x38,0xF8,0x38,0xF8,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0xF0,0x10,0x18,0x88,0x0C,0x04,0x44,0x16,0x4E,0xC6,0x96,0x26,0xCA,0x22,0xC6,0x0A,0x02,0x06,0xD2,0xFA,
0x6E,0xFA,0xDE,0xB6,0xFE,0xC0,0x00,0xF0,0xDF,0x36,0xCB,0x12,0x6D,0x1B,0xEF,0xFF,0x00,0x90,0x01,0x90,
0x41,0x80,0x41,0x10,0x81,0x10,0x91,0x00,0x20,0x02,0x00,0x00,0x00,0x7F,0xE0,0x00,0xF0,0xF8,0x18,0xB8,
0x7C,0xBF,0x1F,0xFF,0x28,0xFF,0x1F,0x58,0x5F,0xF8,0x1E,0x58,0x5F,0xFF,0x28,0xFF,0x1F,0xFF,0xFC,0xF8,
0x18,0x58,0xD8,0xF8,0xF0,0x00,0xE0,0x7F,0x00,0x00,0x00,0x02,0x20,0x00,0x91,0x10,0x81,0x10,0x41,0x80,
0x41,0x90,0x01,0x90,0x00,0xFF,0xEF,0x1B,0x6D,0x12,0xCB,0x36,0xDF,0xF0,0xFF,0xBB,0xD5,0xAA,0x90,0x6A,
0xA9,0xF6,0xFD,0xFF,0x00,0x0D,0xB2,0x5D,0xA2,0xDD,0x22,0xDD,0xA2,0x5C,0x22,0x48,0x21,0x84,0x10,0x00,
0x21,0xFF,0x80,0x87,0x0F,0x0C,0x0F,0x0F,0x0F,0x0C,0x0F,0x0C,0x0F,0x0D,0x0D,0x0C,0x0F,0x0D,0x0D,0x0C,
0x0F,0x0C,0x0F,0x0C,0x0D,0x0D,0x0F,0x0C,0x0D,0x0D,0x0F,0x87,0x80,0xFF,0x21,0x00,0x10,0x84,0x21,0x48,
0x22,0x5C,0xA2,0xDD,0x22,0xDD,0xA2,0x5D,0xB2,0x0D,0x00,0xFF,0xFD,0xF6,0xA9,0x6A,0x90,0xAA,0xD5,0xBB,
0xFF,0xFF,0xFF,0x00,0x83,0x83,0x66,0xB5,0x0F,0x0B,0xB7,0x1C,0x7E,0xBC,0xF0,0xE4,0x18,0x80,0x12,0x00,
0x20,0x0A,0x08,0x06,0x20,0x06,0x00,0x22,0x04,0x80,0x01,0x01,0x03,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x03,0x01,0x01,0x80,
0x04,0x22,0x00,0x06,0x20,0x06,0x08,0x0A,0x20,0x00,0x12,0x80,0x18,0xE4,0xF0,0xBC,0x7E,0x1C,0xB7,0x0B,
0x0F,0xB5,0x66,0x83,0x83,0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x80,0x10,0xA3,0x7A,0x27,0x00,0x4B,0x95,0x0E,
0xF5,0x5F,0x07,0x00,0x00,0x00,0x10,0x01,0x00,0x20,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0xE0,0x1F,0x01,
0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x48,0x90,0x20,0x40,0x20,0x90,0x48,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x01,0x01,0x1F,0xE0,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x20,0x00,0x01,0x10,0x00,0x00,0x00,
0x07,0x5F,0xF5,0x0E,0x95,0x4B,0x00,0x27,0x7A,0xA3,0x10,0x80,0x00,0xFF,0xFF,0xFF,0xDA,0x60,0x86,0xD0,
0x02,0x78,0x95,0x02,0xA8,0x32,0xC8,0xFF,0x00,0x80,0x00,0x00,0x00,0x00,0x01,0x04,0x00,0x00,0x02,0x00,
0x00,0x00,0x00,0xE0,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x04,0x09,0x12,0x09,
0x04,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0xE0,0x00,0x00,0x00,0x00,0x02,0x00,
0x00,0x04,0x01,0x00,0x00,0x00,0x00,0x80,0x00,0xFF,0xC8,0x32,0xA8,0x02,0x95,0x78,0x02,0xD0,0x86,0x60,
0xDA,0xFF,0x00,0x01,0x02,0x06,0x09,0x17,0x2A,0x5D,0xAB,0x7C,0xA4,0xCA,0xFF,0x00,0x00,0x08,0x00,0x40,
0x00,0x04,0x80,0x80,0x08,0x40,0x10,0x20,0x20,0x10,0x0F,0x00,0x00,0x00,0x00,0x1F,0x00,0x1F,0x02,0x84,
0x9F,0x00,0x12,0x95,0x89,0x00,0x1F,0x95,0x11,0x80,0x1F,0x05,0x9A,0x01,0x1F,0x01,0x00,0x00,0x00,0x00,
0x0F,0x10,0x20,0x20,0x10,0x40,0x08,0x80,0x80,0x04,0x00,0x40,0x00,0x08,0x00,0x00,0xFF,0xCA,0xA4,0x7C,
0xAB,0x5D,0x2A,0x17,0x09,0x06,0x02,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,
0x07,0x0B,0x00,0x06,0x00,0x02,0x02,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x03,0x04,0x04,0x00,0x03,0x04,0x04,0x03,0x00,0x07,0x00,0x07,0x01,0x02,0x07,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x02,0x02,
0x00,0x06,0x00,0x0B,0x07,0x02,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

int[128] tmisY1 =
{
0xF1,0xFB,0x3E,0x0E,0x0E,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,
0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x07,0x07,0x07,0x07,0x06,0x06,
0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,
0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,
0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,
0x06,0x07,0x07,0x07,0x07,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,
0x06,0x06,0x06,0x0E,0x0E,0x3E,0xFB,0xF1,
};

int[128] tmisCenter =
{
0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,
};

int[128] tmisY6 =
{
0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0xF0,0xF0,0xF8,
0xF8,0xFC,0xE4,0xC0,0xC0,0xE4,0xFC,0xE8,0xF8,0xD0,0xB0,0x60,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,
};

int[128] tmisY7 =
{
0x3F,0x7F,0xFC,0xF0,0xE0,0xC0,0xC0,0x80,0x80,0x80,0x80,0x80,0xC0,0xC0,0xE0,0xF0,0xF8,0xF8,0xF0,0xE0,
0xC0,0xC0,0x80,0x80,0x80,0x80,0x80,0xC0,0xC0,0xE0,0xF0,0xF8,0xF8,0xF0,0xE0,0xC0,0xC0,0x80,0x80,0x80,
0x80,0x80,0xC0,0xC0,0xE0,0xF0,0xF8,0x98,0x8C,0xA4,0x92,0xCA,0xEC,0xE6,0xF7,0xF3,0x71,0xB1,0x41,0xA3,
0x9F,0xCF,0xC7,0xE3,0xE3,0xC7,0xCF,0x9F,0xA3,0x41,0xB1,0x71,0xF3,0xF7,0xE6,0xEC,0xCA,0x92,0xA4,0x8C,
0x98,0xF8,0xF0,0xE0,0xC0,0xC0,0x80,0x80,0x80,0x80,0x80,0xC0,0xC0,0xE0,0xF0,0xF8,0xF8,0xF0,0xE0,0xC0,
0xC0,0x80,0x80,0x80,0x80,0x80,0xC0,0xC0,0xE0,0xF0,0xF8,0xF8,0xF0,0xE0,0xC0,0xC0,0x80,0x80,0x80,0x80,
0x80,0xC0,0xC0,0xE0,0xF0,0xFC,0x7F,0x3F,
};

// -----------------------------------------------------------------------------
//   Sound (SNDBOX_TMISSILE, expressed as note tables through a shared
//   frame-stepped sequencer - see header comment)
// -----------------------------------------------------------------------------

int[4] tmisSnd0Notes = { 100, 250, 20, 250 };
int[4] tmisSnd3Notes = { 200, 140, 100, 140 };
int[4] tmisSnd4Notes = { 2, 140, 200, 14 };
int[4] tmisSnd5Notes = { 200, 6, 150, 6 };

// case(1)/case(2) upstream are computed sweeps, not hand-authored melodies -
// built once at init via the same loop shape upstream uses, instead of
// transcribing several hundred numbers by hand.
//
// Upstream's real step size (t+=1/t-=1, 254/190 steps) plays fast purely
// because each Sound() call there is a *blocking* AVR bit-bang that only
// takes ~1-2ms - the whole 254-step sweep finishes in under half a real
// second. This port's sequencer can only ever advance one note per real
// 60fps frame (Vircon32's Sound() is a fire-and-forget async channel, not
// a blocking call, so there's no way to "wait less than one frame" and
// still hear a distinct tone) - so a literal 1:1 step count would take a
// minimum of 508/380 real *frames* (8.5s / 6.3s) to finish regardless of
// each note's own true duration, reported directly as "plays too long."
// Fixed by downsampling the step size (t+=8/t-=8, matching the same
// overall frequency sweep with far fewer discrete steps) rather than
// literally reproducing every one of upstream's 254/190 steps - this
// keeps the audible ascending/descending sweep effect while landing in a
// real duration browser-comparable to Arkanoid's own ~46-note intro
// jingle, instead of an upstream step count that was only ever fast
// because of AVR's blocking audio model.
// 32 iterations x 2 notes(freq,dur each) = 64 notes = 128 ints; 24
// iterations x 2 notes = 48 notes = 96 ints.
int[128] tmisWinLevelNotes;
int[96] tmisGameOverNotes;

void tmisBuildSoundTables()
{
    int i, t;

    i = 0;
    for( t = 1; t < 255; t += 8 )
    {
        tmisWinLevelNotes[ i ] = 50; i++;
        tmisWinLevelNotes[ i ] = 2; i++;
        tmisWinLevelNotes[ i ] = t; i++;
        tmisWinLevelNotes[ i ] = 2; i++;
    }

    i = 0;
    for( t = 200; t > 10; t -= 8 )
    {
        tmisGameOverNotes[ i ] = 200 - t; i++;
        tmisGameOverNotes[ i ] = 3; i++;
        tmisGameOverNotes[ i ] = t; i++;
        tmisGameOverNotes[ i ] = 12; i++;
    }
}

int* tmisNoteTable;
int tmisNoteTableLen;
int tmisNoteIndex;
int tmisNoteWaitFrames;
int tmisNoteSeqActive;

void tmisStartNoteSeq( int* table, int lengthValues )
{
    tmisNoteTable = table;
    tmisNoteTableLen = lengthValues;
    tmisNoteIndex = 0;
    tmisNoteWaitFrames = 0;
    tmisNoteSeqActive = true;
}

bool tmisAdvanceNoteSeq()
{
    if( !tmisNoteSeqActive )
      return true;

    if( tmisNoteWaitFrames > 0 )
    {
        tmisNoteWaitFrames--;
        return false;
    }

    if( tmisNoteIndex >= tmisNoteTableLen )
    {
        tmisNoteSeqActive = false;
        return true;
    }

    int freq = tmisNoteTable[ tmisNoteIndex ];
    int dur = tmisNoteTable[ tmisNoteIndex + 1 ];

    Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 )
      periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    // No 1-frame minimum floor here (unlike Arkanoid's/Minez's own copy of
    // this sequencer) - several of this game's own short UI blips
    // (case 3/4/5, ~1-2ms true duration each) are well under one 60fps
    // frame's worth of real time, and forcing a minimum wait would
    // artificially stretch every one of them out to at least 16.67ms -
    // audibly "too long" once several such notes chain together, exactly
    // what got reported. 0 is safe here: this function only ever advances
    // one note per real call regardless, so a 0 wait just means "don't
    // insert an extra idle frame that wasn't earned," not "play sub-frame."
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 0 )
      waitFrames = 0;
    tmisNoteWaitFrames = waitFrames;

    tmisNoteIndex += 2;
    return false;
}

void tmisSndBox( int snd )
{
    if( snd == 0 )
      tmisStartNoteSeq( tmisSnd0Notes, 4 );
    else if( snd == 1 )
      tmisStartNoteSeq( tmisWinLevelNotes, 128 );
    else if( snd == 2 )
      tmisStartNoteSeq( tmisGameOverNotes, 96 );
    else if( snd == 3 )
      tmisStartNoteSeq( tmisSnd3Notes, 4 );
    else if( snd == 4 )
      tmisStartNoteSeq( tmisSnd4Notes, 4 );
    else if( snd == 5 )
      tmisStartNoteSeq( tmisSnd5Notes, 4 );
}

// -----------------------------------------------------------------------------
//   Small drawing/math helpers (ELECTROLIB.cpp)
// -----------------------------------------------------------------------------

int tmisMymap( int x, int inMin, int inMax, int outMin, int outMax )
{
    return ( ( x - inMin ) * ( outMax - outMin ) / ( inMax - inMin ) ) + outMin;
}

// Confirmed safe against negative operands - see header comment.
int tmisRecupeLineY( int valeur )
{
    return valeur >> 3;
}

int tmisRecupeDecalageY( int valeur )
{
    return valeur - ( ( valeur >> 3 ) << 3 );
}

int tmisSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown )
      return input << decalage;
    return input >> ( 8 - decalage );
}

// xPos/yPos: sprite's own top-left position (yPos may straddle a page
// boundary - the sprite is split across two pages via the decalage shift).
int tmisBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = tmisRecupeLineY( yPos );

    if( ( xPass > ( xPos + ( wSprite - 1 ) ) ) || ( xPass < xPos ) ||
        ( ( recupeLineY > yPass ) || ( ( recupeLineY + hSprite ) < yPass ) ) )
      return 0;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = tmisRecupeDecalageY( yPos );
    int scanA = ( ( xPass - xPos ) + ( spriteYLine * wSprite ) ) + 2;
    int scanB = ( ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) ) + 2;
    int outByte;

    if( scanA > wMax )
      outByte = 0;
    else
      outByte = tmisSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = tmisSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax )
          return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

// Page-aligned sprite lookup (no sub-page splitting) - dome/intercept/font.
int tmisSpeedBlitz( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];

    if( ( xPass > ( xPos + ( wSprite - 1 ) ) ) || ( xPass < xPos ) ||
        ( ( yPass < yPos ) || ( yPass > ( yPos + ( hSprite - 1 ) ) ) ) )
      return 0;

    return sprites[ 2 + ( ( xPass - xPos ) + ( ( yPass - yPos ) * wSprite ) ) + ( frame * ( hSprite * wSprite ) ) ];
}

int tmisUniversalAbs( int v )
{
    if( v < 0 )
      return -v;
    return v;
}

int tmisReturnFullByte( int x1, int y1, int x2, int y2, int xPass, int yPass )
{
    int byteAdd = 0;
    int t, tout;
    int a, b;
    int swap;

    a = yPass << 3;
    b = a + 7;
    if( y1 > y2 )
    {
        swap = y1; y1 = y2; y2 = swap;
        swap = x1; x1 = x2; x2 = swap;
    }
    if( a < y1 ) a = y1;
    if( b > y2 ) b = y2;

    for( t = a; t < b + 1; t++ )
    {
        if( yPass == tmisRecupeLineY( t ) )
        {
            tout = tmisMymap( t, y1, y2, x1, x2 );
            if( tout == xPass )
              byteAdd |= ( 1 << ( tmisRecupeDecalageY( t ) ) );
        }
    }
    return byteAdd;
}

int tmisDirectionLine( int desactive, int x1, int y1, int x2, int y2, int xPass, int yPass )
{
    int xl = tmisUniversalAbs( x1 - x2 );
    int yl = tmisUniversalAbs( y1 - y2 );

    if( ( xl < yl ) && ( desactive == 0 ) )
      return tmisReturnFullByte( x1, y1, x2, y2, xPass, yPass );

    int resultant = tmisMymap( xPass, x1, x2, y1, y2 );
    int yref = tmisRecupeLineY( resultant );
    if( yref == yPass )
      return ( 1 << ( tmisRecupeDecalageY( resultant ) ) );
    return 0;
}

int tmisTraceLine( int desactive, int x1, int y1, int x2, int y2, int xPass, int yPass )
{
    int y1r = tmisRecupeLineY( y1 );
    int y2r = tmisRecupeLineY( y2 );

    if( ( xPass < x1 ) && ( xPass < x2 ) ) return 0;
    if( ( xPass > x1 ) && ( xPass > x2 ) ) return 0;

    if( y1r < y2r )
    {
        if( yPass < y1r ) return 0;
        if( yPass > y2r ) return 0;
    }
    else
    {
        if( yPass > y1r ) return 0;
        if( yPass < y2r ) return 0;
    }
    return tmisDirectionLine( desactive, x1, y1, x2, y2, xPass, yPass );
}

// -----------------------------------------------------------------------------
//   CROSS (crosshair)
// -----------------------------------------------------------------------------

void tmisCrossLimitR() { if( tmisCrossX > 123 ) tmisCrossX = 123; }
void tmisCrossLimitL() { if( tmisCrossX < 2 ) tmisCrossX = 2; }
void tmisCrossLimitU() { if( tmisCrossY < 14 ) tmisCrossY = 14; }
void tmisCrossLimitD() { if( tmisCrossY > 48 ) tmisCrossY = 48; }

void tmisCrossInitPos()
{
    tmisCrossX = 62;
    tmisCrossY = 30;
    tmisCrossActive = 1;
}

void tmisCrossRight() { tmisCrossX = tmisCrossX + 3; tmisCrossLimitR(); }
void tmisCrossLeft()  { tmisCrossX = tmisCrossX - 3; tmisCrossLimitL(); }
void tmisCrossDown()  { tmisCrossY = tmisCrossY + 2; tmisCrossLimitD(); }
void tmisCrossUp()    { tmisCrossY = tmisCrossY - 2; tmisCrossLimitU(); }

// -----------------------------------------------------------------------------
//   DEFENCE (a fired interceptor rocket travelling toward its target)
// -----------------------------------------------------------------------------

void tmisDefenceInit( TmisDefence* f )
{
    f->x = 0;
    f->y = 0;
    f->count = 0;
    f->active = 0;
    f->xcf = 0.0;
    f->ycf = 0.0;
}

void tmisCreateNewIntercept( int x, int y );

void tmisDefenceUpdate( TmisDefence* f )
{
    if( f->count > 0 )
    {
        f->xcf = f->xcf + f->xf;
        f->ycf = f->ycf + f->yf;
        f->x = (int)f->xcf;
        f->y = (int)f->ycf;
        f->count--;
    }
    else
    {
        f->active = 0;
        tmisCreateNewIntercept( f->x - 3, f->y - 6 );
    }
}

void tmisDefenceNew( TmisDefence* f, int xEnd, int yEnd )
{
    float tXF, tYF;

    f->xcf = 63.0;
    f->ycf = 51.0;
    f->xf = (float)xEnd - f->xcf;
    f->yf = (float)yEnd - f->ycf;
    tXF = f->xf; if( tXF < 0.0 ) tXF = -tXF;
    tYF = f->yf; if( tYF < 0.0 ) tYF = -tYF;
    if( tXF >= tYF )
      f->count = (int)( tXF / 3.0 );
    else
      f->count = (int)( tYF / 3.0 );
    f->xf = f->xf / (float)f->count;
    f->yf = f->yf / (float)f->count;
    f->active = 1;
    f->x = (int)f->xcf;
    f->y = (int)f->ycf;
}

// -----------------------------------------------------------------------------
//   Level setup / new game
// -----------------------------------------------------------------------------

void tmisResetAllMissile();
void tmisRdlpMix();
void tmisStartRdlpMix();

void tmisAdjLevel( int level )
{
    int t;

    tmisResetAllMissile();
    for( t = 0; t < TMIS_NUM_DEFENCE; t++ )
    {
        tmisDefenceInit( &tmisDefence[t] );
        tmisIntercept[t].x = 0;
        tmisIntercept[t].y = 0;
        tmisIntercept[t].active = 0;
        tmisIntercept[t].frame = 0;
    }
    tmisArmyResetWeapon( level );
    tmisShotAdj = tmisMymap( level, 0, 10, 20, 60 );
    tmisClkInit( &tmisSpeedMissile, 0, tmisMymap( level, 0, 10, 2, 0 ), 0 );
    tmisClkInit( &tmisRenew, 0, tmisMymap( level, 0, 10, 6, 12 ), 0 );
    tmisCrossInitPos();
}

void tmisNewGame()
{
    int t;

    tmisReverse = 0;
    tmisM10000 = 0; tmisM1000 = 0; tmisM100 = 0; tmisM10 = 0; tmisM1 = 0;
    tmisScores = 0;
    tmisLevel = 0;
    for( t = 0; t < TMIS_NUM_DOME; t++ )
      tmisDomeInit( &tmisDome[t], tmisDomeOrder[t], 7, 1 );
}

void tmisRestoreDome()
{
    int t;
    for( t = 0; t < TMIS_NUM_DOME; t++ )
      if( tmisDome[t].active == 2 )
        tmisDome[t].active = 1;
}

void tmisNextLevel()
{
    if( tmisLevel < 10 )
      tmisLevel = tmisLevel + 1;
    else
      tmisLevel = 10;
}

void tmisRefreshTopbar() { }  // partial-redraw call upstream - no-op here, full redraw every frame instead

void tmisCompilSco()
{
    tmisM10000 = tmisScores / 10000;
    tmisM1000 = ( tmisScores - ( tmisM10000 * 10000 ) ) / 1000;
    tmisM100 = ( tmisScores - ( tmisM1000 * 1000 ) - ( tmisM10000 * 10000 ) ) / 100;
    tmisM10 = ( tmisScores - ( tmisM100 * 100 ) - ( tmisM1000 * 1000 ) - ( tmisM10000 * 10000 ) ) / 10;
    tmisM1 = tmisScores - ( tmisM10 * 10 ) - ( tmisM100 * 100 ) - ( tmisM1000 * 1000 ) - ( tmisM10000 * 10000 );
}

void tmisIncScores()
{
    tmisScores += 13;
    tmisCompilSco();
    tmisRefreshTopbar();
}

// -----------------------------------------------------------------------------
//   Missiles (LINE_TMISSILE)
// -----------------------------------------------------------------------------

void tmisNewPos( int* stOut, int* endOut )
{
    int a, b;
    a = tmisStartRdlp;
    if( tmisReverse == 1 )
    {
        if( ( a + tmisRdlp ) > 126 )
          b = a - tmisRdlp;
        else
          b = a + tmisRdlp;
    }
    else
    {
        if( ( a - tmisRdlp ) < 3 )
          b = a + tmisRdlp;
        else
          b = a - tmisRdlp;
    }
    *stOut = a;
    *endOut = b;
}

void tmisStartRdlpMix()
{
    if( tmisStartRdlp > 3 )
      tmisStartRdlp = tmisStartRdlp - 3;
    else
      tmisStartRdlp = 125 - tmisStartRdlp;
}

void tmisRdlpMix()
{
    if( tmisRdlp < 60 )
      tmisRdlp = tmisRdlp + 3;
    else
      tmisRdlp = 22;
}

void tmisMissileInit( TmisMissile* m, int startX, int endX, int active )
{
    m->startX = startX;
    m->posX = startX;
    m->posY = 11;
    m->endX = endX;
    m->active = active;
    m->track = m->posY;
    m->yPass = 0;
    m->yDeca = 0;
}

void tmisResetAllMissile()
{
    int t, a, b;
    for( t = 0; t < TMIS_NUM_MISSILE; t++ )
    {
        tmisRdlpMix();
        tmisStartRdlpMix();
        tmisNewPos( &a, &b );
        tmisMissileInit( &tmisMissile[t], a, b, 0 );
    }
}

void tmisRenewMissile()
{
    int t, a, b;
    tmisRdlpMix();
    tmisStartRdlpMix();
    tmisNewPos( &a, &b );
    for( t = 0; t < TMIS_NUM_MISSILE; t++ )
    {
        if( tmisMissile[t].active == 0 )
        {
            if( tmisReverse == 1 )
              tmisReverse = 0;
            else
              tmisReverse = 1;
            tmisMissileInit( &tmisMissile[t], a, b, 1 );
            return;
        }
    }
}

void tmisAttackWeaponStart();

void tmisDomeCollision( TmisMissile* m )
{
    int t;
    if( ( m->endX > 54 ) && ( m->endX < 73 ) )
      tmisAttackWeaponStart();

    for( t = 0; t < TMIS_NUM_DOME; t++ )
    {
        if( tmisDome[t].active )
        {
            if( ( m->endX > tmisDome[t].x ) && ( m->endX < ( tmisDome[t].x + 14 ) ) )
            {
                tmisDome[t].frame = 1;
                return;
            }
        }
    }
}

// Returns true (level cleared: no active missile survived a speed-tick).
int tmisMissileProgress( TmisMissile* m )
{
    if( m->track < 55 )
    {
        m->track++;
    }
    else
    {
        tmisDomeCollision( m );
        m->active = 0;
        return 0;
    }
    m->yPass = tmisRecupeLineY( m->track );
    m->yDeca = ( 0xFF >> ( 7 - tmisRecupeDecalageY( m->track ) ) );
    return 0;
}

void tmisDestroy( int interceptIndex )
{
    int t;
    for( t = 0; t < TMIS_NUM_MISSILE; t++ )
    {
        if( tmisMissile[t].active )
        {
            if( ( tmisIntercept[ interceptIndex ].y + 15 ) < tmisMissile[t].track ) continue;
            if( tmisIntercept[ interceptIndex ].y > tmisMissile[t].track ) continue;

            int xPos = tmisMymap( tmisMissile[t].track, 11, 55, tmisMissile[t].startX, tmisMissile[t].endX );
            if( xPos < tmisIntercept[ interceptIndex ].x ) continue;
            if( xPos > ( tmisIntercept[ interceptIndex ].x + 9 ) ) continue;

            tmisMissile[t].active = 0;
            tmisIncScores();
        }
    }
}

void tmisCreateNewIntercept( int x, int y )
{
    int t;
    for( t = 0; t < TMIS_NUM_INTERCEPT; t++ )
    {
        if( tmisIntercept[t].active == 0 )
        {
            tmisIntercept[t].x = x;
            tmisIntercept[t].y = y;
            tmisIntercept[t].active = 1;
            tmisIntercept[t].frame = 0;
            return;
        }
    }
}

void tmisRenewShield( int xEnd, int yEnd )
{
    int t;
    for( t = 0; t < TMIS_NUM_DEFENCE; t++ )
    {
        if( tmisDefence[t].active == 0 )
        {
            if( tmisArmyUseWeapon() )
            {
                tmisRefreshTopbar();
                tmisDefenceNew( &tmisDefence[t], xEnd, yEnd );
            }
            return;
        }
    }
}

// -----------------------------------------------------------------------------
//   Per-frame engine update
// -----------------------------------------------------------------------------

void tmisUpdateDome()
{
    int refresh = 0;
    int total = 0;
    int t;
    for( t = 0; t < TMIS_NUM_DOME; t++ )
    {
        if( tmisDome[t].active )
        {
            if( tmisDome[t].frame > 0 )
            {
                // Upstream calls SNDBOX(5) on every one of the 6
                // explosion-animation ticks, but each of its own calls is a
                // blocking ~2ms beep - basically inaudible overlap on real
                // hardware. Vircon32's Sound() is a real async channel with
                // no such blocking, so retriggering it every tick for a
                // single dome's whole explosion instead produced an
                // audible, stuck-sounding buzz (reported directly) - fixed
                // by sounding once, on the tick the explosion actually
                // starts, matching what a player perceives as "one boom."
                if( tmisDome[t].frame == 1 )
                  tmisSndBox( 5 );
                tmisDomeProgressAnim( &tmisDome[t] );
                refresh = 1;
            }
            total++;
        }
    }
    if( total == 0 )
      tmisShotAdj = 0;
}

void tmisUpdateDfence()
{
    int t;
    for( t = 0; t < TMIS_NUM_DEFENCE; t++ )
      if( tmisDefence[t].active == 1 )
        tmisDefenceUpdate( &tmisDefence[t] );
}

int tmisDomeProgressAnimIntercept( int t )
{
    if( tmisIntercept[t].frame < 6 )
    {
        tmisIntercept[t].frame++;
    }
    else
    {
        tmisIntercept[t].frame = 0;
        tmisIntercept[t].active = 0;
    }
    return tmisIntercept[t].frame;
}

void tmisUpdateIntercept()
{
    int t;
    for( t = 0; t < TMIS_NUM_INTERCEPT; t++ )
    {
        if( tmisIntercept[t].active )
        {
            int frame = tmisDomeProgressAnimIntercept( t );
            if( frame == 4 )
              tmisDestroy( t );
        }
    }
}

int tmisAllAnimEnd()
{
    int t;
    for( t = 0; t < TMIS_NUM_DOME; t++ )
      if( tmisDome[t].frame != 0 )
        return 0;
    return 1;
}

// Returns true once the whole level's missiles have been dealt with.
int tmisUpdateEngine()
{
    int checkIfExist = 255;
    int t;

    tmisUpdateDome();
    tmisUpdateDfence();
    tmisUpdateIntercept();

    if( tmisClkProgress( &tmisSpeedMissile ) )
    {
        checkIfExist = 0;
        for( t = 0; t < TMIS_NUM_MISSILE; t++ )
        {
            if( tmisMissile[t].active )
            {
                checkIfExist++;
                tmisStartRdlpMix();
                tmisMissileProgress( &tmisMissile[t] );
            }
        }
        if( tmisClkProgress( &tmisRenew ) )
        {
            if( tmisShotAdj )
            {
                tmisRenewMissile();
                if( tmisShotAdj > 0 )
                  tmisShotAdj = tmisShotAdj - 1;
                else
                  tmisShotAdj = 0;
                tmisClkReset( &tmisRenew );
            }
        }
        tmisClkReset( &tmisSpeedMissile );
    }

    if( checkIfExist == 0 )
    {
        if( tmisShotAdj == 0 )
        {
            for( t = 0; t < TMIS_NUM_DEFENCE; t++ )
            {
                tmisDefence[t].active = 0;
                tmisIntercept[t].active = 0;
            }
            return 1;
        }
    }
    return 0;
}

// ARMY_TMISSILE::ATTACK_WEAPON()'s blocking burst - a missile got through to
// the crosshair, auto-fire every remaining rocket in rapid succession. Ported
// as a non-blocking burst flag instead (one rocket per real frame), with the
// main engine update skipped entirely while it's active - see header comment.
//
// Upstream has two genuinely different outcomes depending on the clip state
// at the *moment the burst starts* (not re-checked every tick): if the
// current clip already had rockets in it, USE_WEAPON()'s own auto-refill
// (pulling a fresh 9-round clip from SPARE once ROCKET hits 0) keeps
// upstream's `while(ROCKET>0)` loop going, so the burst drains the *entire*
// arsenal - current clip plus every spare clip - one continuous barrage. If
// the clip was already empty when the burst started, upstream skips that
// loop entirely and only fires one single defensive shot straight from
// SPARE, no refill. `tmisAttackBurstHadRocket` captures that one-time
// decision; without it, a naive per-tick `if(rocket>0)` check (an earlier
// version of this port) would incorrectly stop the barrage the instant the
// current clip ran dry instead of continuing into the spare clips, and
// would use the wrong "single shot" sound for what should be a continuing
// barrage.
int tmisAttackBurstHadRocket;

void tmisAttackWeaponStart()
{
    tmisAttackBurstActive = 1;
    tmisAttackBurstHadRocket = ( tmisArmyRocket > 0 );
}

// Returns true while the burst is still going (caller should skip the
// normal engine update this frame).
int tmisAttackWeaponStep()
{
    if( tmisAttackBurstHadRocket )
    {
        if( tmisArmyUseWeapon() )
        {
            tmisSndBox( 5 );
            return 1;
        }
        tmisAttackBurstActive = 0;
        return 0;
    }

    if( tmisArmySpare > 0 )
    {
        tmisArmySpare--;
        tmisSndBox( 3 );
    }
    tmisAttackBurstActive = 0;
    return 0;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

// This game's render pipeline never got the per-row call-site gating pass
// every other tinyJoypadShim game here already needed (Bert/Doc/Tris/
// Arkanoid) - 7 composited layers called unconditionally for all 1024
// pixels/frame, several looping over multiple objects internally (4
// missiles, 6 domes, 3 defence, 3 intercepts), pushed real frames over the
// CPU's 250,000-cycle/frame budget consistently (reported directly as a
// constant 100% CPU load, plus the crosshair/domes occasionally missing
// from an otherwise-normal frame when the truncation happened to land on
// them - game *state* was never affected, only what got drawn that one
// frame, since all logic runs before this function each frame).
//
// A first pass just row-gated the call *site* for Dome/Cross/Shield/
// Intercept/Panel (skip the whole call on rows they can't touch) - a real
// improvement, but each object still looped over all 128 columns *within*
// its own gated rows. This pass goes further: each of those layers is
// composited into a shared `tmisPageBuffer[128]` by walking only its own
// small set of active objects and writing *just their own narrow column
// range* (dome width 15, cross width 3, defence width 2, intercept width
// 10) instead of scanning all 128 columns per object - the same per-page-
// buffer technique Bomber/Pacman already use for their own sprite lists.
// Missiles get the same treatment: each one's own x1/x2 span (set by
// NEW_POS_TMISSILE from the RDLP oscillator, which only ever cycles
// roughly 22-60px apart) is usually much narrower than the full 128-column
// width, so bounding the inner loop to [min(x1,x2),max(x1,x2)] skips real
// work instead of just the cheap early-return Trace_LINE already had for
// genuinely out-of-range columns - and their trail only ever spans rows
// 1-6 (starts at pixel-y=11, ends at pixel-y=55 - rows 0 and 7 are *never*
// reachable, a static fact, not just a per-frame check), with a row
// skipped entirely unless at least one active missile's trail has grown
// that far down yet.
int[128] tmisPageBuffer;

// tmisCompositeLineRow() used to scan a missile's *entire* trail width
// (up to ~60 columns, its full [xs,xe] span) on every one of its active
// rows, relying on tmisTraceLine()'s own internal check to reject columns
// that don't belong to this row - but per this project's own established
// "even a correctly self-gated function still costs a full call every
// time it's invoked" lesson, that rejection still pays a full function
// call each time. Reported directly as the game slowing down "when
// missiles reach near our bases" - which is exactly when this cost peaks:
// a missile's trail only reaches its full length (all 6 rows active at
// once) once it's nearly at the bottom, so the wasted-call count grows
// specifically as missiles approach the domes/crosshair, matching the
// report precisely.
//
// The line's geometry is fixed (y1=11, y2=55 always) and strictly linear,
// so for a given page row the sub-range of columns that can possibly
// match is computable directly instead of discovered by scanning: map
// both pixel-Y extremes of this row (intersected with [11,55]) back to X
// via the same tmisMymap() formula tmisDirectionLine() already uses (just
// with X and Y swapped), taking the min/max of the two results (the
// mapping is monotonic, so this is guaranteed to bracket every truly-
// matching column) with a 1px safety margin against integer-division
// rounding differences between this inverse computation and the forward
// one inside tmisTraceLine(). Given the trail's dx/dy ratio (~0.5-1.4,
// from the 22-60px-wide RDLP range over a fixed 44px height), one 8px-tall
// page row's real matching columns typically number only a handful - a
// large cut from scanning the full trail width every time, with the
// output set of drawn pixels unchanged (same underlying math, just fewer
// wasted calls that would've returned 0 anyway).
void tmisMissileRowXRange( int x1, int x2, int y, int xs, int xe, int* outXs, int* outXe )
{
    int yLo = y * 8;
    int yHi = yLo + 7;
    if( yLo < 11 ) yLo = 11;
    if( yHi > 55 ) yHi = 55;

    int xAtLo = tmisMymap( yLo, 11, 55, x1, x2 );
    int xAtHi = tmisMymap( yHi, 11, 55, x1, x2 );
    int rowXs, rowXe;
    if( xAtLo < xAtHi ) { rowXs = xAtLo; rowXe = xAtHi; }
    else { rowXs = xAtHi; rowXe = xAtLo; }

    rowXs -= 1;
    rowXe += 1;
    if( rowXs < xs ) rowXs = xs;
    if( rowXe > xe ) rowXe = xe;

    *outXs = rowXs;
    *outXe = rowXe;
}

void tmisCompositeLineRow( int y )
{
    int t, x, xs, xe, x1, x2, tByte, rowXs, rowXe;
    for( t = 0; t < TMIS_NUM_MISSILE; t++ )
    {
        if( tmisMissile[t].active == 1 && y <= tmisMissile[t].yPass )
        {
            x1 = tmisMissile[t].startX;
            x2 = tmisMissile[t].endX;
            if( x1 < x2 ) { xs = x1; xe = x2; }
            else { xs = x2; xe = x1; }
            if( xs < 0 ) xs = 0;
            if( xe > 127 ) xe = 127;

            tmisMissileRowXRange( x1, x2, y, xs, xe, &rowXs, &rowXe );

            for( x = rowXs; x <= rowXe; x++ )
            {
                tByte = tmisTraceLine( 1, x1, 11, x2, 55, x, y );
                if( tmisMissile[t].yPass == y )
                  tmisPageBuffer[x] |= ( tmisMissile[t].yDeca & tByte );
                else
                  tmisPageBuffer[x] |= tByte;
            }
        }
    }
}

int tmisRecupeTopbar( int xPass, int yPass )
{
    return tmisTopPanel[ xPass + ( yPass * 128 ) ];
}

int tmisRecupeScores( int xPass )
{
    if( xPass < 105 ) return 0;
    if( xPass > 125 ) return 0;
    return
    ( tmisSpeedBlitz( 106, 0, xPass, 0, tmisM10000, tmisPolice ) |
      tmisSpeedBlitz( 110, 0, xPass, 0, tmisM1000, tmisPolice ) |
      tmisSpeedBlitz( 114, 0, xPass, 0, tmisM100, tmisPolice ) |
      tmisSpeedBlitz( 118, 0, xPass, 0, tmisM10, tmisPolice ) |
      tmisSpeedBlitz( 122, 0, xPass, 0, tmisM1, tmisPolice ) );
}

int tmisInventory( int xPass )
{
    int x = ( tmisArmySpare * 8 ) + 2;
    if( xPass < x ) return 0xFF;
    return 0x03;
}

int tmisMunition( int xPass )
{
    int x = ( tmisArmyRocket * 6 ) + 40;
    if( xPass < x ) return 0xFF;
    return 0x01;
}

int tmisRecupPanel( int xPass, int yPass )
{
    if( yPass > 0 )
      return 0;
    int byte1 = tmisRecupeTopbar( xPass, yPass );
    int byte2;

    if( xPass >= 2 && xPass <= 33 )
      byte2 = byte1 & tmisInventory( xPass );
    else if( xPass >= 40 && xPass <= 99 )
      byte2 = byte1 & tmisMunition( xPass );
    else if( xPass >= 106 && xPass <= 124 )
      byte2 = byte1 | tmisRecupeScores( xPass );
    else
      return byte1;

    return byte2;
}

int tmisBackground( int xPass, int yPass )
{
    if( yPass == 0 ) return 0;
    if( yPass == 1 ) return tmisY1[ xPass ];
    if( yPass >= 2 && yPass <= 5 ) return tmisCenter[ xPass ];
    if( yPass == 6 ) return tmisY6[ xPass ];
    if( yPass == 7 ) return tmisY7[ xPass ];
    return 0;
}

void tmisCompositeDomeRow()
{
    int t, x, xs, xe;
    for( t = 0; t < TMIS_NUM_DOME; t++ )
    {
        if( tmisDome[t].active == 1 )
        {
            xs = tmisDome[t].x;
            xe = xs + 14;
            if( xs < 0 ) xs = 0;
            if( xe > 127 ) xe = 127;
            for( x = xs; x <= xe; x++ )
              tmisPageBuffer[x] |= tmisSpeedBlitz( tmisDome[t].x, tmisDome[t].y, x, 7, tmisDome[t].frame, tmisDomeSprite );
        }
    }
}

void tmisCompositeCrossRow( int y )
{
    int x, xs, xe;
    xs = tmisCrossX;
    xe = xs + 2;
    if( xs < 0 ) xs = 0;
    if( xe > 127 ) xe = 127;
    for( x = xs; x <= xe; x++ )
      tmisPageBuffer[x] |= tmisBlitzSprite( tmisCrossX, tmisCrossY, x, y, 0, tmisCross );
}

void tmisCompositeShieldRow( int y )
{
    int t, x, xs, xe;
    for( t = 0; t < TMIS_NUM_DEFENCE; t++ )
    {
        if( tmisDefence[t].active )
        {
            xs = tmisDefence[t].x;
            xe = xs + 1;
            if( xs < 0 ) xs = 0;
            if( xe > 127 ) xe = 127;
            for( x = xs; x <= xe; x++ )
              tmisPageBuffer[x] |= tmisBlitzSprite( tmisDefence[t].x, tmisDefence[t].y, x, y, 0, tmisRocket );
        }
    }
}

void tmisCompositeInterceptRow( int y )
{
    int t, x, xs, xe;
    for( t = 0; t < TMIS_NUM_INTERCEPT; t++ )
    {
        if( tmisIntercept[t].active )
        {
            xs = tmisIntercept[t].x;
            xe = xs + 9;
            if( xs < 0 ) xs = 0;
            if( xe > 127 ) xe = 127;
            for( x = xs; x <= xe; x++ )
              tmisPageBuffer[x] |= tmisBlitzSprite( tmisIntercept[t].x, tmisIntercept[t].y, x, y, tmisIntercept[t].frame, tmisInterceptSprite );
        }
    }
}

void tmisTinyFlip()
{
    md_beginFrame();
    int x, y, t;

    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
          tmisPageBuffer[x] = 0;

        int domeRow = ( y == 7 );
        int panelRow = ( y == 0 );

        int crossRow = 0;
        int crossPage = tmisRecupeLineY( tmisCrossY );
        if( y == crossPage || y == crossPage + 1 )
          crossRow = 1;

        int shieldRow = 0;
        for( t = 0; t < TMIS_NUM_DEFENCE; t++ )
        {
            if( tmisDefence[t].active )
            {
                int p = tmisRecupeLineY( tmisDefence[t].y );
                if( y == p || y == p + 1 )
                  shieldRow = 1;
            }
        }

        int interceptRow = 0;
        for( t = 0; t < TMIS_NUM_INTERCEPT; t++ )
        {
            if( tmisIntercept[t].active )
            {
                int p = tmisRecupeLineY( tmisIntercept[t].y );
                if( y >= p && y <= p + 2 )
                  interceptRow = 1;
            }
        }

        int lineRow = 0;
        if( y >= 1 && y <= 6 )
        {
            for( t = 0; t < TMIS_NUM_MISSILE; t++ )
              if( tmisMissile[t].active && tmisMissile[t].yPass >= y )
                lineRow = 1;
        }

        if( domeRow )      tmisCompositeDomeRow();
        if( crossRow )     tmisCompositeCrossRow( y );
        if( shieldRow )    tmisCompositeShieldRow( y );
        if( interceptRow ) tmisCompositeInterceptRow( y );
        if( lineRow )      tmisCompositeLineRow( y );

        for( x = 0; x < 128; x++ )
        {
            int pixel = tmisPageBuffer[x] | tmisBackground( x, y );

            if( panelRow )
              pixel |= tmisRecupPanel( x, y );

            md_drawColumn( x, y, pixel );
        }
    }
}

int tmisRecupeIntro( int xPass, int yPass, int fl )
{
    if( fl == 1 )
    {
        if( ( xPass < 68 ) && ( xPass > 60 ) )
        {
            if( yPass == 4 ) return 0;
            if( yPass == 5 ) return 0;
        }
    }
    return tmisSpeedBlitz( 21, 0, xPass, yPass, 0, tmisIntro );
}

int tmisNumeric( int xPass ) { return tmisRecupeScores( xPass ); }

void tmisIntroFlip( int fl )
{
    md_beginFrame();
    int x, y, extra;
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
        {
            if( y == 7 )
              extra = tmisNumeric( x );
            else
              extra = 0;
            md_drawColumn( x, y, tmisRecupeIntro( x, y, fl ) | extra );
        }
    }
}

// -----------------------------------------------------------------------------
//   Top-level dispatch (replaces upstream's setup()/loop())
// -----------------------------------------------------------------------------

void gameTinyMissile_init()
{
    tmisState = TMIS_STATE_TITLE;
    tmisPrevUp = false;
    tmisPrevDown = false;
    tmisPrevFire = false;
    tmisFireGateActive = false;
    tmisTickSkipCounter = 0;
    tmisOneDrop = 0;
    tmisAttackBurstActive = 0;
    tmisScores = 0;
    tmisCompilSco();
    tmisClkInit( &tmisBlink, 0, 10, 1 );
    tmisBuildSoundTables();
}

void tmisBeginLevelClear()
{
    tmisState = TMIS_STATE_LEVEL_CLEAR_PAUSE1;
    tmisPauseFrames = TMIS_PAUSE_FRAMES;
}

void gameTinyMissile_update()
{
    bool rawFire = isFirePressed();
    bool up = isUpPressed();
    bool down = isDownPressed();
    bool left = isLeftPressed();
    bool right = isRightPressed();

    bool fire = rawFire;
    if( tmisFireGateActive )
    {
        if( !rawFire )
          tmisFireGateActive = false;
        fire = false;
    }

    tmisAdvanceNoteSeq();

    // Genuine whole-loop upstream throttle - see header comment.
    tmisTickSkipCounter++;
    if( tmisTickSkipCounter < TMIS_TICK_DIVISOR )
      return;
    tmisTickSkipCounter = 0;

    if( tmisState == TMIS_STATE_TITLE )
    {
        int fl = tmisClkProgress( &tmisBlink );
        tmisIntroFlip( fl );

        if( fire && !tmisPrevFire )
        {
            tmisSndBox( 0 );
            tmisNewGame();
            tmisAdjLevel( tmisLevel );
            tmisRestoreDome();
            tmisOneDrop = 0;
            tmisFireGateActive = true;
            tmisState = TMIS_STATE_PLAYING;
            fire = false;
        }
    }
    else if( tmisState == TMIS_STATE_PLAYING )
    {
        if( right ) { tmisCrossRight(); tmisRdlpMix(); }
        else if( left ) { tmisCrossLeft(); tmisRdlpMix(); }

        if( up ) { tmisCrossUp(); tmisStartRdlpMix(); }
        else if( down ) { tmisCrossDown(); tmisStartRdlpMix(); }

        if( fire )
        {
            if( tmisOneDrop == 0 )
            {
                tmisRenewShield( tmisCrossX, tmisCrossY );
                tmisOneDrop = 1;
            }
        }
        else
        {
            tmisOneDrop = 0;
        }

        if( tmisAttackBurstActive )
        {
            tmisAttackWeaponStep();
        }
        else
        {
            if( tmisUpdateEngine() )
            {
                if( tmisAllAnimEnd() )
                  tmisBeginLevelClear();
            }
        }

        tmisTinyFlip();
        tmisRdlpMix();
        tmisStartRdlpMix();
    }
    else if( tmisState == TMIS_STATE_LEVEL_CLEAR_PAUSE1 )
    {
        tmisTinyFlip();
        tmisPauseFrames--;
        if( tmisPauseFrames <= 0 )
          tmisState = TMIS_STATE_LEVEL_CLEAR_DRAIN_AMMO;
    }
    else if( tmisState == TMIS_STATE_LEVEL_CLEAR_DRAIN_AMMO )
    {
        if( tmisArmyUseWeapon() )
        {
            tmisScores++;
            tmisCompilSco();
            tmisSndBox( 3 );
        }
        else
        {
            tmisPauseFrames = TMIS_PAUSE_FRAMES;
            tmisState = TMIS_STATE_LEVEL_CLEAR_PAUSE2;
        }
        tmisTinyFlip();
    }
    else if( tmisState == TMIS_STATE_LEVEL_CLEAR_PAUSE2 )
    {
        tmisTinyFlip();
        tmisPauseFrames--;
        if( tmisPauseFrames <= 0 )
        {
            int t;
            int survivors = 0;
            for( t = 0; t < TMIS_NUM_DOME; t++ )
            {
                if( tmisDome[t].active == 1 )
                {
                    if( tmisDome[t].frame == 0 )
                    {
                        tmisScores += 5;
                        tmisCompilSco();
                        tmisDome[t].active = 2;
                        survivors++;
                        tmisSndBox( 4 );
                    }
                    else
                    {
                        tmisDome[t].active = 0;
                    }
                }
            }
            tmisPauseFrames = TMIS_PAUSE_FRAMES;
            tmisLevelClearSurvivors = survivors;
            tmisState = TMIS_STATE_LEVEL_CLEAR_PAUSE3;
        }
    }
    else if( tmisState == TMIS_STATE_LEVEL_CLEAR_PAUSE3 )
    {
        tmisTinyFlip();
        tmisPauseFrames--;
        if( tmisPauseFrames <= 0 )
        {
            if( tmisLevelClearSurvivors == 0 )
            {
                tmisSndBox( 2 );
                tmisState = TMIS_STATE_TITLE;
            }
            else
            {
                tmisSndBox( 1 );
                tmisNextLevel();
                tmisAdjLevel( tmisLevel );
                tmisRestoreDome();
                tmisOneDrop = 0;
                tmisState = TMIS_STATE_PLAYING;
            }
        }
    }

    tmisPrevUp = up;
    tmisPrevDown = down;
    tmisPrevFire = fire;
}

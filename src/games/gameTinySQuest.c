// =============================================================================
// Tiny SQuest - ported from Daniel C's Tiny-SQuest.ino (tinyjoypad.com,
// GPLv3). Same tinyJoypadShim lineage (FastTinyDriver.h) as every other
// Daniel-C game here.
//
// An underwater rescue shooter - a submarine drifts left/right/up/down,
// shooting enemy fish/subs and collecting divers surfacing from below.
// Rescue 6 divers, get them to the surface (banking score for each), and
// the level advances; get hit by an enemy/enemy shot and a "diver" (a
// hit-point pool) is spent, or a life is lost once divers run out.
//
// The lowest remaining goto count (7) of any untried Tiny X title,
// confirmed via a fresh per-header `class ` grep before committing - a
// 2-level class hierarchy (`PASIVE_SPRITE_TSQUEST` -> `ACTIVE_SPRITE_
// TSQUEST : public PASIVE_SPRITE_TSQUEST`), the same shape already solved
// for Tiny Pipe's own `PASIVE_SPRITE_TPIPE`/`SPRITE_TPIPE`. Flattened into
// one `TsqSprite` struct + explicit-pointer functions, same treatment as
// every class-based port here. `SpeedAdd` (declared in the class, never
// read or written anywhere else - confirmed by grep) dropped as dead.
//
// Structural changes from upstream:
//  - upstream's `loop()` is a `START_:`/`NEXT_LEVEL:`/`RESTART_LEVEL:`
//    goto-chain around a gameplay `while(1)` - rewritten as an explicit
//    frame-stepped state machine, same approach as every other
//    tinyJoypadShim port here. `Main_SPK_Bank` (0/1/2 = normal/destroy-
//    step2/destroy-step3, with 254/255 as "please transition the outer
//    state" signals) is kept exactly as upstream's own inner state
//    variable within TSQ_STATE_PLAYING; a genuine, real 1000ms-plus-music
//    blocking wait inside `Start_Proccess_Destroy_Main_Step4_TSQUEST()`
//    (destroying the sub) needed its own two dedicated states
//    (`TSQ_STATE_DESTROY_MUSIC`/`_DESTROY_WAIT`); `Refill_TSQUEST()`'s own
//    "diver bar full, drain rescued divers into score" sequence (a real
//    ~380-call computed sound sweep plus more `_delay_ms()`s) became
//    `TSQ_STATE_REFILL_SWEEP`; `NEXT_LEVEL_TSQUEST()`'s own per-diver
//    300ms-delay drain loop became `TSQ_STATE_NEXT_LEVEL_DRAIN` (one diver
//    per invocation). A genuine, verified-on-purpose upstream quirk: even
//    on game over, `Start_Proccess_Destroy_Main_Step4_TSQUEST()` still
//    unconditionally routes through the same `RESTART_LEVEL:` label
//    (resetting diver/OX/etc for a fresh attempt) *before* the main
//    loop's own `if (RETURN_START==1) goto START_;` check - reached only
//    inside the *next* gameplay tick - ever gets a chance to fire, since a
//    `goto` short-circuits past it the same tick `Main_SPK_Bank` becomes
//    255. Reproduced faithfully: `tsqPlayingTick()` checks
//    `tsqGP.returnStart` as its own very first statement, matching
//    upstream's check position relative to when the level-restart
//    sequence's own states already ran.
//  - `RENDER_DISPLAY_TSQUEST` is overloaded upstream (a 1-arg attract-
//    screen version and an unrelated 3-arg in-game-OSD version) - this
//    dialect has no function overloading, so renamed to two distinct
//    functions (`tsqRenderIntroDisplay`/dropped-in-favor-of-a-unified-
//    renderer, see below).
//  - **A VRAM-persistence partial-redraw pattern, the same class already
//    fixed in Pinball/Doc/Bert/Trick/Pipe/Plaque, found here proactively**:
//    upstream's `RENDER_TSQUEST()` only ever draws pages 1-6, and the
//    3-arg `RENDER_DISPLAY_TSQUEST()` only ever draws page 0 or page 7
//    (gated by a `Refresh_OSD` dirty flag) - each relies on real SSD1306
//    VRAM to hold what the *other* one drew. Rather than reproduce the
//    exact partial-redraw call structure and the `Refresh_OSD` gating
//    (which would also mean two separate renders most ticks, one showing
//    the *previous* tick's gameplay state next to the *current* tick's
//    OSD - a real but likely-imperceptible one-frame stagger upstream's
//    own call order produces), this port takes the same simplification
//    Tiny Plaque's own mode-0/mode-2 unification did: one `tsqRenderFrame()`
//    draws the background/gameplay (pages 1-6) and the live/diver/OX OSD
//    (pages 0/7) together, called once per tick after that tick's own
//    logic has run - `Refresh_OSD` itself was dropped entirely (no gate
//    needed once every render already includes everything).
//  - `switch`/ternary/intra-function-`goto` avoided proactively throughout
//    (matching Tiny Doc/Bike/Pipe/Morpion/Plaque's own established
//    caution) - including two genuine, deliberate GCC-case-fallthrough
//    uses (the main loop's own `Main_SPK_Bank` dispatch, and `LIBERATE_
//    LINE_TSQUEST`'s own `Amount_Sprite`-gated multi-line unlock) both
//    reproduced exactly as cascading `if`s rather than simplified away,
//    and a third, subtler one in `Recupe_Diver_TSQUEST` (`case 0...32`
//    genuinely falls into `case 92...124`'s own body with no break/
//    return between them - reproduced literally, including the resulting
//    always-checks-Dive_B-too behavior for the low x-range, even though
//    that second check is provably a no-op there in practice since Dive_B
//    is never below 70). Binary literals (`BALLISTIC_TSQUEST`/
//    `BALLISTIC2_TSQUEST`'s own single-byte bitmaps) rewritten as decimal
//    with a `// 0bNNNNNNNN` comment.
//  - **Sound sequencing**: Vircon32's audio channel has no queue (see
//    this project's own established lesson) - every upstream call site
//    that fires more than one synchronous `Sound()` call in the same
//    tick needed converting to a frame-stepped sequencer, not just the
//    large ones. `Sound_1_TSQUEST()` (15 calls, a fixed 5x-repeated
//    3-note pattern) and the diver-rescue chime (`Sound(100,10);
//    Sound(200,10);`, 2 calls) both use a small shared `tsqStartNoteSeq`/
//    `tsqAdvanceNoteSeq` fixed-note-list player; `PLAY_MUSIC(Music)` (27
//    notes, table-driven) gets its own `tsqStartMusic`/`tsqAdvanceMusic`
//    reading the real `Music[]` table directly; `Refill_TSQUEST()`'s own
//    ~380-call computed sweep (`for(t2<2){for(t=200;t>10;t--)
//    Sound(t,5);}`) is downsampled (step -15, matching every other large
//    sweep found in this project) via `tsqStartRefillSweep`/
//    `tsqAdvanceRefillSweep`.
//  - No genuine FPS cap exists anywhere in upstream's own main loop
//    (confirmed by re-reading the real .ino - no `_delay_ms()`/`millis()`
//    check in the gameplay `while(1)` body itself) - the "no timing model
//    to match" category, same as Tiny Plaque before its own real-hardware-
//    comparison fix. Unlike Plaque, `RENDER_TSQUEST()` is called
//    unconditionally on *every* iteration (no analogous "redraw only
//    1-in-N ticks" split to find or drop) - ported at a straightforward
//    one-tick-per-real-frame rate for now; if a similar felt-speed
//    mismatch against real hardware is ever reported, the same "run the
//    tick body N times per real frame, render once" fix Plaque/Arkanoid
//    both needed applies directly.
// =============================================================================

int[6] tsqPosition =
{
-8,-20,-32,-128,-116,-104,
};

int[15] tsqRnd3Table =
{
3,2,2,1,3,1,2,1,3,3,2,1,1,2,3,
};

int[11] tsqRnd2Table =
{
0,1,0,1,1,0,1,0,0,0,1,
};

int[3] tsqLine =
{
17,30,43,
};

int[12] tsqSin =
{
0,0,1,1,1,1,0,0,-1,-1,-1,-1,
};

int[55] tsqMusic =
{
54,102,255,0,255,90,255,0,255,80,255,0,255,72,50,62,50,72,50,62,
50,72,50,62,50,72,50,62,50,72,50,62,50,72,50,62,50,72,50,62,
50,72,50,62,50,72,50,62,50,72,50,62,50,72,50,
};

int[42] tsqPolice =
{
4,1,62,34,62,0,0,62,0,0,58,42,46,0,34,42,62,0,14,8,
62,0,46,42,58,0,62,42,58,0,2,58,6,0,62,42,62,0,46,42,
62,0,
};

int[1024] tsqBackgroundData =
{
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
255,255,255,255,255,255,255,255,2,18,2,18,2,18,2,18,2,18,2,18,
2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,
2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,
2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,
2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,
2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,
2,18,2,18,2,18,2,18,2,18,2,18,2,18,2,18,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,64,160,64,160,64,160,64,160,192,160,192,160,
192,160,64,160,64,160,64,160,64,160,64,160,64,160,64,160,64,160,192,160,
192,160,192,160,64,160,64,160,64,160,64,160,64,160,64,160,64,160,192,160,
192,160,192,160,64,160,64,160,64,160,64,160,64,160,64,160,64,160,64,160,
192,160,192,160,192,160,64,160,64,160,64,160,64,160,64,160,64,160,64,160,
192,160,192,160,192,160,64,160,64,160,64,160,64,160,64,160,64,160,64,160,
64,160,192,160,192,160,192,160,64,160,64,160,64,160,64,160,253,254,253,254,
255,255,255,255,255,255,255,255,255,255,255,255,255,254,255,254,253,254,253,254,
255,254,255,255,255,255,255,255,255,255,255,255,135,183,135,254,183,206,183,254,
231,158,231,255,135,183,151,255,135,167,183,255,135,239,223,134,255,134,181,182,
181,182,183,182,183,183,183,183,183,183,183,183,183,183,183,183,183,182,183,182,
183,182,183,182,183,183,135,255,255,255,255,255,255,255,255,255,255,254,255,254,
253,254,253,254,255,254,255,255,255,255,255,255,255,255,255,255,255,255,255,254,
255,254,255,254,
};

int[104] tsqTSubMain =
{
17,1,24,40,40,63,63,62,62,56,56,56,56,56,56,48,48,104,104,24,
40,40,63,63,62,62,56,56,56,56,56,56,48,48,88,88,24,40,40,63,
63,62,62,56,56,56,56,56,56,48,48,48,48,104,104,48,48,56,56,56,
56,56,56,62,62,63,63,40,40,24,88,88,48,48,56,56,56,56,56,56,
62,62,63,63,40,40,24,48,48,48,48,56,56,56,56,56,56,62,62,63,
63,40,40,24,
};

int[104] tsqBlinkMainSub =
{
17,1,24,40,40,63,63,62,62,56,56,56,56,56,56,48,48,104,104,24,
40,40,63,63,62,62,56,56,56,56,56,56,48,48,104,104,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,104,104,48,48,56,56,56,
56,56,56,62,62,63,63,40,40,24,104,104,48,48,56,56,56,56,56,56,
62,62,63,63,40,40,24,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,
};

int[104] tsqDestroyedMainSub =
{
17,1,0,0,0,0,0,8,8,20,20,20,8,8,0,0,0,0,0,0,
0,0,34,34,8,8,0,20,0,8,8,34,34,0,0,0,130,130,130,146,
16,16,0,68,68,68,0,16,16,146,130,130,130,0,0,0,0,0,16,16,
40,40,40,16,16,0,0,0,0,0,0,0,0,34,34,8,8,0,20,0,
8,8,34,34,0,0,0,130,130,130,146,16,16,0,68,68,68,0,16,16,
146,130,130,130,
};

int[50] tsqSub2 =
{
8,1,12,20,31,30,28,28,24,52,12,20,31,30,28,28,24,44,12,20,
31,30,28,28,24,24,52,24,28,28,30,31,20,12,44,24,28,28,30,31,
20,12,24,24,28,28,30,31,20,12,
};

int[5] tsqBallistic =
{
3,1,1,1,1, // 0b00000001 x3
};

int[5] tsqBallistic2 =
{
3,1,4,5,1, // 0b00000100, 0b00000101, 0b00000001
};

int[50] tsqFish =
{
8,1,4,10,6,15,14,4,10,17,4,10,6,15,14,4,10,18,4,10,
14,15,6,4,10,9,17,10,4,14,15,6,10,4,18,10,4,14,15,6,
10,4,9,10,4,6,15,14,10,4,
};

int[50] tsqPlongeur =
{
8,1,16,20,10,24,18,32,32,65,16,20,8,26,16,33,32,32,16,20,
8,24,50,80,17,1,65,32,32,18,24,10,20,16,32,32,33,16,26,8,
20,16,1,17,80,50,24,8,20,16,
};

int[11] tsqDisplayDiver =
{
255,255,255,223,223,95,191,207,239,215,223,
};

int[9] tsqLive =
{
255,131,199,199,195,193,193,199,231,
};

int[162] tsqIntro =
{
80,2,15,3,1,1,255,255,1,1,15,0,16,243,243,0,0,48,240,224,
48,48,240,224,0,16,112,240,208,0,208,48,16,192,192,192,0,192,158,57,
57,49,115,247,228,0,192,224,48,16,16,224,240,0,16,240,240,0,0,144,
240,240,0,192,224,112,80,80,112,96,0,96,80,208,208,144,0,16,248,252,
16,16,0,0,2,3,3,3,2,0,0,0,2,3,3,2,0,2,3,3,
0,2,3,3,2,24,24,17,15,3,0,0,0,0,0,0,0,1,1,1,
2,2,2,1,0,0,0,1,3,2,1,15,15,8,0,1,3,3,2,1,
3,3,2,0,1,3,2,2,3,1,0,3,2,2,2,1,0,0,1,3,
2,1,
};

int[106] tsqStart =
{
52,2,254,1,1,145,41,73,145,1,9,249,9,1,241,41,41,241,1,249,
41,41,209,1,9,249,9,1,1,1,1,241,9,73,209,1,241,41,41,241,
1,249,17,33,17,249,1,249,41,9,1,1,1,254,7,8,8,8,9,9,
8,8,8,9,8,8,9,8,8,9,8,9,8,8,9,8,8,9,8,8,
8,8,8,8,9,9,9,8,9,8,8,9,8,9,8,8,8,9,8,9,
9,9,8,8,8,7,
};

// -----------------------------------------------------------------------------
//   Sprite state (flattened from PASIVE_SPRITE_TSQUEST/ACTIVE_SPRITE_TSQUEST)
// -----------------------------------------------------------------------------

struct TsqSprite
{
    int x;
    int y;
    int direction;
    int killed;
    int active;
    int width;
    int height;
    int speed;
    int ballistic;
    int ballisticX;
    int ballisticY;
    int ballisticSpeed;
};

TsqSprite tsqMain;
TsqSprite[9] tsqOther;

#define TSQ_MAX_LEFT 2
#define TSQ_MAX_RIGHT 125
#define TSQ_MAX_DOWN 51
#define TSQ_MAX_UP 6

void tsqSpriteInit( TsqSprite* s, int activeVal, int x, int y )
{
    s->x = x;
    s->y = y;
    s->direction = 0;
    s->active = activeVal;
    s->killed = 0;
    s->width = 7;
    s->height = 7;
}

int tsqGetD( TsqSprite* s ) { return s->direction; }
void tsqPutD( TsqSprite* s, int v ) { s->direction = v; }
int tsqGetK( TsqSprite* s ) { return s->killed; }
void tsqPutK( TsqSprite* s, int v ) { s->killed = v; }
int tsqGetX( TsqSprite* s ) { return s->x; }
int tsqGetY( TsqSprite* s ) { return s->y; }
int tsqGetW( TsqSprite* s ) { return s->width; }
int tsqGetH( TsqSprite* s ) { return s->height; }
void tsqPutA( TsqSprite* s, int v ) { s->active = v; }
int tsqGetA( TsqSprite* s ) { return s->active; }
void tsqPutW( TsqSprite* s, int v ) { s->width = v; }
void tsqPutH( TsqSprite* s, int v ) { s->height = v; }
void tsqPutX( TsqSprite* s, int v ) { s->x = v; }
void tsqPutY( TsqSprite* s, int v ) { s->y = v; }

void tsqActiveSpriteInit( TsqSprite* s, int activeVal, int x, int y, int speed )
{
    tsqSpriteInit( s, activeVal, x, y );
    if( x < 63 && x > -100 ) tsqPutD( s, 3 );
    else tsqPutD( s, 0 );
    s->speed = speed;
    s->ballistic = 0;
    s->ballisticX = 0;
    s->ballisticY = 0;
    s->ballisticSpeed = 0;
}

int tsqGetSpeed( TsqSprite* s ) { return s->speed; }
int tsqGetBallistic( TsqSprite* s ) { return s->ballistic; }
void tsqDestroyBallistic( TsqSprite* s ) { s->ballistic = 0; }
int tsqGetBallisticX( TsqSprite* s ) { return s->ballisticX; }
int tsqGetBallisticY( TsqSprite* s ) { return s->ballisticY; }

void tsqMoveXR( TsqSprite* s, int width )
{
    if( ( width + tsqGetX( s ) ) < TSQ_MAX_RIGHT )
    {
        tsqPutX( s, tsqGetX( s ) + s->speed );
        tsqPutD( s, 3 );
    }
}

void tsqMoveXL( TsqSprite* s )
{
    if( tsqGetX( s ) > TSQ_MAX_LEFT )
    {
        tsqPutX( s, tsqGetX( s ) - s->speed );
        tsqPutD( s, 0 );
    }
}

void tsqMoveYD( TsqSprite* s, int width )
{
    if( ( width + tsqGetY( s ) ) < TSQ_MAX_DOWN ) tsqPutY( s, tsqGetY( s ) + s->speed );
}

void tsqMoveYU( TsqSprite* s )
{
    if( tsqGetY( s ) > TSQ_MAX_UP ) tsqPutY( s, tsqGetY( s ) - s->speed );
}

// Upstream's own sprite `x` (and SUBSOLO_X below) are real int8_t fields,
// and this specific branch of SPEEDCALC_NEG is fed sprites deliberately
// spawned far off-screen (X = -128/-116/-104, from POSITION_TSQUEST's own
// second row) that keep decrementing past -128 - on real AVR hardware
// this silently wraps to +127 (two's-complement int8_t overflow), a
// deliberate "spawn off the left edge, wrap around to the right edge,
// scroll back across" trick. Vircon32's plain int never wraps, so without
// this the sprite just drifts to ever-more-negative X forever, failing
// tsqBlitzSprite's own bounds check permanently - the sprite silently
// vanishes for good instead of reappearing. Same bug family as this
// project's already-documented byte-truncation/shift-wraparound/signed-
// sentinel/logical-shift findings, just via int8_t signed overflow this
// time (found from a direct user report - "sometimes no enemies or
// swimmers appear at all" - and their own correct hint, "8bit vs 32bit").
int tsqWrapInt8( int val )
{
    val = val & 0xFF;
    if( val > 127 ) val -= 256;
    return val;
}

void tsqSpeedCalcPos( TsqSprite* s, int speedFrame )
{
    int speed = s->speed;
    if( speed == 0 ) speed = 1;
    if( s->speed == 0 && speedFrame == 0 ) return;
    if( ( tsqGetX( s ) + speed ) <= 127 ) tsqPutX( s, tsqGetX( s ) + speed );
    else tsqPutA( s, 0 );
}

void tsqSpeedCalcNeg( TsqSprite* s, int width, int speedFrame )
{
    int speed = s->speed;
    if( speed == 0 ) speed = 1;
    if( s->speed == 0 && speedFrame == 0 ) return;
    if( ( tsqGetX( s ) + width ) > 0 ) tsqPutX( s, tsqGetX( s ) - speed );
    else
    {
        if( tsqGetX( s ) < -100 ) tsqPutX( s, tsqWrapInt8( tsqGetX( s ) - speed ) );
        else tsqPutA( s, 0 );
    }
}

void tsqAutoMove( TsqSprite* s, int width, int speedFrame )
{
    if( tsqGetA( s ) == 0 ) return;
    if( tsqGetD( s ) > 0 ) tsqSpeedCalcPos( s, speedFrame );
    else tsqSpeedCalcNeg( s, width, speedFrame );
}

void tsqBallisticDeploy( TsqSprite* s, int width, int speedBallistic )
{
    if( s->ballistic == 0 )
    {
        s->ballistic = 1;
        s->ballisticX = tsqGetX( s ) + ( width >> 1 );
        s->ballisticY = tsqGetY( s ) + 3;
        if( tsqGetD( s ) == 0 ) s->ballisticSpeed = -speedBallistic;
        else s->ballisticSpeed = speedBallistic;
    }
}

// BallisticPositionX is a real int8_t upstream, and this function's only
// clear condition is "< -6" - no upper bound at all. For a bullet fired
// while facing right (BallisticSpeed=+8, the sub's own default facing
// direction at spawn), X only ever increases - on real AVR hardware,
// once it exceeds 127 it silently wraps to a large negative int8_t value
// (two's-complement overflow), which is what actually satisfies "< -6"
// and clears the shot; a leftward-fired bullet needs no such trick since
// it decreases toward -6 directly. Vircon32's plain int never wraps, so
// a rightward shot that doesn't hit anything just keeps increasing
// forever, leaving `ballistic` permanently set and the weapon jammed -
// found via a direct user report ("sometimes i don't seem able to shoot
// bullets"), the same int8_t-wraparound bug family as tsqWrapInt8's own
// sprite-position fix above, just affecting the ballistic X coordinate
// instead. Fixes both the player's own shot and every enemy's.
void tsqBallisticUpdate( TsqSprite* s )
{
    if( s->ballistic != 0 )
    {
        s->ballisticX = tsqWrapInt8( s->ballisticX + s->ballisticSpeed );
        if( s->ballisticX < -6 ) s->ballistic = 0;
    }
}

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

struct TsqGamePlay
{
    int returnStart;
    int level;
    int ox;
    int scores;
    int diver;
    int speed;
    int mainSpkBank;
    int amountSprite;
    int libSpriteTimer;
    int libTrig;
    int speedBalistic;
    int frame;
    int frameMain;
    int mainLive;
    int frameCycleStep2;
    int subsoloX;
    int diverOsdState;
    int oxOsdState;
    int flipFlop;
    int flipflopCounter;
    int flipFlop2Ox;
    int flipFlop2OxCounter;
    int limitMoveOtherSprite;
    int limitRefresh;
    int latch0FirstFulling;
    int latch1Refill;
    int latch2AfterDead;
    int noJoy;
    int nextLevel;
    int sinAnim;
    int sa;
    int eb;
    int rnd3Pos;
    int rnd2Pos;
};

TsqGamePlay tsqGP;

int tsqM10000;
int tsqM1000;
int tsqM100;
int tsqM10;
int tsqM1;
int tsqCounterDisplayDiver;
int tsqCounterDisplayLive;
int tsqTimer1;

#define TSQ_LEVELMAX 16

// COUNT_RESET, matching upstream's own #define of the same name.
#define TSQ_COUNT_RESET 5

// -----------------------------------------------------------------------------
//   Blit primitives
// -----------------------------------------------------------------------------

int tsqMymap( int x, int inMin, int inMax, int outMin, int outMax )
{
    return ( x - inMin ) * ( outMax - outMin ) / ( inMax - inMin ) + outMin;
}

// Defensively negative-safe, matching every other port's own RecupeLineY
// fix - no call site here is ever actually fed a negative yPos in
// practice, kept for the same zero-cost defensive reason.
int tsqRecupeLineY( int val )
{
    if( val >= 0 ) return val >> 3;
    return -( ( -val + 7 ) >> 3 );
}

int tsqRecupeDecalageY( int val )
{
    return val - ( tsqRecupeLineY( val ) * 8 );
}

int tsqSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown ) return input << decalage;
    return input >> ( 8 - decalage );
}

int tsqBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = tsqRecupeLineY( yPos );

    if( xPass > ( xPos + wSprite - 1 ) || xPass < xPos ||
        recupeLineY > yPass || ( recupeLineY + hSprite ) < yPass )
      return 0x00;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = tsqRecupeDecalageY( yPos );
    int scanA = ( xPass - xPos ) + ( spriteYLine * wSprite ) + 2;
    int scanB = ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;

    int outByte;
    if( scanA > wMax ) outByte = 0x00;
    else outByte = tsqSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = tsqSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

int tsqSpeedBlitz( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    if( xPass > ( xPos + wSprite - 1 ) || xPass < xPos ||
        yPass < yPos || yPass > ( yPos + hSprite - 1 ) )
      return 0x00;
    return sprites[ 2 + ( xPass - xPos ) + ( yPass - yPos ) * wSprite + frame * ( hSprite * wSprite ) ];
}

// SpR[3]/SpR_SUB_Main[3] upstream (arrays of table pointers, selected by
// enemy type / destroy-sequence bank) - ported as small selector functions
// since this dialect has no array-of-pointers-to-PROGMEM-array idiom to
// mirror directly.
int* tsqSpROther( int idx )
{
    if( idx == 0 ) return tsqPlongeur;
    if( idx == 1 ) return tsqFish;
    return tsqSub2;
}

int* tsqSpRSubMain( int bank )
{
    if( bank == 0 ) return tsqTSubMain;
    if( bank == 1 ) return tsqBlinkMainSub;
    return tsqDestroyedMainSub;
}

// -----------------------------------------------------------------------------
//   Sound sequencing (Vircon32's audio channel has no queue - see this
//   file's own header comment)
// -----------------------------------------------------------------------------

// Small fixed (freq,dur)-pair list player, reused for Sound_1_TSQUEST's own
// 15-call pattern and the 2-call diver-rescue chime.
int tsqNoteActive;
int* tsqNoteTable;
int tsqNoteLength;
int tsqNoteIndex;

void tsqStartNoteSeq( int* table, int lengthPairs )
{
    tsqNoteActive = 1;
    tsqNoteTable = table;
    tsqNoteLength = lengthPairs;
    tsqNoteIndex = 0;
}

int tsqAdvanceNoteSeq()
{
    if( !tsqNoteActive ) return 1;
    if( tsqNoteIndex >= tsqNoteLength ) { tsqNoteActive = 0; return 1; }
    Sound( tsqNoteTable[ tsqNoteIndex * 2 ], tsqNoteTable[ tsqNoteIndex * 2 + 1 ] );
    tsqNoteIndex++;
    return 0;
}

int[30] tsqSound1Notes =
{
10,25,40,25,80,25,
10,25,40,25,80,25,
10,25,40,25,80,25,
10,25,40,25,80,25,
10,25,40,25,80,25,
};

int[4] tsqRescueNotes = { 100, 10, 200, 10 };

// PLAY_MUSIC(Music) - the real Music[] table (tsqMusic[0] is the upstream
// header byte, a count of the following freq/dur values, i.e. 2x the
// note count).
int tsqMusicActive;
int tsqMusicIndex;

void tsqStartMusic()
{
    tsqMusicActive = 1;
    tsqMusicIndex = 0;
}

int tsqAdvanceMusic()
{
    if( !tsqMusicActive ) return 1;
    if( tsqMusicIndex >= ( tsqMusic[0] / 2 ) ) { tsqMusicActive = 0; return 1; }
    Sound( tsqMusic[ 1 + tsqMusicIndex * 2 ], tsqMusic[ 2 + tsqMusicIndex * 2 ] );
    tsqMusicIndex++;
    return 0;
}

// Refill_TSQUEST's own ~380-call computed sweep
// (`for(t2<2){for(t=200;t>10;t--)Sound(t,5);_delay_ms(10);}`) - downsampled
// (step -15) the same way as every other large computed sweep found in
// this project (Tiny Pipe/Missile/Morpion/Arena).
int tsqRefillSweepActive;
int tsqRefillSweepT;
int tsqRefillSweepRep;
int tsqRefillSweepWait;

void tsqStartRefillSweep()
{
    tsqRefillSweepActive = 1;
    tsqRefillSweepT = 200;
    tsqRefillSweepRep = 0;
    tsqRefillSweepWait = 0;
}

int tsqAdvanceRefillSweep()
{
    if( !tsqRefillSweepActive ) return 1;
    if( tsqRefillSweepWait > 0 ) { tsqRefillSweepWait--; return 0; }
    if( tsqRefillSweepT > 10 )
    {
        Sound( tsqRefillSweepT, 5 );
        tsqRefillSweepT -= 15;
        return 0;
    }
    tsqRefillSweepRep++;
    if( tsqRefillSweepRep >= 2 ) { tsqRefillSweepActive = 0; return 1; }
    tsqRefillSweepT = 200;
    tsqRefillSweepWait = 1;
    return 0;
}

// -----------------------------------------------------------------------------
//   RND helpers (deterministic rotating tables, not real randomness)
// -----------------------------------------------------------------------------

void tsqRnd3X()
{
    if( tsqGP.rnd3Pos < 14 ) tsqGP.rnd3Pos++; else tsqGP.rnd3Pos = 0;
}

int tsqRecupRnd3()
{
    tsqRnd3X();
    return tsqRnd3Table[ tsqGP.rnd3Pos ];
}

void tsqRnd2X()
{
    if( tsqGP.rnd2Pos < 10 ) tsqGP.rnd2Pos++; else tsqGP.rnd2Pos = 0;
}

int tsqRecupRnd2()
{
    tsqRnd2X();
    return tsqRnd2Table[ tsqGP.rnd2Pos ];
}

int tsqRecupSinAdd( int activ )
{
    if( activ != 2 ) return 0;
    return tsqSin[ tsqGP.sinAnim ];
}

void tsqSinMove()
{
    if( tsqGP.sinAnim < 11 ) tsqGP.sinAnim++; else tsqGP.sinAnim = 0;
}

// -----------------------------------------------------------------------------
//   Score
// -----------------------------------------------------------------------------

void tsqCompilSco()
{
    tsqM10000 = tsqGP.scores / 10000;
    tsqM1000 = ( tsqGP.scores - ( tsqM10000 * 10000 ) ) / 1000;
    tsqM100 = ( tsqGP.scores - ( tsqM1000 * 1000 ) - ( tsqM10000 * 10000 ) ) / 100;
    tsqM10 = ( tsqGP.scores - ( tsqM100 * 100 ) - ( tsqM1000 * 1000 ) - ( tsqM10000 * 10000 ) ) / 10;
    tsqM1 = tsqGP.scores - ( tsqM10 * 10 ) - ( tsqM100 * 100 ) - ( tsqM1000 * 1000 ) - ( tsqM10000 * 10000 );
}

// -----------------------------------------------------------------------------
//   Render primitives
// -----------------------------------------------------------------------------

int tsqBackground( int xPass, int yPass )
{
    return tsqBackgroundData[ xPass + ( yPass * 128 ) ];
}

// Row/column footprint precomputed once per page row in tsqRenderFrame -
// the sub sprite table's own height is 1 page, so it spans at most 2
// page-rows (the usual sub-byte-offset split) - gating the call site to
// that exact range avoids paying for a call that's provably 0 outside it
// (same "self-gated call still costs a full call" lesson as Arkanoid/
// Bert/Tris/Trick/Plaque's own tube-sprite fix).
int tsqMainRowOk;
int tsqMainXStart;
int tsqMainXEnd;

int tsqRecupeMain( int xPass, int yPass )
{
    if( yPass == 0 || yPass == 7 ) return 0;
    if( tsqGetX( &tsqMain ) > xPass ) return 0x00;
    if( ( tsqGetX( &tsqMain ) + 16 ) < xPass ) return 0x00;
    return tsqBlitzSprite( tsqGetX( &tsqMain ), tsqGetY( &tsqMain ), xPass, yPass,
                            tsqGetD( &tsqMain ) + tsqGP.frameMain, tsqSpRSubMain( tsqGP.mainSpkBank ) );
}

// Row/column footprint precomputed once per page row in tsqRenderFrame -
// this call only ever does real work while the player's own bullet is
// actually in flight (i.e. exactly while "shooting"), yet was being
// called at every one of ~640 pixels/frame across the gameplay rows
// regardless - found via a direct user report of a CPU spike specifically
// tied to firing, same "self-gated call still costs a full call" lesson
// as the sub sprite's own fix above.
int tsqBallisticMainRowOk;
int tsqBallisticMainXStart;
int tsqBallisticMainXEnd;

int tsqRecupeBallisticMain( int xPass, int yPass )
{
    if( tsqGetBallistic( &tsqMain ) == 0 ) return 0;
    if( tsqGetBallisticX( &tsqMain ) > xPass ) return 0x00;
    if( ( tsqGetBallisticX( &tsqMain ) + 2 ) < xPass ) return 0x00;
    return tsqBlitzSprite( tsqGetBallisticX( &tsqMain ), tsqGetBallisticY( &tsqMain ), xPass, yPass, 0, tsqBallistic );
}

void tsqRecupRange( int yPass )
{
    if( yPass == 2 ) { tsqGP.sa = 0; tsqGP.eb = 3; return; }
    if( yPass == 3 || yPass == 4 ) { tsqGP.sa = 3; tsqGP.eb = 6; return; }
    if( yPass == 5 || yPass == 6 ) { tsqGP.sa = 6; tsqGP.eb = 9; return; }
    tsqGP.sa = 0;
    tsqGP.eb = 9;
}

// Composited once per page row instead of re-scanning up to 9 enemy
// sprites (plus their own ballistics) at every one of ~1024 pixels/frame
// - the same O(pixels x objects) shape already fixed in Bomber/Pacman/
// Doc/Bert/Pipe/Plaque. Folds tsqRecupRange()'s own sa/eb range-narrowing
// in directly (this was its only remaining render-side consumer once
// tsqRecupeOther/tsqRecupeBallisticOther's per-pixel loops moved here).
int[128] tsqOtherPageBuffer;

void tsqCompositeOtherRow( int y )
{
    int x;
    for( x = 0; x < 128; x++ ) tsqOtherPageBuffer[x] = 0;
    if( y == 0 || y == 1 || y == 7 ) return;
    tsqRecupRange( y );
    int t;
    for( t = tsqGP.sa; t < tsqGP.eb; t++ )
    {
        int a = tsqGetA( &tsqOther[t] );
        if( a != 0 )
        {
            int ox = tsqGetX( &tsqOther[t] );
            int oy = tsqGetY( &tsqOther[t] ) + tsqRecupSinAdd( a );
            int xStart = ox;
            if( xStart < 0 ) xStart = 0;
            int xEnd = ox + 7; // sprite width 8 (matches upstream's own "(GetX()+7)<xPass" bound)
            if( xEnd > 127 ) xEnd = 127;
            int col;
            for( col = xStart; col <= xEnd; col++ )
              tsqOtherPageBuffer[col] = tsqOtherPageBuffer[col] |
                tsqBlitzSprite( ox, oy, col, y, tsqGetD( &tsqOther[t] ) + tsqGP.frame, tsqSpROther( a - 1 ) );
        }
        if( tsqGetBallistic( &tsqOther[t] ) != 0 )
        {
            int bx = tsqGetBallisticX( &tsqOther[t] );
            int by = tsqGetBallisticY( &tsqOther[t] );
            int xStart = bx;
            if( xStart < 0 ) xStart = 0;
            int xEnd = bx + 2; // sprite width 3
            if( xEnd > 127 ) xEnd = 127;
            int col;
            for( col = xStart; col <= xEnd; col++ )
              tsqOtherPageBuffer[col] = tsqOtherPageBuffer[col] | tsqBlitzSprite( bx, by, col, y, 0, tsqBallistic2 );
        }
    }
}

int tsqRecupeRanged( int xPass, int yPass )
{
    if( yPass == 0 || yPass == 1 || yPass == 7 ) return 0;
    int ballisticPixel = 0;
    if( tsqBallisticMainRowOk && xPass >= tsqBallisticMainXStart && xPass <= tsqBallisticMainXEnd )
      ballisticPixel = tsqRecupeBallisticMain( xPass, yPass );
    return tsqOtherPageBuffer[ xPass ] | ballisticPixel;
}

int tsqRecupeSubsolo( int xPass, int yPass )
{
    if( yPass == 1 ) return tsqSpeedBlitz( tsqGP.subsoloX, 1, xPass, yPass, tsqGP.frame, tsqSub2 );
    return 0x00;
}

int tsqRecupeScores( int xPass, int yPass )
{
    return tsqSpeedBlitz( 52, 0, xPass, yPass, tsqM10000, tsqPolice ) |
           tsqSpeedBlitz( 56, 0, xPass, yPass, tsqM1000, tsqPolice ) |
           tsqSpeedBlitz( 60, 0, xPass, yPass, tsqM100, tsqPolice ) |
           tsqSpeedBlitz( 64, 0, xPass, yPass, tsqM10, tsqPolice ) |
           tsqSpeedBlitz( 68, 0, xPass, yPass, tsqM1, tsqPolice ) |
           tsqSpeedBlitz( 72, 0, xPass, yPass, 0, tsqPolice );
}

int tsqRecupeOx( int flip, int xPass )
{
    int tmp = 61;
    if( flip != 0 ) tmp = tsqGP.ox;
    if( xPass > tmp && xPass < 90 ) return 207; // 0b11001111
    return 0xff;
}

void tsqConfigDisplayDiver( int* aOut, int* bOut )
{
    if( tsqGP.diver > 3 )
    {
        *aOut = ( 3 * 11 ) + 1;
        *bOut = ( ( tsqGP.diver - 3 ) * 11 ) + 91;
    }
    else
    {
        *aOut = ( tsqGP.diver * 11 ) - 1;
        *bOut = 70;
    }
}

int tsqRecupeDiver( int xPass )
{
    if( tsqGP.diver == 0 ) return 0xFF;
    int diveA, diveB;
    tsqConfigDisplayDiver( &diveA, &diveB );

    int inLowRange = ( xPass >= 0 && xPass <= 32 );
    int inHighRange = ( xPass >= 92 && xPass <= 124 );

    if( inLowRange )
    {
        // Upstream's own `case 0...32` falls straight into `case
        // 92...124`'s own body (no break/return between them) - see this
        // file's own header comment. That means the Dive_B check below
        // applies here too, even though it's provably always false for
        // this x-range in practice (Dive_B is never below 70).
        if( xPass > diveA ) return 0xFF;
        if( xPass > diveB ) return 0xFF;
    }
    else if( inHighRange )
    {
        if( xPass > diveB ) return 0xFF;
    }
    else return 0xff;

    int byte_ = tsqDisplayDiver[ tsqCounterDisplayDiver ];
    if( tsqCounterDisplayDiver < 10 ) tsqCounterDisplayDiver++; else tsqCounterDisplayDiver = 0;
    return byte_;
}

int tsqRecupeLive( int xPass )
{
    if( tsqGP.mainLive == 0 ) return 0xFF;
    if( xPass > ( ( tsqGP.mainLive * 9 ) - 1 ) ) return 0xFF;
    int byte_ = tsqLive[ tsqCounterDisplayLive ];
    if( tsqCounterDisplayLive < 8 ) tsqCounterDisplayLive++; else tsqCounterDisplayLive = 0;
    return byte_;
}

int tsqFullDisplayRefresh( int xPass, int yPass )
{
    if( yPass == 0 ) return tsqRecupeLive( xPass ) & ( 0xff - tsqRecupeScores( xPass, yPass ) );
    if( yPass == 7 )
    {
        int diverPart = 0xff;
        if( tsqGP.diverOsdState == 1 ) diverPart = tsqRecupeDiver( xPass );
        return diverPart & tsqRecupeOx( tsqGP.oxOsdState, xPass );
    }
    return 0xFF;
}

int tsqBlinkStartIntro( int xPass, int yPass, int bl )
{
    if( bl > 6 ) return tsqSpeedBlitz( 38, 4, xPass, yPass, 0, tsqStart );
    return 0x00;
}

void tsqRenderIntroDisplay( int bl )
{
    md_beginFrame();
    int x, y;
    for( y = 0; y < 8; y++ )
      for( x = 0; x < 128; x++ )
        md_drawColumn( x, y, tsqSpeedBlitz( 24, 2, x, y, 0, tsqIntro ) | tsqBackground( x, y ) | tsqBlinkStartIntro( x, y, bl ) );
}

// Unified renderer - combines upstream's own RENDER_TSQUEST (pages 1-6,
// gameplay) and the 3-arg RENDER_DISPLAY_TSQUEST (page 0/7, OSD) into one
// always-full-8-page redraw - see this file's own header comment on why.
void tsqRenderFrame()
{
    md_beginFrame();
    int x, y;

    int mainRecupeLineY = tsqRecupeLineY( tsqGetY( &tsqMain ) );
    tsqMainXStart = tsqGetX( &tsqMain );
    tsqMainXEnd = tsqMainXStart + 16;

    int ballisticActive = ( tsqGetBallistic( &tsqMain ) != 0 );
    int ballisticLineY = 0;
    if( ballisticActive )
    {
        ballisticLineY = tsqRecupeLineY( tsqGetBallisticY( &tsqMain ) );
        tsqBallisticMainXStart = tsqGetBallisticX( &tsqMain );
        tsqBallisticMainXEnd = tsqBallisticMainXStart + 2;
    }

    for( y = 0; y < 8; y++ )
    {
        tsqCompositeOtherRow( y );
        tsqMainRowOk = ( y >= mainRecupeLineY && y <= mainRecupeLineY + 1 );
        tsqBallisticMainRowOk = ( ballisticActive && y >= ballisticLineY && y <= ballisticLineY + 1 );
        for( x = 0; x < 128; x++ )
        {
            int pixel;
            if( y == 0 || y == 7 )
              pixel = tsqFullDisplayRefresh( x, y ) & tsqBackground( x, y );
            else
            {
                int mainPixel = 0;
                if( tsqMainRowOk && x >= tsqMainXStart && x <= tsqMainXEnd ) mainPixel = tsqRecupeMain( x, y );
                int subsoloPixel = 0;
                if( y == 1 ) subsoloPixel = tsqRecupeSubsolo( x, y ); // self-gated to yPass==1 only
                pixel = subsoloPixel | tsqRecupeRanged( x, y ) | mainPixel | tsqBackground( x, y );
            }
            md_drawColumn( x, y, pixel );
        }
    }
}

// -----------------------------------------------------------------------------
//   Collision
// -----------------------------------------------------------------------------

int tsqCollision( int x, int y, int w, int h, int x2, int y2, int w2, int h2 )
{
    if( x + w < x2 || x > x2 + w2 || y + h < y2 || y > y2 + h2 ) return 0;
    return 1;
}

// -----------------------------------------------------------------------------
//   Gameplay / level logic
// -----------------------------------------------------------------------------

void tsqOsdDiver( int state )
{
    tsqGP.diverOsdState = state;
}

void tsqOsdOx( int state )
{
    tsqGP.oxOsdState = state;
}

void tsqScoreAdd()
{
    int tmp = tsqM100;
    tsqGP.scores += 3;
    tsqCompilSco();
    if( tsqM100 != tmp )
    {
        if( tsqGP.mainLive < 5 ) tsqGP.mainLive++;
    }
}

void tsqGameOver()
{
    tsqGP.returnStart = 1;
}

void tsqRefreshAnim()
{
    if( tsqGP.frame < 2 ) tsqGP.frame++; else tsqGP.frame = 0;
}

void tsqRefreshMainAnim()
{
    if( tsqGP.frameMain < 2 ) tsqGP.frameMain++; else tsqGP.frameMain = 0;
}

void tsqStartProccessDestroyMain()
{
    tsqStartNoteSeq( tsqSound1Notes, 15 );
    tsqGP.mainSpkBank = 1;
    tsqGP.frameMain = 0;
    tsqGP.frameCycleStep2 = 0;
}

void tsqKilingMain()
{
    if( tsqGP.latch1Refill == 0 ) tsqStartProccessDestroyMain();
}

void tsqStartProccessDestroyMainStep3()
{
    tsqGP.mainSpkBank = 2;
    tsqGP.frameMain = 0;
}

void tsqStep2Counter( int skip )
{
    if( skip != 1 ) return;
    if( tsqGP.frameCycleStep2 == 16 ) tsqStartProccessDestroyMainStep3();
    else tsqGP.frameCycleStep2++;
}

void tsqCheckCollisionSolo()
{
    if( tsqCollision( tsqGetX( &tsqMain ), tsqGetY( &tsqMain ), 16, 7, tsqGP.subsoloX, 8, 6, 4 ) )
    {
        tsqGP.subsoloX = -10;
        tsqStartProccessDestroyMain();
    }
}

void tsqCheckCollisionBallistic()
{
    tsqRecupRange( tsqRecupeLineY( tsqGetBallisticY( &tsqMain ) ) );
    int t;
    for( t = tsqGP.sa; t < tsqGP.eb; t++ )
    {
        if( tsqGetA( &tsqOther[t] ) > 1 )
        {
            if( tsqCollision( tsqGetBallisticX( &tsqMain ), tsqGetBallisticY( &tsqMain ), 2, 1, tsqGetX( &tsqOther[t] ), tsqGetY( &tsqOther[t] ), 7, 6 ) )
            {
                if( tsqGetBallistic( &tsqMain ) )
                {
                    tsqPutA( &tsqOther[t], 0 );
                    tsqScoreAdd();
                    tsqDestroyBallistic( &tsqMain );
                }
            }
        }
    }
}

void tsqCheckOtherCollisionBallistic()
{
    tsqRecupRange( tsqRecupeLineY( tsqGetY( &tsqMain ) ) );
    int t;
    for( t = tsqGP.sa; t < tsqGP.eb; t++ )
    {
        if( tsqCollision( tsqGetBallisticX( &tsqOther[t] ), tsqGetBallisticY( &tsqOther[t] ), 2, 1, tsqGetX( &tsqMain ), tsqGetY( &tsqMain ), 16, 6 ) )
        {
            if( tsqGetBallistic( &tsqOther[t] ) )
            {
                tsqKilingMain();
                tsqDestroyBallistic( &tsqOther[t] );
                return;
            }
        }
    }
}

void tsqCheckCollisionMain2Other()
{
    int t;
    for( t = 0; t < 9; t++ )
    {
        if( tsqGetA( &tsqOther[t] ) )
        {
            if( tsqCollision( tsqGetX( &tsqMain ), tsqGetY( &tsqMain ), 16, 6, tsqGetX( &tsqOther[t] ), tsqGetY( &tsqOther[t] ), 7, 6 ) )
            {
                int a = tsqGetA( &tsqOther[t] );
                if( a == 1 )
                {
                    if( tsqGP.diver < 6 )
                    {
                        tsqGP.diver++;
                        tsqStartNoteSeq( tsqRescueNotes, 2 );
                        tsqOsdDiver( 1 );
                        tsqPutA( &tsqOther[t], 0 );
                    }
                }
                else if( a == 2 || a == 3 ) tsqKilingMain();
            }
        }
    }
}

int tsqP( int direction, int position )
{
    return tsqPosition[ ( direction * 3 ) + position ];
}

int tsqLineAt( int position )
{
    return tsqLine[ position ];
}

void tsqMakeLine( int* aOut, int* bOut, int* cOut )
{
    *aOut = tsqRecupRnd3();
    if( *aOut == 1 || *aOut == 2 ) { *bOut = 2; *cOut = 2; }
    else if( *aOut == 3 ) { *bOut = 3; *cOut = 3; }
}

void tsqLiberateLine( int lineIdx )
{
    int aVal, bVal, cVal;
    int lineTemp = lineIdx * 3;
    int direction = tsqRecupRnd2();
    int lineUse = tsqLineAt( lineIdx );
    int actualSpeed = tsqGP.speed;
    tsqMakeLine( &aVal, &bVal, &cVal );
    // Deliberate cascading unlock (matches upstream's own case-fallthrough
    // switch on Amount_Sprite: 3 unlocks all 3 slots, 2 unlocks 2, 1 just 1
    // - see this file's own header comment).
    if( tsqGP.amountSprite >= 3 ) tsqActiveSpriteInit( &tsqOther[ lineTemp + 2 ], cVal, tsqP( direction, 2 ), lineUse, actualSpeed );
    if( tsqGP.amountSprite >= 2 ) tsqActiveSpriteInit( &tsqOther[ lineTemp + 1 ], bVal, tsqP( direction, 1 ), lineUse, actualSpeed );
    if( tsqGP.amountSprite >= 1 ) tsqActiveSpriteInit( &tsqOther[ lineTemp ], aVal, tsqP( direction, 0 ), lineUse, actualSpeed );
}

void tsqCheckEndingline( int lineIdx )
{
    int lineTemp = lineIdx * 3;
    int t;
    for( t = 0; t < 3; t++ )
      if( tsqGetA( &tsqOther[ lineTemp + t ] ) || tsqGetBallistic( &tsqOther[ lineTemp + t ] ) ) return;
    if( tsqGP.libSpriteTimer == tsqGP.libTrig )
    {
        tsqGP.libSpriteTimer = 0;
        tsqLiberateLine( lineIdx );
    }
    else tsqGP.libSpriteTimer++;
}

void tsqFlipFlop2()
{
    if( tsqGP.ox > 70 ) { tsqOsdOx( 1 ); return; }
    if( tsqGP.flipFlop2OxCounter == 0 )
    {
        if( tsqGP.flipFlop2Ox == 0 ) tsqGP.flipFlop2Ox = 1; else tsqGP.flipFlop2Ox = 0;
        if( tsqGP.flipFlop2Ox == 0 ) Sound( 100, 10 ); else Sound( 140, 10 );
        tsqGP.flipFlop2OxCounter = TSQ_COUNT_RESET;
    }
    else tsqGP.flipFlop2OxCounter--;
    tsqOsdOx( tsqGP.flipFlop2Ox );
}

void tsqFlipFlop()
{
    if( tsqGP.diver != 6 ) { tsqOsdDiver( 1 ); return; }
    if( tsqGP.flipflopCounter == 0 )
    {
        if( tsqGP.flipFlop == 0 ) tsqGP.flipFlop = 1; else tsqGP.flipFlop = 0;
        tsqGP.flipflopCounter = TSQ_COUNT_RESET;
    }
    else tsqGP.flipflopCounter--;
    tsqOsdDiver( tsqGP.flipFlop );
}

int tsqBalisticLine( int sprite, int state )
{
    if( sprite == 2 || sprite == 5 ) return 0;
    if( state == 0 ) return 0;
    return state;
}

int tsqCheckIfDeployed( int t )
{
    int a = 0, b = 0;
    if( t >= 0 && t <= 2 ) { a = 0; b = 2; }
    else if( t >= 3 && t <= 5 ) { a = 3; b = 5; }
    else if( t >= 6 && t <= 8 ) { a = 6; b = 8; }
    int t2;
    for( t2 = a; t2 < b; t2++ )
      if( tsqGetBallistic( &tsqOther[t2] ) != 0 ) return 1;
    return 0;
}

void tsqUpdateBallistic()
{
    int oneShoot = 0;
    tsqBallisticUpdate( &tsqMain );
    int t;
    for( t = 0; t < 9; t++ )
    {
        tsqBallisticUpdate( &tsqOther[t] );
        if( tsqGetA( &tsqOther[t] ) == 3 )
        {
            if( tsqGetBallistic( &tsqOther[t] ) == 0 && oneShoot == 0 && tsqCheckIfDeployed( t ) == 0 )
              tsqBallisticDeploy( &tsqOther[t], tsqSub2[0], tsqGP.speedBalistic );
        }
        oneShoot = tsqBalisticLine( t, oneShoot );
    }
}

void tsqJoyPadRefresh()
{
    if( tsqGP.noJoy ) return;
    if( isLeftPressed() ) tsqMoveXL( &tsqMain );
    if( isRightPressed() ) tsqMoveXR( &tsqMain, tsqTSubMain[0] );
    if( isUpPressed() ) tsqMoveYU( &tsqMain );
    if( isDownPressed() ) tsqMoveYD( &tsqMain, 6 );
    if( isFirePressed() )
    {
        if( tsqGetBallistic( &tsqMain ) == 0 && tsqGetY( &tsqMain ) > 12 )
        {
            Sound( 200, 1 );
            tsqBallisticDeploy( &tsqMain, tsqTSubMain[0], 8 );
        }
    }
}

void tsqOxReduce()
{
    if( tsqGP.limitRefresh < 15 ) tsqGP.limitRefresh++;
    else
    {
        tsqGP.limitRefresh = 0;
        if( tsqGP.ox > 61 )
        {
            tsqGP.ox--;
            if( tsqGP.ox > 70 ) tsqOsdOx( 1 );
        }
        else tsqKilingMain();
    }
}

void tsqRemoveDiver()
{
    if( tsqGP.diver > 0 ) tsqGP.diver--;
    else tsqKilingMain();
}

void tsqNextLevelTrigger()
{
    tsqGP.nextLevel = 1;
}

void tsqNewLevel()
{
    if( tsqGP.level < TSQ_LEVELMAX ) tsqGP.level++; else tsqGP.level = TSQ_LEVELMAX;
    tsqGP.mainSpkBank = 254;
}

int tsqAmountSpriteSet( int level )
{
    if( level >= 1 && level <= 2 ) return 1;
    if( level >= 3 && level <= 6 ) return 2;
    return 3;
}

void tsqLevelAdjust( int level )
{
    tsqGP.libTrig = tsqMymap( level, 1, TSQ_LEVELMAX, 25, 1 );
    tsqGP.speed = tsqMymap( level, 1, TSQ_LEVELMAX, 0, 3 );
    tsqGP.amountSprite = tsqAmountSpriteSet( level );
    tsqGP.speedBalistic = tsqMymap( level, 1, TSQ_LEVELMAX, 2, 5 );
}

void tsqResetVar()
{
    tsqGP.returnStart = 0;
    tsqGP.rnd3Pos = 0;
    tsqGP.rnd2Pos = 0;
    tsqGP.diver = 0;
    tsqGP.level = 1;
    tsqGP.scores = 0;
    tsqGP.frame = 0;
    tsqGP.frameMain = 0;
    tsqGP.mainLive = 3;
    tsqGP.frameCycleStep2 = 0;
    tsqGP.diverOsdState = 1;
    tsqGP.oxOsdState = 1;
    tsqGP.flipFlop = 0;
    tsqGP.flipFlop2Ox = 0;
    tsqGP.limitMoveOtherSprite = 0;
    tsqGP.limitRefresh = 0;
    tsqGP.latch1Refill = 0;
    tsqGP.latch2AfterDead = 0;
    tsqGP.sinAnim = 0;
}

void tsqInitOtherSprites()
{
    int t;
    for( t = 0; t < 9; t++ )
    {
        tsqSpriteInit( &tsqOther[t], 0, 90, 32 );
        tsqDestroyBallistic( &tsqOther[t] );
    }
}

// Upstream reads two floating analogRead() pins as an entropy source to
// spin RND3Pos/RND2Pos an unpredictable extra amount at the start of
// every level - no analogRead() equivalent exists here, so this uses the
// shared arand() helper instead (same reasoning as every other port's
// own random-seed substitution in this project).
void tsqRestLevel()
{
    tsqGP.subsoloX = -60;
    tsqGP.libSpriteTimer = 0;
    tsqGP.flipflopCounter = TSQ_COUNT_RESET;
    tsqGP.flipFlop2OxCounter = TSQ_COUNT_RESET;
    tsqGP.nextLevel = 0;
    tsqGP.noJoy = 0;
    tsqGP.ox = 62;
    tsqGP.latch2AfterDead = 0;
    tsqGP.latch1Refill = 0;
    tsqGP.latch0FirstFulling = 0;
    tsqActiveSpriteInit( &tsqMain, 1, 55, 6, 2 );
    tsqGP.mainSpkBank = 0;
    int extra = arand( 256 );
    int t;
    for( t = 0; t < extra; t++ ) { tsqRnd3X(); tsqRnd2X(); }
    tsqInitOtherSprites();
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define TSQ_STATE_ATTRACT            0
#define TSQ_STATE_LEVEL_START_WAIT   1
#define TSQ_STATE_PLAYING            2
#define TSQ_STATE_DESTROY_MUSIC      3
#define TSQ_STATE_DESTROY_WAIT       4
#define TSQ_STATE_REFILL_SWEEP       5
#define TSQ_STATE_REFILL_WAIT        6
#define TSQ_STATE_NEXT_LEVEL_DRAIN   7

int tsqState;
int tsqWaitFrames;
int tsqBlinkAttract;
int tsqForceRedraw;

// Requested: run the whole game at 30fps instead of a straight 60fps
// tick. Upstream itself has no genuine timing model at all (no
// _delay_ms()/millis() anywhere in the real gameplay while(1) loop - see
// this file's own header comment), so unlike NumberPlace/HollowSeeker/
// t2048/Doc this isn't restoring a real original rate, it's a deliberate
// pacing choice. Implemented by skipping the *entire* per-tick update
// (logic and render together) on alternate real engine frames, letting
// the previous frame persist (same "skip the whole draw call" trick this
// project's dirty-flag caching already relies on elsewhere) - every
// wait-frame constant in this file is still counted in *ticks*, not real
// milliseconds, and deliberately left unchanged: since logic is gated by
// the exact same throttle as rendering, a "24-tick" wait still takes 24
// ticks, just at half the real tick rate, so movement/waits/blinks all
// slow down uniformly together rather than needing individual rescaling.
#define TSQ_TICK_DIVISOR 2
int tsqTickSkipCounter;

int[4] tsqStartGameNotes = { 100, 255, 10, 255 };

void tsqBeginAttract()
{
    tsqBlinkAttract = 0;
    tsqRenderIntroDisplay( 0 );
    tsqState = TSQ_STATE_ATTRACT;
}

// Shared by both upstream's own `NEXT_LEVEL:` path (isNextLevel=1, runs
// Level_Adjust first) and its `RESTART_LEVEL:` path (isNextLevel=0,
// restarting the current level after a death) - upstream itself falls
// through from one label straight into the other, matching this single
// parameterized function.
void tsqBeginLevelRestart( int isNextLevel )
{
    if( isNextLevel ) tsqLevelAdjust( tsqGP.level );
    tsqRestLevel();
    tsqOsdDiver( 1 );
    tsqOsdOx( 1 );
    tsqRenderFrame();
    tsqWaitFrames = 24; // ~400ms at the original 60fps-tick tuning
    tsqState = TSQ_STATE_LEVEL_START_WAIT;
}

void tsqBeginDestroyMainStep4()
{
    if( tsqGP.diver > 0 ) tsqGP.diver--;
    tsqGP.mainSpkBank = 1;
    tsqGP.frameMain = 2;
    tsqPutA( &tsqMain, 0 );
    if( tsqGP.mainLive != 0 ) tsqGP.mainLive--;
    else tsqGameOver();
    tsqRenderFrame();
    tsqStartMusic();
    tsqState = TSQ_STATE_DESTROY_MUSIC;
}

// Returns 1 if this tick began the destroy-main-step4 transition (caller
// should stop processing this tick immediately).
int tsqStep3Counter()
{
    if( tsqGP.frameMain == 2 )
    {
        tsqBeginDestroyMainStep4();
        return 1;
    }
    if( tsqGP.frameMain < 2 ) tsqGP.frameMain++;
    return 0;
}

// Returns 1 if this tick began the refill-sweep state transition.
int tsqRefill( int y )
{
    if( y != 6 ) { tsqGP.latch1Refill = 0; return 0; }
    if( tsqGP.latch1Refill == 0 )
    {
        if( tsqGP.latch0FirstFulling == 0 )
        {
            tsqGP.latch0FirstFulling = 1;
        }
        else
        {
            if( tsqGP.diver == 6 )
            {
                tsqGP.noJoy = 1;
                tsqRenderFrame();
                tsqStartRefillSweep();
                tsqState = TSQ_STATE_REFILL_SWEEP;
                tsqGP.latch1Refill = 1;
                return 1;
            }
            else tsqRemoveDiver();
        }
        tsqGP.latch1Refill = 1;
    }
    else
    {
        if( tsqGP.ox < 90 )
        {
            // Not tsqRenderFrame() here: this branch returns 0 (no state
            // transition), so tsqUpdatePlaying()'s own trailing render at
            // the end of the same tick already redraws this exact scene -
            // an extra render here would just draw the identical frame
            // twice, every tick the oxygen gauge climbs (found via a
            // direct user report of a CPU spike specifically "when
            // oxygen meter fills", the same double-render class already
            // fixed in Tiny Plaque's own decount-fuel sequence).
            tsqGP.noJoy = 1;
            tsqGP.ox++;
            Sound( tsqGP.ox << 1, 10 );
            tsqOsdDiver( 1 );
            tsqOsdOx( 1 );
        }
        else
        {
            tsqGP.noJoy = 0;
            tsqGP.latch2AfterDead = 1;
        }
    }
    return 0;
}

// Returns 1 if this tick's normal playing flow was interrupted by a
// state transition (the refill-sweep or next-level-drain sequences).
int tsqUpdateGameplay()
{
    tsqRnd3X();
    tsqRnd2X();
    tsqFlipFlop();
    tsqFlipFlop2();
    if( tsqGP.nextLevel == 0 )
    {
        if( tsqRefill( tsqGetY( &tsqMain ) ) ) return 1;
        tsqCheckEndingline( tsqRecupRnd3() - 1 );
        if( tsqGP.latch1Refill == 0 )
        {
            tsqCheckCollisionMain2Other();
            tsqCheckCollisionBallistic();
            tsqCheckOtherCollisionBallistic();
            tsqOxReduce();
        }
        if( tsqGP.latch2AfterDead == 1 )
        {
            tsqJoyPadRefresh();
            tsqUpdateBallistic();
            int t;
            for( t = 0; t < 9; t++ ) if( tsqGetA( &tsqOther[t] ) ) tsqAutoMove( &tsqOther[t], tsqSub2[0], tsqGP.limitMoveOtherSprite );
            if( tsqGP.limitMoveOtherSprite == 0 ) tsqGP.limitMoveOtherSprite = 1; else tsqGP.limitMoveOtherSprite = 0;
        }
    }
    else
    {
        tsqGP.latch1Refill = 1;
        tsqGP.latch2AfterDead = 0;
        tsqWaitFrames = 0;
        tsqState = TSQ_STATE_NEXT_LEVEL_DRAIN;
        return 1;
    }
    return 0;
}

void tsqUpdatePlaying()
{
    if( tsqGP.returnStart == 1 ) { tsqBeginAttract(); return; }

    if( tsqGP.mainSpkBank == 0 )
    {
        tsqCheckCollisionSolo();
        if( tsqUpdateGameplay() ) return;
    }

    if( tsqTimer1 < 1 )
    {
        tsqTimer1++;
    }
    else
    {
        tsqSinMove();
        tsqTimer1 = 0;
        // SUBSOLO_X is a real int8_t upstream and relies on the same
        // wraparound-at--128 trick as SPEEDCALC_NEG above (a background
        // "swimmer" that scrolls left forever, wrapping to the right edge
        // every time it exits the left) - see tsqWrapInt8's own comment.
        if( tsqGP.level > 3 ) tsqGP.subsoloX = tsqWrapInt8( tsqGP.subsoloX - 1 ); else tsqGP.subsoloX = -60;

        // Deliberate cascading dispatch (matches upstream's own
        // fallthrough switch on Main_SPK_Bank - see this file's own
        // header comment).
        if( tsqGP.mainSpkBank == 0 ) tsqRefreshAnim();
        if( tsqGP.mainSpkBank == 0 || tsqGP.mainSpkBank == 1 )
        {
            tsqRefreshMainAnim();
            tsqStep2Counter( tsqGP.mainSpkBank );
        }
        else if( tsqGP.mainSpkBank == 2 )
        {
            if( tsqStep3Counter() ) return;
        }
    }

    tsqRenderFrame();
}

void tsqForceRedrawNow()
{
    if( tsqState == TSQ_STATE_ATTRACT ) tsqRenderIntroDisplay( tsqBlinkAttract );
    else tsqRenderFrame();
}

void gameTinySQuest_init()
{
    InitTinyJoypad();
    tsqBeginAttract();
}

// Quit-confirmation-dialog resume hook - checked proactively against the
// onResume audit before shipping (matching Tiny Pipe/Morpion/Plaque's own
// practice) - TSQ_STATE_ATTRACT has no timer of its own.
void gameTinySQuest_forceRedraw()
{
    tsqForceRedraw = 1;
}

void gameTinySQuest_update()
{
    // The onResume hook must respond immediately regardless of tick
    // phase, so it stays outside the throttle below.
    if( tsqForceRedraw )
    {
        tsqForceRedrawNow();
        tsqForceRedraw = 0;
    }

    // 30fps whole-tick throttle - see this file's own comment above
    // TSQ_TICK_DIVISOR's declaration.
    if( tsqTickSkipCounter < TSQ_TICK_DIVISOR - 1 ) { tsqTickSkipCounter++; return; }
    tsqTickSkipCounter = 0;

    // Sound_1_TSQUEST/the diver-rescue chime both free-run in the
    // background via the shared small note-list player - advancing this
    // unconditionally here means neither needs its own dedicated state.
    tsqAdvanceNoteSeq();

    if( tsqState == TSQ_STATE_ATTRACT )
    {
        if( isFirePressed() )
        {
            md_armInputFireGate();
            tsqResetVar();
            tsqStartNoteSeq( tsqStartGameNotes, 2 );
            tsqActiveSpriteInit( &tsqMain, 1, 54, 6, 2 );
            tsqTimer1 = 0;
            tsqBeginLevelRestart( 1 );
            return;
        }
        if( tsqBlinkAttract < 12 ) tsqBlinkAttract++; else tsqBlinkAttract = 0;
        tsqRenderIntroDisplay( tsqBlinkAttract );
        return;
    }

    if( tsqState == TSQ_STATE_LEVEL_START_WAIT )
    {
        if( tsqWaitFrames > 0 ) { tsqWaitFrames--; return; }
        tsqState = TSQ_STATE_PLAYING;
        return;
    }

    if( tsqState == TSQ_STATE_PLAYING ) { tsqUpdatePlaying(); return; }

    if( tsqState == TSQ_STATE_DESTROY_MUSIC )
    {
        if( tsqAdvanceMusic() )
        {
            tsqWaitFrames = 60; // ~1000ms at the original 60fps-tick tuning
            tsqState = TSQ_STATE_DESTROY_WAIT;
        }
        return;
    }

    if( tsqState == TSQ_STATE_DESTROY_WAIT )
    {
        if( tsqWaitFrames > 0 ) { tsqWaitFrames--; return; }
        // Not Respond_TSQUEST()'s own Main_SPK_Bank=255: this port's
        // tsqBeginLevelRestart() calls tsqRestLevel() immediately after,
        // which itself sets mainSpkBank=0 - the intermediate 255 value
        // is never read by anything in between, so setting it at all
        // would be dead work (verified, not assumed).
        tsqBeginLevelRestart( 0 );
        return;
    }

    if( tsqState == TSQ_STATE_REFILL_SWEEP )
    {
        if( tsqAdvanceRefillSweep() )
        {
            tsqWaitFrames = 12; // ~200ms at the original 60fps-tick tuning
            tsqState = TSQ_STATE_REFILL_WAIT;
        }
        return;
    }

    if( tsqState == TSQ_STATE_REFILL_WAIT )
    {
        if( tsqWaitFrames > 0 ) { tsqWaitFrames--; return; }
        tsqNextLevelTrigger();
        tsqState = TSQ_STATE_PLAYING;
        return;
    }

    if( tsqState == TSQ_STATE_NEXT_LEVEL_DRAIN )
    {
        if( tsqWaitFrames > 0 ) { tsqWaitFrames--; return; }
        if( tsqGP.diver > 0 )
        {
            tsqGP.diver--;
            tsqScoreAdd();
            tsqOsdDiver( 1 );
            tsqOsdOx( 1 );
            tsqRenderFrame();
            Sound( 100, 255 );
            tsqWaitFrames = 18; // ~300ms at the original 60fps-tick tuning
            return;
        }
        tsqNewLevel();
        tsqBeginLevelRestart( 1 );
        return;
    }
}

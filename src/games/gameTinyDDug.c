// =============================================================================
// Tiny DDug (Daniel C, 2020-2021, GPLv3) - a Dig-Dug-style game: dig tunnels
// through a bit-packed rock grid, fight up to 4 tracking enemies with a
// 2-segment extending laser, or trap/crush them by tunneling out their
// support. Same tinyJoypadShim/FastTinyDriver.h A0/A3-analog-axis + digital-
// pin-1-fire button lineage as every other Daniel-C game ported so far.
//
// Structural notes:
// - Upstream's class hierarchy (Sprite_TDDUG -> Moving_Sprite_TDDUG -> both
//   Enemy_Sprite_TDDUG and Main_Sprite_TDDUG, plus a separate sibling
//   WEAPON_TDDUG : public Sprite_TDDUG) is flattened into one combined
//   `TddugSprite` holding the union of every field any of those
//   classes ever used, with methods becoming plain functions taking an
//   explicit pointer - the same technique already used for TinyMinez's own
//   class hierarchy, just shallower here.
// - `Moving_Sprite_TDDUG::Ou_suis_je()` and `WEAPON_TDDUG::Ou_suis_je()` are
//   byte-for-byte identical upstream (duplicated once per class) - ported as
//   one shared `tddugOuSuisJe()`.
// - `WEAPON_TDDUG::WEAPON_COLISION_TDDUG(WEAPON_TDDUG W_, uint8_t Nu_)` takes
//   its sibling weapon segment *by value* (a C++ copy) and, in the Nu_==0
//   branch, only ever calls `W_.PUT_ACTIVE(5)` on that local copy - a
//   guaranteed no-op with zero observable effect on the real caller, since
//   the mutation never escapes the copy. Confirmed genuinely dead upstream
//   (not just redundant) before dropping it - `tddugWeaponColision()` here
//   takes no such parameter at all, matching every other confirmed-dead-code
//   drop already made elsewhere in this project (e.g. Tiny Bike's own dead
//   sprite/scroll fields).
// - Upstream's main loop alternates `GD_DDUG.Skip_Frame` between two
//   halves every real loop iteration: case 0 (render + a genuine
//   millis()-based ~66ms busy-wait + the first-time/death-wait checks) and
//   case 1 (Trigger_adj_TDDUG + Check_Collision_TDDUG). Because case 0's own
//   wait dominates the real-time cost of *both* halves (case 1 runs "for
//   free" immediately after it), the true overall tick period is ~66ms
//   (~15.15Hz) - a genuine fixed real-hardware rate, not an AVR performance
//   compromise. Ported as ONE merged per-tick body (movement -> enemy AI ->
//   weapon -> tlEnemy -> Trigger_adj -> Check_Collision -> frame-select ->
//   render), gated by a single `TDDUG_TICK_DIVISOR` whole-function throttle
//   (matching the NumberPlace/HollowSeeker/t2048/Doc/Pacman "genuine rate"
//   precedent) - dropping the Skip_Frame split itself as redundant AVR-era
//   loop-shape rather than a felt gameplay timing (the same category of
//   drop already made for Pacman/Bomber's own FPS_Control+alternate-render
//   split). One real, minor, documented simplification results: the death
//   sequence's own DEAD 1->6 counter (which climbs by re-triggering
//   Check_Collision every tick while the frozen player still overlaps the
//   killing enemy) now advances at the tick rate directly rather than once
//   per *pair* of Skip_Frame halves, so the on-screen death animation plays
//   roughly 2x faster than upstream's real hardware. Not gameplay-critical
//   (a ~6-tick animation), not worth a second nested throttle layer.
// - The attract screen's own real rate (`_delay_ms(20)`, ~50fps) is a
//   *different*, faster rate than gameplay's ~15fps - both are folded into
//   the one shared TDDUG_TICK_DIVISOR for simplicity (matching this
//   project's own general preference for one whole-game throttle over
//   multiple independent per-screen rates); the practical effect is just a
//   slower attract-screen blink cycle than real hardware, not a gameplay
//   change.
// - `Tiny_Flip_TDDUG()`'s own `Maxx_` parameter (109 during real gameplay,
//   128 only for the attract screen) skips columns 109-127 during play,
//   the same real-SSD1306-VRAM-persistence assumption already found and
//   fixed in Pinball/Doc/Bert/Tris/Pipe - avoided proactively here by
//   always redrawing the full 128-wide, 8-row screen every tick.
//   Separately, upstream's own inner loop *explicitly* zeroes columns 0-18
//   every row (`if (x<19) {i2c_write(0x00);goto eend;}`) - this is NOT the
//   same bug (it's a real write of 0, not a skip) and is preserved exactly,
//   since the weapon sprite can legitimately extend a few columns left of
//   19 when firing left from near the tunnel's left edge (LAZER_TDDUG is 4
//   px wide, and weapon X can be as low as MS_.X()-4 with MS_.X() as low as
//   20) - upstream deliberately clips that overhang at the playfield's
//   left border rather than letting it bleed into the UI margin.
// - `RecupeLineY_TDDUG(int8_t Valeur){return Valeur>>3;}` and
//   `Moving_Sprite_TDDUG::Ou_suis_je()`'s own `y_=PY>>2` (PY can be
//   negative - e.g. a weapon Y of -4, or Ou_suis_je's own y_-8 with a
//   sprite Y under 8) both rely on AVR-GCC's arithmetic (sign-extending)
//   right shift on a signed operand; Vircon32's `>>` is documented as a
//   *logical* (zero-fill) shift - the same bug class already found in
//   HollowSeeker and Tiny Pipe's own RecupeLineY. Fixed with shared
//   `tddugSafeShiftDiv8()`/`tddugSafeShiftDiv4()` helpers (branch on sign,
//   `-((-val+N-1)>>k)` for negatives) used everywhere the original signed
//   shift could see a negative operand.
// - `RecupeDecalageY_TDDUG(uint8_t Valeur)` takes its parameter *by value
//   as uint8_t* upstream - any negative int8_t argument (e.g. a weapon Y of
//   -4, passed through `blitzSprite_TDDUG`'s own `RecupeDecalageY_TDDUG(yPos)`
//   call) is implicitly truncated to an unsigned byte *before* entering the
//   function body, matching this project's very first documented bug class
//   (md_drawColumn's own byte-truncation fix). Reproduced explicitly with
//   an `& 0xFF` mask at the top of `tddugRecupeDecalageY()` before the
//   modulo-8 arithmetic, since Vircon32 has no implicit narrowing.
// - Every other shift/OR-composited sprite byte (SplitSpriteDecalageY's own
//   `Input<<decalage`, which can set bits past bit 7 for decalage>0) needs
//   no *additional* masking here - the final composited byte always funnels
//   through the shared `md_drawColumn()`, which already masks `&0xFF`
//   centrally (this project's own original fix), and OR only ever sets
//   bits, never corrupts lower ones, so stray high bits from an
//   intermediate layer are safely discarded regardless of how far they
//   propagate through the composite chain.
// - EEPROM high-score persistence is dropped, matching every other port's
//   precedent (score tracked in-memory for the cartridge session only).
// - A real bug from the initial port, found via live user testing:
//   `tddugRenderFrame()`/`tddugRenderAttract()` never called `md_beginFrame()`
//   (every other game's own flip/render function does, at its very top) -
//   `md_drawColumn()` skips its draw call entirely whenever a column's
//   composited byte is 0, relying on `md_beginFrame()`'s own
//   `clear_screen()` having already blanked the frame - without it, every
//   "background" pixel DDug never explicitly draws just showed whatever
//   pixels were already on screen from earlier games/the menu, layering
//   new content on top of old across multiple unrelated screens instead of
//   a clean frame. Fixed by adding `md_beginFrame()` to the top of both
//   render entry points.
// - Sound: `Sound_TDDUG(freq,dur)` is upstream's own local PORTB bit-bang
//   tone generator, mathematically identical to ELECTROLIB.h's shared
//   Sound() (same square-wave-period formula) used by every other
//   Daniel-C game here - so DDug's own calls are ported straight onto the
//   shared tinyJoypadShim `Sound(freq,dur)` rather than reimplementing a
//   redundant local bit-bang. Every call site that fires more than one
//   Sound() synchronously (SND_TDDUG's 3 cases, the weapon-fire beep pair,
//   the ~50-call death-hit sweep) is routed through one shared generic
//   frame-stepped note sequencer (matching the Arkanoid/Missile/Minez/
//   Bike/Arena/Gilbert/Pipe/Morpion precedent) - none of these are large
//   enough (max 75 notes) to need downsampling, matching the "only truly
//   huge sweeps get downsampled" precedent. Only one sequence can play at
//   a time (Vircon32's audio channel has no queue) - a newly-started
//   sequence always overrides whatever was previously playing, the same
//   accepted simplification already established elsewhere.
// =============================================================================

#define TDDUG_MAX_ENEMY 4
#define TDDUG_MAIN_ACCEL_SPEED 50
#define TDDUG_SPRITE_ACCEL_SPEED 50
#define TDDUG_ENEMY_SPEED_STEP 25

// -----------------------------------------------------------------------------
//   Data tables (extracted + byte-diff verified against PictureTDDUG.h)
// -----------------------------------------------------------------------------

int[28] tddugENEMY_ENABLE_TDDUG =
{
1,1,0,0,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,
};

int[12] tddugENEMY_TDDUG =
{
28,16,0,92,40,1,92,16,0,36,48,1,
};

int[252] tddugLEVEL_TDDUG =
{
255,207,255,255,207,255,131,207,255,131,207,255,255,207,255,255,207,255,255,3,
255,255,3,207,255,255,207,255,255,207,255,255,207,255,255,255,255,207,255,255,
207,255,131,206,15,131,206,15,255,207,255,255,207,255,255,3,255,255,3,207,
255,255,207,255,255,207,255,255,207,255,255,255,255,207,255,255,207,255,207,207,
131,207,207,131,207,207,255,207,207,255,207,3,255,255,3,207,255,255,207,255,
255,207,224,255,207,224,255,255,255,207,159,255,207,159,207,207,131,207,207,131,
193,207,255,193,207,255,207,0,15,255,0,15,255,255,207,255,255,207,224,255,
207,224,255,255,255,207,255,255,207,255,131,207,131,131,207,131,207,207,255,207,
207,255,207,3,255,207,3,207,207,63,207,207,63,207,192,63,207,192,63,255,
207,207,255,207,207,255,131,207,131,131,207,131,255,207,207,255,207,207,252,3,
207,252,3,207,252,255,207,252,255,207,192,255,207,192,255,255,255,255,243,255,
255,243,128,15,195,128,15,195,255,207,255,255,207,255,3,3,207,3,3,207,
63,255,3,63,255,3,0,63,207,0,63,255,
};

int[16] tddugRnD_TDDUG =
{
-1,-1,1,-1,1,1,-1,-1,-1,1,1,1,-1,1,-1,1,
};

int[720] tddugTDDUG =
{
255,128,1,0,1,0,1,0,1,128,1,0,1,0,1,0,1,128,1,0,
1,0,1,0,1,128,1,0,1,0,1,0,1,128,1,0,1,0,1,0,
1,128,1,0,1,0,1,0,1,128,1,0,1,0,1,0,1,128,1,0,
1,0,1,0,1,128,1,0,1,0,1,0,1,128,1,0,1,0,1,0,
1,128,1,0,1,0,1,0,1,255,255,219,219,219,219,219,219,219,219,219,
219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,
219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,
219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,
219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,219,255,
255,182,86,182,166,182,86,182,166,182,86,182,166,182,86,182,166,182,86,182,
166,182,86,182,166,182,86,182,166,182,86,182,166,182,86,182,166,182,86,182,
166,182,86,182,166,182,86,182,166,182,86,182,166,182,86,182,166,182,86,182,
166,182,86,182,166,182,86,182,166,182,86,182,166,182,86,182,166,182,86,182,
166,182,86,182,166,182,86,182,166,255,255,109,85,109,42,109,85,109,42,109,
85,109,42,109,85,109,42,109,85,109,42,109,85,109,42,109,85,109,42,109,
85,109,42,109,85,109,42,109,85,109,42,109,85,109,42,109,85,109,42,109,
85,109,42,109,85,109,42,109,85,109,42,109,85,109,42,109,85,109,42,109,
85,109,42,109,85,109,42,109,85,109,42,109,85,109,42,109,85,109,42,255,
255,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,
81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,
81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,
81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,81,138,
81,138,81,138,81,138,81,138,81,255,255,130,20,130,20,130,20,130,20,130,
20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,
20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,
20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,
20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,130,20,255,
255,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,
4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,
4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,
4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,4,32,
4,32,4,32,4,32,4,32,4,255,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

int[138] tddugDig_TDDUG =
{
8,1,0,28,126,62,62,126,52,0,0,28,62,126,62,126,52,0,0,28,
62,126,62,62,116,0,0,60,126,62,126,124,36,0,0,60,126,62,126,124,
40,0,0,60,126,62,126,124,72,0,0,52,126,62,62,126,28,0,0,52,
126,62,126,62,28,0,0,116,62,62,126,62,28,0,0,60,126,124,126,62,
36,0,0,60,126,124,126,62,20,0,0,60,126,124,126,62,18,0,0,60,
126,126,126,126,60,0,0,60,120,112,112,120,60,0,0,56,112,96,96,112,
56,0,0,32,64,64,64,64,32,0,0,0,0,0,0,0,0,0,
};

int[42] tddugpolice_TDDUG =
{
4,1,124,68,124,0,0,124,0,0,116,84,92,0,68,84,124,0,28,16,
124,0,92,84,116,0,124,84,116,0,4,116,12,0,124,84,124,0,92,84,
124,0,
};

int[162] tddugSprite_ENEMY_TDDUG =
{
8,1,0,24,124,116,60,116,88,0,0,12,126,90,30,122,76,0,0,24,
52,60,28,52,24,0,0,28,50,62,30,50,28,0,14,255,253,217,31,253,
249,206,0,88,116,60,116,124,24,0,0,76,122,30,90,126,12,0,0,24,
52,60,28,52,24,0,0,28,50,62,30,50,28,0,206,249,253,31,217,253,
255,14,32,48,60,124,122,126,116,36,32,48,62,126,117,119,114,34,71,41,
50,20,20,50,41,71,56,86,82,164,164,82,86,56,120,126,254,245,247,243,
242,82,36,116,126,122,124,60,48,32,34,114,119,117,126,62,48,32,71,41,
50,20,20,50,41,71,56,86,82,164,164,82,86,56,82,242,243,247,245,254,
126,120,
};

int[10] tddugLAZER_TDDUG =
{
4,1,2,2,4,4,0,12,3,0,
};

int[16] tddugLIVE_TDDUG =
{
56,56,56,0,56,56,56,0,56,56,56,0,56,56,56,0,
};

int[142] tddugDDUG_INTRO_TDDUG =
{
35,4,0,252,250,250,2,250,250,254,254,238,10,254,254,254,6,246,246,246,
14,254,6,254,254,254,6,254,252,0,0,0,0,0,0,0,0,0,1,195,
195,162,227,67,67,3,251,254,15,239,239,222,63,255,15,238,239,223,54,246,
54,248,255,251,56,248,24,232,232,232,216,240,2,3,3,7,7,7,7,2,
0,15,159,208,215,215,219,156,31,16,23,23,27,28,31,24,23,23,23,144,
95,216,87,214,150,24,15,0,0,0,0,0,0,0,0,0,0,3,15,7,
7,15,6,0,2,2,1,1,2,4,4,2,0,0,9,15,3,11,15,1,
0,0,
};

int[27] tddugSTART_TDDUG =
{
25,1,128,146,165,169,146,128,129,191,129,129,190,133,133,190,128,191,133,133,
186,128,129,191,129,129,128,
};

// -----------------------------------------------------------------------------
//   Sound note sequences (generated programmatically from the exact upstream
//   for-loops, not hand-transcribed, matching this project's established
//   anti-transcription-error practice)
// -----------------------------------------------------------------------------

// SND_TDDUG case 0 (level-complete jingle): for(t=1;t<250;t+=10){Sound(t,4);Sound(0,15);Sound(255-t,2);}
int[150] tddugSnd0Notes =
{
1,4,0,15,254,2,11,4,0,15,244,2,21,4,0,15,234,2,31,4,
0,15,224,2,41,4,0,15,214,2,51,4,0,15,204,2,61,4,0,15,
194,2,71,4,0,15,184,2,81,4,0,15,174,2,91,4,0,15,164,2,
101,4,0,15,154,2,111,4,0,15,144,2,121,4,0,15,134,2,131,4,
0,15,124,2,141,4,0,15,114,2,151,4,0,15,104,2,161,4,0,15,
94,2,171,4,0,15,84,2,181,4,0,15,74,2,191,4,0,15,64,2,
201,4,0,15,54,2,211,4,0,15,44,2,221,4,0,15,34,2,231,4,
0,15,24,2,241,4,0,15,14,2,
};
#define TDDUG_SND0_COUNT 75

// SND_TDDUG case 1 (enemy zip-kill sound): for(t=1;t<100;t+=5)Sound(t,4)
int[40] tddugSnd1Notes =
{
1,4,6,4,11,4,16,4,21,4,26,4,31,4,36,4,41,4,46,4,
51,4,56,4,61,4,66,4,71,4,76,4,81,4,86,4,91,4,96,4,
};
#define TDDUG_SND1_COUNT 20

// SND_TDDUG case 2 (attract -> new game confirm): Sound(20,150);Sound(100,150);
int[4] tddugSnd2Notes =
{
20,150,100,150,
};
#define TDDUG_SND2_COUNT 2

// WEAPON_TDDUG::ADJUST_WEAPON's own fire beep: Sound(100,1);Sound(200,12);
int[4] tddugWeaponFireNotes =
{
100,1,200,12,
};
#define TDDUG_WEAPONFIRE_COUNT 2

// Check_Collision_TDDUG's death-hit sweep: for(t4=200;t4>2;t4-=4)Sound(t4,1)
int[100] tddugDeathSweepNotes =
{
200,1,196,1,192,1,188,1,184,1,180,1,176,1,172,1,168,1,164,1,
160,1,156,1,152,1,148,1,144,1,140,1,136,1,132,1,128,1,124,1,
120,1,116,1,112,1,108,1,104,1,100,1,96,1,92,1,88,1,84,1,
80,1,76,1,72,1,68,1,64,1,60,1,56,1,52,1,48,1,44,1,
40,1,36,1,32,1,28,1,24,1,20,1,16,1,12,1,8,1,4,1,
};
#define TDDUG_DEATHSWEEP_COUNT 50

// -----------------------------------------------------------------------------
//   Sprite / game-data structs (flattened from ClassTDDUG.h's class hierarchy)
// -----------------------------------------------------------------------------

struct TddugSprite
{
    int x;
    int y;
    int directionX;
    int directionY;
    int active;
    int somX;
    int somY;
    int sx;
    int sy;
    // Enemy_Sprite_TDDUG-only fields:
    int first;
    int type;
    int tracking;
    int animDirect;
    int anim;
    int bigZip;
    // WEAPON_TDDUG-only field:
    int animOr;
};

struct TddugGameData
{
    int timeLaps;
    int oneTime;
    int counter;
    int triggerCounter;
    int mainFrame;
    int mainAnimFrame;
    int mainAnim;
    int directionAnim;
    int scores;
    int level;
    int goOut;
    int nospriteGoOut;
    int dead;
    int live;
    int liveComp;
};

TddugSprite tddugMain;
TddugSprite[2] tddugWeapon;
TddugSprite[4] tddugEnemy;
TddugGameData tddugGD;
int[12][3] tddugGrid;

int tddugRD;
int tddugM10000;
int tddugM1000;
int tddugM100;
int tddugM10;
int tddugM1;
int tddugAnimEnemy;
int tddugAnimEnemyFrame;
int tddugMainSpeedStep;

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define TDDUG_STATE_ATTRACT 0
#define TDDUG_STATE_LEVEL_INTRO_WAIT 1
#define TDDUG_STATE_PLAYING 2
#define TDDUG_STATE_DEATH_WAIT 3
#define TDDUG_STATE_LEVEL_CLEAR_WAIT 4

// Originally 4 (matching upstream's genuine ~66ms/~15fps hardware rate,
// see the header comment above), briefly tried at 1 (native 60fps, no
// throttle), settled on 2 (30fps) per direct user request - the same
// "faster feels nicer than historically-accurate" call already made for
// t2048 elsewhere in this project, just not pushed all the way to
// uncapped. Every tick-counted constant (tddugWaitFrames, triggerCounter,
// etc) is left unchanged rather than rescaled - same reasoning as
// Tiny SQuest's own throttle: one divisor is the single source of truth
// for real-world timing, so wait states are proportionally shorter in
// real time too (e.g. the ~1s level-intro/death pause is now ~0.5s at
// divisor 2), not just movement/AI.
#define TDDUG_TICK_DIVISOR 2

int tddugState;
int tddugTickSkipCounter;
int tddugAttractT;
int tddugAttractFireHeld;
int tddugWaitFrames;

// -----------------------------------------------------------------------------
//   Sound sequencer (shared, one active sequence at a time)
// -----------------------------------------------------------------------------

int tddugSeqActive;
int* tddugSeqNotes;
int tddugSeqCount;
int tddugSeqIndex;
int tddugSeqWaitFrames;

void tddugStartNoteSeq( int* notes, int count )
{
    tddugSeqNotes = notes;
    tddugSeqCount = count;
    tddugSeqIndex = 0;
    tddugSeqActive = 1;
    tddugSeqWaitFrames = 0;
}

void tddugAdvanceNoteSeq()
{
    if( !tddugSeqActive )
        return;

    if( tddugSeqWaitFrames > 0 )
    {
        tddugSeqWaitFrames--;
        return;
    }

    if( tddugSeqIndex >= tddugSeqCount )
    {
        tddugSeqActive = 0;
        return;
    }

    int freq = tddugSeqNotes[ tddugSeqIndex * 2 ];
    int dur = tddugSeqNotes[ tddugSeqIndex * 2 + 1 ];
    Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;

    tddugSeqWaitFrames = waitFrames;
    tddugSeqIndex++;
}

// -----------------------------------------------------------------------------
//   Safe signed-shift helpers (logical-vs-arithmetic-shift-on-negative-operand
//   bug class - see header comment)
// -----------------------------------------------------------------------------

int tddugSafeShiftDiv4( int val )
{
    if( val >= 0 )
        return val >> 2;
    return -( ( -val + 3 ) >> 2 );
}

int tddugSafeShiftDiv8( int val )
{
    if( val >= 0 )
        return val >> 3;
    return -( ( -val + 7 ) >> 3 );
}

// -----------------------------------------------------------------------------
//   Grid helpers
// -----------------------------------------------------------------------------

int tddugRecupeDecalageY( int val )
{
    // Matches uint8_t-parameter implicit truncation upstream (see header
    // comment) before the safe (now guaranteed non-negative) modulo-8 math.
    val = val & 255;
    return val - ( ( val >> 3 ) << 3 );
}

int tddugRecupeLineY( int val )
{
    return tddugSafeShiftDiv8( val );
}

void tddugOuSuisJe( int* x, int* y )
{
    int px = *x - 20;
    int py = *y - 8;
    // px is normally >=0 (main/enemy X is always clamped >=20), but a
    // weapon fired left from right against the left wall can make px
    // slightly negative (WEAPON_TDDUG::Ou_suis_je's own uint8_t X
    // parameter would silently wrap upstream) - use the same safe
    // signed-shift helper as py rather than assuming px is non-negative.
    *x = tddugSafeShiftDiv4( px );
    *y = tddugSafeShiftDiv4( py );
}

int tddugReadGrid( int x, int y )
{
    if( x > 21 ) return 1;
    if( x < 0 ) return 1;
    if( y > 11 ) return 1;
    if( y < 0 ) return 0;
    if( ( ( 128 >> tddugRecupeDecalageY( x ) ) & tddugGrid[y][ x >> 3 ] ) != 0 )
        return 1;
    return 0;
}

int tddugWriteGrid( int x, int y )
{
    if( x > 21 ) return 1;
    if( x < 0 ) return 1;
    if( y > 11 ) return 1;
    if( y < 0 ) return 1;
    tddugGrid[y][ x >> 3 ] = tddugGrid[y][ x >> 3 ] & ( 255 - ( 128 >> tddugRecupeDecalageY( x ) ) );
    return 0;
}

int tddugRnd()
{
    if( tddugRD < 15 ) tddugRD = tddugRD + 1; else tddugRD = 0;
    return tddugRnD_TDDUG[ tddugRD ];
}

// -----------------------------------------------------------------------------
//   Score / lives display
// -----------------------------------------------------------------------------

void tddugAdjustLiveComp()
{
    if( tddugGD.live == 0 ) tddugGD.liveComp = 19;
    else if( tddugGD.live == 1 ) tddugGD.liveComp = 23;
    else if( tddugGD.live == 2 ) tddugGD.liveComp = 27;
    else if( tddugGD.live == 3 ) tddugGD.liveComp = 31;
    else if( tddugGD.live == 4 ) tddugGD.liveComp = 34;
}

void tddugCompilSco()
{
    tddugAdjustLiveComp();
    tddugM10000 = tddugGD.scores / 10000;
    tddugM1000 = ( tddugGD.scores - ( tddugM10000 * 10000 ) ) / 1000;
    tddugM100 = ( tddugGD.scores - ( tddugM1000 * 1000 ) - ( tddugM10000 * 10000 ) ) / 100;
    tddugM10 = ( tddugGD.scores - ( tddugM100 * 100 ) - ( tddugM1000 * 1000 ) - ( tddugM10000 * 10000 ) ) / 10;
    tddugM1 = tddugGD.scores - ( tddugM10 * 10 ) - ( tddugM100 * 100 ) - ( tddugM1000 * 1000 ) - ( tddugM10000 * 10000 );
}

void tddugScoresAdd()
{
    tddugGD.scores++;
    tddugCompilSco();
}

// -----------------------------------------------------------------------------
//   Enemy movement / AI
// -----------------------------------------------------------------------------

void tddugEnemyNewLimiteDirection( TddugSprite* e, int dir )
{
    if( dir == 0 )
    {
        if( e->directionX == 1 ) e->directionX = -1; else e->directionX = 1;
    }
    else if( dir == 1 )
    {
        if( e->directionY == 1 ) e->directionY = -1; else e->directionY = 1;
    }
}

void tddugEnemyNewDirection( TddugSprite* e, int dir )
{
    if( dir == 0 )
    {
        if( tddugRecupeDecalageY( e->x - 20 ) != 0 ) e->directionY = tddugRnd();
    }
    else if( dir == 1 )
    {
        if( tddugRecupeDecalageY( e->y ) != 0 ) e->directionX = tddugRnd();
    }
}

int tddugEGridUpdateRight( TddugSprite* e )
{
    int xx = e->x + 7;
    int yy = e->y;
    tddugOuSuisJe( &xx, &yy );
    if( tddugReadGrid( xx, yy ) == 1 ) return e->tracking;
    if( tddugReadGrid( xx, yy + 1 ) == 1 ) return e->tracking;
    return 0;
}

int tddugEGridUpdateLeft( TddugSprite* e )
{
    int xx = e->x;
    int yy = e->y;
    tddugOuSuisJe( &xx, &yy );
    if( tddugReadGrid( xx, yy ) == 1 ) return e->tracking;
    if( tddugReadGrid( xx, yy + 1 ) == 1 ) return e->tracking;
    return 0;
}

int tddugEGridUpdateUp( TddugSprite* e )
{
    int xx = e->x;
    int yy = e->y;
    tddugOuSuisJe( &xx, &yy );
    if( tddugReadGrid( xx, yy ) == 1 ) return e->tracking;
    if( tddugReadGrid( xx + 1, yy ) == 1 ) return e->tracking;
    return 0;
}

int tddugEGridUpdateDown( TddugSprite* e )
{
    int xx = e->x;
    int yy = e->y + 7;
    tddugOuSuisJe( &xx, &yy );
    if( tddugReadGrid( xx, yy ) == 1 ) return e->tracking;
    if( tddugReadGrid( xx + 1, yy ) == 1 ) return e->tracking;
    return 0;
}

void tddugEnemyHaut( TddugSprite* e )
{
    int sy = e->sy, y = e->y, somy = e->somY;
    int tSomY = e->somY;
    if( ( tSomY - TDDUG_ENEMY_SPEED_STEP ) <= -TDDUG_SPRITE_ACCEL_SPEED )
    {
        e->somY = ( tSomY - TDDUG_ENEMY_SPEED_STEP ) + TDDUG_SPRITE_ACCEL_SPEED;
        e->y = e->y - 1;
    }
    else
    {
        e->somY = tSomY - TDDUG_ENEMY_SPEED_STEP;
    }
    if( tddugEGridUpdateUp( e ) || ( e->y < 0 ) )
    {
        e->sy = sy; e->y = y; e->somY = somy;
        tddugEnemyNewLimiteDirection( e, 1 );
    }
    else
    {
        tddugEnemyNewDirection( e, 1 );
        e->directionY = -1;
    }
}

void tddugEnemyDroite( TddugSprite* e )
{
    int sx = e->sx, x = e->x, somx = e->somX;
    int tSomX = e->somX;
    if( ( tSomX + TDDUG_ENEMY_SPEED_STEP ) >= TDDUG_SPRITE_ACCEL_SPEED )
    {
        e->somX = ( tSomX + TDDUG_ENEMY_SPEED_STEP ) - TDDUG_SPRITE_ACCEL_SPEED;
        e->x = e->x + 1;
    }
    else
    {
        e->somX = tSomX + TDDUG_ENEMY_SPEED_STEP;
    }
    if( tddugEGridUpdateRight( e ) || ( e->x > 100 ) )
    {
        e->sx = sx; e->x = x; e->somX = somx;
        tddugEnemyNewLimiteDirection( e, 0 );
    }
    else
    {
        tddugEnemyNewDirection( e, 0 );
        e->directionX = 1;
    }
    if( e->x != x ) e->animDirect = 1;
}

void tddugEnemyBas( TddugSprite* e )
{
    int sy = e->sy, y = e->y, somy = e->somY;
    int tSomY = e->somY;
    if( ( tSomY + TDDUG_ENEMY_SPEED_STEP ) >= TDDUG_SPRITE_ACCEL_SPEED )
    {
        e->somY = ( tSomY + TDDUG_ENEMY_SPEED_STEP ) - TDDUG_SPRITE_ACCEL_SPEED;
        e->y = e->y + 1;
    }
    else
    {
        e->somY = tSomY + TDDUG_ENEMY_SPEED_STEP;
    }
    if( tddugEGridUpdateDown( e ) || ( e->y > 48 ) )
    {
        e->sy = sy; e->y = y; e->somY = somy;
        tddugEnemyNewLimiteDirection( e, 1 );
    }
    else
    {
        tddugEnemyNewDirection( e, 1 );
        e->directionY = 1;
    }
}

void tddugEnemyGauche( TddugSprite* e )
{
    int sx = e->sx, x = e->x, somx = e->somX;
    int tSomX = e->somX;
    if( ( tSomX - TDDUG_ENEMY_SPEED_STEP ) <= -TDDUG_SPRITE_ACCEL_SPEED )
    {
        e->somX = ( tSomX - TDDUG_ENEMY_SPEED_STEP ) + TDDUG_SPRITE_ACCEL_SPEED;
        e->x = e->x - 1;
    }
    else
    {
        e->somX = tSomX - TDDUG_ENEMY_SPEED_STEP;
    }
    if( tddugEGridUpdateLeft( e ) || ( e->x < 20 ) )
    {
        e->sx = sx; e->x = x; e->somX = somx;
        tddugEnemyNewLimiteDirection( e, 0 );
    }
    else
    {
        tddugEnemyNewDirection( e, 0 );
        e->directionX = -1;
    }
    if( e->x != x ) e->animDirect = 0;
}

int tddugTrackX( int t )
{
    TddugSprite* e = &tddugEnemy[t];
    if( e->tracking == 1 )
        return e->directionX;
    if( tddugRecupeDecalageY( e->x - 20 ) == 0 )
    {
        int targetX;
        if( tddugGD.goOut == 0 ) targetX = tddugMain.x; else targetX = 20;
        if( e->x < targetX ) return 1;
        if( e->x > targetX ) return -1;
        return 0;
    }
    return e->directionX;
}

int tddugTrackY( int t )
{
    TddugSprite* e = &tddugEnemy[t];
    if( e->tracking == 1 )
        return e->directionY;
    if( tddugRecupeDecalageY( e->y ) == 0 )
    {
        int targetY;
        if( tddugGD.goOut == 0 ) targetY = tddugMain.y; else targetY = 0;
        if( e->y < targetY ) return 1;
        if( e->y > targetY ) return -1;
        return 0;
    }
    return e->directionY;
}

int tddugRecupeEnemyBig( int spr )
{
    TddugSprite* e = &tddugEnemy[spr];
    int add;
    if( e->tracking == 0 && e->bigZip == 0 ) add = 2; else add = 0;
    int bz = e->bigZip;
    if( bz == 0 ) return tddugAnimEnemyFrame + add;
    if( bz >= 1 && bz <= 9 ) return 0;
    if( bz >= 10 && bz <= 19 ) return 1;
    if( bz >= 20 && bz <= 29 ) return 4;
    if( bz == 30 )
    {
        e->bigZip = 0;
        e->active = 0;
        tddugStartNoteSeq( tddugSnd1Notes, TDDUG_SND1_COUNT );
        tddugWeapon[0].active = 5;
        tddugGD.scores += 5;
        tddugCompilSco();
        return 0;
    }
    return e->anim;
}

int tddugRecupeEnemyFrame( int spr )
{
    TddugSprite* e = &tddugEnemy[spr];
    int addType;
    if( e->type == 0 ) addType = 0; else addType = 10;
    int out = tddugRecupeEnemyBig( spr ) + addType;
    if( e->animDirect == 1 ) out += 0; else out += 5;
    return out;
}

void tddugColapseEnemyAnim()
{
    int t;
    for( t = 0; t < TDDUG_MAX_ENEMY; t++ )
        tddugEnemy[t].anim = tddugRecupeEnemyFrame( t );
}

void tddugUpdateEnemy()
{
    int t;
    for( t = 0; t < TDDUG_MAX_ENEMY; t++ )
    {
        TddugSprite* e = &tddugEnemy[t];
        if( e->active != 0 && e->bigZip == 0 )
        {
            if( tddugRecupeDecalageY( e->y ) == 0 )
            {
                int tx = tddugTrackX( t );
                if( tx == 1 ) tddugEnemyDroite( e );
                else if( tx == -1 ) tddugEnemyGauche( e );
            }
            if( tddugRecupeDecalageY( e->x - 20 ) == 0 )
            {
                int ty = tddugTrackY( t );
                if( ty == 1 ) tddugEnemyBas( e );
                else if( ty == -1 ) tddugEnemyHaut( e );
            }
        }
    }
    if( tddugAnimEnemy < 3 )
        tddugAnimEnemy++;
    else
    {
        if( tddugAnimEnemyFrame == 0 ) tddugAnimEnemyFrame = 1; else tddugAnimEnemyFrame = 0;
        tddugAnimEnemy = 0;
    }
    tddugColapseEnemyAnim();
}

void tddugTlEnemy()
{
    int t;
    if( tddugGD.timeLaps < 2 )
    {
        tddugGD.timeLaps += 1;
    }
    else
    {
        tddugGD.timeLaps = 0;
        for( t = 0; t < TDDUG_MAX_ENEMY; t++ )
            if( tddugEnemy[t].bigZip > 0 ) tddugEnemy[t].bigZip = tddugEnemy[t].bigZip - 1; else tddugEnemy[t].bigZip = 0;
    }
}

void tddugTriggerAdj()
{
    tddugGD.counter++;
    if( tddugGD.counter > tddugGD.triggerCounter )
    {
        tddugGD.counter = 0;
        int t;
        for( t = 0; t < TDDUG_MAX_ENEMY; t++ )
        {
            if( tddugEnemy[t].active )
            {
                if( tddugEnemy[t].tracking == 1 )
                {
                    tddugEnemy[t].tracking = 0;
                    break;
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
//   Weapon (tddugResetWeapon defined here, ahead of Collision below, since
//   tddugCheckCollision needs it - single-pass compiler, no forward decls)
// -----------------------------------------------------------------------------

void tddugResetWeapon()
{
    tddugWeapon[0].active = 0;
    tddugWeapon[1].active = 0;
}

// -----------------------------------------------------------------------------
//   Collision
// -----------------------------------------------------------------------------

int tddugUniversal( int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh )
{
    if( ax > ( bx + bw ) ) return 0;
    if( ( ax + aw ) < bx ) return 0;
    if( ay > ( by + bh ) ) return 0;
    if( ( ay + ah ) < by ) return 0;
    return 1;
}

void tddugCheckCollision()
{
    int t;
    int dx = tddugMain.x + 1;
    int dy = tddugMain.y + 1;
    for( t = 0; t < TDDUG_MAX_ENEMY; t++ )
    {
        TddugSprite* e = &tddugEnemy[t];
        if( e->active == 1 && e->bigZip == 0 )
        {
            if( tddugUniversal( dx, dy, 5, 5, e->x, e->y, 7, 7 ) )
            {
                tddugStartNoteSeq( tddugDeathSweepNotes, TDDUG_DEATHSWEEP_COUNT );
                tddugResetWeapon();
                tddugGD.dead++;
            }
        }
    }
}

int tddugCheckBalisticColid()
{
    int oneAdd = 0;
    int t1, t2, t3;
    int pass = 0;
    for( t1 = 0; t1 < TDDUG_MAX_ENEMY; t1++ )
    {
        if( tddugEnemy[t1].first == 1 )
            pass = 1;
    }
    for( t3 = 0; t3 < TDDUG_MAX_ENEMY; t3++ )
    {
        TddugSprite* e = &tddugEnemy[t3];
        for( t2 = 0; t2 < 2; t2++ )
        {
            TddugSprite* w = &tddugWeapon[t2];
            if( w->active && e->active )
            {
                int bx = w->x + 1;
                int by = w->y + 1;
                if( tddugUniversal( e->x, e->y, 7, 7, bx, by, 1, 1 ) )
                {
                    if( pass == 0 ) e->first = 1;
                    if( e->first )
                    {
                        tddugWeapon[0].active = 4;
                        if( oneAdd == 0 )
                        {
                            if( e->bigZip < 30 ) e->bigZip = e->bigZip + 1;
                            oneAdd++;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
//   Weapon (continued)
// -----------------------------------------------------------------------------

void tddugWeaponColision( TddugSprite* w, int nu )
{
    int xx = w->x + 2;
    int yy = w->y + 2;
    tddugOuSuisJe( &xx, &yy );
    if( tddugReadGrid( xx, yy ) == 1 )
    {
        if( nu != 0 )
            w->active = 0;
    }
}

void tddugAdjustWeapon( TddugSprite* w, TddugSprite* m )
{
    tddugStartNoteSeq( tddugWeaponFireNotes, TDDUG_WEAPONFIRE_COUNT );
    if( tddugGD.directionAnim == 0 ) { w->animOr = 0; w->x = m->x + 8; w->y = m->y + 2; }
    else if( tddugGD.directionAnim == 1 ) { w->animOr = 1; w->x = m->x + 2; w->y = m->y + 8; }
    else if( tddugGD.directionAnim == 2 ) { w->animOr = 0; w->x = m->x - 4; w->y = m->y + 2; }
    else if( tddugGD.directionAnim == 3 ) { w->animOr = 1; w->x = m->x + 2; w->y = m->y - 4; }
    tddugWeaponColision( w, 0 );
}

void tddugAdjustWeapon2( TddugSprite* w, TddugSprite* other, TddugSprite* m )
{
    // `other` matches upstream's own WEAPON_TDDUG::ADJUST_WEAPON2(WEAPON_TDDUG&
    // W_, ...) parameter, confirmed genuinely unused there too (see this
    // file's own header comment on WEAPON_COLISION_TDDUG's dead by-value
    // parameter) - kept here only so the call site's signature stays
    // self-documenting about which weapon segment pair is involved.
    // Self-assigned to silence the unused-parameter warning, since this
    // dialect has no (void)param; idiom.
    other = other;
    int a, b, c;
    if( tddugGD.directionAnim == 0 ) { a = 0; b = m->x + 12; c = m->y + 2; }
    else if( tddugGD.directionAnim == 1 ) { a = 1; b = m->x + 2; c = m->y + 12; }
    else if( tddugGD.directionAnim == 2 ) { a = 0; b = m->x - 8; c = m->y + 2; }
    else if( tddugGD.directionAnim == 3 ) { a = 1; b = m->x + 2; c = m->y - 8; }
    else { a = 0; b = 0; c = 0; }
    w->animOr = a; w->x = b; w->y = c;
    tddugWeaponColision( w, 1 );
}

// -----------------------------------------------------------------------------
//   Main sprite movement / digging
// -----------------------------------------------------------------------------

void tddugAnimUpdateMain( int direct )
{
    if( tddugGD.mainAnim < 4 )
        tddugGD.mainAnim++;
    else
    {
        tddugGD.mainAnim = 0;
        if( tddugGD.mainAnimFrame < 2 ) tddugGD.mainAnimFrame = tddugGD.mainAnimFrame + 1; else tddugGD.mainAnimFrame = 0;
    }
    tddugGD.directionAnim = direct;
}

void tddugGridUpdateRight( TddugSprite* m )
{
    int snd = 0;
    int xx = m->x + 7;
    int yy = m->y;
    tddugOuSuisJe( &xx, &yy );
    if( tddugReadGrid( xx, yy ) == 1 ) { snd = 1; tddugScoresAdd(); tddugWriteGrid( xx, yy ); }
    if( tddugReadGrid( xx, yy + 1 ) == 1 ) { snd = 1; tddugScoresAdd(); tddugWriteGrid( xx, yy + 1 ); }
    if( snd ) Sound( 200, 1 );
}

void tddugGridUpdateLeft( TddugSprite* m )
{
    int snd = 0;
    int xx = m->x;
    int yy = m->y;
    tddugOuSuisJe( &xx, &yy );
    if( tddugReadGrid( xx, yy ) == 1 ) { snd = 1; tddugScoresAdd(); tddugWriteGrid( xx, yy ); }
    if( tddugReadGrid( xx, yy + 1 ) == 1 ) { snd = 1; tddugScoresAdd(); tddugWriteGrid( xx, yy + 1 ); }
    if( snd ) Sound( 200, 1 );
}

void tddugGridUpdateUp( TddugSprite* m )
{
    int snd = 0;
    int xx = m->x;
    int yy = m->y;
    tddugOuSuisJe( &xx, &yy );
    if( tddugReadGrid( xx, yy ) == 1 ) { snd = 1; tddugScoresAdd(); tddugWriteGrid( xx, yy ); }
    if( tddugReadGrid( xx + 1, yy ) == 1 ) { snd = 1; tddugScoresAdd(); tddugWriteGrid( xx + 1, yy ); }
    if( snd ) Sound( 200, 1 );
}

void tddugGridUpdateDown( TddugSprite* m )
{
    int snd = 0;
    int xx = m->x;
    int yy = m->y + 7;
    tddugOuSuisJe( &xx, &yy );
    if( tddugReadGrid( xx, yy ) == 1 ) { snd = 1; tddugScoresAdd(); tddugWriteGrid( xx, yy ); }
    if( tddugReadGrid( xx + 1, yy ) == 1 ) { snd = 1; tddugScoresAdd(); tddugWriteGrid( xx + 1, yy ); }
    if( snd ) Sound( 200, 1 );
}

void tddugMHaut( TddugSprite* m )
{
    int tSomY = m->somY;
    if( ( tSomY - tddugMainSpeedStep ) <= -TDDUG_MAIN_ACCEL_SPEED )
    {
        m->somY = ( tSomY - tddugMainSpeedStep ) + TDDUG_MAIN_ACCEL_SPEED;
        m->y = m->y - 1;
    }
    else
    {
        m->somY = tSomY - tddugMainSpeedStep;
    }
    tddugAnimUpdateMain( 3 );
    if( m->y < 0 ) { m->y = 0; m->sy = 0; m->somY = 0; }
    m->directionY = -1;
    tddugGridUpdateUp( m );
}

void tddugMDroite( TddugSprite* m )
{
    int tSomX = m->somX;
    if( ( tSomX + tddugMainSpeedStep ) >= TDDUG_MAIN_ACCEL_SPEED )
    {
        m->somX = ( tSomX + tddugMainSpeedStep ) - TDDUG_MAIN_ACCEL_SPEED;
        m->x = m->x + 1;
    }
    else
    {
        m->somX = tSomX + tddugMainSpeedStep;
    }
    tddugAnimUpdateMain( 0 );
    if( m->x > 100 ) { m->x = 100; m->sx = 0; m->somX = 0; }
    m->directionX = 1;
    tddugGridUpdateRight( m );
}

void tddugMBas( TddugSprite* m )
{
    int tSomY = m->somY;
    if( ( tSomY + tddugMainSpeedStep ) >= TDDUG_MAIN_ACCEL_SPEED )
    {
        m->somY = ( tSomY + tddugMainSpeedStep ) - TDDUG_MAIN_ACCEL_SPEED;
        m->y = m->y + 1;
    }
    else
    {
        m->somY = tSomY + tddugMainSpeedStep;
    }
    tddugAnimUpdateMain( 1 );
    if( m->y > 48 ) { m->y = 48; m->sy = 0; m->somY = 0; }
    m->directionY = 1;
    tddugGridUpdateDown( m );
}

void tddugMGauche( TddugSprite* m )
{
    int tSomX = m->somX;
    if( ( tSomX - tddugMainSpeedStep ) <= -TDDUG_MAIN_ACCEL_SPEED )
    {
        m->somX = ( tSomX - tddugMainSpeedStep ) + TDDUG_MAIN_ACCEL_SPEED;
        m->x = m->x - 1;
    }
    else
    {
        m->somX = tSomX - tddugMainSpeedStep;
    }
    tddugAnimUpdateMain( 2 );
    if( m->x < 20 ) { m->x = 20; m->sx = 0; m->somX = 0; }
    m->directionX = -1;
    tddugGridUpdateLeft( m );
}

void tddugWalkRight( TddugSprite* m )
{
    if( tddugRecupeDecalageY( m->y ) == 0 )
    {
        tddugMDroite( m );
    }
    else
    {
        if( m->directionY == 1 ) tddugMBas( m );
        else if( m->directionY == -1 ) tddugMHaut( m );
    }
}

void tddugWalkLeft( TddugSprite* m )
{
    if( tddugRecupeDecalageY( m->y ) == 0 )
    {
        tddugMGauche( m );
    }
    else
    {
        if( m->directionY == 1 ) tddugMBas( m );
        else if( m->directionY == -1 ) tddugMHaut( m );
    }
}

void tddugWalkUp( TddugSprite* m )
{
    if( tddugRecupeDecalageY( m->x - 20 ) == 0 )
    {
        tddugMHaut( m );
    }
    else
    {
        if( m->directionX == 1 ) tddugMDroite( m );
        else if( m->directionX == -1 ) tddugMGauche( m );
    }
}

void tddugWalkDown( TddugSprite* m )
{
    if( tddugRecupeDecalageY( m->x - 20 ) == 0 )
    {
        tddugMBas( m );
    }
    else
    {
        if( m->directionX == 1 ) tddugMDroite( m );
        else if( m->directionX == -1 ) tddugMGauche( m );
    }
}

void tddugAdjustMainSpeed( int rt )
{
    if( rt == 9 || rt == 3 || rt == 6 || rt == 12 )
        tddugMainSpeedStep = 13;
    else
        tddugMainSpeedStep = 25;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int tddugSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown )
        return input << decalage;
    return input >> ( 8 - decalage );
}

int tddugBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = tddugRecupeLineY( yPos );
    if( ( xPass > ( xPos + ( wSprite - 1 ) ) ) || ( xPass < xPos ) || ( recupeLineY > yPass ) || ( ( recupeLineY + hSprite ) < yPass ) )
        return 0;
    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = tddugRecupeDecalageY( yPos );
    int scanA = ( ( xPass - xPos ) + ( spriteYLine * wSprite ) ) + 2;
    int scanB = ( ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) ) + 2;
    int outByte;
    if( scanA > wMax ) outByte = 0;
    else outByte = tddugSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );
    if( spriteYLine > 0 )
    {
        int outByte2 = tddugSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

int tddugSpeedBlitz( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    if( ( xPass > ( xPos + ( wSprite - 1 ) ) ) || ( xPass < xPos ) || ( yPass < yPos ) || ( yPass > ( yPos + ( hSprite - 1 ) ) ) )
        return 0;
    return sprites[ 2 + ( ( xPass - xPos ) + ( ( yPass - yPos ) * wSprite ) ) + ( frame * ( hSprite * wSprite ) ) ];
}

int tddugBackgroundData( int xPass, int yPass )
{
    if( xPass < 19 ) return 0;
    if( xPass > 108 ) return 0;
    return tddugTDDUG[ ( xPass - 19 ) + ( yPass * 90 ) ];
}

// The tunnel wall mask only actually changes when a grid cell is dug (once
// every several ticks at most, via tddugWriteGrid), but was being
// recomputed via 2 tddugReadGrid() calls (each its own nested call into
// tddugRecupeDecalageY()) for every one of the ~88x6 tunnel-region pixels
// every tick - up to ~1056 grid reads/tick, even though every 4 consecutive
// columns share the same gridX and therefore the same 2 reads. Cached once
// per row instead (22 gridX values x 2 reads = 44 calls/row) rather than
// per pixel.
int[22] tddugBackWallCache;

void tddugRefreshBackWallCache( int yPass )
{
    if( yPass < 1 || yPass > 6 )
        return;
    int ry = ( yPass - 1 ) * 2;
    int gx;
    for( gx = 0; gx < 22; gx++ )
    {
        int outComp = 0;
        if( tddugReadGrid( gx, ry ) == 1 ) outComp = 15;
        if( tddugReadGrid( gx, ry + 1 ) == 1 ) outComp = outComp | 240;
        tddugBackWallCache[gx] = outComp;
    }
}

int tddugBack( int xPass, int yPass )
{
    if( xPass < 20 ) return tddugBackgroundData( xPass, yPass );
    if( xPass > 107 ) return tddugBackgroundData( xPass, yPass );
    if( yPass < 1 ) return tddugBackgroundData( xPass, yPass );
    if( yPass > 6 ) return tddugBackgroundData( xPass, yPass );
    int gridX = ( xPass - 20 ) / 4;
    return tddugBackWallCache[gridX] & tddugBackgroundData( xPass, yPass );
}

int tddugMainSprite( int xPass, int yPass )
{
    return tddugBlitzSprite( tddugMain.x, tddugMain.y, xPass, yPass, tddugGD.mainFrame, tddugDig_TDDUG );
}

// Composited once per page row (not per pixel) into a shared buffer, over
// only each object's own narrow column footprint - the same "self-gated
// call still costs a full call every time it's invoked" fix already
// applied to Tiny Bike/Arena/Missile/Pipe's own sprite loops. With up to
// 4 enemies + 2 weapon segments each potentially scanned across all 128
// columns x 8 rows regardless of where they actually are, this cuts the
// call count from ~6144 blitzSprite calls/tick worst case down to a
// couple hundred at most.
int[128] tddugEnemyPageBuffer;
int[128] tddugWeaponPageBuffer;

void tddugCompositeEnemyRow( int yPass )
{
    int i;
    for( i = 0; i < 128; i++ )
        tddugEnemyPageBuffer[i] = 0;
    int t, x;
    for( t = 0; t < TDDUG_MAX_ENEMY; t++ )
    {
        if( tddugEnemy[t].active == 0 )
            continue;
        int recupeLineY = tddugRecupeLineY( tddugEnemy[t].y );
        // Sprite_ENEMY_TDDUG's own header is "8,1" (WSPRITE=8, HSPRITE=1
        // page) - matches blitzSprite's own [recupeLineY, recupeLineY+
        // HSPRITE] bound check exactly.
        if( recupeLineY > yPass || ( recupeLineY + 1 ) < yPass )
            continue;
        int xStart = tddugEnemy[t].x;
        int xEnd = tddugEnemy[t].x + 7;
        if( xStart < 0 ) xStart = 0;
        if( xEnd > 127 ) xEnd = 127;
        for( x = xStart; x <= xEnd; x++ )
            tddugEnemyPageBuffer[x] = tddugEnemyPageBuffer[x] | tddugBlitzSprite( tddugEnemy[t].x, tddugEnemy[t].y, x, yPass, tddugEnemy[t].anim, tddugSprite_ENEMY_TDDUG );
    }
}

void tddugCompositeWeaponRow( int yPass )
{
    int i;
    for( i = 0; i < 128; i++ )
        tddugWeaponPageBuffer[i] = 0;
    if( tddugWeapon[0].active == 0 || tddugWeapon[0].active == 5 )
        return;
    int t, x;
    for( t = 0; t < 2; t++ )
    {
        if( t == 1 && tddugWeapon[1].active == 0 )
            continue;
        int wx = tddugWeapon[t].x;
        int wy = tddugWeapon[t].y;
        int recupeLineY = tddugRecupeLineY( wy );
        // LAZER_TDDUG's own header is "4,1" (WSPRITE=4, HSPRITE=1 page).
        if( recupeLineY > yPass || ( recupeLineY + 1 ) < yPass )
            continue;
        int xStart = wx;
        int xEnd = wx + 3;
        if( xStart < 0 ) xStart = 0;
        if( xEnd > 127 ) xEnd = 127;
        for( x = xStart; x <= xEnd; x++ )
            tddugWeaponPageBuffer[x] = tddugWeaponPageBuffer[x] | tddugBlitzSprite( wx, wy, x, yPass, tddugWeapon[0].animOr, tddugLAZER_TDDUG );
    }
}

int tddugRecupeLive( int xPass, int yPass )
{
    if( yPass < 7 ) return 0;
    if( xPass < 20 ) return 0;
    if( xPass > tddugGD.liveComp ) return 0;
    return tddugLIVE_TDDUG[ xPass - 20 ];
}

int tddugRecupeScores( int xPass, int yPass, int pos )
{
    if( xPass < ( 85 + pos ) ) return 0;
    if( xPass > ( 108 + pos ) ) return 0;
    if( yPass < 7 ) return 0;
    return (
        tddugSpeedBlitz( 85 + pos, 7, xPass, yPass, tddugM10000, tddugpolice_TDDUG ) |
        tddugSpeedBlitz( 89 + pos, 7, xPass, yPass, tddugM1000, tddugpolice_TDDUG ) |
        tddugSpeedBlitz( 93 + pos, 7, xPass, yPass, tddugM100, tddugpolice_TDDUG ) |
        tddugSpeedBlitz( 97 + pos, 7, xPass, yPass, tddugM10, tddugpolice_TDDUG ) |
        tddugSpeedBlitz( 101 + pos, 7, xPass, yPass, tddugM1, tddugpolice_TDDUG ) |
        tddugSpeedBlitz( 105 + pos, 7, xPass, yPass, 0, tddugpolice_TDDUG )
    );
}

int tddugRecupeStart( int xPass, int yPass, int blink )
{
    if( blink > 7 ) return 0;
    return tddugSpeedBlitz( 51, 6, xPass, yPass, 0, tddugSTART_TDDUG );
}

void tddugRenderFrame()
{
    int x, y, val;
    md_beginFrame();
    for( y = 0; y < 8; y++ )
    {
        tddugCompositeEnemyRow( y );
        tddugCompositeWeaponRow( y );
        tddugRefreshBackWallCache( y );

        // tddugMainSprite()/tddugRecupeScores() are already self-gated
        // internally (an early return outside their own real footprint),
        // but a self-gated function still costs a full call every time
        // it's invoked - the same lesson already applied to every other
        // sprite/UI layer in this project (Arkanoid/Bert/Tris/Trick/
        // Morpion among others). Precomputing each layer's real footprint
        // once per row and gating the call SITE cuts ~2000 wasted calls/
        // tick down to a few dozen.
        int mainRowOk = 0;
        int mainXStart = 0;
        int mainXEnd = -1;
        int mainRecupeLineY = tddugRecupeLineY( tddugMain.y );
        if( !( mainRecupeLineY > y || ( mainRecupeLineY + 1 ) < y ) )
        {
            mainRowOk = 1;
            mainXStart = tddugMain.x;
            mainXEnd = tddugMain.x + 7;
        }
        int scoreRowOk = ( y == 7 );

        for( x = 0; x < 128; x++ )
        {
            if( x < 19 )
            {
                md_drawColumn( x, y, 0 );
                continue;
            }
            val = tddugBack( x, y ) | tddugEnemyPageBuffer[x] | tddugWeaponPageBuffer[x] | tddugRecupeLive( x, y );
            if( mainRowOk && x >= mainXStart && x <= mainXEnd )
                val = val | tddugMainSprite( x, y );
            if( scoreRowOk && x >= 85 && x <= 108 )
                val = val | tddugRecupeScores( x, y, 0 );
            md_drawColumn( x, y, val );
        }
    }
}

void tddugRenderAttract( int blinkT )
{
    int x, y, val;
    md_beginFrame();
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
        {
            if( x < 19 ) val = 0;
            else val = tddugRecupeScores( x, y, -33 ) | tddugSpeedBlitz( 46, 2, x, y, 0, tddugDDUG_INTRO_TDDUG ) | tddugRecupeStart( x, y, blinkT );
            md_drawColumn( x, y, val );
        }
    }
}

void tddugUpdatePanel( int t )
{
    tddugCompilSco();
    tddugRenderAttract( t );
}

// -----------------------------------------------------------------------------
//   Level / game setup
// -----------------------------------------------------------------------------

void tddugResetAllGD()
{
    tddugGD.timeLaps = 0;
    tddugGD.oneTime = 1;
    tddugGD.counter = 0;
    tddugGD.goOut = 0;
    tddugGD.mainFrame = 0;
    tddugGD.dead = 0;
    tddugGD.mainAnimFrame = 0;
    tddugGD.mainAnim = 0;
    tddugGD.directionAnim = 0;
}

void tddugMainInit()
{
    tddugMain.x = 60;
    tddugMain.y = 32;
    tddugMain.active = 1;
    tddugMain.directionY = 0;
    tddugMain.directionX = 1;
    tddugMain.somX = 0;
    tddugMain.somY = 0;
    tddugMain.sx = 0;
    tddugMain.sy = 0;
}

void tddugEnemyInit( TddugSprite* e, int x, int y, int type )
{
    e->tracking = 1;
    e->somX = 0; e->somY = 0; e->sx = 0; e->sy = 0;
    e->x = x; e->y = y;
    e->directionX = tddugRnd();
    e->directionY = tddugRnd();
    e->active = 1;
    e->first = 0;
    e->type = type;
    e->bigZip = 0;
}

void tddugLoadEnemyPos()
{
    int t;
    for( t = 0; t < TDDUG_MAX_ENEMY; t++ )
    {
        tddugEnemy[t].active = 0;
        if( tddugENEMY_ENABLE_TDDUG[ t + ( tddugGD.level * 4 ) ] )
        {
            int w1 = tddugENEMY_TDDUG[ 0 + ( t * 3 ) ];
            int w2 = tddugENEMY_TDDUG[ 1 + ( t * 3 ) ];
            int w5 = tddugENEMY_TDDUG[ 2 + ( t * 3 ) ];
            tddugEnemyInit( &tddugEnemy[t], w1, w2, w5 );
        }
    }
}

void tddugLoadLevel( int lev )
{
    int yc, xc;
    for( yc = 0; yc < 12; yc++ )
        for( xc = 0; xc < 3; xc++ )
            tddugGrid[yc][xc] = tddugLEVEL_TDDUG[ ( xc + ( yc * 3 ) ) + ( lev * 36 ) ];
}

void tddugNewGame()
{
    tddugGD.scores = 0;
    tddugGD.level = 0;
    tddugGD.liveComp = 34;
    tddugGD.live = 4;
    tddugGD.triggerCounter = 200;
}

void tddugNextLevel()
{
    if( tddugGD.triggerCounter > 12 ) tddugGD.triggerCounter = tddugGD.triggerCounter - 10; else tddugGD.triggerCounter = 10;
    if( tddugGD.level < 6 ) tddugGD.level = tddugGD.level + 1; else tddugGD.level = 2;
}

void tddugCompiling()
{
    tddugCompilSco();
    tddugResetAllGD();
    tddugResetWeapon();
    tddugMainInit();
    tddugLoadEnemyPos();
    tddugWeapon[0].active = 5;
}

void tddugBeginLevelLoad()
{
    tddugLoadLevel( tddugGD.level );
    tddugCompiling();
    tddugWaitFrames = 15;
    tddugState = TDDUG_STATE_LEVEL_INTRO_WAIT;
    tddugRenderFrame();
}

void tddugBeginLevelRetry()
{
    // Matches upstream's own `goto RELOAD_LEVEL` (skips LOAD_LEVEL_TDDUG) -
    // a level retry after death keeps whatever tunnels were already dug,
    // unlike a true new game or a level advance (both of which reload the
    // level's original rock layout from LEVEL_TDDUG first).
    tddugCompiling();
    tddugWaitFrames = 15;
    tddugState = TDDUG_STATE_LEVEL_INTRO_WAIT;
    tddugRenderFrame();
}

// -----------------------------------------------------------------------------
//   Level-complete / escape detection
// -----------------------------------------------------------------------------

void tddugHowManyEnemy()
{
    int val = 0, val2 = 0, t;
    for( t = 0; t < TDDUG_MAX_ENEMY; t++ )
    {
        if( tddugEnemy[t].active == 1 )
        {
            val2 = t;
            val++;
        }
    }
    if( val == 1 )
    {
        tddugEnemy[val2].tracking = 0;
        tddugGD.goOut = 1;
        tddugGD.nospriteGoOut = val2;
    }
}

int tddugGamePlay()
{
    if( tddugGD.goOut == 0 )
    {
        tddugHowManyEnemy();
    }
    else if( tddugGD.goOut == 1 )
    {
        TddugSprite* e = &tddugEnemy[ tddugGD.nospriteGoOut ];
        if( ( e->x == 20 && e->y == 0 ) || e->active == 0 )
        {
            e->active = 0;
            return 1;
        }
    }
    return 0;
}

void tddugAdjustFrameMain()
{
    if( tddugGD.dead == 0 )
        tddugGD.mainFrame = tddugGD.mainAnimFrame + ( tddugGD.directionAnim * 3 );
    else if( tddugGD.dead >= 1 && tddugGD.dead <= 5 )
        tddugGD.mainFrame = tddugGD.dead + 11;
    else if( tddugGD.dead == 6 )
        tddugGD.mainFrame = tddugGD.dead + 10;
}

// -----------------------------------------------------------------------------
//   Top-level state machine
// -----------------------------------------------------------------------------

void gameTinyDDug_init()
{
    tddugRD = 0;
    tddugAnimEnemy = 0;
    tddugAnimEnemyFrame = 0;
    tddugMainSpeedStep = 25;
    tddugSeqActive = 0;
    tddugTickSkipCounter = 0;
    tddugState = TDDUG_STATE_ATTRACT;
    tddugAttractT = 0;
    tddugAttractFireHeld = 0;
    tddugGD.oneTime = 0;
    tddugGD.dead = 0;
}

void gameTinyDDug_forceRedraw()
{
    if( tddugState == TDDUG_STATE_ATTRACT )
        tddugRenderAttract( tddugAttractT );
    else
        tddugRenderFrame();
}

void gameTinyDDug_update()
{
    tddugAdvanceNoteSeq();

    tddugTickSkipCounter++;
    if( tddugTickSkipCounter < TDDUG_TICK_DIVISOR )
        return;
    tddugTickSkipCounter = 0;

    if( tddugState == TDDUG_STATE_ATTRACT )
    {
        if( !tddugAttractFireHeld )
        {
            tddugUpdatePanel( tddugAttractT );
            if( tddugAttractT < 14 ) tddugAttractT = tddugAttractT + 1; else tddugAttractT = 0;
            if( isFirePressed() )
                tddugAttractFireHeld = 1;
        }
        else
        {
            if( !isFirePressed() )
            {
                tddugStartNoteSeq( tddugSnd2Notes, TDDUG_SND2_COUNT );
                tddugNewGame();
                tddugBeginLevelLoad();
            }
        }
    }
    else if( tddugState == TDDUG_STATE_LEVEL_INTRO_WAIT )
    {
        if( tddugWaitFrames > 0 )
            tddugWaitFrames--;
        else
            tddugState = TDDUG_STATE_PLAYING;
    }
    else if( tddugState == TDDUG_STATE_PLAYING )
    {
        int directSp = 0;
        if( tddugGD.dead == 0 )
        {
            int active0 = tddugWeapon[0].active;
            if( active0 == 0 || active0 == 5 )
            {
                if( isRightPressed() ) { tddugRnd(); tddugWalkRight( &tddugMain ); directSp += 1; }
                else if( isLeftPressed() ) { tddugRnd(); tddugWalkLeft( &tddugMain ); directSp += 4; }
                if( isUpPressed() ) { tddugRnd(); tddugWalkUp( &tddugMain ); directSp += 8; }
                else if( isDownPressed() ) { tddugRnd(); tddugWalkDown( &tddugMain ); directSp += 2; }
            }
            tddugAdjustMainSpeed( directSp );
            tddugUpdateEnemy();
            if( isFirePressed() )
            {
                tddugGD.oneTime = 1;
                if( tddugWeapon[0].active < 5 )
                {
                    tddugWeapon[0].active = tddugWeapon[0].active + 1;
                    tddugAdjustWeapon( &tddugWeapon[0], &tddugMain );
                    if( tddugWeapon[0].active > 2 )
                    {
                        tddugWeapon[1].active = 1;
                        tddugAdjustWeapon2( &tddugWeapon[1], &tddugWeapon[0], &tddugMain );
                    }
                    tddugCheckBalisticColid();
                }
            }
            else
            {
                if( tddugGD.oneTime == 1 )
                {
                    tddugResetWeapon();
                    tddugGD.oneTime = 0;
                    int t;
                    for( t = 0; t < TDDUG_MAX_ENEMY; t++ )
                        tddugEnemy[t].first = 0;
                }
            }
        }
        tddugTlEnemy();
        tddugTriggerAdj();
        tddugCheckCollision();
        tddugAdjustFrameMain();
        tddugRenderFrame();

        if( tddugGD.dead == 6 )
        {
            tddugWaitFrames = 15;
            tddugState = TDDUG_STATE_DEATH_WAIT;
        }
        else if( tddugGamePlay() == 1 )
        {
            tddugStartNoteSeq( tddugSnd0Notes, TDDUG_SND0_COUNT );
            tddugWaitFrames = 15;
            tddugState = TDDUG_STATE_LEVEL_CLEAR_WAIT;
        }
        else
        {
            tddugRnd();
        }
    }
    else if( tddugState == TDDUG_STATE_DEATH_WAIT )
    {
        if( tddugWaitFrames > 0 )
        {
            tddugWaitFrames--;
        }
        else if( tddugGD.live > 0 )
        {
            tddugGD.live--;
            tddugBeginLevelRetry();
        }
        else
        {
            tddugState = TDDUG_STATE_ATTRACT;
            tddugAttractT = 0;
            tddugAttractFireHeld = 0;
        }
    }
    else if( tddugState == TDDUG_STATE_LEVEL_CLEAR_WAIT )
    {
        if( tddugWaitFrames > 0 )
            tddugWaitFrames--;
        else
        {
            tddugNextLevel();
            tddugBeginLevelLoad();
        }
    }
}

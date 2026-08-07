// Gilbert in the Downland (Daniel C, 2025, GPLv3 for this ESP port). An
// 11-room climbing platformer: run/jump/climb ropes across rooms full of
// acid-drip hazards, a slow-bouncing balloon patrol enemy, and (once you
// linger too long in one room) a chasing "timeout" enemy - collect gold
// and keys, unlock doors, reach the exit.
//
// Ported from `more games/MEGAcompilation_ESP/DATA/GILBERTINTHEDOWNLAND/`
// - Daniel C's own ESP8285/ESP8266 "MEGA TinyJoypad" combined cartridge
// (see this project's CLAUDE.md for how that compilation was found and
// why it changed the earlier "out of scope" call on these titles). The
// second of the 3 Arduboy-originated games staged from it (after
// Nohzdyve).
//
// ***A genuinely different rendering foundation from every other game in
// this whole cartridge.*** Every other port here draws discrete sprites
// from PROGMEM bitmap tables. This game's own rooms are instead real
// VECTOR line art - `DrawVector()` walks a small per-room command list
// (POS/ROOF/WALL/VEC/ILOT/BIG_ILOT) and draws literal Bresenham line
// segments (`drawLine`) directly into a real, per-pixel-*readable*
// framebuffer via `drawPixel`/`getPixel` - collision detection
// (`ColWall`, the balloon's own `collXBallon`/`collYBallon`) works by
// reading back whatever pixels are already drawn there, not by querying
// any separate collision-shape data. This is exactly the "per-pixel
// procedural rendering" case this project's own CLAUDE.md flags as
// needing special handling - but Tiny Arena already proved the fix
// pattern works on Vircon32 (maintain a real in-memory pixel buffer,
// stream it out via `md_drawColumn()` once per frame) - and critically,
// the room is redrawn completely from scratch every single real tick
// (`DrawRoomInProgress()` calls `MEGA82XX.clear()` then re-runs the whole
// vector command list every frame, not just once on room entry), so
// there's no need to persist buffer state *across* frames either - just
// rebuild it fresh each tick, exactly like every other game here already
// does with its own composed page buffer.
//
// `gitdFrameBuffer[1024]` is laid out identically to the SSD1306 page-
// byte format every other game's own column atlas already assumes (one
// int per (column, page), bit N = pixel row N within that page) - so
// `gitdSetPixel`/`gitdGetPixel` are simple bit ops, and streaming the
// finished buffer to `md_drawColumn()` at the end of a frame needs no
// repacking at all, just a direct 1:1 copy.
//
// Class hierarchy flattened the usual way (TIMERGITD -> GitdTimer,
// StaticSprite_GITD/Sprite_GITD -> GitdPlayer with the base fields
// inlined, AcidDrop_GITD -> GitdAcidDrop, Ballon_GITD -> GitdBalloon,
// TIMEOUT -> GitdTimeout) - unlike Nohzdyve's SPND[9] (one shared struct
// type, since every sprite kind had to live in one uniform array), none
// of these 4 kinds here are ever mixed into a shared array, so each gets
// its own plain struct instead of forcing a unified type.
//
// `SpriteStretchSelect()`'s own GCC case-range extension (`case -50 ...
// -30:` etc) rewritten as an if/else chain, matching this project's own
// standing caution around that extension (only ever confirmed NOT
// supported the hard way once, in Tiny Doc's own history). The one
// nested ternary in the whole file (`MainAnimSelect()`) rewritten as
// plain nested if/else.
//
// Sound: `Sound_GITD()` is the same `255-freq` bit-bang formula as
// every other Daniel-C `Sound()` already in this project - ported
// straight onto the shared `Sound()`. Every `Sound_GITD()` call site in
// this file fires at most 2 calls back-to-back
// (`SoundSystem_GITD(0)`/`(3)`), the same small-burst shape already
// found (and already understood as needing a sequencer) elsewhere in
// this project - `gitdSfxFreq`/`gitdSfxDur`/`gitdSfxLen`/`gitdSfxPos`
// give each a 2-note frame-stepped player rather than letting the second
// note silently replace the first on the same tick.
//
// The splash screen's own elaborate pixel-level growing-rectangle
// animation is simplified to a plain blinking decorative element - the
// same "effort/fidelity tradeoff for a purely decorative, non-gameplay
// sequence" precedent already used for Nohzdyve's own splash and Space
// Attack's attract screen. The title screen's own acid-drop rain and
// hi-score display, and all real gameplay (room rendering, physics,
// rope-climbing, pickups, both enemies, room-transition wipe), are
// ported at full fidelity.

// -----------------------------------------------------------------------------
// Constants (upstream #define values, resolved into the room/pickup data
// at extraction time already - kept here too for the code that reads
// against them directly)
// -----------------------------------------------------------------------------

#define GITD_POS 0
#define GITD_ROOF 1
#define GITD_WALL 2
#define GITD_VEC 3
#define GITD_ILOT 4
#define GITD_BIG_ILOT 5
#define GITD_BALLONSPK 6
#define GITD_EXIT -1

#define GITD_HORIZONTAL 0
#define GITD_VERTICAL 1

#define GITD_GOLD 0
#define GITD_GOLD2 1
#define GITD_KEY 2
#define GITD_DOOR 3

#define GITD_X_OFFSET 7
#define GITD_Y_OFFSET 0
#define GITD_NBOC 11
#define GITD_START_ROOM 16

enum GitdState
{
    GITD_STATE_SPLASH = 0,
    GITD_STATE_TITLE,
    GITD_STATE_ROOM_TRANSITION,
    GITD_STATE_PLAYING
};

// -----------------------------------------------------------------------------
// Structs (flattened from upstream's class hierarchy)
// -----------------------------------------------------------------------------

struct GitdTimer
{
    int activ;
    int startTime;
    int interval;
};

struct GitdPlayer
{
    int actif;
    int x;
    int y;
    int type;
    int direction;
    int animFrame;
    int deadFrameStep;
    int stretchAdd;
    int trigSpeedRestriction;
    int speedRestriction;
    float gravitySpeed;
    int floorStat;
    int oneClick;
    int oneClick2;
    int stretch;
};

struct GitdAcidDrop
{
    int actif;
    int x;
    int y;
    int delays;
};

struct GitdBalloon
{
    int actif;
    int x;
    int y;
    float step;
    int anim;
    int xmem;
    int ymem;
};

struct GitdTimeout
{
    int time;
    int flip;
    int actif;
    int x;
    int y;
    int ud;
    int rl;
};

struct GitdLevel
{
    int room;
    int memRoom;
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

GitdTimer gitdTM1;  // room-rebuild pacing (walking-speed restriction shares this too)
GitdTimer gitdTM2;  // acid-drop release interval
GitdTimer gitdTM3;  // balloon update interval

GitdPlayer gitdP;
GitdAcidDrop[6] gitdAcid;
GitdBalloon gitdBalloon;
GitdTimeout gitdTMout;
GitdLevel gitdLvl;

int[22] gitdVisibleKeyAndDoors;
int[30] gitdVisibleSprite;

int gitdJumpCycle;
int gitdJumpCycleMem;
int gitdRopeMode;
int gitdUpDown;
int gitdDDead;
int gitdRoomChange;
int gitdFrm;
int gitdAcidFlip;   // upstream's own "Flip" - halves the acid-drop update/draw rate

int gitdScores;
int gitdHiScores;
int gitdLives;
int gitdM10000;
int gitdM1000;
int gitdM100;
int gitdM10;
int gitdM1;

int gitdLastX;
int gitdLastY;

int gitdState;

// Edge-detected Fire, computed once per real frame in the update
// dispatcher below. The splash->title and title->playing transitions
// both react to a Fire press - reading isFirePressed() as a plain level
// check (upstream's own polling shape) meant a physical press still held
// across the very frame a state transition happens would immediately
// re-trigger the *next* state's own Fire check too, on the following
// frame (splash's own transition firing, then title's "start game"
// firing right after, from the same single physical press) - fixed by
// only reacting on the rising edge instead.
int gitdPrevFire;
int gitdFireEdge;

// Splash/title decorative state
int gitdSplashBlink;
int gitdTitleAlt;

// Room-transition state
int[888] gitdOldPic;   // 111 columns x 8 pages, matches upstream's own OldPic[111*8]
int gitdTransitStep;
int gitdTransitWait;

// Small 2-note SFX player (see this file's own header comment)
int gitdSfxFreq0;
int gitdSfxDur0;
int gitdSfxFreq1;
int gitdSfxDur1;
int gitdSfxLen;
int gitdSfxPos;
int gitdSfxWaitFrames;

// -----------------------------------------------------------------------------
// Data tables (byte-diff-extracted from
// more games/MEGAcompilation_ESP/DATA/GILBERTINTHEDOWNLAND/{Chamber0..10,SpriteBank_GITD}.h,
// symbolic room-vector constants (POS/ROOF/WALL/VEC/ILOT/BIG_ILOT/
// BALLONSPK/EXIT/HORIZONTAL/VERTICAL/GOLD/GOLD2/KEY/DOOR) resolved to
// their real #define values at extraction time)
// -----------------------------------------------------------------------------

// RopeVec0: 37 values (int8_t)
int[37] gitdRopeVec0 = {
    1, 9, 37, 18, 1, 21, 53, 3, 1, 49, 53, 3, 1, 77, 53, 3, 1, 35, 35, 9, 1, 63, 35, 9, 1, 84, 20, 
    18, 1, 9, 4, 16, 1, 56, 4, 2, -1, 
};

// Vec0: 100 values (int8_t)
int[100] gitdVec0 = {
    4, 18, 44, 4, 46, 44, 4, 74, 44, 0, 0, 5, 3, 2, 3, 1, 93, 3, 3, 96, 6, 3, 96, 13, 3, 16, 13, 3, 
    18, 16, 3, 24, 19, 1, 87, 19, 3, 96, 23, 2, 96, 63, 3, 0, 63, 0, 0, 37, 2, 0, 64, 0, 0, 37, 3, 
    5, 33, 3, 8, 35, 3, 16, 31, 3, 17, 29, 3, 1, 29, 0, 0, 5, 2, 0, 29, 0, 73, 29, 3, 25, 29, 0, 25, 
    29, 3, 27, 32, 3, 34, 34, 1, 64, 34, 3, 70, 31, 3, 72, 30, -1, 
};

// Small_Ilot: 22 values (int8_t)
int[22] gitdSmall_Ilot = {
    0, 0, 0, 3, 6, 0, 2, 6, 6, 0, 0, 0, 2, 0, 6, 3, 3, 8, 3, 6, 6, -1, 
};

// SPK0: 29 values (int8_t)
int[29] gitdSPK0 = {
    0, 60, 46, 0, 1, 32, 46, 1, 0, 18, 32, 2, 2, 72, 32, 1, 2, 14, 16, 2, 3, 98, 6, 2, 6, 81, 32, 0, 
    -1, 
};

// AcidDrop0: 53 values (int8_t)
int[53] gitdAcidDrop0 = {
    26, 6, 3, 12, 3, 16, 3, 22, 3, 42, 3, 53, 2, 59, 2, 90, 3, 87, 18, 81, 18, 52, 19, 37, 20, 25, 
    20, 6, 36, 12, 35, 32, 35, 38, 34, 52, 36, 60, 34, 66, 35, 80, 52, 74, 52, 52, 52, 46, 52, 24, 
    52, 18, 52, 
};

// RopeVec1: 21 values (int8_t)
int[21] gitdRopeVec1 = {
    1, 85, 36, 19, 1, 9, 18, 26, 1, 39, 18, 36, 1, 66, 21, 6, 1, 80, 5, 6, -1, 
};

// Vec1: 184 values (int8_t)
int[184] gitdVec1 = {
    0, 0, 5, 3, 6, 3, 1, 42, 3, 3, 50, 6, 2, 50, 13, 0, 50, 13, 3, 0, 13, 3, 0, 5, 0, 58, 5, 3, 62, 
    3, 1, 90, 3, 3, 96, 6, 3, 96, 13, 3, 90, 13, 3, 92, 15, 3, 96, 18, 2, 96, 24, 3, 96, 29, 3, 78, 
    29, 3, 80, 31, 3, 83, 33, 3, 85, 35, 1, 92, 35, 3, 96, 39, 2, 96, 63, 3, 29, 63, 0, 20, 29, 3, 
    29, 29, 2, 29, 63, 0, 20, 29, 2, 20, 57, 3, 8, 57, 0, 0, 20, 2, 0, 44, 3, 0, 52, 3, 7, 52, 2, 7, 
    56, 3, 7, 57, 0, 0, 20, 1, 44, 20, 3, 50, 22, 2, 50, 47, 3, 55, 52, 1, 71, 52, 0, 58, 6, 2, 58, 
    13, 3, 70, 13, 2, 70, 19, 3, 64, 20, 3, 60, 19, 3, 58, 21, 2, 58, 33, 3, 63, 33, 2, 63, 36, 3, 
    63, 38, 3, 68, 38, 2, 68, 42, 3, 68, 43, 3, 73, 43, 3, 73, 45, 3, 69, 50, -1, 
};

// SPK1: 37 values (int8_t)
int[37] gitdSPK1 = {
    1, 45, 48, 3, 1, 71, 48, 4, 1, 81, 14, 5, 2, 60, 7, 4, 2, 42, 7, 14, 3, -7, 6, 21, 3, -7, 45, 
    100, 3, 98, 6, 101, 3, 98, 22, 102, -1, 
};

// AcidDrop1: 43 values (int8_t)
int[43] gitdAcidDrop1 = {
    21, 6, 22, 12, 22, 20, 20, 29, 21, 36, 22, 42, 22, 54, 53, 62, 53, 70, 51, 82, 34, 88, 34, 63, 
    21, 69, 21, 83, 2, 77, 2, 70, 3, 90, 3, 63, 4, 40, 3, 32, 3, 7, 4, 
};

// RopeVec2: 29 values (int8_t)
int[29] gitdRopeVec2 = {
    1, 40, 38, 17, 1, 12, 18, 22, 1, 69, 40, 3, 1, 79, 3, 8, 1, 67, 3, 19, 1, 45, 19, 3, 1, 26, 4, 
    3, -1, 
};

// Vec2: 145 values (int8_t)
int[145] gitdVec2 = {
    0, 0, 5, 3, 2, 3, 1, 94, 3, 3, 96, 6, 3, 96, 13, 3, 90, 13, 3, 92, 15, 3, 96, 18, 2, 96, 24, 3, 
    96, 29, 3, 38, 29, 0, 0, 5, 3, 0, 13, 3, 56, 13, 3, 50, 15, 3, 50, 16, 3, 46, 18, 3, 42, 17, 3, 
    38, 19, 2, 38, 29, 0, 0, 20, 3, 3, 18, 1, 28, 18, 3, 30, 20, 2, 30, 29, 3, 22, 29, 3, 24, 31, 3, 
    27, 39, 1, 90, 39, 3, 96, 41, 3, 96, 48, 3, 80, 48, 3, 80, 53, 3, 64, 53, 3, 64, 58, 3, 48, 58, 
    3, 48, 63, 3, 0, 63, 0, 0, 20, 2, 0, 39, 3, 0, 48, 3, 33, 48, 2, 33, 51, 0, 0, 54, 1, 31, 54, 3, 
    33, 51, 0, 0, 54, 3, 0, 63, -1, 
};

// SPK2: 49 values (int8_t)
int[49] gitdSPK2 = {
    1, 43, 40, 6, 0, 77, 35, 7, 1, 40, 21, 8, 2, 22, 18, 3, 2, 76, 17, 7, 3, -7, 6, 6, 3, -7, 41, 3, 
    3, -7, 56, 104, 3, 98, 6, 105, 3, 98, 22, 7, 3, 98, 41, 106, 6, 77, 36, 0, -1, 
};

// AcidDrop2: 47 values (int8_t)
int[47] gitdAcidDrop2 = {
    23, 6, 3, 15, 4, 23, 2, 29, 2, 45, 4, 52, 3, 64, 3, 70, 3, 76, 3, 82, 3, 90, 3, 42, 19, 48, 19, 
    9, 20, 15, 20, 28, 40, 37, 39, 43, 39, 57, 41, 66, 38, 72, 38, 14, 54, 33, 53, 
};

// RopeVec3: 17 values (int8_t)
int[17] gitdRopeVec3 = {
    1, 24, 37, 19, 1, 55, 19, 37, 1, 79, 19, 37, 1, 67, 19, 11, -1, 
};

// Vec3: 193 values (int8_t)
int[193] gitdVec3 = {
    0, 0, 5, 3, 2, 3, 1, 94, 3, 3, 96, 6, 3, 96, 13, 3, 0, 13, 3, 0, 5, 0, 0, 21, 3, 3, 19, 1, 34, 
    19, 3, 36, 23, 2, 36, 29, 3, 20, 29, 3, 20, 32, 3, 23, 35, 1, 34, 36, 3, 36, 38, 2, 36, 53, 3, 
    39, 56, 3, 42, 53, 2, 42, 26, 3, 44, 19, 1, 90, 19, 3, 96, 24, 3, 96, 30, 3, 90, 30, 3, 91, 31, 
    3, 92, 33, 3, 96, 36, 3, 96, 38, 3, 95, 38, 3, 95, 39, 3, 96, 40, 3, 96, 46, 3, 90, 46, 3, 91, 
    47, 3, 92, 49, 3, 96, 51, 3, 96, 55, 3, 95, 55, 3, 95, 56, 3, 96, 57, 3, 96, 63, 3, 71, 63, 3, 
    71, 62, 2, 71, 46, 3, 68, 47, 3, 64, 50, 2, 64, 63, 3, 0, 63, 3, 0, 57, 3, 3, 53, 3, 10, 50, 3, 
    11, 48, 3, 12, 46, 3, 0, 46, 3, 0, 39, 3, 3, 35, 3, 6, 33, 3, 9, 35, 3, 12, 33, 2, 12, 29, 3, 0, 
    29, 3, 0, 21, -1, 
};

// SPK3: 49 values (int8_t)
int[49] gitdSPK3 = {
    1, 64, 34, 9, 1, 88, 34, 10, 0, 5, 52, 11, 2, 28, 22, 8, 2, 88, 17, 5, 3, -7, 6, 107, 3, -7, 22, 
    108, 3, -7, 39, 109, 3, 98, 6, 110, 3, 98, 23, 5, 3, 98, 39, 8, 3, 98, 56, 111, -1, 
};

// AcidDrop3: 49 values (int8_t)
int[49] gitdAcidDrop3 = {
    24, 6, 3, 12, 3, 24, 3, 6, 18, 20, 20, 30, 18, 52, 19, 6, 35, 21, 35, 27, 36, 8, 53, 35, 55, 42, 
    55, 58, 19, 64, 19, 70, 19, 76, 19, 82, 19, 90, 18, 90, 32, 90, 48, 91, 4, 81, 4, 61, 4, 
};

// RopeVec4: 21 values (int8_t)
int[21] gitdRopeVec4 = {
    1, 26, 28, 26, 1, 86, 5, 37, 1, 39, 54, 3, 1, 74, 54, 3, 0, 42, 30, 28, -1, 
};

// Vec4: 97 values (int8_t)
int[97] gitdVec4 = {
    5, 36, 38, 5, 71, 38, 0, 0, 5, 3, 2, 3, 1, 94, 3, 3, 96, 6, 2, 96, 63, 3, 0, 63, 3, 0, 57, 2, 0, 
    51, 3, 5, 48, 3, 6, 46, 3, 0, 46, 3, 0, 41, 2, 0, 35, 3, 5, 32, 3, 6, 30, 3, 0, 30, 3, 0, 24, 3, 
    4, 20, 1, 18, 20, 3, 26, 28, 1, 77, 28, 2, 77, 18, 3, 28, 18, 3, 28, 13, 3, 0, 13, 3, 0, 5, 0, 
    42, 30, 3, 39, 27, 0, 72, 30, 3, 76, 26, -1, 
};

// Big_Ilot: 22 values (int8_t)
int[22] gitdBig_Ilot = {
    0, 0, 0, 3, 6, 0, 2, 6, 13, 0, 0, 0, 2, 0, 13, 3, 3, 15, 3, 6, 13, -1, 
};

// SPK4: 37 values (int8_t)
int[37] gitdSPK4 = {
    3, -7, 6, 112, 3, -7, 23, 113, 3, -7, 39, 114, 3, -7, 56, 115, 1, 78, 3, 14, 1, 45, 50, 12, 0, 
    80, 50, 13, 2, 4, 17, 6, 2, 4, 33, 9, -1, 
};

// AcidDrop4: 41 values (int8_t)
int[41] gitdAcidDrop4 = {
    20, 7, 4, 19, 4, 28, 3, 47, 2, 71, 2, 83, 2, 89, 2, 6, 19, 6, 32, 6, 48, 23, 28, 29, 27, 39, 29, 
    47, 27, 65, 27, 74, 30, 36, 53, 42, 53, 71, 53, 77, 53, 
};

// RopeVec5: 17 values (int8_t)
int[17] gitdRopeVec5 = {
    1, 10, 3, 34, 1, 29, 34, 10, 1, 41, 34, 20, 1, 69, 3, 9, -1, 
};

// Vec5: 139 values (int8_t)
int[139] gitdVec5 = {
    0, 0, 5, 3, 2, 3, 1, 94, 3, 3, 96, 6, 3, 96, 13, 3, 78, 13, 3, 80, 15, 3, 80, 16, 3, 84, 19, 3, 
    88, 20, 1, 94, 20, 3, 96, 21, 3, 96, 29, 3, 70, 29, 3, 70, 24, 3, 65, 24, 3, 65, 19, 3, 60, 19, 
    3, 60, 14, 3, 21, 14, 2, 21, 32, 3, 24, 35, 3, 26, 35, 1, 90, 35, 3, 93, 36, 3, 96, 39, 2, 96, 
    44, 0, 96, 43, 3, 96, 50, 3, 84, 50, 3, 84, 45, 3, 52, 45, 3, 54, 47, 3, 54, 48, 3, 58, 51, 1, 
    72, 51, 3, 76, 54, 2, 76, 63, 3, 0, 63, 3, 0, 55, 3, 11, 50, 3, 14, 53, 3, 17, 51, 2, 17, 45, 3, 
    0, 45, 2, 0, 5, -1, 
};

// SPK5: 37 values (int8_t)
int[37] gitdSPK5 = {
    3, -7, 56, 10, 3, 98, 22, 9, 3, 98, 43, 11, 0, -2, 26, 17, 1, 65, 50, 15, 0, 16, 19, 16, 2, -2, 
    11, 10, 2, 87, 2, 11, 6, 79, 32, 0, -1, 
};

// AcidDrop5: 47 values (int8_t)
int[47] gitdAcidDrop5 = {
    23, 7, 4, 13, 4, 23, 2, 41, 2, 53, 2, 26, 37, 32, 37, 38, 37, 44, 37, 7, 54, 17, 53, 66, 3, 72, 
    3, 78, 3, 89, 2, 81, 19, 89, 21, 66, 35, 77, 35, 90, 35, 58, 53, 65, 52, 71, 53, 
};

// RopeVec6: 13 values (int8_t)
int[13] gitdRopeVec6 = {
    1, 11, 3, 53, 1, 80, 3, 9, 1, 80, 30, 26, -1, 
};

// Vec6: 76 values (int8_t)
int[76] gitdVec6 = {
    0, 0, 5, 3, 2, 3, 1, 94, 3, 3, 96, 6, 2, 96, 18, 3, 96, 23, 3, 23, 23, 3, 24, 24, 3, 25, 26, 3, 
    31, 29, 1, 93, 29, 3, 96, 32, 2, 96, 38, 3, 96, 43, 3, 90, 43, 3, 91, 44, 3, 92, 46, 3, 96, 48, 
    3, 96, 52, 3, 95, 52, 3, 95, 53, 3, 96, 54, 2, 96, 63, 3, 0, 63, 2, 0, 5, -1, 
};

// SPK6: 33 values (int8_t)
int[33] gitdSPK6 = {
    3, 98, 36, 13, 3, 98, 16, 12, 2, 16, 2, 12, 2, 88, 30, 13, 0, 16, 26, 20, 0, 38, 47, 18, 0, 50, 
    7, 19, 6, 83, 30, 0, -1, 
};

// AcidDrop6: 37 values (int8_t)
int[37] gitdAcidDrop6 = {
    18, 8, 5, 14, 5, 23, 3, 35, 3, 47, 2, 53, 2, 65, 2, 77, 2, 83, 2, 89, 2, 31, 31, 46, 28, 52, 28, 
    64, 28, 77, 29, 83, 29, 90, 30, 90, 45, 
};

// RopeVec7: 9 values (int8_t)
int[9] gitdRopeVec7 = {
    1, 79, 3, 52, 1, 16, 3, 52, -1, 
};

// Vec7: 148 values (int8_t)
int[148] gitdVec7 = {
    0, 0, 5, 3, 2, 3, 1, 44, 3, 3, 47, 5, 3, 48, 6, 3, 51, 9, 3, 54, 6, 3, 56, 3, 1, 93, 3, 3, 96, 
    5, 2, 96, 23, 3, 96, 29, 3, 90, 29, 3, 91, 30, 3, 92, 32, 3, 96, 35, 2, 96, 63, 3, 48, 63, 2, 
    48, 50, 3, 54, 47, 3, 55, 45, 2, 55, 39, 3, 61, 36, 3, 61, 35, 3, 65, 32, 3, 67, 31, 2, 67, 19, 
    3, 60, 19, 3, 60, 24, 3, 54, 24, 3, 54, 29, 3, 41, 29, 3, 41, 24, 3, 35, 24, 3, 35, 19, 3, 28, 
    19, 2, 28, 32, 3, 45, 45, 3, 43, 46, 3, 40, 48, 3, 39, 50, 2, 39, 63, 3, 0, 63, 2, 0, 27, 3, 5, 
    24, 3, 6, 22, 3, 0, 22, 3, 0, 21, 2, 0, 5, -1, 
};

// SPK7: 29 values (int8_t)
int[29] gitdSPK7 = {
    3, -7, 56, 15, 3, 98, 22, 4, 2, 3, 12, 15, 2, 52, 50, 16, 0, 87, 16, 23, 0, 30, 50, 21, 0, 45, 
    11, 22, -1, 
};

// AcidDrop7: 35 values (int8_t)
int[35] gitdAcidDrop7 = {
    17, 6, 3, 13, 4, 19, 4, 28, 3, 35, 2, 41, 2, 47, 7, 54, 8, 60, 3, 67, 4, 76, 3, 82, 3, 90, 3, 
    28, 34, 34, 39, 61, 38, 66, 34, 
};

// RopeVec8: 25 values (int8_t)
int[25] gitdRopeVec8 = {
    1, 79, 3, 6, 1, 9, 3, 18, 1, 86, 18, 16, 1, 86, 42, 15, 1, 9, 30, 15, 0, 15, 5, 58, -1, 
};

// Vec8: 106 values (int8_t)
int[106] gitdVec8 = {
    0, 0, 5, 3, 2, 3, 1, 93, 3, 3, 96, 5, 2, 96, 11, 3, 96, 17, 3, 30, 17, 0, 90, 17, 3, 91, 18, 3, 
    92, 20, 3, 96, 23, 2, 96, 41, 3, 20, 41, 0, 90, 41, 3, 91, 42, 3, 92, 44, 3, 96, 47, 2, 96, 63, 
    0, 95, 63, 3, 0, 63, 3, 0, 56, 3, 3, 54, 3, 5, 53, 3, 5, 52, 3, 6, 52, 0, 0, 52, 3, 76, 52, 2, 
    0, 33, 3, 3, 31, 3, 5, 30, 3, 5, 30, 3, 6, 29, 0, 0, 29, 3, 76, 29, 2, 0, 5, -1, 
};

// SPK8: 29 values (int8_t)
int[29] gitdSPK8 = {
    3, -7, 56, 18, 3, 98, 10, 14, 2, 20, 13, 17, 2, 45, 7, 18, 1, 78, 16, 24, 1, 78, 40, 25, 0, 16, 
    28, 26, -1, 
};

// AcidDrop8: 75 values (int8_t)
int[75] gitdAcidDrop8 = {
    37, 6, 3, 12, 3, 23, 2, 35, 2, 47, 2, 59, 2, 71, 2, 76, 3, 82, 3, 89, 3, 30, 19, 43, 19, 56, 19, 
    69, 19, 83, 18, 89, 19, 6, 31, 12, 31, 24, 31, 37, 31, 50, 31, 63, 31, 76, 31, 20, 43, 33, 43, 
    46, 43, 59, 43, 72, 43, 83, 42, 89, 43, 5, 55, 12, 54, 24, 54, 37, 54, 50, 54, 63, 54, 76, 54, 
};

// RopeVec9: 33 values (int8_t)
int[33] gitdRopeVec9 = {
    1, 40, 37, 8, 1, 52, 37, 8, 1, 64, 37, 8, 1, 40, 3, 8, 1, 52, 3, 8, 1, 64, 3, 8, 1, 76, 3, 42, 
    1, 8, 18, 34, -1, 
};

// Vec9: 91 values (int8_t)
int[91] gitdVec9 = {
    4, 37, 29, 4, 49, 29, 4, 61, 29, 0, 0, 5, 3, 2, 3, 1, 93, 3, 3, 96, 5, 2, 96, 63, 3, 12, 63, 3, 
    12, 59, 3, 0, 59, 3, 0, 58, 2, 0, 27, 3, 0, 25, 3, 3, 22, 3, 5, 21, 1, 28, 21, 3, 30, 16, 3, 30, 
    15, 3, 31, 14, 3, 0, 14, 3, 0, 5, 0, 19, 44, 3, 31, 44, 3, 30, 45, 3, 30, 47, 3, 25, 50, 3, 19, 
    47, 3, 19, 44, 3, 18, 44, -1, 
};

// SPK9: 33 values (int8_t)
int[33] gitdSPK9 = {
    3, -7, 7, 19, 3, -7, 52, 20, 3, 97, 56, 16, 2, 31, 2, 19, 2, 38, 22, 20, 0, 67, 48, 27, 1, 37, 
    48, 28, 1, 12, 21, 29, -1, 
};

// AcidDrop9: 53 values (int8_t)
int[53] gitdAcidDrop9 = {
    26, 5, 2, 11, 2, 17, 2, 23, 2, 29, 2, 37, 4, 43, 4, 49, 4, 55, 4, 61, 4, 67, 4, 73, 4, 79, 4, 5, 
    23, 11, 23, 18, 22, 24, 22, 30, 18, 19, 49, 30, 49, 37, 37, 43, 37, 49, 37, 55, 37, 61, 37, 67, 
    37, 
};

// RopeVec10: 1 values (int8_t)
int[1] gitdRopeVec10 = {
    -1, 
};

// Vec10: 22 values (int8_t)
int[22] gitdVec10 = {
    0, 0, 5, 3, 2, 3, 1, 93, 3, 3, 96, 5, 2, 96, 63, 3, 0, 63, 2, 0, 5, -1, 
};

// SPK10: 1 values (int8_t)
int[1] gitdSPK10 = {
    -1, 
};

// AcidDrop10: 31 values (int8_t)
int[31] gitdAcidDrop10 = {
    15, 3, 3, 11, 2, 18, 3, 23, 2, 28, 3, 35, 2, 39, 4, 47, 2, 53, 2, 57, 4, 65, 2, 70, 3, 78, 3, 
    85, 4, 89, 2, 
};

// NUMERICGITD: 32 values (uint8_t)
int[32] gitdNUMERICGITD = {
    3, 8, 31, 17, 31, 0, 0, 31, 29, 21, 23, 17, 21, 31, 7, 4, 31, 23, 21, 29, 31, 21, 29, 1, 1, 31, 
    31, 21, 31, 7, 5, 31, 
};

// Position_GITD: 43 values (int8_t)
int[43] gitdPosition_GITD = {
    2, 6, 2, 7, 2, 22, 2, 23, 2, 38, 2, 39, 2, 45, 2, 41, 2, 52, 2, 56, 88, 6, 88, 10, 88, 16, 88, 
    22, 88, 23, 88, 36, 88, 41, 88, 43, 88, 56, 88, 38, 88, 39, -1, 
};

// InAndOut_GITD: 103 values (int8_t)
int[103] gitdInAndOut_GITD = {
    100, 0, 10, 16, 0, 18, 2, 1, 6, 13, 1, 0, 3, 1, 10, 104, 1, 13, 11, 2, 0, 101, 2, 7, 102, 2, 9, 
    107, 2, 10, 108, 2, 13, 109, 2, 16, 105, 3, 0, 7, 3, 2, 106, 3, 5, 112, 3, 10, 113, 3, 14, 114, 
    3, 20, 115, 3, 18, 110, 4, 0, 5, 4, 3, 8, 4, 5, 111, 4, 9, 12, 5, 9, 15, 5, 13, 6, 5, 17, 10, 6, 
    12, 21, 6, 15, 9, 7, 9, 18, 7, 13, 4, 8, 9, 19, 8, 11, 20, 8, 18, 14, 9, 1, -1, 
};

// DigL_GITD: 8 values (uint8_t)
int[8] gitdDigL_GITD = {
    0, 0, 7, 0, 0, 9, 7, 9, 
};

// gilbert_GITD: 107 values (uint8_t)
int[107] gitdgilbert_GITD = {
    7, 8, 2, 70, 43, 63, 47, 6, 0, 2, 6, 107, 63, 79, 6, 0, 2, 6, 43, 127, 79, 6, 0, 0, 6, 47, 63, 
    43, 70, 2, 0, 6, 79, 63, 107, 6, 2, 0, 6, 79, 127, 43, 6, 2, 0, 22, 47, 63, 111, 6, 0, 0, 6, 
    111, 63, 47, 22, 0, 0, 22, 47, 63, 111, 22, 0, 36, 97, 116, 112, 116, 97, 36, 64, 192, 224, 224, 
    224, 192, 64, 0, 6, 107, 61, 75, 2, 0, 0, 2, 111, 27, 94, 6, 0, 0, 2, 111, 58, 15, 4, 0, 0, 4, 
    95, 54, 205, 4, 0, 
};

// Sprites_World_GITD: 26 values (uint8_t)
int[26] gitdSprites_World_GITD = {
    6, 8, 0, 32, 84, 120, 116, 32, 0, 8, 20, 44, 20, 8, 8, 20, 8, 8, 24, 8, 254, 219, 241, 241, 251, 
    254, 
};

// Ballon_SPK_GITD: 12 values (uint8_t)
int[12] gitdBallon_SPK_GITD = {
    5, 8, 0, 64, 160, 64, 0, 64, 160, 224, 160, 64, 
};

// TimeOut_GITD: 16 values (uint8_t)
int[16] gitdTimeOut_GITD = {
    7, 8, 1, 2, 4, 14, 4, 2, 1, 32, 16, 8, 28, 8, 16, 32, 
};

// CH_GITD: 11 values (uint8_t)
int[11] gitdCH_GITD = {
    9, 8, 14, 17, 17, 0, 31, 4, 31, 0, 17, 
};

// MainTitle_GITD: 541 values (uint8_t)
int[541] gitdMainTitle_GITD = {
    77, 55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 14, 17, 17, 21, 12, 0, 17, 31, 17, 0, 223, 80, 
    80, 128, 31, 149, 85, 90, 128, 31, 213, 17, 0, 31, 197, 5, 218, 128, 1, 223, 1, 192, 0, 0, 17, 
    159, 81, 64, 159, 2, 196, 159, 0, 192, 0, 193, 95, 65, 128, 31, 4, 4, 31, 0, 31, 21, 17, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 4, 4, 3, 
    0, 3, 4, 4, 3, 0, 7, 2, 1, 2, 7, 0, 7, 0, 1, 7, 0, 7, 4, 4, 0, 7, 1, 1, 7, 0, 7, 0, 1, 7, 0, 7, 
    4, 4, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 5, 5, 30, 0, 31, 
    5, 13, 18, 0, 31, 17, 17, 14, 0, 15, 16, 16, 15, 0, 31, 21, 21, 26, 0, 14, 17, 17, 14, 0, 195, 
    84, 84, 143, 0, 192, 0, 3, 204, 16, 12, 3, 0, 31, 21, 17, 0, 31, 5, 5, 26, 0, 18, 21, 21, 9, 0, 
    17, 31, 17, 0, 14, 17, 17, 14, 0, 31, 2, 4, 31, 0, 0, 0, 0, 0, 0, 0, 240, 80, 16, 0, 240, 0, 0, 
    0, 240, 80, 16, 0, 224, 16, 16, 16, 0, 16, 240, 16, 0, 240, 80, 80, 160, 0, 224, 16, 16, 224, 7, 
    5, 5, 246, 0, 0, 5, 5, 3, 16, 240, 16, 0, 0, 0, 240, 80, 80, 160, 0, 0, 0, 0, 0, 0, 208, 80, 
    112, 0, 240, 16, 240, 0, 208, 80, 112, 0, 112, 80, 208, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 
    1, 0, 1, 1, 1, 0, 0, 129, 129, 129, 0, 128, 129, 128, 0, 1, 128, 128, 1, 0, 128, 129, 129, 0, 0, 
    128, 128, 129, 1, 1, 0, 1, 128, 129, 129, 1, 0, 1, 128, 129, 1, 1, 129, 0, 0, 0, 128, 0, 128, 
    129, 129, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 
    2, 0, 0, 9, 10, 10, 4, 0, 0, 15, 0, 0, 15, 2, 2, 15, 0, 15, 2, 2, 13, 0, 0, 15, 0, 0, 0, 0, 7, 
    8, 8, 10, 6, 0, 15, 2, 2, 15, 0, 15, 1, 2, 1, 15, 0, 15, 10, 8, 0, 0, 2, 2, 2, 2, 2, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 124, 16, 16, 124, 0, 0, 68, 124, 68, 0, 56, 68, 68, 84, 48, 0, 0, 
    124, 16, 16, 124, 0, 0, 0, 72, 84, 84, 36, 0, 56, 68, 68, 68, 0, 56, 68, 68, 56, 0, 124, 20, 20, 
    104, 0, 124, 84, 68, 0, 68, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
};

// intro_GITD: 1026 values (uint8_t)
int[1026] gitdintro_GITD = {
    128, 64, 170, 85, 170, 85, 170, 85, 170, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 170, 255, 0, 125, 20, 5, 0, 255, 254, 255, 130, 187, 186, 199, 
    254, 131, 186, 187, 130, 255, 130, 223, 238, 223, 130, 255, 130, 247, 238, 131, 254, 131, 190, 
    191, 254, 135, 234, 235, 134, 255, 130, 247, 238, 131, 254, 131, 186, 187, 198, 255, 254, 255, 
    254, 255, 254, 255, 254, 255, 254, 255, 254, 255, 254, 255, 254, 255, 254, 255, 254, 255, 254, 
    255, 254, 255, 254, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 170, 85, 170, 85, 
    170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 170, 255, 7, 119, 119, 7, 255, 7, 223, 175, 119, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 170, 85, 170, 85, 170, 85, 170, 85, 
    170, 85, 170, 85, 170, 85, 170, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 170, 
    255, 7, 7, 7, 7, 7, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 85, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 
    85, 170, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 170, 255, 252, 252, 252, 252, 
    252, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 170, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 42, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 
    127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 
    63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 
    127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 127, 63, 85, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 170, 85, 170, 85, 170, 85, 170, 85, 170, 85, 
    170, 85, 170, 85, 170, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 170, 85, 170, 85, 
    170, 85, 170, 85, 
};

int* gitdVecTable( int room )
{
    if( room == 0 ) return gitdVec0;
    if( room == 1 ) return gitdVec1;
    if( room == 2 ) return gitdVec2;
    if( room == 3 ) return gitdVec3;
    if( room == 4 ) return gitdVec4;
    if( room == 5 ) return gitdVec5;
    if( room == 6 ) return gitdVec6;
    if( room == 7 ) return gitdVec7;
    if( room == 8 ) return gitdVec8;
    if( room == 9 ) return gitdVec9;
    return gitdVec10;
}

int* gitdRopeVecTable( int room )
{
    if( room == 0 ) return gitdRopeVec0;
    if( room == 1 ) return gitdRopeVec1;
    if( room == 2 ) return gitdRopeVec2;
    if( room == 3 ) return gitdRopeVec3;
    if( room == 4 ) return gitdRopeVec4;
    if( room == 5 ) return gitdRopeVec5;
    if( room == 6 ) return gitdRopeVec6;
    if( room == 7 ) return gitdRopeVec7;
    if( room == 8 ) return gitdRopeVec8;
    if( room == 9 ) return gitdRopeVec9;
    return gitdRopeVec10;
}

int* gitdSpkTable( int room )
{
    if( room == 0 ) return gitdSPK0;
    if( room == 1 ) return gitdSPK1;
    if( room == 2 ) return gitdSPK2;
    if( room == 3 ) return gitdSPK3;
    if( room == 4 ) return gitdSPK4;
    if( room == 5 ) return gitdSPK5;
    if( room == 6 ) return gitdSPK6;
    if( room == 7 ) return gitdSPK7;
    if( room == 8 ) return gitdSPK8;
    if( room == 9 ) return gitdSPK9;
    return gitdSPK10;
}

int* gitdAcidDropTable( int room )
{
    if( room == 0 ) return gitdAcidDrop0;
    if( room == 1 ) return gitdAcidDrop1;
    if( room == 2 ) return gitdAcidDrop2;
    if( room == 3 ) return gitdAcidDrop3;
    if( room == 4 ) return gitdAcidDrop4;
    if( room == 5 ) return gitdAcidDrop5;
    if( room == 6 ) return gitdAcidDrop6;
    if( room == 7 ) return gitdAcidDrop7;
    if( room == 8 ) return gitdAcidDrop8;
    if( room == 9 ) return gitdAcidDrop9;
    return gitdAcidDrop10;
}

// -----------------------------------------------------------------------------
// Framebuffer primitives - a real, readable 128x64 pixel buffer, laid out
// exactly like every other game's own SSD1306 page-byte model (one int
// per (column,page), bit N = pixel row N within that page) so streaming
// it to md_drawColumn() needs no repacking. Any (x,y) outside the visible
// 0-127/0-63 range is silently rejected rather than shifted, both because
// it's meaningless off-screen and to avoid ever shifting a negative
// value (see this project's own well-documented logical-vs-arithmetic-
// shift bug family).
// -----------------------------------------------------------------------------

int[1024] gitdFrameBuffer;

void gitdClearBuffer( void )
{
    int i;
    for( i = 0; i < 1024; i++ ) gitdFrameBuffer[ i ] = 0;
}

void gitdSetPixel( int x, int y, int val )
{
    if( x < 0 || x > 127 || y < 0 || y > 63 ) return;
    int idx = x + ( ( y >> 3 ) * 128 );
    int bit = 1 << ( y & 7 );
    if( val ) gitdFrameBuffer[ idx ] = gitdFrameBuffer[ idx ] | bit;
    else gitdFrameBuffer[ idx ] = gitdFrameBuffer[ idx ] & ( 0xFF - bit );
}

int gitdGetPixel( int x, int y )
{
    if( x < 0 || x > 127 || y < 0 || y > 63 ) return 0;
    int idx = x + ( ( y >> 3 ) * 128 );
    int bit = 1 << ( y & 7 );
    if( gitdFrameBuffer[ idx ] & bit ) return 1;
    return 0;
}

void gitdInvertPixel( int x, int y )
{
    gitdSetPixel( x, y, 1 - gitdGetPixel( x, y ) );
}

int gitdAbs( int v )
{
    if( v < 0 ) return -v;
    return v;
}

// -----------------------------------------------------------------------------
// Bresenham line drawing (drawLine/drawRope) - direct integer translation,
// no floats needed. drawRope alternates on/off every pixel to render as a
// dashed line, matching upstream's own drawPixelFlag toggle exactly.
// -----------------------------------------------------------------------------

void gitdDrawLine( int x0, int y0, int x1, int y1 )
{
    int dx = gitdAbs( x1 - x0 );
    int dy = gitdAbs( y1 - y0 );
    int sx = 1; if( x0 >= x1 ) sx = -1;
    int sy = 1; if( y0 >= y1 ) sy = -1;
    int err = dx - dy;
    while( 1 )
    {
        gitdSetPixel( x0, y0, 1 );
        if( x0 == x1 && y0 == y1 ) break;
        int err2 = err * 2;
        if( err2 > -dy ) { err = err - dy; x0 = x0 + sx; }
        if( err2 < dx ) { err = err + dx; y0 = y0 + sy; }
    }
}

void gitdDrawRope( int x0, int y0, int x1, int y1 )
{
    int dx = gitdAbs( x1 - x0 );
    int dy = gitdAbs( y1 - y0 );
    int sx = 1; if( x0 >= x1 ) sx = -1;
    int sy = 1; if( y0 >= y1 ) sy = -1;
    int err = dx - dy;
    int drawFlag = 1;
    while( 1 )
    {
        if( drawFlag ) gitdSetPixel( x0, y0, 1 );
        drawFlag = 1 - drawFlag;
        if( x0 == x1 && y0 == y1 ) break;
        int err2 = err * 2;
        if( err2 > -dy ) { err = err - dy; x0 = x0 + sx; }
        if( err2 < dx ) { err = err + dx; y0 = y0 + sy; }
    }
}

// -----------------------------------------------------------------------------
// Sprite blit (non-vector sprites: gilbert_GITD, the balloon, the timeout
// enemy, world pickups, HUD digits/labels, title/intro pictures) - the
// same ESPKIT-format `[width, raw_pixel_height, ...]` header and partial-
// page-rounding convention already proven for Nohzdyve's own
// `ndvBlitzSprite()` (this MEGA-compilation shim's own sprite tables all
// share it, not just Nohzdyve's). Unlike Nohzdyve's per-page compose-
// buffer model, this game keeps a real random-access pixel framebuffer,
// so rather than a per-(column,page) query, `gitdBlitzGitd()` walks the
// sprite's own real column/page footprint directly and OR-composites
// each byte straight into `gitdFrameBuffer` (matching upstream's own
// `drawSelfMasked` - self-masked, never explicitly clears a pixel).
// -----------------------------------------------------------------------------

int gitdRecupeLineY( int valeur )
{
    if( valeur >= 0 ) return valeur >> 3;
    return -( ( -valeur + 7 ) >> 3 );
}

int gitdRecupeDecalageY( int valeur )
{
    return valeur - ( gitdRecupeLineY( valeur ) << 3 );
}

int gitdSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown ) return ( input << decalage ) & 0xFF;
    return input >> ( 8 - decalage );
}

int gitdPageCount( int* table )
{
    int rawH = table[ 1 ];
    int pages = rawH >> 3;
    if( ( rawH - ( pages << 3 ) ) != 0 ) pages = pages + 1;
    return pages;
}

// wMax/picByte/recupeLineY/spriteYDecalage never depend on which column
// or page is being drawn (only x/y/frame/table, all fixed for the whole
// call) - and spriteYLine only depends on the page, not the column - so
// hoisting them out of the (page,col) loops avoids recomputing the same
// values on every single pixel. Proactively applied here (not waiting
// for a report) since this project already found and fixed the exact
// same pattern once in Nohzdyve's own ndvOrBlit()/ndvXorBlit(), and this
// game's own widest sprites (`intro_GITD` at a full 128 columns,
// `MainTitle_GITD` at 77) are even wider than what triggered that bug.
void gitdBlitzGitd( int x, int y, int* table, int frame )
{
    int w = table[ 0 ];
    int pages = gitdPageCount( table );
    int wMax = ( pages * w ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = gitdRecupeLineY( y );
    int spriteYDecalage = gitdRecupeDecalageY( y );

    int firstPage = recupeLineY;
    int pageMax = firstPage + pages;
    if( firstPage < 0 ) firstPage = 0;
    if( pageMax > 7 ) pageMax = 7;
    int cMin = x, cMax = x + w - 1;
    if( cMin < 0 ) cMin = 0;
    if( cMax > 127 ) cMax = 127;

    int page, col;
    for( page = firstPage; page <= pageMax; page++ )
    {
        int spriteYLine = page - recupeLineY;
        for( col = cMin; col <= cMax; col++ )
        {
            int scanA = ( col - x ) + ( spriteYLine * w ) + 2;
            int outByte;
            if( scanA > wMax ) outByte = 0x00;
            else outByte = gitdSplitSpriteDecalageY( spriteYDecalage, table[ scanA + picByte ], 1 );

            if( spriteYLine > 0 )
            {
                int scanB = ( col - x ) + ( ( spriteYLine - 1 ) * w ) + 2;
                if( scanB <= wMax )
                  outByte = outByte | gitdSplitSpriteDecalageY( spriteYDecalage, table[ scanB + picByte ], 0 );
            }
            if( outByte )
            {
                int idx = col + ( page * 128 );
                gitdFrameBuffer[ idx ] = gitdFrameBuffer[ idx ] | outByte;
            }
        }
    }
}

void gitdDrawRecBW( int x, int y, int x2, int y2, int col )
{
    int x_, y_;
    for( y_ = y; y_ <= y2; y_++ )
      for( x_ = x; x_ <= x2; x_++ )
        gitdSetPixel( x_, y_, col );
}

void gitdInvertPix( int flip, int x, int y, int x2, int y2 )
{
    int x_, y_;
    if( !flip ) return;
    for( y_ = y; y_ <= y2; y_++ )
      for( x_ = x; x_ <= x2; x_++ )
        gitdInvertPixel( x_, y_ );
}

// -----------------------------------------------------------------------------
// Timer helpers (identical shape to Nohzdyve's own NdvTimer - matches
// upstream's own TIMERGITD class exactly, the two games share this
// timer design byte-for-byte).
// -----------------------------------------------------------------------------

void gitdTimerInitP( GitdTimer* t, int interval )
{
    t->startTime = 0;
    t->interval = interval;
    t->activ = 0;
}

void gitdTimerActivateP( GitdTimer* t ) { t->activ = 1; }
void gitdTimerDeactivateP( GitdTimer* t ) { t->activ = 0; }

int gitdTimerTriggerP( GitdTimer* t )
{
    if( t->activ == 0 ) return 0;
    if( t->startTime < t->interval ) { t->startTime = t->startTime + 1; return 0; }
    t->startTime = 0;
    return 1;
}

// -----------------------------------------------------------------------------
// Vector-drawing engine (DrawVector and friends) - walks a room's own
// small command list, drawing wall/stalactite/floor/line/platform
// geometry directly into the framebuffer via Bresenham lines.
// -----------------------------------------------------------------------------

void gitdSetVectorStartPos( int x, int y )
{
    gitdLastX = x;
    gitdLastY = y;
}

void gitdDrawStalagtite( int x0, int x1, int y )
{
    int step = 3;
    y = y - 3;
    int alt = y + 3;
    int fmx = x0, fmy = y + 3;
    int smx = x0 + 3, smy = y;
    while( 1 )
    {
        gitdDrawLine( fmx, fmy, smx, smy );
        x0 = x0 + step;
        if( x0 <= x1 )
        {
            fmx = smx; fmy = smy;
            smx = x0;
            if( alt == y ) alt = y + 3; else alt = y;
            smy = alt;
        }
        else
        {
            gitdLastX = x0 - step;
            gitdLastY = smy;
            break;
        }
    }
}

void gitdDrawWalls( int y0, int y1, int x )
{
    int y0_, y1_;
    if( y0 < y1 ) { y0_ = y0; y1_ = y1; } else { y0_ = y1; y1_ = y0; }
    int step = 3;
    int ys = y0_;
    int alt = x;
    int fmx = x, fmy = y0_;
    int smx = x, smy = y0_ + 2;
    while( 1 )
    {
        gitdDrawLine( fmx, fmy, smx, smy );
        ys = ys + step;
        if( ys <= y1_ )
        {
            fmx = smx; fmy = smy;
            smy = ys;
            if( alt == x ) alt = x - 1; else alt = x;
            smx = alt;
        }
        else
        {
            gitdLastX = smx;
            gitdLastY = y1;
            break;
        }
    }
}

// Room 0's own two floating-platform shapes (Small_Ilot/Big_Ilot) are
// themselves plain Vec command lists, drawn via a recursive call back
// into DrawVector() - self-recursion needs no forward declaration since
// gitdDrawVector already knows its own name within its own body.
void gitdDrawVector( int x, int y, int* vec )
{
    gitdLastX = x;
    gitdLastY = y;
    int mx = x, my = y;
    int command, xPos, yPos;
    int ps = 0;
    while( 1 )
    {
        command = vec[ ps ];
        xPos = vec[ ps + 1 ] + mx;
        yPos = vec[ ps + 2 ] + my;
        if( command == GITD_EXIT ) return;
        else if( command == GITD_POS ) gitdSetVectorStartPos( xPos, yPos );
        else if( command == GITD_ROOF ) gitdDrawStalagtite( gitdLastX, xPos, yPos );
        else if( command == GITD_WALL ) gitdDrawWalls( gitdLastY, yPos, xPos );
        else if( command == GITD_VEC )
        {
            gitdDrawLine( gitdLastX, gitdLastY, xPos, yPos );
            gitdLastX = xPos;
            gitdLastY = yPos;
        }
        else if( command == GITD_ILOT ) gitdDrawVector( xPos, yPos, gitdSmall_Ilot );
        else if( command == GITD_BIG_ILOT ) gitdDrawVector( xPos, yPos, gitdBig_Ilot );
        ps = ps + 3;
    }
}

void gitdDrawRopeVector( int x, int y, int* vec )
{
    int addr = 0;
    int hv, rx, ry, len;
    while( 1 )
    {
        hv = vec[ addr ]; addr = addr + 1;
        rx = vec[ addr ] + x; addr = addr + 1;
        ry = vec[ addr ] + y; addr = addr + 1;
        len = vec[ addr ]; addr = addr + 1;
        if( hv == GITD_EXIT ) return;
        else if( hv == GITD_HORIZONTAL ) gitdDrawRope( rx, ry, rx + len, ry );
        else if( hv == GITD_VERTICAL ) gitdDrawRope( rx, ry, rx, ry + len );
        else return;
    }
}

// -----------------------------------------------------------------------------
// Collision primitives
// -----------------------------------------------------------------------------

int gitdColidUniv( int x1, int w1, int y1, int h1, int x2, int w2, int y2, int h2 )
{
    if( x1 > ( x2 + w2 ) ) return 0;
    if( ( x1 + w1 ) < x2 ) return 0;
    if( y1 > ( y2 + h2 ) ) return 0;
    if( ( y1 + h1 ) < y2 ) return 0;
    return 1;
}

int gitdColWall( int cx, int cy )
{
    int x_, y_;
    for( y_ = cy + 5; y_ < cy + 7; y_++ )
      for( x_ = cx + 2; x_ < cx + 5; x_++ )
        if( gitdGetPixel( x_, y_ ) )
        {
            gitdJumpCycle = 2;
            return 1;
        }

    if( gitdGetPixel( cx + 3, cy + 4 ) )
    {
        if( gitdJumpCycleMem == 0 ) { gitdJumpCycle = 1; gitdJumpCycleMem = 1; }
        else if( gitdJumpCycleMem == 1 ) { gitdJumpCycle = 0; gitdJumpCycleMem = 0; }
        return 2;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Sound - Sound_GITD is the same 255-freq bit-bang formula every other
// Daniel-C Sound() in this project already uses; ported onto the shared
// Sound(). SoundSystem_GITD's own 2-note bursts get a small frame-stepped
// player so the second note doesn't just silently replace the first on
// the same tick (Vircon32's audio channel has no queue).
// -----------------------------------------------------------------------------

void gitdStartSfx2( int f0, int d0, int f1, int d1 )
{
    gitdSfxFreq0 = f0; gitdSfxDur0 = d0;
    gitdSfxFreq1 = f1; gitdSfxDur1 = d1;
    gitdSfxLen = 2;
    gitdSfxPos = 0;
    gitdSfxWaitFrames = 0;
}

void gitdStartSfx1( int f0, int d0 )
{
    gitdSfxFreq0 = f0; gitdSfxDur0 = d0;
    gitdSfxLen = 1;
    gitdSfxPos = 0;
    gitdSfxWaitFrames = 0;
}

void gitdAdvanceSfx( void )
{
    if( gitdSfxPos >= gitdSfxLen ) return;
    if( gitdSfxWaitFrames > 0 ) { gitdSfxWaitFrames = gitdSfxWaitFrames - 1; return; }
    int freq, dur;
    if( gitdSfxPos == 0 ) { freq = gitdSfxFreq0; dur = gitdSfxDur0; }
    else { freq = gitdSfxFreq1; dur = gitdSfxDur1; }
    Sound( freq, dur );
    gitdSfxWaitFrames = dur / 4;
    if( gitdSfxWaitFrames < 1 ) gitdSfxWaitFrames = 1;
    gitdSfxPos = gitdSfxPos + 1;
}

void gitdSoundSystem( int snd )
{
    if( snd == 0 ) gitdStartSfx2( 100, 100, 140, 100 );
    else if( snd == 1 ) gitdStartSfx2( 1, 2, 200, 1 );
    else if( snd == 2 ) gitdStartSfx2( 1, 2, 200, 1 );
    else if( snd == 3 ) gitdStartSfx2( 150, 2, 175, 2 );
    else if( snd == 4 ) gitdStartSfx1( 150, 1 );
}

void gitdSubLive( void )
{
    if( gitdLives > 0 ) gitdLives = gitdLives - 1;
}

// -----------------------------------------------------------------------------
// Player physics (flattened from Sprite_GITD)
// -----------------------------------------------------------------------------

void gitdInitDeadAnim( void )
{
    if( gitdDDead == 4 )
    {
        gitdSubLive();
        gitdP.deadFrameStep = 110;
        gitdP.x = gitdP.x + gitdP.stretchAdd;
        gitdP.stretchAdd = 0;
        gitdP.stretch = 0;
        gitdDDead = 0;
    }
}

void gitdForcedJumpTrig( void )
{
    gitdP.gravitySpeed = -1.2;
    gitdP.floorStat = 0;
    gitdP.oneClick = 0;
}

void gitdRopeJump( void )
{
    if( gitdDDead ) return;
    if( !isFirePressed() ) gitdP.oneClick2 = 1;
    if( isFirePressed() && gitdP.oneClick2 )
    {
        gitdUpDown = 2;
        gitdRopeMode = 0;
        if( isLeftPressed() )
        {
            gitdJumpCycle = 0;
            gitdJumpCycleMem = 0;
            gitdForcedJumpTrig();
        }
        else if( isRightPressed() )
        {
            gitdJumpCycle = 1;
            gitdJumpCycleMem = 1;
            gitdForcedJumpTrig();
        }
    }
}

void gitdSpriteStretchSelect( void )
{
    int s = gitdP.stretch;
    if( s >= -29 && s <= 29 )
    {
        gitdRopeJump();
        gitdP.stretchAdd = 0;
    }
    else if( s >= -50 && s <= -30 ) gitdP.stretchAdd = -2;
    else if( s >= 30 && s <= 50 ) gitdP.stretchAdd = 2;
    else if( s >= -68 && s <= -51 )
    {
        gitdP.x = gitdP.x - 4;
        gitdP.stretchAdd = 0;
        gitdP.stretch = 0;
    }
    else if( s >= 51 && s <= 68 )
    {
        gitdP.x = gitdP.x + 4;
        gitdP.stretchAdd = 0;
        gitdP.stretch = 0;
    }
}

int gitdColWallP( void )
{
    return gitdColWall( gitdP.x, gitdP.y );
}

int gitdMainAnimSelect( void )
{
    int base;
    if( gitdP.floorStat ) base = gitdP.animFrame;
    else if( gitdP.direction == 6 ) base = gitdP.animFrame;
    else base = 1;
    return base + gitdP.direction;
}

int gitdDeadAnim( void )
{
    if( gitdP.deadFrameStep > 0 ) gitdP.deadFrameStep = gitdP.deadFrameStep - 1;
    if( gitdP.deadFrameStep > 106 )
    {
        return 9;
    }
    else
    {
        if( gitdP.deadFrameStep > 98 ) Sound( 1, 4 );
        if( gitdP.deadFrameStep == 0 ) gitdDDead = 3;
        return 10;
    }
}

int gitdNoiseMain( void )
{
    if( gitdP.deadFrameStep != 0 ) gitdP.deadFrameStep = gitdP.deadFrameStep - 1;
    else gitdP.deadFrameStep = 3;
    return 11 + gitdP.deadFrameStep;
}

int gitdGetMainFrame( void )
{
    if( gitdDDead == 0 ) return gitdMainAnimSelect();
    if( gitdDDead == 1 )
    {
        if( gitdP.gravitySpeed < 0 ) gitdP.gravitySpeed = 0;
        return gitdMainAnimSelect();
    }
    if( gitdDDead == 2 ) return gitdDeadAnim();
    if( gitdDDead == 3 || gitdDDead == 4 ) return gitdNoiseMain();
    return 0;
}

void gitdJumpTrig( void )
{
    if( gitdDDead ) return;
    if( gitdP.floorStat && gitdP.oneClick )
    {
        gitdP.gravitySpeed = -1.2;
        gitdP.floorStat = 0;
        gitdP.oneClick = 0;
    }
}

void gitdGravityReset( void )
{
    gitdP.gravitySpeed = 0;
}

void gitdGravityUpdate( void )
{
    if( ( gitdP.gravitySpeed + 0.2 ) < 3 ) gitdP.gravitySpeed = gitdP.gravitySpeed + 0.2;
    else gitdDDead = 1;
    gitdP.y = gitdP.y + gitdP.gravitySpeed;
}

void gitdFloorDirectCheck( void )
{
    if( gitdColWall( gitdP.x, gitdP.y + 1 ) ) gitdDDead = 2;
}

void gitdGravitycalcule( void )
{
    if( gitdDDead == 1 ) gitdFloorDirectCheck();
    if( gitdP.gravitySpeed >= 0 )
    {
        while( gitdColWall( gitdP.x, gitdP.y ) )
        {
            gitdGravityReset();
            gitdP.stretch = 0;
            gitdP.floorStat = 1;
            if( !isFirePressed() ) gitdP.oneClick = 1;
            gitdP.y = gitdP.y - 1;
            if( gitdDDead == 1 ) gitdDDead = 2;
        }
    }
    else
    {
        while( gitdColWall( gitdP.x, gitdP.y ) )
        {
            gitdGravityReset();
            gitdP.y = gitdP.y + 1;
            gitdJumpCycleMem = 2;
        }
    }

    if( gitdP.gravitySpeed > 1 )
    {
        gitdP.floorStat = 0;
        gitdP.oneClick = 0;
        gitdP.oneClick2 = 0;
    }
}

void gitdSetSpritePos( int x, int y )
{
    gitdP.x = GITD_X_OFFSET + x;
    gitdP.y = GITD_Y_OFFSET + y;
}

void gitdNeutral( void )
{
    gitdP.trigSpeedRestriction = gitdP.speedRestriction;
    gitdP.animFrame = 1;
}

void gitdGo2Left( void )
{
    int trig = 0;
    if( gitdDDead == 1 || gitdDDead == 2 || gitdDDead == 3 ) return;
    gitdInitDeadAnim();
    if( gitdP.trigSpeedRestriction < gitdP.speedRestriction )
    {
        gitdP.trigSpeedRestriction = gitdP.trigSpeedRestriction + 1;
    }
    else
    {
        if( gitdP.x > 0 )
        {
            gitdP.x = gitdP.x - 1;
            if( gitdP.floorStat && gitdP.animFrame == 2 ) trig = 1;
        }
        if( gitdColWallP() )
        {
            trig = 0;
            gitdP.x = gitdP.x + 1;
            if( gitdP.gravitySpeed < 0 ) gitdP.gravitySpeed = 0;
        }
        else
        {
            gitdP.trigSpeedRestriction = 0;
            if( gitdP.animFrame < 2 ) gitdP.animFrame = gitdP.animFrame + 1; else gitdP.animFrame = 0;
            gitdP.direction = 0;
            if( trig ) gitdSoundSystem( 1 );
        }
    }
}

void gitdGo2Right( void )
{
    int trig = 0;
    if( gitdDDead == 1 || gitdDDead == 2 || gitdDDead == 3 ) return;
    gitdInitDeadAnim();
    if( gitdP.trigSpeedRestriction < gitdP.speedRestriction )
    {
        gitdP.trigSpeedRestriction = gitdP.trigSpeedRestriction + 1;
    }
    else
    {
        if( gitdP.x < 127 )
        {
            gitdP.x = gitdP.x + 1;
            if( gitdP.floorStat && gitdP.animFrame == 2 ) trig = 1;
        }
        if( gitdColWallP() )
        {
            gitdP.x = gitdP.x - 1;
            if( gitdP.gravitySpeed < 0 ) gitdP.gravitySpeed = 0;
        }
        else
        {
            gitdP.trigSpeedRestriction = 0;
            if( gitdP.animFrame < 2 ) gitdP.animFrame = gitdP.animFrame + 1; else gitdP.animFrame = 0;
            gitdP.direction = 3;
            if( trig ) gitdSoundSystem( 1 );
        }
    }
}

void gitdGoClimb( void )
{
    int trig = 0;
    if( gitdDDead ) return;
    if( gitdP.trigSpeedRestriction < gitdP.speedRestriction )
    {
        gitdP.trigSpeedRestriction = gitdP.trigSpeedRestriction + 1;
    }
    else
    {
        gitdP.y = gitdP.y - 1;
        trig = 1;
        if( gitdColWallP() )
        {
            if( gitdP.animFrame > 0 ) gitdP.animFrame = gitdP.animFrame - 1;
            gitdP.y = gitdP.y + 1;
            trig = 0;
        }
        gitdP.gravitySpeed = 0;
        gitdP.trigSpeedRestriction = 0;
        if( gitdP.animFrame < 2 ) gitdP.animFrame = gitdP.animFrame + 1; else gitdP.animFrame = 0;
        if( trig ) gitdSoundSystem( 3 );
    }
}

void gitdGoFall( void )
{
    if( gitdDDead ) return;
    if( gitdP.trigSpeedRestriction < gitdP.speedRestriction )
    {
        gitdP.trigSpeedRestriction = gitdP.trigSpeedRestriction + 1;
    }
    else
    {
        gitdP.y = gitdP.y + 1;
        gitdP.gravitySpeed = 2;
        gitdP.trigSpeedRestriction = 0;
        if( gitdP.animFrame > 0 ) gitdP.animFrame = gitdP.animFrame - 1; else gitdP.animFrame = 2;
        gitdSoundSystem( 4 );
    }
}

void gitdLeftstretch( void )
{
    if( gitdDDead ) return;
    if( gitdP.stretch > -127 ) gitdP.stretch = gitdP.stretch - 1;
    gitdSpriteStretchSelect();
    if( gitdP.stretchAdd ) gitdP.direction = 0;
}

void gitdRightstretch( void )
{
    if( gitdDDead ) return;
    if( gitdP.stretch < 127 ) gitdP.stretch = gitdP.stretch + 1;
    gitdSpriteStretchSelect();
    if( gitdP.stretchAdd ) gitdP.direction = 3;
}

void gitdNeutralStretch( void )
{
    if( gitdP.stretch > 29 || gitdP.stretch < -29 ) return;
    gitdP.stretch = 0;
    gitdSpriteStretchSelect();
}

// -----------------------------------------------------------------------------
// Acid drops
// -----------------------------------------------------------------------------

void gitdInitAcidDrop( int slot ) { gitdAcid[ slot ].actif = 0; }

void gitdSetAcidDrop( int slot, int x, int y )
{
    gitdAcid[ slot ].actif = 1;
    gitdAcid[ slot ].x = x + GITD_X_OFFSET;
    gitdAcid[ slot ].y = y + GITD_Y_OFFSET;
    gitdAcid[ slot ].delays = 35;
}

void gitdUpdateAcidDrop( int slot )
{
    if( gitdAcid[ slot ].delays > 0 ) gitdAcid[ slot ].delays = gitdAcid[ slot ].delays - 1;
    else gitdAcid[ slot ].y = gitdAcid[ slot ].y + 1;
}

int gitdColAcidDrop( int x, int y, int slot )
{
    if( gitdGetPixel( x, y ) )
    {
        gitdAcid[ slot ].actif = 0;
        return 0;
    }
    return 1;
}

void gitdReleaseNewDrop( void )
{
    int* table = gitdAcidDropTable( gitdLvl.room );
    int nb = table[ 0 ];
    int t;
    for( t = 0; t < 6; t++ )
    {
        if( !gitdAcid[ t ].actif )
        {
            int rnd = ( arand( nb ) * 2 ) + 1;
            gitdSetAcidDrop( t, table[ rnd ], table[ rnd + 1 ] );
            return;
        }
    }
}

// Upstream's own CalculateAcidDrop()/DrawAcidDrop() share a single
// "Flip" toggle (gitdAcidFlip here) that halves their effective update
// rate: on a Flip==0 tick, physics update AND drawing both happen, then
// Flip is set to 1; on the following Flip==1 tick, neither happens -
// instead DrawAcidDrop() just resets Flip back to 0 and polls whether a
// new drop should release. A first draft of this port missed this
// entirely (dropped straight to unconditional per-tick update/draw),
// caught by re-reading upstream carefully before ever compiling rather
// than discovering it as a "drops fall too fast" report later.
void gitdCalculateAcidDrop( void )
{
    if( gitdAcidFlip == 0 )
    {
        int t;
        for( t = 0; t < 6; t++ )
        {
            if( gitdAcid[ t ].actif )
            {
                gitdColAcidDrop( gitdAcid[ t ].x, gitdAcid[ t ].y, t );
                gitdUpdateAcidDrop( t );
            }
        }
    }
}

void gitdDrawAcidDrop( void )
{
    if( gitdAcidFlip == 0 )
    {
        int t;
        for( t = 0; t < 6; t++ )
          if( gitdAcid[ t ].actif )
            gitdSetPixel( gitdAcid[ t ].x, gitdAcid[ t ].y, 1 );
        gitdAcidFlip = 1;
    }
    else
    {
        gitdAcidFlip = 0;
        if( gitdTimerTriggerP( &gitdTM2 ) ) gitdReleaseNewDrop();
    }
}

// -----------------------------------------------------------------------------
// Balloon patrol enemy
// -----------------------------------------------------------------------------

void gitdInitBalloon( void )
{
    gitdBalloon.actif = 0;
    gitdBalloon.x = 0;
    gitdBalloon.y = 0;
}

void gitdSetBalloon( int x, int y )
{
    gitdBalloon.actif = 1;
    gitdBalloon.x = x;
    gitdBalloon.y = y;
    gitdBalloon.step = 0;
    gitdBalloon.anim = 0;
    gitdBalloon.xmem = x;
    gitdBalloon.ymem = y;
}

void gitdRsetBalloon( void )
{
    gitdBalloon.actif = 1;
    gitdBalloon.x = gitdBalloon.xmem;
    gitdBalloon.y = gitdBalloon.ymem;
}

int gitdCollXBalloon( void )
{
    if( gitdGetPixel( gitdBalloon.x + 1, gitdBalloon.y + 5 ) || gitdGetPixel( gitdBalloon.x + 2, gitdBalloon.y + 5 ) ) return 1;
    return 0;
}

int gitdCollYBalloon( void )
{
    if( gitdGetPixel( gitdBalloon.x + 2, gitdBalloon.y + 6 ) || gitdGetPixel( gitdBalloon.x + 2, gitdBalloon.y + 7 ) ) return 1;
    return 0;
}

void gitdBalloonUpdateGravity( void )
{
    if( gitdBalloon.step < 2 ) gitdBalloon.step = gitdBalloon.step + 0.2;
    gitdBalloon.y = gitdBalloon.y + gitdBalloon.step;
    gitdBalloon.anim = 0;
    while( gitdCollYBalloon() )
    {
        gitdBalloon.anim = 1;
        gitdBalloon.step = -0.8;
        gitdBalloon.y = gitdBalloon.y - 1;
    }
}

void gitdBalloonLeftmove( void )
{
    if( !gitdCollXBalloon() )
    {
        gitdBalloon.x = gitdBalloon.x - 1;
    }
    else
    {
        gitdBalloon.actif = 0;
        gitdBalloon.step = 0;
        gitdRsetBalloon();
    }
}

void gitdUpdateBalloon( void )
{
    if( !gitdBalloon.actif ) return;
    gitdBalloonLeftmove();
    gitdBalloonUpdateGravity();
}

void gitdUpdateBalloonTick( void )
{
    if( gitdTimerTriggerP( &gitdTM3 ) ) gitdUpdateBalloon();
}

void gitdDrawBalloon( void )
{
    if( gitdBalloon.actif )
      gitdBlitzGitd( gitdBalloon.x, gitdBalloon.y, gitdBallon_SPK_GITD, gitdBalloon.anim );
}

// -----------------------------------------------------------------------------
// Timeout chaser enemy (flattened from TIMEOUT)
// -----------------------------------------------------------------------------

void gitdTimeoutDeactivate( void )
{
    gitdTMout.actif = 0;
    gitdTMout.flip = 0;
}

void gitdTimeoutActivate( void )
{
    if( gitdTMout.actif ) return;
    gitdTMout.actif = 1;
    gitdTMout.x = 0;
    gitdTMout.y = 0;
    gitdTMout.rl = 1;
    gitdTMout.ud = 1;
}

void gitdTimeoutSetTime( void ) { gitdTMout.time = 4000; }

void gitdTimeoutResetTime( void )
{
    if( gitdTMout.actif && gitdTMout.time == 0 ) gitdTMout.time = 2048;
}

int gitdTimeoutFlipFrm( void )
{
    if( gitdTMout.flip < 10 ) gitdTMout.flip = gitdTMout.flip + 1; else gitdTMout.flip = 0;
    if( gitdTMout.flip < 6 ) return 0;
    return 1;
}

void gitdTimeoutGo2Right( void )
{
    if( gitdTMout.x < ( GITD_X_OFFSET + 96 ) ) gitdTMout.x = gitdTMout.x + 1;
    else gitdTMout.rl = 0;
}

void gitdTimeoutGo2Left( void )
{
    if( gitdTMout.x > GITD_X_OFFSET ) gitdTMout.x = gitdTMout.x - 1;
    else gitdTMout.rl = 1;
}

void gitdTimeoutGo2Up( void )
{
    if( gitdTMout.y < ( GITD_Y_OFFSET + 60 ) ) gitdTMout.y = gitdTMout.y + 1;
    else gitdTMout.ud = 1;
}

void gitdTimeoutGo2Down( void )
{
    if( gitdTMout.y > GITD_Y_OFFSET ) gitdTMout.y = gitdTMout.y - 1;
    else gitdTMout.ud = 0;
}

void gitdTimeoutFunction( int px, int py )
{
    if( gitdDDead > 2 ) return;
    if( gitdTMout.time > 0 ) gitdTMout.time = gitdTMout.time - 1;
    else gitdTimeoutActivate();
    if( !gitdTMout.actif ) return;
    if( gitdColidUniv( px + 1, 4, py + 2, 5, gitdTMout.x, 5, gitdTMout.y, 3 ) )
      if( !gitdDDead ) gitdDDead = 1;
    if( gitdTMout.ud ) gitdTimeoutGo2Down(); else gitdTimeoutGo2Up();
    if( gitdTMout.rl ) gitdTimeoutGo2Right(); else gitdTimeoutGo2Left();
    gitdBlitzGitd( gitdTMout.x, gitdTMout.y, gitdTimeOut_GITD, gitdTimeoutFlipFrm() );
}

// -----------------------------------------------------------------------------
// Room/door/pickup logic
// -----------------------------------------------------------------------------

int gitdCheckMainColision( int t, int bx, int by )
{
    int add_ = 0, sous_ = 0;
    if( t == GITD_DOOR ) { add_ = 3; sous_ = 7; }
    if( !gitdColidUniv( gitdP.x + 1 + gitdP.stretchAdd, 4 + gitdP.stretchAdd, gitdP.y + 1, 4, bx + 1 - add_, 3 + sous_, by + 3, 3 ) ) return 0;
    return 1;
}

void gitdGotoNexRoom( int doorNumber );
void gitdCheckIfBallExist( int* spk );

int gitdPickupSprites( int t, int x, int y, int i )
{
    if( gitdCheckMainColision( t, x, y ) )
    {
        if( t == GITD_GOLD || t == GITD_GOLD2 )
        {
            if( gitdVisibleSprite[ i ] == 1 )
            {
                gitdScores = gitdScores + 282;
                gitdVisibleSprite[ i ] = 0;
                gitdSoundSystem( 0 );
            }
            return 0;
        }
        if( t == GITD_KEY )
        {
            if( gitdVisibleKeyAndDoors[ i ] == 0 )
            {
                gitdScores = gitdScores + 496;
                gitdVisibleKeyAndDoors[ i ] = 1;
                if( i == 9 ) gitdVisibleKeyAndDoors[ 21 ] = 1;
                gitdSoundSystem( 0 );
            }
            return 0;
        }
        if( t == GITD_DOOR )
        {
            gitdGotoNexRoom( i );
            return 1;
        }
        return 0;
    }
    return 0;
}

void gitdInitNextRoom( void )
{
    int t;
    for( t = 0; t < 6; t++ ) gitdInitAcidDrop( t );
    gitdCheckIfBallExist( gitdSpkTable( gitdLvl.room ) );
}

void gitdGotoNexRoom( int doorNumber )
{
    int type_, room_, pos_;
    int ps = 0;
    while( 1 )
    {
        type_ = gitdInAndOut_GITD[ ps ];
        room_ = gitdInAndOut_GITD[ ps + 1 ];
        pos_ = gitdInAndOut_GITD[ ps + 2 ];
        if( type_ == doorNumber )
        {
            int keyIdx = type_;
            if( type_ > 99 ) keyIdx = 0;
            if( gitdVisibleKeyAndDoors[ keyIdx ] == 0 ) return;
            gitdLvl.memRoom = gitdLvl.room;
            gitdLvl.room = room_;
            gitdTimeoutSetTime();
            gitdSetSpritePos( gitdPosition_GITD[ pos_ * 2 ], gitdPosition_GITD[ ( pos_ * 2 ) + 1 ] );
            gitdInitNextRoom();
            gitdTimeoutDeactivate();
            return;
        }
        ps = ps + 3;
    }
}

void gitdDrawSprites( int* spk )
{
    int type_, xPos, yPos, itemPos;
    int ps = 0;
    while( 1 )
    {
        type_ = spk[ ps ];
        xPos = spk[ ps + 1 ] + GITD_X_OFFSET;
        yPos = spk[ ps + 2 ] + GITD_Y_OFFSET;
        itemPos = spk[ ps + 3 ];
        if( gitdPickupSprites( type_, xPos, yPos, itemPos ) )
        {
            int keyIdx = itemPos;
            if( itemPos > 99 ) keyIdx = 0;
            if( type_ == GITD_DOOR && gitdVisibleKeyAndDoors[ keyIdx ] == 1 ) gitdRoomChange = 1;
        }
        if( itemPos > 99 ) itemPos = 0;
        if( type_ == GITD_GOLD ) { if( gitdVisibleSprite[ itemPos ] ) gitdBlitzGitd( xPos, yPos, gitdSprites_World_GITD, 0 ); }
        else if( type_ == GITD_GOLD2 ) { if( gitdVisibleSprite[ itemPos ] ) gitdBlitzGitd( xPos, yPos, gitdSprites_World_GITD, 1 ); }
        else if( type_ == GITD_KEY ) { if( !gitdVisibleKeyAndDoors[ itemPos ] ) gitdBlitzGitd( xPos, yPos, gitdSprites_World_GITD, 2 ); }
        else if( type_ == GITD_DOOR ) { if( gitdVisibleKeyAndDoors[ itemPos ] ) gitdBlitzGitd( xPos, yPos, gitdSprites_World_GITD, 3 ); }
        else if( type_ == GITD_EXIT ) return;
        ps = ps + 4;
    }
}

void gitdCheckIfBallExist( int* spk )
{
    int type_, xPos, yPos;
    int ps = 0;
    while( 1 )
    {
        type_ = spk[ ps ];
        xPos = spk[ ps + 1 ] + GITD_X_OFFSET;
        yPos = spk[ ps + 2 ] + GITD_Y_OFFSET;
        if( type_ == GITD_BALLONSPK ) { gitdSetBalloon( xPos, yPos ); return; }
        ps = ps + 4;
        if( type_ == GITD_EXIT ) break;
    }
    gitdInitBalloon();
}

void gitdSpriteColid( void )
{
    if( gitdDDead != 0 ) return;
    int px = gitdP.x, py = gitdP.y;
    int t;
    for( t = 0; t < 6; t++ )
    {
        if( gitdAcid[ t ].actif && gitdAcid[ t ].delays == 0 )
        {
            if( gitdColidUniv( px + 2 + gitdP.stretchAdd, 2 + gitdP.stretchAdd, py, 5, gitdAcid[ t ].x, 0, gitdAcid[ t ].y, 0 ) )
            {
                gitdDDead = 1;
                break;
            }
        }
    }
    if( gitdBalloon.actif )
    {
        if( gitdColidUniv( px + 1 + gitdP.stretchAdd, 5 + gitdP.stretchAdd, py, 5, gitdBalloon.x + 1, 2, gitdBalloon.y + 5, 2 ) )
          gitdDDead = 1;
    }
}

int gitdStdHcollision( int x1, int y1, int l_, int x2, int y2 )
{
    if( y1 != y2 || x1 + l_ < x2 || x2 < x1 ) return 0;
    return 1;
}

int gitdStdVcollision( int x1, int y1, int l_, int x2, int y2 )
{
    if( x1 != x2 || y1 + l_ < y2 || y2 + 3 < y1 ) return 0;
    return 1;
}

// Upstream's own RopeDetectionGITD/RopeProcess check "Get_GS() < 0" /
// "Get_GS() > 1.4" through a getter that returns GravitySpeed truncated
// to int8_t (GravitySpeed itself is a real float) - a genuine upstream
// truncate-toward-zero quirk (e.g. a real -0.6 truncates to 0, reading as
// "not negative"), not one of this project's own AVR-narrow-type porting
// bugs - preserved deliberately via a plain (int) cast (C's own int cast
// truncates toward zero identically) rather than comparing the true float
// sign, since there's no way to tell from the code alone whether this was
// deliberate jump-arc tuning or an accident, and this project's own
// precedent is to preserve *observed* behavior rather than guess at a
// "more correct" replacement.
int gitdGetGSTruncated( void )
{
    int v = gitdP.gravitySpeed;
    return v;
}

int gitdRopeDetection( int x_, int y_, int* rope )
{
    if( gitdGetGSTruncated() < 0 ) return 0;
    int scan = 0;
    int hv, x, y, l_;
    while( 1 )
    {
        hv = rope[ scan ];
        x = rope[ scan + 1 ] + x_;
        y = rope[ scan + 2 ] + y_;
        l_ = rope[ scan + 3 ];
        if( hv == GITD_EXIT ) return 0;
        else if( hv == GITD_VERTICAL ) { if( gitdStdVcollision( x, y, l_, gitdP.x + 3, gitdP.y + 3 ) ) return 1; }
        else if( hv == GITD_HORIZONTAL ) { if( gitdStdHcollision( x, y, l_, gitdP.x + 3, gitdP.y + 3 ) ) return 2; }
        scan = scan + 4;
    }
}

void gitdWalkingMode( void )
{
    if( gitdJumpCycleMem == 0 ) gitdGo2Left();
    else if( gitdJumpCycleMem == 1 ) gitdGo2Right();
    else if( gitdJumpCycleMem == 2 ) gitdNeutral();
}

void gitdRopeModeStep( void )
{
    if( isUpPressed() )
    {
        if( !gitdP.stretchAdd ) { if( gitdRopeMode == 2 ) gitdUpDown = 2; else gitdUpDown = 0; }
    }
    else if( isDownPressed() )
    {
        if( !gitdP.stretchAdd ) { if( gitdRopeMode == 2 ) gitdUpDown = 2; else gitdUpDown = 1; }
    }
    else if( isRightPressed() ) gitdUpDown = 3;
    else if( isLeftPressed() ) gitdUpDown = 4;
    else gitdUpDown = 2;

    if( gitdUpDown == 0 ) gitdGoClimb();
    else if( gitdUpDown == 1 ) gitdGoFall();
    else if( gitdUpDown == 2 ) gitdNeutralStretch();
    else if( gitdUpDown == 3 ) gitdRightstretch();
    else if( gitdUpDown == 4 ) gitdLeftstretch();
}

void gitdRopeProcess( void )
{
    int tmp_;
    if( gitdP.oneClick || gitdP.floorStat ) gitdJumpCycleMem = gitdJumpCycle;
    if( gitdGetGSTruncated() > 1.4 ) { gitdJumpCycle = 2; gitdJumpCycleMem = 2; }
    tmp_ = gitdRopeDetection( GITD_X_OFFSET, GITD_Y_OFFSET, gitdRopeVecTable( gitdLvl.room ) );
    if( gitdDDead ) tmp_ = 0;
    if( tmp_ )
    {
        gitdJumpCycle = 2; gitdJumpCycleMem = 2;
        gitdRopeMode = tmp_;
        if( !gitdP.stretchAdd ) gitdP.direction = 6;
        gitdGravityReset();
    }
    if( gitdRopeMode )
    {
        gitdRopeModeStep();
        gitdRopeMode = 0;
    }
    else
    {
        gitdWalkingMode();
        if( gitdTimerTriggerP( &gitdTM1 ) ) gitdGravityUpdate();
        gitdGravitycalcule();
    }
}

void gitdMovingMode( void )
{
    if( isLeftPressed() ) gitdJumpCycle = 0;
    else if( isRightPressed() ) gitdJumpCycle = 1;
    else gitdJumpCycle = 2;
    if( isFirePressed() ) gitdJumpTrig();
}

// -----------------------------------------------------------------------------
// HUD
// -----------------------------------------------------------------------------

void gitdNumCalculate( int n )
{
    gitdM10000 = n / 10000;
    gitdM1000 = ( n - ( gitdM10000 * 10000 ) ) / 1000;
    gitdM100 = ( n - ( gitdM1000 * 1000 ) - ( gitdM10000 * 10000 ) ) / 100;
    gitdM10 = ( n - ( gitdM100 * 100 ) - ( gitdM1000 * 1000 ) - ( gitdM10000 * 10000 ) ) / 10;
    gitdM1 = n - ( gitdM10 * 10 ) - ( gitdM100 * 100 ) - ( gitdM1000 * 1000 ) - ( gitdM10000 * 10000 );
}

void gitdDrawScore( int xx, int yy, int val )
{
    int x_ = xx + GITD_X_OFFSET;
    int y_ = yy + GITD_Y_OFFSET;
    gitdNumCalculate( val );
    gitdBlitzGitd( x_, y_, gitdNUMERICGITD, gitdM10000 );
    gitdBlitzGitd( x_ + 4, y_, gitdNUMERICGITD, gitdM1000 );
    gitdBlitzGitd( x_ + 8, y_, gitdNUMERICGITD, gitdM100 );
    gitdBlitzGitd( x_ + 12, y_, gitdNUMERICGITD, gitdM10 );
    gitdBlitzGitd( x_ + 16, y_, gitdNUMERICGITD, gitdM1 );
}

void gitdDrawTime( int xx, int yy )
{
    int x_ = xx + GITD_X_OFFSET;
    int y_ = yy + GITD_Y_OFFSET;
    gitdNumCalculate( gitdTMout.time );
    gitdBlitzGitd( x_, y_, gitdNUMERICGITD, gitdM1000 );
    gitdBlitzGitd( x_ + 4, y_, gitdNUMERICGITD, gitdM100 );
    gitdBlitzGitd( x_ + 8, y_, gitdNUMERICGITD, gitdM10 );
    gitdBlitzGitd( x_ + 12, y_, gitdNUMERICGITD, gitdM1 );
}

void gitdDrawLives( int x_, int y_ )
{
    int t;
    for( t = 0; t < gitdLives; t++ )
      gitdBlitzGitd( gitdDigL_GITD[ t * 2 ] + x_, gitdDigL_GITD[ ( t * 2 ) + 1 ] + y_, gitdgilbert_GITD, gitdFrm );
}

void gitdDrawCH( int x_, int y_ )
{
    gitdBlitzGitd( x_, y_, gitdCH_GITD, 0 );
    gitdBlitzGitd( 11 + x_, y_, gitdNUMERICGITD, gitdLvl.room );
}

void gitdDisplayNum( void )
{
    gitdDrawScore( 102, 0, gitdScores );
    gitdDrawTime( 106, 59 );
    gitdDrawLives( 114, 32 );
    gitdDrawCH( 114, 16 );
}

// -----------------------------------------------------------------------------
// Init
// -----------------------------------------------------------------------------

void gitdInitPan( void )
{
    gitdScores = 0;
    gitdLives = 4;
    gitdM10000 = 0; gitdM1000 = 0; gitdM100 = 0; gitdM10 = 0; gitdM1 = 0;
}

void gitdResetPickupSprite( void )
{
    int t;
    for( t = 0; t < 22; t++ ) gitdVisibleKeyAndDoors[ t ] = 0;
    gitdVisibleKeyAndDoors[ 0 ] = 1;
    for( t = 0; t < 30; t++ ) gitdVisibleSprite[ t ] = 1;
}

void gitdResetVarForNewGame( void )
{
    gitdJumpCycle = 0;
    gitdJumpCycleMem = 0;
    gitdRopeMode = 0;
    gitdUpDown = 2;
    gitdDDead = 0;
    gitdRoomChange = 0;
}

void gitdInitAllItems( void )
{
    gitdResetPickupSprite();
    gitdInitNextRoom();
    gitdInitPan();
    gitdResetVarForNewGame();
}

void gitdPlayerInit( void )
{
    gitdP.direction = 0;
    gitdP.actif = 0;
    gitdP.x = 0;
    gitdP.y = 0;
    gitdP.type = 0;
    gitdP.trigSpeedRestriction = 0;
    gitdP.speedRestriction = 4;
    gitdP.animFrame = 0;
    gitdP.gravitySpeed = 0;
    gitdP.floorStat = 1;
    gitdP.oneClick = 1;
    gitdP.oneClick2 = 1;
    gitdP.stretch = 0;
    gitdP.stretchAdd = 0;
    gitdP.deadFrameStep = 0;
    gitdDDead = 0;
}

// -----------------------------------------------------------------------------
// Main per-tick draw helpers
// -----------------------------------------------------------------------------

void gitdFrmCall( void ) { gitdFrm = gitdGetMainFrame(); }

void gitdDrawGilbert( void )
{
    gitdFrm = gitdGetMainFrame();
    gitdBlitzGitd( gitdP.x + gitdP.stretchAdd, gitdP.y, gitdgilbert_GITD, gitdFrm );
}

void gitdDrawAllSprites( void )
{
    gitdDrawGilbert();
    gitdDrawSprites( gitdSpkTable( gitdLvl.room ) );
    gitdDrawAcidDrop();
}

void gitdDrawRoomInProgress( void )
{
    gitdClearBuffer();
    gitdDrawVector( GITD_X_OFFSET, GITD_Y_OFFSET, gitdVecTable( gitdLvl.room ) );
    gitdUpdateBalloonTick();
}

// -----------------------------------------------------------------------------
// Room-transition wipe (RoomTransition) - converted into its own explicit
// state, one step every ~30ms-equivalent real ticks. `gitdOldPic` is a
// direct snapshot of gitdFrameBuffer (x_offset - 7 == 0 here, so no
// coordinate offset is even needed - a genuinely simpler mapping than it
// first looks). The mask formula needed an explicit `&0xFF` - upstream's
// own `0xFF << ((t>7)?8:t)` relies on AVR's implicit uint8_t truncation
// (shifting by 8 silently zeroing the whole byte) exactly like this
// project's very first documented bug class; Vircon32's wider int would
// otherwise keep those high bits set.
// -----------------------------------------------------------------------------

void gitdBeginRoomTransition( void )
{
    int tmp = gitdLvl.room;
    gitdLvl.room = gitdLvl.memRoom;
    gitdDrawRopeVector( GITD_X_OFFSET, GITD_Y_OFFSET, gitdRopeVecTable( gitdLvl.room ) );
    gitdLvl.room = tmp;
    gitdDisplayNum();

    int x, y;
    for( y = 0; y < 8; y++ )
      for( x = 0; x < 111; x++ )
        gitdOldPic[ x + ( y * 111 ) ] = gitdFrameBuffer[ x + ( y * 128 ) ];

    if( gitdLvl.room == 0 && gitdLvl.memRoom == 9 ) gitdResetPickupSprite();

    gitdTransitStep = 0;
    gitdTransitWait = 0;
    gitdState = GITD_STATE_ROOM_TRANSITION;
}

void gitdRoomTransitionStep( void )
{
    gitdDrawRoomInProgress();
    gitdDrawAllSprites();
    gitdDrawRopeVector( GITD_X_OFFSET, GITD_Y_OFFSET, gitdRopeVecTable( gitdLvl.room ) );
    gitdDisplayNum();

    int t = gitdTransitStep;
    int mask, mask2;
    if( t > 7 ) mask = 0x00; else mask = ( 0xFF << t ) & 0xFF;
    if( t > 7 ) mask2 = ( 0xFF << ( t - 8 ) ) & 0xFF; else mask2 = 0xFF;

    int extraBit1 = 0; if( t < 8 ) extraBit1 = 1 << t;
    int extraBit2 = 0; if( t > 7 ) extraBit2 = 1 << ( t - 8 );

    int x, y;
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 111; x++ )
        {
            int b_ = x + ( y * 111 );
            int bn_ = x + ( y * 128 );
            if( ( y % 2 ) == 0 )
              gitdFrameBuffer[ bn_ ] = ( mask & gitdOldPic[ b_ ] ) | ( ( 0xFF - mask ) & gitdFrameBuffer[ bn_ ] ) | extraBit1;
            else
              gitdFrameBuffer[ bn_ ] = ( mask2 & gitdOldPic[ b_ ] ) | ( ( 0xFF - mask2 ) & gitdFrameBuffer[ bn_ ] ) | extraBit2;
        }
    }

    gitdTransitStep = gitdTransitStep + 1;
    if( gitdTransitStep >= 16 )
    {
        gitdRoomChange = 0;
        gitdState = GITD_STATE_PLAYING;
    }
}

void gitdUpdateRoomTransition( void )
{
    if( gitdTransitWait > 0 ) { gitdTransitWait = gitdTransitWait - 1; return; }
    gitdRoomTransitionStep();
    gitdTransitWait = 1;
}

// -----------------------------------------------------------------------------
// Splash (simplified, see this file's own header comment) / title / playing
// -----------------------------------------------------------------------------

void gitdUpdateSplash( void )
{
    gitdClearBuffer();
    gitdBlitzGitd( 0, 0, gitdintro_GITD, 0 );
    gitdSplashBlink = gitdSplashBlink + 1;
    if( gitdSplashBlink > 90 ) gitdSplashBlink = 0;
    if( gitdSplashBlink < 45 ) gitdDrawRecBW( 28, 19, 36, 33, 1 );
    if( gitdFireEdge )
    {
        gitdState = GITD_STATE_TITLE;
        gitdTitleAlt = 0;
    }
}

void gitdUpdateTitle( void )
{
    gitdClearBuffer();
    gitdDrawVector( GITD_X_OFFSET, GITD_Y_OFFSET, gitdVec10 );
    gitdCalculateAcidDrop();
    gitdBlitzGitd( 6 + GITD_X_OFFSET, 6 + GITD_Y_OFFSET, gitdMainTitle_GITD, 0 );
    if( gitdTitleAlt > 50 ) gitdDrawRecBW( 25, 45, 83, 52, 0 );
    if( gitdTitleAlt < 100 ) gitdTitleAlt = gitdTitleAlt + 1; else gitdTitleAlt = 0;
    gitdDrawScore( 64, 56, gitdHiScores );
    gitdDrawAcidDrop();

    if( gitdFireEdge )
    {
        gitdInitAllItems();
        gitdPlayerInit();
        gitdVisibleKeyAndDoors[ GITD_START_ROOM ] = 1;
        gitdGotoNexRoom( GITD_START_ROOM );
        gitdVisibleKeyAndDoors[ GITD_START_ROOM ] = 0;
        gitdLvl.memRoom = 10;
        gitdTimeoutDeactivate();
        gitdTimeoutResetTime();
        gitdDDead = 4;
        gitdFrmCall();
        gitdBeginRoomTransition();
    }
}

void gitdUpdatePlaying( void )
{
    if( gitdRoomChange ) { gitdBeginRoomTransition(); return; }

    gitdDrawRoomInProgress();
    gitdMovingMode();
    gitdRopeProcess();
    gitdCalculateAcidDrop();
    gitdDrawAllSprites();
    if( !gitdRoomChange ) gitdDrawRopeVector( GITD_X_OFFSET, GITD_Y_OFFSET, gitdRopeVecTable( gitdLvl.room ) );
    gitdDrawBalloon();
    if( !gitdRoomChange ) gitdSpriteColid();
    gitdDisplayNum();
    gitdTimeoutFunction( gitdP.x, gitdP.y );

    if( gitdDDead == 3 )
    {
        if( gitdLives == 0 )
        {
            if( gitdScores > gitdHiScores )
            {
                gitdHiScores = gitdScores;
                eeprom_write_word( 0, gitdHiScores );
            }
            gitdClearBuffer();
            gitdState = GITD_STATE_TITLE;
            gitdTitleAlt = 0;
            gitdAdvanceSfx();
            return;
        }
        gitdTimeoutResetTime();
        gitdTimeoutDeactivate();
        gitdDDead = 4;
        gitdFrmCall();
    }
    gitdAdvanceSfx();
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gitdRenderFrame( void )
{
    md_beginFrame();
    int col, page;
    for( page = 0; page < 8; page++ )
      for( col = 0; col < 128; col++ )
        md_drawColumn( col, page, gitdFrameBuffer[ col + ( page * 128 ) ] );
}

void gameGilbertDownland_init( void )
{
    InitTinyJoypad();
    gitdState = GITD_STATE_SPLASH;
    gitdPrevFire = 0;
    gitdFireEdge = 0;
    gitdSplashBlink = 0;
    gitdTitleAlt = 0;
    gitdAcidFlip = 0;
    gitdHiScores = eeprom_read_word( 0 );
    if( gitdHiScores == 65535 ) gitdHiScores = 0;
    gitdScores = 0;
    gitdLives = 4;
    gitdClearBuffer();
    gitdTimerInitP( &gitdTM1, 3 ); gitdTimerActivateP( &gitdTM1 );
    gitdTimerInitP( &gitdTM2, 10 ); gitdTimerActivateP( &gitdTM2 );
    gitdTimerInitP( &gitdTM3, 2 ); gitdTimerActivateP( &gitdTM3 );
    gitdSfxLen = 0;
    gitdSfxPos = 0;
}

void gameGilbertDownland_update( void )
{
    int fireNow = isFirePressed();
    gitdFireEdge = fireNow && !gitdPrevFire;
    gitdPrevFire = fireNow;

    if( gitdState == GITD_STATE_SPLASH ) gitdUpdateSplash();
    else if( gitdState == GITD_STATE_TITLE ) gitdUpdateTitle();
    else if( gitdState == GITD_STATE_ROOM_TRANSITION ) gitdUpdateRoomTransition();
    else gitdUpdatePlaying();

    gitdRenderFrame();
}




// Nohzdyve (Daniel C, 2024, GPLv3 for this ESP port - "Based on Nohzdyve on
// ARDUBOY originally on (MIT) License"). A vertical dive/descent game: the
// player steers left/right while diving down a scrolling shaft, dodging
// wall-mounted hazards (climbing pegs / carnivorous flowers) and a chasing
// "jaw" enemy, while touching a bouncing "glob" target scores points. 3
// lives per game.
//
// Ported from `more games/MEGAcompilation_ESP/DATA/NOHZDYVE/` - Daniel C's
// own combined cartridge for the *third* TinyJoypad hardware target
// (ESP8285/ESP8266, "MEGA TinyJoypad") - not the raw Arduboy original. See
// this project's CLAUDE.md ("beyond the original scope") for how this was
// found: the ESP compilation already reimplements Nohzdyve on top of a
// shared `ESPCOMPATIBILITY`/`ESPKIT.h` shim built around the exact same
// `[width,height]`-headed PROGMEM sprite format and `255-freq` tone formula
// already used throughout this project's own `ELECTROLIB.h`-lineage ports -
// Daniel C had already done the hard adaptation work away from raw Arduboy2
// hardware calls, so this port reuses that ESP version rather than the
// original Arduboy one.
//
// Sprite-table height convention is genuinely different from this project's
// established `blitzSprite` family (Tiny Bert/Doc/Jump Slime etc, whose own
// upstream tables store page-count directly at index 1): Nohzdyve's own
// tables store *raw pixel height* at index 1 (confirmed by checking the
// actual byte counts against several tables, e.g. `StartGameND`'s own
// declared "43,5" - 5 raw pixels, not 5 pages, rounding up to a single
// partial page) - `ndvBlitzSprite()` below is a direct translation of
// ESPKIT.h's own `ESP_blitzSprite()` (including its partial-page rounding),
// not a reuse of the existing `bertBlitzSprite`-style helper, since the two
// use incompatible header units.
//
// Sprite pool: exactly one array slot per sprite *kind* (`NDV_MAIN`=0
// through `NDV_LINE`=8), not a general object pool - matches upstream's own
// `SPND[MAXSP]` being indexed directly by the same enum used to select which
// kind to activate. `Sprite_ND`'s own `PIC`/`ANIM` raw pointer fields were
// replaced with small integer IDs resolved through `ndvResolvePic()`/
// `ndvSelectFrame()` (matching this project's own established pattern, e.g.
// Tiny Dungeon's `tdngResolveBitmapArray`) - `ANIM` is only ever actually
// read for slots 0-3 (MAIN/GLOB/JAW/CLIM), each cycling through a shared
// frame index (`ndvCnt`, 0-3) driven by one common timer; every other slot's
// own `ANIM = &AnimFrame` self-pointer is dead upstream (never dereferenced
// through that path), so it isn't reproduced at all.
//
// Compositing needs three different modes depending on element (a first for
// this project - previous ports have almost always used pure OR): sprites
// and side walls use plain OR (`drawSelfMasked`); the whole HUD (score/
// lives/labels) uses XOR (`drawInvertPixel`, upstream's own "always visible
// regardless of background" trick - safe to apply in any order since XOR
// accumulation is commutative); the attract screen's own fade-out uses
// AND-NOT against a growing dither mask (`drawErase`); the one-off bonus
// "video code" screen uses a background-clear-then-stamp (`drawOverwrite`,
// needed because it draws over an all-white/0xFF background - a plain OR
// there would be a no-op).
//
// The splash/attract intro is deliberately simplified from upstream's own
// elaborate multi-screen sequence (a 10-step animated crossfade between
// each of 5 full-screen pictures, a 50-iteration loading-flicker with a
// beep each, a 65-frame "choose your handedness" screen, and an optional
// bonus "video code" screen) down to a plain sequential hold of the same 4
// splash pictures - the same "effort/fidelity tradeoff for a purely
// decorative, non-gameplay sequence" precedent already used elsewhere in
// this project (e.g. Space Attack's own simplified attract slide-in). The
// "choose your handedness" screen was dropped entirely rather than
// simplified, for a stronger reason than just effort: reading `SelectND()`
// directly confirms the chosen value is a local variable only ever used to
// decide whether to show the bonus video-code screen afterward - it's
// never read anywhere else, so the handedness "choice" has zero actual
// gameplay effect even on real hardware. Real gameplay (`PlayGameND`) and
// the per-life window-opening reveal (`ExitWindowND`) are both ported at
// full fidelity.
//
// No EEPROM usage exists anywhere in this game upstream (confirmed by
// direct grep) - `ndvHiScores` is genuinely session-only even on real
// hardware, carried forward only across playthroughs within one power-on
// session (reset to 0 at the very top of `loop_NOHZDYVE`, i.e. on cartridge
// launch here) - no persistence to wire up.
//
// rand()%n calls routed through the shared arand() helper; the real-
// hardware entropy reseed (`srand(analogRead(A0))`) has no equivalent and
// is dropped, matching this project's own established precedent for this
// exact pattern (e.g. Tiny Minez, Wren Rollercoaster).

// -----------------------------------------------------------------------------
// Sprite slot IDs (upstream enum, unchanged values - SPND[] is indexed
// directly by these, one array slot per sprite kind)
// -----------------------------------------------------------------------------

enum NdvSpriteSlot
{
    NDV_MAIN = 0,
    NDV_GLOB = 1,
    NDV_JAW = 2,
    NDV_CLIM = 3,
    NDV_WINDOW = 4,
    NDV_PLANT = 5,
    NDV_SLIDE = 6,
    NDV_EXPLOD = 7,
    NDV_LINE = 8
};

#define NDV_MAX_SPRITES 9

// Pic-table IDs, resolved via ndvResolvePic()
enum NdvPicId
{
    NDV_PIC_MAIN = 0,
    NDV_PIC_GLOB = 1,
    NDV_PIC_JAW = 2,
    NDV_PIC_CLIM_L = 3,
    NDV_PIC_CLIM_R = 4,
    NDV_PIC_WINDOW = 5,
    NDV_PIC_PLANT_L = 6,
    NDV_PIC_PLANT_R = 7,
    NDV_PIC_SLIDE = 8,
    NDV_PIC_EXPLOD = 9,
    NDV_PIC_LINE = 10
};

// Top-level game states
enum NdvState
{
    NDV_STATE_SPLASH = 0,
    NDV_STATE_ATTRACT,
    NDV_STATE_ATTRACT_FADE,
    NDV_STATE_WINDOW_OPEN,
    NDV_STATE_PLAYING,
    NDV_STATE_DEAD_WAIT
};

struct NdvSprite
{
    int actif;
    int type;
    int direction;
    int x;
    int y;
    int animFrame;
    int pic;
};

NdvSprite[9] ndvSprites;

struct NdvTimer
{
    int activ;
    int startTime;
    int interval;
};

NdvTimer ndvTr1;   // shared blink/anim-frame cycle (Cnt)
NdvTimer ndvTr2;   // explosion anim advance
NdvTimer ndvTr3;   // main sin-wave wobble / attract fade step
NdvTimer ndvTr4;   // JAW horizontal move
NdvTimer ndvTr5;   // JAW vertical bob
NdvTimer ndvTr6;   // GLOB sin-wave wobble
NdvTimer ndvTr7;   // in-game song step
NdvTimer ndvTr8;   // start-of-life tone sequence
NdvTimer ndvTr9;   // death tone sequence
NdvTimer ndvTr10;  // glob-eaten tone sequence

int ndvState;
int ndvSubFrame;     // generic per-state frame counter
int ndvSplashStep;

int ndvFrmND;         // 0-59 frame counter (matches upstream FrmND)
int ndvRLND;          // -2/-1/0/1/2 steering value
int ndvYScroll;
int ndvSimAnim1;       // main sin-wave phase
int ndvSimAnim2;       // glob sin-wave phase
int ndvStepSong;
int ndvStepStart;
int ndvStepDead;
int ndvStepGlob;
int ndvOneClic;
int ndvCnt;            // shared 0-3 animation-frame cycle

int ndvScores;
int ndvHiScores;
int ndvLives;

int ndvAttractSG;     // "SG" in StartPageND - fading out to gameplay
int ndvAttractFadeStep; // "*FD_" in FadeMainScreenND

// -----------------------------------------------------------------------------
// Sprite-table data (byte-diff-extracted from
// more games/MEGAcompilation_ESP/DATA/NOHZDYVE/{SpriteBank.h,engineOBJ.h})
// -----------------------------------------------------------------------------

// AnimJawND: 4 values (uint8_t)
int[4] ndvAnimJawND = {
    0, 1, 0, 1, 
};

// AnimenemyND: 4 values (uint8_t)
int[4] ndvAnimenemyND = {
    0, 0, 1, 1, 
};

// AnimClimND: 4 values (uint8_t)
int[4] ndvAnimClimND = {
    0, 1, 2, 3, 
};

// AnimGlobND: 4 values (uint8_t)
int[4] ndvAnimGlobND = {
    0, 1, 2, 1, 
};

// StepListND: 32 values (uint8_t)
int[32] ndvStepListND = {
    0, 0, 1, 1, 2, 2, 3, 3, 5, 5, 4, 4, 3, 3, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 0, 0, 1, 1, 2, 2, 3, 3, 
};

// SinWavND: 38 values (int8_t)
int[38] ndvSinWavND = {
    0, 0, 1, 2, 2, 3, 3, 4, 4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 0, 0, -1, -2, -2, -3, -3, -4, -4, -4, 
    -4, -4, -4, -3, -3, -2, -2, -1, -1, 
};

// SinWavBND: 38 values (int8_t)
int[38] ndvSinWavBND = {
    0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 8, 7, 7, 6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6, -7, -7, -8, 
    -8, -7, -7, -6, -5, -4, -3, -2, -1, 
};

// StartGameND: 45 values (uint8_t)
int[45] ndvStartGameND = {
    43, 5, 23, 21, 29, 0, 1, 31, 1, 0, 30, 5, 5, 30, 0, 31, 13, 23, 0, 1, 31, 1, 0, 0, 0, 31, 17, 
    21, 29, 0, 30, 5, 5, 30, 0, 31, 2, 4, 2, 31, 0, 31, 21, 21, 17, 
};

// fadeND: 82 values (uint8_t)
int[82] ndvfadeND = {
    8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 128, 64, 32, 16, 8, 4, 2, 1, 192, 96, 48, 24, 12, 6, 3, 129, 224, 
    112, 56, 28, 14, 7, 131, 193, 248, 124, 62, 31, 15, 135, 195, 225, 248, 124, 62, 31, 143, 199, 
    227, 241, 252, 126, 63, 159, 207, 231, 243, 249, 254, 127, 191, 223, 239, 247, 251, 253, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 
};

// SlideMainPND: 78 values (uint8_t)
int[78] ndvSlideMainPND = {
    19, 16, 0, 0, 0, 0, 0, 0, 128, 64, 66, 76, 130, 223, 155, 86, 66, 66, 112, 0, 0, 0, 0, 0, 0, 0, 
    192, 67, 64, 64, 123, 27, 11, 203, 248, 152, 0, 0, 0, 0, 64, 64, 0, 64, 0, 0, 0, 64, 64, 64, 64, 
    64, 64, 64, 64, 64, 64, 64, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 
};

// MainPND: 42 values (uint8_t)
int[42] ndvMainPND = {
    10, 16, 224, 0, 0, 190, 210, 221, 130, 66, 33, 16, 0, 3, 4, 3, 55, 61, 89, 60, 120, 0, 0, 224, 
    0, 190, 211, 220, 130, 67, 48, 0, 0, 3, 4, 3, 55, 61, 89, 60, 56, 64, 
};

// ExplodND: 146 values (uint8_t)
int[146] ndvExplodND = {
    18, 16, 0, 0, 96, 224, 240, 240, 240, 176, 176, 176, 160, 64, 224, 224, 64, 0, 0, 0, 0, 0, 0, 1, 
    3, 7, 3, 7, 5, 7, 5, 3, 7, 2, 0, 0, 0, 0, 0, 192, 32, 240, 224, 224, 184, 184, 24, 24, 56, 48, 
    96, 224, 240, 224, 0, 0, 0, 3, 7, 6, 7, 4, 0, 0, 3, 0, 6, 6, 15, 15, 15, 7, 0, 0, 224, 240, 152, 
    24, 48, 60, 12, 12, 12, 28, 120, 48, 24, 24, 248, 232, 144, 32, 1, 3, 7, 3, 10, 22, 20, 28, 12, 
    12, 4, 12, 14, 15, 1, 2, 0, 0, 132, 0, 2, 0, 0, 0, 1, 1, 1, 2, 0, 0, 0, 128, 32, 2, 34, 12, 1, 
    0, 0, 0, 4, 4, 8, 0, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 
};

// WindowND: 98 values (uint8_t)
int[98] ndvWindowND = {
    16, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 255, 3, 99, 211, 43, 19, 3, 255, 255, 0, 0, 0, 0, 0, 0, 0, 255, 192, 193, 192, 192, 
    192, 192, 255, 255, 0, 0, 0, 0, 0, 0, 0, 255, 255, 3, 3, 3, 3, 131, 67, 35, 19, 139, 67, 35, 3, 
    255, 255, 255, 255, 192, 192, 192, 193, 192, 192, 192, 193, 192, 192, 192, 192, 255, 255, 
};

// enemyND: 70 values (uint8_t)
int[70] ndvenemyND = {
    17, 16, 0, 0, 0, 0, 254, 157, 106, 106, 0, 106, 106, 157, 254, 0, 0, 0, 0, 0, 0, 0, 0, 63, 51, 
    45, 45, 0, 45, 45, 51, 63, 0, 0, 0, 0, 28, 252, 206, 182, 116, 160, 0, 0, 0, 0, 0, 160, 116, 
    182, 206, 252, 28, 0, 0, 3, 29, 63, 27, 27, 12, 0, 12, 27, 27, 63, 29, 3, 0, 0, 
};

// FlowerRND: 16 values (uint8_t)
int[16] ndvFlowerRND = {
    7, 16, 128, 136, 31, 221, 29, 24, 0, 124, 253, 253, 255, 254, 255, 12, 
};

// FlowerLND: 16 values (uint8_t)
int[16] ndvFlowerLND = {
    7, 16, 0, 24, 29, 221, 31, 136, 128, 12, 255, 254, 255, 253, 253, 124, 
};

// DroiteND: 82 values (uint8_t)
int[82] ndvDroiteND = {
    10, 64, 15, 15, 47, 111, 111, 111, 111, 111, 111, 111, 0, 240, 0, 119, 118, 7, 118, 119, 118, 
    119, 0, 95, 0, 119, 119, 7, 119, 119, 112, 119, 0, 255, 0, 119, 119, 7, 119, 119, 112, 119, 0, 
    247, 0, 119, 119, 7, 119, 119, 112, 119, 0, 255, 0, 119, 119, 7, 119, 119, 112, 119, 192, 223, 
    0, 119, 119, 7, 119, 119, 112, 119, 0, 2, 0, 119, 119, 7, 119, 119, 112, 119, 
};

// GaucheND: 82 values (uint8_t)
int[82] ndvGaucheND = {
    10, 64, 111, 111, 111, 111, 111, 111, 111, 47, 15, 15, 119, 118, 119, 118, 7, 118, 119, 0, 240, 
    0, 119, 112, 119, 119, 7, 119, 119, 0, 95, 0, 119, 112, 119, 119, 7, 119, 119, 0, 255, 0, 119, 
    112, 119, 119, 7, 119, 119, 0, 247, 0, 119, 112, 119, 119, 7, 119, 119, 0, 255, 0, 119, 112, 
    119, 119, 7, 119, 119, 0, 223, 192, 119, 112, 119, 119, 7, 119, 119, 0, 2, 0, 
};

// ClimRND: 106 values (uint8_t)
int[106] ndvClimRND = {
    13, 16, 0, 3, 2, 85, 85, 255, 127, 53, 85, 21, 191, 127, 255, 0, 6, 2, 13, 13, 15, 14, 13, 8, 
    10, 12, 14, 15, 4, 4, 2, 85, 85, 255, 127, 181, 21, 85, 63, 127, 255, 1, 2, 2, 13, 13, 15, 14, 
    12, 10, 8, 13, 14, 15, 1, 1, 2, 85, 85, 255, 127, 53, 85, 21, 191, 127, 255, 2, 4, 2, 13, 13, 
    15, 14, 13, 8, 10, 12, 14, 15, 2, 4, 2, 85, 85, 255, 127, 181, 21, 85, 63, 127, 255, 1, 2, 2, 
    13, 13, 15, 14, 12, 10, 8, 13, 14, 15, 
};

// ClimLND: 106 values (uint8_t)
int[106] ndvClimLND = {
    13, 16, 255, 127, 191, 21, 85, 53, 127, 255, 85, 85, 2, 3, 0, 15, 14, 12, 10, 8, 13, 14, 15, 13, 
    13, 2, 6, 0, 255, 127, 63, 85, 21, 181, 127, 255, 85, 85, 2, 4, 4, 15, 14, 13, 8, 10, 12, 14, 
    15, 13, 13, 2, 2, 1, 255, 127, 191, 21, 85, 53, 127, 255, 85, 85, 2, 1, 1, 15, 14, 12, 10, 8, 
    13, 14, 15, 13, 13, 2, 4, 2, 255, 127, 63, 85, 21, 181, 127, 255, 85, 85, 2, 4, 2, 15, 14, 13, 
    8, 10, 12, 14, 15, 13, 13, 2, 2, 1, 
};

// GlobND: 50 values (uint8_t)
int[50] ndvGlobND = {
    8, 16, 60, 94, 187, 217, 185, 251, 110, 60, 0, 51, 200, 6, 26, 4, 3, 0, 60, 94, 187, 217, 185, 
    251, 110, 60, 0, 3, 12, 190, 74, 4, 3, 0, 60, 94, 187, 217, 185, 251, 110, 60, 0, 3, 4, 26, 6, 
    200, 51, 0, 
};

// nohzdyve_IntroND: 370 values (uint8_t)
int[370] ndvnohzdyve_IntroND = {
    92, 32, 0, 0, 0, 136, 128, 132, 136, 144, 26, 42, 0, 120, 252, 254, 207, 135, 135, 151, 207, 
    190, 236, 120, 0, 106, 106, 106, 0, 0, 0, 0, 128, 255, 0, 0, 8, 24, 56, 127, 63, 31, 155, 203, 
    227, 243, 243, 159, 159, 143, 135, 128, 212, 212, 212, 0, 246, 246, 0, 246, 246, 246, 246, 128, 
    128, 128, 128, 246, 246, 246, 0, 0, 0, 0, 60, 66, 126, 110, 110, 110, 110, 110, 238, 238, 238, 
    14, 14, 63, 30, 12, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 7, 15, 31, 62, 124, 248, 240, 225, 195, 
    130, 3, 2, 3, 105, 108, 0, 0, 15, 223, 31, 28, 28, 28, 252, 253, 253, 248, 0, 248, 24, 8, 8, 8, 
    8, 11, 139, 11, 75, 139, 11, 11, 75, 43, 139, 11, 251, 251, 0, 255, 255, 0, 227, 39, 231, 231, 
    127, 127, 127, 63, 7, 7, 3, 224, 0, 224, 232, 12, 14, 255, 255, 14, 12, 8, 0, 255, 255, 255, 14, 
    14, 14, 14, 159, 206, 196, 128, 0, 64, 192, 192, 255, 255, 255, 192, 192, 64, 0, 0, 0, 1, 131, 
    135, 15, 31, 62, 62, 63, 63, 0, 0, 12, 15, 0, 0, 0, 8, 31, 63, 63, 31, 8, 3, 7, 12, 12, 12, 12, 
    14, 12, 44, 108, 108, 79, 76, 76, 76, 76, 70, 103, 97, 48, 15, 7, 0, 15, 8, 15, 14, 140, 204, 
    204, 220, 252, 124, 0, 255, 192, 255, 127, 60, 30, 15, 7, 0, 0, 0, 0, 31, 31, 31, 28, 28, 28, 
    29, 29, 31, 31, 1, 1, 0, 0, 1, 3, 7, 3, 1, 0, 0, 0, 0, 14, 17, 46, 42, 17, 14, 0, 0, 2, 30, 2, 
    0, 30, 16, 30, 0, 30, 18, 0, 30, 12, 18, 0, 30, 22, 18, 0, 30, 10, 22, 0, 16, 22, 22, 10, 0, 30, 
    18, 30, 0, 30, 5, 0, 2, 30, 2, 0, 8, 24, 56, 127, 255, 127, 56, 24, 8, 0, 3, 1, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
};

// LineND: 1250 values (uint8_t)
int[1250] ndvLineND = {
    104, 24, 2, 2, 250, 242, 242, 242, 226, 226, 242, 244, 244, 244, 228, 228, 228, 228, 228, 244, 
    4, 8, 8, 8, 168, 168, 8, 8, 8, 8, 8, 144, 208, 208, 144, 16, 144, 208, 208, 16, 16, 16, 16, 16, 
    16, 16, 16, 16, 16, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 16, 16, 16, 16, 16, 
    16, 16, 16, 16, 16, 16, 16, 16, 16, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
    114, 250, 226, 194, 226, 250, 250, 58, 2, 2, 0, 0, 255, 255, 255, 255, 143, 63, 255, 255, 255, 
    255, 255, 63, 255, 255, 255, 255, 0, 0, 0, 0, 3, 7, 6, 2, 0, 0, 0, 0, 1, 255, 255, 255, 255, 
    255, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 63, 63, 
    63, 31, 31, 31, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 58, 250, 250, 242, 196, 244, 244, 
    116, 4, 4, 4, 116, 244, 228, 72, 72, 232, 232, 104, 8, 8, 8, 8, 8, 16, 16, 16, 16, 16, 16, 16, 
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 
    32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4, 4, 
    4, 116, 244, 244, 228, 228, 244, 242, 58, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 15, 15, 15, 
    63, 63, 63, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 63, 63, 63, 63, 31, 31, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 52, 116, 116, 164, 164, 116, 116, 
    52, 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 
    16, 16, 16, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 16, 16, 16, 16, 16, 16, 16, 
    16, 16, 16, 16, 16, 16, 16, 8, 168, 168, 8, 8, 8, 8, 8, 8, 8, 4, 116, 244, 244, 196, 196, 244, 
    244, 116, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 6, 7, 3, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 63, 63, 63, 63, 31, 31, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 
    2, 2, 2, 58, 250, 242, 226, 242, 244, 116, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 32, 32, 32, 32, 32, 32, 
    32, 32, 32, 32, 32, 32, 32, 32, 16, 16, 16, 16, 16, 208, 208, 144, 16, 16, 208, 208, 208, 208, 
    8, 8, 8, 8, 168, 168, 8, 8, 8, 8, 4, 244, 244, 228, 228, 228, 228, 244, 244, 242, 250, 226, 226, 
    250, 250, 250, 2, 2, 2, 0, 0, 0, 0, 0, 15, 15, 15, 31, 31, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 255, 255, 255, 255, 255, 255, 1, 0, 0, 0, 1, 3, 3, 1, 0, 
    0, 0, 0, 0, 255, 255, 255, 127, 159, 255, 255, 255, 255, 255, 159, 79, 127, 127, 127, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 
};

// SCOREND: 42 values (uint8_t)
int[42] ndvSCOREND = {
    20, 8, 11, 15, 13, 0, 15, 9, 9, 0, 15, 9, 15, 0, 15, 5, 11, 0, 15, 11, 9, 0, 15, 2, 15, 0, 9, 
    15, 9, 0, 11, 15, 13, 0, 15, 9, 9, 0, 15, 5, 11, 0, 
};

// NUMERICND: 32 values (uint8_t)
int[32] ndvNUMERICND = {
    3, 8, 15, 9, 15, 0, 0, 15, 13, 13, 11, 9, 13, 15, 3, 2, 15, 11, 11, 13, 15, 11, 13, 1, 1, 15, 
    15, 13, 15, 7, 5, 15, 
};

// HARTND: 5 values (uint8_t)
int[5] ndvHARTND = {
    3, 8, 3, 6, 3, 
};

// GameOverND: 45 values (uint8_t)
int[45] ndvGameOverND = {
    43, 8, 31, 17, 21, 29, 0, 30, 5, 5, 30, 0, 31, 2, 4, 2, 31, 0, 31, 21, 21, 17, 0, 0, 0, 31, 17, 
    17, 31, 0, 15, 16, 8, 7, 0, 31, 21, 21, 17, 0, 31, 5, 13, 23, 0, 
};

// Pic2ND: 1026 values (uint8_t)
int[1026] ndvPic2ND = {
    128, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240, 224, 128, 128, 240, 240, 240, 240, 240, 240, 240, 0, 
    0, 240, 112, 48, 48, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 48, 48, 32, 32, 
    224, 240, 240, 48, 48, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    224, 248, 255, 255, 7, 7, 7, 7, 7, 0, 0, 224, 224, 224, 224, 224, 240, 248, 248, 248, 248, 248, 
    192, 192, 224, 224, 224, 240, 248, 248, 248, 254, 254, 254, 254, 224, 224, 199, 199, 6, 8, 8, 
    248, 248, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 15, 15, 0, 0, 0, 192, 
    231, 255, 255, 255, 255, 255, 255, 255, 255, 231, 207, 223, 255, 255, 223, 223, 223, 223, 255, 
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 223, 223, 216, 0, 0, 15, 15, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 255, 192, 128, 0, 0, 128, 128, 0, 0, 0, 36, 36, 
    16, 16, 4, 4, 0, 0, 0, 128, 128, 240, 248, 248, 240, 128, 128, 16, 0, 36, 8, 24, 4, 4, 3, 3, 0, 
    0, 0, 0, 128, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 7, 7, 
    0, 0, 7, 7, 31, 30, 252, 252, 252, 252, 252, 252, 252, 252, 252, 255, 127, 63, 63, 127, 127, 63, 
    3, 3, 255, 255, 252, 252, 252, 252, 252, 252, 252, 28, 28, 31, 31, 7, 0, 0, 1, 1, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 63, 48, 0, 31, 255, 255, 
    255, 127, 127, 127, 127, 127, 126, 124, 124, 124, 127, 127, 127, 127, 63, 127, 255, 255, 255, 
    240, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 128, 128, 128, 128, 128, 128, 192, 224, 240, 240, 240, 240, 240, 240, 
    240, 240, 240, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 240, 252, 252, 252, 252, 
    0, 0, 0, 0, 31, 30, 60, 127, 255, 223, 223, 231, 231, 231, 231, 231, 231, 231, 255, 223, 255, 
    255, 124, 124, 127, 127, 31, 15, 0, 28, 252, 252, 252, 240, 224, 240, 240, 240, 240, 240, 240, 
    240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 192, 128, 128, 128, 128, 
    128, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 12, 
    28, 28, 28, 30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 63, 63, 63, 63, 63, 63, 63, 
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 3, 59, 
    59, 59, 59, 59, 59, 59, 3, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 
    63, 63, 63, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 15, 15, 14, 
    14, 14, 14, 14, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
};

// Loading1ND: 1026 values (uint8_t)
int[1026] ndvLoading1ND = {
    128, 64, 60, 60, 60, 60, 60, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
    60, 60, 60, 60, 60, 142, 142, 142, 142, 142, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 142, 142, 142, 142, 142, 199, 199, 199, 199, 199, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240, 80, 
    112, 0, 240, 0, 0, 0, 240, 80, 16, 0, 240, 80, 240, 0, 112, 80, 208, 0, 240, 80, 16, 0, 0, 240, 
    128, 240, 0, 240, 80, 240, 0, 16, 240, 16, 0, 16, 240, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 199, 199, 199, 
    199, 199, 113, 113, 113, 113, 113, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240, 32, 64, 240, 0, 0, 0, 224, 17, 16, 224, 0, 1, 1, 241, 64, 
    65, 241, 1, 0, 1, 144, 81, 48, 17, 1, 1, 0, 241, 17, 17, 224, 0, 1, 0, 17, 32, 193, 32, 17, 0, 
    1, 1, 113, 128, 0, 129, 112, 0, 0, 0, 240, 80, 16, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 113, 113, 113, 113, 113, 28, 28, 28, 28, 
    28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    1, 0, 0, 1, 0, 0, 0, 0, 17, 241, 16, 0, 112, 80, 209, 0, 0, 1, 0, 240, 0, 1, 1, 241, 17, 240, 0, 
    240, 81, 241, 1, 240, 16, 224, 0, 16, 240, 17, 0, 240, 64, 128, 240, 0, 240, 17, 80, 144, 0, 0, 
    0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 28, 28, 28, 28, 28, 199, 199, 199, 199, 199, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 
    1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1, 
    1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 199, 199, 199, 199, 199, 227, 227, 227, 227, 227, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 227, 227, 227, 227, 227, 56, 56, 56, 56, 56, 
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 56, 56, 
    56, 56, 56, 
};

// TinyJoypadND: 1026 values (uint8_t)
int[1026] ndvTinyJoypadND = {
    128, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 224, 64, 128, 64, 224, 0, 224, 160, 32, 0, 
    224, 32, 160, 160, 0, 192, 160, 224, 0, 0, 32, 224, 32, 0, 32, 224, 32, 0, 224, 64, 128, 224, 0, 
    96, 128, 224, 0, 0, 0, 224, 0, 224, 32, 224, 0, 96, 128, 224, 0, 224, 160, 224, 0, 192, 160, 
    224, 0, 224, 32, 192, 0, 0, 0, 224, 0, 224, 0, 224, 160, 32, 0, 224, 160, 224, 0, 224, 160, 160, 
    0, 224, 0, 224, 32, 224, 0, 224, 64, 128, 224, 0, 0, 0, 224, 160, 64, 0, 96, 128, 224, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 3, 0, 
    3, 2, 2, 0, 3, 2, 242, 19, 224, 3, 240, 83, 240, 0, 240, 35, 64, 240, 2, 19, 242, 16, 3, 240, 
    80, 19, 0, 240, 3, 0, 0, 1, 2, 225, 16, 19, 2, 3, 0, 0, 3, 0, 0, 3, 0, 0, 0, 3, 0, 3, 0, 3, 2, 
    1, 0, 0, 0, 1, 2, 1, 0, 3, 2, 2, 0, 3, 1, 2, 0, 2, 2, 3, 0, 3, 0, 3, 2, 3, 0, 3, 0, 0, 3, 0, 0, 
    0, 3, 2, 3, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 249, 
    169, 137, 0, 249, 129, 129, 0, 249, 169, 137, 0, 249, 136, 136, 1, 9, 248, 8, 0, 248, 104, 184, 
    0, 248, 136, 248, 0, 0, 0, 248, 128, 128, 0, 128, 0, 136, 248, 136, 0, 128, 0, 248, 168, 208, 0, 
    0, 0, 0, 0, 232, 168, 184, 0, 248, 136, 248, 0, 232, 168, 184, 0, 56, 32, 248, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    192, 0, 0, 0, 192, 0, 0, 192, 0, 0, 0, 192, 0, 0, 192, 0, 0, 0, 192, 0, 0, 0, 0, 64, 64, 192, 
    64, 64, 0, 0, 64, 192, 64, 0, 0, 192, 128, 0, 0, 192, 0, 0, 64, 128, 0, 128, 64, 0, 0, 0, 0, 
    192, 0, 0, 128, 64, 64, 64, 128, 0, 0, 64, 128, 0, 128, 64, 0, 0, 192, 64, 64, 128, 0, 128, 64, 
    64, 128, 0, 0, 192, 64, 64, 128, 0, 0, 0, 128, 64, 64, 64, 0, 128, 64, 64, 64, 128, 0, 192, 128, 
    0, 128, 192, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 2, 
    1, 2, 7, 0, 0, 7, 2, 1, 2, 7, 0, 0, 7, 2, 1, 2, 7, 0, 0, 4, 0, 0, 0, 7, 0, 0, 0, 0, 4, 7, 4, 0, 
    0, 7, 0, 1, 2, 7, 0, 0, 0, 0, 7, 0, 0, 0, 2, 4, 4, 3, 0, 0, 3, 4, 4, 4, 3, 0, 0, 0, 0, 7, 0, 0, 
    0, 0, 7, 2, 2, 1, 0, 7, 1, 1, 7, 0, 0, 7, 4, 4, 3, 0, 4, 0, 3, 4, 4, 4, 0, 3, 4, 4, 4, 3, 0, 7, 
    0, 1, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
};

// PaxND: 1026 values (uint8_t)
int[1026] ndvPaxND = {
    128, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 128, 128, 128, 128, 128, 0, 0, 0, 0, 96, 96, 128, 0, 0, 0, 0, 
    0, 0, 224, 0, 0, 32, 192, 0, 0, 0, 224, 0, 0, 0, 0, 0, 64, 0, 192, 192, 0, 0, 128, 128, 128, 64, 
    64, 64, 64, 192, 192, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 56, 248, 248, 248, 120, 248, 248, 240, 
    225, 225, 225, 2, 4, 12, 16, 16, 16, 1, 131, 131, 128, 240, 240, 240, 240, 248, 248, 248, 248, 
    240, 0, 224, 224, 240, 248, 248, 252, 252, 252, 224, 195, 131, 0, 6, 97, 97, 97, 16, 12, 12, 12, 
    8, 192, 193, 193, 193, 192, 192, 192, 192, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 128, 0, 15, 31, 31, 56, 56, 120, 224, 227, 
    227, 7, 0, 0, 0, 0, 0, 0, 128, 127, 127, 127, 127, 127, 127, 127, 255, 207, 207, 255, 255, 14, 
    15, 255, 255, 255, 255, 255, 127, 127, 255, 223, 223, 88, 0, 0, 0, 0, 0, 0, 0, 30, 143, 131, 
    195, 227, 113, 127, 31, 1, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240, 240, 15, 15, 0, 126, 44, 0, 0, 254, 252, 0, 0, 0, 0, 0, 0, 
    128, 192, 192, 192, 224, 224, 224, 192, 220, 223, 219, 196, 192, 129, 1, 237, 237, 238, 238, 
    237, 193, 1, 131, 140, 158, 146, 156, 128, 128, 192, 192, 192, 192, 0, 0, 0, 0, 0, 129, 65, 1, 
    0, 192, 8, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 63, 1, 0, 0, 224, 224, 16, 0, 0, 224, 31, 3, 0, 224, 0, 0, 0, 1, 1, 1, 1, 1, 
    1, 1, 195, 195, 3, 241, 243, 127, 255, 255, 247, 199, 199, 135, 135, 199, 231, 247, 251, 63, 
    255, 227, 6, 196, 135, 7, 3, 3, 3, 3, 0, 0, 0, 0, 135, 0, 0, 0, 63, 192, 0, 224, 240, 1, 62, 
    192, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    248, 0, 0, 255, 7, 0, 0, 0, 0, 0, 248, 248, 7, 0, 0, 0, 254, 0, 0, 0, 0, 0, 0, 7, 7, 0, 0, 0, 0, 
    36, 1, 1, 25, 45, 45, 45, 25, 1, 37, 45, 0, 0, 1, 128, 31, 3, 0, 0, 0, 0, 0, 96, 128, 0, 0, 15, 
    240, 0, 0, 0, 255, 0, 135, 143, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 224, 255, 0, 0, 3, 0, 192, 240, 14, 0, 0, 15, 0, 0, 0, 112, 48, 
    15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 255, 255, 63, 3, 23, 19, 17, 1, 255, 255, 255, 0, 12, 
    15, 7, 0, 0, 0, 0, 0, 0, 0, 0, 241, 0, 0, 0, 15, 0, 0, 0, 15, 240, 1, 1, 0, 0, 0, 254, 14, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 3, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
};

int[6] ndvNoteND = { 1, 21, 41, 61, 91, 121 };
int[13] ndvStartTone = { 100, 100, 100, 100, 100, 100, 1, 1, 1, 1, 1, 1, 0 };

// -----------------------------------------------------------------------------
// Sprite blit primitives - a direct translation of ESPKIT.h's own
// ESP_blitzSprite()/ESP_RecupeLineY()/ESP_RecupeDecalageY()/
// ESP_SplitSpriteDecalageY(), NOT a reuse of this project's existing
// bertBlitzSprite-style helper - see this file's own header comment for why
// the two use incompatible sprite-table height units.
// -----------------------------------------------------------------------------

// yPos can genuinely be negative here (ndvYScroll ranges 0 down to -63,
// sprite Y positions go negative once scrolled above the visible screen -
// see ndvCleanOverScan()'s own `y < -24` check) - a plain `valeur >> 3`
// breaks on Vircon32, whose `>>` is a documented *logical* (zero-fill)
// shift, not arithmetic like AVR-GCC's. Same bug class already found and
// fixed the same way in HollowSeeker/Tiny Pipe/TinY Fi: branch on sign,
// only ever shift a non-negative operand.
int ndvRecupeLineY( int valeur )
{
    if( valeur >= 0 ) return valeur >> 3;
    return -( ( -valeur + 7 ) >> 3 );
}

int ndvRecupeDecalageY( int valeur )
{
    return valeur - ( ndvRecupeLineY( valeur ) << 3 );
}

int ndvSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown )
      return ( input << decalage ) & 0xFF;
    return input >> ( 8 - decalage );
}

// hSpritePages: caller-computed page-height, matching ESPKIT's own
// drawSelfMasked()-family wrapper (raw pixel height >>3, rounded UP by one
// extra page whenever it isn't an exact multiple of 8).
int ndvBlitzSprite( int xPos, int yPos, int hSpritePages, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[ 0 ];
    int wMax = ( hSpritePages * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = ndvRecupeLineY( yPos );

    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos ||
        recupeLineY > yPass || ( recupeLineY + hSpritePages ) < yPass )
      return 0x00;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = ndvRecupeDecalageY( yPos );
    int scanA = ( xPass - xPos ) + ( spriteYLine * wSprite ) + 2;
    int scanB = ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;

    int outByte;
    if( scanA > wMax )
      outByte = 0x00;
    else
      outByte = ndvSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = ndvSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

int ndvPageCount( int* sprites )
{
    int rawH = sprites[ 1 ];
    int pages = rawH >> 3;
    if( ( rawH - ( pages << 3 ) ) != 0 ) pages = pages + 1;
    return pages;
}

int* ndvResolvePic( int picId )
{
    if( picId == NDV_PIC_MAIN ) return ndvMainPND;
    if( picId == NDV_PIC_GLOB ) return ndvGlobND;
    if( picId == NDV_PIC_JAW ) return ndvenemyND;
    if( picId == NDV_PIC_CLIM_L ) return ndvClimLND;
    if( picId == NDV_PIC_CLIM_R ) return ndvClimRND;
    if( picId == NDV_PIC_WINDOW ) return ndvWindowND;
    if( picId == NDV_PIC_PLANT_L ) return ndvFlowerLND;
    if( picId == NDV_PIC_PLANT_R ) return ndvFlowerRND;
    if( picId == NDV_PIC_SLIDE ) return ndvSlideMainPND;
    if( picId == NDV_PIC_EXPLOD ) return ndvExplodND;
    return ndvLineND;
}

// -----------------------------------------------------------------------------
// Sprite helpers (flattened from upstream's Sprite_ND class)
// -----------------------------------------------------------------------------

void ndvSpriteInit( int slot )
{
    ndvSprites[ slot ].actif = 0;
    ndvSprites[ slot ].type = 0;
    ndvSprites[ slot ].x = 0;
    ndvSprites[ slot ].y = 0;
    ndvSprites[ slot ].animFrame = 0;
    ndvSprites[ slot ].pic = NDV_PIC_MAIN;
}

int ndvCheckPic( int type, int x )
{
    if( type == NDV_MAIN ) return NDV_PIC_MAIN;
    if( type == NDV_GLOB ) return NDV_PIC_GLOB;
    if( type == NDV_JAW ) return NDV_PIC_JAW;
    if( type == NDV_CLIM )
    {
        if( x > 64 ) return NDV_PIC_CLIM_R;
        return NDV_PIC_CLIM_L;
    }
    if( type == NDV_WINDOW ) return NDV_PIC_WINDOW;
    if( type == NDV_PLANT )
    {
        if( x > 64 ) return NDV_PIC_PLANT_R;
        return NDV_PIC_PLANT_L;
    }
    if( type == NDV_SLIDE ) return NDV_PIC_SLIDE;
    if( type == NDV_EXPLOD ) return NDV_PIC_EXPLOD;
    return NDV_PIC_LINE;
}

void ndvActivateSprite( int slot, int x, int y, int type )
{
    ndvSprites[ slot ].x = x;
    ndvSprites[ slot ].y = y;
    ndvSprites[ slot ].actif = 1;
    ndvSprites[ slot ].type = type;
    ndvSprites[ slot ].animFrame = 0;
    ndvSprites[ slot ].pic = ndvCheckPic( type, x );
    ndvSprites[ slot ].direction = arand( 2 );
}

void ndvSpriteDestroy( int slot )
{
    ndvSprites[ slot ].animFrame = 0;
    ndvSprites[ slot ].type = NDV_EXPLOD;
    ndvSprites[ slot ].x = ndvSprites[ slot ].x - 3;
    ndvSprites[ slot ].y = ndvSprites[ slot ].y + 1;
    ndvSprites[ slot ].pic = NDV_PIC_EXPLOD;
}

void ndvUpdateMoveDirection( int slot )
{
    if( ndvSprites[ slot ].direction )
      ndvSprites[ slot ].x = ndvSprites[ slot ].x + 1;
    else
      ndvSprites[ slot ].x = ndvSprites[ slot ].x - 1;
}

void ndvMoveJaw( void )
{
    int x = ndvSprites[ NDV_JAW ].x;
    if( x > 10 && x < 102 )
    {
        ndvUpdateMoveDirection( NDV_JAW );
    }
    else
    {
        if( ndvSprites[ NDV_JAW ].direction == 1 ) ndvSprites[ NDV_JAW ].direction = 0;
        else ndvSprites[ NDV_JAW ].direction = 1;
        ndvUpdateMoveDirection( NDV_JAW );
    }
}

// -----------------------------------------------------------------------------
// Timer helpers (flattened from upstream's TIMERND class)
// -----------------------------------------------------------------------------

void ndvTimerInitP( NdvTimer* t, int interval )
{
    t->startTime = 0;
    t->interval = interval;
    t->activ = 0;
}

void ndvTimerActivateP( NdvTimer* t )
{
    t->activ = 1;
}

void ndvTimerDeactivateP( NdvTimer* t )
{
    t->activ = 0;
}

int ndvTimerTriggerP( NdvTimer* t )
{
    if( t->activ == 0 ) return 0;
    if( t->startTime < t->interval )
    {
        t->startTime = t->startTime + 1;
        return 0;
    }
    t->startTime = 0;
    return 1;
}

// -----------------------------------------------------------------------------
// Score panel
// -----------------------------------------------------------------------------

int ndvPanD10000;
int ndvPanD1000;
int ndvPanD100;
int ndvPanD10;
int ndvPanD1;

void ndvCalculatePan( int scores )
{
    ndvPanD10000 = scores / 10000;
    ndvPanD1000 = ( scores - ( ndvPanD10000 * 10000 ) ) / 1000;
    ndvPanD100 = ( scores - ( ndvPanD1000 * 1000 ) - ( ndvPanD10000 * 10000 ) ) / 100;
    ndvPanD10 = ( scores - ( ndvPanD100 * 100 ) - ( ndvPanD1000 * 1000 ) - ( ndvPanD10000 * 10000 ) ) / 10;
    ndvPanD1 = scores - ( ndvPanD10 * 10 ) - ( ndvPanD100 * 100 ) - ( ndvPanD1000 * 1000 ) - ( ndvPanD10000 * 10000 );
}

// -----------------------------------------------------------------------------
// Game setup / reset
// -----------------------------------------------------------------------------

void ndvSpriteSetCFG( void )
{
    int t;
    for( t = 0; t < NDV_MAX_SPRITES; t++ ) ndvSpriteInit( t );
}

void ndvTimerConfig( void )
{
    ndvTimerInitP( &ndvTr1, 3 ); ndvTimerActivateP( &ndvTr1 );
    ndvTimerInitP( &ndvTr2, 4 ); ndvTimerActivateP( &ndvTr2 );
    ndvTimerInitP( &ndvTr3, 2 ); ndvTimerActivateP( &ndvTr3 );
    ndvTimerInitP( &ndvTr4, 1 ); ndvTimerActivateP( &ndvTr4 );
    ndvTimerInitP( &ndvTr5, 2 ); ndvTimerActivateP( &ndvTr5 );
    ndvTimerInitP( &ndvTr6, 2 ); ndvTimerActivateP( &ndvTr6 );
    ndvTimerInitP( &ndvTr7, 7 );
    ndvTimerInitP( &ndvTr8, 1 );
    ndvTimerInitP( &ndvTr9, 1 );
    ndvTimerInitP( &ndvTr10, 1 );
}

void ndvResetVar( void )
{
    int t;
    ndvYScroll = 0;
    ndvFrmND = 0;
    ndvRLND = 0;
    ndvSimAnim1 = 0;
    ndvSimAnim2 = 0;
    for( t = 0; t < NDV_MAX_SPRITES; t++ ) ndvSpriteInit( t );
    ndvActivateSprite( NDV_CLIM, 107, 41, NDV_CLIM );
}

void ndvResetStepSN( void )
{
    ndvStepSong = 0;
    ndvStepStart = 0;
    ndvStepDead = 0;
    ndvStepGlob = 0;
}

// -----------------------------------------------------------------------------
// State-transition entry points (upstream's own goto-chain targets:
// Menu:/Start_game:/NewLive: - grouped together here, ahead of every
// per-state Compose/Update function, since this dialect has no forward
// declarations and each is called from a DIFFERENT state's own Update
// function (ndvChangePic -> ndvBeginPlaying; ndvUpdateAttractFade ->
// ndvBeginWindowOpen; ndvUpdatePlaying -> both) - none of the three ever
// call each other, only later Update functions call into them, so this is
// the one place all three can sit before every caller.
// -----------------------------------------------------------------------------

void ndvBeginAttract( void )
{
    // Upstream itself never persists this (no EEPROM usage anywhere in
    // Nohzdyve's own real source - confirmed by direct grep), so this is
    // a deliberate extension beyond what upstream ever did, not a
    // restoration - same precedent as Tiny Bert's own high-score save.
    if( ndvScores > ndvHiScores )
    {
        ndvHiScores = ndvScores;
        eeprom_write_word( 0, ndvHiScores );
    }
    ndvLives = 3;
    ndvScores = 0;
    ndvPanD10000 = 0; ndvPanD1000 = 0; ndvPanD100 = 0; ndvPanD10 = 0; ndvPanD1 = 0;
    ndvResetVar();
    ndvSpriteSetCFG();
    ndvTimerConfig();
    ndvActivateSprite( NDV_CLIM, 107, 41, NDV_CLIM );
    ndvTimerActivateP( &ndvTr3 );
    ndvOneClic = 2;
    ndvAttractSG = 0;
    ndvAttractFadeStep = 0;
    ndvState = NDV_STATE_ATTRACT;
}

void ndvBeginWindowOpen( void )
{
    ndvResetVar();
    ndvActivateSprite( NDV_WINDOW, 8, 37, NDV_WINDOW );
    Sound( 140, 40 );
    ndvOneClic = 2;
    ndvState = NDV_STATE_WINDOW_OPEN;
}

void ndvBeginPlaying( void )
{
    ndvTimerActivateP( &ndvTr8 );
    ndvResetStepSN();
    ndvOneClic = 2;
    ndvActivateSprite( NDV_MAIN, ndvSprites[ NDV_SLIDE ].x + 5, ndvSprites[ NDV_SLIDE ].y, NDV_MAIN );
    ndvSpriteInit( NDV_SLIDE );
    ndvTimerActivateP( &ndvTr3 );
    ndvState = NDV_STATE_PLAYING;
}

// -----------------------------------------------------------------------------
// Sound (each of these fires at most once per real tick, already gated by
// its own TIMERND trigger - no burst-collapse risk, so every tunes.tone()
// call ports straight onto the shared Sound() with no sequencer needed)
// -----------------------------------------------------------------------------

void ndvPlayInGameSong( void )
{
    if( ndvTimerTriggerP( &ndvTr7 ) )
    {
        Sound( ndvNoteND[ ndvStepListND[ ndvStepSong ] ], 20 );
        if( ndvStepSong < 31 ) ndvStepSong = ndvStepSong + 1;
        else ndvStepSong = 0;
    }
}

void ndvStartTonePlay( void )
{
    if( ndvTimerTriggerP( &ndvTr8 ) )
    {
        Sound( ndvStartTone[ ndvStepStart ], 10 );
        if( ndvStepStart < 12 )
        {
            ndvStepStart = ndvStepStart + 1;
        }
        else
        {
            ndvTimerDeactivateP( &ndvTr8 );
            ndvTimerActivateP( &ndvTr7 );
        }
    }
}

void ndvDeadTonePlay( void )
{
    if( ndvTr9.activ )
    {
        if( ndvStepDead % 2 )
          Sound( 210 - ( ndvStepDead * 5 ), 10 );
        if( ndvStepDead < 40 )
        {
            ndvStepDead = ndvStepDead + 1;
        }
        else
        {
            ndvTimerDeactivateP( &ndvTr9 );
        }
    }
}

void ndvGlobTonePlay( void )
{
    if( ndvTr10.activ )
    {
        if( ndvStepGlob % 2 )
          Sound( 1, 1 );
        else
          Sound( ndvStepGlob * 10, 1 );
        if( ndvStepGlob < 14 )
        {
            ndvStepGlob = ndvStepGlob + 1;
        }
        else
        {
            ndvTimerDeactivateP( &ndvTr10 );
        }
    }
}

void ndvPlaySong( void )
{
    ndvStartTonePlay();
    ndvDeadTonePlay();
    ndvGlobTonePlay();
    ndvPlayInGameSong();
}

// -----------------------------------------------------------------------------
// Gameplay logic
// -----------------------------------------------------------------------------

void ndvAdjustScore( void )
{
    ndvScores = ndvScores + 10;
}

void ndvExplodeMain( void )
{
    if( ndvSprites[ NDV_MAIN ].type != NDV_EXPLOD )
    {
        ndvTimerDeactivateP( &ndvTr7 );
        ndvTimerDeactivateP( &ndvTr3 );
        ndvTimerActivateP( &ndvTr9 );
        ndvSpriteDestroy( NDV_MAIN );
    }
}

void ndvExplodeGlob( void )
{
    if( ndvSprites[ NDV_GLOB ].type != NDV_EXPLOD )
    {
        ndvAdjustScore();
        ndvTimerDeactivateP( &ndvTr6 );
        ndvStepGlob = 0;
        ndvTimerActivateP( &ndvTr10 );
        ndvSpriteDestroy( NDV_GLOB );
    }
}

int ndvColidCheck( int x, int y, int x2, int y2, int marginX, int marginY )
{
    if( x > ( x2 + marginX ) ) return 0;
    if( y > ( y2 + marginY ) ) return 0;
    if( ( x + 7 ) < x2 ) return 0;
    if( ( y + 13 ) < y2 ) return 0;
    return 1;
}

void ndvWallCollisionCheckMain( void )
{
    int xm = ndvSprites[ NDV_MAIN ].x;
    if( xm < 8 ) ndvExplodeMain();
    if( xm > 110 ) ndvExplodeMain();
}

void ndvMain2Solid( void )
{
    if( ndvSprites[ NDV_MAIN ].actif == 0 ) return;
    if( ndvSprites[ NDV_MAIN ].type == NDV_EXPLOD ) return;

    int mx = ndvSprites[ NDV_MAIN ].x;
    int my = ndvSprites[ NDV_MAIN ].y + ndvSinWavND[ ndvSimAnim1 ];

    if( ndvColidCheck( mx, my, ndvSprites[ NDV_JAW ].x, ndvSprites[ NDV_JAW ].y, 12, 14 ) )
    {
        if( ndvSprites[ NDV_JAW ].actif ) ndvExplodeMain();
    }
    if( ndvColidCheck( mx, my, ndvSprites[ NDV_PLANT ].x, ndvSprites[ NDV_PLANT ].y, 7, 14 ) )
    {
        if( ndvSprites[ NDV_PLANT ].actif ) ndvExplodeMain();
    }
    if( ndvColidCheck( mx, my, ndvSprites[ NDV_CLIM ].x, ndvSprites[ NDV_CLIM ].y, 8, 14 ) )
    {
        if( ndvSprites[ NDV_CLIM ].actif ) ndvExplodeMain();
    }
}

void ndvMain2Glob( void )
{
    if( ndvSprites[ NDV_MAIN ].actif == 0 ) return;
    if( ndvSprites[ NDV_MAIN ].type == NDV_EXPLOD ) return;

    int mx = ndvSprites[ NDV_MAIN ].x;
    int my = ndvSprites[ NDV_MAIN ].y + ndvSinWavND[ ndvSimAnim1 ];
    int gx = ndvSprites[ NDV_GLOB ].x + ndvSinWavBND[ ndvSimAnim2 ];
    int gy = ndvSprites[ NDV_GLOB ].y;

    if( ndvColidCheck( mx, my, gx, gy, 8, 14 ) ) ndvExplodeGlob();
}

void ndvCollisionCheck( void )
{
    ndvWallCollisionCheckMain();
    ndvMain2Solid();
    ndvMain2Glob();
}

int ndvYLineSet( void )
{
    return ndvYScroll + 127;
}

void ndvLiberateLine( void )
{
    if( ndvSprites[ NDV_LINE ].actif ) return;
    if( arand( 100 ) > 75 && ndvYScroll == -63 )
    {
        ndvActivateSprite( NDV_LINE, 12, ndvYLineSet(), NDV_LINE );
        ndvSprites[ NDV_LINE ].animFrame = arand( 4 );
    }
}

void ndvLiberateJaw( void )
{
    if( ndvSprites[ NDV_JAW ].actif ) return;
    ndvActivateSprite( NDV_JAW, arand( 92 ) + 10, 106, NDV_JAW );
}

void ndvLiberateGlob( void )
{
    if( ndvSprites[ NDV_GLOB ].actif ) return;
    ndvActivateSprite( NDV_GLOB, arand( 70 ) + 25, 74, NDV_GLOB );
    ndvTimerActivateP( &ndvTr6 );
}

void ndvLiberateWallSprite( void )
{
    if( ndvSprites[ NDV_CLIM ].actif ) return;
    if( ndvSprites[ NDV_PLANT ].actif ) return;
    if( arand( 2 ) )
    {
        int x;
        if( arand( 2 ) ) x = 8; else x = 107;
        ndvActivateSprite( NDV_CLIM, x, 105 + ndvYScroll, NDV_CLIM );
    }
    else
    {
        int x;
        if( arand( 2 ) ) x = 11; else x = 110;
        ndvActivateSprite( NDV_PLANT, x, 108 + ndvYScroll, NDV_PLANT );
    }
}

void ndvLibarateSprite( void )
{
    ndvLiberateWallSprite();
    if( ndvSprites[ NDV_MAIN ].actif )
    {
        ndvLiberateJaw();
        ndvLiberateGlob();
    }
    ndvLiberateLine();
}

void ndvCleanOverScan( void )
{
    int t;
    for( t = 0; t < NDV_MAX_SPRITES; t++ )
    {
        if( ndvSprites[ t ].actif )
        {
            if( ndvSprites[ t ].y < -24 ) ndvSpriteInit( t );
        }
    }
}

void ndvExplodLogistic( void )
{
    if( ndvTimerTriggerP( &ndvTr2 ) )
    {
        int t;
        for( t = 0; t < NDV_MAX_SPRITES; t++ )
        {
            if( ndvSprites[ t ].actif == 1 && ndvSprites[ t ].type == NDV_EXPLOD )
            {
                int add = ndvSprites[ t ].animFrame;
                if( add < 3 )
                {
                    ndvSprites[ t ].animFrame = add + 1;
                }
                else
                {
                    ndvSpriteInit( t );
                }
            }
        }
    }
}

void ndvSinWavRun( void )
{
    if( ndvTimerTriggerP( &ndvTr3 ) )
    {
        if( ndvSimAnim1 < 37 ) ndvSimAnim1 = ndvSimAnim1 + 1; else ndvSimAnim1 = 0;
    }
    if( ndvTimerTriggerP( &ndvTr6 ) )
    {
        if( ndvSimAnim2 < 37 ) ndvSimAnim2 = ndvSimAnim2 + 1; else ndvSimAnim2 = 0;
    }
}

void ndvUpdateMain( void )
{
    ndvSinWavRun();
    if( ndvRLND == 0 ) return;
    if( ndvSprites[ NDV_MAIN ].type == NDV_EXPLOD ) return;
    if( ndvSprites[ NDV_MAIN ].actif == 0 ) return;

    if( ndvRLND == -2 ) ndvSprites[ NDV_MAIN ].x = ndvSprites[ NDV_MAIN ].x - 2;
    else if( ndvRLND == -1 ) ndvSprites[ NDV_MAIN ].x = ndvSprites[ NDV_MAIN ].x - 1;
    else if( ndvRLND == 1 ) ndvSprites[ NDV_MAIN ].x = ndvSprites[ NDV_MAIN ].x + 1;
    else if( ndvRLND == 2 ) ndvSprites[ NDV_MAIN ].x = ndvSprites[ NDV_MAIN ].x + 2;
}

void ndvJoyPadRefresh( void )
{
    if( isRightPressed() )
    {
        ndvRLND = 2;
    }
    else if( isLeftPressed() )
    {
        ndvRLND = -2;
    }
    else
    {
        if( ndvRLND == 2 ) ndvRLND = 1;
        if( ndvRLND == -2 ) ndvRLND = -1;
    }
}

void ndvFrameRate( void )
{
    if( ndvFrmND < 59 ) ndvFrmND = ndvFrmND + 1; else ndvFrmND = 0;
}

void ndvFlipScreenTick( void )
{
    if( ndvTimerTriggerP( &ndvTr1 ) )
    {
        if( ndvCnt < 3 ) ndvCnt = ndvCnt + 1; else ndvCnt = 0;
    }
}

void ndvMovingSprite( void )
{
    int t;
    for( t = 1; t < NDV_MAX_SPRITES; t++ )
    {
        if( ndvSprites[ t ].actif == 1 ) ndvSprites[ t ].y = ndvSprites[ t ].y - 1;
    }
}

void ndvScrollDown( int spd )
{
    if( ( ndvFrmND % spd ) == 0 )
    {
        if( ndvYScroll > -63 ) ndvYScroll = ndvYScroll - 1; else ndvYScroll = 0;
        ndvMovingSprite();
    }
}

void ndvGamePlayAdj( void )
{
    ndvLibarateSprite();
    ndvExplodLogistic();
    ndvCleanOverScan();
    if( ndvTimerTriggerP( &ndvTr4 ) ) ndvMoveJaw();
    if( ndvTimerTriggerP( &ndvTr5 ) ) ndvSprites[ NDV_JAW ].y = ndvSprites[ NDV_JAW ].y - 1;
}

// -----------------------------------------------------------------------------
// Frame selection (upstream SelectFrameND/WavAnimND/WavXAnimND)
// -----------------------------------------------------------------------------

int ndvSelectFrame( int slot )
{
    if( ndvSprites[ slot ].type == NDV_EXPLOD ) return ndvSprites[ slot ].animFrame;
    if( slot == NDV_MAIN ) return ndvAnimenemyND[ ndvCnt ];
    if( slot == NDV_GLOB ) return ndvAnimGlobND[ ndvCnt ];
    if( slot == NDV_JAW ) return ndvAnimJawND[ ndvCnt ];
    if( slot == NDV_CLIM ) return ndvAnimClimND[ ndvCnt ];
    return ndvSprites[ slot ].animFrame;
}

int ndvWavAnim( int slot )
{
    if( slot == NDV_MAIN ) return ndvSinWavND[ ndvSimAnim1 ];
    return 0;
}

int ndvWavXAnim( int slot )
{
    if( slot == NDV_GLOB ) return ndvSinWavBND[ ndvSimAnim2 ];
    return 0;
}

// -----------------------------------------------------------------------------
// Rendering - one shared per-page compose buffer, built once per page (not
// once per pixel) from the start, matching this project's own established
// "gate every draw by its own real footprint" practice.
// -----------------------------------------------------------------------------

int[128] ndvPageBuffer;

void ndvClearPageBuffer( void )
{
    int c;
    for( c = 0; c < 128; c++ ) ndvPageBuffer[ c ] = 0;
}

// ndvBlitzSprite() recomputes several values that are actually constant
// for a whole (sprite, page) call - wMax/picByte/recupeLineY/spriteYLine/
// spriteYDecalage never depend on which column is being read, only on x
// relative to the sprite's own left edge - but it recomputed all of them
// on every single column call. Harmless for a narrow sprite, but LineND
// ("washing lines") is 104 of 128 columns wide, so this was ~103 wasted
// recomputations of the same 5 values on every relevant page, every
// frame it's active - a direct user report ("game reaches washing lines,
// hits 100% CPU") traced to this after call-site page-gating alone
// (see ndvDrawSprites()) wasn't enough on its own. Both ndvOrBlit() and
// ndvXorBlit() now compute those row-constant values once here, then
// only do the genuinely per-column work (scanA/scanB/outByte) inside the
// loop - the same "hoist row-invariant work out of the per-column loop"
// technique already used elsewhere in this project (e.g. TinY Fi's own
// tfiBlitzSpriteRow()). Output is unchanged - this is the exact same
// math ndvBlitzSprite() already did, just computed in the right place.

void ndvOrBlit( int x, int y, int* table, int frame, int page )
{
    int wSprite = table[ 0 ];
    int pages = ndvPageCount( table );
    int wMax = ( pages * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = ndvRecupeLineY( y );
    if( recupeLineY > page || ( recupeLineY + pages ) < page ) return;

    int spriteYLine = page - recupeLineY;
    int spriteYDecalage = ndvRecupeDecalageY( y );

    int cMin = x;
    int cMax = x + wSprite - 1;
    if( cMin < 0 ) cMin = 0;
    if( cMax > 127 ) cMax = 127;

    int c;
    for( c = cMin; c <= cMax; c++ )
    {
        int scanA = ( c - x ) + ( spriteYLine * wSprite ) + 2;
        int outByte;
        if( scanA > wMax ) outByte = 0x00;
        else outByte = ndvSplitSpriteDecalageY( spriteYDecalage, table[ scanA + picByte ], 1 );

        if( spriteYLine > 0 )
        {
            int scanB = ( c - x ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;
            if( scanB <= wMax )
              outByte = outByte | ndvSplitSpriteDecalageY( spriteYDecalage, table[ scanB + picByte ], 0 );
        }
        ndvPageBuffer[ c ] = ndvPageBuffer[ c ] | outByte;
    }
}

void ndvXorBlit( int x, int y, int* table, int frame, int page )
{
    int wSprite = table[ 0 ];
    int pages = ndvPageCount( table );
    int wMax = ( pages * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = ndvRecupeLineY( y );
    if( recupeLineY > page || ( recupeLineY + pages ) < page ) return;

    int spriteYLine = page - recupeLineY;
    int spriteYDecalage = ndvRecupeDecalageY( y );

    int cMin = x;
    int cMax = x + wSprite - 1;
    if( cMin < 0 ) cMin = 0;
    if( cMax > 127 ) cMax = 127;

    int c;
    for( c = cMin; c <= cMax; c++ )
    {
        int scanA = ( c - x ) + ( spriteYLine * wSprite ) + 2;
        int outByte;
        if( scanA > wMax ) outByte = 0x00;
        else outByte = ndvSplitSpriteDecalageY( spriteYDecalage, table[ scanA + picByte ], 1 );

        if( spriteYLine > 0 )
        {
            int scanB = ( c - x ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;
            if( scanB <= wMax )
              outByte = outByte | ndvSplitSpriteDecalageY( spriteYDecalage, table[ scanB + picByte ], 0 );
        }
        ndvPageBuffer[ c ] = ndvPageBuffer[ c ] ^ outByte;
    }
}

void ndvDrawSideWalls( int page )
{
    ndvOrBlit( 118, ndvYScroll, ndvDroiteND, 0, page );
    ndvOrBlit( 118, ndvYScroll + 64, ndvDroiteND, 0, page );
    ndvOrBlit( 0, ndvYScroll, ndvGaucheND, 0, page );
    ndvOrBlit( 0, ndvYScroll + 64, ndvGaucheND, 0, page );
}

void ndvDrawSprites( int page )
{
    int t;
    for( t = 0; t < NDV_MAX_SPRITES; t++ )
    {
        if( ndvSprites[ t ].actif == 1 )
        {
            int* table = ndvResolvePic( ndvSprites[ t ].pic );
            int px = ndvSprites[ t ].x + ndvWavXAnim( t );
            int py = ndvSprites[ t ].y + ndvWavAnim( t );

            // Call-site page gate - a literal duplicate of ndvBlitzSprite's
            // own internal bounds check, not an approximation, so this
            // can't change what renders. Needed because a self-gated call
            // still costs a full call every time it's invoked (this
            // project's own recurring lesson - Arkanoid/Bert/Tris/Trick/
            // Morpion etc) - without it, every active sprite gets called
            // on all 8 pages regardless of its real footprint. Especially
            // costly for NDV_LINE ("washing lines" - upstream's LineND is
            // 104 of 128 columns wide but only ever 3 pages tall), which
            // is exactly the case a direct user report caught pegging
            // CPU at 100%.
            int pages = ndvPageCount( table );
            int firstPage = ndvRecupeLineY( py );
            if( page < firstPage || page > firstPage + pages ) continue;

            if( t == NDV_LINE )
              ndvXorBlit( px, py, table, ndvSprites[ t ].animFrame, page );
            else
              ndvOrBlit( px, py, table, ndvSelectFrame( t ), page );
        }
    }
}

void ndvScorePannel( int x, int y, int page )
{
    ndvXorBlit( 22 + x, y, ndvNUMERICND, ndvPanD1, page );
    ndvXorBlit( 18 + x, y, ndvNUMERICND, ndvPanD10, page );
    ndvXorBlit( 14 + x, y, ndvNUMERICND, ndvPanD100, page );
    ndvXorBlit( 10 + x, y, ndvNUMERICND, ndvPanD1000, page );
    ndvXorBlit( 6 + x, y, ndvNUMERICND, ndvPanD10000, page );
}

void ndvDrawScore( int page )
{
    if( page != 0 ) return;
    ndvXorBlit( 25, 0, ndvSCOREND, 0, page );
    ndvCalculatePan( ndvScores );
    ndvScorePannel( 41, 0, page );
    ndvXorBlit( 75, 0, ndvSCOREND, 1, page );
    ndvCalculatePan( ndvHiScores );
    ndvScorePannel( 91, 0, page );
}

void ndvDrawLives( int page )
{
    if( page != 0 ) return;
    int t;
    for( t = 0; t < ndvLives; t++ )
      ndvXorBlit( 12 + ( t * 4 ), 1, ndvHARTND, 0, page );
}

// -----------------------------------------------------------------------------
// Splash sequence (simplified - see this file's own header comment)
// -----------------------------------------------------------------------------

#define NDV_SPLASH_STEP0_FRAMES 90
#define NDV_SPLASH_STEP1_FRAMES 40
#define NDV_SPLASH_STEP2_FRAMES 60
#define NDV_SPLASH_STEP3_FRAMES 60

void ndvComposeSplash( int page )
{
    if( ndvSplashStep == 0 ) ndvOrBlit( 0, 0, ndvTinyJoypadND, 0, page );
    else if( ndvSplashStep == 1 ) ndvOrBlit( 0, 0, ndvLoading1ND, 0, page );
    else if( ndvSplashStep == 2 ) ndvOrBlit( 0, 0, ndvPic2ND, 0, page );
    else ndvOrBlit( 0, 0, ndvPaxND, 0, page );
}

void ndvUpdateSplash( void )
{
    ndvSubFrame = ndvSubFrame + 1;

    int limit = NDV_SPLASH_STEP3_FRAMES;
    if( ndvSplashStep == 0 ) limit = NDV_SPLASH_STEP0_FRAMES;
    else if( ndvSplashStep == 1 ) limit = NDV_SPLASH_STEP1_FRAMES;
    else if( ndvSplashStep == 2 ) limit = NDV_SPLASH_STEP2_FRAMES;

    if( ndvSplashStep == 1 && ( ndvSubFrame == 5 || ndvSubFrame == 25 ) )
    {
        if( ndvSubFrame == 5 ) Sound( 200, 20 );
        else Sound( 220, 20 );
    }

    if( ndvSubFrame >= limit )
    {
        ndvSubFrame = 0;
        ndvSplashStep = ndvSplashStep + 1;
        if( ndvSplashStep > 3 )
        {
            ndvBeginAttract();
            return;
        }
    }
}

// -----------------------------------------------------------------------------
// Attract screen
// -----------------------------------------------------------------------------

void ndvComposeAttract( int page )
{
    ndvOrBlit( 18, 2, ndvnohzdyve_IntroND, 0, page );
    if( ndvCnt > 1 ) ndvOrBlit( 42, 42, ndvStartGameND, 0, page );
    ndvDrawSideWalls( page );
    ndvDrawSprites( page );
}

void ndvUpdateAttract( void )
{
    // Matches upstream's own StartPageND exactly - it never calls
    // SinWavRunND() (only PlayGameND does, via UpDateMainND()).
    ndvFlipScreenTick();
    ndvFrameRate();

    if( isFirePressed() && ndvOneClic == 0 )
    {
        ndvAttractSG = 1;
        ndvState = NDV_STATE_ATTRACT_FADE;
        ndvAttractFadeStep = 0;
    }
    if( !isFirePressed() ) ndvOneClic = 0;
}

// Attract fade-out: erases the intro picture in a growing dither pattern
// (AND-NOT against fadeND's own mask), a 12x4 grid of 8x8 cells, one mask
// step every ndvTr3 trigger (2-tick interval) until fully erased.
void ndvComposeAttractFade( int page )
{
    // Draw the intro picture FIRST, then erase (AND-NOT) the growing dither
    // mask over it - matching upstream's own call order exactly
    // (DrawMainScreenND() then FadeMainScreenND() every loop iteration).
    // Erasing before drawing would just have the intro picture's own OR
    // overwrite the erased cells right back again.
    ndvOrBlit( 18, 2, ndvnohzdyve_IntroND, 0, page );

    int y;
    int x;
    int fadePages = ndvPageCount( ndvfadeND );
    for( y = 0; y < 4; y++ )
    {
        int cellY = 2 + ( y * 8 );
        int cellPage = ndvRecupeLineY( cellY );
        if( cellPage != page ) continue;
        for( x = 0; x < 12; x++ )
        {
            int cellX = 18 + ( x * 8 );
            int c;
            for( c = cellX; c < cellX + 8 && c < 128; c++ )
              ndvPageBuffer[ c ] = ndvPageBuffer[ c ] & ( 0xFF - ndvBlitzSprite( cellX, cellY, fadePages, c, page, ndvAttractFadeStep, ndvfadeND ) );
        }
    }
    ndvDrawSideWalls( page );
    ndvDrawSprites( page );
}

void ndvUpdateAttractFade( void )
{
    // StartPageND's own while(1) loop keeps calling these every tick
    // regardless of SG's value - only the drawn content (fade vs. "press
    // start" text) differs once SG is set. SinWavRunND() is deliberately
    // NOT called here (nor in ndvUpdateAttract) - upstream's own
    // StartPageND never calls it at all, only PlayGameND does (via
    // UpDateMainND()); the Tr3 timer instead gets its one, single
    // Trigger() poll for this whole tick right below, matching upstream's
    // own `FadeMainScreenND(&FDT, Tr3.Trigger())` call - polling it a
    // second time (e.g. from inside SinWavRunND) would silently halve its
    // real trigger rate, since Trigger() advances startTime as a side
    // effect on every call regardless of which caller made it.
    ndvFlipScreenTick();
    ndvFrameRate();

    if( ndvTimerTriggerP( &ndvTr3 ) )
    {
        if( ndvAttractFadeStep < 8 )
        {
            ndvAttractFadeStep = ndvAttractFadeStep + 1;
        }
        else
        {
            ndvBeginWindowOpen();
        }
    }
}

// -----------------------------------------------------------------------------
// Per-life window-opening reveal (ExitWindowND)
// -----------------------------------------------------------------------------

void ndvChangePic( void )
{
    int wy = ndvSprites[ NDV_WINDOW ].y;
    if( wy < 30 ) ndvSprites[ NDV_WINDOW ].animFrame = 2;
    else if( wy < 36 ) ndvSprites[ NDV_WINDOW ].animFrame = 1;
    else ndvSprites[ NDV_WINDOW ].animFrame = 0;

    if( wy == 30 ) ndvActivateSprite( NDV_SLIDE, 12, 30, NDV_SLIDE );

    if( ndvSprites[ NDV_SLIDE ].actif )
    {
        if( ndvSprites[ NDV_SLIDE ].x < 64 )
        {
            ndvSprites[ NDV_SLIDE ].x = ndvSprites[ NDV_SLIDE ].x + 1;
        }
        else
        {
            ndvBeginPlaying();
        }
    }
}

void ndvComposeWindowOpen( int page )
{
    ndvDrawLives( page );
    ndvDrawScore( page );
    ndvDrawSideWalls( page );
    if( ndvSprites[ NDV_SLIDE ].x > 20 )
      ndvOrBlit( ndvSprites[ NDV_SLIDE ].x - 18, ndvSprites[ NDV_SLIDE ].y, ndvSlideMainPND, 1, page );
    ndvDrawSprites( page );
}

void ndvUpdateWindowOpen( void )
{
    ndvChangePic();
    if( ndvState != NDV_STATE_WINDOW_OPEN ) return; // ndvChangePic may have advanced us
    ndvFrameRate();
    ndvScrollDown( 3 );
    ndvFlipScreenTick();
}

// -----------------------------------------------------------------------------
// Core gameplay (PlayGameND)
// -----------------------------------------------------------------------------

void ndvComposePlaying( int page )
{
    ndvDrawSideWalls( page );
    ndvDrawSprites( page );
    if( ndvSprites[ NDV_MAIN ].actif == 0 && ndvLives == 0 )
      ndvOrBlit( 42, 42, ndvGameOverND, 0, page );
    ndvDrawScore( page );
    ndvDrawLives( page );
}

void ndvUpdatePlaying( void )
{
    ndvJoyPadRefresh();
    ndvUpdateMain();
    ndvGamePlayAdj();
    ndvFrameRate();
    ndvCollisionCheck();

    if( ndvSprites[ NDV_MAIN ].actif ) ndvScrollDown( 1 );

    if( ndvSprites[ NDV_MAIN ].actif == 0 )
    {
        if( isFirePressed() && ndvOneClic == 0 && !ndvTr9.activ )
        {
            ndvOneClic = 2;
            if( ndvLives > 0 )
            {
                ndvLives = ndvLives - 1;
                ndvBeginWindowOpen();
                return;
            }
            else
            {
                ndvBeginAttract();
                return;
            }
        }
        if( !isFirePressed() && ndvOneClic == 2 ) ndvOneClic = 0;
    }

    ndvFlipScreenTick();
    ndvPlaySong();
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void ndvComposeRow( int page )
{
    ndvClearPageBuffer();
    if( ndvState == NDV_STATE_SPLASH ) ndvComposeSplash( page );
    else if( ndvState == NDV_STATE_ATTRACT ) ndvComposeAttract( page );
    else if( ndvState == NDV_STATE_ATTRACT_FADE ) ndvComposeAttractFade( page );
    else if( ndvState == NDV_STATE_WINDOW_OPEN ) ndvComposeWindowOpen( page );
    else ndvComposePlaying( page );
}

void ndvRenderFrame( void )
{
    md_beginFrame();
    int page;
    int col;
    for( page = 0; page < 8; page++ )
    {
        ndvComposeRow( page );
        for( col = 0; col < 128; col++ )
          md_drawColumn( col, page, ndvPageBuffer[ col ] );
    }
}

void gameNohzdyve_init( void )
{
    InitTinyJoypad();
    ndvState = NDV_STATE_SPLASH;
    ndvSplashStep = 0;
    ndvSubFrame = 0;
    ndvHiScores = eeprom_read_word( 0 );
    // Virgin slot: a never-written pair of 0xFF cells composes to 65535
    // via eeprom_read_word() on this platform (not a negative 16-bit int
    // the way real AVR hardware would give) - matches every other
    // "simple 2-byte score" game's own guard in this project.
    if( ndvHiScores == 65535 ) ndvHiScores = 0;
    ndvScores = 0;
    ndvLives = 3;
    ndvCnt = 0;
    ndvSpriteSetCFG();
    ndvTimerConfig();
}

void gameNohzdyve_update( void )
{
    if( ndvState == NDV_STATE_SPLASH ) ndvUpdateSplash();
    else if( ndvState == NDV_STATE_ATTRACT ) ndvUpdateAttract();
    else if( ndvState == NDV_STATE_ATTRACT_FADE ) ndvUpdateAttractFade();
    else if( ndvState == NDV_STATE_WINDOW_OPEN ) ndvUpdateWindowOpen();
    else ndvUpdatePlaying();

    ndvRenderFrame();
}

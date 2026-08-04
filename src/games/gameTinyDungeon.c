// =============================================================================
// Tiny Dungeon v2.0.1 - ported from Sven B's TinyDungeon (tinyjoypad.com,
// MIT License, contact Lorandil@gmx.de - same author/contact as Tiny Minez,
// credited here the same way: "SVEN B / LORANDIL"). Same tinyJoypadShim
// lineage as every other Daniel-C/Sven-B game here (isXPressed() reused
// as-is) - but this game's own display/render code never used the shim's
// SendPixels() streaming API at all, so this port skips that layer and
// calls md_drawColumn() directly instead (see tdngRenderImage() below).
//
// A first-person dungeon crawler: rotate/move through a 16x16 grid one
// cell at a time, fight monsters, collect items, open chests, trigger
// switches/teleporters/spinners, find the fountain (victory item) and get
// out alive. The hardest, most deferred port in this whole project - a
// full C++ class combining a raycaster-like renderer with combat/dice/
// inventory logic, previously scoped but explicitly left for later. Tiny
// Arena (this project's first raycaster port) was picked earlier
// specifically to de-risk the rendering technique before attempting this.
//
// KEY FINDING: despite the "raycaster" framing, the active renderer
// (bitmapDrawing.cpp's `#else` branch - the OTHER, disabled `#if 0` branch
// supports a fuller 0-7 view-distance model, but was never compiled even
// upstream) is not a runtime DDA raycast at all. It's a small, fixed
// 24-entry lookup table (`arrayOfWallInfo`/`tdngWallInfo` here) describing
// which screen-column ranges each of a handful of pre-drawn wall bitmaps
// (view distances 0-3 only) can occupy, checked against the actual dungeon
// cell in that direction/distance. This maps onto the exact same "compute
// one byte per (column,page), call md_drawColumn()" model every other
// port here already uses - no new machineDependent primitives needed.
//
// Structural conversions from upstream:
//  - `Dungeon`/`DUNGEON` C++ classes -> flat global state + `td*`-prefixed
//    functions, the same treatment already proven for Tiny Minez/Missile/
//    Pipe/Plaque/SQuest/DDug's own class hierarchies.
//  - `gameLoop()`'s outer `while(isPlayerAlive())` and
//    `checkPlayerMovement()`'s inner `while(!playerAction &&
//    !disableFlashEffect)` busy-wait-for-input loop both become an
//    explicit per-frame state machine (`TDNG_STATE_*`), the same
//    "blocking loop -> resumable state" treatment every port here needs.
//    Upstream's various "flash the screen/monster/status bar, held until
//    the player does something else" effects (teleporter/spinner XOR
//    flash, hit-monster inversion held through a busy-wait for Fire to be
//    released, retaliation-hit inversion) all relied on renderImage()
//    being called sparingly by hand at exact moments - this engine redraws
//    every real frame unconditionally (this project's own standing
//    convention), so each flash became its own explicit fixed-duration
//    state (~200-250ms) instead of relying on renderImage()'s own
//    self-clearing side effect, which would otherwise clear the very next
//    engine frame (1/60s) instead of staying visible.
//  - `getCellRaw()` returned a raw `uint8_t*` into the level array upstream
//    (with the rest of the code doing pointer arithmetic like
//    `cell - _dungeon.currentLevel` to recover an index) - ported as a
//    plain integer index (`tdngGetCellIndex()`) into `tdngCurrentLevel[256]`
//    throughout instead, avoiding pointer arithmetic entirely.
//  - `NON_WALL_OBJECT`/`SIMPLE_WALL_INFO`'s own `bitmapData`/`wallBitmap`
//    fields are C pointers to OTHER global bitmap arrays baked into a
//    PROGMEM static initializer - never attempted anywhere else in this
//    project and not proven safe under this dialect. Replaced with a
//    small integer ID per bitmap/wall table (`tdngObjectList`/`tdngWallInfo`,
//    both plain `int[]` data now) plus `tdngResolveBitmapArray()`/
//    `tdngWallBitmapByteAt()` helpers - a *runtime* pointer assignment
//    (`int* arr = tdngJoey;` etc, proven safe elsewhere in this project,
//    e.g. gameTinyMinez.c's own dispatch) is fine; only a pointer baked
//    into a *static initializer* was the untested risk.
//  - Every data table (level grid, interaction/special-cell/monster
//    tables, all bitmaps, all lookup tables) was extracted with a Python
//    script and byte-diff verified against the original source before
//    ever being pasted in here - this project's established anti-Bomber-
//    bug discipline for large hand-transcription-prone tables. The
//    symbolic ones (Level_1's cell values, interactionData, monsterStats)
//    use enum-combining expressions (`SWITCH_L | N_S`,
//    `4 + 4 * LEVEL_WIDTH`) rather than plain literals upstream - a small
//    Python evaluator with the real enum constants defined was used to
//    resolve these to final byte values rather than hand-computing them.
//
// A latent AVR-implicit-behavior bug this project's "check every 0xFF/
// wraparound-reliant site" discipline caught BEFORE ever compiling (not
// via a crash report): `getDownScaledBitmapData()`'s inner bit-scan loop
// is `for (uint8_t bitValue = 1; bitValue != 0; bitValue <<= 1)` - this
// relies on uint8_t wraparound (128 << 1 == 256 == 0 on a real byte) to
// terminate after exactly 8 iterations. Vircon32 ints don't wrap at 8
// bits, so `bitValue` would just keep growing forever and the loop would
// never terminate - fixed with an explicit `bitValue <= 128` bound
// instead (same class of bug as the shift-count-wraparound and cave-phase
// counter issues found in earlier ports).
//
// Two more upstream quirks, both harmless on real AVR flash (silently
// reads a few garbage-but-not-crashing PROGMEM bytes past a small lookup
// table) but genuine out-of-bounds risks on Vircon32's real memory model
// (the same class of issue as Tiny Arena's own Lvl1 off-by-one bound):
//  - `maxObjectDistance` (the NWO draw-distance loop's start value)
//    defaults to MAX_VIEW_DISTANCE (7) if no wall at all is found in a
//    column's line of sight (a long, empty corridor) - but the 4-entry
//    scaling/offset tables and 12-entry objectCenterPositions table only
//    cover distances 0-3. Clamped to 3 right after the wall search,
//    matching the "fix once, centrally" precedent from this project's
//    very first bug (md_drawColumn's byte mask).
//  - `getLightingMask()`'s index (`lightingOffset + viewDistance*2 +
//    (x&1)`, up to 10+7=17) can exceed the 16-entry lightingTable during
//    the initial fade-in. Clamped to [0,15] inside the helper itself.
//
// Sound: real ATtiny85 hardware trades this game's sound effects away
// entirely for the dungeon-floor rendering feature (`settings.h` defines
// both `_ENABLE_DUNGEON_FLOOR_` and `NO_SOUND` together for that target -
// every one of upstream's own sound functions is a complete no-op on real
// hardware). This port stays faithful to that shipped, silent behavior
// (floor rendering IS ported - see the background-fill step in
// tdngRenderDungeonColumn()) rather than adding new sound effects Vircon32
// could otherwise easily afford; a real, cheap enhancement opportunity if
// ever requested.
// =============================================================================

// -----------------------------------------------------------------------------
// Constants (mirroring dungeonTypes.h)
// -----------------------------------------------------------------------------

#define TDNG_LEVEL_WIDTH 16
#define TDNG_LEVEL_HEIGHT 16
#define TDNG_MAX_LEVEL_BYTES 256
#define TDNG_MAX_VIEW_DISTANCE 7
#define TDNG_MAX_MONSTERS 10

#define TDNG_WINDOW_SIZE_X 96
#define TDNG_WINDOW_SIZE_Y 64
#define TDNG_DASHBOARD_SIZE_X 32
#define TDNG_DASHBOARD_SIZE_Y 64

#define TDNG_POTION_HITPOINT_BONUS 8

#define TDNG_COMPASS_ROW 0
#define TDNG_SKELETON_ROW 3
#define TDNG_HIT_POINTS_ROW 3
#define TDNG_ITEMS_ROW 4
#define TDNG_VICTORY_ROW 5

#define TDNG_ITEM_SWORD_POS_X 2
#define TDNG_ITEM_SHIELD_POS_X 8
#define TDNG_ITEM_AMULET_POS_X 14
#define TDNG_ITEM_RING_POS_X 20
#define TDNG_ITEM_KEY_POS_X 26
#define TDNG_ITEM_LAST_POS_X 31
#define TDNG_COMPASS_START_X 14
#define TDNG_COMPASS_SIZE_X 5

// item type / cell value bits (computed via Python from the real enum
// combinations, not hand-added - see this file's own header comment)
#define TDNG_EMPTY 0
#define TDNG_FLAG_LIMITED_VISIBILITY 2
#define TDNG_FLAG_MONSTER 4
#define TDNG_FLAG_SOLID 8
#define TDNG_FAKE_WALL 16
#define TDNG_WALL 24
// WALL_MASK == FAKE_WALL == (WALL & ~FLAG_SOLID) == 16 - all three are the
// same value upstream, so a single constant covers every "looks like a
// wall, fake or solid" check.
#define TDNG_WALL_MASK 16
#define TDNG_DOOR 56
#define TDNG_SWITCH_L 88
#define TDNG_SWITCH_R 120
#define TDNG_RAT 44
#define TDNG_SKELETON 76
#define TDNG_BEHOLDER 108
#define TDNG_CLOSED_CHEST 128
#define TDNG_MIMIC 140
#define TDNG_OPEN_CHEST 160
#define TDNG_FOUNTAIN 200
#define TDNG_BARS 232
#define TDNG_OBJECT_MASK 252
#define TDNG_E_W 2
#define TDNG_N_S 3
#define TDNG_LIMITED_VISIBILITY_MASK 1

#define TDNG_ITEM_COMPASS 1
#define TDNG_ITEM_AMULET 2
#define TDNG_ITEM_RING 4
#define TDNG_ITEM_KEY 8
#define TDNG_ITEM_POTION 16
#define TDNG_ITEM_SWORD 32
#define TDNG_ITEM_SHIELD 64
#define TDNG_ITEM_VICTORY 128

#define TDNG_TELEPORTER 1
#define TDNG_SPINNER 2

#define TDNG_NORTH 0
#define TDNG_EAST 1
#define TDNG_SOUTH 2
#define TDNG_WEST 3

// -----------------------------------------------------------------------------
// Data tables - byte-diff verified against the original source (see this
// file's own header comment). Symbolic tables (level grid, interaction
// data, monster stats) were resolved to final numeric values with a small
// Python evaluator using the real enum constants, not hand-computed.
// -----------------------------------------------------------------------------

int[16] tdngNibbleBitCount =
{
0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,
};

int[4] tdngScalingFactorFromDistance =
{
0,1,2,4,
};

int[4] tdngVerticalStartOffset =
{
0,0,2,3,
};

int[4] tdngVerticalEndOffset =
{
0,7,5,4,
};

int[5] tdngBitMaskFromScalingFactor =
{
0,1,3,0,15,
};

int[16] tdngLightingTable =
{
255,255,255,255,85,170,136,36,0,0,0,0,0,0,0,0,
};

int[12] tdngObjectCenterPositions =
{
-127,48,127,-19,48,115,18,48,78,33,48,63,
};

int[256] tdngLevel1Init =
{
24,24,24,24,24,24,24,0,24,24,0,0,0,0,0,24,
0,0,234,0,130,24,0,24,131,24,0,24,24,24,0,24,
0,0,24,0,24,0,0,0,0,24,0,24,200,24,0,24,
0,24,0,0,24,0,24,0,0,24,0,24,0,24,0,24,
88,24,0,24,90,0,24,0,0,24,0,24,0,234,0,24,
24,0,0,0,24,24,0,0,24,24,0,24,24,24,24,24,
0,0,24,0,0,0,0,24,128,24,0,0,0,0,130,234,
24,24,0,0,24,24,24,24,24,24,24,24,24,235,24,24,
24,16,0,0,24,0,24,0,24,0,24,0,0,0,0,0,
24,0,24,16,24,0,0,0,0,0,24,235,24,0,24,0,
24,0,24,128,24,0,24,0,24,0,24,0,24,0,0,0,
24,0,0,24,24,0,0,0,0,0,24,24,0,0,24,0,
91,24,16,234,234,0,24,128,90,0,0,58,0,24,0,0,
0,0,0,24,24,0,0,0,0,0,24,0,0,24,0,24,
24,24,0,0,24,0,24,0,24,0,24,0,24,0,0,0,
0,91,24,91,24,0,0,0,0,0,24,24,24,91,24,0,
};

int[126] tdngInteractionData =
{
68,88,120,0,88,0,
7,88,120,0,18,0,
7,120,88,0,18,0,
20,128,160,1,20,162,
24,128,160,64,24,160,
44,200,200,128,44,200,
64,88,120,0,18,0,
64,120,88,0,18,234,
104,128,160,2,104,163,
110,128,160,20,110,162,
192,88,120,0,195,0,
192,120,88,0,196,234,
199,128,160,9,199,160,
200,88,0,0,202,0,
203,56,0,0,203,0,
241,88,120,0,145,235,
241,120,88,0,145,0,
243,88,120,0,195,234,
243,120,88,0,196,0,
253,88,120,0,125,0,
253,120,88,0,77,0,
};
#define TDNG_INTERACTION_COUNT 21

int[16] tdngSpecialCellFX =
{
1,183,5,13,
1,215,5,11,
2,42,2,0,
2,151,3,0,
};
#define TDNG_SPECIALFX_COUNT 4

int[60] tdngMonsterStatsInit =
{
48,44,3,-6,1,0,
40,44,3,-4,1,0,
60,108,40,7,1,0,
83,44,5,-4,1,0,
109,76,15,4,0,96,
135,76,12,3,0,96,
163,140,15,-1,1,48,
171,140,15,1,0,48,
227,76,12,3,0,96,
237,76,12,3,0,96,
};

// itemType,bitmapWidth,vertOffsetBits,heightBytes,thr0,thr1,thr2,bitmapId
// bitmap id order: joey=0, beholder=1, newBars=2, door=3, leverLeft=4,
// leverRight=5, chestClosed=6, chestOpen=7, fountain=8, rat=9
int[88] tdngObjectList =
{
76,28,16,5,1,2,5,0,
108,32,0,7,1,2,5,1,
232,28,8,6,1,2,5,2,
56,32,8,7,1,3,12,3,
88,16,24,1,1,2,8,4,
120,16,24,1,1,2,8,5,
128,24,32,3,1,3,8,6,
140,24,32,3,1,3,8,6,
160,24,32,3,1,3,8,7,
200,12,32,3,1,2,99,8,
44,20,40,2,1,2,99,9,
};
#define TDNG_OBJECT_COUNT 11

// wallId,startX,endX,posStartEndY,viewDistance,leftRightOffset,relPos,width
// wall id order: leftRightWallsD0=0, smallFrontWallD1=1, leftRightWallsD1=2,
// smallFrontWallD2=3, leftRightWallsD2=4, outerLeftRightWallsD2=5,
// smallFrontWallD3=6, leftRightWallsD3=7, outerLeftRightWallsD3=8
int[192] tdngWallInfo =
{
0,0,3,7,0,-1,0,8,
0,92,95,7,0,1,4,8,
1,0,3,7,1,-1,84,88,
1,4,91,7,1,0,0,88,
1,92,95,7,1,1,0,88,
2,4,25,7,1,-1,0,44,
2,70,91,7,1,1,22,44,
3,0,25,37,2,-1,18,44,
3,26,69,37,2,0,0,44,
3,70,95,37,2,1,0,44,
4,26,36,37,2,-1,0,22,
4,59,69,37,2,1,11,22,
5,0,14,37,2,-2,0,30,
5,81,95,37,2,2,15,30,
6,0,14,52,3,-2,7,22,
6,15,36,52,3,-1,0,22,
6,37,58,52,3,0,0,22,
6,59,80,52,3,1,0,22,
6,81,95,52,3,2,0,22,
7,37,41,52,3,-1,0,10,
7,54,58,52,3,1,5,10,
8,15,29,52,3,-2,0,30,
8,66,80,52,3,2,15,30,
-1,0,0,7,0,0,0,96,
};
#define TDNG_WALLINFO_COUNT 24

int[448] tdngBeholder =
{
56,0,0,192,69,7,0,68,0,0,48,0,24,0,130,3,
0,8,0,32,0,57,4,0,4,254,64,0,41,24,0,130,
255,131,0,57,96,0,225,239,7,1,130,131,129,224,231,7,
2,68,12,70,232,247,47,4,56,48,72,228,243,111,4,0,
192,48,212,251,71,8,0,0,51,150,251,211,8,56,0,28,
54,255,217,8,68,192,19,115,225,220,17,130,63,28,243,64,
222,17,57,0,16,243,192,223,17,41,128,17,251,192,223,17,
57,126,30,155,64,222,17,130,1,30,203,225,216,17,68,128,
17,243,255,211,17,56,96,48,242,237,223,8,112,24,44,246,
237,223,8,136,4,35,228,205,79,8,4,195,64,236,220,111,
4,114,48,64,104,158,47,4,82,8,128,96,191,7,2,114,
4,0,225,255,3,1,4,3,0,130,255,131,0,136,0,0,
4,254,64,0,112,0,0,8,0,32,0,0,0,0,48,0,
24,0,0,0,0,192,69,7,0,0,0,0,0,0,0,0,
199,255,255,63,0,248,255,131,255,255,15,0,224,255,1,252,
255,7,0,192,255,0,248,255,3,0,128,255,0,224,255,1,
0,0,255,0,128,255,0,0,0,254,1,0,126,0,0,0,
252,131,3,56,0,0,0,248,199,15,48,0,0,0,248,255,
63,0,0,0,0,240,255,255,0,0,0,0,240,199,255,3,
0,0,0,240,131,63,12,0,0,0,224,1,0,0,0,0,
0,224,0,0,0,0,0,0,224,0,0,0,0,0,0,224,
0,128,1,0,0,0,224,1,254,1,0,0,0,224,131,127,
0,0,0,0,224,199,31,0,0,0,0,240,143,7,16,0,
0,0,240,7,3,28,0,0,0,240,3,0,63,0,0,0,
248,1,192,63,0,0,0,248,1,240,127,0,0,0,252,1,
248,255,0,0,0,254,3,252,255,1,0,0,255,7,255,255,
3,0,128,255,143,255,255,7,0,192,255,255,255,255,15,0,
224,255,255,255,255,63,0,248,255,255,255,255,255,255,255,255,
};

int[280] tdngJoey =
{
0,62,0,0,0,0,193,7,0,0,128,0,120,4,0,0,
63,128,11,0,0,192,3,10,0,0,0,124,8,0,0,0,
128,16,0,0,0,128,16,0,0,0,0,6,0,0,0,128,
15,0,0,0,240,1,0,0,0,60,0,64,224,1,7,0,
96,240,15,125,8,32,56,15,245,93,45,56,41,85,19,0,
252,175,3,0,0,252,41,249,15,0,60,15,217,24,0,56,
15,53,48,45,248,7,119,102,32,224,7,19,105,96,0,0,
118,102,64,0,0,48,48,0,0,0,216,24,0,0,0,248,
15,0,0,0,0,0,0,0,0,0,0,0,255,193,255,255,
255,255,0,248,255,255,127,0,128,251,255,255,0,0,240,255,
255,63,0,240,255,255,255,3,240,255,255,255,127,224,255,255,
255,127,224,255,255,255,127,224,255,255,255,7,224,255,255,255,
1,224,191,15,254,0,252,31,7,112,128,227,15,3,96,0,
34,128,3,64,0,0,128,1,0,0,0,192,1,0,0,224,
255,1,0,0,192,255,1,64,0,0,192,1,96,0,0,128,
3,112,0,0,128,3,112,0,0,15,15,248,0,0,31,255,
255,1,0,191,255,255,3,128,255,255,255,3,192,255,255,255,
3,224,255,255,255,255,255,255,
};

int[80] tdngRat =
{
0,0,0,12,24,6,126,67,176,67,240,97,240,49,184,14,
252,15,238,31,192,51,192,77,192,95,128,127,0,92,0,8,
40,4,132,10,0,0,0,0,255,226,231,225,129,48,0,8,
1,24,7,12,7,128,3,192,1,224,0,192,17,0,31,0,
31,0,63,0,127,0,195,163,1,224,1,224,123,240,255,255,
};

int[256] tdngStatusPanelVertical =
{
24,0,128,221,221,221,221,221,12,1,128,0,0,0,0,128,
12,129,16,12,40,14,4,0,24,129,144,30,48,112,63,134,
48,191,159,62,24,14,4,137,48,129,144,124,44,48,63,134,
24,181,16,62,6,72,8,2,12,1,143,30,0,48,48,143,
12,57,128,12,30,56,152,130,24,8,134,12,34,64,44,128,
20,49,40,30,94,120,40,0,62,129,142,62,34,0,128,143,
34,185,160,124,30,0,0,136,0,160,174,62,0,0,63,128,
0,249,2,30,6,124,144,14,0,1,172,12,41,10,63,130,
0,1,160,12,81,48,0,143,0,0,164,30,41,72,152,130,
0,1,42,62,6,48,36,6,0,1,158,124,0,56,60,139,
34,0,128,62,28,64,4,138,62,1,12,30,34,120,30,0,
20,1,142,12,42,0,164,139,24,0,138,12,34,120,24,128,
12,101,128,30,28,8,44,128,12,85,6,62,0,112,40,0,
24,8,138,124,7,0,0,128,48,65,140,62,125,48,56,128,
48,0,128,30,87,72,4,128,24,101,14,12,0,126,4,0,
28,84,130,0,0,0,0,128,12,72,140,221,221,221,221,221,
};

int[336] tdngNewBars =
{
0,0,0,0,0,0,0,0,0,0,0,0,254,255,255,255,
255,63,254,255,255,255,255,63,192,0,0,0,0,3,192,0,
0,0,0,3,192,0,0,0,0,3,192,0,0,0,0,3,
254,255,255,255,255,63,254,255,255,255,255,63,192,0,0,0,
0,3,192,0,0,0,0,3,192,0,0,0,0,3,192,0,
0,0,0,3,254,255,255,255,255,63,254,255,255,255,255,63,
192,0,0,0,0,3,192,0,0,0,0,3,192,0,0,0,
0,3,192,0,0,0,0,3,254,255,255,255,255,63,254,255,
255,255,255,63,192,0,0,0,0,3,192,0,0,0,0,3,
192,0,0,0,0,3,192,0,0,0,0,3,254,255,255,255,
255,63,254,255,255,255,255,63,255,255,255,255,255,255,1,0,
0,0,0,192,0,0,0,0,0,128,0,0,0,0,0,128,
1,0,0,0,0,192,31,254,255,255,127,248,31,254,255,255,
127,248,1,0,0,0,0,192,0,0,0,0,0,128,0,0,
0,0,0,128,63,255,255,255,255,252,63,255,255,255,255,252,
63,255,255,255,255,252,63,255,255,255,255,252,0,0,0,0,
0,128,0,0,0,0,0,128,63,255,255,255,255,252,63,255,
255,255,255,252,63,255,255,255,255,252,63,255,255,255,255,252,
0,0,0,0,0,128,0,0,0,0,0,128,1,0,0,0,
0,192,31,254,255,255,127,248,31,254,255,255,127,248,1,0,
0,0,0,192,0,0,0,0,0,128,0,0,0,0,0,128,
};

int[448] tdngDoor =
{
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,197,184,123,55,70,7,128,0,0,0,
0,0,8,64,215,250,255,191,214,13,160,199,248,255,63,198,
13,160,239,253,255,127,239,13,16,2,128,0,4,64,12,192,
255,255,255,255,255,13,208,255,255,255,255,255,1,232,255,255,
255,255,255,13,0,16,0,8,0,1,12,232,255,255,255,255,
255,13,236,255,255,255,255,255,13,230,255,255,255,255,255,13,
12,1,16,0,0,16,0,234,255,255,255,255,255,13,236,255,
255,255,255,255,13,232,255,255,255,255,255,13,0,128,0,15,
0,1,14,232,255,255,249,255,255,13,208,255,255,249,255,255,
1,192,255,255,249,255,255,13,16,0,4,153,0,8,12,160,
255,255,225,255,255,13,160,255,255,225,255,255,13,64,255,255,
255,255,255,13,128,0,0,0,0,0,8,0,247,222,123,119,
119,7,0,0,1,128,128,0,0,0,0,0,0,0,0,0,
255,255,255,255,255,255,255,255,199,248,255,63,198,255,255,4,
0,0,0,130,224,127,0,0,0,0,0,224,63,0,0,0,
0,0,224,31,0,0,0,0,0,224,15,0,0,0,0,0,
224,15,0,0,0,0,0,224,7,0,0,0,0,0,224,7,
0,0,0,0,0,224,7,0,0,0,0,0,224,3,0,0,
0,0,0,224,3,0,0,0,0,0,224,3,0,0,0,0,
0,224,1,0,0,0,0,0,224,0,0,0,0,0,0,224,
1,0,0,0,0,0,224,0,0,0,0,0,0,224,1,0,
0,0,0,0,224,3,0,0,0,0,0,224,3,0,0,0,
0,0,224,3,0,0,0,0,0,224,7,0,0,0,0,0,
224,7,0,0,0,0,0,224,7,0,0,0,0,0,224,15,
0,0,0,0,0,224,15,0,0,0,0,0,224,31,0,0,
0,0,0,224,63,0,0,0,0,0,224,127,0,0,0,0,
0,224,255,0,1,128,128,0,224,255,255,255,255,255,255,255,
};

int[32] tdngLeverLeft =
{
0,0,0,0,0,0,0,0,60,60,60,60,60,60,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

int[32] tdngLeverRight =
{
0,0,60,60,60,60,60,60,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

int[144] tdngChestClosed =
{
0,0,0,0,119,119,128,87,85,64,119,119,64,119,119,64,
119,119,64,119,119,64,119,119,64,119,119,64,135,119,64,39,
118,64,103,118,64,39,118,64,135,119,64,119,119,64,119,119,
64,119,119,64,119,119,64,119,119,64,119,119,128,87,85,0,
119,119,0,0,0,0,0,0,255,0,0,127,0,0,63,0,
0,31,0,0,31,0,0,31,0,0,31,0,0,31,0,0,
31,0,0,31,0,0,31,0,0,31,0,0,31,0,0,31,
0,0,31,0,0,31,0,0,31,0,0,31,0,0,31,0,
0,31,0,0,63,0,0,127,0,0,255,0,0,255,255,255,
};

int[144] tdngChestOpen =
{
0,0,0,0,112,119,0,80,85,252,117,119,220,113,119,220,
117,119,220,113,119,220,117,119,220,113,119,220,117,119,220,241,
119,220,53,118,220,49,118,220,245,119,220,113,119,220,117,119,
220,113,119,220,117,119,220,113,119,252,117,119,0,80,85,0,
112,119,0,0,0,0,0,0,255,7,0,255,3,0,1,0,
0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,
1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,
0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,
0,1,0,0,1,0,0,255,3,0,255,7,0,255,255,255,
};

int[72] tdngFountain =
{
0,0,0,0,0,0,96,12,0,16,52,16,48,106,56,36,
232,47,252,15,32,36,232,47,48,106,56,16,52,16,96,12,
0,0,0,0,255,255,255,159,243,255,15,193,239,135,129,199,
131,0,128,1,0,128,1,0,128,1,0,128,131,0,128,135,
129,199,15,193,239,159,243,255,
};

int[20] tdngCompass =
{
32,56,62,56,32,62,28,28,8,8,2,14,62,14,2,8,
8,28,28,62,
};

int[704] tdngSmallFrontWallD1 =
{
0,120,252,63,255,159,255,15,128,255,252,63,255,158,255,15,
224,255,248,63,255,158,255,31,240,255,241,159,255,157,255,31,
240,252,1,128,255,159,127,31,112,255,1,128,255,159,255,31,
248,255,241,143,255,159,255,31,248,255,249,31,255,31,255,31,
248,255,253,63,254,31,14,12,248,255,253,63,254,15,192,0,
248,255,253,119,238,15,255,3,248,255,252,111,254,135,255,7,
240,255,252,127,252,195,255,15,240,255,252,255,248,193,251,15,
240,255,254,255,0,224,247,15,240,255,254,255,0,224,255,7,
240,255,254,127,192,195,255,7,240,127,254,127,224,135,255,7,
224,63,254,127,248,143,255,7,192,7,254,127,252,31,255,7,
0,0,254,127,252,31,255,15,0,120,252,127,252,31,255,15,
192,243,220,127,254,159,255,15,224,247,220,127,254,159,255,31,
240,239,253,127,254,159,255,31,240,239,253,127,254,191,255,31,
240,255,249,63,255,63,255,15,240,255,241,63,255,63,255,15,
248,255,1,31,239,63,255,15,248,255,1,0,223,63,255,14,
248,255,225,0,223,63,254,7,248,255,241,31,127,63,224,7,
248,255,249,63,255,63,192,3,240,255,249,63,255,63,14,0,
240,255,249,127,255,31,31,0,240,255,253,127,255,31,255,3,
240,255,253,127,255,31,255,7,240,255,61,127,255,143,255,15,
240,255,253,126,254,135,255,31,224,255,253,125,124,128,255,31,
192,255,252,127,0,128,255,31,0,124,252,123,0,135,255,31,
0,0,252,127,252,143,255,31,128,3,252,127,252,143,255,31,
224,127,252,127,254,159,255,15,224,255,252,127,254,159,191,31,
240,255,252,127,254,159,127,31,240,255,249,63,254,159,127,31,
240,255,249,63,254,159,255,31,240,255,241,31,255,143,255,15,
248,255,225,1,255,151,255,15,248,255,1,0,255,23,255,15,
248,247,1,14,255,27,206,7,248,255,225,63,255,31,0,3,
248,255,241,127,255,31,0,0,248,255,249,127,255,31,120,0,
120,255,249,127,254,31,254,1,240,254,253,127,254,31,255,15,
240,255,253,127,254,143,255,15,240,255,253,127,254,135,255,31,
240,255,253,127,28,128,251,31,224,255,253,255,0,128,255,31,
192,255,253,255,192,135,255,31,192,255,188,255,248,143,255,31,
0,120,200,255,252,143,255,31,0,0,252,127,254,159,251,31,
192,7,252,127,254,159,255,31,224,127,252,127,254,159,255,15,
240,255,252,127,254,159,255,15,240,255,248,127,254,191,255,15,
240,255,249,127,190,191,255,15,240,255,241,63,222,63,127,15,
248,254,1,62,223,63,255,15,248,254,1,48,239,63,255,7,
248,253,241,3,239,127,254,7,248,251,249,15,239,127,252,3,
248,255,249,31,255,127,252,0,248,255,253,127,255,63,56,0,
248,255,125,127,255,31,0,3,248,255,125,127,254,31,192,7,
248,255,253,126,254,15,254,15,248,255,253,127,254,7,255,15,
240,255,253,127,252,128,255,15,240,255,252,127,0,128,255,15,
240,255,248,127,0,142,255,15,224,63,248,63,248,159,255,31,
192,3,248,63,252,191,255,31,0,0,252,63,254,191,239,31,
};

int[176] tdngSmallFrontWallD2 =
{
254,60,127,0,254,124,127,126,190,124,127,123,254,126,62,119,
126,126,0,127,124,127,30,127,0,127,127,127,124,126,127,127,
124,124,127,127,254,48,127,60,254,8,127,0,246,60,119,62,
254,126,127,126,254,126,126,127,222,126,62,123,126,126,0,127,
124,126,60,127,0,110,127,63,120,126,127,63,254,126,127,127,
254,62,127,126,254,12,119,0,254,48,127,60,254,124,127,127,
254,126,127,127,254,126,127,127,126,126,60,119,124,126,0,127,
24,126,60,127,128,124,126,127,252,125,127,127,254,57,127,62,
254,0,127,0,222,60,127,62,238,124,127,127,254,126,63,127,
254,126,63,127,254,126,30,95,254,126,0,127,254,108,62,119,
124,110,110,127,0,118,127,127,28,62,127,126,254,0,127,60,
};

int[44] tdngSmallFrontWallD3 =
{
110,78,228,13,226,98,238,236,14,238,110,238,236,14,224,96,
230,238,238,238,14,238,78,102,230,14,224,224,230,236,238,238,
14,206,110,14,236,108,224,224,238,238,142,238,
};

int[64] tdngLeftRightWallsD0 =
{
255,63,254,127,254,63,254,127,254,127,127,127,255,127,255,126,
252,255,190,127,255,191,255,63,248,253,254,63,255,62,255,15,
248,255,126,127,247,191,255,30,252,253,254,124,255,191,255,62,
254,127,255,127,254,63,255,127,255,63,254,63,252,15,254,127,
};

int[352] tdngLeftRightWallsD1 =
{
240,253,252,1,63,31,0,0,240,247,1,62,255,31,255,7,
224,255,249,127,255,159,247,7,192,255,253,127,255,207,255,3,
128,31,248,127,254,231,255,3,0,192,249,125,0,224,255,1,
0,252,243,123,252,227,247,0,0,254,247,127,254,247,255,0,
0,158,247,127,239,247,127,0,0,252,239,127,223,7,0,0,
0,252,239,63,255,231,31,0,0,248,227,7,255,243,31,0,
0,0,24,56,254,185,15,0,0,192,159,127,0,248,7,0,
0,224,221,127,254,240,3,0,0,192,187,119,255,1,0,0,
0,192,191,125,255,253,3,0,0,128,159,127,253,254,1,0,
0,0,0,127,223,254,0,0,0,0,63,127,126,222,0,0,
0,0,126,255,56,127,0,0,0,0,252,62,6,63,0,0,
0,0,252,254,14,63,0,0,0,0,0,127,112,126,0,0,
0,0,126,0,254,0,0,0,0,0,119,63,255,112,0,0,
0,0,191,127,239,254,1,0,0,0,128,127,255,189,1,0,
0,192,143,127,254,252,3,0,0,224,223,119,0,252,7,0,
0,240,221,63,252,240,7,0,0,240,31,0,254,3,0,0,
0,240,143,63,191,195,15,0,0,0,192,127,255,231,63,0,
0,248,225,127,255,247,62,0,0,254,243,125,255,247,127,0,
0,255,247,126,62,224,255,0,0,215,247,127,192,227,231,0,
128,255,243,63,254,231,255,1,192,255,3,48,127,207,255,0,
192,255,241,3,127,159,127,0,128,24,248,63,255,31,0,0,
0,103,252,127,255,29,255,7,224,255,254,127,255,191,255,15,
};

int[88] tdngLeftRightWallsD2 =
{
252,0,126,0,248,249,62,31,0,252,190,15,240,221,158,15,
96,251,214,6,192,51,222,3,192,195,28,0,128,247,232,1,
0,208,230,0,0,119,238,0,0,14,110,0,0,174,78,0,
0,241,238,0,0,150,224,0,128,247,94,0,128,248,158,2,
192,243,222,3,224,11,166,7,224,252,176,3,0,253,46,12,
248,237,127,31,252,254,126,55,
};

int[20] tdngLeftRightWallsD3 =
{
228,0,220,54,128,22,208,8,176,10,176,10,192,10,24,22,
236,52,104,54,
};

int[120] tdngOuterLeftRightWallsD2 =
{
224,123,223,3,128,127,255,2,64,122,239,3,192,123,239,1,
192,59,11,0,192,71,239,1,128,119,239,1,128,127,239,1,
128,113,254,1,0,116,240,1,0,119,246,0,0,127,254,0,
0,125,229,0,0,127,15,0,0,127,127,0,0,96,247,0,
0,118,223,0,0,119,255,0,0,95,242,0,0,125,244,0,
128,119,254,1,128,71,235,0,128,55,235,0,128,112,15,0,
0,123,239,1,128,123,254,1,192,123,241,2,192,91,255,2,
192,127,254,3,224,123,206,3,
};

int[60] tdngOuterLeftRightWallsD3 =
{
54,119,64,118,108,112,110,54,116,7,124,63,44,63,96,31,
108,10,216,26,88,24,24,2,240,10,192,14,240,14,208,6,
224,8,176,11,216,26,216,24,96,26,24,6,92,23,124,61,
96,63,108,62,110,120,110,118,14,6,110,103,
};

int[288] tdngDungeonFloor =
{
0,0,0,0,0,65,0,0,0,0,0,32,0,0,1,0,
16,64,0,0,64,0,0,0,0,34,5,0,0,64,0,0,
2,0,8,0,16,2,0,0,16,64,0,0,65,0,2,1,
0,0,0,0,0,128,16,17,65,0,0,0,0,0,1,64,
0,72,0,18,4,0,0,0,0,0,0,16,130,0,0,0,
65,64,0,64,0,18,1,0,8,1,0,0,0,0,0,1,
0,17,128,144,2,1,0,0,17,32,0,4,0,0,0,8,
66,64,0,16,1,0,2,64,16,1,1,64,2,0,0,0,
1,16,18,1,0,0,64,0,0,65,16,2,64,0,0,0,
168,84,1,0,0,168,0,0,1,0,18,0,16,0,1,0,
0,1,32,2,64,128,0,1,0,0,0,16,18,1,0,0,
64,0,32,65,16,128,0,32,2,0,0,16,65,128,2,8,
0,0,33,16,0,128,0,4,1,0,8,65,0,2,64,16,
32,0,128,66,1,0,0,64,0,0,64,16,0,66,64,16,
0,0,2,8,0,0,17,0,8,1,16,1,0,0,0,129,
0,0,0,0,68,0,64,0,1,16,2,64,0,8,64,0,
16,2,0,2,0,0,0,0,0,0,73,0,0,0,0,0,
0,0,2,16,0,128,0,0,0,1,0,16,0,0,0,0,
};

// -----------------------------------------------------------------------------
// Bitmap/wall-bitmap ID dispatch - avoids ever needing a pointer-valued
// static initializer or a function returning a pointer to a global array
// (an untested corner of this dialect - see this file's own header
// comment). Every read funnels through one of these two functions.
// -----------------------------------------------------------------------------

// Resolves a bitmap ID to its actual array ONCE (a plain runtime pointer
// assignment, not a static initializer - already proven safe elsewhere in
// this project, e.g. gameTinyMinez.c's own `int* bitmap = ...` dispatch)
// instead of re-running a 10-way if/else chain on every single byte read
// - tdngGetDownScaledBitmapPair()'s own inner scan loop used to do exactly
// that dispatch dozens of times per column for a visible sprite, which
// direct user testing confirmed was the dominant remaining CPU cost.
int* tdngResolveBitmapArray( int bitmapId )
{
    if( bitmapId == 0 ) return tdngJoey;
    if( bitmapId == 1 ) return tdngBeholder;
    if( bitmapId == 2 ) return tdngNewBars;
    if( bitmapId == 3 ) return tdngDoor;
    if( bitmapId == 4 ) return tdngLeverLeft;
    if( bitmapId == 5 ) return tdngLeverRight;
    if( bitmapId == 6 ) return tdngChestClosed;
    if( bitmapId == 7 ) return tdngChestOpen;
    if( bitmapId == 8 ) return tdngFountain;
    return tdngRat; // bitmapId == 9
}

int* tdngResolveWallArray( int wallId )
{
    if( wallId == 0 ) return tdngLeftRightWallsD0;
    if( wallId == 1 ) return tdngSmallFrontWallD1;
    if( wallId == 2 ) return tdngLeftRightWallsD1;
    if( wallId == 3 ) return tdngSmallFrontWallD2;
    if( wallId == 4 ) return tdngLeftRightWallsD2;
    if( wallId == 5 ) return tdngOuterLeftRightWallsD2;
    if( wallId == 6 ) return tdngSmallFrontWallD3;
    if( wallId == 7 ) return tdngLeftRightWallsD3;
    return tdngOuterLeftRightWallsD3; // wallId == 8
}

// -----------------------------------------------------------------------------
// Mutable game state (was DUNGEON)
// -----------------------------------------------------------------------------

int tdngPlayerX;
int tdngPlayerY;
int tdngDir;
int tdngPlayerHP;
int tdngPlayerDamage;
int tdngPlayerArmour;
int tdngPlayerItems;
int tdngDisplayXorEffect;
int tdngInvertMonsterEffect;
int tdngInvertStatusEffect;
int tdngLightingOffset;

int[256] tdngCurrentLevel;
// flattened MONSTER_STATS[10]: position,monsterType,hitpoints,damageBonus,attacksFirst,treasureItemMask
int[60] tdngMonsterStats;

int[8] tdngLineBuffer;

// Per-frame compact lists of only the wall/object entries that actually
// matter for the current view - see tdngPrepareVisibleWalls()/
// tdngPrepareVisibleObjects()'s own comments for why these exist. Both
// the per-column wall search (up to 24 entries) and the per-column NWO
// search (up to 3 distances x 11 objects x 3 offsets = 99 combos) used
// to be rescanned in full for every one of the 96 columns, regardless of
// how few of them are ever relevant to the current player position/
// direction - typically only a small handful. Built once per
// tdngRenderImage() call, then every column just walks the short list.
int tdngMatchedWallCount;
int[24] tdngMatchedWallIdx;

int tdngVisObjCount;
int[99] tdngVisObjDistance;
int[99] tdngVisObjN;
int[99] tdngVisObjOffset;
int[99] tdngVisObjWidth;
int[99] tdngVisObjOnWall;
int[99] tdngVisObjLR;

// state machine
#define TDNG_STATE_FADE_IN 0
#define TDNG_STATE_PLAYING 1
#define TDNG_STATE_ACTION_WAIT 2
#define TDNG_STATE_TELEPORT_FLASH 3
#define TDNG_STATE_COMBAT_PRESTRIKE_FLASH 4
#define TDNG_STATE_COMBAT_PLAYER_ATTACK_GATE 5
#define TDNG_STATE_COMBAT_RETALIATE_WAIT 6
#define TDNG_STATE_COMBAT_RETALIATE_FLASH 7
#define TDNG_STATE_DEATH_FADEOUT 8
#define TDNG_STATE_GAMEOVER_WAIT 9

#define TDNG_FADE_IN_FRAMES 10
#define TDNG_ACTION_WAIT_FRAMES 12
#define TDNG_TELEPORT_FLASH_FRAMES 12
#define TDNG_PRESTRIKE_FLASH_FRAMES 15
#define TDNG_RETALIATE_WAIT_FRAMES 30
#define TDNG_RETALIATE_FLASH_FRAMES 15
#define TDNG_DEATH_FADEOUT_FRAMES 16

int tdngState;
int tdngStateTimer;
int tdngCombatMonsterIdx;
int tdngCombatCellIdx;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

bool tdngIsPlayerAlive()
{
    return( tdngPlayerHP > 0 );
}

// D(maxValue) - returns a value in [0, maxValue] inclusive, matching
// upstream's own intent (a dice roll including the max face). Upstream's
// actual mechanism (an AVR hardware timer register used as an entropy
// source, folded into range via repeated subtraction) has no real
// equivalent on Vircon32 - substituted with the shared arand() helper
// every other port in this project already uses for RNG replacement.
int tdngGetDice( int maxValue )
{
    return( arand( maxValue + 1 ) );
}

void tdngLimitPosition()
{
    tdngPlayerX = tdngPlayerX & 15;
    tdngPlayerY = tdngPlayerY & 15;
}

// Returns the level-array index of the cell 'distance' away, offset
// 'offsetLR' to the left/right, in direction 'orientation', from
// (x, y) - wraps around at the 16x16 level boundary. Ported from
// getCellRaw(), but returns a plain index instead of a raw pointer (see
// this file's own header comment on avoiding pointer arithmetic here).
int tdngGetCellIndex( int x, int y, int distance, int offsetLR, int orientation )
{
    if( orientation == TDNG_NORTH )
    {
        y = y - distance;
        x = x + offsetLR;
    }
    else if( orientation == TDNG_SOUTH )
    {
        y = y + distance;
        x = x - offsetLR;
    }
    else if( orientation == TDNG_EAST )
    {
        x = x + distance;
        y = y + offsetLR;
    }
    else // TDNG_WEST
    {
        x = x - distance;
        y = y - offsetLR;
    }

    x = x & 15;
    y = y & 15;

    return( y * TDNG_LEVEL_WIDTH + x );
}

// lightingOffset + viewDistance can exceed the 16-entry table during the
// initial fade-in (see this file's own header comment) - clamped here,
// the one place every lighting lookup funnels through.
int tdngGetLightingMask( int viewDistance )
{
    int idx = tdngLightingOffset + viewDistance;
    if( idx > 15 ) idx = 15;
    if( idx < 0 ) idx = 0;
    return( tdngLightingTable[ idx ] );
}

int tdngFindMonster( int position )
{
    int n;
    for( n = 0; n < TDNG_MAX_MONSTERS; n++ )
    {
        if( tdngMonsterStats[ n * 6 + 0 ] == position ) return( n );
    }
    return( -1 ); // should never happen given level design - avoids an
                   // infinite loop rather than replicating upstream's own
                   // (compiled-out on real hardware) unbounded search
}

void tdngOpenChest( int interactionIdx )
{
    int newItem = tdngInteractionData[ interactionIdx * 6 + 3 ];
    int i;

    tdngPlayerItems = tdngPlayerItems | newItem;

    if( newItem == TDNG_ITEM_AMULET )
    {
        for( i = 0; i < TDNG_MAX_LEVEL_BYTES; i++ )
        {
            if( tdngCurrentLevel[ i ] == TDNG_FAKE_WALL ) tdngCurrentLevel[ i ] = TDNG_EMPTY;
        }
    }
}

void tdngPlayerInteraction( int cellIdx, int cellValue )
{
    int n;
    for( n = 0; n < TDNG_INTERACTION_COUNT; n++ )
    {
        int base = n * 6;
        int currentPos = tdngInteractionData[ base + 0 ];
        int currentStatus = tdngInteractionData[ base + 1 ];
        int nextStatus = tdngInteractionData[ base + 2 ];
        int newItem = tdngInteractionData[ base + 3 ];
        int modifiedPos = tdngInteractionData[ base + 4 ];
        int modifiedPosCellValue = tdngInteractionData[ base + 5 ];

        if( currentPos == cellIdx )
        {
            if( ( cellValue & TDNG_OBJECT_MASK ) == currentStatus )
            {
                int modifyCurrentPosition = 1;
                int modifyTargetPosition = 1;

                if( currentStatus == TDNG_CLOSED_CHEST )
                {
                    tdngOpenChest( n );
                }
                else if( currentStatus == TDNG_DOOR )
                {
                    if( tdngPlayerItems & TDNG_ITEM_KEY )
                    {
                        tdngCurrentLevel[ cellIdx ] = TDNG_EMPTY;
                        tdngPlayerItems = tdngPlayerItems & ( ~TDNG_ITEM_KEY );
                    }
                    else
                    {
                        modifyCurrentPosition = 0;
                        modifyTargetPosition = 0;
                    }
                }
                else
                {
                    tdngPlayerItems = tdngPlayerItems | newItem;
                }

                if( modifyCurrentPosition )
                {
                    tdngCurrentLevel[ cellIdx ] = ( cellValue - currentStatus ) | nextStatus;
                }
                if( modifyTargetPosition )
                {
                    tdngCurrentLevel[ modifiedPos ] = modifiedPosCellValue;
                }

                // perform only the first matching action, matching upstream
                return;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

// Per-(object,distance) constant scan parameters for
// tdngGetDownScaledBitmapData() below, computed once per drawn object per
// column (via tdngPrepareScaledBitmap()) instead of being recomputed on
// every one of its 16 calls per column (8 rows x mask+bitmap) the way a
// direct port of getDownScaledBitmapData() would - none of these actually
// depend on x/y/useMask, only on which object/distance is being drawn.
int* tdngSbBitmapArray;
int tdngSbBitmapWidth;
int tdngSbHeightBytes;
int tdngSbStartBitNo;
int tdngSbScaleFactor;
int tdngSbSquaredScaleFactor;
int tdngSbThreshold;
int tdngSbStartOffsetY;
int tdngSbEndOffsetY;
int tdngSbBitMask;

void tdngPrepareScaledBitmap( int objIdx, int distance )
{
    int base = objIdx * 8;
    tdngSbBitmapWidth = tdngObjectList[ base + 1 ];
    tdngSbStartBitNo = tdngObjectList[ base + 2 ];
    tdngSbHeightBytes = tdngObjectList[ base + 3 ];
    int thr0 = tdngObjectList[ base + 4 ];
    int thr1 = tdngObjectList[ base + 5 ];
    int thr2 = tdngObjectList[ base + 6 ];
    tdngSbBitmapArray = tdngResolveBitmapArray( tdngObjectList[ base + 7 ] );

    tdngSbScaleFactor = tdngScalingFactorFromDistance[ distance ];
    tdngSbSquaredScaleFactor = tdngSbScaleFactor * tdngSbScaleFactor;

    if( distance == 1 ) tdngSbThreshold = thr0;
    else if( distance == 2 ) tdngSbThreshold = thr1;
    else tdngSbThreshold = thr2;

    tdngSbStartOffsetY = tdngVerticalStartOffset[ distance ];
    tdngSbEndOffsetY = tdngVerticalEndOffset[ distance ];
    tdngSbBitMask = tdngBitMaskFromScalingFactor[ tdngSbScaleFactor ];
}

int tdngSbResultBitmap;
int tdngSbResultMask;

// Computes BOTH the downscaled bitmap byte AND its mask byte at (x,y) for
// whichever object tdngPrepareScaledBitmap() was last called for, in one
// pass (results left in tdngSbResultBitmap/tdngSbResultMask) - ported
// from two separate calls to getDownScaledBitmapData() (once per
// useMask value), merged here since both scans walk the exact same bit
// range and only differ in which half of the source array they read
// from - halves the outer 8-iteration bit loop's own setup cost (the
// dominant per-column cost once an object/sprite is actually visible,
// per direct user testing) instead of paying it twice.
void tdngGetDownScaledBitmapPair( int x, int y )
{
    if( ( y >= tdngSbStartOffsetY ) && ( y <= tdngSbEndOffsetY ) )
    {
        int startBitNo = tdngSbStartBitNo;
        int endBitNo = startBitNo + tdngSbHeightBytes * 8;
        int bitNo = ( y - tdngSbStartOffsetY ) * 8 * tdngSbScaleFactor;
        int maskDataOffset = tdngSbHeightBytes * tdngSbBitmapWidth;
        int bitValue;

        tdngSbResultBitmap = 0;
        tdngSbResultMask = 0;

        for( bitValue = 1; bitValue <= 128; bitValue = bitValue << 1 )
        {
            int bitSumBitmap = 0;
            int bitSumMask = tdngSbSquaredScaleFactor;

            if( ( bitNo >= startBitNo ) && ( bitNo < endBitNo ) )
            {
                int row = ( bitNo - startBitNo ) / 8;
                int dataIdxBitmap = x * tdngSbScaleFactor * tdngSbHeightBytes + row;
                int dataIdxMask = maskDataOffset + dataIdxBitmap;
                int shift = bitNo & 7;
                int col;

                bitSumMask = 0;
                for( col = 0; col < tdngSbScaleFactor; col++ )
                {
                    int byteValBitmap = tdngSbBitmapArray[ dataIdxBitmap ];
                    bitSumBitmap = bitSumBitmap + tdngNibbleBitCount[ ( byteValBitmap >> shift ) & tdngSbBitMask ];
                    int byteValMask = tdngSbBitmapArray[ dataIdxMask ];
                    bitSumMask = bitSumMask + tdngNibbleBitCount[ ( byteValMask >> shift ) & tdngSbBitMask ];
                    dataIdxBitmap = dataIdxBitmap + tdngSbHeightBytes;
                    dataIdxMask = dataIdxMask + tdngSbHeightBytes;
                }
            }

            bitNo = bitNo + tdngSbScaleFactor;

            if( bitSumBitmap >= tdngSbThreshold ) tdngSbResultBitmap = tdngSbResultBitmap | bitValue;
            if( bitSumMask >= tdngSbThreshold ) tdngSbResultMask = tdngSbResultMask | bitValue;
        }
    }
    else
    {
        tdngSbResultBitmap = 0;
        tdngSbResultMask = 255;
    }
}

// Per-frame precompute: which of the 24 wall-info entries actually
// reference a real wall cell, as a short compact list instead of a
// 24-entry lookup table - built once per tdngRenderImage() call, not
// once per column. Preserves the original array's relative order (so a
// column's own search still finds the CLOSEST matching wall first,
// matching upstream exactly), just skips entries that can never match
// any column this frame.
void tdngPrepareVisibleWalls()
{
    tdngMatchedWallCount = 0;

    int wallIdx;
    for( wallIdx = 0; wallIdx < TDNG_WALLINFO_COUNT; wallIdx++ )
    {
        int base = wallIdx * 8;
        int wb = tdngWallInfo[ base + 0 ];
        if( wb == -1 ) continue;

        int viewDistance = tdngWallInfo[ base + 4 ];
        int leftRightOffset = tdngWallInfo[ base + 5 ];
        int cellIdx = tdngGetCellIndex( tdngPlayerX, tdngPlayerY, viewDistance, leftRightOffset, tdngDir );
        int cellVal = tdngCurrentLevel[ cellIdx ];

        if( ( cellVal & TDNG_WALL_MASK ) == TDNG_WALL_MASK )
        {
            tdngMatchedWallIdx[ tdngMatchedWallCount ] = wallIdx;
            tdngMatchedWallCount = tdngMatchedWallCount + 1;
        }
    }
}

// Per-frame precompute: which (distance, object, leftRightOffset) combos
// have a real, visible-from-this-direction match in the dungeon, as a
// short compact list instead of a 99-entry lookup table - same reasoning
// as tdngPrepareVisibleWalls() above, just for the much larger NWO search
// space. Built in distance-descending order (matching the "draw back to
// front" order tdngRenderDungeonColumn()'s own NWO loop needs), so the
// per-column loop can just walk this list directly instead of iterating
// distance/object/offset itself.
void tdngPrepareVisibleObjects()
{
    tdngVisObjCount = 0;

    int distance;
    for( distance = 3; distance >= 1; distance = distance - 1 )
    {
        int n;
        for( n = 0; n < TDNG_OBJECT_COUNT; n++ )
        {
            int objBase = n * 8;
            int itemType = tdngObjectList[ objBase + 0 ];
            int bitmapWidth = tdngObjectList[ objBase + 1 ];
            int objectWidth = bitmapWidth >> distance;
            int offsetsBase = distance * 3;

            int leftRightOffset;
            for( leftRightOffset = -1; leftRightOffset <= 1; leftRightOffset = leftRightOffset + 1 )
            {
                int cellIdx = tdngGetCellIndex( tdngPlayerX, tdngPlayerY, distance, leftRightOffset, tdngDir );
                int cellVal = tdngCurrentLevel[ cellIdx ];

                if( ( cellVal & TDNG_OBJECT_MASK ) == itemType )
                {
                    if( cellVal & TDNG_FLAG_LIMITED_VISIBILITY )
                    {
                        if( ( cellVal & TDNG_LIMITED_VISIBILITY_MASK ) == ( tdngDir & TDNG_LIMITED_VISIBILITY_MASK ) )
                        {
                            // matches upstream: stop checking further
                            // offsets for this (distance, object) combo
                            break;
                        }
                    }

                    int offset = tdngObjectCenterPositions[ offsetsBase + ( leftRightOffset + 1 ) ];

                    tdngVisObjDistance[ tdngVisObjCount ] = distance;
                    tdngVisObjN[ tdngVisObjCount ] = n;
                    tdngVisObjOffset[ tdngVisObjCount ] = offset;
                    tdngVisObjWidth[ tdngVisObjCount ] = objectWidth;
                    tdngVisObjLR[ tdngVisObjCount ] = leftRightOffset;
                    if( cellVal & TDNG_WALL_MASK ) tdngVisObjOnWall[ tdngVisObjCount ] = 1;
                    else tdngVisObjOnWall[ tdngVisObjCount ] = 0;
                    tdngVisObjCount = tdngVisObjCount + 1;
                }
            }
        }
    }
}

// Processes one vertical column of the dungeon view (0..95). Ported from
// renderDungeonColumn()'s active (`#else`) branch - see this file's own
// header comment for why the disabled `#if 0` branch (a fuller 0-7 view
// distance model) doesn't matter here.
void tdngRenderDungeonColumn( int x )
{
    int y;
    for( y = 0; y < 8; y++ ) tdngLineBuffer[ y ] = 0;

    int mirror = ( tdngPlayerX + tdngPlayerY + tdngDir ) & 1;
    int maxObjectDistance = TDNG_MAX_VIEW_DISTANCE;
    int lastWallViewDistance = 0;

    int wi;
    for( wi = 0; wi < tdngMatchedWallCount; wi++ )
    {
        int wallIdx = tdngMatchedWallIdx[ wi ];
        int base = wallIdx * 8;
        int wb = tdngWallInfo[ base + 0 ];
        int startX = tdngWallInfo[ base + 1 ];
        int endX = tdngWallInfo[ base + 2 ];

        if( ( x >= startX ) && ( x <= endX ) )
        {
            int posStartEndY = tdngWallInfo[ base + 3 ];
            int viewDistance = tdngWallInfo[ base + 4 ];
            int relPos = tdngWallInfo[ base + 6 ];
            int width = tdngWallInfo[ base + 7 ];

            int startPosY = posStartEndY >> 4;
            int endPosY = posStartEndY & 15;
            int sizeY = endPosY - startPosY + 1;
            int posX = x - startX;
            int offsetX;
            if( mirror ) offsetX = width - 1 - posX - relPos;
            else offsetX = posX + relPos;

            int* wallArray = tdngResolveWallArray( wb );
            int bitmapBase = offsetX * sizeY;
            int row;
            for( row = 0; row < sizeY; row++ )
            {
                tdngLineBuffer[ startPosY + row ] = wallArray[ bitmapBase + row ];
            }

            lastWallViewDistance = viewDistance;
            maxObjectDistance = viewDistance;
            break;
        }
    }

    // Upstream harmlessly reads a few garbage-but-not-crashing PROGMEM
    // bytes past the 4/12-entry scaling tables when no wall at all is
    // found in a column's line of sight (maxObjectDistance stays at
    // MAX_VIEW_DISTANCE == 7) - a real out-of-bounds read here instead,
    // clamped centrally (see this file's own header comment).
    if( maxObjectDistance > 3 ) maxObjectDistance = 3;

    // apply background shading + dungeon floor
    for( y = 0; y < 8; y++ )
    {
        int pixels = tdngLineBuffer[ y ];
        pixels = pixels & tdngGetLightingMask( lastWallViewDistance * 2 + ( x & 1 ) );
        if( ( y >= 5 ) && ( pixels == 0 ) )
        {
            int floorX;
            if( mirror ) floorX = 96 - x; else floorX = x;
            // Upstream harmlessly overruns dungeonFloor's PROGMEM table by
            // one 3-byte column on real hardware (x==0 with mirror active
            // gives floorX==96, one past the last valid column 0-95) - a
            // real out-of-bounds read here instead, clamped the same way
            // as this file's other latent-upstream-bound fixes.
            if( floorX > 95 ) floorX = 95;
            pixels = pixels | tdngDungeonFloor[ floorX * 3 + ( y - 5 ) ];
        }
        tdngLineBuffer[ y ] = pixels;
    }

    // draw Non Wall Objects, back to front - tdngVisObjCount is typically
    // a small handful (or zero) rather than the full 99 combos, and the
    // list is already ordered distance-descending to match this loop's
    // required "farthest first" compositing order.
    int lastDrawnDistance = -1;
    int lastDrawnN = -1;
    int oi;
    for( oi = 0; oi < tdngVisObjCount; oi++ )
    {
        int distance = tdngVisObjDistance[ oi ];
        if( distance > maxObjectDistance ) continue;

        int n = tdngVisObjN[ oi ];
        // matches upstream's break-after-draw: only the first matching
        // offset for a given (distance, object) is drawn per column
        if( ( distance == lastDrawnDistance ) && ( n == lastDrawnN ) ) continue;

        int offset = tdngVisObjOffset[ oi ];
        int objectWidth = tdngVisObjWidth[ oi ];

        if( ( x >= offset - objectWidth ) && ( x < offset + objectWidth ) )
        {
            if( ( lastWallViewDistance == distance ) && !tdngVisObjOnWall[ oi ] )
            {
                if( tdngVisObjLR[ oi ] == 0 ) return;
            }

            tdngPrepareScaledBitmap( n, distance );

            // Outside [tdngSbStartOffsetY, tdngSbEndOffsetY],
            // tdngGetDownScaledBitmapData() always returns mask=255/
            // bitmap=0 - a guaranteed no-op against tdngLineBuffer (an
            // AND with 255 changes nothing, an OR with 0 changes
            // nothing) - so those rows are skipped entirely instead of
            // paying a full call's overhead per row for a known-nothing
            // result. This range is often much narrower than all 8 rows
            // (e.g. just 2 of 8 for a distant object), the same
            // "self-gated call still costs a full call" lesson this
            // project has hit repeatedly in other ports.
            for( y = tdngSbStartOffsetY; y <= tdngSbEndOffsetY; y++ )
            {
                int posX = x - offset + objectWidth;
                tdngGetDownScaledBitmapPair( posX, y );
                int mask = tdngSbResultMask;
                tdngLineBuffer[ y ] = tdngLineBuffer[ y ] & mask;
                int scaledBitmap = tdngSbResultBitmap;
                scaledBitmap = scaledBitmap & tdngGetLightingMask( lastWallViewDistance );
                if( distance == 1 )
                {
                    scaledBitmap = scaledBitmap ^ ( tdngInvertMonsterEffect & ( ~mask ) );
                }
                tdngLineBuffer[ y ] = tdngLineBuffer[ y ] | scaledBitmap;
            }
            lastDrawnDistance = distance;
            lastDrawnN = n;
        }
    }
}

// Draws the whole 128-column screen (96 dungeon view + 32 dashboard).
// Ported from renderImage() - but does NOT clear invertMonsterEffect/
// invertStatusEffect at the end the way upstream does (see this file's
// own header comment on why those flags are owned by the state machine
// instead, now that every engine frame redraws unconditionally).
void tdngRenderImage()
{
    int x, y;

    // Same class of bug already found and fixed in Tiny DDug: every
    // render function must clear the frame itself before drawing -
    // md_drawColumn() skips its own draw call whenever a column's byte
    // is 0, relying on md_beginFrame()'s clear_screen() to have already
    // blanked the frame. Missing this call leaves whatever the menu (or
    // a previous game) last drew on screen, stacking new content on top
    // instead of a clean frame.
    md_beginFrame();

    // Precompute once per frame (not once per column) - see each
    // function's own comment for why this was the dominant CPU cost.
    tdngPrepareVisibleWalls();
    tdngPrepareVisibleObjects();

    for( x = 0; x < TDNG_WINDOW_SIZE_X; x++ )
    {
        tdngRenderDungeonColumn( x );
        for( y = 0; y < 8; y++ )
        {
            md_drawColumn( x, y, tdngLineBuffer[ y ] ^ tdngDisplayXorEffect );
        }
    }

    int statusPanelOffset = 0;
    int alive = tdngIsPlayerAlive();

    for( x = 0; x < TDNG_DASHBOARD_SIZE_X; x++ )
    {
        for( y = 0; y < 8; y++ )
        {
            int pixels = 0;

            if( y || ( tdngPlayerItems & TDNG_ITEM_COMPASS ) )
            {
                pixels = tdngStatusPanelVertical[ statusPanelOffset ];
                if( !y )
                {
                    if( ( x >= TDNG_COMPASS_START_X ) && ( x < TDNG_COMPASS_START_X + TDNG_COMPASS_SIZE_X ) )
                    {
                        pixels = pixels | tdngCompass[ x - TDNG_COMPASS_START_X + TDNG_COMPASS_SIZE_X * tdngDir ];
                    }
                }
            }
            statusPanelOffset = statusPanelOffset + 1;

            if( ( x > 0 ) && ( x < TDNG_DASHBOARD_SIZE_X - 1 ) )
            {
                if( y == TDNG_HIT_POINTS_ROW )
                {
                    if( ( x - 2 ) > ( tdngPlayerHP / 2 ) ) pixels = 0;
                    pixels = pixels ^ tdngInvertStatusEffect;
                }
                if( y == TDNG_ITEMS_ROW )
                {
                    if( x < TDNG_ITEM_LAST_POS_X )
                    {
                        if( x >= TDNG_ITEM_KEY_POS_X )
                        {
                            if( !( tdngPlayerItems & TDNG_ITEM_KEY ) ) pixels = 0;
                        }
                        else if( x >= TDNG_ITEM_RING_POS_X )
                        {
                            if( !( tdngPlayerItems & TDNG_ITEM_RING ) ) pixels = 0;
                        }
                        else if( x >= TDNG_ITEM_AMULET_POS_X )
                        {
                            if( !( tdngPlayerItems & TDNG_ITEM_AMULET ) ) pixels = 0;
                        }
                        else if( x >= TDNG_ITEM_SHIELD_POS_X )
                        {
                            if( !( tdngPlayerItems & TDNG_ITEM_SHIELD ) ) pixels = 0;
                        }
                        else if( x >= TDNG_ITEM_SWORD_POS_X )
                        {
                            if( !( tdngPlayerItems & TDNG_ITEM_SWORD ) ) pixels = 0;
                        }
                    }
                }
                if( y >= TDNG_VICTORY_ROW )
                {
                    if( !( tdngPlayerItems & TDNG_ITEM_VICTORY ) ) pixels = 0;
                }
            }

            if( !alive )
            {
                if( y >= TDNG_SKELETON_ROW )
                {
                    if( ( x >= 1 ) && ( x < 29 ) )
                    {
                        int offsetXY = ( y - TDNG_SKELETON_ROW ) + ( 28 - x ) * 5;
                        pixels = pixels & tdngJoey[ offsetXY + 140 ];
                        pixels = pixels | tdngJoey[ offsetXY ];
                    }
                }
            }

            md_drawColumn( TDNG_WINDOW_SIZE_X + x, y, pixels );
        }
    }

}

// -----------------------------------------------------------------------------
// Game state machine
// -----------------------------------------------------------------------------

void tdngStartNewGame()
{
    int i;

    for( i = 0; i < TDNG_MAX_LEVEL_BYTES; i++ ) tdngCurrentLevel[ i ] = tdngLevel1Init[ i ];
    for( i = 0; i < 60; i++ ) tdngMonsterStats[ i ] = tdngMonsterStatsInit[ i ];

    tdngPlayerX = 1;
    tdngPlayerY = 1;
    tdngDir = TDNG_EAST;
    tdngPlayerHP = 12;
    tdngPlayerDamage = 3;
    tdngPlayerArmour = 0;
    tdngPlayerItems = 0;
    tdngDisplayXorEffect = 0;
    tdngInvertMonsterEffect = 0;
    tdngInvertStatusEffect = 0;

    for( i = 0; i < TDNG_MAX_MONSTERS; i++ )
    {
        int pos = tdngMonsterStats[ i * 6 + 0 ];
        int type = tdngMonsterStats[ i * 6 + 1 ];
        tdngCurrentLevel[ pos ] = type;
    }

    tdngLightingOffset = 16 - ( TDNG_MAX_VIEW_DISTANCE / 2 ) * 2;

    tdngState = TDNG_STATE_FADE_IN;
    tdngStateTimer = TDNG_FADE_IN_FRAMES;
}

// Begins the potion-check + short pause every completed player turn ends
// with (matches upstream's own once-per-turn potion check + _delay_ms(200),
// called right after checkPlayerMovement() returns).
void tdngBeginActionWait()
{
    if( tdngPlayerItems & TDNG_ITEM_POTION )
    {
        tdngPlayerHP = tdngPlayerHP + TDNG_POTION_HITPOINT_BONUS + tdngGetDice( 8 );
        tdngPlayerItems = tdngPlayerItems - TDNG_ITEM_POTION;
    }
    tdngState = TDNG_STATE_ACTION_WAIT;
    tdngStateTimer = TDNG_ACTION_WAIT_FRAMES;
}

void tdngBeginPlayerAttack()
{
    tdngMonsterStats[ tdngCombatMonsterIdx * 6 + 2 ] = tdngMonsterStats[ tdngCombatMonsterIdx * 6 + 2 ] - ( tdngGetDice( 7 ) + tdngPlayerDamage );
    tdngInvertMonsterEffect = 255;
    tdngState = TDNG_STATE_COMBAT_PLAYER_ATTACK_GATE;
}

void gameTinyDungeon_init()
{
    InitTinyJoypad();
    tdngStartNewGame();
}

void gameTinyDungeon_update()
{
    // matches upstream's own once-per-outer-loop-iteration check
    if( tdngPlayerItems & TDNG_ITEM_SWORD ) tdngPlayerDamage = 10;
    if( tdngPlayerItems & TDNG_ITEM_SHIELD ) tdngPlayerArmour = 3;

    if( tdngState == TDNG_STATE_FADE_IN )
    {
        if( tdngLightingOffset > 0 )
        {
            tdngLightingOffset = tdngLightingOffset - 1;
        }
        else
        {
            tdngState = TDNG_STATE_PLAYING;
        }
    }
    else if( tdngState == TDNG_STATE_PLAYING )
    {
        int frontCellIdx = tdngGetCellIndex( tdngPlayerX, tdngPlayerY, 1, 0, tdngDir );
        int playerHasReachedNewCell = 0;
        int playerAction = 0;

        if( isLeftPressed() )
        {
            tdngDir = ( tdngDir + 3 ) & 3;
            playerAction = 1;
        }
        if( isRightPressed() )
        {
            tdngDir = ( tdngDir + 1 ) & 3;
            playerAction = 1;
        }
        if( isUpPressed() )
        {
            if( !( tdngCurrentLevel[ frontCellIdx ] & TDNG_FLAG_SOLID ) )
            {
                if( tdngDir == TDNG_NORTH ) tdngPlayerY = tdngPlayerY - 1;
                else if( tdngDir == TDNG_EAST ) tdngPlayerX = tdngPlayerX + 1;
                else if( tdngDir == TDNG_SOUTH ) tdngPlayerY = tdngPlayerY + 1;
                else tdngPlayerX = tdngPlayerX - 1;
                playerHasReachedNewCell = 1;
            }
        }
        if( isDownPressed() )
        {
            int backCellIdx = tdngGetCellIndex( tdngPlayerX, tdngPlayerY, -1, 0, tdngDir );
            if( !( tdngCurrentLevel[ backCellIdx ] & TDNG_FLAG_SOLID ) )
            {
                if( tdngDir == TDNG_NORTH ) tdngPlayerY = tdngPlayerY + 1;
                else if( tdngDir == TDNG_EAST ) tdngPlayerX = tdngPlayerX - 1;
                else if( tdngDir == TDNG_SOUTH ) tdngPlayerY = tdngPlayerY - 1;
                else tdngPlayerX = tdngPlayerX + 1;
                playerHasReachedNewCell = 1;
            }
        }

        tdngLimitPosition();

        if( playerHasReachedNewCell )
        {
            int pos = tdngPlayerX + tdngPlayerY * TDNG_LEVEL_WIDTH;
            int n;
            for( n = 0; n < TDNG_SPECIALFX_COUNT; n++ )
            {
                int base = n * 4;
                if( tdngSpecialCellFX[ base + 1 ] == pos )
                {
                    if( tdngSpecialCellFX[ base + 0 ] == TDNG_TELEPORTER )
                    {
                        tdngPlayerX = tdngSpecialCellFX[ base + 2 ];
                        tdngPlayerY = tdngSpecialCellFX[ base + 3 ];
                    }
                    else
                    {
                        tdngDir = ( tdngDir + tdngSpecialCellFX[ base + 2 ] ) & 3;
                    }
                    if( tdngPlayerItems & TDNG_ITEM_RING )
                    {
                        tdngDisplayXorEffect = 255;
                    }
                }
            }

            if( tdngDisplayXorEffect )
            {
                tdngState = TDNG_STATE_TELEPORT_FLASH;
                tdngStateTimer = TDNG_TELEPORT_FLASH_FRAMES;
            }
            else
            {
                tdngBeginActionWait();
            }
        }
        else if( isFirePressed() )
        {
            int cellValue = tdngCurrentLevel[ frontCellIdx ];

            if( cellValue & TDNG_FLAG_MONSTER )
            {
                tdngCombatCellIdx = frontCellIdx;
                tdngCombatMonsterIdx = tdngFindMonster( frontCellIdx );

                if( tdngMonsterStats[ tdngCombatMonsterIdx * 6 + 4 ] )
                {
                    // monster attacks first
                    int damage = tdngGetDice( 7 ) + tdngMonsterStats[ tdngCombatMonsterIdx * 6 + 3 ] - tdngPlayerArmour;
                    if( damage > 0 )
                    {
                        tdngPlayerHP = tdngPlayerHP - damage;
                        tdngInvertStatusEffect = 255;
                    }
                    tdngState = TDNG_STATE_COMBAT_PRESTRIKE_FLASH;
                    tdngStateTimer = TDNG_PRESTRIKE_FLASH_FRAMES;
                }
                else
                {
                    tdngBeginPlayerAttack();
                }
            }
            else
            {
                tdngPlayerInteraction( frontCellIdx, cellValue );
                tdngBeginActionWait();
            }
        }
        else if( playerAction )
        {
            // Turning (Left/Right) alone still counts as a completed turn
            // upstream (playerAction=true exits checkPlayerMovement(),
            // which is followed by the outer loop's own _delay_ms(200)) -
            // missing this branch meant a held turn button re-processed
            // every real 60fps frame instead of pacing at ~5 turns/sec
            // like every other action here, the exact "moves too fast"
            // bug reported via live play.
            tdngBeginActionWait();
        }
    }
    else if( tdngState == TDNG_STATE_ACTION_WAIT )
    {
        tdngStateTimer = tdngStateTimer - 1;
        if( tdngStateTimer <= 0 ) tdngState = TDNG_STATE_PLAYING;
    }
    else if( tdngState == TDNG_STATE_TELEPORT_FLASH )
    {
        tdngStateTimer = tdngStateTimer - 1;
        if( tdngStateTimer <= 0 )
        {
            tdngDisplayXorEffect = 0;
            tdngBeginActionWait();
        }
    }
    else if( tdngState == TDNG_STATE_COMBAT_PRESTRIKE_FLASH )
    {
        tdngStateTimer = tdngStateTimer - 1;
        if( tdngStateTimer <= 0 )
        {
            tdngInvertStatusEffect = 0;
            if( !tdngIsPlayerAlive() )
            {
                tdngState = TDNG_STATE_DEATH_FADEOUT;
                tdngStateTimer = TDNG_DEATH_FADEOUT_FRAMES;
            }
            else
            {
                tdngBeginPlayerAttack();
            }
        }
    }
    else if( tdngState == TDNG_STATE_COMBAT_PLAYER_ATTACK_GATE )
    {
        if( !isFirePressed() )
        {
            tdngInvertMonsterEffect = 0;

            if( tdngMonsterStats[ tdngCombatMonsterIdx * 6 + 2 ] <= 0 )
            {
                tdngCurrentLevel[ tdngCombatCellIdx ] = TDNG_EMPTY;
                tdngPlayerItems = tdngPlayerItems | tdngMonsterStats[ tdngCombatMonsterIdx * 6 + 5 ];
                tdngBeginActionWait();
            }
            else if( tdngMonsterStats[ tdngCombatMonsterIdx * 6 + 4 ] )
            {
                // already attacked first, no further retaliation this turn
                tdngBeginActionWait();
            }
            else
            {
                tdngState = TDNG_STATE_COMBAT_RETALIATE_WAIT;
                tdngStateTimer = TDNG_RETALIATE_WAIT_FRAMES;
            }
        }
    }
    else if( tdngState == TDNG_STATE_COMBAT_RETALIATE_WAIT )
    {
        tdngStateTimer = tdngStateTimer - 1;
        if( tdngStateTimer <= 0 )
        {
            int damage = tdngGetDice( 7 ) + tdngMonsterStats[ tdngCombatMonsterIdx * 6 + 3 ] - tdngPlayerArmour;
            if( damage > 0 )
            {
                tdngPlayerHP = tdngPlayerHP - damage;
                tdngInvertStatusEffect = 255;
            }
            tdngState = TDNG_STATE_COMBAT_RETALIATE_FLASH;
            tdngStateTimer = TDNG_RETALIATE_FLASH_FRAMES;
        }
    }
    else if( tdngState == TDNG_STATE_COMBAT_RETALIATE_FLASH )
    {
        tdngStateTimer = tdngStateTimer - 1;
        if( tdngStateTimer <= 0 )
        {
            tdngInvertStatusEffect = 0;
            if( !tdngIsPlayerAlive() )
            {
                tdngState = TDNG_STATE_DEATH_FADEOUT;
                tdngStateTimer = TDNG_DEATH_FADEOUT_FRAMES;
            }
            else
            {
                tdngBeginActionWait();
            }
        }
    }
    else if( tdngState == TDNG_STATE_DEATH_FADEOUT )
    {
        if( tdngLightingOffset < ( TDNG_MAX_VIEW_DISTANCE + 1 ) * 2 )
        {
            tdngLightingOffset = tdngLightingOffset + 1;
        }
        else
        {
            int i;
            for( i = 0; i < TDNG_MAX_LEVEL_BYTES; i++ ) tdngCurrentLevel[ i ] = TDNG_EMPTY;
            tdngPlayerHP = 0;
            tdngPlayerItems = 0;
            tdngInvertMonsterEffect = 0;
            tdngInvertStatusEffect = 0;
            tdngDisplayXorEffect = 0;
            tdngState = TDNG_STATE_GAMEOVER_WAIT;
        }
    }
    else if( tdngState == TDNG_STATE_GAMEOVER_WAIT )
    {
        if( isFirePressed() )
        {
            tdngStartNewGame();
        }
    }

    tdngRenderImage();
}

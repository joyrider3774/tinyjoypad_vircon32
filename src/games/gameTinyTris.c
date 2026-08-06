// =============================================================================
// Tiny Tris - ported from Daniel C's tiny-tris_v3.ino (tinyjoypad.com, GPLv3).
// Same tinyJoypadShim lineage as Invaders/Pinball/Pacman/Bomber/Doc/Bert
// (FastTinyDriver.h) - button reads reuse that shim as-is. A Tetris clone:
// a 12x19 grid, 7 tetromino shapes falling and rotating, full rows clear.
//
// Button mapping: same analogRead(A0)/(A3)+digitalRead(1) pattern as every
// other Daniel-C game here (thresholds confirmed in this game's own
// spritebank_TTRIS.h) - isLeftPressed()/isRightPressed() off A0,
// isDownPressed() off A3 (soft drop; no up needed), isFirePressed() off
// digitalRead(1) (rotate).
//
// Structural changes from upstream, applying every lesson from this
// session's earlier ports/optimization rounds *from the start* instead of
// needing a later pass:
//  - upstream's loop() is MENU:/goto-chain around a big while(1) -
//    rewritten as an explicit frame-stepped state machine, same approach
//    as every other tinyJoypadShim port here.
//  - `Tiny_Flip_TTRIS(uint8_t HR_TTRIS)` took a partial-redraw width
//    (82-of-128 columns most frames) - the *exact* "skip columns and rely
//    on real SSD1306 VRAM persistence" pattern already found and fixed in
//    Pinball/Doc - always redraws the full 128 columns here, avoided from
//    the start rather than re-discovered from a bug report.
//  - `DELETE_LINE_TTRIS()`'s line-clear flash (`FLASH_LINE_TTRIS()`, a
//    blocking 5-iteration paint-on/paint-off loop that draws+delays
//    itself) is a genuinely blocking multi-frame sequence upstream -
//    rewritten as an explicit `TRIS_STATE_LINE_FLASH` sub-state advancing
//    one half-step per real engine frame, same "blocking loop -> explicit
//    resumable state" treatment every port here has needed.
//  - grid/falling-piece/preview-piece rendering composites per *cell*
//    once per page row into a shared buffer, instead of the per-pixel
//    scan-every-cell shape upstream's `Recupe_TTRIS()`/`DropPiece_TTRIS()`/
//    `NEXT_BLOCK_TTRIS()` used (the same O(pixels x objects) cost already
//    found and fixed for Bomber/Pacman/Doc/Bert's sprite and grid
//    rendering) - built this way from the start rather than shipping the
//    naive version.
//  - dropped upstream's own `SKIP_FRAME`-gated "only redraw every 7th
//    logic tick" split and its real-time `_delay_us()`-based `Sound_TTRIS`
//    busy-wait tone loops (approximated with a handful of representative
//    `Sound()` calls) - both AVR performance compromises this project's
//    fast real-60fps model doesn't need, same reasoning as every other
//    port here.
//  - EEPROM-backed high-score persistence restored (see the project-wide
//    "Real persistent high-score saving" section in CLAUDE.md) - ported as
//    a single level/lines/score triple rather than upstream's own 4-slot
//    checksummed backup scheme, since that redundancy exists purely to
//    guard against real AVR EEPROM wear/corruption, a concern this port's
//    own memory-card-backed shim doesn't share (it already checksums the
//    whole slot on every load) - see `trisRecupeHighscore()`/
//    `trisCheckNewRecord()`'s own comments for the full reasoning.
//  - `PSEUDO_RND_TTRIS()` is upstream's own rotating 0-6 counter (not a
//    real `rand()` call), so none of this project's usual `arand()` range
//    fix applies here - ported as-is.
//  - the bit-packed grid storage (`Grid_TTRIS[12][3]`, 19 rows packed
//    8-per-byte per column) is kept faithful to upstream rather than
//    simplified to a plain per-cell array, consistent with how this
//    project has preserved other games' own bit-packed data structures
//    (e.g. Tiny Doc's 105-bit destructible-block tracker) rather than
//    second-guessing them.
// =============================================================================

// State machine constants/vars declared up front - Vircon32 has no forward
// declarations (single-pass compile), and several functions defined well
// before the "State machine" section below (trisDeleteLine, trisRecupeStart,
// trisFlipIntro) already need to reference trisState/trisIntroTimer1/etc.
#define TRIS_STATE_ATTRACT       0
#define TRIS_STATE_PLAYING       1
#define TRIS_STATE_LINE_FLASH    2
#define TRIS_STATE_GAMEOVER_WAIT 3

int trisState;
int trisIntroTimer1;
int trisIntroFrameCounter;
int trisFlashStep;
int trisWaitFrames;

// The attract screen is otherwise 100% static (background/chateau/score/
// lines/level never change there) - the only thing that ever changes is
// whether the START button box is shown, and that only flips a handful
// of times a second. Redrawing the whole screen every single frame
// regardless was wasted, constant work - this dirty flag makes the
// attract screen draw once when the box state actually changes, then
// hold that exact frame until it needs to flip again, instead of
// recomputing an identical frame 60 times a second.
bool trisAttractDirty = true;
int trisAttractBoxVisible;

// Dirty-flag cache for the locked-grid composite - the grid only actually
// changes when a piece locks in or a line clears, not on every frame a
// piece is merely falling, so recomputing it unconditionally every frame
// wastes work that scales with how full the board is (worst right before
// a game over) - same "dirty-flag caching for a mostly-static structure"
// fix already applied to Tiny Doc's pill/virus grid. Set true at every
// site that actually mutates trisGrid (found by grepping every
// trisChangeGridStat call site plus trisInitAllVar's own direct reset).
bool trisGridDirty = true;
int[1024] trisGridCache;

int[12][3] trisGrid;
int trisLevel;
int trisScores;
int trisNbOfLineF;
int trisLevelSpeedAdj;
int[3] trisNbOfLine;
int trisRndVar;
int trisLongPressX;
int trisDownDesactive;
int trisDropSpeed;
int trisSpeedXTrig;
int trisDropTrig;
int trisXx, trisYy;
int[5][5] trisPieceMat2;
int trisRippleFilter;
int trisPieces_;
int trisPiecesPreview;
int trisPiecesRot;
int trisDropBreak;
int trisOuSuisJeX;
int trisOuSuisJeY;
int trisOuSuisJeXEngaged;
int trisOuSuisJeYEngaged;
int trisDeplacementXx;
int trisDeplacementYy;
int trisRot;

// Loaded from/saved to EEPROM (see header comment and
// trisRecupeHighscore()/trisCheckNewRecord()) - not in-memory-only anymore.
int trisHighLevel;
int trisHighLines;
int trisHighScore;

// -----------------------------------------------------------------------------
//   spritebank_TTRIS.h data (programmatically extracted from upstream,
//   byte-for-byte, same technique used for every other game's data tables
//   this session to avoid manual-transcription slips)
// -----------------------------------------------------------------------------

int[16] trisMem =
{
0x00,0x02,0x00,0x04,0x03,0x07,0x06,0x09,0x09,0x0C,0x0B,0x0F,0x0E,0x11,0x11,0x13,
};

int[35] trisPieces =
{
0x00,0x20,0x70,0x00,0x00,0x00,0x00,0x30,0x30,0x00,0x00,0x00,0x30,0x60,0x00,0x00,0x10,0x30,0x20,0x00,
0x20,0x20,0x20,0x20,0x00,0x00,0x20,0x20,0x60,0x00,0x00,0x20,0x20,0x30,0x00,
};

int[4] trisPreviewBlock =
{
0x02,0x01,0xC0,0xC0,
};

int[5] trisTinyblock =
{
0x03,0x01,0x07,0x05,0x07,
};

int[5] trisTinyblock2 =
{
0x03,0x01,0xE0,0xE0,0xE0,
};

int[42] trisPolice =
{
0x04,0x01,0x1F,0x11,0x1F,0x00,0x00,0x1F,0x00,0x00,0x1D,0x15,0x17,0x00,0x11,0x15,0x1F,0x00,0x07,0x04,
0x1F,0x00,0x17,0x15,0x1D,0x00,0x1F,0x15,0x1D,0x00,0x01,0x1D,0x03,0x00,0x1F,0x15,0x1F,0x00,0x17,0x15,
0x1F,0x00,
};

int[32] trisStartButton1 =
{
0x1E,0x01,0xFE,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0xFE,
};

int[32] trisStartButton2 =
{
0x1E,0x01,0x03,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,
0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x03,
};

int[288] trisChateau =
{
0xD5,0xF8,0xF4,0xEA,0x54,0x7A,0x55,0x68,0x74,0x7A,0x54,0x6A,0x55,0x78,0x74,0x6A,0x54,0x7A,0x55,0x68,
0x74,0x7A,0x54,0x6A,0x55,0x78,0x74,0x6A,0x54,0x7A,0x55,0x68,0xF4,0xFA,0xD4,0xEA,0xFF,0x01,0x00,0x02,
0x02,0x82,0xFE,0x82,0x02,0x02,0x88,0xFA,0x80,0x00,0xF8,0x10,0x08,0x08,0xF0,0x00,0x18,0x60,0xC0,0x38,
0x88,0xA0,0xA8,0xA8,0x28,0x28,0x28,0x08,0x08,0x08,0x01,0xFF,0xFF,0x00,0x04,0x04,0x04,0x14,0x14,0x14,
0x54,0x54,0x50,0x50,0x44,0x04,0x04,0xFC,0x04,0x04,0x04,0xF0,0x22,0x12,0x01,0x10,0xF4,0x00,0x00,0x20,
0x50,0x50,0x90,0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,0x80,0x40,0x40,0x00,0x40,0x40,0xC0,
0x40,0x40,0x01,0x81,0x41,0x40,0x80,0x01,0x00,0xC0,0x40,0x41,0x81,0x01,0x40,0x41,0xC1,0x41,0x40,0x00,
0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,0x01,0x12,0x12,0x0C,0x00,0x00,0x1F,0x00,0x00,0x00,0x1F,
0x02,0x02,0x1F,0x00,0x00,0x1F,0x06,0x0A,0x11,0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,
0xFF,0x00,0x00,0x00,0xF0,0xB0,0x50,0x88,0x88,0xC8,0x50,0x50,0x20,0x20,0x60,0x60,0xB0,0x90,0x10,0x18,
0x08,0x98,0x98,0xA4,0xE4,0x46,0xE4,0xE4,0xE8,0xF8,0xF0,0xF0,0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
0x03,0x06,0x0D,0x1A,0x3F,0x6B,0xD6,0xAA,0x54,0xF8,0xFC,0xFC,0xFE,0xFE,0xAB,0x7F,0x7F,0x3F,0x3F,0x1F,
0x0F,0x0D,0x07,0x07,0x03,0x03,0x01,0x01,0x00,0x00,0x00,0xFF,0x1F,0x3C,0x38,0x38,0x30,0x30,0x30,0x30,
0x30,0x30,0x30,0x31,0x33,0x37,0x33,0x31,0x31,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,
0x30,0x30,0x30,0x30,0x38,0x38,0x3C,0x1F,
};

int[1024] trisBackground =
{
0xFC,0x02,0xF9,0xF5,0x1D,0x0D,0x0D,0x0D,0x0D,0x0B,0x07,0x1F,0x3F,0x20,0x2F,0x2F,0x3F,0x22,0x3F,0x20,
0x39,0x33,0x20,0x3F,0x20,0x2A,0x2E,0x3F,0x2D,0x2A,0x2A,0x36,0x1F,0x07,0x0B,0x0D,0x0D,0x0D,0x0D,0x1D,
0xF5,0xF9,0x02,0xFC,0xF8,0x00,0x40,0x00,0x40,0x40,0x00,0x40,0x40,0x00,0x40,0x40,0x00,0x40,0x40,0x00,
0x40,0x40,0x00,0x40,0x40,0x00,0x40,0x40,0x00,0x40,0x40,0x00,0x40,0x40,0x00,0x40,0x40,0x00,0x40,0x40,
0x00,0x40,0x00,0xF8,0xFC,0x02,0xF9,0xF5,0x1D,0x0D,0x0D,0x0B,0x07,0x1F,0x3F,0x2D,0x2A,0x2A,0x36,0x3F,
0x31,0x2E,0x2E,0x3F,0x31,0x2E,0x2E,0x31,0x3F,0x20,0x3A,0x32,0x2D,0x3F,0x20,0x2A,0x2E,0x3F,0x1F,0x07,
0x0B,0x0D,0x0D,0x1D,0xF5,0xF9,0x02,0xFC,0xFF,0x00,0x7F,0xBF,0xE0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,
0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,
0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xE0,0xBF,0x7F,0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,0x7F,0xBF,0xE0,0xC0,0xC0,0xC0,
0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,
0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xE0,0xBF,0x7F,0x00,0xFF,0x00,0x01,0xFA,0x5A,
0x7A,0x5A,0xDA,0x5A,0x7A,0x5A,0xDA,0x5A,0x7A,0x5A,0xDA,0xDA,0x7A,0x5A,0xDA,0x5A,0x7A,0x5A,0xDA,0x5A,
0x7A,0x5A,0xDA,0x5A,0x7A,0x5A,0xDA,0x5A,0x7A,0x5A,0xDA,0x5A,0x7A,0x5A,0xDA,0x5A,0x7A,0xFA,0x01,0x00,
0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,
0x00,0x01,0xFA,0xFA,0x5A,0x5A,0x5A,0x7A,0x5A,0x5A,0x5A,0x7A,0x5A,0x5A,0x5A,0x7A,0x5A,0x5A,0x5A,0x7A,
0x5A,0x5A,0x5A,0x7A,0xDA,0xDA,0x5A,0x7A,0x5A,0xDA,0x5A,0x7A,0x5A,0xDA,0x5A,0x7A,0x5A,0xDA,0x5A,0x7A,
0x5A,0xFA,0x01,0x00,0x00,0x00,0xFF,0x55,0x77,0x55,0xDD,0x55,0x77,0x55,0xDD,0xF5,0xFF,0x3F,0x07,0xE0,
0xFC,0xBC,0xE0,0x81,0x07,0x1D,0x7D,0xD5,0x77,0x55,0xDD,0x55,0x77,0x55,0xDD,0x55,0x77,0x55,0xDD,0x55,
0x77,0x55,0xDD,0x55,0x77,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0xFE,0xFD,0x07,0x03,0x03,0x03,0x03,0x03,
0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x07,0xFD,0xFE,0x00,0x01,0xFF,0xF7,0xD5,0xDD,0xD5,0xF7,
0xD5,0xDD,0xD5,0xF7,0xD5,0xDD,0xD5,0x77,0x55,0xFF,0x00,0x00,0x00,0x00,0xFF,0x55,0x77,0x55,0xDD,0xF5,
0x7F,0x3F,0x8F,0xC3,0xF0,0xDC,0xBF,0xF7,0xEF,0xFD,0xEA,0xD7,0xBC,0xF0,0x40,0x81,0x07,0x1D,0x75,0x3F,
0x87,0xF1,0x81,0x03,0x0F,0x35,0xDD,0x55,0x77,0x55,0xDD,0x55,0x77,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,
0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0x7E,0xC2,0x5E,0x7E,
0x42,0x52,0x5A,0x7E,0x62,0x5E,0x62,0x7E,0x42,0x52,0x5A,0x7E,0xC2,0x5E,0xFC,0x01,0x03,0xFF,0x00,0x00,
0x00,0x00,0xFF,0x55,0x77,0x55,0xDF,0x57,0x7C,0xF8,0xE1,0x04,0x2A,0xEE,0xDA,0x4E,0x0A,0x0A,0x0E,0x1A,
0xAE,0x0A,0x05,0x01,0xC0,0x60,0x78,0x6F,0x5F,0x7E,0x75,0x6F,0x58,0x60,0xC0,0x03,0x0F,0x75,0xDD,0x55,
0x77,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0x78,0x73,0xE5,0x67,0x66,0x66,0xE6,0x66,0x66,0x6E,0xFE,0xFE,0x0E,
0x06,0xC6,0x00,0xFF,0xFF,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x80,0xFF,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x0F,0x15,0x17,0x95,0x1D,0x15,0x97,0x1F,0x9F,0x00,
0x09,0xBF,0x3A,0x95,0x20,0x00,0x91,0x00,0xAA,0x00,0x00,0x80,0x02,0x85,0x37,0x2D,0x95,0x07,0x85,0x0D,
0x37,0x85,0x02,0x80,0x18,0x1C,0x9F,0x15,0x17,0x0F,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x0F,0x17,0x15,0x9D,0x15,0x17,
0x95,0x1D,0x95,0x17,0x15,0x7F,0x3F,0x80,0x00,0x1D,0x94,0x19,0x9A,0x13,0x13,0x93,0x13,0x93,0x13,0x17,
0x7F,0x3F,0x87,0x03,0x13,0x93,0x13,0x93,0x13,0x12,0x91,0x10,0x18,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,
0x06,0x0F,0x0D,0x1F,0x1D,0x16,0x1F,0x1D,0x1F,0x1D,0x36,0x3F,0x3D,0x7F,0x7D,0x76,0xFF,0xFD,0xFF,0xFD,
0x76,0x7F,0x7D,0x3F,0x3D,0x36,0x1F,0x1D,0x1F,0x1D,0x16,0x1F,0x1D,0x0F,0x0D,0x06,0x00,0x00,0x00,0x00,
0xFF,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0xFF,
0x00,0x00,0x00,0x00,0x06,0x0F,0x0D,0x1F,0x1D,0x16,0x1F,0x1D,0x1F,0x1D,0x36,0x3F,0x3D,0x7F,0x7D,0x76,
0xFF,0xFD,0xFF,0xFD,0x76,0x7F,0x7D,0x3F,0x3D,0x36,0x1F,0x1D,0x1F,0x1D,0x16,0x1F,0x1D,0x0F,0x0D,0x06,
0x00,0x00,0x00,0x00,
};

// -----------------------------------------------------------------------------
//   Generic sprite-blit primitives (same shape as every other game's own
//   ELECTROLIB-style helpers this session)
// -----------------------------------------------------------------------------

int trisRecupeLineY( int valeur )
{
    return valeur >> 3;
}

int trisRecupeDecalageY( int valeur )
{
    return valeur - ( ( valeur >> 3 ) << 3 );
}

int trisSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown )
      return ( input << decalage ) & 0xFF;
    return input >> ( 8 - decalage );
}

int trisBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[ 0 ];
    int hSprite = sprites[ 1 ];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = trisRecupeLineY( yPos );

    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos ||
        recupeLineY > yPass || ( recupeLineY + hSprite ) < yPass )
      return 0x00;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = trisRecupeDecalageY( yPos );
    int scanA = ( xPass - xPos ) + ( spriteYLine * wSprite ) + 2;
    int scanB = ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;

    int outByte;
    if( scanA > wMax )
      outByte = 0x00;
    else
      outByte = trisSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = trisSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

int trisMap( int x, int inMin, int inMax, int outMin, int outMax )
{
    return ( x - inMin ) * ( outMax - outMin ) / ( inMax - inMin ) + outMin;
}

// -----------------------------------------------------------------------------
//   Sound (simplified two heavy sweep cases - see header comment)
//
// Every multi-call branch here used to fire synchronously, no real time
// between calls - md_playTone() (which Sound() calls into) has no queue,
// so only the very last call of each burst was ever audible. Fixed with a
// small non-blocking byte-pair sequencer (same shape as gameTinyDoc.c's/
// gameTinyTrick.c's own fix) - still calls the shared
// Sound(freqByte,durByte) directly. This file has no whole-function tick
// divisor of its own (ships at full real 60fps rate - see CLAUDE.md's own
// frame-pacing history for Tris), so wait-frame counts are computed
// against 60.
// -----------------------------------------------------------------------------

#define TRIS_SND_MAX_NOTES 4
int[TRIS_SND_MAX_NOTES] trisSndFreqBytes;
int[TRIS_SND_MAX_NOTES] trisSndDurBytes;
int trisSndLen;
int trisSndPos;
int trisSndWaitFrames;

void trisAdvanceSfx()
{
    if( trisSndPos >= trisSndLen )
      return;

    if( trisSndWaitFrames > 0 )
    {
        trisSndWaitFrames--;
        return;
    }

    int freqByte = trisSndFreqBytes[ trisSndPos ];
    int durByte = trisSndDurBytes[ trisSndPos ];
    Sound( freqByte, durByte );

    int periodUs = 255 - freqByte;
    if( periodUs < 1 )
      periodUs = 1;
    float durationSeconds = (float)( durByte * 2 * periodUs ) / 1000000.0;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 )
      waitFrames = 1;
    trisSndWaitFrames = waitFrames;

    trisSndPos++;
}

void trisSndSet3( int f0, int d0, int f1, int d1, int f2, int d2 )
{
    trisSndFreqBytes[ 0 ] = f0; trisSndDurBytes[ 0 ] = d0;
    trisSndFreqBytes[ 1 ] = f1; trisSndDurBytes[ 1 ] = d1;
    trisSndFreqBytes[ 2 ] = f2; trisSndDurBytes[ 2 ] = d2;
    trisSndLen = 3; trisSndPos = 0; trisSndWaitFrames = 0;
}

void trisSndSet4( int f0, int d0, int f1, int d1, int f2, int d2, int f3, int d3 )
{
    trisSndFreqBytes[ 0 ] = f0; trisSndDurBytes[ 0 ] = d0;
    trisSndFreqBytes[ 1 ] = f1; trisSndDurBytes[ 1 ] = d1;
    trisSndFreqBytes[ 2 ] = f2; trisSndDurBytes[ 2 ] = d2;
    trisSndFreqBytes[ 3 ] = f3; trisSndDurBytes[ 3 ] = d3;
    trisSndLen = 4; trisSndPos = 0; trisSndWaitFrames = 0;
}

void trisSndSet2( int f0, int d0, int f1, int d1 )
{
    trisSndFreqBytes[ 0 ] = f0; trisSndDurBytes[ 0 ] = d0;
    trisSndFreqBytes[ 1 ] = f1; trisSndDurBytes[ 1 ] = d1;
    trisSndLen = 2; trisSndPos = 0; trisSndWaitFrames = 0;
}

void trisSndTtris( int snd )
{
    if( snd == 0 ) trisSndSet3( 3, 5, 10, 10, 3, 5 );
    else if( snd == 1 ) Sound( 3, 2 );
    else if( snd == 2 ) trisSndSet4( 40, 80, 150, 80, 40, 80, 150, 80 );
    else if( snd == 3 ) trisSndSet4( 180, 6, 90, 12, 60, 6, 30, 12 );
    else if( snd == 4 ) trisSndSet2( 20, 150, 100, 150 );
    else if( snd == 5 ) trisSndSet3( 40, 1, 120, 1, 200, 1 );
}

// -----------------------------------------------------------------------------
//   Bit-packed grid (12 columns x 19 rows, 8 rows packed per byte, 3 bytes
//   per column) - kept faithful to upstream's own bit-packed storage, see
//   header comment.
// -----------------------------------------------------------------------------

int trisGridStat( int xScan, int yScan )
{
    if( yScan < 0 ) return 0;
    if( xScan < 0 || xScan > 11 ) return 1;
    if( yScan > 18 ) return 1;
    int yVarSelect = yScan >> 3;
    int yVarDecalage = trisRecupeDecalageY( yScan );
    int compByteDecalage = 0x80 >> yVarDecalage;
    if( ( compByteDecalage & trisGrid[ xScan ][ yVarSelect ] ) == 0 ) return 0;
    return 1;
}

void trisChangeGridStat( int xScan, int yScan, int value )
{
    if( xScan < 0 || xScan > 11 ) return;
    if( yScan < 0 || yScan > 18 ) return;
    trisGridDirty = true;
    int yVarSelect = yScan >> 3;
    int yVarDecalage = trisRecupeDecalageY( yScan );
    int compByteDecalage = 0x80 >> yVarDecalage;
    if( value )
      trisGrid[ xScan ][ yVarSelect ] = compByteDecalage | trisGrid[ xScan ][ yVarSelect ];
    else
      trisGrid[ xScan ][ yVarSelect ] = ( 0xFF - compByteDecalage ) & trisGrid[ xScan ][ yVarSelect ];
}

// -----------------------------------------------------------------------------
//   Piece selection / rotation
// -----------------------------------------------------------------------------

int trisScanPieceMatrix( int xMat, int yMat )
{
    int result = ( 0x80 >> xMat ) & trisPieces[ yMat ];
    if( result ) return 1;
    return 0;
}

void trisRotateMatrix( int rot )
{
    int x, y, a = 0, b = 0;
    for( y = 0; y < 5; y++ )
      for( x = 0; x < 5; x++ )
      {
          if( rot == 0 ) { a = x; b = y; }
          else if( rot == 1 ) { a = 4 - y; b = x; }
          else if( rot == 2 ) { a = 4 - x; b = 4 - y; }
          else if( rot == 3 ) { a = y; b = 4 - x; }
          trisPieceMat2[ a ][ b ] = trisScanPieceMatrix( x, y + ( trisPieces_ * 5 ) );
      }
}

void trisSelectPiece( int piece )
{
    trisPieces_ = piece;
    if( piece == 0 ) trisPiecesRot = 3;
    else if( piece == 1 ) trisPiecesRot = 0;
    else if( piece == 2 || piece == 3 || piece == 4 ) trisPiecesRot = 1;
    else if( piece == 5 || piece == 6 ) trisPiecesRot = 3;
    else trisPiecesRot = 0;
}

int trisPseudoRnd()
{
    if( trisRndVar < 6 ) trisRndVar++;
    else trisRndVar = 0;
    return trisRndVar;
}

void trisSetupNewPreviewPiece()
{
    trisPiecesPreview = trisPseudoRnd();
    trisSelectPiece( trisPieces_ );
    trisRot = 0;
    trisRotateMatrix( trisRot );
}

// -----------------------------------------------------------------------------
//   Collision / position tracking
// -----------------------------------------------------------------------------

void trisOuSuisJe( int xx_, int yy_ )
{
    int xxT = ( xx_ + 9 ) - 46;
    int yyT = ( yy_ + 9 ) - 5;
    trisOuSuisJeX = ( xxT / 3 ) - 3;
    if( ( xxT % 3 ) != 0 ) trisOuSuisJeXEngaged = 1;
    else trisOuSuisJeXEngaged = 0;
    trisOuSuisJeY = ( yyT / 3 ) - 3;
    if( yyT != ( ( trisOuSuisJeY + 3 ) * 3 ) ) trisOuSuisJeYEngaged = 1;
    else trisOuSuisJeYEngaged = 0;
}

int trisCheckCollisionX( int xAxe )
{
    int x, y;
    for( y = 0; y < 5; y++ )
      for( x = 0; x < 5; x++ )
        if( trisPieceMat2[ x ][ y ] == 1 )
          if( trisGridStat( ( x + trisOuSuisJeX ) + xAxe, y + trisOuSuisJeY ) )
            return 1;
    return 0;
}

int trisCheckCollisionY( int yAxe )
{
    int x, y;
    for( y = 0; y < 5; y++ )
      for( x = 0; x < 5; x++ )
        if( trisPieceMat2[ x ][ y ] == 1 )
          if( trisGridStat( x + trisOuSuisJeX, ( y + trisOuSuisJeY ) + yAxe ) )
            return 1;
    return 0;
}

int trisCheckIfRotOk()
{
    int memRot = trisRot;
    trisOuSuisJe( trisXx, trisYy );
    if( trisRot < trisPiecesRot ) trisRot++;
    else trisRot = 0;
    trisRotateMatrix( trisRot );

    if( trisCheckCollisionX( trisOuSuisJeXEngaged ) || trisCheckCollisionY( trisOuSuisJeYEngaged ) )
    {
        trisRot = memRot;
        trisRotateMatrix( trisRot );
        return 1;
    }
    trisSndTtris( 0 );
    return 0;
}

// -----------------------------------------------------------------------------
//   Score / level display data
// -----------------------------------------------------------------------------

void trisConvertNbOfLine()
{
    trisNbOfLine[ 2 ] = trisNbOfLineF / 100;
    trisNbOfLine[ 1 ] = ( trisNbOfLineF - ( trisNbOfLine[ 2 ] * 100 ) ) / 10;
    trisNbOfLine[ 0 ] = trisNbOfLineF - ( trisNbOfLine[ 2 ] * 100 ) - ( trisNbOfLine[ 1 ] * 10 );
}

int trisCalculOfScore( int tmp )
{
    if( tmp == 1 ) return 2;
    if( tmp == 2 ) return 5;
    if( tmp == 3 ) return 8;
    if( tmp == 4 ) return 12;
    return 0;
}

void trisGamePlay()
{
    int levelTmp = trisNbOfLineF / 20;
    if( trisLevel != levelTmp ) { trisLevel = levelTmp; trisSndTtris( 2 ); }
    if( trisLevel < 21 ) trisLevelSpeedAdj = trisMap( trisLevel, 0, 20, 11, 1 );
}

int trisEndPlay()
{
    int t;
    for( t = 0; t < 12; t++ )
      if( trisGridStat( t, 1 ) == 1 ) return 1;
    return 0;
}

// Upstream's own recupe_HIGHSCORE_TTRIS() stores the level/lines/score
// triple redundantly across 4 backup slots (addr 1-9, 11-19, 21-29, 31-39),
// using whichever copy's own checksum still passes as a guard against real
// AVR EEPROM wear/corruption - a concern this shim's own memory-card
// backing store doesn't have (the shim already checksums the whole slot on
// every load, see eepromShim.c). Ported as a single copy (addr 0=level,
// 1-2=lines, 3-4=score) relying on that already-existing whole-slot
// checksum for the same corruption-safety guarantee upstream's own 4x
// redundancy provided - the same "preserve behavior, not a hardware-
// specific implementation quirk" precedent already established throughout
// this project.
void trisRecupeHighscore()
{
    trisHighLevel = eeprom_read_byte( 0 );
    trisHighLines = eeprom_read_word( 1 );
    trisHighScore = eeprom_read_word( 3 );

    // A never-written slot reads back as all-0xFF cells (real AVR EEPROM's
    // own erased state) - 255 is never a real level, so it doubles as this
    // slot's own "never saved" sentinel. Upstream has no equivalent guard
    // (its own 4-slot checksum scheme happens to already reject an all-0xFF
    // copy on its own), but skipping this here would read a nonsense
    // level/lines/score triple as if it were a real earlier session.
    if( trisHighLevel == 255 )
    {
        trisHighLevel = 0;
        trisHighLines = 0;
        trisHighScore = 0;
    }
}

void trisCheckNewRecord()
{
    if( trisScores > trisHighScore )
    {
        trisHighScore = trisScores;
        trisHighLevel = trisLevel;
        trisHighLines = trisNbOfLineF;

        eeprom_write_byte( 0, trisHighLevel );
        eeprom_write_word( 1, trisHighLines );
        eeprom_write_word( 3, trisHighScore );
    }
}

void trisResetScore()
{
    int x;
    for( x = 0; x < 3; x++ ) trisNbOfLine[ x ] = 0;
    trisLevel = 0;
    trisScores = 0;
    trisNbOfLineF = 0;
}

void trisResetValue()
{
    trisLevel = 0;
    trisNbOfLineF = 0;
    trisScores = 0;
}

// -----------------------------------------------------------------------------
//   Line clear
// -----------------------------------------------------------------------------

int[19] trisLineMem;

void trisPaintLine( int visible )
{
    int loop, scanLine;
    for( loop = 0; loop < 19; loop++ )
      if( trisLineMem[ loop ] == 1 )
        for( scanLine = 0; scanLine < 12; scanLine++ )
          trisChangeGridStat( scanLine, loop, visible );
}

void trisCleanGrid()
{
    int grid2 = 18, grid1 = 18;
    int x;
    while( true )
    {
        if( trisLineMem[ grid1 ] == 1 )
        {
            grid2 = grid1;
            if( grid2 > 0 ) grid2--;
            break;
        }
        if( grid1 > 0 ) grid1--;
    }
    while( true )
    {
        while( true )
        {
            if( trisLineMem[ grid2 ] == 1 ) { if( grid2 > 0 ) grid2--; }
            else break;
        }
        for( x = 0; x < 12; x++ )
        {
            int src = 0;
            if( grid2 > 0 ) src = trisGridStat( x, grid2 );
            trisChangeGridStat( x, grid1, src );
        }
        if( grid1 > 0 ) grid1--;
        if( grid2 > 0 ) grid2--;
        if( grid1 == 0 ) break;
    }
}

void trisDeleteLine()
{
    int loop, scanLine, addsBlock;
    bool okDelete = false;
    int nbOfLineTemp = 0;
    for( loop = 0; loop < 19; loop++ ) trisLineMem[ loop ] = 0;

    for( loop = 0; loop < 19; loop++ )
    {
        addsBlock = 1;
        for( scanLine = 0; scanLine < 12; scanLine++ )
          if( trisGridStat( scanLine, loop ) == 0 ) addsBlock = 0;
        if( addsBlock )
        {
            trisLineMem[ loop ] = 1;
            okDelete = true;
        }
    }

    if( okDelete )
    {
        trisState = TRIS_STATE_LINE_FLASH;
        trisFlashStep = 0;
    }
    else
    {
        for( loop = 0; loop < 19; loop++ )
          if( trisLineMem[ loop ] == 1 ) nbOfLineTemp++;
        trisNbOfLineF = trisNbOfLineF + nbOfLineTemp;
        trisScores = trisScores + trisCalculOfScore( nbOfLineTemp );
    }
}

void trisFinishLineClear()
{
    trisCleanGrid();

    int loop, nbOfLineTemp = 0;
    for( loop = 0; loop < 19; loop++ )
      if( trisLineMem[ loop ] == 1 ) nbOfLineTemp++;
    trisNbOfLineF = trisNbOfLineF + nbOfLineTemp;
    trisScores = trisScores + trisCalculOfScore( nbOfLineTemp );

    // Must run *after* trisNbOfLineF is updated above, not before - it
    // refreshes the on-screen LINES digits (trisNbOfLine[]) from
    // trisNbOfLineF, so calling it first left the display permanently one
    // clear-event behind the real count.
    trisConvertNbOfLine();
}

// -----------------------------------------------------------------------------
//   Movement / drop
// -----------------------------------------------------------------------------

void trisMovePiece()
{
    trisOuSuisJe( trisXx, trisYy );
    if( trisOuSuisJeXEngaged == 0 )
      if( trisCheckCollisionX( trisDeplacementXx ) )
        trisDeplacementXx = 0;

    if( trisDeplacementXx == 1 ) trisXx++;
    if( trisDeplacementXx == -1 ) trisXx--;
    trisOuSuisJe( trisXx, trisYy );
    if( trisOuSuisJeXEngaged == 0 ) trisDeplacementXx = 0;

    if( trisCheckCollisionY( trisDeplacementYy ) )
    {
        trisDeplacementYy = 0;
        trisLongPressX = 0;
        trisRippleFilter = 0;
        trisDropBreak = 6;
    }
    else
      trisDropBreak = 0;

    if( trisDropSpeed == 0 )
    {
        if( trisDeplacementYy == -1 ) trisYy--;
        if( trisDeplacementYy == 1 ) trisYy++;
    }
    trisOuSuisJe( trisXx, trisYy );
    if( trisOuSuisJeYEngaged == 0 ) trisDeplacementYy = 0;
}

void trisEndDrop()
{
    int x, y;
    trisDropBreak = 0;
    for( y = 0; y < 5; y++ )
      for( x = 0; x < 5; x++ )
        if( trisPieceMat2[ x ][ y ] == 1 )
          trisChangeGridStat( trisOuSuisJeX + x, trisOuSuisJeY + y, 1 );

    if( trisOuSuisJeY < 9 ) trisScores = trisScores + 2;
    else trisScores = trisScores + 1;
    trisYy = 0;
    trisXx = 0;
    trisDeleteLine();
}

void trisControle()
{
    if( trisOuSuisJeXEngaged == 0 )
    {
        if( trisSpeedXTrig == 0 )
        {
            if( isRightPressed() )
            {
                if( trisLongPressX == 0 ) trisSndTtris( 1 );
                if( trisLongPressX == 0 || trisLongPressX == 20 ) { trisDeplacementXx = 1; trisSpeedXTrig = 2; }
                if( trisLongPressX < 20 ) trisLongPressX++;
            }
            if( isLeftPressed() )
            {
                if( trisLongPressX == 0 ) trisSndTtris( 1 );
                if( trisLongPressX == 0 || trisLongPressX == 20 ) { trisDeplacementXx = -1; trisSpeedXTrig = 2; }
                if( trisLongPressX < 20 ) trisLongPressX++;
            }
        }
        else
        {
            if( trisSpeedXTrig > 0 ) trisSpeedXTrig--;
        }
    }
    if( !isRightPressed() && !isLeftPressed() )
    {
        trisLongPressX = 0;
        trisPseudoRnd();
    }

    if( !isFirePressed() )
      if( trisOuSuisJeXEngaged == 0 && trisOuSuisJeYEngaged == 0 )
        trisRippleFilter = 0;

    if( trisRippleFilter == 1 )
    {
        trisCheckIfRotOk();
        trisRippleFilter = 2;
    }

    if( trisOuSuisJeYEngaged == 0 )
    {
        trisDropTrig--;
        if( trisDropTrig == 0 ) { trisDeplacementYy = 1; trisDropTrig = trisLevelSpeedAdj; }
    }

    if( trisDropSpeed > 0 ) trisDropSpeed--;
    else trisDropSpeed = trisLevelSpeedAdj;

    if( isDownPressed() )
    {
        if( trisOuSuisJeXEngaged == 0 )
        {
            trisDeplacementXx = 0;
            trisLongPressX = 1;
        }
        trisPseudoRnd();
        if( trisDownDesactive == 0 ) { trisDeplacementYy = 1; trisDropSpeed = 0; }
    }
    else
      trisDownDesactive = 0;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int[128] trisPageBuffer;

// Composites the locked grid, the falling piece, and the preview piece
// once per page row (walking only actual filled cells) into the shared
// buffer, instead of upstream's per-pixel Recupe_TTRIS()/DropPiece_TTRIS()/
// NEXT_BLOCK_TTRIS() which re-scanned every candidate cell at every one of
// the 1024 pixels/frame - same restructuring as every other game's sprite/
// grid compositing this session.
void trisCompositeGridIntoBuffer( int y )
{
    int rowBase = y * 128;
    int cx;
    for( cx = 46; cx <= 81; cx++ )
      trisGridCache[ rowBase + cx ] = 0;

    int a = trisMem[ y * 2 ];
    int b = trisMem[ y * 2 + 1 ];
    int gy, gx, col;
    for( gy = a; gy < b; gy++ )
      for( gx = 0; gx < 12; gx++ )
      {
          if( trisGridStat( gx, gy ) != 1 ) continue;
          int xPos = 46 + ( gx * 3 );
          int yPos = 5 + ( gy * 3 );
          for( col = 0; col < 3; col++ )
          {
              int x = xPos + col;
              if( x < 46 || x > 81 ) continue;
              int v = trisBlitzSprite( xPos, yPos, x, y, 0, trisTinyblock );
              trisPageBuffer[ x ] = trisPageBuffer[ x ] | v;
              trisGridCache[ rowBase + x ] = trisGridCache[ rowBase + x ] | v;
          }
      }
}

void trisCompositeDropPieceIntoBuffer( int y )
{
    // Bounds-checked against the grid's own column range (46-81), not the
    // full screen width - upstream's DropPiece_TTRIS() is only ever
    // *reached* for x already in that range (Recupe_TTRIS()'s early-return
    // for x outside the grid returns before ever calling it), so it never
    // needed its own explicit range check. This composited version has no
    // such implicit protection, and trisXx/trisYy are briefly both 0 right
    // after a piece locks (trisEndDrop(), before the next piece's spawn
    // position is set) - without this check, the just-locked piece's
    // shape (trisPieceMat2 isn't cleared until the next piece spawns)
    // gets redrawn at x~0-12 during every frame of the line-clear flash,
    // a stray block-shaped artifact bleeding into the LINES HUD area.
    int gx, gy, col;
    for( gy = 0; gy < 5; gy++ )
      for( gx = 0; gx < 5; gx++ )
      {
          if( trisPieceMat2[ gx ][ gy ] != 1 ) continue;
          int xPos = trisXx + ( gx * 3 );
          int yPos = ( trisYy + ( gy * 3 ) ) - 5;
          int recupeLineY = trisRecupeLineY( yPos );
          if( y < recupeLineY || y > recupeLineY + 1 ) continue;
          for( col = 0; col < 3; col++ )
          {
              int x = xPos + col;
              if( x < 46 || x > 81 ) continue;
              trisPageBuffer[ x ] = trisPageBuffer[ x ] | trisBlitzSprite( xPos, yPos, x, y, 0, trisTinyblock2 );
          }
      }
}

void trisCompositeNextBlockIntoBuffer( int y )
{
    int xAdd = 0, yAdd = 0;
    if( trisPiecesPreview == 0 ) { xAdd = 1; yAdd = 1; }
    else if( trisPiecesPreview == 1 ) yAdd = -1;
    else if( trisPiecesPreview == 2 ) xAdd = 1;
    else if( trisPiecesPreview == 3 ) { }
    else if( trisPiecesPreview == 4 ) { xAdd = 1; yAdd = 1; }
    else if( trisPiecesPreview == 5 ) xAdd = 1;
    else if( trisPiecesPreview == 6 ) { }

    int gx, gy, col;
    for( gy = 0; gy < 5; gy++ )
      for( gx = 0; gx < 5; gx++ )
      {
          if( trisScanPieceMatrix( gx, gy + ( trisPiecesPreview * 5 ) ) != 1 ) continue;
          int xPos = 92 + ( gx * 2 ) + xAdd;
          int yPos = ( 27 + ( gy * 2 ) ) - 5 + yAdd;
          int recupeLineY = trisRecupeLineY( yPos );
          if( y < recupeLineY || y > recupeLineY + 1 ) continue;
          for( col = 0; col < 2; col++ )
          {
              int x = xPos + col;
              if( x < 90 || x > 127 ) continue;
              trisPageBuffer[ x ] = trisPageBuffer[ x ] | trisBlitzSprite( xPos, yPos, x, y, 0, trisPreviewBlock );
          }
      }
}

int trisRecupeScores( int x, int y )
{
    // Self-gated exactly like upstream's own recupe_SCORES_TTRIS (bounds
    // check before the digit math) - see header comment on the intro-
    // screen flicker bug this fixes: without this guard, both this
    // function's divisions/mods AND its 6 blitzSprite calls ran
    // unconditionally for all 1024 pixels/frame in the attract-screen
    // loop, blowing the 250,000-instruction/frame budget and truncating
    // the frame mid-draw - visible as heavy flicker between a fully-drawn
    // and a half-drawn frame.
    if( x < 95 || x > 119 || y > 1 ) return 0;
    int m10000 = trisScores / 10000;
    int m1000 = ( trisScores - ( m10000 * 10000 ) ) / 1000;
    int m100 = ( trisScores - ( m1000 * 1000 ) - ( m10000 * 10000 ) ) / 100;
    int m10 = ( trisScores - ( m100 * 100 ) - ( m1000 * 1000 ) - ( m10000 * 10000 ) ) / 10;
    int m1 = trisScores - ( m10 * 10 ) - ( m100 * 100 ) - ( m1000 * 1000 ) - ( m10000 * 10000 );
    return trisBlitzSprite( 95, 8, x, y, m10000, trisPolice ) |
           trisBlitzSprite( 99, 8, x, y, m1000, trisPolice ) |
           trisBlitzSprite( 103, 8, x, y, m100, trisPolice ) |
           trisBlitzSprite( 107, 8, x, y, m10, trisPolice ) |
           trisBlitzSprite( 111, 8, x, y, m1, trisPolice ) |
           trisBlitzSprite( 115, 8, x, y, 0, trisPolice );
}

int trisRecupeNbOfLine( int x, int y )
{
    if( x < 16 || x > 28 || y > 1 ) return 0;
    return trisBlitzSprite( 16, 8, x, y, trisNbOfLine[ 2 ], trisPolice ) |
           trisBlitzSprite( 20, 8, x, y, trisNbOfLine[ 1 ], trisPolice ) |
           trisBlitzSprite( 24, 8, x, y, trisNbOfLine[ 0 ], trisPolice );
}

int trisRecupeLevel( int x, int y )
{
    if( x < 109 || x > 118 || y != 5 ) return 0;
    return trisBlitzSprite( 109, 41, x, y, trisLevel / 10, trisPolice ) |
           trisBlitzSprite( 114, 41, x, y, trisLevel % 10, trisPolice );
}

int trisRecupeChateau( int x, int y )
{
    if( x < 46 || x > 81 ) return 0;
    return trisChateau[ ( x - 46 ) + ( y * 36 ) ];
}

int trisRecupeStart( int x, int y, int timer1 )
{
    if( timer1 <= 3 ) return 0;
    // Self-gated the same way the score/lines/level layers were fixed
    // above: upstream's own Recupe_Start_TTRIS has no position check at
    // all (only the timer check), so during the "button visible" half of
    // every blink cycle this was calling blitzSprite twice per pixel
    // across all 1024 pixels/frame with nothing but the timer gate - the
    // budget cost the user could still see as flicker specifically
    // correlated with the box being drawn.
    if( x < 49 || x > 78 || y < 3 || y > 5 ) return 0;
    return trisBlitzSprite( 49, 28, x, y, 0, trisStartButton1 ) |
           trisBlitzSprite( 49, 36, x, y, 0, trisStartButton2 );
}

void trisTinyFlip()
{
    md_beginFrame();
    int x, y;
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
          trisPageBuffer[ x ] = 0;

        if( trisGridDirty )
          trisCompositeGridIntoBuffer( y );
        else
        {
            int rowBase = y * 128;
            for( x = 46; x <= 81; x++ )
              trisPageBuffer[ x ] = trisPageBuffer[ x ] | trisGridCache[ rowBase + x ];
        }
        trisCompositeDropPieceIntoBuffer( y );
        trisCompositeNextBlockIntoBuffer( y );

        for( x = 0; x < 128; x++ )
        {
            // trisRecupeScores/NbOfLine/Level are self-gated (bounds-check
            // before any digit math, matching upstream's own
            // recupe_SCORES_TTRIS/etc.) so calling them unconditionally
            // here is cheap outside their small active window.
            int pixel = trisBackground[ x + ( y * 128 ) ] | trisPageBuffer[ x ] |
                        trisRecupeScores( x, y ) | trisRecupeNbOfLine( x, y ) | trisRecupeLevel( x, y );
            md_drawColumn( x, y, pixel );
        }
    }
    trisGridDirty = false;
}

void trisFlipIntro()
{
    md_beginFrame();
    int x, y;
    for( y = 0; y < 8; y++ )
      for( x = 0; x < 128; x++ )
      {
          int pixel = trisBackground[ x + ( y * 128 ) ] |
                      trisRecupeChateau( x, y ) |
                      trisRecupeStart( x, y, trisIntroTimer1 ) |
                      trisRecupeScores( x, y ) |
                      trisRecupeNbOfLine( x, y ) |
                      trisRecupeLevel( x, y );
          md_drawColumn( x, y, pixel );
      }
}

// -----------------------------------------------------------------------------
//   State machine (replaces loop()'s MENU:/goto-chain and its while(1)s)
// -----------------------------------------------------------------------------

void trisInitAllVar()
{
    int x, y;
    trisGridDirty = true;
    for( y = 0; y < 3; y++ )
      for( x = 0; x < 12; x++ )
        trisGrid[ x ][ y ] = 0;
    for( y = 0; y < 5; y++ )
      for( x = 0; x < 5; x++ )
        trisPieceMat2[ x ][ y ] = 0;
    trisLongPressX = 0;
    trisDownDesactive = 0;
    trisDropSpeed = 0;
    trisSpeedXTrig = 0;
    trisDropTrig = 1;
    trisXx = 0;
    trisYy = 0;
    trisRippleFilter = 0;
    trisPieces_ = 0;
    trisPiecesPreview = 0;
    trisPiecesRot = 0;
    trisDropBreak = 0;
    trisOuSuisJeX = 0;
    trisOuSuisJeY = 0;
    trisOuSuisJeXEngaged = 0;
    trisOuSuisJeYEngaged = 0;
    trisDeplacementXx = 0;
    trisDeplacementYy = 0;
}

void trisBeginLevelSetup()
{
    trisInitAllVar();
    trisGamePlay();
    trisOuSuisJe( trisXx, trisYy );
    trisSetupNewPreviewPiece();
    trisXx = 55;
    trisYy = 5;
}

void trisBeginAttract()
{
    trisRecupeHighscore();
    // Upstream's own recupe_HIGHSCORE_TTRIS() loads the saved backup
    // directly into Level_TTRIS/Nb_of_line_F_TTRIS/Scores_TTRIS - the same
    // live variables the normal in-game HUD already renders via
    // trisRecupeScores()/trisRecupeNbOfLine()/trisRecupeLevel() (all three
    // already called unconditionally from trisFlipIntro(), the attract
    // screen's own render function) - so on real hardware the attract
    // screen visibly shows the last saved best while idle, and only resets
    // to 0 once trisResetScore() runs at the start of a real game. This
    // port kept trisHighScore/trisHighLevel/trisHighLines as a separate
    // variable set (for a clearer load/compare/save story), but never
    // mirrored them back into the shared display variables - so the
    // attract screen loaded and re-saved the high score correctly, but
    // never actually showed it. Mirroring here matches upstream exactly.
    trisScores = trisHighScore;
    trisLevel = trisHighLevel;
    trisNbOfLineF = trisHighLines;
    trisConvertNbOfLine();
    trisIntroTimer1 = 0;
    trisIntroFrameCounter = 0;
    trisAttractBoxVisible = 0;
    trisAttractDirty = true;
    trisState = TRIS_STATE_ATTRACT;
}

void gameTinyTris_init()
{
    InitTinyJoypad();
    trisResetValue();
    trisBeginAttract();
}

// Called once when resuming from the quit-confirmation dialog (see
// menuGameList.c/portVircon32.c) - forces both of this game's own dirty
// flags back to true, since either one skipping its work could otherwise
// leave the dialog's pixels on screen instead of Tris's own content:
// trisAttractDirty gates the *entire* attract-screen redraw (the one
// place in this game a real frame can be skipped outright), and
// trisGridDirty gates only the locked-grid composite within an
// otherwise-unconditional gameplay redraw - harmless to force both
// regardless of which state Tris was actually paused in.
void gameTinyTris_forceRedraw()
{
    trisAttractDirty = true;
    trisGridDirty = true;
}

void gameTinyTris_update()
{
    trisAdvanceSfx();

    if( trisState == TRIS_STATE_ATTRACT )
    {
        trisPiecesPreview = trisPseudoRnd();
        if( isFirePressed() )
        {
            md_armInputFireGate();
            // Upstream's SND_TTRIS case 4 - fired right here (new-game
            // confirm chime) but never actually called anywhere in this
            // port; found via a project-wide missing-sound-cue audit.
            trisSndTtris( 4 );
            trisResetScore();
            trisBeginLevelSetup();
            trisState = TRIS_STATE_PLAYING;
            trisTinyFlip();
            return;
        }
        trisIntroFrameCounter++;
        if( trisIntroFrameCounter >= 4 )
        {
            trisIntroFrameCounter = 0;
            if( trisIntroTimer1 < 7 ) trisIntroTimer1++;
            else trisIntroTimer1 = 0;

            int newBoxVisible = 0;
            if( trisIntroTimer1 > 3 ) newBoxVisible = 1;
            if( newBoxVisible != trisAttractBoxVisible )
            {
                trisAttractBoxVisible = newBoxVisible;
                trisAttractDirty = true;
            }
        }
        if( trisAttractDirty )
        {
            trisFlipIntro();
            trisAttractDirty = false;
        }
        return;
    }

    if( trisState == TRIS_STATE_LINE_FLASH )
    {
        int visible = 0;
        if( ( trisFlashStep % 2 ) == 0 ) visible = 1;
        trisPaintLine( visible );
        trisTinyFlip();
        trisFlashStep++;
        if( trisFlashStep >= 10 )
        {
            trisSndTtris( 5 );
            trisFinishLineClear();
            if( trisEndPlay() )
            {
                trisSndTtris( 3 );
                trisCheckNewRecord();
                trisState = TRIS_STATE_GAMEOVER_WAIT;
                trisWaitFrames = 120; // ~2000ms at 60fps
                return;
            }
            trisYy = 2;
            trisXx = 55;
            trisPieces_ = trisPiecesPreview;
            trisSetupNewPreviewPiece();
            trisDownDesactive = 1;
            trisGamePlay();
            trisState = TRIS_STATE_PLAYING;
        }
        return;
    }

    if( trisState == TRIS_STATE_GAMEOVER_WAIT )
    {
        trisWaitFrames--;
        if( trisWaitFrames <= 0 )
          trisBeginAttract();
        trisTinyFlip();
        return;
    }

    // TRIS_STATE_PLAYING
    trisControle();

    if( trisDropBreak == 6 )
    {
        trisEndDrop();
        if( trisState == TRIS_STATE_LINE_FLASH )
        {
            trisTinyFlip();
            return;
        }
        if( trisEndPlay() )
        {
            trisTinyFlip();
            trisSndTtris( 3 );
            trisCheckNewRecord();
            trisState = TRIS_STATE_GAMEOVER_WAIT;
            trisWaitFrames = 120; // ~2000ms at 60fps
            return;
        }
        trisYy = 2;
        trisXx = 55;
        trisPieces_ = trisPiecesPreview;
        trisSetupNewPreviewPiece();
        trisDownDesactive = 1;
        trisGamePlay();
    }

    if( isFirePressed() && trisRippleFilter == 0 )
    {
        trisPseudoRnd();
        trisRippleFilter = 1;
    }

    trisMovePiece();
    trisTinyFlip();
}

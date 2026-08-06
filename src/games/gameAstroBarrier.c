// =============================================================================
// Astro Barrier (Sean Price, GitHub `SeanP2001`, GPLv3) - staged 2026-08-06
// during a wider search for uncatalogued ATtiny85/TinyJoypad games (see
// CLAUDE.md's own `more games/` catalog entry), ported next at direct user
// request. A shooting gallery: move left/right along the bottom of the
// screen and shoot 1-3 moving targets per level (17 levels total) before
// running out of a limited per-level bullet supply; unused bullets at the
// end of a level become bonus score.
//
// Uses the real `ssd1306xled` library's own `ssd1306_draw_bmp(x0,y0,x1,y1,
// bitmap)` (confirmed directly from the bundled `Libraries/ssd1306xled-
// master.zip` source, not guessed) - a genuinely different, and simpler,
// rendering model than every other AttinyArcade-family game ported in this
// project so far: `y0`/`y1` there are PAGE indices (not pixel rows or a
// rotated column-as-page scheme), and the bitmap data itself is streamed in
// plain row-major page order - the *exact* same "one byte = 8 vertical
// pixels of one column" model `md_drawColumn()` already handles, with no
// rotation or bit-shift trickery needed at all. Text similarly uses the
// library's own standard, non-rotated `ssd1306_string_font6x8()` - the same
// 95-char `font6x8`/`ssd1306xled_font6x8` table already extracted and
// verified for this project's own Oroboros/Run Dude Run/Dino Game ports
// (`orbFont` et al) - reused here directly (`barrFont`, copied from
// Oroboros's own already-proven extraction) rather than re-extracted from
// scratch, since it's confirmed the same table (same header comment
// crediting the same `ssd1306xled` bitbucket source).
//
// Button.cpp's own 3-analog-threshold-on-one-pin scheme (`LEFT`/`RIGHT` on
// A2, `A`/fire on A3) needed no new shim - `isLeftPressed()`/
// `isRightPressed()`/`isFirePressed()` from the already-proven
// `tinyJoypadShim` cover the whole input surface with a clean 1:1 mapping,
// unlike Falling Blocks'/Blocks Gold's own need for a deliberate control
// remap. Left/Right and Fire are both read as plain level checks every
// tick during play (no edge-detection), matching upstream's own
// `checkButtons()` exactly - it has no debounce beyond "don't fire again
// while a bullet's still in flight", so holding Fire refires automatically
// the instant the previous bullet expires, and holding Left/Right just
// keeps moving every tick - both are genuinely intended real-time-shooter
// behaviors, not omissions.
//
// **One deliberate, documented deviation from a literal port**: upstream's
// own `loop()` has no "press start" gate at all - it shows the title
// screen for a fixed `delay(2000)` (playing `gameStartup()`'s jingle) and
// then unconditionally begins level 1, looping this whole sequence forever
// with no player input required to (re)start. Every other game in this
// entire cartridge instead waits on its own attract screen for an explicit
// Fire press before starting - adding that same gate here (edge-detected
// `isFirePressed()`) was a deliberate choice for UX consistency with the
// rest of the menu, not an oversight, and is the only place this port's
// own control flow diverges from upstream's literal structure. Everything
// else (level intro screen, the 2-second holds between game states, the
// end-of-game New-High-Score/Game-Over/Game-Complete sequence) is ported
// faithfully, just as explicit frame-counted wait states instead of real
// blocking `delay()` calls - the standard "blocking loop -> resumable
// state" treatment every port in this project needs.
//
// Sound: `Sound.cpp`'s own `note(n,octave)` is a real ATtiny85 Timer1 CTC
// tone generator (David Johnson-Davies' well-known "Tiny Tune" design,
// credited directly in its own header comment), not a NOP-loop beep like
// several other AttinyArcade-family games in this project - so rather than
// the usual "no exact real-Hz equivalent" heuristic those needed, the exact
// frequency was derived and *numerically verified* against real musical
// pitches: `freq = F_CPU / (2^(11-octave) * scale[n%12])` with F_CPU
// assumed at 8MHz (`Clock=3` in the source's own `((F_CPU/1000000)==8) ?
// 3 : ...` ternary) - computing `note(0,4)` gives 261.51Hz (essentially
// exact middle C, 261.63Hz) and `note(9,4)` gives 440.14Hz (essentially
// exact concert-pitch A4, 440Hz), confirming both the 8MHz assumption and
// the formula itself rather than just trusting the derivation on paper.
// Each of the 5 named jingles (`gameStartup`/`levelComplete`/`highScore`/
// `gameComplete`/`gameOver`) is a short sequence of real notes (some
// including an explicit silent rest, e.g. `highScore()`'s own
// `note(0,4);delay(200);note(0,0);delay(100);...`) - ported as a small
// frame-stepped sequencer (`barrStartJingle`/`barrAdvanceJingle`, the same
// established pattern used throughout this project) rather than upstream's
// real blocking `note()`+`delay()` pairs, since `note()` itself doesn't
// block at all (it just reconfigures the timer and returns instantly) -
// the actual holding-a-pitch-for-N-ms behavior comes entirely from the
// *caller's* own `delay(n)` between `note()` calls, which maps directly
// onto this project's already-established sequencer shape.
//
// Levels (`Levels.h`'s own `level[18][13]` table - 17 real levels, index 0
// unused, matching upstream's own `for(levelNo=1;levelNo<=noOfLevels;...)`)
// and all 5 sprite bitmaps (`largeTarget`/`mediumTarget`/`smallTarget`/
// `player`/`bullet`, all standard row-major page-ordered data) were byte-
// diff-verified via a small Python script against the upstream source
// before ever being pasted in, matching this project's own standing
// discipline for transcribed data tables.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data
// -----------------------------------------------------------------------------

// Same 95-char font6x8/ssd1306xled_font6x8 table already extracted and
// proven for gameOroboros.c/gameRunDudeRun.c/gameDinoGame.c - reused
// directly rather than re-extracted, since it's confirmed the same source
// (both header comments credit the same ssd1306xled bitbucket library).
int[570] barrFont =
{
0,0,0,0,0,0,0,0,0,47,0,0,0,0,7,0,
7,0,0,20,127,20,127,20,0,36,42,127,42,18,0,98,
100,8,19,35,0,54,73,85,34,80,0,0,5,3,0,0,
0,0,28,34,65,0,0,0,65,34,28,0,0,20,8,62,
8,20,0,8,8,62,8,8,0,0,0,160,96,0,0,8,
8,8,8,8,0,0,96,96,0,0,0,32,16,8,4,2,
0,62,81,73,69,62,0,0,66,127,64,0,0,66,97,81,
73,70,0,33,65,69,75,49,0,24,20,18,127,16,0,39,
69,69,69,57,0,60,74,73,73,48,0,1,113,9,5,3,
0,54,73,73,73,54,0,6,73,73,41,30,0,0,54,54,
0,0,0,0,86,54,0,0,0,8,20,34,65,0,0,20,
20,20,20,20,0,0,65,34,20,8,0,2,1,81,9,6,
0,50,73,89,81,62,0,124,18,17,18,124,0,127,73,73,
73,54,0,62,65,65,65,34,0,127,65,65,34,28,0,127,
73,73,73,65,0,127,9,9,9,1,0,62,65,73,73,122,
0,127,8,8,8,127,0,0,65,127,65,0,0,32,64,65,
63,1,0,127,8,20,34,65,0,127,64,64,64,64,0,127,
2,12,2,127,0,127,4,8,16,127,0,62,65,65,65,62,
0,127,9,9,9,6,0,62,65,81,33,94,0,127,9,25,
41,70,0,70,73,73,73,49,0,1,1,127,1,1,0,63,
64,64,64,63,0,31,32,64,32,31,0,63,64,56,64,63,
0,99,20,8,20,99,0,7,8,112,8,7,0,97,81,73,
69,67,0,0,127,65,65,0,0,2,4,8,16,32,0,0,
65,65,127,0,0,4,2,1,2,4,0,64,64,64,64,64,
0,0,1,2,4,0,0,32,84,84,84,120,0,127,72,68,
68,56,0,56,68,68,68,32,0,56,68,68,72,127,0,56,
84,84,84,24,0,8,126,9,1,2,0,24,164,164,164,124,
0,127,8,4,4,120,0,0,68,125,64,0,0,64,128,132,
125,0,0,127,16,40,68,0,0,0,65,127,64,0,0,124,
4,24,4,120,0,124,8,4,4,120,0,56,68,68,68,56,
0,252,36,36,36,24,0,24,36,36,24,252,0,124,8,4,
4,8,0,72,84,84,84,32,0,4,63,68,64,32,0,60,
64,64,32,124,0,28,32,64,32,28,0,60,64,48,64,60,
0,68,40,16,40,68,0,28,160,160,160,124,0,68,100,84,
76,68,0,8,54,65,65,0,0,0,0,127,0,0,0,0,
65,65,54,8,0,8,4,8,16,8,
};

int barrFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return barrFont[ ( ch - 32 ) * 6 + col ];
}

int barrTextByteAt( int* text, int textLen, int startX, int x )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= textLen ) return 0;
    return barrFontByte( text[ charIdx ], rel % 6 );
}

// Sprites (row-major, one page-row per group, matching ssd1306_draw_bmp's
// own real streaming order) - Sprites.h's own comments confirm dimensions.
int[128] barrLargeTarget = // 32x32 (4 pages x 32 cols)
{
0x00, 0x00, 0x80, 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x84, 0xC6, 0xE2, 0xE2, 0xE3, 0xF1, 0xF1, 0xF1,
0xF1, 0xF1, 0xF1, 0xE3, 0xE2, 0xE2, 0xC6, 0x84, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00, 0x00,
0xF0, 0x1E, 0x03, 0x00, 0xE0, 0xFC, 0xFE, 0xFF, 0xFF, 0x3F, 0x0F, 0x07, 0xC3, 0xE3, 0xF1, 0xF1,
0xF1, 0xF1, 0xE3, 0xC3, 0x07, 0x0F, 0x3F, 0xFF, 0xFF, 0xFE, 0xFC, 0xE0, 0x00, 0x03, 0x1E, 0xF0,
0x0F, 0x78, 0xC0, 0x00, 0x07, 0x3F, 0x7F, 0xFF, 0xFF, 0xFC, 0xF0, 0xE0, 0xC3, 0xC7, 0x8F, 0x8F,
0x8F, 0x8F, 0xC7, 0xC3, 0xE0, 0xF0, 0xFC, 0xFF, 0xFF, 0x7F, 0x3F, 0x07, 0x00, 0xC0, 0x78, 0x0F,
0x00, 0x00, 0x01, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x21, 0x63, 0x47, 0x47, 0xC7, 0x8F, 0x8F, 0x8F,
0x8F, 0x8F, 0x8F, 0xC7, 0x47, 0x47, 0x63, 0x21, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00, 0x00,
};

int[32] barrMediumTarget = // 16x16 (2 pages x 16 cols)
{
0xF0, 0x0C, 0x02, 0xE2, 0x11, 0x09, 0x89, 0xC9, 0xC9, 0x89, 0x09, 0x11, 0xE2, 0x02, 0x0C, 0xF0,
0x0F, 0x30, 0x40, 0x47, 0x88, 0x90, 0x91, 0x93, 0x93, 0x91, 0x90, 0x88, 0x47, 0x40, 0x30, 0x0F,
};

int[8] barrSmallTarget = { 0x00, 0x3c, 0x42, 0x5a, 0x5a, 0x42, 0x3c, 0x00 }; // 8x8

int[8] barrPlayer = { 0xc0, 0xe0, 0xf0, 0xc0, 0xc0, 0xf0, 0xe0, 0xc0 }; // 8x8

int[2] barrBullet = { 0x3c, 0x3c }; // 2x8

// level[levelNo][0]=bullets, then 3x(size,speed,x0,y0) for up to 3 targets.
// Index 0 is unused (matches upstream's own for(levelNo=1;...) skip).
int[18][13] barrLevel =
{
{ 3,    32,3,0,1,    0,0,0,0,    0,0,0,0 },
{ 3,    32,3,0,1,    0,0,0,0,    0,0,0,0 },
{ 2,    32,5,0,1,    0,0,0,0,    0,0,0,0 },
{ 2,    32,10,0,1,   0,0,0,0,    0,0,0,0 },
{ 3,    16,3,0,1,    0,0,0,0,    0,0,0,0 },
{ 4,    32,10,0,1,   16,5,0,5,   0,0,0,0 },
{ 4,    16,5,0,1,    16,6,0,4,   0,0,0,0 },
{ 4,    16,5,0,1,    16,10,0,4,  0,0,0,0 },
{ 5,    16,5,0,1,    16,6,0,3,   16,4,0,5 },
{ 4,    16,7,0,1,    16,8,50,4,  0,0,0,0 },
{ 4,    16,10,0,1,   16,8,0,4,   0,0,0,0 },
{ 4,    16,7,0,1,    8,2,0,4,    0,0,0,0 },
{ 5,    8,3,0,1,     16,8,0,2,   8,4,0,4 },
{ 4,    8,6,0,1,     8,5,0,4,    0,0,0,0 },
{ 3,    8,6,0,1,     16,10,0,3,  0,0,0,0 },
{ 5,    8,3,0,1,     8,3,80,3,   8,4,100,5 },
{ 4,    8,4,0,1,     8,5,25,3,   8,6,100,5 },
{ 4,    8,8,50,1,    8,6,0,3,    8,7,75,5 },
};
#define BARR_NUM_LEVELS 17

// Title screen (128x64, standard row-major page order).
int[1024] barrTitleScreen =
{
0, 248, 8, 14, 2, 2, 2, 226, 226, 226, 226, 2, 2, 2, 8, 248,
224, 248, 8, 14, 2, 2, 98, 98, 98, 98, 98, 2, 2, 142, 136, 248,
224, 254, 34, 34, 226, 2, 2, 2, 2, 2, 226, 226, 98, 126, 120, 254,
2, 2, 2, 2, 98, 98, 98, 98, 98, 2, 2, 14, 8, 248, 224, 248,
8, 2, 2, 2, 226, 226, 226, 226, 2, 2, 2, 14, 8, 248, 224, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 255, 0, 0, 0, 0, 128, 241, 241, 241, 241, 0, 0, 0, 0, 255,
255, 255, 71, 199, 4, 4, 28, 28, 28, 28, 28, 0, 0, 193, 193, 255,
255, 255, 0, 0, 255, 0, 0, 0, 0, 128, 255, 255, 0, 0, 0, 255,
0, 0, 0, 0, 252, 252, 252, 252, 252, 0, 0, 1, 1, 255, 255, 255,
64, 128, 0, 0, 31, 31, 31, 31, 0, 0, 0, 192, 192, 255, 255, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 131, 195, 195, 227, 227, 227, 224, 224, 224, 227, 227, 195, 195,
131, 3, 0, 0, 0, 3, 3, 243, 19, 19, 19, 19, 19, 19, 19, 19,
16, 16, 16, 16, 112, 64, 3, 195, 67, 115, 19, 19, 16, 16, 16, 16,
16, 19, 19, 19, 67, 195, 3, 240, 16, 16, 19, 19, 19, 19, 19, 19,
16, 16, 19, 19, 67, 195, 3, 243, 19, 19, 19, 19, 19, 19, 16, 16,
16, 16, 16, 112, 64, 192, 0, 240, 16, 16, 16, 16, 240, 192, 240, 16,
16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 240, 192, 240, 16,
16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 112, 64, 192, 0, 0, 0,
0, 0, 254, 255, 255, 255, 3, 1, 49, 121, 121, 49, 1, 3, 255, 255,
255, 254, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0, 0, 227, 227, 227,
227, 0, 0, 0, 8, 8, 255, 255, 0, 0, 0, 0, 0, 143, 143, 143,
143, 0, 0, 0, 0, 255, 255, 255, 0, 0, 0, 0, 0, 227, 227, 227,
227, 0, 0, 0, 8, 255, 255, 255, 0, 0, 0, 0, 227, 227, 227, 227,
227, 0, 0, 8, 8, 255, 255, 255, 0, 0, 0, 0, 255, 255, 255, 0,
0, 0, 0, 227, 227, 227, 227, 227, 227, 227, 255, 255, 255, 3, 255, 0,
0, 0, 0, 227, 227, 227, 227, 0, 0, 0, 8, 8, 255, 255, 0, 0,
0, 0, 1, 7, 15, 15, 31, 30, 30, 30, 30, 30, 30, 31, 15, 15,
7, 1, 0, 0, 0, 0, 0, 7, 4, 24, 24, 24, 24, 24, 24, 24,
24, 24, 24, 24, 30, 30, 7, 7, 4, 24, 24, 24, 28, 31, 31, 7,
7, 0, 24, 24, 24, 31, 31, 31, 0, 24, 24, 24, 28, 31, 31, 7,
7, 0, 24, 24, 24, 31, 31, 31, 0, 24, 24, 24, 31, 31, 31, 7,
7, 0, 24, 24, 24, 31, 31, 31, 0, 24, 24, 24, 31, 31, 31, 0,
24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 31, 30, 31, 0,
24, 24, 24, 31, 31, 31, 7, 4, 24, 24, 24, 24, 31, 31, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 192, 224, 0,
224, 192, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 16, 24, 28, 30, 31, 31, 31, 31, 30,
31, 31, 31, 31, 30, 28, 24, 16, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// -----------------------------------------------------------------------------
//   Sound (see this file's own header comment for the frequency derivation)
// -----------------------------------------------------------------------------

int barrSeqActive;
int* barrSeqNotes;
int barrSeqCount;
int barrSeqIndex;
int barrSeqWaitFrames;

void barrStartJingle( int* notes, int count )
{
    barrSeqNotes = notes;
    barrSeqCount = count;
    barrSeqIndex = 0;
    barrSeqActive = 1;
    barrSeqWaitFrames = 0;
}

void barrAdvanceJingle()
{
    if( !barrSeqActive ) return;
    if( barrSeqWaitFrames > 0 ) { barrSeqWaitFrames--; return; }
    if( barrSeqIndex >= barrSeqCount ) { barrSeqActive = 0; return; }
    float freqHz = (float)barrSeqNotes[ barrSeqIndex * 2 ];
    int durMs = barrSeqNotes[ barrSeqIndex * 2 + 1 ];
    float durationSeconds = (float)durMs / 1000.0;
    md_playTone( freqHz, durationSeconds );

    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    barrSeqWaitFrames = waitFrames;
    barrSeqIndex++;
}

// note(0,4)=261, note(4,4)=329, note(5,4)=349, note(6,4)=370, note(7,4)=391,
// note(9,4)=440, note(11,4)=492, note(0,5)=523 - freq(Hz),duration(ms) pairs,
// 0 Hz meaning a genuine silent rest (matches upstream's own note(0,0) mid-
// jingle rests, e.g. highScore()'s own middle beat).
int[6] barrStartupNotes    = { 261,100, 329,100, 391,250 };
int[8] barrLevelCompleteNotes = { 261,100, 349,100, 440,100, 523,250 };
int[8] barrHighScoreNotes  = { 261,200, 0,100, 261,200, 391,500 };
int[10] barrGameCompleteNotes = { 261,100, 329,100, 391,100, 492,100, 523,250 };
int[4] barrGameOverNotes   = { 370,100, 261,250 };

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

#define BARR_TICK_DIVISOR 3 // upstream's own real delay(50) ~= 20fps

#define BARR_PLAYER_SPEED 5
#define BARR_PLAYER_SIZE 8
#define BARR_PLAYER_Y 7

struct BarrTarget
{
    int size, speed, x0, x1, y0, y1;
    bool enabled, active, dir;
};

BarrTarget[3] barrTargets;

int barrPlayerX0, barrPlayerX1;

int barrBulletX0, barrBulletX1, barrBulletY0, barrBulletY1;
bool barrBulletFired;

int barrLevelNo;
int barrBulletsLeft;
int barrScore;
int barrTopScore;
bool barrNewHigh;
int barrTickCounter;

void barrConfigureTarget( int idx, int size, int speed, int x0, int y0 )
{
    barrTargets[ idx ].size = size;
    barrTargets[ idx ].speed = speed;
    barrTargets[ idx ].x0 = x0;
    barrTargets[ idx ].y0 = y0;
    barrTargets[ idx ].x1 = x0 + size;
    barrTargets[ idx ].y1 = y0 + size / 8;
    barrTargets[ idx ].enabled = size != 0;
    barrTargets[ idx ].active = true;
    barrTargets[ idx ].dir = false;
}

void barrSetupLevel( int levelNo )
{
    barrBulletsLeft = barrLevel[ levelNo ][ 0 ];
    barrConfigureTarget( 0, barrLevel[ levelNo ][ 1 ], barrLevel[ levelNo ][ 2 ], barrLevel[ levelNo ][ 3 ], barrLevel[ levelNo ][ 4 ] );
    barrConfigureTarget( 1, barrLevel[ levelNo ][ 5 ], barrLevel[ levelNo ][ 6 ], barrLevel[ levelNo ][ 7 ], barrLevel[ levelNo ][ 8 ] );
    barrConfigureTarget( 2, barrLevel[ levelNo ][ 9 ], barrLevel[ levelNo ][ 10 ], barrLevel[ levelNo ][ 11 ], barrLevel[ levelNo ][ 12 ] );
}

bool barrLevelIsComplete()
{
    int i;
    for( i = 0; i < 3; i++ )
      if( barrTargets[ i ].enabled && barrTargets[ i ].active ) return false;
    return true;
}

bool barrOutOfBullets()
{
    return ( barrBulletsLeft == 0 ) && !barrBulletFired;
}

void barrUpdateTarget( int idx )
{
    BarrTarget* t = &barrTargets[ idx ];
    if( !t->dir )
    {
        if( t->x1 < ( 128 - t->speed ) ) { t->x0 = t->x0 + t->speed; t->x1 = t->x0 + t->size; }
        else t->dir = true;
    }
    else
    {
        if( t->x0 > ( 0 + t->speed ) ) { t->x0 = t->x0 - t->speed; t->x1 = t->x0 + t->size; }
        else t->dir = false;
    }
}

bool barrBulletInRange( int idx )
{
    BarrTarget* t = &barrTargets[ idx ];
    return ( barrBulletY1 <= t->y1 ) && ( barrBulletY0 >= t->y0 ) && ( barrBulletX1 <= t->x1 ) && ( barrBulletX0 >= t->x0 );
}

// -----------------------------------------------------------------------------
//   Text line buffers (built once per state-entry, not every frame)
// -----------------------------------------------------------------------------

int[24] barrTitleLine;
int barrTitleLineLen;
int barrTitleLineCol;
int barrTitleLinePage;

int[16] barrSubLine;
int barrSubLineLen;
int barrSubLineCol;
int barrSubLinePage;

int[16] barrScoreLine;
int barrScoreLineLen;
int[16] barrHighLine;
int barrHighLineLen;

int[5] barrDigitBuf;
int barrDigitBufLen;

void barrDigitsOf( int value )
{
    if( value < 0 ) value = 0;
    if( value > 9999 ) value = 9999;
    if( value == 0 ) { barrDigitBuf[ 0 ] = 48; barrDigitBufLen = 1; return; }

    int[4] digits;
    int n = 0;
    int tmp = value;
    while( ( tmp > 0 ) && ( n < 4 ) ) { digits[ n ] = tmp % 10; tmp = tmp / 10; n = n + 1; }
    int i;
    for( i = 0; i < n; i++ ) barrDigitBuf[ i ] = 48 + digits[ n - 1 - i ];
    barrDigitBufLen = n;
}

int barrSetText( int* dest, int* text, int textLen )
{
    int i;
    for( i = 0; i < textLen; i++ ) dest[ i ] = text[ i ];
    return textLen;
}

// Builds "prefix" + digitsOf(value) into dest, returns combined length.
int barrSetTextPlusNumber( int* dest, int* text, int textLen, int value )
{
    int i;
    for( i = 0; i < textLen; i++ ) dest[ i ] = text[ i ];
    barrDigitsOf( value );
    for( i = 0; i < barrDigitBufLen; i++ ) dest[ textLen + i ] = barrDigitBuf[ i ];
    return textLen + barrDigitBufLen;
}

// Builds digitsOf(value) + "suffix" into dest, returns combined length.
int barrSetNumberPlusText( int* dest, int value, int* text, int textLen )
{
    barrDigitsOf( value );
    int i;
    for( i = 0; i < barrDigitBufLen; i++ ) dest[ i ] = barrDigitBuf[ i ];
    for( i = 0; i < textLen; i++ ) dest[ barrDigitBufLen + i ] = text[ i ];
    return barrDigitBufLen + textLen;
}

void barrBuildScoreHighLines()
{
    barrScoreLineLen = barrSetTextPlusNumber( barrScoreLine, "Score: ", 7, barrScore );
    barrHighLineLen = barrSetTextPlusNumber( barrHighLine, "High Score: ", 12, barrTopScore );
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

// Per-page composite for PLAYING mode. The naive version of this (calling
// barrBulletByte()/barrTargetByte() x3 unconditionally for every one of the
// 1024 pixels/frame) hit the same "self-gated call still costs a full call
// every time it's invoked" cost this project has found and fixed
// repeatedly elsewhere (Arkanoid/Bert/Tris/Trick/Morpion/Tiny Missile,
// among others) - up to 4 function calls (bullet + 3 targets) per pixel,
// each with its own struct-pointer dereference, even on the ~90% of pixels
// guaranteed to be background. Fixed by writing each object directly into
// a shared row buffer, gated to its own real (page,column) footprint - a
// literal duplicate of what barrSpriteByte's own internal bounds check
// already computes, not an approximation, so this cannot change what ends
// up on screen, only how many times it's computed. Found via a direct user
// report ("during certain levels 100% cpu is reached") - diagnosed and
// fixed by code inspection only, per the user's own explicit instruction
// not to test it in the emulator.
int[128] barrPageBuffer;

void barrComposePlayingRow( int page )
{
    int col;
    for( col = 0; col < 128; col++ ) barrPageBuffer[ col ] = 0;

    if( page == BARR_PLAYER_Y )
    {
        int c;
        for( c = barrPlayerX0; c < barrPlayerX0 + BARR_PLAYER_SIZE; c++ )
          barrPageBuffer[ c ] = barrPageBuffer[ c ] | barrPlayer[ c - barrPlayerX0 ];
    }

    if( barrBulletFired && page == barrBulletY0 )
    {
        int c;
        for( c = barrBulletX0; c < barrBulletX1; c++ )
          barrPageBuffer[ c ] = barrPageBuffer[ c ] | barrBullet[ c - barrBulletX0 ];
    }

    int i;
    for( i = 0; i < 3; i++ )
    {
        BarrTarget* t = &barrTargets[ i ];
        if( !t->enabled ) continue;
        if( page < t->y0 || page >= t->y1 ) continue;

        int* sprite = NULL;
        if( t->size == 8 ) sprite = barrSmallTarget;
        else if( t->size == 16 ) sprite = barrMediumTarget;
        else if( t->size == 32 ) sprite = barrLargeTarget;
        if( sprite == NULL ) continue;

        int rowOffset = ( page - t->y0 ) * t->size;
        int c;
        for( c = t->x0; c < t->x1; c++ )
          barrPageBuffer[ c ] = barrPageBuffer[ c ] | sprite[ rowOffset + ( c - t->x0 ) ];
    }

    if( page == 0 && barrSubLineLen > 0 )
    {
        int endCol = barrSubLineLen * 6;
        if( endCol > 128 ) endCol = 128;
        int c;
        for( c = 0; c < endCol; c++ )
          barrPageBuffer[ c ] = barrPageBuffer[ c ] | barrTextByteAt( barrSubLine, barrSubLineLen, 0, c );
    }
}

#define BARR_MODE_ATTRACT   0
#define BARR_MODE_PLAYING   1
#define BARR_MODE_SCREEN    2 // level intro / level complete / new high / game over / game complete

void barrRenderFrame( int mode )
{
    md_beginFrame();
    int col, page;
    for( page = 0; page < 8; page++ )
    {
        if( mode == BARR_MODE_PLAYING )
        {
            barrComposePlayingRow( page );
            for( col = 0; col < 128; col++ )
              md_drawColumn( col, page, barrPageBuffer[ col ] );
            continue;
        }

        for( col = 0; col < 128; col++ )
        {
            int val = 0;
            if( mode == BARR_MODE_ATTRACT )
            {
                val = barrTitleScreen[ page * 128 + col ];
            }
            else // BARR_MODE_SCREEN
            {
                if( barrTitleLineLen > 0 && page == barrTitleLinePage )
                  val = val | barrTextByteAt( barrTitleLine, barrTitleLineLen, barrTitleLineCol, col );
                if( barrSubLineLen > 0 && page == barrSubLinePage )
                  val = val | barrTextByteAt( barrSubLine, barrSubLineLen, barrSubLineCol, col );
                if( barrScoreLineLen > 0 && page == 4 )
                  val = val | barrTextByteAt( barrScoreLine, barrScoreLineLen, 32, col );
                if( barrHighLineLen > 0 && page == 6 )
                  val = val | barrTextByteAt( barrHighLine, barrHighLineLen, 2, col );
            }
            md_drawColumn( col, page, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define BARR_STATE_ATTRACT        0
#define BARR_STATE_LEVEL_INTRO    1
#define BARR_STATE_PLAYING        2
#define BARR_STATE_LEVEL_COMPLETE 3
#define BARR_STATE_NEW_HIGH       4
#define BARR_STATE_GAME_OVER      5
#define BARR_STATE_GAME_COMPLETE  6

int barrState;
int barrWaitFrames;
int barrAfterHighState; // which state to enter once BARR_STATE_NEW_HIGH finishes
bool barrPrevFire;

void barrClearScreenText()
{
    barrTitleLineLen = 0;
    barrSubLineLen = 0;
    barrScoreLineLen = 0;
    barrHighLineLen = 0;
}

void barrBeginAttract()
{
    barrPrevFire = false;
    barrStartJingle( barrStartupNotes, 3 );
    barrState = BARR_STATE_ATTRACT;
}

void barrBeginLevelIntro( int levelNo )
{
    barrLevelNo = levelNo;
    barrSetupLevel( levelNo );

    barrClearScreenText();
    barrTitleLineLen = barrSetTextPlusNumber( barrTitleLine, "Level ", 6, levelNo );
    barrTitleLineCol = 40; barrTitleLinePage = 4;
    barrSubLineLen = barrSetNumberPlusText( barrSubLine, barrBulletsLeft, "x Bullets", 9 );
    barrSubLineCol = 32; barrSubLinePage = 5;

    barrWaitFrames = 120; // ~2s @ 60fps, matches upstream's own delay(2000)
    barrState = BARR_STATE_LEVEL_INTRO;
}

void barrBeginPlaying()
{
    barrPlayerX0 = ( 128 / 2 ) - ( BARR_PLAYER_SIZE / 2 );
    barrPlayerX1 = barrPlayerX0 + BARR_PLAYER_SIZE;
    barrBulletFired = false;

    barrClearScreenText();
    barrSubLineLen = barrSetNumberPlusText( barrSubLine, barrBulletsLeft, "x Bullets", 9 );

    barrTickCounter = 0;
    barrState = BARR_STATE_PLAYING;
}

void barrBeginLevelComplete()
{
    barrScore = barrScore + barrBulletsLeft * barrLevelNo;

    barrClearScreenText();
    barrTitleLineLen = barrSetText( barrTitleLine, "Level Complete", 14 );
    barrTitleLineCol = 16; barrTitleLinePage = 3;
    barrBuildScoreHighLines();
    barrScoreLineLen = barrSetTextPlusNumber( barrScoreLine, "Score: ", 7, barrScore );
    // levelCompleteScreen only ever shows the score line (col 32, page 5),
    // not the high-score one - reuse that same (col,page) via barrSubLine
    // instead of barrScoreLine's own fixed page-4 slot.
    barrHighLineLen = 0;
    int i;
    for( i = 0; i < barrScoreLineLen; i++ ) barrSubLine[ i ] = barrScoreLine[ i ];
    barrSubLineLen = barrScoreLineLen;
    barrSubLineCol = 32; barrSubLinePage = 5;
    barrScoreLineLen = 0;

    barrStartJingle( barrLevelCompleteNotes, 4 );
    barrWaitFrames = 120;
    barrState = BARR_STATE_LEVEL_COMPLETE;
}

void barrBeginNewHigh( int afterState )
{
    barrTopScore = barrScore;
    barrNewHigh = true;
    barrAfterHighState = afterState;

    barrClearScreenText();
    barrTitleLineLen = barrSetText( barrTitleLine, "New High Score", 14 );
    barrTitleLineCol = 16; barrTitleLinePage = 3;
    int hlen = barrSetTextPlusNumber( barrHighLine, "High Score: ", 12, barrTopScore );
    int i;
    for( i = 0; i < hlen; i++ ) barrSubLine[ i ] = barrHighLine[ i ];
    barrSubLineLen = hlen;
    barrSubLineCol = 2; barrSubLinePage = 5;
    barrHighLineLen = 0;

    barrStartJingle( barrHighScoreNotes, 4 );
    barrWaitFrames = 120;
    barrState = BARR_STATE_NEW_HIGH;
}

void barrBeginGameOver()
{
    barrClearScreenText();
    barrTitleLineLen = barrSetText( barrTitleLine, "Game Over", 9 );
    barrTitleLineCol = 32; barrTitleLinePage = 2;
    barrBuildScoreHighLines();

    barrStartJingle( barrGameOverNotes, 2 );
    barrWaitFrames = 120;
    barrState = BARR_STATE_GAME_OVER;
}

void barrBeginGameComplete()
{
    barrClearScreenText();
    barrTitleLineLen = barrSetText( barrTitleLine, "Game Complete", 13 );
    barrTitleLineCol = 16; barrTitleLinePage = 2;
    barrBuildScoreHighLines();

    barrStartJingle( barrGameCompleteNotes, 5 );
    barrWaitFrames = 120;
    barrState = BARR_STATE_GAME_COMPLETE;
}

void gameAstroBarrier_init()
{
    barrTopScore = 0;
    barrSeqActive = 0;
    barrBeginAttract();
}

void gameAstroBarrier_forceRedraw()
{
    if( barrState == BARR_STATE_ATTRACT ) barrRenderFrame( BARR_MODE_ATTRACT );
    else if( barrState == BARR_STATE_PLAYING ) barrRenderFrame( BARR_MODE_PLAYING );
    else barrRenderFrame( BARR_MODE_SCREEN );
}

void gameAstroBarrier_update()
{
    barrAdvanceJingle();

    if( barrState == BARR_STATE_ATTRACT )
    {
        bool fireNow = isFirePressed();
        if( fireNow && !barrPrevFire )
        {
            barrScore = 0;
            barrNewHigh = false;
            barrBeginLevelIntro( 1 );
            barrRenderFrame( BARR_MODE_SCREEN );
            return;
        }
        barrPrevFire = fireNow;
        barrRenderFrame( BARR_MODE_ATTRACT );
    }
    else if( barrState == BARR_STATE_LEVEL_INTRO )
    {
        barrWaitFrames--;
        if( barrWaitFrames <= 0 ) { barrBeginPlaying(); barrRenderFrame( BARR_MODE_PLAYING ); return; }
        barrRenderFrame( BARR_MODE_SCREEN );
    }
    else if( barrState == BARR_STATE_PLAYING )
    {
        barrTickCounter++;
        if( barrTickCounter >= BARR_TICK_DIVISOR )
        {
            barrTickCounter = 0;

            if( isLeftPressed() && barrPlayerX0 > 0 + BARR_PLAYER_SPEED )
            {
                barrPlayerX0 = barrPlayerX0 - BARR_PLAYER_SPEED;
                barrPlayerX1 = barrPlayerX0 + BARR_PLAYER_SIZE;
            }
            if( isRightPressed() && barrPlayerX1 < 128 - BARR_PLAYER_SPEED )
            {
                barrPlayerX0 = barrPlayerX0 + BARR_PLAYER_SPEED;
                barrPlayerX1 = barrPlayerX0 + BARR_PLAYER_SIZE;
            }
            if( isFirePressed() && !barrBulletFired )
            {
                barrBulletFired = true;
                barrBulletX0 = barrPlayerX0 + 3;
                barrBulletX1 = barrPlayerX1 - 3;
                barrBulletY0 = BARR_PLAYER_Y;
                barrBulletY1 = BARR_PLAYER_Y + 1;
                barrBulletsLeft--;
                barrSubLineLen = barrSetNumberPlusText( barrSubLine, barrBulletsLeft, "x Bullets", 9 );
            }

            if( barrBulletFired )
            {
                bool hit = false;
                int i;
                for( i = 0; i < 3; i++ )
                {
                    if( barrTargets[ i ].enabled && barrTargets[ i ].active && barrBulletInRange( i ) )
                    {
                        barrTargets[ i ].active = false;
                        barrBulletFired = false;
                        hit = true;
                    }
                }
                if( !hit )
                {
                    barrBulletY0--;
                    barrBulletY1--;
                    if( barrBulletY1 <= 0 ) barrBulletFired = false;
                }
            }

            int i;
            for( i = 0; i < 3; i++ )
              if( barrTargets[ i ].enabled && barrTargets[ i ].active ) barrUpdateTarget( i );

            if( barrLevelIsComplete() )
            {
                if( barrLevelNo == BARR_NUM_LEVELS )
                {
                    barrScore = barrScore + barrBulletsLeft * barrLevelNo;
                    if( barrScore > barrTopScore ) { barrBeginNewHigh( BARR_STATE_GAME_COMPLETE ); barrRenderFrame( BARR_MODE_SCREEN ); return; }
                    barrBeginGameComplete();
                    barrRenderFrame( BARR_MODE_SCREEN );
                    return;
                }
                barrBeginLevelComplete();
                barrRenderFrame( BARR_MODE_SCREEN );
                return;
            }
            else if( barrOutOfBullets() )
            {
                if( barrScore > barrTopScore ) { barrBeginNewHigh( BARR_STATE_GAME_OVER ); barrRenderFrame( BARR_MODE_SCREEN ); return; }
                barrBeginGameOver();
                barrRenderFrame( BARR_MODE_SCREEN );
                return;
            }
        }
        barrRenderFrame( BARR_MODE_PLAYING );
    }
    else if( barrState == BARR_STATE_LEVEL_COMPLETE )
    {
        barrWaitFrames--;
        if( barrWaitFrames <= 0 ) { barrBeginLevelIntro( barrLevelNo + 1 ); barrRenderFrame( BARR_MODE_SCREEN ); return; }
        barrRenderFrame( BARR_MODE_SCREEN );
    }
    else if( barrState == BARR_STATE_NEW_HIGH )
    {
        barrWaitFrames--;
        if( barrWaitFrames <= 0 )
        {
            if( barrAfterHighState == BARR_STATE_GAME_COMPLETE ) barrBeginGameComplete();
            else barrBeginGameOver();
            barrRenderFrame( BARR_MODE_SCREEN );
            return;
        }
        barrRenderFrame( BARR_MODE_SCREEN );
    }
    else if( barrState == BARR_STATE_GAME_OVER || barrState == BARR_STATE_GAME_COMPLETE )
    {
        barrWaitFrames--;
        if( barrWaitFrames <= 0 ) { barrBeginAttract(); barrRenderFrame( BARR_MODE_ATTRACT ); return; }
        barrRenderFrame( BARR_MODE_SCREEN );
    }
}

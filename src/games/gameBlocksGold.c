// =============================================================================
// Blocks Gold (upstream repo `ATtiny-Tetris-Gold`, by Jarosław Mazurkiewicz
// (GitHub `jaromaz`) - its own header credits "Andy Jackson, Anthony
// Russell, Tobozo, Neven Boyanov, Jarosław Mazurkiewicz", license "can be
// used non-commercially with or without attribution" for the code not
// already covered by those other sources) - staged 2026-08-06 during a
// wider search for uncatalogued ATtiny85/TinyJoypad games (see CLAUDE.md's
// own `more games/` catalog entry), ported next at direct user request.
//
// **Deliberately named/credited without the trademarked genre name this
// game's own upstream repo/title spells out, anywhere in this project's own
// menu, documentation, or attract-screen text** - at direct user request
// ("port the next game also about tetris gold remember to rename the word
// tetris"), the same treatment already established for the sibling
// `gameFallingBlocks.c` (see that file's own header). Confirmed before
// touching anything that the on-screen word is a plain font-rendered
// string (`ssd1306_char_f8x8(1,64,"TETRIS")`), not baked bitmap data, so -
// same as Falling Blocks - the string itself needed changing, not just the
// menu title. An initial version replaced it with "GOLD" instead of
// reusing Falling Blocks' own "BLOCKS" (to avoid the two games' title
// screens reading as duplicates) and dropped upstream's own separate
// "GOLD" splash (shown right before a normal game starts) as redundant
// once the main title already said it. Revised by direct follow-up
// request: the main title word is "BLOCKS" after all (matching Falling
// Blocks' own word exactly), with "GOLD" instead given its own dedicated
// line below "Attiny"/"Arcade" (a blank spacer column standing in for a
// newline) and vertically centered within the same page span the other
// text uses - preserving this fork's own real brand word without either
// duplicating "GOLD" twice on the same screen or reusing "BLOCKS" as the
// sole differentiator between the two games' title screens. Menu title
// "BLOCKS GOLD" for the same reason (distinct from "FALLING BLOCKS" while
// still describing the genre and carrying this fork's own actual brand
// word).
//
// **This is, almost data-table-for-data-table, the exact same underlying
// engine as the already-shipped Falling Blocks** - confirmed directly, not
// assumed, by extracting every data table from both upstream sources and
// diffing them: `miniBlock`(28)/`blocks`(7)/`blockout`(16)/`ghostout`(16)/
// `brickLogo`(288) are all byte-for-byte identical between the two
// repos, and the attract-screen text call sites
// (`ssd1306_char_f8x8(1,64,"TETRIS")`/`(1,48,"Attiny")`/`(1,40,"Arcade")`)
// land on the exact same (page,column) positions as Falling Blocks' own
// "BLOCKS"/"Attiny"/"Arcade" calls - unsurprising given both credit Andy
// Jackson as their base author, this fork just layers real music/extra
// sound effects on top of the same core. Reused Falling Blocks' own
// already-verified `gldReaderForPage`/rotated-rendering technique, bit-grid
// -to-`bool`-grid conversion (same shift-arithmetic hazard this project's
// own established bug family would otherwise hit - see that file's own
// header for the full reasoning), decode-table logic, and even its exact
// `gldHappyNotes`/`gldGameOverNotes` derived sound tables (the underlying
// `beep(30,i)`/`beep(50,i)` loop shapes are, byte-for-byte, the same lines
// of code in both upstream sources) - rather than re-deriving already-
// solved problems a second time. `STARTLEVEL` differs (this source's own
// literal value is 1, not Falling Blocks' 3) and is kept as this source's
// own real value rather than matched to its sibling's.
//
// Font (`font8x8AJ.h`) is a genuinely different, cleaner table than Falling
// Blocks' own "hacked" 51-glyph subset with letter-substitution tricks -
// byte-diff-verified independently via a small Python script before ever
// being pasted in (66 real glyphs: space/-/./digits/A-Z/a-z, no substituted
// letters needed, confirmed by tracing the shared index formula
// `c=ch-32;if(c>0)c-=12;if(c>15)c-=7;if(c>40)c-=6;` - identical formula to
// Falling Blocks' own `tetFontIndex`, just against a taller table, so the
// out-of-range clamp here is `c>65` instead of Falling Blocks' `c>50`).
//
// New additions beyond the shared engine: a real Tetris-theme melody
// (`gldMusic[50][2]`, freq/duration pairs consumed by upstream's own
// `soundPlay(note,duration)` - `freqHz=1000000/(2*note)` for a real tone,
// `note==0` a silent rest, `durationSeconds=duration/1000`, mapping
// directly onto `md_playTone()`'s own documented "freqHz<=0 is silence"
// contract with no conversion needed) played once, frozen on the title
// screen, before a fresh game begins - reproduced with a frame-stepped
// sequencer (`gldStartMusic`/`gldAdvanceMusic`, matching this project's
// own established `arkStartNoteSeq`-style pattern) rather than the real
// blocking loop upstream uses. Upstream's own quirk - `sng = ghost?50:25`,
// i.e. only the first half of the tune plays when the ghost piece is
// disabled, the full tune when it's enabled - is preserved faithfully
// rather than "fixed" into always playing the whole thing. Two more single-
// shot cues (`beep(20,956)` on every new-piece spawn, `beep(20,568)` on
// every drop tick - genuinely new relative to Falling Blocks, which has
// neither) needed no sequencer at all (each is a single call, not a same-
// tick burst, and `md_playTone()` is real multi-voice per this project's
// own history) - just a heuristic Hz/duration mapping for the NOP-loop-
// timed `beep(count,delay)` primitive (same "no exact real-Hz equivalent to
// reproduce faithfully" situation this project's own Wren Rollercoaster
// port already documented for an identical function): collapsing the
// already-established `freqByte=255-delay/4` heuristic (used to derive
// this project's own `gldHappyNotes`/`gldGameOverNotes` tables further
// below) with `Sound()`'s own `freqHz=500000/(255-freqByte)` formula
// algebraically cancels the intermediate byte-quantization step entirely,
// leaving a direct `freqHz=2000000/delay` - verified by plugging back in
// the delay values already used for the two established sweep tables
// (800/600/400 and the 0..950-step-50 sweep) and confirming they reproduce
// the same frequencies those tables already carry.
//
// Control scheme deliberately matches Falling Blocks exactly, not this
// source's own real hardware button assignment (a DROP button on its own
// dedicated interrupt pin, doubling as attract-screen confirm/hold-gesture
// trigger, plus 3 more buttons - Left/Right/Rotate - sharing one analog-
// ladder pin) - Left/Right move, Up-or-Fire rotates, Down soft/fast-drops,
// Fire confirms/holds on the attract screen. This project's own two
// falling-block games sharing one control scheme (rather than each
// matching its own upstream's real button wiring) means muscle memory
// carries over between them - a deliberate consistency choice, not a
// literal fidelity one. The 2-second hold-to-toggle gesture also reuses
// Falling Blocks' own modifier choice (Down held at the 2s mark = toggle
// challenge mode, otherwise toggle the ghost piece) rather than this
// source's own literal (if imprecisely documented - its header comment
// says "hold DROP and ROTATE together" but the actual code just checks
// "is any of Left/Right/Rotate's shared pin currently high", not
// specifically Rotate) hardware-driven modifier, for the same cross-game-
// consistency reason.
//
// A genuine, if harmless-on-real-AVR-flat-memory, out-of-bounds read found
// and fixed by inspection before ever building (same bug class as this
// whole project's own well-documented "harmless on AVR, a real memory-
// safety risk on Vircon32" family): `drawScreen()`'s own `reader+1` column
// read reaches column index 10 for the last physical page (HORIZ is only
// 10, valid indices 0-9) - on real AVR this silently reads into whatever
// global happens to sit right after `blockArray[10][3]` in memory
// (`ghostArray[10][3]`, per the two arrays' declaration order), a harmless
// (if occasionally visually-noisy) accident; on Vircon32 it would be a
// genuine out-of-bounds array access. Fixed the same way as Falling
// Blocks' own identical fix: `gldBlockAt`/`gldGhostAt` bounds-check and
// return false for any column outside [0,HORIZ) instead of reading past
// the array.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data (byte-diff-verified via a small Python script against the
//   upstream source before ever being pasted in; the 5 tables shared with
//   Falling Blocks were separately diffed against that file's own already-
//   verified copies rather than re-derived)
// -----------------------------------------------------------------------------

int[528] gldFont =
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 63, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 12, 12, 0, 96, 48, 24, 12, 6, 3, 1, 0,
    62, 99, 115, 123, 111, 103, 62, 0, 12, 14, 12, 12, 12, 12, 63, 0,
    30, 51, 48, 28, 6, 51, 63, 0, 30, 51, 48, 28, 48, 51, 30, 0,
    56, 60, 54, 51, 127, 48, 120, 0, 63, 3, 31, 48, 48, 51, 30, 0,
    28, 6, 3, 31, 51, 51, 30, 0, 63, 51, 48, 24, 12, 12, 12, 0,
    30, 51, 51, 30, 51, 51, 30, 0, 30, 51, 51, 62, 48, 24, 14, 0,
    12, 30, 51, 51, 63, 51, 51, 0, 63, 102, 102, 62, 102, 102, 63, 0,
    60, 102, 3, 3, 3, 102, 60, 0, 31, 54, 102, 102, 102, 54, 31, 0,
    127, 70, 22, 30, 22, 70, 127, 0, 127, 70, 22, 30, 22, 6, 15, 0,
    60, 102, 3, 3, 115, 102, 124, 0, 51, 51, 51, 63, 51, 51, 51, 0,
    30, 12, 12, 12, 12, 12, 30, 0, 120, 48, 48, 48, 51, 51, 30, 0,
    103, 102, 54, 30, 54, 102, 103, 0, 15, 6, 6, 6, 70, 102, 127, 0,
    99, 119, 127, 127, 107, 99, 99, 0, 99, 103, 111, 123, 115, 99, 99, 0,
    28, 54, 99, 99, 99, 54, 28, 0, 63, 102, 102, 62, 6, 6, 15, 0,
    30, 51, 51, 51, 59, 30, 56, 0, 63, 102, 102, 62, 54, 102, 103, 0,
    30, 51, 7, 14, 56, 51, 30, 0, 63, 45, 12, 12, 12, 12, 30, 0,
    51, 51, 51, 51, 51, 51, 63, 0, 51, 51, 51, 51, 51, 30, 12, 0,
    99, 99, 99, 107, 127, 119, 99, 0, 99, 99, 54, 28, 28, 54, 99, 0,
    51, 51, 51, 30, 12, 12, 30, 0, 127, 99, 49, 24, 76, 102, 127, 0,
    0, 0, 30, 48, 62, 51, 110, 0, 7, 6, 6, 62, 102, 102, 59, 0,
    0, 0, 30, 51, 3, 51, 30, 0, 56, 48, 48, 62, 51, 51, 110, 0,
    0, 0, 30, 51, 63, 3, 30, 0, 28, 54, 6, 15, 6, 6, 15, 0,
    0, 0, 110, 51, 51, 62, 48, 31, 7, 6, 54, 110, 102, 102, 103, 0,
    12, 0, 14, 12, 12, 12, 30, 0, 48, 0, 48, 48, 48, 51, 51, 30,
    7, 6, 102, 54, 30, 54, 103, 0, 14, 12, 12, 12, 12, 12, 30, 0,
    0, 0, 51, 127, 127, 107, 99, 0, 0, 0, 31, 51, 51, 51, 51, 0,
    0, 0, 30, 51, 51, 51, 30, 0, 0, 0, 59, 102, 102, 62, 6, 15,
    0, 0, 110, 51, 51, 62, 48, 120, 0, 0, 59, 110, 102, 6, 15, 0,
    0, 0, 62, 3, 30, 48, 31, 0, 8, 12, 62, 12, 12, 44, 24, 0,
    0, 0, 51, 51, 51, 51, 110, 0, 0, 0, 51, 51, 51, 30, 12, 0,
    0, 0, 99, 107, 127, 127, 54, 0, 0, 0, 99, 54, 28, 54, 99, 0,
    0, 0, 51, 51, 51, 62, 48, 31, 0, 0, 63, 25, 12, 38, 63, 0,
};

int[28] gldMiniBlock =
{
    119, 119, 0, 0, 112, 119, 112, 0, 112, 0, 112, 119, 112, 7, 112, 7,
    112, 7, 0, 238, 112, 119, 0, 14, 112, 7, 238, 0,
};

int[7] gldBlocks = { 17476, 17600, 17504, 1632, 1728, 3648, 3168, };

int[16] gldBlockout = { 248, 0, 62, 128, 15, 224, 3, 248, 62, 128, 15, 224, 3, 248, 62, 0, };
int[16] gldGhostout  = { 136, 0, 34, 128, 8, 32, 2, 136, 34, 128, 8, 32, 2, 136, 34, 0, };

int[288] gldBrickLogo =
{
    1, 1, 1, 1, 129, 129, 193, 225, 241, 241, 1, 17, 241, 241, 225, 193,
    193, 129, 129, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 252, 252, 254, 255, 255, 255, 255, 255, 255, 255, 255, 0,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 254, 248, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 127, 63, 191, 159, 207, 239,
    231, 247, 251, 224, 1, 251, 243, 247, 231, 239, 207, 223, 223, 191, 191, 48,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 254, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 0, 63, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 224, 6, 254, 252, 252, 252, 248, 248, 240, 240, 224, 0,
    0, 255, 255, 127, 127, 63, 191, 159, 223, 207, 239, 239, 228, 0, 247, 231,
    239, 239, 207, 223, 223, 159, 191, 191, 63, 0, 7, 127, 255, 255, 255, 255,
    255, 255, 255, 240, 0, 252, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 128, 7, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 192, 14,
    62, 30, 28, 29, 13, 9, 3, 3, 0, 7, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 127, 127, 63, 0, 63, 63, 127, 127, 127, 127, 127, 255, 255,
    255, 255, 255, 128, 0, 0, 0, 0, 0, 0, 0, 0, 128, 128, 131, 131,
    131, 137, 141, 141, 140, 142, 142, 142, 143, 143, 159, 143, 143, 143, 143, 135,
    134, 134, 130, 130, 130, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
};

// Real melody data (freq,duration pairs, consumed by upstream's own
// soundPlay() - see gldAdvanceMusic() below for the exact formula).
int[100] gldMusic =
{
    752, 828, 1004, 384, 948, 384, 845, 384, 752, 190, 845, 190, 948, 384, 1004, 384, 1127, 808, 0, 40,
    1127, 364, 948, 384, 752, 828, 845, 384, 948, 384, 1004, 808, 0, 40, 1004, 380, 948, 384, 845, 828,
    752, 828, 948, 828, 1127, 768, 0, 40, 1127, 1676, 845, 798, 0, 40, 845, 354, 710, 384, 562, 828,
    632, 384, 710, 384, 752, 808, 0, 40, 752, 364, 948, 384, 752, 828, 845, 384, 948, 384, 1004, 808,
    0, 40, 1004, 380, 948, 384, 845, 828, 752, 828, 948, 828, 1127, 748, 0, 14, 0, 26, 1127, 1696,
};
#define GLD_MUSIC_FULL_COUNT 50
#define GLD_MUSIC_HALF_COUNT 25

int gldFontIndex( int ch )
{
    int c = ch - 32;
    if( c > 0 ) c = c - 12;
    if( c > 15 ) c = c - 7;
    if( c > 40 ) c = c - 6;
    if( c < 0 || c > 65 ) return 0;
    return c;
}

// Rotated text: character N of the string occupies physical page
// (startPage+N), spanning physical columns [startCol,startCol+8) - see
// gameFallingBlocks.c's own identical `tetCharByte` for why (both games
// share the same underlying engine's own text-rendering mechanism).
int gldCharByte( int* str, int strLen, int startPage, int startCol, int physPage, int physCol )
{
    int charIdx = physPage - startPage;
    if( charIdx < 0 || charIdx >= strLen ) return 0;
    if( physCol < startCol || physCol >= startCol + 8 ) return 0;
    int ch = str[ charIdx ];
    if( ch == 0 ) return 0;
    int idx = gldFontIndex( ch );
    int col = physCol - startCol;
    return gldFont[ idx * 8 + ( 7 - col ) ];
}

int gldDigitByte( int digit, int startPage, int startCol, int physPage, int physCol )
{
    if( physPage != startPage ) return 0;
    if( physCol < startCol || physCol >= startCol + 8 ) return 0;
    int col = physCol - startCol;
    return gldFont[ ( 4 + digit ) * 8 + ( 7 - col ) ];
}

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

int gldSeqActive;
int* gldSeqNotes;
int gldSeqCount;
int gldSeqIndex;
int gldSeqWaitFrames;

void gldStartNoteSeq( int* notes, int count )
{
    gldSeqNotes = notes;
    gldSeqCount = count;
    gldSeqIndex = 0;
    gldSeqActive = 1;
    gldSeqWaitFrames = 0;
}

void gldAdvanceNoteSeq()
{
    if( !gldSeqActive ) return;
    if( gldSeqWaitFrames > 0 ) { gldSeqWaitFrames--; return; }
    if( gldSeqIndex >= gldSeqCount ) { gldSeqActive = 0; return; }
    int freq = gldSeqNotes[ gldSeqIndex * 2 ];
    int dur = gldSeqNotes[ gldSeqIndex * 2 + 1 ];
    Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    gldSeqWaitFrames = waitFrames;
    gldSeqIndex++;
}

// for(i=800;i>200;i-=200) beep(30,i) - line clear "happy sound", byte-for-
// byte the same loop shape (and therefore the same derived table) as
// Falling Blocks'/Stacker's/Breakout's/Space Attack's own identical cue.
int[8] gldHappyNotes = { 55, 31, 105, 31, 155, 31, 205, 31, };
#define GLD_HAPPY_COUNT 4

// for(i=0;i<1000;i+=50) beep(50,i) - game over sweep, same shape/table as
// the sibling games listed above.
int[40] gldGameOverNotes =
{
250,40,243,40,230,40,218,40,205,40,193,40,180,40,168,40,155,40,143,40,
130,40,118,40,105,40,93,40,80,40,68,40,55,40,43,40,30,40,18,40,
};
#define GLD_GAMEOVER_COUNT 20

// Real melody sequencer - freq==0 is a silent rest (md_playTone's own
// freqHz<=0 contract already means "silence", so Sound()'s byte-quantized
// path isn't used here; this game's own music table needs real Hz/seconds,
// not the ELECTROLIB freq-byte convention the two note tables above use).
//
// Upstream's own soundPlay(note,duration) genuinely blocks for `duration`
// real milliseconds on real hardware too (traced directly: its loop
// increments a microsecond counter by exactly note*2 per iteration until
// it reaches duration*1000, so the call takes duration ms regardless of
// pitch) - summed across the whole table that's a real ~12.9s (ghost off,
// 25 notes) to ~25.8s (ghost on, 50 notes) freeze on the title screen
// before every single game start, not a porting artifact. Confirmed via
// direct user report this reads as far too long in practice - scaled down
// by GLD_MUSIC_SPEEDUP (pitch unchanged, only each note's own real-time
// duration shortened) rather than skipping notes outright, since unlike
// this project's other oversized-sequence fixes (computed sweeps with no
// real melodic shape to preserve) this is a genuine composed tune -
// speeding the whole thing up uniformly keeps its relative rhythm/shape
// intact while landing at a duration comparable to this project's other
// title jingles (~3.9s ghost off, ~7.8s ghost on).
#define GLD_MUSIC_SPEEDUP 0.3
void gldAdvanceMusic()
{
    if( !gldSeqActive ) return;
    if( gldSeqWaitFrames > 0 ) { gldSeqWaitFrames--; return; }
    if( gldSeqIndex >= gldSeqCount ) { gldSeqActive = 0; return; }
    int note = gldMusic[ gldSeqIndex * 2 ];
    int dur = gldMusic[ gldSeqIndex * 2 + 1 ];

    float freqHz = 0.0;
    if( note > 0 ) freqHz = 1000000.0 / ( 2.0 * (float)note );
    float durationSeconds = ( (float)dur / 1000.0 ) * GLD_MUSIC_SPEEDUP;
    md_playTone( freqHz, durationSeconds );

    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    gldSeqWaitFrames = waitFrames;
    gldSeqIndex++;
}

void gldStartMusic( int count )
{
    gldSeqCount = count;
    gldSeqIndex = 0;
    gldSeqActive = 1;
    gldSeqWaitFrames = 0;
}

// beep(count,delay) - a NOP-loop-timed square wave with no exact real-Hz
// equivalent (same situation as this project's own Wren Rollercoaster
// port already documented for an identical function) - freqHz=2000000/delay
// is the already-established freqByte=255-delay/4 heuristic (used to
// derive gldHappyNotes/gldGameOverNotes above) collapsed algebraically with
// Sound()'s own freqHz=500000/(255-freqByte) formula, verified by
// reproducing those two tables' own frequencies from their real delay
// values. Both real call sites (new-piece spawn, drop tick) are single,
// standalone calls, not a same-tick burst, so no sequencer is needed -
// md_playTone() is real multi-voice per this project's own established
// history.
void gldBeepOnce( int delay )
{
    if( delay < 1 ) delay = 1;
    float freqHz = 2000000.0 / (float)delay;
    md_playTone( freqHz, 0.05 );
}

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

#define GLD_HORIZ 10
#define GLD_VERTDRAW 19
#define GLD_VERTMAX 24
#define GLD_STARTX 3
#define GLD_STARTY 19
#define GLD_STARTLEVEL 1
#define GLD_LEVELFACTOR 4
#define GLD_DROPDELAY 600

struct GldPiece
{
    int[4][4] blocks;
    int row, column;
};

GldPiece gldCurrent, gldOld, gldGhostPiece;

bool[10][24] gldBlockGrid;
bool[10][24] gldGhostGrid;

int[8][2] gldNextBlockBuffer;
int gldNextPiece;
int gldScore;
int gldTop;
int gldLevel;
bool gldChallengeMode;
bool gldGhostEnabled;

int gldDropCounter;
int gldDropFrames;

bool gldPrevLeft, gldPrevRight, gldPrevRotate;

bool gldReadBlock( int x, int y ) { return gldBlockGrid[ x ][ y ]; }
void gldWriteBlock( int x, int y, bool value ) { gldBlockGrid[ x ][ y ] = value; }
bool gldReadGhost( int x, int y ) { return gldGhostGrid[ x ][ y ]; }
void gldWriteGhost( int x, int y, bool value ) { gldGhostGrid[ x ][ y ] = value; }

void gldFillGrid( bool value, bool ghostMode )
{
    int r, c;
    for( r = 0; r < GLD_VERTMAX; r++ )
      for( c = 0; c < GLD_HORIZ; c++ )
      {
          if( ghostMode ) gldWriteGhost( c, r, value );
          else gldWriteBlock( c, r, value );
      }
}

int gldComputeDropFrames()
{
    int lvl = gldLevel;
    if( lvl * GLD_LEVELFACTOR > GLD_DROPDELAY ) lvl = GLD_DROPDELAY / GLD_LEVELFACTOR;
    int frames = ( ( GLD_DROPDELAY - lvl * GLD_LEVELFACTOR ) * 60 ) / 1000;
    if( frames < 1 ) frames = 1;
    return frames;
}

int gldCheckCollision()
{
    int pieceRow, pieceColumn;
    int c, r;
    pieceColumn = 0;
    for( c = gldCurrent.column; c < gldCurrent.column + 4; c++ )
    {
        pieceRow = 0;
        for( r = gldCurrent.row; r < gldCurrent.row + 4; r++ )
        {
            if( gldCurrent.blocks[ pieceColumn ][ pieceRow ] )
            {
                if( c < 0 ) return 2;
                if( c > 9 ) return 1;
                if( r < 0 ) return 1;
                if( c >= 0 && r >= 0 && c < GLD_HORIZ && r < GLD_VERTMAX )
                {
                    if( gldReadBlock( c, r ) ) return 1;
                }
            }
            pieceRow++;
        }
        pieceColumn++;
    }
    return 0;
}

void gldDrawPiece( int action ) // 0=DRAW, 1=ERASE
{
    int lxn, lxn2;
    for( lxn = 0; lxn < 4; lxn++ )
      for( lxn2 = 0; lxn2 < 4; lxn2++ )
      {
          if( gldCurrent.blocks[ lxn ][ lxn2 ] == 1 )
            gldWriteBlock( gldCurrent.column + lxn, gldCurrent.row + lxn2, action == 0 );
      }
}

void gldDrawGhost( int action )
{
    int lxn, lxn2;
    for( lxn = 0; lxn < 4; lxn++ )
      for( lxn2 = 0; lxn2 < 4; lxn2++ )
      {
          if( gldGhostPiece.blocks[ lxn ][ lxn2 ] == 1 )
            gldWriteGhost( gldGhostPiece.column + lxn, gldGhostPiece.row + lxn2, action == 0 );
      }
}

bool gldCreateGhost()
{
    int tempRow = gldCurrent.row;

    if( gldCurrent.row < 3 ) return false;

    gldCurrent.row -= 2;
    while( gldCheckCollision() == 0 ) gldCurrent.row--;

    int i, j;
    for( i = 0; i < 4; i++ ) for( j = 0; j < 4; j++ ) gldGhostPiece.blocks[i][j] = gldCurrent.blocks[i][j];
    gldGhostPiece.row = gldCurrent.row + 1;
    gldGhostPiece.column = gldCurrent.column;
    gldCurrent.row = tempRow;

    if( gldGhostPiece.row > gldCurrent.row - 3 ) return false;
    return true;
}

void gldLoadPiece( int pieceNumber, int row, int column )
{
    int incr = 0;
    pieceNumber--;
    int lxn, lxn2;
    for( lxn = 0; lxn < 4; lxn++ )
      for( lxn2 = 0; lxn2 < 4; lxn2++ )
      {
          if( ( ( 1 << incr ) & gldBlocks[ pieceNumber ] ) >> incr == 1 ) gldCurrent.blocks[lxn][lxn2] = 1;
          else gldCurrent.blocks[lxn][lxn2] = 0;
          incr++;
      }
    gldCurrent.row = row;
    gldCurrent.column = column;
}

void gldSetNextBlock( int pieceNumber )
{
    gldBeepOnce( 956 );

    int r, c;
    for( r = 0; r < 8; r++ ) for( c = 0; c < 2; c++ ) gldNextBlockBuffer[r][c] = 0;

    pieceNumber--;
    if( pieceNumber == 0 )
    {
        int k;
        for( k = 2; k < 6; k++ )
        {
            gldNextBlockBuffer[k][0] = gldMiniBlock[ pieceNumber * 4 + 0 ];
            gldNextBlockBuffer[k][1] = gldMiniBlock[ pieceNumber * 4 + 0 ];
        }
    }
    else
    {
        int k;
        for( k = 0; k < 3; k++ )
        {
            gldNextBlockBuffer[k][0] = gldMiniBlock[ pieceNumber * 4 + 0 ];
            gldNextBlockBuffer[k][1] = gldMiniBlock[ pieceNumber * 4 + 1 ];
        }
        for( k = 4; k < 7; k++ )
        {
            gldNextBlockBuffer[k][0] = gldMiniBlock[ pieceNumber * 4 + 2 ];
            gldNextBlockBuffer[k][1] = gldMiniBlock[ pieceNumber * 4 + 3 ];
        }
    }
}

void gldRotatePiece()
{
    int[4][4] blocks;
    gldOld = gldCurrent;

    int i, j;
    for( i = 0; i < 4; i++ )
      for( j = 0; j < 4; j++ )
        blocks[j][i] = gldCurrent.blocks[ 4 - i - 1 ][ j ];

    for( i = 0; i < 4; i++ ) for( j = 0; j < 4; j++ ) gldCurrent.blocks[i][j] = blocks[i][j];

    if( gldCheckCollision() ) gldCurrent = gldOld;
    else
    {
        gldDrawGhost( 1 );
        if( gldCreateGhost() ) gldDrawGhost( 0 );
    }
}

void gldMovePieceLeft()
{
    gldOld = gldCurrent;
    gldCurrent.column = gldCurrent.column - 1;
    if( gldCheckCollision() ) gldCurrent = gldOld;
    else
    {
        gldDrawGhost( 1 );
        if( gldCreateGhost() ) gldDrawGhost( 0 );
    }
}

void gldMovePieceRight()
{
    gldOld = gldCurrent;
    gldCurrent.column = gldCurrent.column + 1;
    if( gldCheckCollision() ) gldCurrent = gldOld;
    else
    {
        gldDrawGhost( 1 );
        if( gldCreateGhost() ) gldDrawGhost( 0 );
    }
}

// Direct 1:1 translation of upstream's own scan/clear/shift/recheck loop -
// already correctly handles multiple simultaneous full rows via its own
// "recheck the same index" behavior. Returns the number of rows cleared.
int gldClearFullRows()
{
    int totalRows = 0;
    int row, col;
    for( row = 0; row < GLD_VERTMAX; row++ )
    {
        bool rowFull = true;
        for( col = 0; col < GLD_HORIZ; col++ ) if( !gldReadBlock( col, row ) ) rowFull = false;

        if( rowFull )
        {
            totalRows++;
            for( col = 0; col < GLD_HORIZ; col++ ) gldWriteBlock( col, row, false );
            int dropCol, dropRow;
            for( dropCol = 0; dropCol < GLD_HORIZ; dropCol++ )
              for( dropRow = row; dropRow < GLD_VERTMAX - 1; dropRow++ )
                gldWriteBlock( dropCol, dropRow, gldReadBlock( dropCol, dropRow + 1 ) );
            row--;
        }
    }
    return totalRows;
}

// Set by gldMovePieceDown() itself the instant a freshly-spawned piece is
// found to already collide with the stack - see gameFallingBlocks.c's own
// identical `tetGameOverFlag` comment for why this is the only correct
// moment to detect game-over.
bool gldGameOverFlag;

bool gldMovePieceDown()
{
    gldGameOverFlag = false;
    gldOld = gldCurrent;
    gldCurrent.row--;

    if( gldCheckCollision() )
    {
        gldCurrent.row = gldOld.row;
        gldDrawPiece( 0 );

        int totalRows = gldClearFullRows();
        gldLevel += totalRows;
        if( totalRows == 1 ) gldScore += 40;
        else if( totalRows == 2 ) gldScore += 100;
        else if( totalRows == 3 ) gldScore += 300;
        else if( totalRows == 4 ) gldScore += 800;
        if( totalRows > 0 ) gldStartNoteSeq( gldHappyNotes, GLD_HAPPY_COUNT );

        gldLoadPiece( gldNextPiece, GLD_STARTY, GLD_STARTX );
        if( gldCheckCollision() )
        {
            gldGameOverFlag = true;
            return true;
        }
        gldDrawGhost( 1 );
        if( gldCreateGhost() ) gldDrawGhost( 0 );
        gldNextPiece = arand( 7 ) + 1;
        gldSetNextBlock( gldNextPiece );

        gldDropFrames = gldComputeDropFrames();
        return true;
    }

    gldDrawGhost( 1 );
    if( gldCreateGhost() ) gldDrawGhost( 0 );
    return false;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

// physPage(0-7) -> logical column "reader" (this page shows reader AND
// reader+1, matching upstream's own 2-logical-column-per-physical-page
// overlap scheme). Column 10 (reader+1 at physPage 7) doesn't exist -
// guarded to read as empty rather than out of bounds (see this file's own
// header comment on the shared bug found here and in Falling Blocks).
int gldReaderForPage( int page )
{
    if( page < 4 ) return page;
    if( page < 7 ) return page + 1;
    return page + 2;
}

bool gldBlockAt( int col, int row )
{
    if( col < 0 || col >= GLD_HORIZ || row < 0 || row >= GLD_VERTMAX ) return false;
    return gldReadBlock( col, row );
}

bool gldGhostAt( int col, int row )
{
    if( col < 0 || col >= GLD_HORIZ || row < 0 || row >= GLD_VERTMAX ) return false;
    return gldReadGhost( col, row );
}

int gldBorderByte( int physCol, int physPage )
{
    if( physPage == 0 || physPage == 7 )
    {
        if( physCol == 0 || physCol == 126 ) return 0xFF;
        if( physCol >= 1 && physCol <= 125 )
        {
            if( physPage == 0 ) return 0x01;
            return 0x80;
        }
        return 0;
    }
    if( physCol == 0 || physCol == 127 ) return 0xFF;
    return 0;
}

int[128] gldPageBuffer;

void gldComposeGameRow( int physPage )
{
    int base = 0;
    if( physPage == 0 ) base = 0x01;
    else if( physPage == 7 ) base = 0x80;

    int reader = gldReaderForPage( physPage );
    int blockOutA = gldBlockout[ physPage * 2 ];
    int blockOutB = gldBlockout[ physPage * 2 + 1 ];
    int ghostOutA = gldGhostout[ physPage * 2 ];
    int ghostOutB = gldGhostout[ physPage * 2 + 1 ];

    int row;
    for( row = 0; row < GLD_VERTDRAW; row++ )
    {
        bool blockA = gldBlockAt( reader, row );
        bool blockB = gldBlockAt( reader + 1, row );
        bool ghostA = gldGhostEnabled && gldGhostAt( reader, row );
        bool ghostB = gldGhostEnabled && gldGhostAt( reader + 1, row );

        int sub;
        for( sub = 0; sub < 5; sub++ )
        {
            int physCol = row * 6 + sub;
            int val = base;
            if( blockA ) val = val | blockOutA;
            if( blockB ) val = val | blockOutB;
            if( ghostA )
            {
                if( sub == 0 || sub == 4 ) val = val | blockOutA;
                else val = val | ghostOutA;
            }
            if( ghostB )
            {
                if( sub == 0 || sub == 4 ) val = val | blockOutB;
                else val = val | ghostOutB;
            }
            gldPageBuffer[ physCol ] = val | gldBorderByte( physCol, physPage );
        }
        int sepCol = row * 6 + 5;
        gldPageBuffer[ sepCol ] = base | gldBorderByte( sepCol, physPage );
    }

    int physCol;
    for( physCol = GLD_VERTDRAW * 6; physCol < 128; physCol++ )
    {
        int val = gldBorderByte( physCol, physPage );
        if( physPage == 6 || physPage == 7 )
        {
            if( physCol >= GLD_VERTDRAW * 6 && physCol < GLD_VERTDRAW * 6 + 8 )
              val = val | gldNextBlockBuffer[ physCol - GLD_VERTDRAW * 6 ][ physPage - 6 ];
        }
        gldPageBuffer[ physCol ] = val;
    }
}

#define GLD_MODE_ATTRACT   0
#define GLD_MODE_PLAYING   1
#define GLD_MODE_GAMEOVER  2

int* gldAttractMsg1;
int gldAttractMsg1Len;
int gldAttractMsg1Page;
int gldAttractMsg1Col;
int* gldAttractMsg2;
int gldAttractMsg2Len;
int gldAttractMsg2Page;
int gldAttractMsg2Col;
int gldAttractMsgWaitFrames;

bool gldNewHigh;
bool gldScoreBlank;
int gldGameOverWaitFrames;
int gldGameOverBlinkCount;

void gldRenderFrame( int mode )
{
    md_beginFrame();
    int physCol, physPage;
    for( physPage = 0; physPage < 8; physPage++ )
    {
        if( mode == GLD_MODE_PLAYING )
        {
            gldComposeGameRow( physPage );
            for( physCol = 0; physCol < 128; physCol++ )
                md_drawColumn( physCol, physPage, gldPageBuffer[ physCol ] );
            continue;
        }

        for( physCol = 0; physCol < 128; physCol++ )
        {
            int val = 0;
            if( mode == GLD_MODE_ATTRACT )
            {
                val = val | gldBorderByte( physCol, physPage );
                if( physCol >= 78 && physCol < 114 )
                  val = val | gldBrickLogo[ physPage * 36 + ( physCol - 78 ) ];

                // Same call-site gating as gameFallingBlocks.c's own
                // identical layer - each gldCharByte call below is only
                // ever nonzero within its own exact footprint.
                if( physPage >= 1 && physPage < 7 )
                {
                    if( physCol >= 64 && physCol < 72 )
                      val = val | gldCharByte( "BLOCKS", 6, 1, 64, physPage, physCol );
                    if( physCol >= 48 && physCol < 56 )
                      val = val | gldCharByte( "Attiny", 6, 1, 48, physPage, physCol );
                    if( physCol >= 40 && physCol < 48 )
                      val = val | gldCharByte( "Arcade", 6, 1, 40, physPage, physCol );
                }
                // "GOLD" - this fork's own real brand word, placed as its
                // own line below Attiny/Arcade (a blank spacer column at
                // col 32-39 stands in for a newline) and vertically
                // centered within the same 6-page span (1-6) the other
                // three lines use - GOLD is only 4 characters, so it's
                // inset by 1 page top and bottom (pages 2-5) rather than
                // starting flush at page 1 like the longer words above.
                if( physPage >= 2 && physPage < 6 && physCol >= 24 && physCol < 32 )
                  val = val | gldCharByte( "GOLD", 4, 2, 24, physPage, physCol );
                if( gldAttractMsg1Len > 0
                    && physPage >= gldAttractMsg1Page && physPage < gldAttractMsg1Page + gldAttractMsg1Len
                    && physCol >= gldAttractMsg1Col && physCol < gldAttractMsg1Col + 8 )
                  val = val | gldCharByte( gldAttractMsg1, gldAttractMsg1Len, gldAttractMsg1Page, gldAttractMsg1Col, physPage, physCol );
                if( gldAttractMsg2Len > 0
                    && physPage >= gldAttractMsg2Page && physPage < gldAttractMsg2Page + gldAttractMsg2Len
                    && physCol >= gldAttractMsg2Col && physCol < gldAttractMsg2Col + 8 )
                  val = val | gldCharByte( gldAttractMsg2, gldAttractMsg2Len, gldAttractMsg2Page, gldAttractMsg2Col, physPage, physCol );
            }
            else // GLD_MODE_GAMEOVER
            {
                val = val | gldBorderByte( physCol, physPage );
                if( !gldScoreBlank && physCol >= 80 && physCol < 88 )
                {
                    if( physPage == 1 ) val = val | gldDigitByte( ( gldScore / 100000 ) % 10, 1, 80, physPage, physCol );
                    else if( physPage == 2 ) val = val | gldDigitByte( ( gldScore / 10000 ) % 10, 2, 80, physPage, physCol );
                    else if( physPage == 3 ) val = val | gldDigitByte( ( gldScore / 1000 ) % 10, 3, 80, physPage, physCol );
                    else if( physPage == 4 ) val = val | gldDigitByte( ( gldScore / 100 ) % 10, 4, 80, physPage, physCol );
                    else if( physPage == 5 ) val = val | gldDigitByte( ( gldScore / 10 ) % 10, 5, 80, physPage, physCol );
                    else if( physPage == 6 ) val = val | gldDigitByte( gldScore % 10, 6, 80, physPage, physCol );
                }
                if( !( gldNewHigh && gldScoreBlank ) && physCol >= 40 && physCol < 48 )
                {
                    if( physPage == 1 ) val = val | gldDigitByte( ( gldTop / 100000 ) % 10, 1, 40, physPage, physCol );
                    else if( physPage == 2 ) val = val | gldDigitByte( ( gldTop / 10000 ) % 10, 2, 40, physPage, physCol );
                    else if( physPage == 3 ) val = val | gldDigitByte( ( gldTop / 1000 ) % 10, 3, 40, physPage, physCol );
                    else if( physPage == 4 ) val = val | gldDigitByte( ( gldTop / 100 ) % 10, 4, 40, physPage, physCol );
                    else if( physPage == 5 ) val = val | gldDigitByte( ( gldTop / 10 ) % 10, 5, 40, physPage, physCol );
                    else if( physPage == 6 ) val = val | gldDigitByte( gldTop % 10, 6, 40, physPage, physCol );
                }
            }
            md_drawColumn( physCol, physPage, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define GLD_STATE_ATTRACT    0
#define GLD_STATE_MUSICWAIT  1
#define GLD_STATE_PLAYING    2
#define GLD_STATE_GAMEOVER   3

int gldState;
int gldFireHeld;
int gldFireHoldTicks;
int gldGestureDone;

void gldClearAttractMsg()
{
    gldAttractMsg1Len = 0;
    gldAttractMsg2Len = 0;
}

void gldBeginAttract()
{
    gldFireHeld = 0;
    gldFireHoldTicks = 0;
    gldGestureDone = 0;
    gldClearAttractMsg();
    gldState = GLD_STATE_ATTRACT;
}

void gldBeginGame()
{
    gldScore = 0;
    gldLevel = GLD_STARTLEVEL;
    gldFillGrid( false, false );
    gldFillGrid( false, true );

    gldLoadPiece( arand( 7 ) + 1, GLD_STARTY, GLD_STARTX );
    gldDrawPiece( 0 );
    if( gldCreateGhost() ) gldDrawGhost( 0 );
    gldNextPiece = arand( 7 ) + 1;
    gldSetNextBlock( gldNextPiece );

    if( gldChallengeMode )
    {
        int cl;
        for( cl = 0; cl < 100; cl++ )
        {
            gldDrawPiece( 1 );
            gldMovePieceDown();
            if( ( arand( 7 ) + 1 ) > 4 ) gldMovePieceLeft();
            gldDrawPiece( 0 );
        }
        gldLevel = GLD_STARTLEVEL;
    }

    gldDropFrames = gldComputeDropFrames();
    gldDropCounter = gldDropFrames;
    gldPrevLeft = false;
    gldPrevRight = false;
    gldPrevRotate = false;

    gldState = GLD_STATE_PLAYING;
}

void gldBeginGameOver()
{
    if( gldScore > gldTop )
    {
        gldTop = gldScore;
        gldNewHigh = true;
        // Direct translation of upstream's own 2-byte big-endian
        // EEPROM.write topScore save. Upstream also persists a separate
        // RNG seed at addr 0x03 via raw avr-libc eeprom_read_word/
        // write_word - not restored here, since it isn't a high score
        // (just a minor start-of-session variety tweak) and this pass is
        // scoped to score persistence specifically.
        eeprom_write_word( 0, gldTop );
    }
    else gldNewHigh = false;

    gldScoreBlank = false;
    gldGameOverBlinkCount = 0;
    gldGameOverWaitFrames = 12; // ~200ms at 60fps, matches upstream's own delay(200)
    gldStartNoteSeq( gldGameOverNotes, GLD_GAMEOVER_COUNT );
    gldState = GLD_STATE_GAMEOVER;
}

void gameBlocksGold_init()
{
    // Direct translation of upstream's own topScore = EEPROM.read(0)<<8 |
    // EEPROM.read(1), guarded against a never-written slot's own virgin
    // 65535 read the same way as every other game in this pass.
    gldTop = eeprom_read_word( 0 );
    if( gldTop == 65535 ) gldTop = 0;
    gldChallengeMode = false;
    gldGhostEnabled = true;
    gldSeqActive = 0;
    gldBeginAttract();
}

void gameBlocksGold_forceRedraw()
{
    if( gldState == GLD_STATE_PLAYING ) gldRenderFrame( GLD_MODE_PLAYING );
    else if( gldState == GLD_STATE_GAMEOVER ) gldRenderFrame( GLD_MODE_GAMEOVER );
    else gldRenderFrame( GLD_MODE_ATTRACT );
}

void gameBlocksGold_update()
{
    if( gldState == GLD_STATE_ATTRACT )
    {
        int fireDown = isFirePressed();
        if( fireDown )
        {
            gldFireHoldTicks++;
            if( gldFireHoldTicks >= 120 && !gldGestureDone )
            {
                gldGestureDone = 1;
                if( isDownPressed() )
                {
                    gldChallengeMode = !gldChallengeMode;
                    gldAttractMsg1 = "MODE"; gldAttractMsg1Len = 4; gldAttractMsg1Page = 2; gldAttractMsg1Col = 8;
                    if( gldChallengeMode ) { gldAttractMsg2 = "HARD"; gldAttractMsg2Len = 4; gldAttractMsg2Page = 2; gldAttractMsg2Col = 16; }
                    else { gldAttractMsg2 = "NORMAL"; gldAttractMsg2Len = 6; gldAttractMsg2Page = 1; gldAttractMsg2Col = 16; }
                }
                else
                {
                    gldGhostEnabled = !gldGhostEnabled;
                    gldAttractMsg1 = "GHOST"; gldAttractMsg1Len = 5; gldAttractMsg1Page = 1; gldAttractMsg1Col = 16;
                    if( gldGhostEnabled ) { gldAttractMsg2 = "ON"; gldAttractMsg2Len = 2; gldAttractMsg2Page = 2; gldAttractMsg2Col = 8; }
                    else { gldAttractMsg2 = "OFF"; gldAttractMsg2Len = 3; gldAttractMsg2Page = 2; gldAttractMsg2Col = 8; }
                }
                gldBeepOnce( 956 );
                gldAttractMsgWaitFrames = 90;
            }
        }
        else
        {
            if( gldFireHeld && !gldGestureDone )
            {
                gldFireHeld = fireDown;
                if( gldGhostEnabled ) gldStartMusic( GLD_MUSIC_FULL_COUNT );
                else gldStartMusic( GLD_MUSIC_HALF_COUNT );
                gldState = GLD_STATE_MUSICWAIT;
                gldRenderFrame( GLD_MODE_ATTRACT );
                return;
            }
            gldFireHoldTicks = 0;
            gldGestureDone = 0;
        }
        gldFireHeld = fireDown;

        if( gldAttractMsg1Len > 0 )
        {
            gldAttractMsgWaitFrames--;
            if( gldAttractMsgWaitFrames <= 0 ) gldClearAttractMsg();
        }

        gldRenderFrame( GLD_MODE_ATTRACT );
    }
    else if( gldState == GLD_STATE_MUSICWAIT )
    {
        gldAdvanceMusic();
        if( !gldSeqActive )
        {
            gldBeginGame();
            gldRenderFrame( GLD_MODE_PLAYING );
            return;
        }
        gldRenderFrame( GLD_MODE_ATTRACT );
    }
    else if( gldState == GLD_STATE_PLAYING )
    {
        gldAdvanceNoteSeq();

        int rotateNow = isFirePressed() || isUpPressed();

        if( isLeftPressed() && !gldPrevLeft ) { gldDrawPiece( 1 ); gldMovePieceLeft(); gldDrawPiece( 0 ); }
        else if( isRightPressed() && !gldPrevRight ) { gldDrawPiece( 1 ); gldMovePieceRight(); gldDrawPiece( 0 ); }

        if( rotateNow && !gldPrevRotate ) { gldDrawPiece( 1 ); gldRotatePiece(); gldDrawPiece( 0 ); }

        gldPrevLeft = isLeftPressed();
        gldPrevRight = isRightPressed();
        gldPrevRotate = rotateNow;

        bool doDrop = false;
        if( isDownPressed() && gldCurrent.row < GLD_STARTY - 5 ) doDrop = true;
        gldDropCounter--;
        if( gldDropCounter <= 0 ) { doDrop = true; gldDropCounter = gldDropFrames; }

        if( doDrop )
        {
            gldDrawPiece( 1 );
            gldMovePieceDown();
            gldDrawPiece( 0 );
            gldBeepOnce( 568 );

            if( gldGameOverFlag )
            {
                gldBeginGameOver();
                gldRenderFrame( GLD_MODE_GAMEOVER );
                return;
            }
        }

        gldRenderFrame( GLD_MODE_PLAYING );
    }
    else // GLD_STATE_GAMEOVER
    {
        gldAdvanceNoteSeq();

        if( gldSeqActive ) { gldRenderFrame( GLD_MODE_GAMEOVER ); return; }

        gldGameOverWaitFrames--;
        if( gldGameOverWaitFrames <= 0 )
        {
            gldGameOverWaitFrames = 12;
            gldScoreBlank = !gldScoreBlank;
            gldGameOverBlinkCount++;
            if( gldGameOverBlinkCount >= 8 )
            {
                gldBeginAttract();
                gldRenderFrame( GLD_MODE_ATTRACT );
                return;
            }
        }
        gldRenderFrame( GLD_MODE_GAMEOVER );
    }
}

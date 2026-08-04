// =============================================================================
// Falling Blocks (upstream folder is the "multi-button" variant of Andy
// Jackson's own falling-block puzzle clone, non-commercial-with-or-
// without-attribution; ATtiny-Joypad port by Billy Cheung, 2018) -
// deliberately credited/named without the trademarked genre name it's a
// clone of, anywhere in this project's own menu, documentation, or
// attract-screen text - at direct user request. Upstream's own attract
// screen spells the genre name out via a plain font-rendered string
// (`tetCharByte("...", 6, 1, 64, ...)` below), not baked pixel/bitmap
// data - a genuinely different case from the separate decorative
// brick/diamond logo graphic next to it (`tetBrickLogo`, real bitmap data,
// which doesn't spell out any word at all) - so unlike a baked bitmap
// asset, this string was simply changed to "BLOCKS" in the source rather
// than needing to stay as shipped. Upstream's own header comment describes it as
// "essentially a clone of [the same well-known falling-block puzzle game
// referenced above] by Anthony Russell, with some additional features"
// (Highscore, an optional Ghost/Shadow piece, and a
// Hard/Challenge mode that seeds the board with junk at the start).
// Last of the 3 games found via the `more games/gametiny/` re-verification
// (see that folder's own catalog entry and this file's own Status intro in
// CLAUDE.md) - confirmed via direct reading to be a genuinely distinct
// codebase from Daniel C's own Tiny Tris (different author, different
// author's own from-scratch rendering/engine code, different data
// representation), sharing nothing but the genre itself.
//
// Not `tinyJoypadShim`/`obonoCoreShim` lineage by name, and genuinely more
// structurally different from every other `gametiny`/Andy-Jackson-family
// port so far - needed no new shim (same A0/A3 500-750/750-950 analog
// thresholds as every other game in this family), but its own rendering
// model is unique in this project: **the OLED is driven sideways**. Every
// other TinyJoypad game treats the 128 physical columns as the visual
// width and the 8 pages as the visual height; this game's own
// `ssd1306_char_f8x8()`/`drawScreen()` instead treat physical *pages* as
// the board's column axis and physical *columns* as the board's row axis
// (each board row is 6 physical columns wide; each on-screen text
// character occupies one whole physical page). Ported by building the
// per-(physColumn,physPage) query function directly around this rotated
// mapping, rather than trying to "un-rotate" it into a more conventional
// layout - safer than second-guessing a coordinate scheme this
// intertwined with the game's own data layout.
//
// Structural notes:
// - `blockArray[10][3]`/`ghostArray[10][3]` are byte-packed bitfields
//   upstream (24 rows packed 8-per-byte, addressed via shift/mask
//   trickery in `readBlockArray`/`writeblockArray`) - a real RAM-saving
//   trick with no benefit on Vircon32. More importantly, tracing the
//   exact shift arithmetic for row 23 (the top spawn-buffer row - `y-15`
//   reaches 8, a shift amount that only stays safe on AVR because a
//   `uint8_t & (1<<8)` is *always* 0 by definition, and because AVR-GCC's
//   own variable-shift-count codegen saturates to 0 past the operand
//   width - both of them AVR-specific narrow-type behaviors already
//   documented elsewhere in this project as *not* safely portable)
//   revealed that a literal port of this bit-packing would either need
//   yet another shift-safety workaround for no real benefit, or would
//   quietly behave differently (and worse - shifting by a genuinely
//   negative amount, not just a wrapping one) on Vircon32's plain,
//   non-truncating `int`s. Ported instead as a plain `bool[10][24]` grid
//   with the identical read/write *interface* - correct by construction,
//   avoiding the whole shift-arithmetic hazard rather than working around
//   it, unlike Tiny Tris's own bit-packed grid (kept faithful there since
//   no such hazard was ever found in that game's own equivalent code).
// - Upstream's own real per-piece drop timer (the game's own outer
//   `while` loop: move down, then busy-wait via repeated `handleInput()`
//   calls until `(millis()-moveTime) >= DROPDELAY-level*LEVELFACTOR`) is a
//   genuine, real-time-based rate - unlike most other `gametiny` ports in
//   this project, which had no real timing model at all. Converted to a
//   frame-counted `tetDropFrames` derived directly from the same formula
//   (`(600-level*4)*60/1000`), decremented once per real engine tick;
//   `handleInput()`'s own key-press handling was *not* ported as a
//   busy-wait-for-release dance (a debounce technique that exists purely
//   to work around noisy analog voltage-divider buttons, which Vircon32's
//   clean digital gamepad reads don't need - matching this project's own
//   established simplification for every other `gametiny` port's real-
//   hardware debounce code) - ported instead as plain edge-detected
//   presses for move-left/right/rotate, and a level-held check for soft-
//   drop (drops once per tick while Down is held, approximating upstream's
//   own `delay(10)`-paced soft-drop busy-wait loop closely enough at this
//   engine's own 60fps tick rate).
// - Rotate is triggered by *either* the dedicated Fire/Rotate button *or*
//   the Up-analog direction (matching upstream's own dual `keyLock=3`
//   sources: the real interrupt on the physical rotate button, and the
//   "up" reading on the shared up/down axis).
// - The line-clear-and-shift scan (`for each row: if full, clear it, shift
//   everything above down, recheck the same row index`) is ported as a
//   direct 1:1 translation of upstream's own algorithm (just reading a
//   plain bool grid instead of bit-shifting a packed byte) - this already
//   correctly handles multiple simultaneous full rows via its
//   own re-check-same-index behavior, with no restructuring needed.
//   Upstream's own brief per-row visual flash (`drawGameScreen(...);
//   delay(30);` before each row's own shift) is *not* reproduced as a
//   dedicated multi-frame state - resolved instantly in the same tick as
//   landing instead, a deliberate effort/fidelity tradeoff for a ~30ms
//   cosmetic detail (this project's other ports have made the same call
//   for comparably brief, purely-decorative flourishes, e.g. Space
//   Attack's own dropped slide-in animation).
// - Sound: the "happy sound" 4-note line-clear cue
//   (`for(i=800;i>200;i-=200)beep(30,i)`) fires *inside* upstream's own
//   per-row loop - once per row cleared, not once per landing event. This
//   port fires it once per landing event that clears at least one row
//   (not once per row within that event) - Vircon32's audio channel has
//   no queue, so multiple same-tick sequencer restarts would only ever
//   play the last one's notes anyway, making the distinction largely
//   inaudible in practice; simplified rather than engineered around for a
//   difference this narrow. The game-over sweep
//   (`for(i=0;i<1000;i+=50)beep(50,i)`) is, byte-for-byte, the same loop
//   shape already fixed for Stacker/Breakout/Space Attack's own identical
//   cues (same author/boilerplate lineage) - reused via the same derived
//   note table.
// - **A real, pre-existing upstream bug, confirmed independently via a
//   small script before ever building anything**: this game's own
//   `font8x8AJ.h` is a "hacked" 51-glyph subset (space/-/./digits/A-Z/
//   a,c,d,e,i,j,k, plus 4 deliberately-relabeled slots - typing 'b'/'f'/
//   'g'/'h' in a source string actually renders an n/r/y/t-shaped glyph
//   instead, the same "type the wrong letter to get the right-looking
//   glyph" trick already seen in Wren/Bat Bonanza/Stacker's own fonts).
//   But tracing `ssd1306_char_f8x8()`'s own index formula
//   (`c=ch-32;if(c>0)c-=12;if(c>15)c-=7;if(c>40)c-=6;`) against the
//   game's *own real UI strings* shows most of them contain lowercase
//   letters that were never remapped to a substitute slot at all (t, n,
//   y, r appearing directly, not the g/h/b/f stand-ins the substitution
//   scheme actually requires) - computing out to indices 53-64, genuinely
//   past the 51-entry table (`"Attiny"` alone hits this for its own two
//   't's, its 'n', and its 'y'; `"Arcade"`'s own 'r'; `"Andy-J"`'s own 'n'
//   and 'y'). This isn't a porting mistake - it reproduces upstream's own
//   literal formula and literal strings exactly - it looks like a genuine,
//   shipped bug in the original game (harmless-buy-undefined PROGMEM
//   overrun on real AVR flash, never caught/fixed), affecting its own
//   title-screen credit text. A true out-of-bounds read is a real memory-
//   safety risk on Vircon32 (not just a wrong-looking glyph), so
//   `tetFontIndex()` clamps any out-of-range result to the blank/space
//   glyph (index 0) - a safety fix, not an attempt to guess what the
//   "correct" glyph should have been, since upstream's own real behavior
//   for these specific characters was already undefined.
// - EEPROM high-score persistence dropped (session-only), matching every
//   other port's precedent. The attract screen's own hold-gestures are
//   kept (in-memory flags): hold Fire/Rotate ~2s to toggle the ghost
//   piece; hold it with Down also held to toggle challenge/hard mode
//   instead - matching upstream's own dual-gesture structure.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data (byte-diff-verified via a small Python script against the
//   upstream source before ever being pasted in)
// -----------------------------------------------------------------------------

int[408] tetFont =
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
    0, 0, 30, 48, 62, 51, 110, 0, 0, 0, 31, 51, 51, 51, 51, 0,
    0, 0, 30, 51, 3, 51, 30, 0, 56, 48, 48, 62, 51, 51, 110, 0,
    0, 0, 30, 51, 63, 3, 30, 0, 0, 0, 59, 110, 102, 6, 15, 0,
    0, 0, 51, 51, 51, 62, 48, 31, 8, 12, 62, 12, 12, 44, 24, 0,
    12, 0, 14, 12, 12, 12, 30, 0, 48, 0, 48, 48, 48, 51, 51, 30,
    7, 6, 102, 54, 30, 54, 103, 0,
};

int[28] tetMiniBlock =
{
    119, 119, 0, 0, 112, 119, 112, 0, 112, 0, 112, 119, 112, 7, 112, 7,
    112, 7, 0, 238, 112, 119, 0, 14, 112, 7, 238, 0,
};

int[7] tetBlocks = { 17476, 17600, 17504, 1632, 1728, 3648, 3168, };

int[16] tetBlockout = { 248, 0, 62, 128, 15, 224, 3, 248, 62, 128, 15, 224, 3, 248, 62, 0, };
int[16] tetGhostout  = { 136, 0, 34, 128, 8, 32, 2, 136, 34, 128, 8, 32, 2, 136, 34, 0, };

int[288] tetBrickLogo =
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

int tetFontIndex( int ch )
{
    int c = ch - 32;
    if( c > 0 ) c = c - 12;
    if( c > 15 ) c = c - 7;
    if( c > 40 ) c = c - 6;
    if( c < 0 || c > 50 ) return 0;
    return c;
}

// Rotated text: character N of the string occupies physical page
// (startPage+N), spanning physical columns [startCol,startCol+8).
int tetCharByte( int* str, int strLen, int startPage, int startCol, int physPage, int physCol )
{
    int charIdx = physPage - startPage;
    if( charIdx < 0 || charIdx >= strLen ) return 0;
    if( physCol < startCol || physCol >= startCol + 8 ) return 0;
    int ch = str[ charIdx ];
    if( ch == 0 ) return 0;
    int idx = tetFontIndex( ch );
    int col = physCol - startCol;
    return tetFont[ idx * 8 + ( 7 - col ) ];
}

int tetDigitByte( int digit, int startPage, int startCol, int physPage, int physCol )
{
    if( physPage != startPage ) return 0;
    if( physCol < startCol || physCol >= startCol + 8 ) return 0;
    int col = physCol - startCol;
    return tetFont[ ( 4 + digit ) * 8 + ( 7 - col ) ];
}

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

int tetSeqActive;
int* tetSeqNotes;
int tetSeqCount;
int tetSeqIndex;
int tetSeqWaitFrames;

void tetStartNoteSeq( int* notes, int count )
{
    tetSeqNotes = notes;
    tetSeqCount = count;
    tetSeqIndex = 0;
    tetSeqActive = 1;
    tetSeqWaitFrames = 0;
}

void tetAdvanceNoteSeq()
{
    if( !tetSeqActive ) return;
    if( tetSeqWaitFrames > 0 ) { tetSeqWaitFrames--; return; }
    if( tetSeqIndex >= tetSeqCount ) { tetSeqActive = 0; return; }
    int freq = tetSeqNotes[ tetSeqIndex * 2 ];
    int dur = tetSeqNotes[ tetSeqIndex * 2 + 1 ];
    Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    tetSeqWaitFrames = waitFrames;
    tetSeqIndex++;
}

// for(i=800;i>200;i-=200) beep(30,i) - line clear "happy sound"
int[8] tetHappyNotes = { 55, 31, 105, 31, 155, 31, 205, 31, };
#define TET_HAPPY_COUNT 4

// for(i=0;i<1000;i+=50) beep(50,i) - game over (same shape as Stacker/
// Breakout/Space Attack's own identical sweep)
int[40] tetGameOverNotes =
{
250,40,243,40,230,40,218,40,205,40,193,40,180,40,168,40,155,40,143,40,
130,40,118,40,105,40,93,40,80,40,68,40,55,40,43,40,30,40,18,40,
};
#define TET_GAMEOVER_COUNT 20

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

#define TET_HORIZ 10
#define TET_VERTDRAW 19
#define TET_VERTMAX 24
#define TET_STARTX 3
#define TET_STARTY 19
#define TET_STARTLEVEL 3
#define TET_LEVELFACTOR 4
#define TET_DROPDELAY 600

struct TetPiece
{
    int[4][4] blocks;
    int row, column;
};

TetPiece tetCurrent, tetOld, tetGhostPiece;

bool[10][24] tetBlockGrid;
bool[10][24] tetGhostGrid;

int[8][2] tetNextBlockBuffer;
int tetNextPiece;
int tetScore;
int tetTop;
int tetLevel;
bool tetChallengeMode;
bool tetGhostEnabled;

int tetDropCounter;
int tetDropFrames;

bool tetPrevLeft, tetPrevRight, tetPrevRotate;

bool tetReadBlock( int x, int y ) { return tetBlockGrid[ x ][ y ]; }
void tetWriteBlock( int x, int y, bool value ) { tetBlockGrid[ x ][ y ] = value; }
bool tetReadGhost( int x, int y ) { return tetGhostGrid[ x ][ y ]; }
void tetWriteGhost( int x, int y, bool value ) { tetGhostGrid[ x ][ y ] = value; }

void tetFillGrid( bool value, bool ghostMode )
{
    int r, c;
    for( r = 0; r < TET_VERTMAX; r++ )
      for( c = 0; c < TET_HORIZ; c++ )
      {
          if( ghostMode ) tetWriteGhost( c, r, value );
          else tetWriteBlock( c, r, value );
      }
}

int tetComputeDropFrames()
{
    int lvl = tetLevel;
    if( lvl * TET_LEVELFACTOR > TET_DROPDELAY ) lvl = TET_DROPDELAY / TET_LEVELFACTOR;
    int frames = ( ( TET_DROPDELAY - lvl * TET_LEVELFACTOR ) * 60 ) / 1000;
    if( frames < 1 ) frames = 1;
    return frames;
}

int tetCheckCollision()
{
    int pieceRow, pieceColumn;
    int c, r;
    pieceColumn = 0;
    for( c = tetCurrent.column; c < tetCurrent.column + 4; c++ )
    {
        pieceRow = 0;
        for( r = tetCurrent.row; r < tetCurrent.row + 4; r++ )
        {
            if( tetCurrent.blocks[ pieceColumn ][ pieceRow ] )
            {
                if( c < 0 ) return 2;
                if( c > 9 ) return 1;
                if( r < 0 ) return 1;
                if( c >= 0 && r >= 0 && c < TET_HORIZ && r < TET_VERTMAX )
                {
                    if( tetReadBlock( c, r ) ) return 1;
                }
            }
            pieceRow++;
        }
        pieceColumn++;
    }
    return 0;
}

void tetDrawPiece( int action ) // 0=DRAW, 1=ERASE
{
    int lxn, lxn2;
    for( lxn = 0; lxn < 4; lxn++ )
      for( lxn2 = 0; lxn2 < 4; lxn2++ )
      {
          if( tetCurrent.blocks[ lxn ][ lxn2 ] == 1 )
            tetWriteBlock( tetCurrent.column + lxn, tetCurrent.row + lxn2, action == 0 );
      }
}

void tetDrawGhost( int action )
{
    int lxn, lxn2;
    for( lxn = 0; lxn < 4; lxn++ )
      for( lxn2 = 0; lxn2 < 4; lxn2++ )
      {
          if( tetGhostPiece.blocks[ lxn ][ lxn2 ] == 1 )
            tetWriteGhost( tetGhostPiece.column + lxn, tetGhostPiece.row + lxn2, action == 0 );
      }
}

bool tetCreateGhost()
{
    int tempRow = tetCurrent.row;

    if( tetCurrent.row < 3 ) return false;

    tetCurrent.row -= 2;
    while( tetCheckCollision() == 0 ) tetCurrent.row--;

    int i, j;
    for( i = 0; i < 4; i++ ) for( j = 0; j < 4; j++ ) tetGhostPiece.blocks[i][j] = tetCurrent.blocks[i][j];
    tetGhostPiece.row = tetCurrent.row + 1;
    tetGhostPiece.column = tetCurrent.column;
    tetCurrent.row = tempRow;

    if( tetGhostPiece.row > tetCurrent.row - 3 ) return false;
    return true;
}

void tetLoadPiece( int pieceNumber, int row, int column )
{
    int incr = 0;
    pieceNumber--;
    int lxn, lxn2;
    for( lxn = 0; lxn < 4; lxn++ )
      for( lxn2 = 0; lxn2 < 4; lxn2++ )
      {
          if( ( ( 1 << incr ) & tetBlocks[ pieceNumber ] ) >> incr == 1 ) tetCurrent.blocks[lxn][lxn2] = 1;
          else tetCurrent.blocks[lxn][lxn2] = 0;
          incr++;
      }
    tetCurrent.row = row;
    tetCurrent.column = column;
}

void tetSetNextBlock( int pieceNumber )
{
    int r, c;
    for( r = 0; r < 8; r++ ) for( c = 0; c < 2; c++ ) tetNextBlockBuffer[r][c] = 0;

    pieceNumber--;
    if( pieceNumber == 0 )
    {
        int k;
        for( k = 2; k < 6; k++ )
        {
            tetNextBlockBuffer[k][0] = tetMiniBlock[ pieceNumber * 4 + 0 ];
            tetNextBlockBuffer[k][1] = tetMiniBlock[ pieceNumber * 4 + 0 ];
        }
    }
    else
    {
        int k;
        for( k = 0; k < 3; k++ )
        {
            tetNextBlockBuffer[k][0] = tetMiniBlock[ pieceNumber * 4 + 0 ];
            tetNextBlockBuffer[k][1] = tetMiniBlock[ pieceNumber * 4 + 1 ];
        }
        for( k = 4; k < 7; k++ )
        {
            tetNextBlockBuffer[k][0] = tetMiniBlock[ pieceNumber * 4 + 2 ];
            tetNextBlockBuffer[k][1] = tetMiniBlock[ pieceNumber * 4 + 3 ];
        }
    }
}

void tetRotatePiece()
{
    int[4][4] blocks;
    tetOld = tetCurrent;

    int i, j;
    for( i = 0; i < 4; i++ )
      for( j = 0; j < 4; j++ )
        blocks[j][i] = tetCurrent.blocks[ 4 - i - 1 ][ j ];

    for( i = 0; i < 4; i++ ) for( j = 0; j < 4; j++ ) tetCurrent.blocks[i][j] = blocks[i][j];

    if( tetCheckCollision() ) tetCurrent = tetOld;
    else
    {
        tetDrawGhost( 1 );
        if( tetCreateGhost() ) tetDrawGhost( 0 );
    }
}

void tetMovePieceLeft()
{
    tetOld = tetCurrent;
    tetCurrent.column = tetCurrent.column - 1;
    if( tetCheckCollision() ) tetCurrent = tetOld;
    else
    {
        tetDrawGhost( 1 );
        if( tetCreateGhost() ) tetDrawGhost( 0 );
    }
}

void tetMovePieceRight()
{
    tetOld = tetCurrent;
    tetCurrent.column = tetCurrent.column + 1;
    if( tetCheckCollision() ) tetCurrent = tetOld;
    else
    {
        tetDrawGhost( 1 );
        if( tetCreateGhost() ) tetDrawGhost( 0 );
    }
}

// Direct 1:1 translation of upstream's own scan/clear/shift/recheck loop -
// already correctly handles multiple simultaneous full rows via its own
// "recheck the same index" behavior. Returns the number of rows cleared.
int tetClearFullRows()
{
    int totalRows = 0;
    int row, col;
    for( row = 0; row < TET_VERTMAX; row++ )
    {
        bool rowFull = true;
        for( col = 0; col < TET_HORIZ; col++ ) if( !tetReadBlock( col, row ) ) rowFull = false;

        if( rowFull )
        {
            totalRows++;
            for( col = 0; col < TET_HORIZ; col++ ) tetWriteBlock( col, row, false );
            int dropCol, dropRow;
            for( dropCol = 0; dropCol < TET_HORIZ; dropCol++ )
              for( dropRow = row; dropRow < TET_VERTMAX - 1; dropRow++ )
                tetWriteBlock( dropCol, dropRow, tetReadBlock( dropCol, dropRow + 1 ) );
            row--;
        }
    }
    return totalRows;
}

// Set by tetMovePieceDown() itself the instant a freshly-spawned piece is
// found to already collide with the stack - the *only* correct moment to
// detect game-over, since checking collision at any point *after* the
// caller's own trailing tetDrawPiece(DRAW) has committed the current piece
// into the grid would trivially "collide" with itself every single time
// (a real bug this project's own user testing caught immediately: the
// game ended on literally the first drop, since an earlier draft had the
// caller redundantly re-checking collision post-draw instead of reading
// this flag).
bool tetGameOverFlag;

// Returns true if the piece landed this tick (a fresh piece/next-piece
// load happened, or the game ended - check tetGameOverFlag to tell which).
bool tetMovePieceDown()
{
    tetGameOverFlag = false;
    tetOld = tetCurrent;
    tetCurrent.row--;

    if( tetCheckCollision() )
    {
        tetCurrent.row = tetOld.row;
        tetDrawPiece( 0 );

        int totalRows = tetClearFullRows();
        tetLevel += totalRows;
        if( totalRows == 1 ) tetScore += 40;
        else if( totalRows == 2 ) tetScore += 100;
        else if( totalRows == 3 ) tetScore += 300;
        else if( totalRows == 4 ) tetScore += 800;
        if( totalRows > 0 ) tetStartNoteSeq( tetHappyNotes, TET_HAPPY_COUNT );

        tetLoadPiece( tetNextPiece, TET_STARTY, TET_STARTX );
        if( tetCheckCollision() )
        {
            tetGameOverFlag = true;
            return true;
        }
        tetDrawGhost( 1 );
        if( tetCreateGhost() ) tetDrawGhost( 0 );
        tetNextPiece = arand( 7 ) + 1;
        tetSetNextBlock( tetNextPiece );

        tetDropFrames = tetComputeDropFrames();
        return true;
    }

    tetDrawGhost( 1 );
    if( tetCreateGhost() ) tetDrawGhost( 0 );
    return false;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

// physPage(0-7) -> logical column "reader" (this page shows reader AND
// reader+1, matching upstream's own 2-logical-column-per-physical-page
// overlap scheme). Column 10 (reader+1 at physPage 7) doesn't exist -
// guarded to read as empty rather than out of bounds.
int tetReaderForPage( int page )
{
    if( page < 4 ) return page;
    if( page < 7 ) return page + 1;
    return page + 2;
}

bool tetBlockAt( int col, int row )
{
    if( col < 0 || col >= TET_HORIZ || row < 0 || row >= TET_VERTMAX ) return false;
    return tetReadBlock( col, row );
}

bool tetGhostAt( int col, int row )
{
    if( col < 0 || col >= TET_HORIZ || row < 0 || row >= TET_VERTMAX ) return false;
    return tetReadGhost( col, row );
}

int tetBorderByte( int physCol, int physPage )
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

int tetGameByte( int physCol, int physPage )
{
    int row = physCol / 6;
    int sub = physCol % 6;
    if( row >= TET_VERTDRAW ) return 0;

    int reader = tetReaderForPage( physPage );

    int base = 0;
    if( physPage == 0 ) base = 0x01;
    else if( physPage == 7 ) base = 0x80;

    if( sub == 5 ) return base; // separator column - wall bit only, no block overlay

    int val = base;
    if( tetBlockAt( reader, row ) ) val = val | tetBlockout[ physPage * 2 ];
    if( tetBlockAt( reader + 1, row ) ) val = val | tetBlockout[ physPage * 2 + 1 ];

    if( tetGhostEnabled )
    {
        if( tetGhostAt( reader, row ) )
        {
            if( sub == 0 || sub == 4 ) val = val | tetBlockout[ physPage * 2 ];
            else val = val | tetGhostout[ physPage * 2 ];
        }
        if( tetGhostAt( reader + 1, row ) )
        {
            if( sub == 0 || sub == 4 ) val = val | tetBlockout[ physPage * 2 + 1 ];
            else val = val | tetGhostout[ physPage * 2 + 1 ];
        }
    }

    return val;
}

int tetPreviewByte( int physCol, int physPage )
{
    if( physPage != 6 && physPage != 7 ) return 0;
    if( physCol < TET_VERTDRAW * 6 || physCol >= TET_VERTDRAW * 6 + 8 ) return 0;
    int blockline = physCol - TET_VERTDRAW * 6;
    return tetNextBlockBuffer[ blockline ][ physPage - 6 ];
}

// Per-page composite buffer for PLAYING mode (see tetComposeGameRow below) -
// avoids recomputing the same tetBlockAt/tetGhostAt lookup 6 times per board
// row (once per sub-column) the way a naive per-pixel query would, the same
// "cache what doesn't change every pixel" lesson already applied elsewhere
// in this project (Tiny Doc/Tiny DDug's own row-scoped caching).
int[128] tetPageBuffer;

void tetComposeGameRow( int physPage )
{
    int base = 0;
    if( physPage == 0 ) base = 0x01;
    else if( physPage == 7 ) base = 0x80;

    int reader = tetReaderForPage( physPage );
    int blockOutA = tetBlockout[ physPage * 2 ];
    int blockOutB = tetBlockout[ physPage * 2 + 1 ];
    int ghostOutA = tetGhostout[ physPage * 2 ];
    int ghostOutB = tetGhostout[ physPage * 2 + 1 ];

    int row;
    for( row = 0; row < TET_VERTDRAW; row++ )
    {
        bool blockA = tetBlockAt( reader, row );
        bool blockB = tetBlockAt( reader + 1, row );
        bool ghostA = tetGhostEnabled && tetGhostAt( reader, row );
        bool ghostB = tetGhostEnabled && tetGhostAt( reader + 1, row );

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
            tetPageBuffer[ physCol ] = val | tetBorderByte( physCol, physPage );
        }
        int sepCol = row * 6 + 5;
        tetPageBuffer[ sepCol ] = base | tetBorderByte( sepCol, physPage );
    }

    int physCol;
    for( physCol = TET_VERTDRAW * 6; physCol < 128; physCol++ )
        tetPageBuffer[ physCol ] = tetBorderByte( physCol, physPage ) | tetPreviewByte( physCol, physPage );
}

#define TET_MODE_ATTRACT   0
#define TET_MODE_PLAYING   1
#define TET_MODE_GAMEOVER  2

int* tetAttractMsg1;
int tetAttractMsg1Len;
int tetAttractMsg1Page;
int tetAttractMsg1Col;
int* tetAttractMsg2;
int tetAttractMsg2Len;
int tetAttractMsg2Page;
int tetAttractMsg2Col;
int tetAttractMsgWaitFrames;

bool tetNewHigh;
bool tetScoreBlank;
int tetGameOverWaitFrames;
int tetGameOverBlinkCount;

void tetRenderFrame( int mode )
{
    md_beginFrame();
    int physCol, physPage;
    for( physPage = 0; physPage < 8; physPage++ )
    {
        if( mode == TET_MODE_PLAYING )
        {
            tetComposeGameRow( physPage );
            for( physCol = 0; physCol < 128; physCol++ )
                md_drawColumn( physCol, physPage, tetPageBuffer[ physCol ] );
            continue;
        }

        for( physCol = 0; physCol < 128; physCol++ )
        {
            int val = 0;
            if( mode == TET_MODE_ATTRACT )
            {
                val = val | tetBorderByte( physCol, physPage );
                if( physCol >= 78 && physCol < 114 )
                  val = val | tetBrickLogo[ physPage * 36 + ( physCol - 78 ) ];

                // Each tetCharByte call below is only ever nonzero within its
                // own exact (startPage,startPage+strLen) x [startCol,startCol+8)
                // footprint - gating the call site to that same range (a
                // literal duplicate of tetCharByte's own internal bounds
                // check, not an approximation) avoids paying a full function
                // call for a guaranteed no-op on the other ~7/8 of pixels,
                // the same "self-gated call still costs a full call every
                // time it's invoked" lesson found repeatedly elsewhere in
                // this project (Arkanoid/Bert/Tris/Trick/Morpion).
                if( physPage >= 1 && physPage < 7 )
                {
                    if( physCol >= 64 && physCol < 72 )
                      val = val | tetCharByte( "BLOCKS", 6, 1, 64, physPage, physCol );
                    if( physCol >= 48 && physCol < 56 )
                      val = val | tetCharByte( "Attiny", 6, 1, 48, physPage, physCol );
                    if( physCol >= 40 && physCol < 48 )
                      val = val | tetCharByte( "Arcade", 6, 1, 40, physPage, physCol );
                }
                if( tetAttractMsg1Len > 0
                    && physPage >= tetAttractMsg1Page && physPage < tetAttractMsg1Page + tetAttractMsg1Len
                    && physCol >= tetAttractMsg1Col && physCol < tetAttractMsg1Col + 8 )
                  val = val | tetCharByte( tetAttractMsg1, tetAttractMsg1Len, tetAttractMsg1Page, tetAttractMsg1Col, physPage, physCol );
                if( tetAttractMsg2Len > 0
                    && physPage >= tetAttractMsg2Page && physPage < tetAttractMsg2Page + tetAttractMsg2Len
                    && physCol >= tetAttractMsg2Col && physCol < tetAttractMsg2Col + 8 )
                  val = val | tetCharByte( tetAttractMsg2, tetAttractMsg2Len, tetAttractMsg2Page, tetAttractMsg2Col, physPage, physCol );
            }
            else // TET_MODE_GAMEOVER
            {
                val = val | tetBorderByte( physCol, physPage );
                // Same call-site-gating treatment as the attract text above -
                // tetDigitByte is only ever nonzero on its own exact page and
                // an 8-column span.
                if( !tetScoreBlank && physCol >= 80 && physCol < 88 )
                {
                    if( physPage == 1 ) val = val | tetDigitByte( ( tetScore / 100000 ) % 10, 1, 80, physPage, physCol );
                    else if( physPage == 2 ) val = val | tetDigitByte( ( tetScore / 10000 ) % 10, 2, 80, physPage, physCol );
                    else if( physPage == 3 ) val = val | tetDigitByte( ( tetScore / 1000 ) % 10, 3, 80, physPage, physCol );
                    else if( physPage == 4 ) val = val | tetDigitByte( ( tetScore / 100 ) % 10, 4, 80, physPage, physCol );
                    else if( physPage == 5 ) val = val | tetDigitByte( ( tetScore / 10 ) % 10, 5, 80, physPage, physCol );
                    else if( physPage == 6 ) val = val | tetDigitByte( tetScore % 10, 6, 80, physPage, physCol );
                }
                // Matches upstream exactly: the high score only ever blanks
                // during the blink cycle if this round actually set a new
                // high score (`if(newHigh) displayScore(topScore,...,1);` -
                // only called at all when newHigh) - otherwise it stays
                // statically shown throughout, never blanked.
                if( !( tetNewHigh && tetScoreBlank ) && physCol >= 40 && physCol < 48 )
                {
                    if( physPage == 1 ) val = val | tetDigitByte( ( tetTop / 100000 ) % 10, 1, 40, physPage, physCol );
                    else if( physPage == 2 ) val = val | tetDigitByte( ( tetTop / 10000 ) % 10, 2, 40, physPage, physCol );
                    else if( physPage == 3 ) val = val | tetDigitByte( ( tetTop / 1000 ) % 10, 3, 40, physPage, physCol );
                    else if( physPage == 4 ) val = val | tetDigitByte( ( tetTop / 100 ) % 10, 4, 40, physPage, physCol );
                    else if( physPage == 5 ) val = val | tetDigitByte( ( tetTop / 10 ) % 10, 5, 40, physPage, physCol );
                    else if( physPage == 6 ) val = val | tetDigitByte( tetTop % 10, 6, 40, physPage, physCol );
                }
            }
            md_drawColumn( physCol, physPage, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define TET_STATE_ATTRACT  0
#define TET_STATE_PLAYING  1
#define TET_STATE_GAMEOVER 2

int tetState;
int tetFireHeld;
int tetFireHoldTicks;
int tetGestureDone;

void tetClearAttractMsg()
{
    tetAttractMsg1Len = 0;
    tetAttractMsg2Len = 0;
}

void tetBeginAttract()
{
    tetFireHeld = 0;
    tetFireHoldTicks = 0;
    tetGestureDone = 0;
    tetClearAttractMsg();
    tetState = TET_STATE_ATTRACT;
}

void tetBeginGame()
{
    tetScore = 0;
    tetLevel = TET_STARTLEVEL;
    tetFillGrid( false, false );
    tetFillGrid( false, true );

    tetLoadPiece( arand( 7 ) + 1, TET_STARTY, TET_STARTX );
    tetDrawPiece( 0 );
    if( tetCreateGhost() ) tetDrawGhost( 0 );
    tetNextPiece = arand( 7 ) + 1;
    tetSetNextBlock( tetNextPiece );

    if( tetChallengeMode )
    {
        int cl;
        for( cl = 0; cl < 100; cl++ )
        {
            tetDrawPiece( 1 );
            tetMovePieceDown();
            if( ( arand( 7 ) + 1 ) > 4 ) tetMovePieceLeft();
            tetDrawPiece( 0 );
        }
        tetLevel = TET_STARTLEVEL;
    }

    tetDropFrames = tetComputeDropFrames();
    tetDropCounter = tetDropFrames;
    tetPrevLeft = false;
    tetPrevRight = false;
    tetPrevRotate = false;

    tetState = TET_STATE_PLAYING;
}

void tetBeginGameOver()
{
    if( tetScore > tetTop )
    {
        tetTop = tetScore;
        tetNewHigh = true;
    }
    else tetNewHigh = false;

    tetScoreBlank = false;
    tetGameOverBlinkCount = 0;
    tetGameOverWaitFrames = 12; // ~200ms at 60fps, matches upstream's own delay(200)
    tetStartNoteSeq( tetGameOverNotes, TET_GAMEOVER_COUNT );
    tetState = TET_STATE_GAMEOVER;
}

void gameFallingBlocks_init()
{
    tetTop = 0;
    tetChallengeMode = false;
    tetGhostEnabled = true;
    tetSeqActive = 0;
    tetBeginAttract();
}

void gameFallingBlocks_forceRedraw()
{
    if( tetState == TET_STATE_ATTRACT ) tetRenderFrame( TET_MODE_ATTRACT );
    else if( tetState == TET_STATE_PLAYING ) tetRenderFrame( TET_MODE_PLAYING );
    else tetRenderFrame( TET_MODE_GAMEOVER );
}

void gameFallingBlocks_update()
{
    tetAdvanceNoteSeq();

    if( tetState == TET_STATE_ATTRACT )
    {
        int fireDown = isFirePressed();
        if( fireDown )
        {
            tetFireHoldTicks++;
            if( tetFireHoldTicks >= 120 && !tetGestureDone )
            {
                tetGestureDone = 1;
                if( isDownPressed() )
                {
                    tetChallengeMode = !tetChallengeMode;
                    tetAttractMsg1 = "MODE"; tetAttractMsg1Len = 4; tetAttractMsg1Page = 2; tetAttractMsg1Col = 8;
                    if( tetChallengeMode ) { tetAttractMsg2 = "HARD"; tetAttractMsg2Len = 4; tetAttractMsg2Page = 2; tetAttractMsg2Col = 16; }
                    else { tetAttractMsg2 = "NORMAL"; tetAttractMsg2Len = 6; tetAttractMsg2Page = 1; tetAttractMsg2Col = 16; }
                }
                else
                {
                    tetGhostEnabled = !tetGhostEnabled;
                    tetAttractMsg1 = "GHOST"; tetAttractMsg1Len = 5; tetAttractMsg1Page = 1; tetAttractMsg1Col = 16;
                    if( tetGhostEnabled ) { tetAttractMsg2 = "ON"; tetAttractMsg2Len = 2; tetAttractMsg2Page = 2; tetAttractMsg2Col = 8; }
                    else { tetAttractMsg2 = "OFF"; tetAttractMsg2Len = 3; tetAttractMsg2Page = 2; tetAttractMsg2Col = 8; }
                }
                tetAttractMsgWaitFrames = 90;
            }
        }
        else
        {
            if( tetFireHeld && !tetGestureDone )
            {
                tetFireHeld = fireDown;
                tetBeginGame();
                tetRenderFrame( TET_MODE_PLAYING );
                return;
            }
            tetFireHoldTicks = 0;
            tetGestureDone = 0;
        }
        tetFireHeld = fireDown;

        if( tetAttractMsg1Len > 0 )
        {
            tetAttractMsgWaitFrames--;
            if( tetAttractMsgWaitFrames <= 0 ) tetClearAttractMsg();
        }

        tetRenderFrame( TET_MODE_ATTRACT );
    }
    else if( tetState == TET_STATE_PLAYING )
    {
        int rotateNow = isFirePressed() || isUpPressed();

        if( isLeftPressed() && !tetPrevLeft ) { tetDrawPiece( 1 ); tetMovePieceLeft(); tetDrawPiece( 0 ); }
        else if( isRightPressed() && !tetPrevRight ) { tetDrawPiece( 1 ); tetMovePieceRight(); tetDrawPiece( 0 ); }

        if( rotateNow && !tetPrevRotate ) { tetDrawPiece( 1 ); tetRotatePiece(); tetDrawPiece( 0 ); }

        tetPrevLeft = isLeftPressed();
        tetPrevRight = isRightPressed();
        tetPrevRotate = rotateNow;

        bool doDrop = false;
        if( isDownPressed() && tetCurrent.row < TET_STARTY - 5 ) doDrop = true;
        tetDropCounter--;
        if( tetDropCounter <= 0 ) { doDrop = true; tetDropCounter = tetDropFrames; }

        if( doDrop )
        {
            tetDrawPiece( 1 );
            tetMovePieceDown();
            tetDrawPiece( 0 );

            if( tetGameOverFlag )
            {
                tetBeginGameOver();
                tetRenderFrame( TET_MODE_GAMEOVER );
                return;
            }
        }

        tetRenderFrame( TET_MODE_PLAYING );
    }
    else // TET_STATE_GAMEOVER
    {
        if( tetSeqActive ) { tetRenderFrame( TET_MODE_GAMEOVER ); return; }

        tetGameOverWaitFrames--;
        if( tetGameOverWaitFrames <= 0 )
        {
            tetGameOverWaitFrames = 12;
            tetScoreBlank = !tetScoreBlank;
            tetGameOverBlinkCount++;
            if( tetGameOverBlinkCount >= 8 )
            {
                tetBeginAttract();
                tetRenderFrame( TET_MODE_ATTRACT );
                return;
            }
        }
        tetRenderFrame( TET_MODE_GAMEOVER );
    }
}

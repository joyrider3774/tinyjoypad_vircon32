// =============================================================================
// ATtiny Tetromino (Franco Trimboli, GitHub "sunpazed", GPLv3) - an enhanced
// falling-block puzzle game built directly on `jfoucher/attiny-tetris`
// (Jonathan Foucher's own "probably the smallest tetris game in the world",
// no license stated) - a 7-bag randomiser, NES-matched level speed curve,
// a lines/levels counter, and a restart gesture were all added on top by
// sunpazed's own fork. From `more games/attiny-tetromino/` - staged during
// the "very very deep scan" search batch, confirmed via a direct
// feasibility audit (per this project's own established "check if it
// extends an existing port already" discipline) to be a completely
// separate codebase from Andy Jackson's own TinyTetris/`font8x8AJ.h`
// lineage already shipped twice in this project (Falling Blocks, Blocks
// Gold) - built on the `Tiny4kOLED` Arduino library's own page/cursor API
// instead, with its own from-scratch board representation and font, zero
// shared functions or data tables with the Andy-Jackson family.
//
// **Confirmed via direct source reading (`oled.begin()`/
// `page_pixels[SCREEN_HEIGHT][4]`) that this targets a genuine 128x32
// display, not 128x64** - a real, different screen layout from every
// other Tetris-family game in this project, not just a re-skin: this
// game's own board is noticeably shorter/more compressed on real
// hardware. Placed within Vircon32's fixed 128x64 canvas the same way
// Tiny Bulls And Cows' own native-128x64-but-half-height UI was handled -
// gameplay only ever touches pages 0-3 (the top half), with pages 4-7
// explicitly redrawn blank every frame rather than left untouched, to
// avoid the VRAM-persistence bug class this project has hit repeatedly
// elsewhere.
//
// **The board is genuinely rendered rotated 90 degrees from what a
// "normal" top-down Tetris view would look like** - confirmed by tracing
// the real byte layout, not assumed: `draw()`'s own outer loop iterates
// hardware PAGE (`p`, 0-3), and for each page streams 24 real *columns*
// worth of data (`repeatData(page_pixels[y][p], 3)`), meaning the board's
// own Y axis (gravity/falling direction) maps to real screen *columns*,
// while the board's own X axis (native Left/Right movement) maps to real
// screen *pages* (vertical position). On an unrotated Vircon32 screen, a
// piece falling under gravity visually drifts left-to-right across the
// display, and upstream's own Left/Right buttons visually move a piece
// *up/down*, not left/right. **Controls were deliberately remapped to
// match what the player actually sees, not upstream's literal button
// wiring** (confirmed with the user directly, since this is a genuine
// UX judgment call with no single obviously-correct answer, not something
// derivable from the code alone) - Up/Down move the piece (matching their
// own real up/down motion on screen), Fire rotates, Left/Right both soft-
// drop. This is a deliberate departure from the "preserve the native
// button wiring untouched" precedent Falling Blocks/Blocks Gold used for
// their own sideways-rendered boards - those two games' own native wiring
// already happened to feel natural post-port; this one's didn't, so it
// was remapped instead, per direct user instruction.
//
// Not `tinyJoypadShim`/`obonoCoreShim` lineage - genuine bespoke hardware
// (3 discrete digital-pin buttons, plus a clever 4th "DROP" button wired
// through the RESET pin via a 470k resistor and read back via
// `analogRead(0)` - a real ATtiny85 pin-repurposing trick with no direct
// Vircon32 equivalent, mapped onto a normal button read instead of
// replicated literally). No new shim needed beyond that - `isUpPressed()`/
// `isDownPressed()`/`isLeftPressed()`/`isRightPressed()`/`isFirePressed()`/
// `arand()` already cover the whole input/RNG surface.
//
// **A real AVR-specific software-reset trick, not portable at all**:
// upstream's own `void(*resetFunc)(void) = 0;` followed by `resetFunc();`
// is a classic AVR trick - calling through a null function pointer jumps
// to address 0, the reset vector, restarting the whole program. Ported as
// a normal state-machine transition back to the attract screen instead
// (matching this project's own established convention), not attempted
// literally - a null-pointer call has no meaningful equivalent on
// Vircon32 and would simply crash.
//
// **Timing is genuinely real-time** (`millis()`-gated fall/move/rotate
// intervals, a per-level speed table in real milliseconds tuned to match
// the NES's own Tetris speed curve) - ported as frame-counted equivalents
// at Vircon32's real 60fps (`trmoMsToFrames()`), preserving the exact
// same per-level table structure rather than collapsing it into a
// simplified formula, since this speed curve is a real, deliberately-
// tuned design element (sunpazed's own changelog explicitly calls out
// "tweaked speed timing for levels to match NES").
//
// **A diagonally-shifted, dual-digit-per-page number rendering
// technique**, used for the score/high-score/level+lines displays: each
// of the 4 hardware pages shows a *blend* of two adjacent decimal digits,
// bit-shifted by different amounts per page (`shifts[]`) and OR-combined
// - not a simple "one digit per page" layout. Traced the exact divisor-
// stepping logic (including a deliberate `if (l==1) divisor *= 10;`
// double-step quirk that skips one digit's own "primary" page slot) and
// ported it verbatim rather than reimplementing a more conventional
// digit-layout from scratch, since the real visual result depends on
// this exact bit-interleaving scheme. The intermediate shift values can
// exceed a real byte's own width before the final `& 0xff` mask (already
// present in upstream's own source, since its own intermediate `int`
// variables are AVR's real 16-bit `int`, not `uint8_t`) - kept the same
// explicit mask at the same site, the same byte-truncation-family fix
// this whole project's history starts with, needing no *additional* mask
// beyond what upstream already had.
//
// The 7-bag randomiser, wall-kick rotation (try shifting away from
// whichever wall a rotation collides with; cancel the rotation if it
// collides with both), and line-clear/gravity-cascade logic are all
// direct, faithful translations - none of them touch AVR-narrow-type
// behavior. Upstream's own bit-packed `pixels[]` board (1 bit per cell,
// `SCREEN_WIDTH*SCREEN_HEIGHT/8` bytes) was ported as a plain `bool[16][24]`
// grid instead, matching this project's own established "bit-packing
// saves real AVR RAM but has no benefit on Vircon32, and avoids the
// whole shift-arithmetic hazard class" precedent (Falling Blocks' own
// port made the identical call). Binary literals (`bitmap_lines[]`/
// `bitmap_score[]`, two small decorative label bitmaps) were converted to
// hex, matching this dialect's lack of `0b` literal support. `struct
// activePiece`'s own C++ pointer-reassignment pattern (`active.piece`
// pointed at whichever of several buffers - the spawn table, a rotation
// scratch buffer, etc - was currently relevant) was replaced with one
// persistent, in-place-mutated array instead of chasing pointer
// reassignment through this dialect, avoiding an unproven pattern for a
// purely internal implementation detail with no behavioral difference.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data
// -----------------------------------------------------------------------------

#define TRMO_BOARD_W 16
#define TRMO_BOARD_H 24

// 7 tetromino shapes, 4x4 each - byte-diff-verified against upstream's
// own pieces[7][16].
int[112] trmoPieces =
{
0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0,
0,0,0,0, 0,1,1,0, 0,1,1,0, 0,0,0,0,
0,0,0,0, 0,1,1,0, 0,0,1,1, 0,0,0,0,
0,0,0,0, 0,1,1,0, 1,1,0,0, 0,0,0,0,
0,0,0,0, 1,1,1,0, 0,1,0,0, 0,0,0,0,
0,1,0,0, 0,1,0,0, 1,1,0,0, 0,0,0,0,
0,1,0,0, 0,1,0,0, 0,1,1,0, 0,0,0,0,
};

// Small digit font (10 digits x 6 rows, row 0 always blank) - byte-diff-
// verified against upstream's own numz[].
int[60] trmoDigitFont =
{
0, 12,18,18,18,12, // 0
0, 4,12,4,4,4,     // 1
0, 28,2,12,16,30,  // 2
0, 28,2,12,2,28,   // 3
0, 10,18,30,2,2,   // 4
0, 30,16,28,2,28,  // 5
0, 12,16,28,18,12, // 6
0, 30,2,4,8,8,     // 7
0, 12,18,12,18,12, // 8
0, 12,18,14,2,12,  // 9
};

// Per-page shift amounts for the dual-digit diagonal number rendering -
// byte-diff-verified against upstream's own shifts[].
int[8] trmoShifts = { 0,3,1,4, 5,2,4,1 };

// Two small decorative label bitmaps ("SCORE"/lines-icon area) - byte-
// diff-verified against upstream's own bitmap_lines[]/bitmap_score[],
// converted from binary literals (unsupported by this dialect) to hex.
int[20] trmoBitmapLines =
{
0x48,0xA0,0x45,0x2E,
0x48,0xA0,0x45,0xA8,
0x45,0x20,0x45,0x6C,
0x45,0x20,0x45,0x28,
0x72,0x38,0x75,0x2E,
};

int[20] trmoBitmapScore =
{
0x03,0x18,0xCE,0x70,
0x04,0x25,0x29,0x40,
0x03,0x21,0x29,0x60,
0x00,0xA5,0x2E,0x40,
0x07,0x18,0xC9,0x70,
};

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

bool[16][24] trmoBoard;

int trmoActiveX;
int trmoActiveY;
int trmoActiveId;
int[16] trmoActivePiece;

int trmoNextId;
int[16] trmoNextPiece;

int[7] trmoBag;
int trmoBagIndex;

int trmoScore;
int trmoHighScore;
int trmoLevel;
int trmoLinesCleared;
bool trmoMadeLine;

// Frame-counted equivalents of upstream's own real millis()-gated timers.
int trmoFallCountdown;
int trmoAwaitFrames;
int trmoMoveCooldown;
int trmoRotateCooldown;

// Rounds to the nearest frame at Vircon32's real 60fps.
int trmoMsToFrames( int ms )
{
    int frames = ( ms * 60 + 500 ) / 1000;
    if( frames < 1 ) frames = 1;
    return frames;
}

// -----------------------------------------------------------------------------
//   7-bag randomiser
// -----------------------------------------------------------------------------

void trmoInitBag()
{
    int i;
    for( i = 0; i < 7; i++ ) trmoBag[ i ] = i;
}

void trmoShuffleBag()
{
    int i;
    for( i = 0; i < 7; i++ )
    {
        int j = arand( 7 );
        int temp = trmoBag[ i ];
        trmoBag[ i ] = trmoBag[ j ];
        trmoBag[ j ] = temp;
    }
    trmoBagIndex = 0;
}

int trmoDrawFromBag()
{
    if( trmoBagIndex >= 7 ) trmoShuffleBag();
    int v = trmoBag[ trmoBagIndex ];
    trmoBagIndex++;
    return v;
}

void trmoReadPiece( int* dest, int pieceId )
{
    int i;
    for( i = 0; i < 16; i++ ) dest[ i ] = trmoPieces[ pieceId * 16 + i ];
}

// -----------------------------------------------------------------------------
//   Board / collision
// -----------------------------------------------------------------------------

void trmoInitBoard()
{
    int x, y;
    for( y = 0; y < TRMO_BOARD_H; y++ )
      for( x = 0; x < TRMO_BOARD_W; x++ )
        trmoBoard[ x ][ y ] = ( x == 0 ) || ( x >= 11 ) || ( y == TRMO_BOARD_H - 1 );
}

bool trmoCanMoveDown()
{
    int i, j;
    for( i = 0; i < 4; i++ )
      for( j = 0; j < 4; j++ )
      {
          int bx = trmoActiveX + j;
          int by = trmoActiveY + i + 1;
          if( trmoActivePiece[ i * 4 + j ] && trmoBoard[ bx ][ by ] ) return false;
      }
    return true;
}

bool trmoCanMoveLeft()
{
    int i, j;
    for( i = 0; i < 4; i++ )
      for( j = 0; j < 4; j++ )
      {
          int bx = trmoActiveX + j - 1;
          int by = trmoActiveY + i;
          if( trmoActivePiece[ i * 4 + j ] && trmoBoard[ bx ][ by ] ) return false;
      }
    return true;
}

bool trmoCanMoveRight()
{
    int i, j;
    for( i = 0; i < 4; i++ )
      for( j = 0; j < 4; j++ )
      {
          if( trmoActivePiece[ i * 4 + j ] == 1 )
          {
              int bx = trmoActiveX + j + 1;
              int by = trmoActiveY + i;
              if( trmoBoard[ bx ][ by ] ) return false;
          }
      }
    return true;
}

void trmoRotatePieceInto( int* data, int* newData )
{
    int i, j;
    for( i = 0; i < 4; i++ )
      for( j = 0; j < 4; j++ )
        newData[ j * 4 + 3 - i ] = data[ i * 4 + j ];
}

// -----------------------------------------------------------------------------
//   Game logic
// -----------------------------------------------------------------------------

#define TRMO_LEVEL_DELAY_MS 28

// Direct translation of upstream's own getAwait() - a per-level real-ms
// speed table (tuned to match the NES's own Tetris curve, per sunpazed's
// changelog), converted to frames at the exact same site rather than
// pre-baked into a separate lookup table, so the level-to-multiplier
// mapping stays visually identical to upstream's own if/else chain.
void trmoGetAwait()
{
    if( trmoLevel == 0 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 17 );
    else if( trmoLevel == 1 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 16 );
    else if( trmoLevel == 2 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 15 );
    else if( trmoLevel == 3 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 14 );
    else if( trmoLevel == 4 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 13 );
    else if( trmoLevel == 5 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 11 );
    else if( trmoLevel == 6 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 10 );
    else if( trmoLevel == 7 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 8 );
    else if( trmoLevel == 8 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 7 );
    else if( trmoLevel == 9 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 6 );
    else if( trmoLevel == 10 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 5 );
    else if( trmoLevel <= 13 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 4 );
    else if( trmoLevel <= 16 ) trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS * 2 );
    else trmoAwaitFrames = trmoMsToFrames( TRMO_LEVEL_DELAY_MS );
}

void trmoSpawnPiece()
{
    trmoActiveX = 4;
    trmoActiveY = 0;
    trmoActiveId = trmoNextId;
    int i;
    for( i = 0; i < 16; i++ ) trmoActivePiece[ i ] = trmoNextPiece[ i ];

    trmoNextId = trmoDrawFromBag();
    trmoReadPiece( trmoNextPiece, trmoNextId );
}

void trmoInitGame()
{
    trmoInitBoard();
    trmoScore = 0;
    trmoLevel = 0;
    trmoLinesCleared = 0;
    trmoMadeLine = false;

    trmoInitBag();
    trmoShuffleBag();

    trmoNextId = trmoDrawFromBag();
    trmoReadPiece( trmoNextPiece, trmoNextId );
    trmoSpawnPiece();

    trmoGetAwait();
    trmoFallCountdown = trmoAwaitFrames;
    trmoMoveCooldown = 0;
    trmoRotateCooldown = 0;
}

// Locks the active piece into the board, clears any full lines, and
// spawns the next piece - returns true if the new piece's spawn position
// is already blocked (game over), matching upstream's own
// `active.y <= 1` check.
bool trmoLockPieceAndAdvance()
{
    int i, j;
    for( i = 0; i < 4; i++ )
      for( j = 0; j < 4; j++ )
        if( trmoActivePiece[ i * 4 + j ] == 1 )
          trmoBoard[ trmoActiveX + j ][ trmoActiveY + i ] = true;

    int nlines = 0;
    int y;
    for( y = 0; y < TRMO_BOARD_H - 1; y++ )
    {
        bool full = true;
        int x;
        for( x = 0; x < TRMO_BOARD_W; x++ )
          if( !trmoBoard[ x ][ y ] ) full = false;

        if( full )
        {
            nlines++;
            trmoMadeLine = true;
            int y2;
            for( y2 = y; y2 >= 2; y2-- )
            {
                int xx;
                for( xx = 0; xx < TRMO_BOARD_W; xx++ )
                  trmoBoard[ xx ][ y2 ] = trmoBoard[ xx ][ y2 - 1 ];
            }
        }
    }

    if( nlines > 0 )
    {
        trmoLinesCleared += nlines;
        if( nlines == 1 ) trmoScore += 40 * ( trmoLevel + 1 );
        else if( nlines == 2 ) trmoScore += 100 * ( trmoLevel + 1 );
        else if( nlines == 3 ) trmoScore += 300 * ( trmoLevel + 1 );
        else if( nlines == 4 ) trmoScore += 1200 * ( trmoLevel + 1 );
    }

    bool gameOver = ( trmoActiveY <= 1 );

    if( trmoMadeLine )
      if( trmoScore > trmoHighScore ) trmoHighScore = trmoScore;

    trmoSpawnPiece();

    if( trmoLinesCleared > ( trmoLevel + 1 ) * 10 ) trmoLevel++;

    return gameOver;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------
//
// Everything below composites once per real hardware page (0-3), not once
// per pixel/column - matching this project's own standing "avoid
// O(pixels x objects)" lesson applied proactively from the start, rather
// than retrofitted after a CPU report the way several earlier ports in
// this project needed.

int[128] trmoPageBuffer;

// One board row's own byte for a given page - a direct translation of
// draw()'s own inner per-(y,p) computation, called once per (row, page)
// rather than maintaining upstream's own full page_pixels[24][4] cache
// (which existed there purely to let one single sendData pass emit all
// 4 pages from one board scan - not a saving that matters here, where
// each page is composited independently anyway).
int trmoBoardRowByte( int y, int page )
{
    int value = 0;
    if( page == 0 ) value |= 1;
    if( page == 3 ) value |= 0x80;

    int x;
    for( x = 1; x < 11; x++ )
    {
        bool filled = trmoBoard[ x ][ y ];
        if( ( x >= trmoActiveX ) && ( y >= trmoActiveY ) && ( x < trmoActiveX + 4 ) && ( y < trmoActiveY + 4 ) )
        {
            int ind = ( y - trmoActiveY ) * 4 + ( x - trmoActiveX );
            if( trmoActivePiece[ ind ] & 1 ) filled = true;
        }
        if( filled )
        {
            int pix = x * 3 - 2;
            int k;
            for( k = 0; k < 3; k++ )
            {
                int p = ( pix + k ) / 8;
                if( p == page ) value |= ( 1 << ( ( pix + k ) % 8 ) );
            }
        }
    }
    return value & 0xFF;
}

// Board + active piece - columns 30-102 (a leading border byte at 30,
// then 24 board rows x 3 real columns each).
void trmoComposeBoard( int page )
{
    trmoPageBuffer[ 30 ] |= 0xFF;
    int y;
    for( y = 0; y < TRMO_BOARD_H; y++ )
    {
        int value;
        if( y == TRMO_BOARD_H - 1 ) value = 0xFF;
        else value = trmoBoardRowByte( y, page );

        int base = 31 + y * 3;
        int k;
        for( k = 0; k < 3; k++ ) trmoPageBuffer[ base + k ] |= value;
    }
}

// Next-piece preview - columns 16-27 (4 rows x 3 columns, no border).
void trmoComposeNextPiece( int page )
{
    if( page > 3 ) return;

    int y;
    for( y = 0; y < 4; y++ )
    {
        int value = 0;
        int x;
        for( x = 4; x < 8; x++ )
        {
            int ind = y * 4 + ( x - 4 );
            if( trmoNextPiece[ ind ] & 1 )
            {
                int pix = x * 3 - 2;
                int k;
                for( k = 0; k < 3; k++ )
                {
                    int p = ( pix + k ) / 8;
                    if( p == page ) value |= ( 1 << ( ( pix + k ) % 8 ) );
                }
            }
        }
        int base = 16 + y * 3;
        int k;
        for( k = 0; k < 3; k++ ) trmoPageBuffer[ base + k ] |= value;
    }
}

// The dual-digit diagonal-shift number display shared by the high-score/
// score/level+lines readouts - direct translation of upstream's own
// divisor-stepping digit-pair selection (including the deliberate
// `if (l==1) divisor *= 10;` double-step at page 1, which intentionally
// skips one digit's own "primary" page slot - see this file's own header
// comment for why this is preserved exactly, not simplified).
void trmoComposeNumber( int value, int startCol, int page, bool suppressPrimaryOnPage2 )
{
    if( page > 3 ) return;

    int divisor = 1;
    int n = 0, nx = 0;
    int l;
    for( l = 0; l <= page; l++ )
    {
        n = ( value / divisor ) % 10;
        divisor *= 10;
        nx = ( value / divisor ) % 10;
        if( l == 1 ) divisor *= 10;
    }

    int i;
    for( i = 1; i < 6; i++ )
    {
        int a = trmoDigitFont[ n * 6 + i ] >> trmoShifts[ page ];
        int b = trmoDigitFont[ nx * 6 + i ] << trmoShifts[ page + 4 ];
        if( suppressPrimaryOnPage2 && page == 2 ) a = 0;
        int val = ( a | b ) & 0xFF;
        trmoPageBuffer[ startCol + i - 1 ] |= val;
    }
}

// The small "SCORE"/lines-icon decorative label bitmaps - drawn with a
// reversed page order (upstream's own `setCursor(X, 3-l)`).
void trmoComposeLabel( int* table, int startCol, int page )
{
    if( page > 3 ) return;
    int l = 3 - page;
    int col;
    for( col = 0; col < 5; col++ )
      trmoPageBuffer[ startCol + col ] |= table[ l + col * 4 ];
}

void trmoComposeRow( int page )
{
    int col;
    for( col = 0; col < 128; col++ ) trmoPageBuffer[ col ] = 0;

    if( page > 3 ) return; // gameplay only ever touches the top half

    trmoComposeLabel( trmoBitmapLines, 1, page );
    trmoComposeNumber( trmoLevel * 10000 + trmoLinesCleared, 8, page, true );
    trmoComposeNextPiece( page );
    trmoComposeBoard( page );
    trmoComposeLabel( trmoBitmapScore, 106, page );
    trmoComposeNumber( trmoScore, 113, page, false );
    trmoComposeNumber( trmoHighScore, 121, page, false );
}

// Standard 95-char font, already proven for several ports in this project
// - reused for the attract screen, each game keeping its own self-
// contained copy per this project's convention.
int[570] trmoFont =
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

int trmoFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return trmoFont[ ( ch - 32 ) * 6 + col ];
}

int trmoTextByteAt( int* text, int textLen, int startX, int x )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= textLen ) return 0;
    return trmoFontByte( text[ charIdx ], rel % 6 );
}

#define TRMO_MODE_ATTRACT 0
#define TRMO_MODE_PLAYING 1

void trmoRenderAttract()
{
    int page, col;
    for( page = 0; page < 8; page++ )
    {
        for( col = 0; col < 128; col++ ) trmoPageBuffer[ col ] = 0;

        if( page == 1 )
        {
            int* t = "ATTINY TETROMINO";
            for( col = 0; col < 128; col++ ) trmoPageBuffer[ col ] |= trmoTextByteAt( t, 16, 16, col );
        }
        if( page == 3 )
        {
            int* t = "UP/DOWN MOVE";
            for( col = 0; col < 128; col++ ) trmoPageBuffer[ col ] |= trmoTextByteAt( t, 12, 28, col );
        }
        if( page == 4 )
        {
            int* t = "FIRE ROTATES";
            for( col = 0; col < 128; col++ ) trmoPageBuffer[ col ] |= trmoTextByteAt( t, 12, 28, col );
        }
        if( page == 5 )
        {
            int* t = "LEFT/RIGHT DROP";
            for( col = 0; col < 128; col++ ) trmoPageBuffer[ col ] |= trmoTextByteAt( t, 15, 19, col );
        }
        if( page == 6 )
        {
            int* t = "BY SUNPAZED";
            for( col = 0; col < 128; col++ ) trmoPageBuffer[ col ] |= trmoTextByteAt( t, 11, 34, col );
        }
        if( page == 7 )
        {
            int* t = "PRESS FIRE";
            for( col = 0; col < 128; col++ ) trmoPageBuffer[ col ] |= trmoTextByteAt( t, 10, 34, col );
        }

        for( col = 0; col < 128; col++ )
          md_drawColumn( col, page, trmoPageBuffer[ col ] );
    }
}

void trmoRenderPlaying()
{
    int page, col;
    for( page = 0; page < 8; page++ )
    {
        trmoComposeRow( page );
        for( col = 0; col < 128; col++ )
          md_drawColumn( col, page, trmoPageBuffer[ col ] );
    }
}

void trmoRenderFrame( int mode )
{
    md_beginFrame();
    if( mode == TRMO_MODE_ATTRACT ) trmoRenderAttract();
    else trmoRenderPlaying();
}

// -----------------------------------------------------------------------------
//   Input / per-tick update
// -----------------------------------------------------------------------------

// Direct translation of gameLoop()'s own per-tick input handling -
// Up/Down move the piece (matching what visually moves up/down on an
// unrotated screen - see this file's own header comment), Fire rotates
// with the same wall-kick logic upstream uses, Left/Right soft-drop.
// Returns true if locking a piece this tick caused a game over.
bool trmoUpdatePlaying()
{
    if( trmoMadeLine )
    {
        trmoMadeLine = false;
    }

    if( trmoMoveCooldown > 0 ) trmoMoveCooldown--;
    else
    {
        bool downNow = isDownPressed();
        bool upNow = isUpPressed();

        if( downNow && trmoCanMoveRight() ) trmoActiveX += 1;
        if( upNow && trmoCanMoveLeft() ) trmoActiveX -= 1;

        if( upNow || downNow ) trmoMoveCooldown = trmoMsToFrames( 100 );
    }

    trmoGetAwait();

    bool dropHeld = isLeftPressed() || isRightPressed();
    if( dropHeld )
    {
        trmoScore += 1;
        trmoAwaitFrames = trmoMsToFrames( 8 );
    }

    bool fireNow = isFirePressed();
    if( fireNow && trmoRotateCooldown <= 0 )
    {
        int[16] np;
        int[16] op;
        int l;
        for( l = 0; l < 16; l++ ) op[ l ] = trmoActivePiece[ l ];

        trmoRotatePieceInto( op, np );
        for( l = 0; l < 16; l++ ) trmoActivePiece[ l ] = np[ l ];

        int oldX = trmoActiveX;
        bool movedLeft = false;
        bool movedRight = false;
        if( !trmoCanMoveRight() ) { trmoActiveX -= 1; movedLeft = true; }
        if( !trmoCanMoveLeft() ) { trmoActiveX += 1; movedRight = true; }

        if( movedLeft && movedRight )
        {
            for( l = 0; l < 16; l++ ) trmoActivePiece[ l ] = op[ l ];
            trmoActiveX = oldX;
        }
        else
        {
            if( movedLeft )
            {
                while( !trmoCanMoveRight() ) trmoActiveX -= 1;
                trmoActiveX += 1;
            }
            if( movedRight )
            {
                while( !trmoCanMoveLeft() ) trmoActiveX += 1;
                trmoActiveX -= 1;
            }
        }

        trmoRotateCooldown = trmoMsToFrames( 160 );
    }
    if( trmoRotateCooldown > 0 ) trmoRotateCooldown--;

    bool gameOver = false;
    trmoFallCountdown--;
    if( trmoFallCountdown <= 0 )
    {
        if( trmoCanMoveDown() ) trmoActiveY += 1;
        else gameOver = trmoLockPieceAndAdvance();

        trmoFallCountdown = trmoAwaitFrames;
    }

    return gameOver;
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define TRMO_STATE_ATTRACT 0
#define TRMO_STATE_PLAYING 1
int trmoState;
bool trmoPrevAttractFire;

// Upstream has no genuine real-time rate of its own to match (the bare
// loop just runs as fast as real AVR hardware executes it, gated only by
// the fall-timer's own real-ms check - the "no timing model whatsoever
// upstream" category already established for several other ports in this
// project) - added at direct user request after reporting the very next
// piece feels like it becomes controllable too abruptly right after the
// previous one locks. Confirmed by re-reading upstream directly: there
// really is no gatekeeper delay there either (a locked piece's own
// replacement is spawned and made controllable within the very same
// tick) - this isn't fixing a porting bug, it's a deliberate slowdown of
// the whole game to make that already-immediate transition feel less
// abrupt. Gates the whole tick (logic + render together), matching the
// majority precedent in this project rather than a movement-only split -
// every existing frame-counted constant (`trmoMoveCooldown`,
// `trmoRotateCooldown`, `trmoFallCountdown`) is deliberately left
// unrescaled, so they simply now take twice as long in real time.
#define TRMO_TICK_DIVISOR 2
int trmoTickCounter;

void trmoBeginAttract()
{
    trmoPrevAttractFire = false;
    trmoState = TRMO_STATE_ATTRACT;
}

void trmoBeginPlaying()
{
    trmoInitGame();
    trmoTickCounter = 0;
    trmoState = TRMO_STATE_PLAYING;
}

void gameAttinyTetromino_init()
{
    trmoHighScore = 0;
    trmoBeginAttract();
}

void gameAttinyTetromino_forceRedraw()
{
    if( trmoState == TRMO_STATE_PLAYING ) trmoRenderFrame( TRMO_MODE_PLAYING );
    else trmoRenderFrame( TRMO_MODE_ATTRACT );
}

void gameAttinyTetromino_update()
{
    if( trmoState == TRMO_STATE_ATTRACT )
    {
        bool fireNow = isFirePressed();
        if( fireNow && !trmoPrevAttractFire )
        {
            trmoBeginPlaying();
            trmoRenderFrame( TRMO_MODE_PLAYING );
            return;
        }
        trmoPrevAttractFire = fireNow;
        trmoRenderFrame( TRMO_MODE_ATTRACT );
    }
    else // TRMO_STATE_PLAYING
    {
        trmoTickCounter++;
        if( trmoTickCounter < TRMO_TICK_DIVISOR ) return;
        trmoTickCounter = 0;

        bool gameOver = trmoUpdatePlaying();
        trmoRenderFrame( TRMO_MODE_PLAYING );
        if( gameOver ) trmoBeginAttract();
    }
}

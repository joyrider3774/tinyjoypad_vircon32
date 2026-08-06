// =============================================================================
// ATtiny: Snake (Sean Price, GitHub `SeanP2001`, GPLv3) - staged 2026-08-06
// during the same wider search that found Astro Barrier (see CLAUDE.md's
// own `more games/` catalog entry) - same author, same hardware, same
// `ssd1306xled` driver family. A classic absolute-direction Snake on a
// 16x8 grid (unlike this project's own Oroboros, which only supports
// relative turn-left/turn-right input) - genre-duplicates the already-
// shipped Oroboros and SnakeGame85, but is a genuinely distinct, from-
// scratch codebase (confirmed by reading it directly, not assumed from
// genre alone, matching this project's own established practice for
// candidate genre-duplicates).
//
// Menu title "ATTINY SNAKE" (matching the .ino's own header comment,
// "// ATtiny: Snake") rather than reusing "SNAKEGAME85"'s own generic
// "Snake" naming, since that title is already taken by a different game
// in this cartridge.
//
// Grid model is genuinely the simplest of any port in this project so
// far: `Display::block(x,y)` treats `x` as a GRID COLUMN (0-15, each 8
// physical pixels wide) and `y` as a real PAGE index directly (0-7) - so
// every cell maps onto exactly one physical page's own 8-column band,
// with `filledBlock`/`blankBlock` both being constant bytes (0xFF/0x00
// for all 8 sub-columns) - no per-column sprite variation to speak of
// except the apple's own `circle` icon. No rotation, no bit-shift
// tricks, no sub-page splitting anywhere.
//
// **The snake body is a real singly-linked list upstream** (`SnakeSegment*
// next`, `new`/`free` calls throughout `Snake.cpp`/`SnakeSegment.cpp`) -
// this dialect has no dynamic allocation at all (confirmed by this
// project's own entire history: zero uses of `new`/`malloc` anywhere),
// so it's ported as a fixed-size shift-array instead (`asnkBodyX[128]`/
// `asnkBodyY[128]`/`asnkLen`, index 0 = head), the same established
// pattern already used for this project's own Oroboros port (`orbSnakeX`/
// `orbLen`) - 128 is the true worst case (every one of the 16x8 grid
// cells), not an arbitrary guess.
//
// A real occupancy grid (`bool[8][16] asnkGrid`) is maintained
// incrementally (set the new head's cell, clear the old tail's cell only
// when not growing) rather than rescanned from the body array every
// frame or every collision check - an O(1) collision test instead of
// O(length), and avoids the O(pixels x objects) rendering trap this
// project has hit repeatedly elsewhere before it was even written, since
// a snake can genuinely reach non-trivial length here.
//
// **A genuine upstream bug found by inspection, not replicated**:
// `SnakeSegment::moveLeft()`/`moveUp()` check `xPos-1 < 0`/`yPos-1 < 0` to
// detect wraparound at the left/top edge - but `xPos`/`yPos` are `uint8_t`
// on real AVR, where an unsigned value can never be negative, so these
// conditions are always false and the intended "wrap to the other edge"
// branch is dead code on real hardware; moving left off the board instead
// computes a wrapped-to-255 column (accessing the board **far** out of its
// real 0-15 range) rather than the clearly-intended wrap to column 15.
// This is the *opposite* of this project's usual "AVR narrow-type
// behavior the port's plain `int`s can't preserve" bug family - here,
// avrCompat.h's `int`-widening would, if the comparison were ported
// completely literally, accidentally make the check start working
// correctly (since a real `int`'s `0-1` genuinely is `-1 < 0`) - but
// rather than leave this as an accidental side effect, both wraparound
// checks are ported as an explicit, intentional modulo-style wrap
// (`(x - 1 + cols) % cols`, etc.), matching the code's own clearly-stated
// comment intent ("loop around to the other side of the screen") rather
// than either its real (broken) AVR behavior or an unexamined accident of
// this port's own int-widening.
//
// **A second real design question, resolved by simplifying rather than
// replicating literally**: upstream's own main loop calls `snake.move()`
// unconditionally every tick *before* checking whether the new head
// position ate the apple, and if it did, calls `snake.grow()` - which
// itself calls `moveHead()` a *second* time that same tick (advancing the
// head one cell *further*, past the apple) without ever removing a tail
// segment. Traced through the linked-list mechanics directly (not
// guessed) - the net effect is that eating an apple makes the snake
// advance two cells in that single tick instead of one, with length only
// increasing by 1 (2 head segments added across the tick, only 1 tail
// segment removed, from the initial unconditional `move()` call). This
// reads as an unintended consequence of `grow()` being bolted on after
// `move()` already unconditionally runs every tick, not a deliberately
// designed "bonus dash on eating" feature - ported instead as the
// standard, universally-expected Snake mechanic (eating just means the
// tail isn't removed this tick, a single-cell head advance), matching
// this project's own already-shipped Oroboros/SnakeGame85 conventions.
//
// Sound: `Sound.cpp` is the exact same David Johnson-Davies Timer1 CTC
// tone generator already ported for Astro Barrier (same author, same
// file, byte-for-byte identical `note()`/`scale[]`/`Clock` code) - the
// `highScore()`/`gameOver()` jingles are even byte-for-byte the same note
// sequences, so their already-derived-and-verified frequency tables
// (`asnkHighScoreNotes`/`asnkGameOverNotes`) are reused directly rather
// than re-derived. Only `eating(duration)` is new here (a 2-note "C4 then
// D#4" blip, `duration` always the constant `refreshDelay` in every real
// call site, so ported with that value baked in rather than threaded
// through as a parameter).
//
// **One more deliberate deviation, matching Astro Barrier's own
// precedent exactly and for the same reason**: upstream's `setup()` shows
// the title screen/S-animation once at real power-on and then `loop()`
// runs forever with no "press start" gate at all. Every other game in
// this cartridge waits on its own attract screen for an explicit Fire
// press - added the same gate here, and (unlike upstream, where the S-
// animation is a true one-time-ever event) show the full title animation
// again every time the player returns to this game's own attract screen,
// matching how every other game's own attract screen behaves on re-entry.
//
// `arand()` used in place of every `rand()%n` call (the well-documented
// `rand()`-range-mismatch bug family), matching every other port in this
// project. `title[]` (1024 bytes) and the 3 tiny sprite bytes
// (`filledBlock`/`blankBlock`/`circle`) were byte-diff-verified via a
// small Python script before ever being pasted in.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data
// -----------------------------------------------------------------------------

// Same 95-char font table already reused for Astro Barrier (itself
// already proven for Oroboros/Run Dude Run/Dino Game).
int[570] asnkFont =
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

int asnkFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return asnkFont[ ( ch - 32 ) * 6 + col ];
}

int asnkTextByteAt( int* text, int textLen, int startX, int x )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= textLen ) return 0;
    return asnkFontByte( text[ charIdx ], rel % 6 );
}

int[8] asnkCircle = { 0x18, 0x3c, 0x7e, 0xff, 0xff, 0x7e, 0x3c, 0x18 }; // apple

// Title screen (128x64, standard row-major page order) - the "nake" part
// of the logo; the "S" is animated in separately (see asnkSTrail below).
int[1024] asnkTitle =
{
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,
224, 224, 224, 224, 224, 224, 0, 0, 224, 224, 224, 224, 224, 224, 224, 224,
224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 0, 0, 224,
224, 224, 224, 224, 224, 224, 0, 0, 0, 0, 0, 0, 0, 224, 224, 224,
224, 224, 224, 224, 0, 0, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,
224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 255, 255, 255, 255, 255, 255, 255, 15, 15, 15, 15, 15, 15, 15, 255,
255, 255, 255, 255, 255, 255, 0, 0, 255, 255, 255, 255, 255, 255, 255, 15,
15, 15, 15, 15, 15, 15, 255, 255, 255, 255, 255, 255, 255, 0, 0, 255,
255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255,
255, 255, 255, 255, 0, 0, 255, 255, 255, 255, 255, 255, 255, 15, 15, 15,
15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 255,
255, 255, 255, 255, 255, 255, 0, 0, 255, 255, 255, 255, 255, 255, 255, 248,
248, 248, 248, 248, 248, 248, 255, 255, 255, 255, 255, 255, 255, 0, 0, 255,
255, 255, 255, 255, 255, 255, 248, 248, 248, 248, 248, 248, 248, 7, 7, 7,
7, 7, 7, 7, 0, 0, 255, 255, 255, 255, 255, 255, 255, 248, 248, 248,
248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 255,
255, 255, 255, 255, 255, 255, 0, 0, 255, 255, 255, 255, 255, 255, 255, 3,
3, 3, 3, 3, 3, 3, 255, 255, 255, 255, 255, 255, 255, 0, 0, 255,
255, 255, 255, 255, 255, 255, 3, 3, 3, 3, 3, 3, 3, 252, 252, 252,
252, 252, 252, 252, 0, 0, 255, 255, 255, 255, 255, 255, 255, 3, 3, 3,
3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 255,
255, 255, 255, 255, 255, 255, 0, 0, 255, 255, 255, 255, 255, 255, 255, 0,
0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 0, 0, 255,
255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255,
255, 255, 255, 255, 0, 0, 255, 255, 255, 255, 255, 255, 255, 254, 254, 254,
254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 0, 0, 0, 0, 0,
};

// S-animation reveal order (block()'s own real (x,y) call sequence).
int[38] asnkSTrail =
{
0,7, 1,7, 2,7, 3,7, 3,6, 3,5, 3,4, 3,3, 2,3, 1,3, 0,3, 0,2, 0,1, 0,0, 1,0, 2,0, 3,0, 4,0, 5,0,
};
#define ASNK_S_STEPS 19

// -----------------------------------------------------------------------------
//   Sound (formula/derivation identical to Astro Barrier - see that file's
//   own header comment for the frequency derivation and numeric proof).
// -----------------------------------------------------------------------------

int asnkSeqActive;
int* asnkSeqNotes;
int asnkSeqCount;
int asnkSeqIndex;
int asnkSeqWaitFrames;

void asnkStartJingle( int* notes, int count )
{
    asnkSeqNotes = notes;
    asnkSeqCount = count;
    asnkSeqIndex = 0;
    asnkSeqActive = 1;
    asnkSeqWaitFrames = 0;
}

void asnkAdvanceJingle()
{
    if( !asnkSeqActive ) return;
    if( asnkSeqWaitFrames > 0 ) { asnkSeqWaitFrames--; return; }
    if( asnkSeqIndex >= asnkSeqCount ) { asnkSeqActive = 0; return; }
    float freqHz = (float)asnkSeqNotes[ asnkSeqIndex * 2 ];
    int durMs = asnkSeqNotes[ asnkSeqIndex * 2 + 1 ];
    float durationSeconds = (float)durMs / 1000.0;
    md_playTone( freqHz, durationSeconds );

    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    asnkSeqWaitFrames = waitFrames;
    asnkSeqIndex++;
}

// note(0,4)=261 (C4), note(3,4)=310 (D#4) - eating(refreshDelay), duration
// always the constant refreshDelay(250)/2=125ms per note in every real call.
int[4] asnkEatingNotes = { 261,125, 310,125 };
// Byte-for-byte the same jingles as Astro Barrier's own already-derived
// tables (same author, same Sound.cpp, same note sequence).
int[8] asnkHighScoreNotes = { 261,200, 0,100, 261,200, 391,500 };
int[4] asnkGameOverNotes  = { 370,100, 261,250 };

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

#define ASNK_COLS 16
#define ASNK_ROWS 8
#define ASNK_MAX_LEN 128
#define ASNK_TICK_DIVISOR 15 // upstream's own real delay(250) ~= 4 ticks/sec

#define ASNK_UP    0
#define ASNK_DOWN  1
#define ASNK_LEFT  2
#define ASNK_RIGHT 3

int[128] asnkBodyX;
int[128] asnkBodyY;
int asnkLen;
int asnkDirection;

bool[8][16] asnkGrid;

int asnkAppleX, asnkAppleY;

int asnkScore, asnkTopScore;
int asnkTickCounter;

bool asnkPrevUp, asnkPrevDown, asnkPrevLeft, asnkPrevRight;

void asnkPlaceApple()
{
    int tries = 0;
    int x, y;
    while( tries < 300 )
    {
        x = arand( ASNK_COLS );
        y = arand( ASNK_ROWS );
        if( !asnkGrid[ y ][ x ] ) break;
        tries++;
    }
    asnkAppleX = x;
    asnkAppleY = y;
}

// -----------------------------------------------------------------------------
//   Text line buffers
// -----------------------------------------------------------------------------

int[24] asnkTitleLine;
int asnkTitleLineLen;
int asnkTitleLineCol;
int asnkTitleLinePage;

// gameOverScreen's own score line (page 4, col 32) - unused by
// newHighScoreScreen, which has no separate score line of its own.
int[16] asnkScoreLine;
int asnkScoreLineLen;

// The high-score line's own (page,col) position differs between the two
// end-of-game screens (page 6/col 2 for Game Over, page 5/col 2 for New
// High Score) - tracked explicitly rather than hardcoded, since it's the
// one line both screens share.
int[16] asnkHighLine;
int asnkHighLineLen;
int asnkHighLineCol;
int asnkHighLinePage;

int[5] asnkDigitBuf;
int asnkDigitBufLen;

void asnkDigitsOf( int value )
{
    if( value < 0 ) value = 0;
    if( value > 9999 ) value = 9999;
    if( value == 0 ) { asnkDigitBuf[ 0 ] = 48; asnkDigitBufLen = 1; return; }

    int[4] digits;
    int n = 0;
    int tmp = value;
    while( ( tmp > 0 ) && ( n < 4 ) ) { digits[ n ] = tmp % 10; tmp = tmp / 10; n = n + 1; }
    int i;
    for( i = 0; i < n; i++ ) asnkDigitBuf[ i ] = 48 + digits[ n - 1 - i ];
    asnkDigitBufLen = n;
}

int asnkSetTextPlusNumber( int* dest, int* text, int textLen, int value )
{
    int i;
    for( i = 0; i < textLen; i++ ) dest[ i ] = text[ i ];
    asnkDigitsOf( value );
    for( i = 0; i < asnkDigitBufLen; i++ ) dest[ textLen + i ] = asnkDigitBuf[ i ];
    return textLen + asnkDigitBufLen;
}

void asnkBuildScoreHighLines()
{
    asnkScoreLineLen = asnkSetTextPlusNumber( asnkScoreLine, "Score: ", 7, asnkScore );
    asnkHighLineLen = asnkSetTextPlusNumber( asnkHighLine, "High Score: ", 12, asnkTopScore );
    asnkHighLineCol = 2; asnkHighLinePage = 6; // gameOverScreen's own position
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int[128] asnkPageBuffer;

void asnkComposePlayingRow( int page )
{
    int col;
    for( col = 0; col < 128; col++ ) asnkPageBuffer[ col ] = 0;

    int gc;
    for( gc = 0; gc < ASNK_COLS; gc++ )
    {
        int val = 0;
        if( asnkGrid[ page ][ gc ] ) val = 0xFF;
        else if( gc == asnkAppleX && page == asnkAppleY )
        {
            int sc;
            for( sc = 0; sc < 8; sc++ )
              asnkPageBuffer[ gc * 8 + sc ] = asnkCircle[ sc ];
            continue;
        }
        if( val != 0 )
        {
            int sc;
            for( sc = 0; sc < 8; sc++ ) asnkPageBuffer[ gc * 8 + sc ] = val;
        }
    }
}

#define ASNK_MODE_ATTRACT 0
#define ASNK_MODE_PLAYING 1
#define ASNK_MODE_SCREEN  2

int asnkSRevealCount;

// The S-animation reveal, tracked as an occupancy grid (matching
// asnkGrid's own established shape) rather than re-scanned per pixel -
// see this file's own header comment on why an O(pixels x objects) scan
// (checking every one of asnkSRevealCount's up to 19 entries against
// every one of 1024 pixels/frame) is a real cost this project has found
// and fixed repeatedly elsewhere, applied here proactively rather than
// waiting for a report. Updated only when a new cell is actually
// revealed (once per ~100ms step), not every frame.
bool[8][16] asnkSGrid;

void asnkRenderFrame( int mode )
{
    md_beginFrame();
    int col, page;
    for( page = 0; page < 8; page++ )
    {
        if( mode == ASNK_MODE_PLAYING )
        {
            asnkComposePlayingRow( page );
            for( col = 0; col < 128; col++ )
              md_drawColumn( col, page, asnkPageBuffer[ col ] );
            continue;
        }

        // Row-gate the SCREEN-mode text calls to their own known column
        // range too - each asnkTextByteAt() call below is only ever
        // nonzero within [startCol, startCol+len*6), so gating the call
        // site to that range (not just the page) avoids paying a full
        // function call for a guaranteed no-op on the rest of the row.
        int titleEndCol = asnkTitleLineCol + asnkTitleLineLen * 6;
        int scoreEndCol = 32 + asnkScoreLineLen * 6;
        int highEndCol = asnkHighLineCol + asnkHighLineLen * 6;

        for( col = 0; col < 128; col++ )
        {
            int val = 0;
            if( mode == ASNK_MODE_ATTRACT )
            {
                val = asnkTitle[ page * 128 + col ];
                // apple icon at grid (7,0)
                if( page == 0 && col >= 56 && col < 64 )
                  val = val | asnkCircle[ col - 56 ];
                if( asnkSGrid[ page ][ col / 8 ] ) val = val | 0xFF;
            }
            else // ASNK_MODE_SCREEN
            {
                if( asnkTitleLineLen > 0 && page == asnkTitleLinePage
                    && col >= asnkTitleLineCol && col < titleEndCol )
                  val = val | asnkTextByteAt( asnkTitleLine, asnkTitleLineLen, asnkTitleLineCol, col );
                if( asnkScoreLineLen > 0 && page == 4 && col >= 32 && col < scoreEndCol )
                  val = val | asnkTextByteAt( asnkScoreLine, asnkScoreLineLen, 32, col );
                if( asnkHighLineLen > 0 && page == asnkHighLinePage
                    && col >= asnkHighLineCol && col < highEndCol )
                  val = val | asnkTextByteAt( asnkHighLine, asnkHighLineLen, asnkHighLineCol, col );
            }
            md_drawColumn( col, page, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define ASNK_STATE_ATTRACT    0
#define ASNK_STATE_PLAYING    1
#define ASNK_STATE_NEW_HIGH   2
#define ASNK_STATE_GAME_OVER  3

int asnkState;
int asnkWaitFrames;
bool asnkPrevFire;
int asnkSAnimWaitFrames;

void asnkBeginAttract()
{
    asnkPrevFire = false;
    asnkSRevealCount = 0;
    asnkSAnimWaitFrames = ASNK_TICK_DIVISOR / 2; // ~100ms/step at 60fps
    int r, c;
    for( r = 0; r < ASNK_ROWS; r++ )
      for( c = 0; c < ASNK_COLS; c++ )
        asnkSGrid[ r ][ c ] = false;
    asnkState = ASNK_STATE_ATTRACT;
}

void asnkResetGrid()
{
    int r, c;
    for( r = 0; r < ASNK_ROWS; r++ )
      for( c = 0; c < ASNK_COLS; c++ )
        asnkGrid[ r ][ c ] = false;
}

void asnkBeginPlaying()
{
    asnkResetGrid();
    asnkLen = 1;
    asnkBodyX[ 0 ] = 0;
    asnkBodyY[ 0 ] = 0;
    asnkGrid[ 0 ][ 0 ] = true;
    asnkDirection = ASNK_DOWN;

    asnkPlaceApple();

    asnkScore = 0;
    asnkTickCounter = 0;
    asnkPrevUp = false; asnkPrevDown = false; asnkPrevLeft = false; asnkPrevRight = false;

    asnkState = ASNK_STATE_PLAYING;
}

void asnkBeginNewHigh()
{
    asnkTopScore = asnkScore;

    int i;
    int* t = "New High Score";
    for( i = 0; i < 15; i++ ) asnkTitleLine[ i ] = t[ i ];
    asnkTitleLineLen = 15;
    asnkTitleLineCol = 16; asnkTitleLinePage = 3;

    asnkScoreLineLen = 0; // newHighScoreScreen has no separate score line
    asnkHighLineLen = asnkSetTextPlusNumber( asnkHighLine, "High Score: ", 12, asnkTopScore );
    asnkHighLineCol = 2; asnkHighLinePage = 5; // newHighScoreScreen's own position

    asnkStartJingle( asnkHighScoreNotes, 4 );
    asnkWaitFrames = 120;
    asnkState = ASNK_STATE_NEW_HIGH;
}

void asnkBeginGameOver()
{
    asnkTitleLineLen = 0;
    int i;
    int* t = "Game Over";
    for( i = 0; i < 9; i++ ) asnkTitleLine[ i ] = t[ i ];
    asnkTitleLineLen = 9;
    asnkTitleLineCol = 32; asnkTitleLinePage = 2;
    asnkBuildScoreHighLines();

    asnkStartJingle( asnkGameOverNotes, 2 );
    asnkWaitFrames = 120;
    asnkState = ASNK_STATE_GAME_OVER;
}

void gameAttinySnake_init()
{
    asnkTopScore = 0;
    asnkSeqActive = 0;
    asnkBeginAttract();
}

void gameAttinySnake_forceRedraw()
{
    if( asnkState == ASNK_STATE_PLAYING ) asnkRenderFrame( ASNK_MODE_PLAYING );
    else if( asnkState == ASNK_STATE_ATTRACT ) asnkRenderFrame( ASNK_MODE_ATTRACT );
    else asnkRenderFrame( ASNK_MODE_SCREEN );
}

void gameAttinySnake_update()
{
    if( asnkState == ASNK_STATE_ATTRACT )
    {
        if( asnkSRevealCount < ASNK_S_STEPS )
        {
            asnkSAnimWaitFrames--;
            if( asnkSAnimWaitFrames <= 0 )
            {
                int gx = asnkSTrail[ asnkSRevealCount * 2 ];
                int gy = asnkSTrail[ asnkSRevealCount * 2 + 1 ];
                asnkSGrid[ gy ][ gx ] = true;
                asnkSRevealCount++;
                asnkSAnimWaitFrames = ASNK_TICK_DIVISOR / 2;
            }
        }

        bool fireNow = isFirePressed();
        if( fireNow && !asnkPrevFire )
        {
            asnkBeginPlaying();
            asnkRenderFrame( ASNK_MODE_PLAYING );
            return;
        }
        asnkPrevFire = fireNow;
        asnkRenderFrame( ASNK_MODE_ATTRACT );
    }
    else if( asnkState == ASNK_STATE_PLAYING )
    {
        asnkTickCounter++;
        if( asnkTickCounter >= ASNK_TICK_DIVISOR )
        {
            asnkTickCounter = 0;

            bool up = isUpPressed(), down = isDownPressed(), left = isLeftPressed(), right = isRightPressed();
            if( left && !asnkPrevLeft && asnkDirection != ASNK_RIGHT ) asnkDirection = ASNK_LEFT;
            if( right && !asnkPrevRight && asnkDirection != ASNK_LEFT ) asnkDirection = ASNK_RIGHT;
            if( up && !asnkPrevUp && asnkDirection != ASNK_DOWN ) asnkDirection = ASNK_UP;
            if( down && !asnkPrevDown && asnkDirection != ASNK_UP ) asnkDirection = ASNK_DOWN;
            asnkPrevUp = up; asnkPrevDown = down; asnkPrevLeft = left; asnkPrevRight = right;

            int headX = asnkBodyX[ 0 ];
            int headY = asnkBodyY[ 0 ];
            int newX = headX, newY = headY;
            if( asnkDirection == ASNK_UP ) newY = ( headY - 1 + ASNK_ROWS ) % ASNK_ROWS;
            else if( asnkDirection == ASNK_DOWN ) newY = ( headY + 1 ) % ASNK_ROWS;
            else if( asnkDirection == ASNK_LEFT ) newX = ( headX - 1 + ASNK_COLS ) % ASNK_COLS;
            else newX = ( headX + 1 ) % ASNK_COLS;

            bool eating = ( newX == asnkAppleX ) && ( newY == asnkAppleY );

            int tailX = 0, tailY = 0;
            if( !eating && asnkLen > 0 )
            {
                tailX = asnkBodyX[ asnkLen - 1 ];
                tailY = asnkBodyY[ asnkLen - 1 ];
                asnkGrid[ tailY ][ tailX ] = false;
            }

            if( asnkGrid[ newY ][ newX ] )
            {
                // collided with its own body - restore the tail cell we
                // speculatively cleared above before ending the game, so
                // the final rendered frame still shows a correct snake.
                if( !eating ) asnkGrid[ tailY ][ tailX ] = true;
                if( asnkScore > asnkTopScore ) { asnkBeginNewHigh(); asnkRenderFrame( ASNK_MODE_SCREEN ); return; }
                asnkBeginGameOver();
                asnkRenderFrame( ASNK_MODE_SCREEN );
                return;
            }

            int i;
            if( eating )
            {
                if( asnkLen < ASNK_MAX_LEN )
                {
                    for( i = asnkLen; i > 0; i-- ) { asnkBodyX[ i ] = asnkBodyX[ i - 1 ]; asnkBodyY[ i ] = asnkBodyY[ i - 1 ]; }
                    asnkLen++;
                }
                asnkScore++;
                asnkStartJingle( asnkEatingNotes, 2 );
                asnkPlaceApple();
            }
            else
            {
                for( i = asnkLen - 1; i > 0; i-- ) { asnkBodyX[ i ] = asnkBodyX[ i - 1 ]; asnkBodyY[ i ] = asnkBodyY[ i - 1 ]; }
            }
            asnkBodyX[ 0 ] = newX;
            asnkBodyY[ 0 ] = newY;
            asnkGrid[ newY ][ newX ] = true;
        }

        asnkAdvanceJingle();
        asnkRenderFrame( ASNK_MODE_PLAYING );
    }
    else if( asnkState == ASNK_STATE_NEW_HIGH )
    {
        asnkAdvanceJingle();
        asnkWaitFrames--;
        if( asnkWaitFrames <= 0 ) { asnkBeginGameOver(); asnkRenderFrame( ASNK_MODE_SCREEN ); return; }
        asnkRenderFrame( ASNK_MODE_SCREEN );
    }
    else // ASNK_STATE_GAME_OVER
    {
        asnkAdvanceJingle();
        asnkWaitFrames--;
        if( asnkWaitFrames <= 0 ) { asnkBeginAttract(); asnkRenderFrame( ASNK_MODE_ATTRACT ); return; }
        asnkRenderFrame( ASNK_MODE_SCREEN );
    }
}

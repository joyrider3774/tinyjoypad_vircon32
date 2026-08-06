// =============================================================================
// Tiny Bulls And Cows (datacute, MIT) - a Mastermind-style number-guessing
// game: guess a hidden 4-digit code (0-9, repeats allowed), each guess
// scored in "bulls" (right digit, right position) and "cows" (right digit,
// wrong position), up to 10 guesses to find it. From `more games/
// TinyBullsAndCows/` - staged during the "very very deep scan" search
// batch, confirmed via a direct feasibility audit to be genuinely portable
// at a normal effort level (native 128x64 display, 3 plain digital
// buttons, no C++ classes/switch/float anywhere in the source).
//
// Not `tinyJoypadShim`/`obonoCoreShim` lineage - built on the `Tiny4kOLED`
// Arduino library (the same library ATtiny Tetromino also uses) with 3
// discrete digital-pin buttons (up/down/select) rather than TinyJoypad's
// own analog ladder - needed no new shim, `isUpPressed()`/
// `isDownPressed()`/`isFirePressed()`/`arand()` already cover the whole
// input/RNG surface (select -> Fire). No sound anywhere in this game
// (confirmed by grep - no tone/beep call at all).
//
// **Confirmed via `oled.setVerticalScrollArea(0,64)` (in the real
// upstream source) that this targets a genuine 128x64 display** - unlike
// ATtiny Tetromino/ATTiny85_Pong (both confirmed 128x32 via their own
// direct source reads during the same feasibility audit), this game
// needed no canvas-placement decision at all, native fit.
//
// **Upstream's own input model is a held-repeat, not a single-shot edge**:
// `readInputs()` tracks the select button's own press/release edge every
// real tick (`inputSwitchChanged`), but `processInputs()` gates ALL
// actual state changes (including a held up/down repeatedly nudging a
// digit or cursor) behind a shared `millis()`-based 200ms cooldown -
// meaning a held Up/Down button repeats roughly 5x/second, not once per
// press. Ported directly as a frame-counted equivalent
// (`tbcInputCooldown`, reset to `TBC_INPUT_COOLDOWN_FRAMES` = 12 frames
// @ 60fps = 200ms) rather than collapsed into a plain single-press edge
// check, since the repeat-rate behavior is a real, deliberate part of how
// this UI feels to navigate (a 10-row history list plus 4 digit slots
// needs more than one nudge per press to be usable). `bcReadInputs()`
// itself still runs every real frame (tracking the raw edge state), only
// the actual *application* of an input is throttled - matching upstream's
// own two-tier structure exactly, not simplified into one flat check.
//
// **A genuinely quirky, deliberately-preserved-not-"fixed" upstream
// behavior**: a fresh, never-yet-entered digit slot holds sentinel value
// 10 ("blank"). Upstream's own digit-edit logic coerces this to a local
// 0 *before* applying the Up/Down delta, meaning the very first Up press
// on a blank slot jumps straight to displaying "1" (0 coerced, then
// incremented, then stored) - not "0" - while the very first Down press
// on a blank slot does *nothing at all* (0 coerced locally, but `0 > 0`
// is false, so the write branch never runs and the stored sentinel stays
// 10/blank). Ported with the exact same two-step coerce-then-conditionally
// -write logic, not "corrected" into a symmetric wrap.
//
// **Rendering is a dense, precisely-interleaved multi-region layout**,
// confirmed non-overlapping column-by-column before porting rather than
// assumed: a 10-entry guess-history strip (columns 0-60, pages 0-3, 6
// columns per entry - 5 data + 1 connector-line byte forming a vertical
// timeline linking consecutive rows), a 7-column "hidden/revealed answer
// squares" indicator (columns 63-69, pages 0-3), 4 large 8x16-font
// answer/guess digits interleaved with 5 small 5-column cursor-arrow
// indicator slots (columns 71-127, pages 0-1, confirmed via exact column
// math that neither region's bytes ever land on the same column as the
// other), and a 2-line text menu/status area (columns 72+, pages 2-3).
// Composited with OR (`|=`) throughout as a defensive default (matching
// this project's own established lesson from Meteor Storm's border bug)
// even though the column math confirms these regions don't actually
// overlap - cheap insurance against a miscounted column range.
//
// Upstream's own digit-packing math (`(b2 << 6)`, `(b3 >> 4) | (b4 << 2)`,
// etc - fitting two 5-row-tall 3-column glyphs' bits into shared bytes)
// relies on AVR's implicit `uint8_t` truncation to stay within a real
// byte - the same byte-truncation bug class this whole project's history
// starts with - fixed the same way as every prior instance: explicit
// `& 0xFF` masks at each shift site.
//
// The splash screen's own real hardware scroll animation
// (`oled.scrollLeftOffset`/`activateScroll`, a decorative decorative-only
// effect with no gameplay role) was not reproduced - shown as a plain
// static instructions screen instead, matching this project's own
// precedent of not chasing purely-cosmetic real-hardware animation
// tricks that have no bearing on the actual game.
//
// `digits[]` (10 digits x 3 bytes, the small in-game font) was byte-diff-
// verified against upstream's own table; the large 8x16 answer-digit font
// is not part of this game's own source at all - it's `FONT8X16DIGITS`,
// pulled in from the separate `Tiny4kOLED` library upstream depends on
// (`datacute/Tiny4kOLED`, `src/font8x16digits.h`) - fetched and byte-diff
// -verified directly from that library's own real source rather than
// approximated or substituted with an already-available smaller font.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data
// -----------------------------------------------------------------------------

// Small 3x5 digit font (10 digits x 3 bytes) - byte-diff-verified against
// upstream's own digits[].
int[30] tbcSmallDigits =
{
0x0E,0x11,0x0E, // 0
0x02,0x1F,0x00, // 1
0x12,0x19,0x16, // 2
0x15,0x15,0x0A, // 3
0x07,0x04,0x1F, // 4
0x17,0x15,0x09, // 5
0x0E,0x15,0x09, // 6
0x01,0x19,0x07, // 7
0x0A,0x15,0x0A, // 8
0x12,0x15,0x0E, // 9
};

// Large 8x16 digit font (10 digits x 16 bytes, 2 pages tall) - from the
// Tiny4kOLED library's own font8x16digits.h (FONT8X16DIGITS), byte-diff-
// verified against that library's real source.
int[160] tbcBigDigits =
{
0x00,0xE0,0x10,0x08,0x08,0x10,0xE0,0x00,0x00,0x0F,0x10,0x20,0x20,0x10,0x0F,0x00, // 0
0x00,0x10,0x10,0xF8,0x00,0x00,0x00,0x00,0x00,0x20,0x20,0x3F,0x20,0x20,0x00,0x00, // 1
0x00,0x70,0x08,0x08,0x08,0x88,0x70,0x00,0x00,0x30,0x28,0x24,0x22,0x21,0x30,0x00, // 2
0x00,0x30,0x08,0x88,0x88,0x48,0x30,0x00,0x00,0x18,0x20,0x20,0x20,0x11,0x0E,0x00, // 3
0x00,0x00,0xC0,0x20,0x10,0xF8,0x00,0x00,0x00,0x07,0x04,0x24,0x24,0x3F,0x24,0x00, // 4
0x00,0xF8,0x08,0x88,0x88,0x08,0x08,0x00,0x00,0x19,0x21,0x20,0x20,0x11,0x0E,0x00, // 5
0x00,0xE0,0x10,0x88,0x88,0x18,0x00,0x00,0x00,0x0F,0x11,0x20,0x20,0x11,0x0E,0x00, // 6
0x00,0x38,0x08,0x08,0xC8,0x38,0x08,0x00,0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00, // 7
0x00,0x70,0x88,0x08,0x08,0x88,0x70,0x00,0x00,0x1C,0x22,0x21,0x21,0x22,0x1C,0x00, // 8
0x00,0xE0,0x10,0x08,0x08,0x10,0xE0,0x00,0x00,0x00,0x31,0x22,0x22,0x11,0x0F,0x00, // 9
};

// Standard 95-char font, already proven for several ports in this project
// (Oroboros, Run Dude Run, Astro Barrier, ATtiny Snake, etc) - reused here
// for the menu/status text, each game keeping its own self-contained copy
// per this project's own convention.
int[570] tbcFont =
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

int tbcFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return tbcFont[ ( ch - 32 ) * 6 + col ];
}

int tbcTextByteAt( int* text, int textLen, int startX, int x )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= textLen ) return 0;
    return tbcFontByte( text[ charIdx ], rel % 6 );
}

// -----------------------------------------------------------------------------
//   Game state (direct translation of the upstream globals)
// -----------------------------------------------------------------------------

#define TBC_MAX_GUESSES 10
#define TBC_BLANK 10

bool tbcEnteringDigit;
int tbcSelectedRow;
int tbcNumGuesses;
int tbcGuess;
// [row][0..3]=digits, [row][4]=bulls, [row][5]=cows
int[10][6] tbcGuesses;
int[4] tbcSolution;
bool tbcGameOver;
bool tbcGameWon;

bool tbcPrevUp;
bool tbcPrevDown;
bool tbcPrevSwitch;
bool tbcSwitchChanged;

#define TBC_INPUT_COOLDOWN_FRAMES 12
int tbcInputCooldown;

#define TBC_STATE_ATTRACT 0
#define TBC_STATE_PLAYING 1
int tbcState;
bool tbcPrevFire;

// Forward-reference-free ordering: this dialect requires definition
// before use, so the small state-transition helpers that
// tbcProcessInputs() below needs to call (on "Restart"/game-over ->
// attract) are defined here, ahead of the game logic that calls them,
// rather than down with the rest of the state machine at the bottom of
// the file.
void tbcBeginAttract()
{
    tbcPrevFire = false;
    tbcState = TBC_STATE_ATTRACT;
}

// -----------------------------------------------------------------------------
//   Game logic
// -----------------------------------------------------------------------------

void tbcInitGame()
{
    tbcEnteringDigit = true;
    tbcSelectedRow = 11;
    tbcGuess = 0;
    tbcNumGuesses = 0;
    tbcGameOver = false;
    tbcGameWon = false;

    int g, d;
    for( g = 0; g < TBC_MAX_GUESSES; g++ )
    {
        for( d = 0; d < 4; d++ ) tbcGuesses[ g ][ d ] = TBC_BLANK;
        tbcGuesses[ g ][ 4 ] = 0;
        tbcGuesses[ g ][ 5 ] = 0;
    }

    tbcSolution[ 0 ] = arand( 10 );
    tbcSolution[ 1 ] = arand( 10 );
    tbcSolution[ 2 ] = arand( 10 );
    tbcSolution[ 3 ] = arand( 10 );
}

void tbcReadInputs()
{
    bool switchNow = isFirePressed();
    tbcSwitchChanged = ( tbcPrevSwitch != switchNow );
    tbcPrevSwitch = switchNow;
    tbcPrevUp = isUpPressed();
    tbcPrevDown = isDownPressed();
}

void tbcProcessInputs()
{
    bool switchIn = tbcPrevSwitch;
    bool up = tbcPrevUp;
    bool down = tbcPrevDown;

    if( !( tbcSwitchChanged || up || down ) ) return;
    if( tbcInputCooldown > 0 ) return;
    tbcInputCooldown = TBC_INPUT_COOLDOWN_FRAMES;

    if( tbcEnteringDigit )
    {
        int pos = tbcSelectedRow - 11;
        int currentDigit = tbcGuesses[ tbcGuess ][ pos ];
        if( currentDigit == TBC_BLANK ) currentDigit = 0;
        if( up && currentDigit < 9 )
        {
            currentDigit++;
            tbcGuesses[ tbcGuess ][ pos ] = currentDigit;
        }
        if( down && currentDigit > 0 )
        {
            currentDigit--;
            tbcGuesses[ tbcGuess ][ pos ] = currentDigit;
        }
        if( tbcSwitchChanged && switchIn ) tbcEnteringDigit = false;
        return;
    }

    if( up && tbcSelectedRow > 0 )
    {
        tbcSelectedRow--;
        if( tbcGameOver )
        {
            if( ( tbcSelectedRow >= 10 ) || ( tbcNumGuesses == 0 ) ) tbcSelectedRow = 10;
            else if( tbcSelectedRow > tbcNumGuesses - 1 ) tbcSelectedRow = tbcNumGuesses - 1;
        }
        else
        {
            if( ( tbcSelectedRow >= tbcGuess ) && ( tbcSelectedRow < 11 ) )
            {
                if( tbcGuess > 0 ) tbcSelectedRow = tbcGuess - 1;
                else tbcSelectedRow = 11;
            }
        }
    }

    int upperBound = 16;
    if( tbcGameOver ) upperBound = 15;
    if( down && tbcSelectedRow < upperBound )
    {
        tbcSelectedRow++;
        if( tbcGameOver )
        {
            if( tbcSelectedRow > 10 ) tbcSelectedRow = 15;
            else if( tbcSelectedRow >= tbcNumGuesses ) tbcSelectedRow = 10;
        }
        else
        {
            if( ( tbcSelectedRow >= tbcGuess ) && ( tbcSelectedRow < 11 ) ) tbcSelectedRow = 11;
        }
    }

    if( tbcSwitchChanged && switchIn )
    {
        if( tbcSelectedRow >= 11 )
        {
            if( tbcSelectedRow < 15 )
            {
                tbcEnteringDigit = true;
            }
            else
            {
                if( tbcGameOver )
                {
                    tbcBeginAttract();
                    return;
                }
                if( tbcSelectedRow == 16 )
                {
                    int d;
                    for( d = 0; d < 4; d++ ) tbcGuesses[ tbcGuess ][ d ] = TBC_BLANK;
                    tbcGameOver = true;
                    tbcSelectedRow = 10;
                    return;
                }

                // submit guess
                tbcNumGuesses++;
                int bulls = 0;
                int cows = 0;
                int[4] herd = { 10, 10, 10, 10 };
                int d;
                for( d = 0; d < 4; d++ )
                {
                    int digit = tbcGuesses[ tbcGuess ][ d ];
                    if( digit == TBC_BLANK )
                    {
                        tbcGuesses[ tbcGuess ][ d ] = 0;
                        digit = 0;
                    }
                    if( digit == tbcSolution[ d ] ) bulls++;
                    else herd[ d ] = tbcSolution[ d ];
                }
                tbcGuesses[ tbcGuess ][ 4 ] = bulls;
                if( bulls == 4 )
                {
                    tbcGameOver = true;
                    tbcGameWon = true;
                    tbcSelectedRow = 10;
                    tbcGuess++;
                    return;
                }
                for( d = 0; d < 4; d++ )
                {
                    int digit = tbcGuesses[ tbcGuess ][ d ];
                    if( digit != tbcSolution[ d ] )
                    {
                        int h;
                        for( h = 0; h < 4; h++ )
                          if( digit == herd[ h ] ) { cows++; herd[ h ] = 10; }
                    }
                }
                tbcGuesses[ tbcGuess ][ 5 ] = cows;
                tbcSelectedRow = tbcGuess;
                tbcGuess++;
                if( tbcNumGuesses == 10 )
                {
                    tbcGameOver = true;
                    tbcSelectedRow = 10;
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int[128] tbcPageBuffer;

int tbcSmallGlyphByte( int digit, int row )
{
    if( digit == TBC_BLANK ) return 0;
    return tbcSmallDigits[ digit * 3 + row ];
}

int tbcDigit12Byte( int d1, int d2, int row )
{
    int b1 = tbcSmallGlyphByte( d1, row );
    int b2 = tbcSmallGlyphByte( d2, row );
    return ( b1 | ( b2 << 6 ) ) & 0xFF;
}

int tbcDigit23Byte( int d2, int d3, int row )
{
    int b2 = tbcSmallGlyphByte( d2, row );
    int b3 = tbcSmallGlyphByte( d3, row );
    return ( ( b2 >> 2 ) | ( b3 << 4 ) ) & 0xFF;
}

int tbcDigit34Byte( int d3, int d4, int row )
{
    int b3 = tbcSmallGlyphByte( d3, row );
    int b4 = tbcSmallGlyphByte( d4, row );
    return ( ( b3 >> 4 ) | ( b4 << 2 ) ) & 0xFF;
}

int tbcDisplayedRow()
{
    int displayedRow = tbcSelectedRow;
    if( tbcSelectedRow > tbcGuess )
    {
        if( tbcGameOver ) displayedRow = 10;
        else displayedRow = tbcGuess;
    }
    return displayedRow;
}

int tbcBullsCowsByte( int bulls, int cows, bool rowSelected )
{
    int data = 0x00;
    if( rowSelected ) data = 0x40;
    int bullBit = 0x01;
    int b;
    for( b = 0; b < bulls; b++ ) { data |= bullBit; bullBit <<= 1; }
    int cowBit = 0x10;
    int c;
    for( c = 0; c < cows; c++ ) { data |= cowBit; cowBit >>= 1; }
    return data & 0xFF;
}

// The 10-entry guess-history strip - columns 0-60, one of the 4 gameplay
// pages at a time. Entry-data gating against tbcGuess isn't needed: every
// row's digits are reset to the TBC_BLANK sentinel by tbcInitGame() and
// only ever change when that exact row is actually played, so an unplayed
// row's glyph bytes are already naturally blank (tbcSmallGlyphByte returns
// 0 for TBC_BLANK) without an explicit "only render if d<=guess" check.
void tbcComposeHistoryArea( int page, int displayedRow )
{
    int leader = 0;
    if( page == 0 )
    {
        if( displayedRow == 0 ) leader = 0xC0;
        else if( tbcGameOver && tbcNumGuesses == 0 ) leader = 0x80;
    }
    else if( page == 1 ) { if( tbcGuess == 0 ) leader = 0x20; }
    else if( page == 2 ) { if( tbcGuess == 0 ) leader = 0x08; }
    else if( page == 3 ) { if( tbcGuess == 0 ) leader = 0x82; }
    tbcPageBuffer[ 0 ] |= leader;

    int d;
    for( d = 0; d < 10; d++ )
    {
        int base = 1 + d * 6;
        int d1 = tbcGuesses[ d ][ 0 ];
        int d2 = tbcGuesses[ d ][ 1 ];
        int d3 = tbcGuesses[ d ][ 2 ];
        int d4 = tbcGuesses[ d ][ 3 ];

        if( page == 0 )
        {
            int val = tbcBullsCowsByte( tbcGuesses[ d ][ 4 ], tbcGuesses[ d ][ 5 ], d == displayedRow );
            int sc;
            for( sc = 0; sc < 5; sc++ ) tbcPageBuffer[ base + sc ] |= val;
        }
        else
        {
            int row;
            for( row = 0; row < 3; row++ )
            {
                int val = 0;
                if( page == 1 ) val = tbcDigit12Byte( d1, d2, row );
                else if( page == 2 ) val = tbcDigit23Byte( d2, d3, row );
                else val = tbcDigit34Byte( d3, d4, row );
                tbcPageBuffer[ base + 1 + row ] |= val;
            }
        }

        bool progressGap = ( ( d + 1 < tbcGuess ) || ( tbcGuess == 10 ) );
        int connector = 0;
        if( page == 0 )
        {
            bool onSelected = ( d == displayedRow ) || ( ( d + 1 == displayedRow ) && ( d < 9 ) );
            if( onSelected ) connector = 0xC0;
            else if( !progressGap ) connector = 0x80;
        }
        else if( page == 1 ) { if( !progressGap ) connector = 0x20; }
        else if( page == 2 ) { if( !progressGap ) connector = 0x08; }
        else { if( !progressGap ) connector = 0x82; }
        tbcPageBuffer[ base + 5 ] |= connector;
    }
}

// The "answer squares" indicator - columns 63-69, hidden (a plain frame)
// while playing, revealed (the real solution digits) once the game ends.
void tbcComposeAnswerSquares( int page )
{
    int col;
    if( !tbcGameOver )
    {
        if( page == 0 ) { for( col = 0; col < 7; col++ ) tbcPageBuffer[ 63 + col ] |= 0x80; return; }
        int[7] row1 = { 0xFF, 0x20, 0x20, 0x24, 0x20, 0x20, 0xFF };
        int[7] row2 = { 0xFF, 0x08, 0x08, 0x49, 0x08, 0x08, 0xFF };
        int[7] row3 = { 0xFF, 0x82, 0x82, 0x92, 0x82, 0x82, 0xFF };
        int* src = row3;
        if( page == 1 ) src = row1;
        else if( page == 2 ) src = row2;
        for( col = 0; col < 7; col++ ) tbcPageBuffer[ 63 + col ] |= src[ col ];
        return;
    }

    if( page == 0 )
    {
        if( tbcSelectedRow == 10 )
        {
            int[7] v = { 0xC0, 0x40, 0x40, 0x40, 0x40, 0x40, 0xC0 };
            for( col = 0; col < 7; col++ ) tbcPageBuffer[ 63 + col ] |= v[ col ];
        }
        return;
    }

    // pages 1-3: [0x00, 0x00, glyphRow0, glyphRow1, glyphRow2, 0x00, 0x00]
    int d1 = tbcSolution[ 0 ], d2 = tbcSolution[ 1 ], d3 = tbcSolution[ 2 ], d4 = tbcSolution[ 3 ];
    int row;
    for( row = 0; row < 3; row++ )
    {
        int val = 0;
        if( page == 1 ) val = tbcDigit12Byte( d1, d2, row );
        else if( page == 2 ) val = tbcDigit23Byte( d2, d3, row );
        else val = tbcDigit34Byte( d3, d4, row );
        tbcPageBuffer[ 63 + 2 + row ] |= val;
    }
}

// Cursor arrow indicators (5 slots, columns 71-127 interleaved with the
// big digits below) - pages 0-1 only.
int tbcSelectDigitHighlight()
{
    if( ( tbcSelectedRow >= 11 ) && ( tbcSelectedRow < 15 ) ) return tbcSelectedRow - 11;
    return 5;
}

void tbcComposeSelectDigit( int page )
{
    int highlight = tbcSelectDigitHighlight();
    int k;
    for( k = 0; k <= 4; k++ )
    {
        int x = 71 + k * 13;
        int[5] vals = { 0, 0, 0, 0, 0 };
        if( k == highlight )
        {
            if( tbcEnteringDigit )
            {
                if( page == 0 ) { vals[0]=0xFE; vals[1]=0xFE; vals[2]=0x06; vals[3]=0x02; vals[4]=0x00; }
                else { vals[0]=0x7F; vals[1]=0x7F; vals[2]=0x60; vals[3]=0x40; vals[4]=0x00; }
            }
            else
            {
                if( page == 0 ) { vals[0]=0x00; vals[1]=0xF8; vals[2]=0x04; vals[3]=0x02; vals[4]=0x00; }
                else { vals[0]=0x00; vals[1]=0x1F; vals[2]=0x20; vals[3]=0x40; vals[4]=0x00; }
            }
        }
        else if( k == highlight + 1 )
        {
            if( tbcEnteringDigit )
            {
                if( page == 0 ) { vals[0]=0x00; vals[1]=0x02; vals[2]=0x06; vals[3]=0xFE; vals[4]=0xFE; }
                else { vals[0]=0x00; vals[1]=0x40; vals[2]=0x60; vals[3]=0x7F; vals[4]=0x7F; }
            }
            else
            {
                if( page == 0 ) { vals[0]=0x00; vals[1]=0x02; vals[2]=0x04; vals[3]=0xF8; vals[4]=0x00; }
                else { vals[0]=0x00; vals[1]=0x40; vals[2]=0x20; vals[3]=0x1F; vals[4]=0x00; }
            }
        }

        int sc;
        for( sc = 0; sc < 5; sc++ ) tbcPageBuffer[ x + sc ] |= vals[ sc ];
    }
}

// Large 8x16-font answer/guess digits - columns 76,89,102,115 - pages 0-1.
void tbcComposeBigDigits( int page, int displayedRow )
{
    int d0, d1, d2, d3;
    if( displayedRow == 10 )
    {
        d0 = tbcSolution[ 0 ]; d1 = tbcSolution[ 1 ]; d2 = tbcSolution[ 2 ]; d3 = tbcSolution[ 3 ];
    }
    else
    {
        d0 = tbcGuesses[ displayedRow ][ 0 ]; d1 = tbcGuesses[ displayedRow ][ 1 ];
        d2 = tbcGuesses[ displayedRow ][ 2 ]; d3 = tbcGuesses[ displayedRow ][ 3 ];
    }

    int[4] xs = { 76, 89, 102, 115 };
    int[4] ds = { 0, 0, 0, 0 };
    if( d0 != TBC_BLANK ) ds[0] = d0;
    if( d1 != TBC_BLANK ) ds[1] = d1;
    if( d2 != TBC_BLANK ) ds[2] = d2;
    if( d3 != TBC_BLANK ) ds[3] = d3;

    int i, col;
    for( i = 0; i < 4; i++ )
      for( col = 0; col < 8; col++ )
        tbcPageBuffer[ xs[ i ] + col ] |= tbcBigDigits[ ds[ i ] * 16 + page * 8 + col ];
}

// The 2-line status/menu text - columns 72-125, pages 2-3.
void tbcComposeMenuText( int page )
{
    int[9] text;
    int i;

    if( tbcSelectedRow < tbcGuess )
    {
        if( page == 2 )
        {
            int[9] t = { ' ','B','u','l','l','s',' ', 48 + tbcGuesses[ tbcSelectedRow ][ 4 ], ' ' };
            for( i = 0; i < 9; i++ ) text[ i ] = t[ i ];
        }
        else
        {
            int[9] t = { ' ','C','o','w','s',' ',' ', 48 + tbcGuesses[ tbcSelectedRow ][ 5 ], ' ' };
            for( i = 0; i < 9; i++ ) text[ i ] = t[ i ];
        }
    }
    else if( tbcGameOver )
    {
        if( page == 2 )
        {
            if( tbcGameWon ) { int[9] t={' ','Y','o','u',' ','W','o','n',' '}; for(i=0;i<9;i++) text[i]=t[i]; }
            else { int[9] t={' ','Y','o','u',' ','L','o','s','t'}; for(i=0;i<9;i++) text[i]=t[i]; }
        }
        else
        {
            int edge = ' ';
            int edge2 = ' ';
            if( tbcSelectedRow == 15 ) { edge = '['; edge2 = ']'; }
            int[9] t = { edge,'R','e','s','t','a','r','t',edge2 };
            for( i = 0; i < 9; i++ ) text[ i ] = t[ i ];
        }
    }
    else
    {
        if( page == 2 )
        {
            int e1 = ' ';
            int e2 = ' ';
            if( tbcSelectedRow == 15 ) { e1 = '['; e2 = ']'; }
            int[9] t = { ' ',e1,'G','u','e','s','s',e2,' ' };
            for( i = 0; i < 9; i++ ) text[ i ] = t[ i ];
        }
        else
        {
            int edge = ' ';
            int edge2 = ' ';
            if( tbcSelectedRow == 16 ) { edge = '['; edge2 = ']'; }
            int[9] t = { edge,'G','i','v','e',' ','U','p',edge2 };
            for( i = 0; i < 9; i++ ) text[ i ] = t[ i ];
        }
    }

    int col;
    for( col = 0; col < 128; col++ )
      tbcPageBuffer[ col ] |= tbcTextByteAt( text, 9, 72, col );
}

void tbcComposeRow( int page )
{
    int col;
    for( col = 0; col < 128; col++ ) tbcPageBuffer[ col ] = 0;

    if( page > 3 ) return; // gameplay never touches pages 4-7

    int displayedRow = tbcDisplayedRow();
    tbcComposeHistoryArea( page, displayedRow );
    tbcComposeAnswerSquares( page );
    if( page < 2 )
    {
        tbcComposeSelectDigit( page );
        tbcComposeBigDigits( page, displayedRow );
    }
    else
    {
        tbcComposeMenuText( page );
    }
}

#define TBC_MODE_ATTRACT 0
#define TBC_MODE_PLAYING 1

// Upstream's own splash screen relies entirely on real-hardware scroll
// animation (scrollLeftOffset/activateScroll) for its visual interest -
// a purely decorative effect with no gameplay role - shown here as a
// plain static instructions screen instead (see this file's own header
// comment).
void tbcRenderAttract()
{
    int col;
    for( col = 0; col < 128; col++ ) tbcPageBuffer[ col ] = 0;

    int page;
    for( page = 0; page < 8; page++ )
    {
        for( col = 0; col < 128; col++ ) tbcPageBuffer[ col ] = 0;

        if( page == 0 )
        {
            int* t = "BULLS & COWS";
            for( col = 0; col < 128; col++ ) tbcPageBuffer[ col ] |= tbcTextByteAt( t, 12, 28, col );
        }
        if( page == 2 )
        {
            int* t = "GUESS FOUR NUMBERS";
            for( col = 0; col < 128; col++ ) tbcPageBuffer[ col ] |= tbcTextByteAt( t, 18, 10, col );
        }
        if( page == 3 )
        {
            int* t = "BULLS ARE CORRECT";
            for( col = 0; col < 128; col++ ) tbcPageBuffer[ col ] |= tbcTextByteAt( t, 17, 13, col );
        }
        if( page == 4 )
        {
            int* t = "COWS ARE WRONG PLACE";
            for( col = 0; col < 128; col++ ) tbcPageBuffer[ col ] |= tbcTextByteAt( t, 20, 4, col );
        }
        if( page == 6 )
        {
            int* t = "BY DATACUTE";
            for( col = 0; col < 128; col++ ) tbcPageBuffer[ col ] |= tbcTextByteAt( t, 11, 34, col );
        }
        if( page == 7 )
        {
            int* t = "PRESS FIRE";
            for( col = 0; col < 128; col++ ) tbcPageBuffer[ col ] |= tbcTextByteAt( t, 10, 34, col );
        }

        for( col = 0; col < 128; col++ )
          md_drawColumn( col, page, tbcPageBuffer[ col ] );
    }
}

void tbcRenderPlaying()
{
    md_beginFrame();
    int page, col;
    for( page = 0; page < 8; page++ )
    {
        tbcComposeRow( page );
        for( col = 0; col < 128; col++ )
          md_drawColumn( col, page, tbcPageBuffer[ col ] );
    }
}

void tbcRenderFrame( int mode )
{
    md_beginFrame();
    if( mode == TBC_MODE_ATTRACT ) tbcRenderAttract();
    else tbcRenderPlaying();
}

void tbcBeginPlaying()
{
    tbcInitGame();
    tbcState = TBC_STATE_PLAYING;
}

void gameTinyBullsAndCows_init()
{
    tbcBeginAttract();
}

void gameTinyBullsAndCows_forceRedraw()
{
    if( tbcState == TBC_STATE_PLAYING ) tbcRenderFrame( TBC_MODE_PLAYING );
    else tbcRenderFrame( TBC_MODE_ATTRACT );
}

void gameTinyBullsAndCows_update()
{
    if( tbcState == TBC_STATE_ATTRACT )
    {
        bool fireNow = isFirePressed();
        if( fireNow && !tbcPrevFire )
        {
            tbcBeginPlaying();
            tbcRenderFrame( TBC_MODE_PLAYING );
            return;
        }
        tbcPrevFire = fireNow;
        tbcRenderFrame( TBC_MODE_ATTRACT );
    }
    else // TBC_STATE_PLAYING
    {
        if( tbcInputCooldown > 0 ) tbcInputCooldown--;

        tbcReadInputs();
        tbcProcessInputs();

        if( tbcState == TBC_STATE_ATTRACT )
        {
            // tbcProcessInputs() itself requested a return to the attract
            // screen (Restart, from the game-over menu) - render that
            // instead of the now-stale playing state.
            tbcRenderFrame( TBC_MODE_ATTRACT );
            return;
        }

        tbcRenderFrame( TBC_MODE_PLAYING );
    }
}

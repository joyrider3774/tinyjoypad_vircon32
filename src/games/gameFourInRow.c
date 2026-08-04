// =============================================================================
// Four in a Row - ported from four-in-row.ino, bundled in
// Yevgeniy-Olexandrenko/tiny-handheld's own "attiny-arcade" games folder
// (more games/tiny-handheld/software/games/attiny-arcade/four-in-row/).
// Unlike this project's other Ilya-Titov-credited AttinyArcade ports
// (UFO/Oroboros/Run Dude Run), this file carries no author name anywhere -
// its own header just says "connect 4 engine with custom functions /
// ported to ATTiny85 hardware....see pockeTETRIS", and it isn't present
// in webboggles/AttinyArcade (the canonical Ilya Titov repo) either, so
// there's no name to credit - menu credit is "UNKNOWN". No LICENSE file
// exists anywhere in the tiny-handheld repo either.
//
// Connect 4 vs a simple lookahead AI: move a column selector up/down,
// drop a piece, first to connect 4 (any direction) wins; a full board
// ties.
//
// Structural notes:
//  - Genuine #AttinyArcade hardware (analog-ladder axis + 2 discrete
//    digital buttons), not tinyJoypadShim/obonoCoreShim lineage by name -
//    needs no new shim though, since isLeftPressed()/isRightPressed()/
//    isFirePressed() cover the whole input surface.
//  - Upstream's own comment "Input for vertical screen orientation"
//    explains a real mapping quirk: `IsLeft()`/`IsRight()` read the A3
//    analog axis - this project's own established "up/down axis" for
//    every other Daniel-C-lineage game (see CLAUDE.md's Tiny Pinball
//    writeup) - and functionally, in-game, they move the column-select
//    cursor between the 7 on-screen ROWS (screen pages 1-7), i.e. a
//    vertical motion on a normal landscape screen. `IsUp()`/`IsDown()`
//    (the A0 axis) are declared upstream but never actually called
//    anywhere - confirmed dead by grep. So this port maps the *real*
//    up/down d-pad to what upstream calls IsLeft/IsRight (matching what
//    the motion actually looks like on screen), with left/right kept as
//    aliases for robustness - the same "faithfully-computed but sideways
//    art needs its control mapping fixed, not its pixels" treatment
//    already used for Tiny Arkanoid. The board itself renders exactly as
//    upstream computes it (a connect-4 board whose "columns" are screen
//    pages/rows and whose gravity direction is +X on screen) rather than
//    being pixel-transposed to look conventional - this project's own
//    established precedent (Arkanoid again) is to keep the actual pixel
//    output faithful and only fix the *input* mapping.
//  - No sound anywhere in the upstream source at all - nothing to port on
//    that front, a first for this project's AttinyArcade-lineage ports.
//  - The AI (`firoComputerThinkStep()`) is upstream's own real minimax-
//    ish lookahead (AISCANS=20 random rollouts x AIDEPTH=5 plies, per
//    candidate column) - upstream runs the *entire* 7-column evaluation
//    synchronously in one call, visible as a real "thinking" pause on
//    real 8MHz AVR hardware. Spread across real engine frames here *from
//    the start* (not retrofitted after a CPU report) - per this project's
//    own now-standing "always check for optimizations without being
//    asked" practice - needing two rounds even before ever shipping,
//    both caught by measuring with the perf overlay rather than trusting
//    a back-of-envelope estimate alone: (1) eliminating the `boardId`-
//    dispatch function-call overhead for the hot AI-evaluation path
//    (`firoScratchPlay()`/`firoScratchWin()`, operating directly on
//    `firoAiCboard` instead of going through `firoCellGet()`'s dispatch)
//    - the same nested-function-call-overhead shape already found and
//    fixed in Tiny SQuest/Run Dude Run; (2) even after that fix, spreading
//    one whole column's 20 scans per real frame still measured a pegged
//    100% CPU for ~2 real frames per column - reduced further to a single
//    rollout *scan* evaluated per real frame (worst case 7 columns x 20
//    scans = 140 real frames, ~2.3s at 60fps) so each individual frame's
//    AI cost is small, fixed, and safely under budget instead of
//    variable and occasionally right at the edge - removing any doubt
//    about the perf overlay's 0-100-clamped meter being unable to tell
//    "right at budget" from "over budget and silently truncating a
//    render." The longer ~2.3s "thinking" pause this produces is still a
//    perfectly reasonable wait for a turn-based board game, and arguably
//    reproduces upstream's own real, perceptible AVR "thinking" delay
//    better than either of the shorter spreads did.
//  - `board`/`cboard` (a scratch board the AI evaluates candidate moves
//    against) were NOT ported as 2D array function parameters - no
//    existing port in this project passes a 2D array by pointer/value,
//    and 1D array params already need an explicit `int*` (not `int[N]`)
//    per Tiny DDug's own established fix - avoided the risk entirely with
//    a `boardId` selector (0=real board, 1=AI scratch board) resolved by
//    `firoCellGet()`/`firoCellSet()`, the same technique Tiny Dungeon's
//    own `tdngResolveBitmapArray(id)` already proved safe for a similar
//    "which of several arrays" problem.
//  - **A real out-of-bounds read caught by inspection before ever
//    compiling**: `boardWin()`'s own anti-diagonal check indexes
//    `board[x+c][BOARDWIDTH-1-y-c]` - using BOARDWIDTH (7) instead of
//    BOARDHEIGHT (6) in a formula indexing the *height* dimension, which
//    can compute an index up to 6 (out of the valid 0-5 range) or
//    negative - harmless on real AVR (silently reads adjacent flash/RAM),
//    a real out-of-bounds array read here. Rather than "fix" what upstream
//    actually intended (unclear - possibly a genuine authoring bug, not
//    tested here), ported the exact formula but added a bounds guard that
//    skips the check when the computed index falls outside [0,
//    FIRO_BOARD_H) - preserves upstream's actual (if odd) behavior
//    whenever it's in-bounds, without ever risking a real crash.
// =============================================================================

int[80] firoFont =
{
255,129,165,153,153,165,129,255, // X
255,129,153,165,165,153,129,255, // O
0,0,0,0,0,0,0,0,                 // blank
255,129,129,129,129,129,129,255, // empty
// PL/AY/WI/NS/TI/E below are upstream's own byte values with each byte's
// own bits reversed (a per-byte vertical flip, column order unchanged) -
// arrived at per explicit user instruction: starting from upstream's
// original values, rotate 180 degrees then flip (that composition
// algebraically collapses to a plain per-byte bit-reversal - the two
// column-order reversals from "rotate 180" and "flip" cancel out,
// leaving only each byte's own bits reversed). X/O/blank/empty are
// symmetric under this transform too (verified directly), so only these
// 6 letter-pair glyphs needed new data.
228,148,148,228,132,132,135,0,   // PL
101,149,149,149,242,146,146,0,   // AY
138,170,170,170,170,218,138,0,   // WI
147,212,212,178,177,145,150,0,   // NS
250,34,34,34,34,34,34,0,         // TI
248,128,128,240,128,128,248,0,   // E
};

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

#define FIRO_BOARD_W 7
#define FIRO_BOARD_H 6
#define FIRO_TO_WIN 4
#define FIRO_AI_SCANS 20
#define FIRO_AI_DEPTH 5

#define FIRO_CELL_EMPTY 0
#define FIRO_CELL_X 1
#define FIRO_CELL_O 2

#define FIRO_GLYPH_X 0
#define FIRO_GLYPH_O 1
#define FIRO_GLYPH_BLANK 2
#define FIRO_GLYPH_EMPTY 3
#define FIRO_GLYPH_PL 4
#define FIRO_GLYPH_AY 5
#define FIRO_GLYPH_WI 6
#define FIRO_GLYPH_NS 7
#define FIRO_GLYPH_TI 8
#define FIRO_GLYPH_E 9

#define FIRO_STATE_PLAYER_TURN 0
#define FIRO_STATE_COMPUTER_THINK 1
#define FIRO_STATE_COMPUTER_WAIT 2
#define FIRO_STATE_RESULT 3

#define FIRO_RESULT_PLAYER_WIN 1
#define FIRO_RESULT_COMPUTER_WIN 2
#define FIRO_RESULT_TIE 3

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

int[7][6] firoBoard;
int[7][6] firoAiCboard;

int firoState;
int firoWaitFrames;

int firoPlayerPos;
int firoPrevUp;
int firoPrevDown;
int firoPrevFire;

int firoCursorGlyph;
int firoCursorPos;

int firoAiColumn;
int firoAiScanIdx;
int firoAiColumnScore;
int firoAiSmax;
int firoAiChoice;

int firoResultType;

// -----------------------------------------------------------------------------
// Board helpers
// -----------------------------------------------------------------------------

int firoCellGet( int boardId, int x, int y )
{
    if( boardId == 0 ) return firoBoard[ x ][ y ];
    return firoAiCboard[ x ][ y ];
}

void firoCellSet( int boardId, int x, int y, int val )
{
    if( boardId == 0 ) firoBoard[ x ][ y ] = val;
    else firoAiCboard[ x ][ y ] = val;
}

void firoBoardInit( int boardId )
{
    int x, y;
    for( x = 0; x < FIRO_BOARD_W; x++ )
        for( y = 0; y < FIRO_BOARD_H; y++ )
            firoCellSet( boardId, x, y, FIRO_CELL_EMPTY );
}

void firoBoardCopyFromReal()
{
    int x, y;
    for( x = 0; x < FIRO_BOARD_W; x++ )
        for( y = 0; y < FIRO_BOARD_H; y++ )
            firoAiCboard[ x ][ y ] = firoBoard[ x ][ y ];
}

// Places piece p in column x (bottom-most empty row). Returns 1 if a move
// was made, 0 if x is out of range or the column is already full.
int firoBoardPlay( int boardId, int x, int p )
{
    if( x < 0 ) return 0;
    if( x >= FIRO_BOARD_W ) return 0;

    int y;
    for( y = FIRO_BOARD_H - 1; y >= 0; y-- )
    {
        if( firoCellGet( boardId, x, y ) == FIRO_CELL_EMPTY )
        {
            firoCellSet( boardId, x, y, p );
            return 1;
        }
    }
    return 0;
}

// Fast-path AI-evaluation variants: operate directly on firoAiCboard with
// plain array reads instead of going through firoCellGet()'s boardId
// dispatch - measured via the perf overlay that the dispatch-function-call
// overhead (called up to ~26,880 times/frame during one column's worth of
// AI evaluation: 20 scans x up to 2 firoBoardWin() calls x up to 168 inner
// iterations x up to 4 cell reads each) pegged CPU at 100% for the whole
// AI-think window even after already spreading one column per real frame -
// the same nested-function-call-overhead lesson just found in Run Dude
// Run's own bomb rendering, here at a much larger multiplier. Only the AI
// evaluation (called this often) needs this treatment - firoBoardPlay()/
// firoBoardWin() below are still used for the real board, called only a
// couple of times per actual move.
int firoScratchPlay( int x, int p )
{
    if( x < 0 ) return 0;
    if( x >= FIRO_BOARD_W ) return 0;

    int y;
    for( y = FIRO_BOARD_H - 1; y >= 0; y-- )
    {
        if( firoAiCboard[ x ][ y ] == FIRO_CELL_EMPTY )
        {
            firoAiCboard[ x ][ y ] = p;
            return 1;
        }
    }
    return 0;
}

int firoScratchWin( int p )
{
    int result = 0;
    int x, y, c;
    int hwin, vwin, dwin, dwin2, idx2;

    for( x = 0; x < FIRO_BOARD_W; x++ )
    {
        for( y = 0; y < FIRO_BOARD_H; y++ )
        {
            hwin = 0;
            vwin = 0;
            dwin = 0;
            dwin2 = 0;

            for( c = 0; c < FIRO_TO_WIN; c++ )
            {
                if( x + c < FIRO_BOARD_W )
                {
                    if( firoAiCboard[ x + c ][ y ] == p ) hwin = hwin + 1;
                }
                if( y + c < FIRO_BOARD_H )
                {
                    if( firoAiCboard[ x ][ y + c ] == p ) vwin = vwin + 1;
                }
                if( ( y + c < FIRO_BOARD_H ) && ( x + c < FIRO_BOARD_W ) )
                {
                    if( firoAiCboard[ x + c ][ y + c ] == p ) dwin = dwin + 1;
                }

                idx2 = FIRO_BOARD_W - 1 - y - c;
                if( ( x + c < FIRO_BOARD_W ) && ( idx2 >= 0 ) && ( idx2 < FIRO_BOARD_H ) )
                {
                    if( firoAiCboard[ x + c ][ idx2 ] == p ) dwin2 = dwin2 + 1;
                }

                if( hwin == FIRO_TO_WIN ) result = result + 1;
                if( vwin == FIRO_TO_WIN ) result = result + 1;
                if( dwin == FIRO_TO_WIN ) result = result + 1;
                if( dwin2 == FIRO_TO_WIN ) result = result + 1;
            }
        }
    }
    return result;
}

// Counts winning 4-in-a-row positions for player p (>0 means "has won") -
// matches upstream's own boardWin() exactly, including its odd
// BOARDWIDTH-based anti-diagonal formula (see this file's own header
// comment) with an added bounds guard for the out-of-range cases.
int firoBoardWin( int boardId, int p )
{
    int result = 0;
    int x, y, c;
    int hwin, vwin, dwin, dwin2, idx2;

    for( x = 0; x < FIRO_BOARD_W; x++ )
    {
        for( y = 0; y < FIRO_BOARD_H; y++ )
        {
            hwin = 0;
            vwin = 0;
            dwin = 0;
            dwin2 = 0;

            for( c = 0; c < FIRO_TO_WIN; c++ )
            {
                if( x + c < FIRO_BOARD_W )
                {
                    if( firoCellGet( boardId, x + c, y ) == p ) hwin = hwin + 1;
                }
                if( y + c < FIRO_BOARD_H )
                {
                    if( firoCellGet( boardId, x, y + c ) == p ) vwin = vwin + 1;
                }
                if( ( y + c < FIRO_BOARD_H ) && ( x + c < FIRO_BOARD_W ) )
                {
                    if( firoCellGet( boardId, x + c, y + c ) == p ) dwin = dwin + 1;
                }

                idx2 = FIRO_BOARD_W - 1 - y - c;
                if( ( x + c < FIRO_BOARD_W ) && ( idx2 >= 0 ) && ( idx2 < FIRO_BOARD_H ) )
                {
                    if( firoCellGet( boardId, x + c, idx2 ) == p ) dwin2 = dwin2 + 1;
                }

                if( hwin == FIRO_TO_WIN ) result = result + 1;
                if( vwin == FIRO_TO_WIN ) result = result + 1;
                if( dwin == FIRO_TO_WIN ) result = result + 1;
                if( dwin2 == FIRO_TO_WIN ) result = result + 1;
            }
        }
    }
    return result;
}

int firoBoardTie( int boardId )
{
    int x;
    for( x = 0; x < FIRO_BOARD_W; x++ )
    {
        if( firoCellGet( boardId, x, 0 ) == FIRO_CELL_EMPTY ) return 0;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// AI - one single rollout SCAN evaluated per call (not a whole column's
// worth of AISCANS(20) scans), returns 1 once every column has been fully
// evaluated. Originally spread one column per real frame instead, but
// measuring that via the perf overlay showed CPU still pegged at 100% for
// ~2 real frames per column - a single column's own 20 scans x 2
// firoScratchWin() calls each still adds up to a lot of real work in one
// frame even with the dispatch-call-elimination fix above. Spreading down
// to one scan per real frame (worst case 7 columns x 20 scans = 140 real
// frames, ~2.3s at 60fps - still a perfectly reasonable "thinking" pause
// for a turn-based board game) keeps each individual frame's AI cost to a
// small, fixed, safe amount instead of a variable, occasionally-100%
// amount - removes any doubt about the 0-100-clamped perf meter being
// unable to distinguish "right at budget" from "over budget and silently
// truncating a render."
int firoAiColumnPlayable;

int firoComputerThinkStep()
{
    firoBoardCopyFromReal();
    firoAiColumnPlayable = firoScratchPlay( firoAiColumn, FIRO_CELL_O );

    if( firoAiColumnPlayable )
    {
        int r, p;
        for( r = 0; r < FIRO_AI_DEPTH; r++ )
        {
            if( r & 1 ) p = FIRO_CELL_O;
            else p = FIRO_CELL_X;
            firoScratchPlay( arand( FIRO_BOARD_W ), p );
        }
        firoAiColumnScore = firoAiColumnScore + firoScratchWin( FIRO_CELL_O ) - firoScratchWin( FIRO_CELL_X ) * 2;
        firoAiScanIdx = firoAiScanIdx + 1;
    }
    else
    {
        // matches upstream's own `else break` - an unplayable column can
        // only ever be unplayable from its very first scan (the real
        // board doesn't change mid-evaluation), so finish it immediately.
        firoAiScanIdx = FIRO_AI_SCANS;
    }

    if( firoAiScanIdx >= FIRO_AI_SCANS )
    {
        // A real bug found via a user-provided board that reproduced it
        // (bx=0 already full): upstream's own original code compares
        // mscore[m] against smax with NO check that column m was ever
        // actually playable - an unplayable column's score stays at its
        // initial 0, which still beats firoAiSmax's initial -99999999,
        // so firoAiChoice could end up pointing at a FULL column. The
        // WAIT state's own firoBoardPlay() call then legitimately fails
        // on that full column, falling into what was assumed to be a
        // "practically unreachable" defensive branch that unconditionally
        // declares a player win - a real, reachable false-win path, not
        // a hypothetical one. Fixed by requiring the column to have been
        // playable before it's allowed to win the comparison.
        if( firoAiColumnPlayable && ( firoAiColumnScore > firoAiSmax ) )
        {
            firoAiSmax = firoAiColumnScore;
            firoAiChoice = firoAiColumn;
        }
        firoAiColumn = firoAiColumn + 1;
        firoAiScanIdx = 0;
        firoAiColumnScore = 0;
    }

    return firoAiColumn >= FIRO_BOARD_W;
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

int firoFontByte( int glyph, int col )
{
    return firoFont[ glyph * 8 + col ];
}

int firoComputeByte( int x, int page )
{
    // Board cells - screen X 40..87 (6 cells x 8px, board's own "row-
    // within-column" axis), page 1..7 (board's own "column" axis).
    if( ( x >= 40 ) && ( x < 88 ) && ( page >= 1 ) && ( page <= 7 ) )
    {
        int by = ( x - 40 ) / 8;
        int bx = page - 1;
        int cell = firoBoard[ bx ][ by ];
        int glyph;
        if( cell == FIRO_CELL_X ) glyph = FIRO_GLYPH_X;
        else if( cell == FIRO_CELL_O ) glyph = FIRO_GLYPH_O;
        else glyph = FIRO_GLYPH_EMPTY;
        return firoFontByte( glyph, ( x - 40 ) % 8 );
    }

    // Column-select cursor - fixed screen X 16..23, one glyph per page 1..7.
    if( ( x >= 16 ) && ( x < 24 ) && ( page >= 1 ) && ( page <= 7 ) )
    {
        int row = page - 1;
        int glyph;
        if( row == firoCursorPos ) glyph = firoCursorGlyph;
        else glyph = FIRO_GLYPH_BLANK;
        return firoFontByte( glyph, x - 16 );
    }

    // Side status text - fixed screen X 112..119, pages 3/4/6 only.
    if( ( x >= 112 ) && ( x < 120 ) )
    {
        // Page order swapped (page3<->page4 content) per direct user
        // instruction - PL/WI/TI (the first half of each word) needs to
        // read before AY/NS/E (the second half), which meant putting the
        // *second* half at page3 and the first half at page4.
        if( page == 3 )
        {
            if( firoState == FIRO_STATE_RESULT )
            {
                if( firoResultType == FIRO_RESULT_TIE ) return firoFontByte( FIRO_GLYPH_E, x - 112 );
                return firoFontByte( FIRO_GLYPH_NS, x - 112 );
            }
            return firoFontByte( FIRO_GLYPH_AY, x - 112 );
        }
        if( page == 4 )
        {
            if( firoState == FIRO_STATE_RESULT )
            {
                if( firoResultType == FIRO_RESULT_TIE ) return firoFontByte( FIRO_GLYPH_TI, x - 112 );
                return firoFontByte( FIRO_GLYPH_WI, x - 112 );
            }
            return firoFontByte( FIRO_GLYPH_PL, x - 112 );
        }
        if( page == 6 )
        {
            int glyph;
            if( firoState == FIRO_STATE_RESULT )
            {
                // upstream hardcodes the player's mark (X) on a tie too,
                // rather than showing no mark at all - preserved as-is.
                if( firoResultType == FIRO_RESULT_COMPUTER_WIN ) glyph = FIRO_GLYPH_O;
                else glyph = FIRO_GLYPH_X;
            }
            else if( firoState == FIRO_STATE_PLAYER_TURN ) glyph = FIRO_GLYPH_X;
            else glyph = FIRO_GLYPH_O;
            return firoFontByte( glyph, x - 112 );
        }
        return 0;
    }

    return 0;
}

void firoRenderImage()
{
    md_beginFrame();

    int x, page;
    for( page = 0; page < 8; page++ )
    {
        for( x = 0; x < 128; x++ )
        {
            md_drawColumn( x, page, firoComputeByte( x, page ) );
        }
    }
}

// -----------------------------------------------------------------------------
// Game flow
// -----------------------------------------------------------------------------

void firoBeginPlayerTurn()
{
    firoState = FIRO_STATE_PLAYER_TURN;
    firoCursorGlyph = FIRO_GLYPH_X;
    firoCursorPos = firoPlayerPos;
}

void firoNewGame()
{
    firoBoardInit( 0 );
    firoPlayerPos = ( FIRO_BOARD_W - 1 ) / 2;
    firoBeginPlayerTurn();
}

void firoCheckOutcome( int mover, int otherwiseState )
{
    if( firoBoardWin( 0, mover ) )
    {
        if( mover == FIRO_CELL_X ) firoResultType = FIRO_RESULT_PLAYER_WIN;
        else firoResultType = FIRO_RESULT_COMPUTER_WIN;
        firoState = FIRO_STATE_RESULT;
        firoWaitFrames = 300;
    }
    else if( firoBoardTie( 0 ) )
    {
        firoResultType = FIRO_RESULT_TIE;
        firoState = FIRO_STATE_RESULT;
        firoWaitFrames = 300;
    }
    else
    {
        firoState = otherwiseState;
    }
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

void gameFourInRow_init()
{
    InitTinyJoypad();
    firoPrevUp = 0;
    firoPrevDown = 0;
    firoPrevFire = 0;
    firoNewGame();
}

void gameFourInRow_update()
{
    if( firoState == FIRO_STATE_PLAYER_TURN )
    {
        int upNow = isUpPressed() || isLeftPressed();
        int downNow = isDownPressed() || isRightPressed();

        if( upNow && !firoPrevUp )
        {
            firoPlayerPos = firoPlayerPos - 1;
            if( firoPlayerPos < 0 ) firoPlayerPos = 0;
        }
        if( downNow && !firoPrevDown )
        {
            firoPlayerPos = firoPlayerPos + 1;
            if( firoPlayerPos >= FIRO_BOARD_W ) firoPlayerPos = FIRO_BOARD_W - 1;
        }
        firoPrevUp = upNow;
        firoPrevDown = downNow;
        firoCursorPos = firoPlayerPos;

        int fireNow = isFirePressed();
        if( fireNow && !firoPrevFire )
        {
            if( firoBoardPlay( 0, firoPlayerPos, FIRO_CELL_X ) )
            {
                firoCursorPos = -1; // matches upstream's showplayer(10,-1)
                firoAiColumn = 0;
                firoAiScanIdx = 0;
                firoAiColumnScore = 0;
                firoAiSmax = -99999999;
                firoAiChoice = -1;
                firoCheckOutcome( FIRO_CELL_X, FIRO_STATE_COMPUTER_THINK );
            }
        }
        firoPrevFire = fireNow;
    }
    else if( firoState == FIRO_STATE_COMPUTER_THINK )
    {
        if( firoComputerThinkStep() )
        {
            firoCursorGlyph = FIRO_GLYPH_O;
            firoCursorPos = firoAiChoice;
            firoState = FIRO_STATE_COMPUTER_WAIT;
            firoWaitFrames = 60;
        }
    }
    else if( firoState == FIRO_STATE_COMPUTER_WAIT )
    {
        firoWaitFrames = firoWaitFrames - 1;
        if( firoWaitFrames <= 0 )
        {
            if( firoBoardPlay( 0, firoAiChoice, FIRO_CELL_O ) )
            {
                firoCheckOutcome( FIRO_CELL_O, FIRO_STATE_PLAYER_TURN );
                if( firoState == FIRO_STATE_PLAYER_TURN ) firoBeginPlayerTurn();
            }
            else
            {
                // Defensive fallback matching upstream's own - should no
                // longer be reachable after the firoAiColumnPlayable fix
                // above (a column can only get here already confirmed
                // playable during evaluation).
                firoResultType = FIRO_RESULT_PLAYER_WIN;
                firoState = FIRO_STATE_RESULT;
                firoWaitFrames = 300;
            }
        }
    }
    else if( firoState == FIRO_STATE_RESULT )
    {
        firoWaitFrames = firoWaitFrames - 1;
        if( firoWaitFrames <= 0 ) firoNewGame();
    }

    firoRenderImage();
}

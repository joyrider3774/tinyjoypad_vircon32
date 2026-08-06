// =============================================================================
// Frogger (Andy Jackson, 2015-2017, non-commercial-with-attribution;
// ATtiny-Joypad port by Billy Cheung, 2018; artwork by @senkunmusashi) - a
// classic Frogger clone: hop the frog across 3 rows of scrolling logs/crocs
// (don't fall in the river) then 3 rows of scrolling traffic (don't get
// hit), and land in one of 5 docks at the top to score a point and refill
// the frog supply; filling all 5 docks advances the level and speeds
// everything up.
//
// Structural notes:
// - Not `tinyJoypadShim`/`obonoCoreShim` lineage by name (its own hand-
//   rolled `ssd1306_send_byte()` bit-bang driver + a real PCINT ISR for
//   the fire button), but - matching Tiny Lander's and Wren Rollercoaster's
//   own precedent - needed no new shim: Billy Cheung's own header comment
//   documents the identical A0/A3 500-750/750-950 analog thresholds and
//   digital-pin-1 fire button every other game here uses, so button reads
//   go straight onto `isLeftPressed()`/`isRightPressed()`/`isUpPressed()`/
//   `isFirePressed()` from `tinyJoypadShim`. The `ISR(PCINT0_vect)` fire-
//   button interrupt (sets `moveForward=1` on a LOW edge, gated by the same
//   `clickLock` debounce the rest of the input handling already uses) was
//   converted to a plain polled check of `isFirePressed() && !frgClickLock`
//   each tick - Vircon32 has no asynchronous interrupts, and the debounce
//   gate already made the ISR's own behavior indistinguishable from polling
//   once per tick (it can't re-fire until `clickLock` clears either way).
// - Upstream never keeps a real framebuffer - `drawGameScreen()` streams
//   one SSD1306 page's worth of bytes at a time via a sequential cursor
//   (`sendByte`/`sendBlock`), advancing through a "wraparound tail /
//   15 grid columns (with an embedded frog overlay) / wraparound head"
//   structure for each of 6 scrolling rows, alternating scroll direction
//   row by row. Given how genuinely stateful/sequential this cursor-based
//   composite is (each byte's position depends on how many bytes the
//   *previous* iterations already emitted, not a closed-form function of
//   x alone), this was ported as a **direct structural mirror** -
//   `frgComputeGameRowLeft()`/`frgComputeGameRowRight()` walk the exact
//   same loop shape, writing into a `frgGameRowBuf[128]` cursor buffer
//   instead of streaming to hardware - rather than trying to invert the
//   logic into a stateless per-column query function the way most other
//   ports' simpler per-object composites allow. This is lower-risk for
//   logic this intricate, even though it means each row is computed once
//   (not once per pixel), which the row-buffer approach already gives
//   for free.
// - **A genuine VRAM-persistence undershoot, found by literally counting
//   bytes upstream's own loop emits, not from a user report**: each of
//   `drawGameScreen()`'s 6 row-composites sends exactly
//   `(7-shift) + 15*8 + shift = 127` bytes to a 128-column-wide page
//   (the leading/trailing wraparound split always adds up to 7, one byte
//   short of the 8-byte-wide object it's slicing - deliberately, it looks
//   like, trading one column of scrolling precision for a simpler split)
//   - meaning column 127 is *never* written by this function on real
//     hardware, relying on stale SSD1306 VRAM there (and, in the rarer
//     case where the frog is actively hopping across a traffic row, the
//     "draw padding + frog + padding" branch emits only 15 bytes for 2
//     grid cells instead of 16, undershooting by a further byte for that
//     one frame). Fixed the same way as every prior game with this bug
//     class (Pinball/Doc/Bert/Tris/Pipe/Plaque): `frgGameRowBuf` is always
//     padded out to the full 128 columns with a plain background byte
//     after the real cursor-driven writes finish, so no column is ever
//     left holding stale/undefined content.
// - `drawFrog(mode, frogDead)`'s own position formula (blockShiftL/R-
//   adjusted when frogRow is 1/2/3, exact `frogColumn*8` otherwise) is
//   reused faithfully in 3 separate contexts, matching every one of
//   upstream's own call sites: the frog's "waiting at the start row"
//   sprite (frogRow==7, drawn as part of the bottom-page composite), the
//   `frogColumn==0` special case (the per-column grid loop can only ever
//   match a traffic-row frog at `col+1==frogColumn`, i.e. frogColumn>=1,
//   so a frog sitting in the literal leftmost column needs this explicit
//   post-pass overlay to ever get drawn at all), and the 4-frame death-
//   animation blink (which force-draws regardless of `frogRow`).
// - `doNumber`/`ssd1306_char_f6x8` (this game's own truncated font, a
//   *different* remap formula from Wren's own font despite looking
//   similar: `c-=9` in the final bracket here vs. Wren's `c-=6` -
//   confirmed directly from this game's own `font6x8AJ2.h`, not assumed
//   from the last game's formula) ported as `frgTextByte`/`frgNumberByte`,
//   the same shape as Wren's own `wrenTextByte`/`wrenNumberByte` -
//   including the `strlen()`-bounds-check fix Wren's own text renderer
//   needed only after a user bug report, applied here proactively from
//   the start now that the bug class is known.
// - `beep(bCount,bDelay)` bit-bangs a square wave via raw NOP-loop counts,
//   the same shape as Wren's own driver - no exact NOP-count-to-Hz
//   conversion exists, so ported via a heuristic `frgBeep()`-equivalent
//   mapping (higher bDelay -> lower pitch). Unlike Wren, this game's own
//   bDelay values range much wider (0-1000, vs Wren's ~0-700 mostly-
//   clamped range), so the mapping first rescales bDelay into a 0-250
//   band before inverting it - reusing Wren's exact `255-bDelay` formula
//   verbatim here would have clamped almost every one of this game's own
//   sound sequences to the same floor pitch, losing all tonal variation.
//   Every one of this game's 8 real sound sequences (jump, 4 death-
//   animation groups, dock-fill chime, level-up flash, game-over/new-high
//   sweep) is table-driven through the shared frame-stepped note
//   sequencer (`frgStartNoteSeq`/`frgAdvanceNoteSeq`, matching every other
//   port's own established shape) rather than reproducing each raw
//   `beep()` call site - Vircon32's queueless audio channel would only
//   ever make the *last* of several synchronous calls per site audible
//   anyway, the same reasoning already applied throughout this project.
// - `(byte)floor(frogColumn/3)` (the dock-index calculation): frogColumn
//   is a plain non-negative int and 3 is an int literal, so `frogColumn/3`
//   is already a truncating integer division before `floor()` ever runs -
//   ported as plain integer division, the same "already-integer floor()
//   is a no-op" finding already documented for Wren's own `boost/40`.
// - EEPROM high-score persistence restored (see the project-wide "Real
//   persistent high-score saving" section in CLAUDE.md - a 2-byte big-
//   endian score at address 0, matching upstream exactly). The "hold
//   fire ~2s" dual gesture is fully restored too, matching upstream's own
//   exact branching: fire+left/right resets the persisted high score
//   (`frgShowResetMsg`, a "-HIGH SCORE RESET-" banner); fire alone
//   toggles mute (a pure in-memory flag, unaffected by the reset branch).
// =============================================================================

// -----------------------------------------------------------------------------
//   Data: sprites/bitmaps (extracted + byte-diff verified against the
//   .ino - the extraction script's first pass mis-handled the
//   `#ifdef SMALLLOGS ... #else ... #endif` wrapped around the log
//   sprites (SMALLLOGS is commented out upstream, so only the #else
//   "bigger logs" branch actually compiles) and grabbed both branches,
//   yielding 144 values instead of the correct 120 - caught by the
//   count not matching `bitmaps[15][8]`'s own expected 120, fixed with a
//   second extraction pass that explicitly strips the inactive branch.
// -----------------------------------------------------------------------------

int[120] frgbitmaps =
{
131,220,122,63,63,122,220,131,153,189,219,126,126,60,231,129,
129,231,60,126,126,219,189,153,60,126,215,181,173,191,255,237,
173,173,255,183,245,191,183,173,237,189,195,189,165,189,66,60,
0,127,65,85,85,85,85,85,85,85,85,85,85,85,85,85,
65,127,34,127,127,99,34,28,65,99,70,110,124,126,122,62,
188,254,126,62,190,190,252,124,120,56,56,56,112,96,96,64,
0,28,34,99,127,127,34,34,34,62,62,127,99,99,34,28,
34,62,62,127,99,99,34,28,
};

int[200] frgtitleBmp =
{
0,0,0,0,0,0,0,0,128,192,240,124,6,115,89,67,
6,60,56,48,48,56,62,38,123,89,67,6,124,240,192,128,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,120,255,
207,1,0,0,48,96,224,192,128,128,128,128,128,128,128,192,
192,96,48,0,0,0,1,207,254,120,0,0,0,0,0,0,
124,254,134,14,14,28,24,49,127,254,252,28,24,56,56,56,
57,57,57,57,57,57,57,56,56,56,56,24,28,252,254,127,
57,24,28,14,14,198,254,60,0,1,7,14,28,56,112,192,
192,128,3,7,252,248,0,0,240,192,192,192,192,192,224,240,
0,0,248,252,15,3,128,192,224,112,56,28,14,3,1,0,
4,6,15,15,6,6,3,3,3,99,115,51,59,255,255,127,
63,56,240,192,0,240,248,63,127,255,255,59,51,99,99,3,
3,3,6,6,15,15,6,0,
};

int[384] frgFONT =
{
0,0,0,0,0,0,0,8,8,8,8,8,0,0,96,96,
0,0,0,60,64,48,64,60,0,62,81,73,69,62,0,0,
66,127,64,0,0,66,97,81,73,70,0,33,65,69,75,49,
0,24,20,18,127,16,0,39,69,69,69,57,0,60,74,73,
73,48,0,1,113,9,5,3,0,54,73,73,73,54,0,6,
73,73,41,30,0,0,54,54,0,0,0,124,18,17,18,124,
0,127,73,73,73,54,0,62,65,65,65,34,0,127,65,65,
34,28,0,127,73,73,73,65,0,127,9,9,9,1,0,62,
65,73,73,122,0,127,8,8,8,127,0,0,65,127,65,0,
0,32,64,65,63,1,0,127,8,20,34,65,0,127,64,64,
64,64,0,127,2,12,2,127,0,127,4,8,16,127,0,62,
65,65,65,62,0,127,9,9,9,6,0,62,65,81,33,94,
0,127,9,25,41,70,0,70,73,73,73,49,0,1,1,127,
1,1,0,63,64,64,64,63,0,31,32,64,32,31,0,63,
64,56,64,63,0,32,84,84,84,120,0,127,72,68,68,56,
0,56,68,68,68,32,0,56,68,68,72,127,0,56,84,84,
84,24,0,8,126,9,1,2,0,24,164,164,164,124,0,127,
8,4,4,120,0,0,68,125,64,0,0,64,128,132,125,0,
0,127,16,40,68,0,0,0,65,127,64,0,0,124,4,24,
4,120,0,124,8,4,4,120,0,56,68,68,68,56,0,252,
36,36,36,24,0,24,36,36,24,252,0,124,8,4,4,8,
0,72,84,84,84,32,0,4,63,68,64,32,0,60,64,64,
32,124,0,28,32,64,32,28,0,60,64,48,64,60,0,68,
40,16,40,68,0,28,160,160,160,124,0,50,73,89,81,62,
};

// -----------------------------------------------------------------------------
//   Sound (see header comment above for the bDelay-rescaling rationale)
// -----------------------------------------------------------------------------

int[6] frgJumpNotes =
{
155,31,180,31,205,31,
};
#define FRG_JUMP_COUNT 3

int[10] frgDeath1Notes =
{
250,40,243,40,230,40,218,40,205,40,
};
int[10] frgDeath2Notes =
{
193,40,180,40,168,40,155,40,143,40,
};
int[10] frgDeath3Notes =
{
130,40,118,40,105,40,93,40,80,40,
};
int[10] frgDeath4Notes =
{
68,40,55,40,43,40,30,40,18,40,
};
#define FRG_DEATH_COUNT 5

int[16] frgDockFillNotes =
{
5,11,30,11,55,11,80,11,105,11,130,11,155,11,180,11,
};
#define FRG_DOCKFILL_COUNT 8

int[6] frgLevelFlashNotes =
{
55,21,105,21,155,21,
};
#define FRG_LEVELFLASH_COUNT 3

int[20] frgGameOverNotes =
{
80,31,93,31,105,31,118,31,130,31,143,31,155,31,168,31,180,31,193,31,
};
#define FRG_GAMEOVER_COUNT 10

int frgMute;
bool frgShowResetMsg;

int frgSeqActive;
int* frgSeqNotes;
int frgSeqCount;
int frgSeqIndex;
int frgSeqWaitFrames;

void frgStartNoteSeq( int* notes, int count )
{
    frgSeqNotes = notes;
    frgSeqCount = count;
    frgSeqIndex = 0;
    frgSeqActive = 1;
    frgSeqWaitFrames = 0;
}

void frgAdvanceNoteSeq()
{
    if( !frgSeqActive )
        return;
    if( frgSeqWaitFrames > 0 )
    {
        frgSeqWaitFrames--;
        return;
    }
    if( frgSeqIndex >= frgSeqCount )
    {
        frgSeqActive = 0;
        return;
    }
    int freq = frgSeqNotes[ frgSeqIndex * 2 ];
    int dur = frgSeqNotes[ frgSeqIndex * 2 + 1 ];
    if( !frgMute )
        Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    frgSeqWaitFrames = waitFrames;
    frgSeqIndex++;
}

// -----------------------------------------------------------------------------
//   Font / number rendering
// -----------------------------------------------------------------------------

int frgCharIndex( int ch )
{
    int c = ch - 32;
    if( c > 0 ) c = c - 12;
    if( c > 15 ) c = c - 6;
    if( c > 40 ) c = c - 9;
    return c;
}

int frgTextByte( int x, int y, int startX, int pageY, int* str )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= strlen( str ) ) return 0;
    int ch = str[charIdx];
    if( ch == 0 ) return 0;
    int within = rel - charIdx * 6;
    int fontIdx = frgCharIndex( ch );
    return frgFONT[ fontIdx * 6 + within ];
}

int frgCountDigits( int value )
{
    if( value <= 0 ) return 1;
    int v = value;
    int n = 0;
    while( v > 0 ) { n++; v = v / 10; }
    return n;
}

int frgDigitAt( int value, int posFromLeft, int totalDigits )
{
    int power = 1;
    int i;
    for( i = 0; i < totalDigits - 1 - posFromLeft; i++ )
        power = power * 10;
    return ( value / power ) % 10;
}

int frgNumberByte( int x, int y, int startX, int pageY, int value )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int totalDigits = frgCountDigits( value );
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= totalDigits ) return 0;
    int within = rel - charIdx * 6;
    int digit = frgDigitAt( value, charIdx, totalDigits );
    int fontIdx = frgCharIndex( 48 + digit );
    return frgFONT[ fontIdx * 6 + within ];
}

// -----------------------------------------------------------------------------
//   Game state (direct translation of upstream's own globals - all plain
//   int upstream too, no narrow-type wraparound-reliance concerns)
// -----------------------------------------------------------------------------

int frgWatchDog;
int frgStopAnimate;
int frgLives;
int[5] frgFrogDocks;
int frgFlipFlop;
int frgFlipFlopShift;
int frgFrogColumn;
int frgFrogRow;
int frgFrogLeftLimit;
int frgFrogRightLimit;
int frgLevel;
int frgBlockShiftL;
int frgBlockShiftR;
int frgInterimStep;
int frgMoveDelay;
int frgDockedFrogs;
int frgScore;
int frgTopScore;
int frgNewHigh;
int[6][16] frgGrid;
int frgFrogMode;
int frgMoveForward;
int frgMoveLeft;
int frgMoveRight;

int frgTickCounter;
int frgClickBase;
int frgClickLock;

#define FRG_CLICK_DELAY 7      // ~120ms @ 60fps
#define FRG_CLICK_HALF_DELAY 4 // ~60ms @ 60fps
#define FRG_MOVEBASE 1000

// -----------------------------------------------------------------------------
//   Core byte helpers (mirror sendByte/sendBlock)
// -----------------------------------------------------------------------------

int frgByteVal( int fill, int inverse )
{
    if( inverse ) return (~fill) & 0xFF;
    return fill & 0xFF;
}

int frgBlockByte( int fill, int inverse, int idx )
{
    if( fill > 0 )
    {
        int b = frgbitmaps[ (fill-1)*8 + idx ];
        return frgByteVal( b, inverse );
    }
    if( inverse ) return 0xFF;
    return 0;
}

// -----------------------------------------------------------------------------
//   Game logic - direct ports of initScreen()/moveBlocks()/checkCollision()
// -----------------------------------------------------------------------------

void frgResetDock( int value )
{
    int incr;
    for( incr = 0; incr < 5; incr++ ) frgFrogDocks[incr] = value;
}

void frgInitScreen()
{
    int[6] initCounter;
    initCounter[0]=3; initCounter[1]=2; initCounter[2]=4; initCounter[3]=2; initCounter[4]=2; initCounter[5]=3;
    int[6] gapCounter;
    gapCounter[0]=-2; gapCounter[1]=-3; gapCounter[2]=-4; gapCounter[3]=-4; gapCounter[4]=-3; gapCounter[5]=-5;
    int[6] counter;
    int stepMode = 0;
    int stepShift = 0;
    int crocStartColumn = 0;

    if( frgLevel == 1 ) gapCounter[5] = -14;
    if( frgLevel < 3 ) gapCounter[4] = -6;
    if( frgLevel < 4 ) gapCounter[3] = -7;
    if( frgLevel > 4 )
    {
        int incr;
        for( incr = 1; incr < 3; incr++ ) gapCounter[incr]--;
    }
    if( frgLevel > 7 )
    {
        gapCounter[3] = -4;
        gapCounter[4] = -2;
        gapCounter[5] = -3;
    }
    if( frgLevel > 2 ) crocStartColumn = 5;
    if( frgLevel > 6 ) crocStartColumn = 9;

    int incr;
    for( incr = 0; incr < 6; incr++ ) counter[incr] = initCounter[incr];

    int col, row;
    for( col = 0; col < 16; col++ )
        for( row = 0; row < 6; row++ )
            frgGrid[row][col] = 0;

    stepMode = 0;
    for( row = 0; row < 6; row++ )
    {
        for( col = 0; col < 15; col++ )
        {
            if( counter[row] > 0 )
            {
                if( 14 - row > counter[row] )
                {
                    if( counter[row] == 1 )
                        if( stepMode == 1 ) stepMode = 2;
                    if( row > 2 ) stepShift = 3; else stepShift = 0;
                    if( row == 4 ) stepShift = 9;

                    if( row > 0 )
                        frgGrid[row][col] = 4 + stepMode + stepShift;
                    else if( col >= crocStartColumn )
                        frgGrid[row][col] = 4 + stepMode + stepShift;
                    else
                        frgGrid[row][col] = 10 + stepMode;
                    if( stepMode == 0 ) stepMode = 1;
                    if( stepMode == 2 ) stepMode = 0;
                }
            }
            counter[row]--;
            if( counter[row] <= gapCounter[row] ) counter[row] = initCounter[row];
        }
    }
}

void frgMoveBlocks()
{
    int direct = 0;
    int row;

    if( frgFlipFlop == 1 ) frgFlipFlop = 0; else frgFlipFlop = 1;

    for( row = 0; row < 6; row++ )
    {
        if( frgFrogRow < 4 && frgFrogRow > 0 )
        {
            if( frgFrogRow == row + 1 )
            {
                if( direct == 1 && frgFlipFlop == 1 )
                {
                    if( frgFrogColumn >= 14 ) frgStopAnimate = 1; else frgFrogColumn++;
                }
                else if( direct == 0 )
                {
                    if( frgFrogColumn < 1 ) frgStopAnimate = 1; else frgFrogColumn--;
                }
            }
        }
        if( direct == 0 )
        {
            int temp = frgGrid[row][0];
            int col;
            for( col = 0; col < 15; col++ ) frgGrid[row][col] = frgGrid[row][col+1];
            frgGrid[row][15] = temp;
            direct = 1;
        }
        else
        {
            if( frgFlipFlop == 1 )
            {
                int temp = frgGrid[row][15];
                int col;
                for( col = 15; col > 0; col-- ) frgGrid[row][col] = frgGrid[row][col-1];
                frgGrid[row][0] = temp;
            }
            direct = 0;
        }
    }
}

void frgCheckCollision()
{
    if( frgFrogRow > 0 && frgFrogRow < 4 && frgGrid[frgFrogRow-1][frgFrogColumn] == 0 ) frgStopAnimate = 1;
    if( frgFrogRow > 0 && frgFrogRow < 4 && frgGrid[frgFrogRow-1][frgFrogColumn] > 9 ) frgStopAnimate = 1;
    if( frgFrogRow < 7 && frgFrogRow > 3 && ( frgGrid[frgFrogRow-1][frgFrogColumn] != 0 || frgGrid[frgFrogRow-1][frgFrogColumn-1] != 0 ) ) frgStopAnimate = 1;
}

// -----------------------------------------------------------------------------
//   Render: game-row composite (mirrors drawGameScreen's own sequential
//   cursor-driven byte stream for one grid row, see header comment)
// -----------------------------------------------------------------------------

int[128] frgGameRowBuf;

void frgComputeGameRowLeft( int row, int mode )
{
    int inverse = 0;
    if( row < 3 ) inverse = 1;
    int pos = 0;
    int incr;
    int wrapVal = frgGrid[row][15];

    for( incr = 0; incr < 7 - frgBlockShiftL; incr++ )
    {
        if( wrapVal == 0 ) frgGameRowBuf[pos] = frgByteVal( 0, inverse );
        else frgGameRowBuf[pos] = frgByteVal( frgbitmaps[ (wrapVal-1)*8 + 1 + frgBlockShiftL + incr ], inverse );
        pos++;
    }

    int col = 0;
    while( col < 15 )
    {
        if( frgFrogRow == row+1 && frgFrogColumn == col && frgFrogRow < 4 && frgFrogRow > 0 )
        {
            int b;
            for( b = 0; b < 8; b++ ) frgGameRowBuf[pos+b] = frgBlockByte( mode, 0, b );
            pos += 8;
            col++;
        }
        else if( frgStopAnimate == 0 && frgFrogRow == row+1 && frgFrogColumn == col+1 && frgFrogRow > 3 && frgFrogRow < 7 )
        {
            for( incr = 0; incr < frgBlockShiftL; incr++ ) frgGameRowBuf[pos++] = frgByteVal(0,0);
            int b;
            for( b = 0; b < 8; b++ ) frgGameRowBuf[pos+b] = frgBlockByte( mode, 0, b );
            pos += 8;
            for( incr = 0; incr < 7 - frgBlockShiftL; incr++ ) frgGameRowBuf[pos++] = frgByteVal(0,0);
            col += 2;
        }
        else
        {
            int b;
            for( b = 0; b < 8; b++ ) frgGameRowBuf[pos+b] = frgBlockByte( frgGrid[row][col], inverse, b );
            pos += 8;
            col++;
        }
    }

    for( incr = 0; incr < frgBlockShiftL; incr++ )
    {
        if( wrapVal == 0 ) frgGameRowBuf[pos] = frgByteVal( 0, inverse );
        else frgGameRowBuf[pos] = frgByteVal( frgbitmaps[ (wrapVal-1)*8 + incr ], inverse );
        pos++;
    }

    while( pos < 128 ) frgGameRowBuf[pos++] = frgByteVal( 0, inverse );
}

void frgComputeGameRowRight( int row, int mode )
{
    int inverse = 0;
    if( row > 0 && row < 3 ) inverse = 1;
    int pos = 0;
    int incr;
    int wrapVal = frgGrid[row][15];

    for( incr = 0; incr < frgBlockShiftR; incr++ )
    {
        if( wrapVal == 0 ) frgGameRowBuf[pos] = frgByteVal( 0, inverse );
        else frgGameRowBuf[pos] = frgByteVal( frgbitmaps[ (wrapVal-1)*8 + incr + (8-frgBlockShiftR) ], inverse );
        pos++;
    }

    int col = 0;
    while( col < 15 )
    {
        if( frgFrogRow == row+1 && frgFrogColumn == col && frgFrogRow < 4 && frgFrogRow > 0 )
        {
            int b;
            for( b = 0; b < 8; b++ ) frgGameRowBuf[pos+b] = frgBlockByte( mode, 0, b );
            pos += 8;
            col++;
        }
        else if( frgStopAnimate == 0 && frgFrogRow == row+1 && frgFrogColumn == col+1 && frgFrogRow > 3 && frgFrogRow < 7 )
        {
            for( incr = 0; incr < 7 - frgBlockShiftR; incr++ ) frgGameRowBuf[pos++] = frgByteVal(0,0);
            int b;
            for( b = 0; b < 8; b++ ) frgGameRowBuf[pos+b] = frgBlockByte( mode, 0, b );
            pos += 8;
            for( incr = 0; incr < frgBlockShiftR; incr++ ) frgGameRowBuf[pos++] = frgByteVal(0,0);
            col += 2;
        }
        else
        {
            int b;
            for( b = 0; b < 8; b++ ) frgGameRowBuf[pos+b] = frgBlockByte( frgGrid[row][col], inverse, b );
            pos += 8;
            col++;
        }
    }

    for( incr = 0; incr < 7 - frgBlockShiftR; incr++ )
    {
        if( wrapVal == 0 ) frgGameRowBuf[pos] = frgByteVal( 0, inverse );
        else frgGameRowBuf[pos] = frgByteVal( frgbitmaps[ (wrapVal-1)*8 + incr ], inverse );
        pos++;
    }

    while( pos < 128 ) frgGameRowBuf[pos++] = frgByteVal( 0, inverse );
}

// Mirrors drawFrog(mode,frogDead)'s own position formula - reused by the
// frogColumn==0 overlay, the start-row frog, and the death-animation blink.
int frgFrogXStart( int screenRow )
{
    if( screenRow == 1 || screenRow == 3 ) return frgFrogColumn*8 + 7 - frgBlockShiftL;
    if( screenRow == 2 ) return frgFrogColumn*8 + frgBlockShiftR;
    return frgFrogColumn * 8;
}

void frgOverlayFrogAt( int* buf, int screenRow, int mode )
{
    int xStart = frgFrogXStart( screenRow );
    int b;
    for( b = 0; b < 8; b++ )
    {
        int x = xStart + b;
        if( x >= 0 && x < 128 ) buf[x] = frgBlockByte( mode, 0, b );
    }
}

// -----------------------------------------------------------------------------
//   Render: docks (page 0) + score/lives/start-row-frog (page 7)
// -----------------------------------------------------------------------------

int frgComputeDockByte( int x )
{
    // Docks only ever occupy columns [3,113) (5 docks * 24px stride, last
    // one 14px wide starting at 99) - bounding here avoids running the
    // full 5-dock scan below for the ~58 columns that can never match any
    // of them, rather than relying on each iteration's own bounds check
    // to reject them one at a time.
    if( x < 3 || x >= 113 ) return 0;

    int incr;
    for( incr = 0; incr < 5; incr++ )
    {
        int drawPos = 3 + incr * 24;
        if( x < drawPos || x >= drawPos + 14 ) continue;
        int off = x - drawPos;
        if( off == 0 || off == 13 ) return 0xFF;
        if( off == 1 || off == 2 || off == 11 || off == 12 ) return 1;
        int blockOff = off - 3;
        if( frgFrogDocks[incr] == 1 ) return frgbitmaps[ 0*8 + blockOff ];
        return 1;
    }
    return 0;
}

// Set by the death-animation state (see gameFrogger_update()) to force the
// frog's blank/sprite blink at its exact death position, overriding the
// normal position-matching draws - mirrors upstream's own drawFrog(x,1)
// (frogDead=1) forcing an unconditional draw regardless of frogRow.
int frgDeathOverlayActive;
int frgDeathOverlayMode;

// Refreshed once per playing-frame render (frgRenderPlaying()), not
// recomputed on every one of the 128 columns' own frgComputeBottomByte()
// call - frgScore is constant for the whole page, so counting its digits
// per-column was pure repeated work for an unchanging result.
int frgScoreDigits;

int frgComputeBottomByte( int x )
{
    if( x < frgScoreDigits * 6 )
    {
        int v = frgNumberByte( x, 7, 0, 7, frgScore );
        if( v != 0 ) return v;
    }

    if( frgDeathOverlayActive && frgFrogRow == 7 )
    {
        int fx = frgFrogColumn * 8;
        if( x >= fx && x < fx + 8 )
            return frgBlockByte( frgDeathOverlayMode, 0, x - fx );
    }
    else if( frgFrogRow == 7 )
    {
        int fx = frgFrogColumn * 8;
        if( x >= fx && x < fx + 8 )
            return frgBlockByte( frgFrogMode, 0, x - fx );
    }

    if( x >= 13*8 && x < 15*8 )
    {
        int col = x / 8;
        int within = x % 8;
        int incrFromRight = 15 - col;
        if( frgLives >= incrFromRight )
            return frgBlockByte( 1, 0, within );
        return 0;
    }

    return 0;
}

// -----------------------------------------------------------------------------
//   Top-level render dispatch
// -----------------------------------------------------------------------------

#define FRG_MODE_ATTRACT 0
#define FRG_MODE_PLAYING 1
#define FRG_MODE_LEVELUP_FLASH 2
#define FRG_MODE_LEVELUP_TEXT 3
#define FRG_MODE_GAMEOVER 4
#define FRG_MODE_NEWHIGH 5

void frgRenderPlaying()
{
    md_beginFrame();
    int x, row;

    for( x = 0; x < 128; x++ )
    {
        int v = frgComputeDockByte( x );
        if( v != 0 ) md_drawColumn( x, 0, v );
    }

    for( row = 0; row < 6; row++ )
    {
        if( row % 2 == 0 ) frgComputeGameRowLeft( row, frgFrogMode );
        else frgComputeGameRowRight( row, frgFrogMode );

        if( frgDeathOverlayActive && frgFrogRow == row + 1 )
            frgOverlayFrogAt( frgGameRowBuf, row + 1, frgDeathOverlayMode );
        else if( frgFrogColumn == 0 && frgFrogRow == row + 1 )
            frgOverlayFrogAt( frgGameRowBuf, row + 1, frgFrogMode );

        for( x = 0; x < 128; x++ )
            if( frgGameRowBuf[x] != 0 ) md_drawColumn( x, row + 1, frgGameRowBuf[x] );
    }

    frgScoreDigits = frgCountDigits( frgScore );
    for( x = 0; x < 128; x++ )
    {
        int v = frgComputeBottomByte( x );
        if( v != 0 ) md_drawColumn( x, 7, v );
    }
}

void frgRenderFrame( int mode )
{
    if( mode == FRG_MODE_PLAYING )
    {
        frgRenderPlaying();
        return;
    }

    int x, y, val;
    md_beginFrame();
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
        {
            val = 0;
            if( mode == FRG_MODE_ATTRACT )
            {
                // Title artwork (@senkunmusashi) sits at columns 85-124,
                // rows 2-6 - doesn't overlap any of the text below, which
                // all stays within the first ~78 columns.
                if( y >= 2 && y <= 6 && x >= 85 && x < 125 )
                    val = val | frgtitleBmp[ (y-2)*40 + (x-85) ];

                if( y == 2 ) val = val | frgTextByte( x, y, 0, 2, "F R O G G E R" );
                else if( y == 4 ) val = val | frgTextByte( x, y, 0, 4, "andy jackson" );
                else if( y == 6 ) val = val | frgTextByte( x, y, 0, 6, "artwork by" );
                // Upstream's own source string is literally "zsenkunmusashi"
                // - the font substitutes a special "@"-shaped glyph at the
                // 'z' slot (see font6x8AJ2.h's own "@ in place of z"
                // comment), so the literal '@' character must NOT be used
                // here - it maps through frgCharIndex to a *different* font
                // slot (14, ':') and renders wrong.
                else if( y == 7 ) val = val | frgTextByte( x, y, 0, 7, "zsenkunmusashi" );
                else if( y == 0 && frgShowResetMsg ) val = val | frgTextByte( x, y, 8, 0, "-HIGH SCORE RESET-" );
                else if( y == 0 && frgMute ) val = val | frgTextByte( x, y, 32, 0, "-- MUTE --" );
            }
            else if( mode == FRG_MODE_LEVELUP_TEXT )
            {
                if( y == 1 ) val = val | frgTextByte( x, y, 35, 1, "---------" );
                else if( y == 3 ) val = val | frgTextByte( x, y, 35, 3, " LEVEL " ) | frgNumberByte( x, y, 77, 3, frgLevel );
                else if( y == 5 ) val = val | frgTextByte( x, y, 35, 5, "---------" );
            }
            else if( mode == FRG_MODE_GAMEOVER )
            {
                if( y == 1 ) val = val | frgTextByte( x, y, 11, 1, "----------------" );
                else if( y == 2 ) val = val | frgTextByte( x, y, 11, 2, "G A M E  O V E R" );
                else if( y == 3 ) val = val | frgTextByte( x, y, 11, 3, "----------------" );
                else if( y == 5 ) val = val | frgTextByte( x, y, 37, 5, "SCORE:" ) | frgNumberByte( x, y, 75, 5, frgScore );
                else if( y == 7 && !frgNewHigh ) val = val | frgTextByte( x, y, 21, 7, "HIGH SCORE:" ) | frgNumberByte( x, y, 88, 7, frgTopScore );
            }
            else if( mode == FRG_MODE_NEWHIGH )
            {
                if( y == 1 ) val = val | frgTextByte( x, y, 10, 1, "----------------" );
                else if( y == 3 ) val = val | frgTextByte( x, y, 10, 3, " NEW HIGH SCORE " );
                else if( y == 7 ) val = val | frgTextByte( x, y, 10, 7, "----------------" );
                else if( y == 5 ) val = val | frgNumberByte( x, y, 50, 5, frgTopScore );
            }
            md_drawColumn( x, y, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define FRG_STATE_ATTRACT 0
#define FRG_STATE_PLAYING 1
#define FRG_STATE_DEATH_ANIM 2
#define FRG_STATE_LEVELUP_FLASH 3
#define FRG_STATE_LEVELUP_TEXT 4
#define FRG_STATE_GAMEOVER_WAIT 5
#define FRG_STATE_NEWHIGH_WAIT 6

int frgState;
int frgWaitFrames;
int frgDeathStep;
int frgLevelFlashStep;
int frgFireHeld;
int frgFireHoldTicks;
int frgMuteActionDone;

void frgBeginAttract()
{
    frgFireHeld = 0;
    frgFireHoldTicks = 0;
    frgMuteActionDone = 0;
    frgState = FRG_STATE_ATTRACT;
}

void frgBeginLevelUpFlash()
{
    frgLevelFlashStep = 0;
    frgSeqActive = 0; // let the LEVELUP_FLASH tick run its first step immediately
    frgState = FRG_STATE_LEVELUP_FLASH;
}

void frgBeginLevelUpText()
{
    frgRenderFrame( FRG_MODE_LEVELUP_TEXT );
    frgWaitFrames = 90;
    frgState = FRG_STATE_LEVELUP_TEXT;
}

void frgBeginGameOverWait()
{
    frgRenderFrame( FRG_MODE_GAMEOVER );
    frgWaitFrames = 60;
    frgState = FRG_STATE_GAMEOVER_WAIT;
}

void frgBeginNewHighWait()
{
    frgRenderFrame( FRG_MODE_NEWHIGH );
    frgStartNoteSeq( frgGameOverNotes, FRG_GAMEOVER_COUNT );
    frgWaitFrames = 72;
    frgState = FRG_STATE_NEWHIGH_WAIT;
}

void frgBeginGame()
{
    frgStopAnimate = 0;
    frgScore = 0;
    frgMoveDelay = FRG_MOVEBASE;
    frgLevel = 1;
    frgFrogColumn = 8;
    frgFrogRow = 7;
    frgClickLock = 0;
    frgFrogMode = 1;
    frgInterimStep = 0;
    frgBlockShiftL = 0;
    frgBlockShiftR = 0;
    frgFlipFlop = 1;
    frgFlipFlopShift = 1;
    frgDockedFrogs = 0;
    frgLives = 2;
    frgFrogRightLimit = 12;
    frgWatchDog = 1;
    frgMoveForward = 0;
    frgMoveLeft = 0;
    frgMoveRight = 0;

    frgInitScreen();
    frgResetDock(0);

    frgState = FRG_STATE_PLAYING;
}

void frgBeginDeathAnim()
{
    frgDeathStep = 0;
    frgDeathOverlayActive = 1;
    frgDeathOverlayMode = 0; // first toggle frame: blank
    frgStartNoteSeq( frgDeath1Notes, FRG_DEATH_COUNT );
    frgRenderFrame( FRG_MODE_PLAYING );
    frgState = FRG_STATE_DEATH_ANIM;
}

void frgApplyLifeLoss()
{
    frgLives--;
    frgFrogRightLimit++;
    frgStopAnimate = 0;
    frgFrogColumn = 8;
    frgFrogRow = 7;
}

// Upstream's own while(lives>=0) loop has no vsync wait at all -
// interimStep counts raw bare-AVR-loop iterations (thousands/sec on real
// hardware, gated only by two analogRead() calls' own real time cost),
// far faster than the 60Hz this port's update() actually runs at - the
// same "Frame counter increments every uncapped loop iteration" bug
// already found and fixed in Tiny Arkanoid (see that file's own header
// comment). Unlike Arkanoid, only interimStep itself needs decoupling
// here - the click debounce (frgClickBase/frgTickCounter) mirrors
// upstream's own millis()-based real wall-clock timer, which already
// advances at genuine real-time regardless of loop speed, so it's left
// alone. FRG_INTERIM_STEP_RATE is a best-effort estimate (analogRead()
// alone costs roughly 100-200us on real AVR hardware, so two consecutive
// calls plus the rest of the loop body likely land somewhere around
// 2000+ iterations/second - scaled here to land grid-shifts at a normal
// arcade pace) rather than a precisely measured value; adjust if the
// pace still feels off.
#define FRG_INTERIM_STEP_RATE 32

void frgPlayingTick()
{
    frgInterimStep += FRG_INTERIM_STEP_RATE;

    if( frgWatchDog >= 500 ) frgLives = -1;

    frgFrogLeftLimit = 1;
    if( (frgScore / 10) % 10 != 0 ) frgFrogLeftLimit++;
    if( (frgScore / 100) % 10 != 0 ) frgFrogLeftLimit++;
    if( (frgScore / 1000) % 10 != 0 ) frgFrogLeftLimit++;

    if( frgInterimStep > frgMoveDelay / 8 )
    {
        frgWatchDog++;
        frgBlockShiftL++;
        if( frgFlipFlopShift == 1 ) frgFlipFlopShift = 0; else frgFlipFlopShift = 1;
        if( frgFlipFlopShift == 1 ) frgBlockShiftR++;
        if( frgBlockShiftL == 7 )
        {
            frgMoveBlocks();
            frgBlockShiftL = 0;
        }
        if( frgBlockShiftR == 7 ) frgBlockShiftR = 0;
        frgInterimStep = 0;
        frgCheckCollision();
    }

    int upPressed = isUpPressed();
    int rightPressed = isRightPressed();
    int leftPressed = isLeftPressed();
    int firePressed = isFirePressed();

    if( ( upPressed || firePressed ) && frgClickLock == 0 )
    {
        frgMoveForward = 1;
        frgWatchDog = 0;
        frgClickLock = 1;
        frgClickBase = frgTickCounter;
    }
    if( rightPressed && frgClickLock == 0 )
    {
        frgMoveRight = 1;
        frgClickLock = 1;
        frgClickBase = frgTickCounter;
    }
    if( leftPressed && frgClickLock == 0 )
    {
        frgMoveLeft = 1;
        frgClickLock = 1;
        frgClickBase = frgTickCounter;
    }

    if( frgMoveLeft == 1 && frgTickCounter > frgClickBase + FRG_CLICK_HALF_DELAY )
    {
        frgWatchDog = 0;
        frgMoveLeft = 0;
        if( ( frgFrogRow == 7 && frgFrogColumn > frgFrogLeftLimit ) || ( frgFrogRow < 7 && frgFrogColumn > 0 ) )
            frgFrogColumn--;
        else if( frgFrogRow < 7 ) frgStopAnimate = 1;
        frgFrogMode = 2;
    }

    if( frgMoveRight == 1 && frgTickCounter > frgClickBase + FRG_CLICK_HALF_DELAY )
    {
        frgWatchDog = 0;
        frgMoveRight = 0;
        if( ( frgFrogRow == 7 && frgFrogColumn < frgFrogRightLimit ) || ( frgFrogRow < 7 && frgFrogColumn < 14 ) )
            frgFrogColumn++;
        else if( frgFrogRow < 7 ) frgStopAnimate = 1;
        frgFrogMode = 3;
    }

    if( frgMoveForward == 1 && frgTickCounter > frgClickBase + FRG_CLICK_HALF_DELAY )
    {
        frgMoveForward = 0;
        frgScore += frgLevel;

        if( frgFrogRow > 1 )
        {
            frgFrogRow--;
            if( frgFrogRow == 3 && frgBlockShiftL < 4 ) frgFrogColumn--;
            if( frgFrogRow == 2 && frgBlockShiftR + frgBlockShiftL < 5 ) frgFrogColumn++;
            if( frgFrogRow == 1 && frgBlockShiftR + frgBlockShiftL < 5 ) frgFrogColumn--;
        }
        else
        {
            if( frgBlockShiftL < 4 && frgFrogColumn < 15 ) frgFrogColumn++;
            int dockPos = frgFrogColumn / 3;
            if( frgFrogDocks[dockPos] == 0 )
            {
                frgDockedFrogs++;
                frgFrogDocks[dockPos] = 1;
                frgFrogRow = 7;
                frgFrogColumn = 8;
                frgStartNoteSeq( frgDockFillNotes, FRG_DOCKFILL_COUNT );
            }
            else frgStopAnimate = 1;
        }
        frgFrogMode = 1;

        if( frgDockedFrogs >= 5 )
        {
            frgLevel++;
            if( frgMoveDelay > 99 ) frgMoveDelay -= 100;
            frgBeginLevelUpFlash();
            return;
        }
    }

    if( frgWatchDog == 0 && frgStopAnimate == 0 )
    {
        frgWatchDog = 1;
        frgStartNoteSeq( frgJumpNotes, FRG_JUMP_COUNT );
    }

    frgCheckCollision();

    if( frgClickLock == 1 && frgTickCounter > frgClickBase + FRG_CLICK_DELAY && !leftPressed && !rightPressed && !upPressed && !firePressed )
        frgClickLock = 0;

    if( frgStopAnimate != 0 )
    {
        frgBeginDeathAnim();
        return;
    }

    frgRenderFrame( FRG_MODE_PLAYING );
}

// -----------------------------------------------------------------------------
//   Top-level
// -----------------------------------------------------------------------------

void gameFrogger_init()
{
    // Direct translation of upstream's own topScore = EEPROM.read(0)<<8 |
    // EEPROM.read(1) - eeprom_read_word() already combines the byte pair
    // the same hi-then-lo way. A never-written slot reads back as a real
    // 65535 (both bytes still their virgin 0xFF, matching real AVR EEPROM's
    // own erased state - see eepromShim.c) - upstream has no equivalent
    // guard, but leaving it unguarded here would make a first-ever session
    // unable to ever register a new high score (no real score reaches
    // 65535), a genuine regression rather than faithful behavior - treated
    // the same way Pipe Bird's own upstream already treats its own virgin-
    // byte sentinel (255) elsewhere in this project.
    frgTopScore = eeprom_read_word( 0 );
    if( frgTopScore == 65535 ) frgTopScore = 0;
    frgMute = 0;
    frgSeqActive = 0;
    frgTickCounter = 0;
    frgBeginAttract();
}

void gameFrogger_forceRedraw()
{
    if( frgState == FRG_STATE_ATTRACT ) frgRenderFrame( FRG_MODE_ATTRACT );
    else if( frgState == FRG_STATE_GAMEOVER_WAIT ) frgRenderFrame( FRG_MODE_GAMEOVER );
    else if( frgState == FRG_STATE_NEWHIGH_WAIT ) frgRenderFrame( FRG_MODE_NEWHIGH );
    else if( frgState == FRG_STATE_LEVELUP_TEXT ) frgRenderFrame( FRG_MODE_LEVELUP_TEXT );
    else if( frgState == FRG_STATE_PLAYING || frgState == FRG_STATE_DEATH_ANIM ) frgRenderFrame( FRG_MODE_PLAYING );
}

void gameFrogger_update()
{
    frgTickCounter++;
    frgAdvanceNoteSeq();

    if( frgState == FRG_STATE_ATTRACT )
    {
        int fireDown = isFirePressed();
        if( fireDown )
        {
            frgFireHoldTicks++;
            if( frgFireHoldTicks >= 120 && !frgMuteActionDone )
            {
                frgMuteActionDone = 1;
                // Direct translation of upstream's own boot-time dual
                // gesture: fire+left/right resets the persisted high
                // score, fire alone toggles mute.
                if( isLeftPressed() || isRightPressed() )
                {
                    frgTopScore = 0;
                    eeprom_write_word( 0, 0 );
                    frgShowResetMsg = true;
                }
                else if( frgMute == 0 ) frgMute = 1; else frgMute = 0;
            }
        }
        else
        {
            if( frgFireHeld && !frgMuteActionDone )
            {
                frgFireHeld = fireDown;
                frgBeginGame();
                frgRenderFrame( FRG_MODE_PLAYING );
                return;
            }
            if( frgFireHeld ) frgShowResetMsg = false;
            frgFireHoldTicks = 0;
            frgMuteActionDone = 0;
        }
        frgFireHeld = fireDown;
        frgRenderFrame( FRG_MODE_ATTRACT );
    }
    else if( frgState == FRG_STATE_PLAYING )
    {
        frgPlayingTick();
    }
    else if( frgState == FRG_STATE_DEATH_ANIM )
    {
        if( frgSeqActive )
            return;

        if( frgDeathStep == 0 )
        {
            frgDeathOverlayMode = frgFrogMode; // sprite
            frgRenderFrame( FRG_MODE_PLAYING );
            frgStartNoteSeq( frgDeath2Notes, FRG_DEATH_COUNT );
            frgDeathStep = 1;
        }
        else if( frgDeathStep == 1 )
        {
            frgDeathOverlayMode = 0; // blank
            frgRenderFrame( FRG_MODE_PLAYING );
            frgStartNoteSeq( frgDeath3Notes, FRG_DEATH_COUNT );
            frgDeathStep = 2;
        }
        else if( frgDeathStep == 2 )
        {
            frgDeathOverlayMode = frgFrogMode; // sprite
            frgRenderFrame( FRG_MODE_PLAYING );
            frgStartNoteSeq( frgDeath4Notes, FRG_DEATH_COUNT );
            frgDeathStep = 3;
        }
        else if( frgDeathStep == 3 )
        {
            frgWaitFrames = 36; // ~600ms hold, matching upstream's delay(600)
            frgDeathStep = 4;
        }
        else if( frgWaitFrames > 0 )
        {
            frgWaitFrames--;
        }
        else
        {
            frgDeathOverlayActive = 0;
            frgApplyLifeLoss();
            if( frgLives < 0 )
            {
                // Direct translation of upstream's own EEPROM.write(1,score&0xFF);
                // EEPROM.write(0,(score>>8)&0xFF) pair - eeprom_write_word()
                // already combines a byte pair the same hi-then-lo way.
                if( frgScore > frgTopScore ) { frgTopScore = frgScore; frgNewHigh = 1; eeprom_write_word( 0, frgTopScore ); } else frgNewHigh = 0;
                if( frgNewHigh ) frgBeginNewHighWait();
                else frgBeginGameOverWait();
            }
            else
            {
                frgState = FRG_STATE_PLAYING;
                frgRenderFrame( FRG_MODE_PLAYING );
            }
        }
    }
    else if( frgState == FRG_STATE_LEVELUP_FLASH )
    {
        if( frgSeqActive )
            return;

        if( frgLevelFlashStep >= 10 )
        {
            frgInitScreen();
            frgResetDock(0);
            frgDockedFrogs = 0;
            frgFrogColumn = 8;
            frgFrogRow = 7;
            frgBeginLevelUpText();
            return;
        }
        if( frgLevelFlashStep % 2 == 0 ) frgResetDock(0); else frgResetDock(1);
        frgRenderFrame( FRG_MODE_PLAYING );
        frgStartNoteSeq( frgLevelFlashNotes, FRG_LEVELFLASH_COUNT );
        frgLevelFlashStep++;
    }
    else if( frgState == FRG_STATE_LEVELUP_TEXT )
    {
        frgWaitFrames--;
        if( frgWaitFrames <= 0 )
        {
            frgState = FRG_STATE_PLAYING;
            frgRenderFrame( FRG_MODE_PLAYING );
        }
    }
    else if( frgState == FRG_STATE_GAMEOVER_WAIT )
    {
        frgWaitFrames--;
        if( frgWaitFrames <= 0 ) frgBeginAttract();
    }
    else if( frgState == FRG_STATE_NEWHIGH_WAIT )
    {
        frgWaitFrames--;
        if( frgWaitFrames <= 0 ) frgBeginAttract();
    }
}

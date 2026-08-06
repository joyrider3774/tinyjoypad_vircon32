// =============================================================================
// Stacker (Andy Jackson, 2015-2017, non-commercial-with-attribution; ATtiny-
// Joypad port by Billy Cheung, 2018) - a classic Stacker tower-building
// clone: a row of blocks sweeps left/right across the screen; press fire
// (or up/down) to lock it in place against the row below, then build
// upward - any columns that don't line up are trimmed away, and running out
// of columns (matchCount reaching 0) ends the game. Reach the top of the
// screen to level up (narrower starting width, faster sweep) and restart
// the tower from the bottom.
//
// From `more games/gametiny/UFO_Stacker_Attiny/` - upstream is a genuinely
// **combined cartridge** (its own boot-time prompt lets the player choose
// UFO or Stacker, sharing one font/sound/EEPROM-highscore/game-over-screen
// codebase between both). Matching this project's own established
// precedent for combined-file sources (Obono's `TinyJoypadWorks` monorepo
// became 3 separate menu entries), this is ported as its own standalone
// menu entry rather than replicating upstream's own in-cartridge sub-menu -
// UFO (the other half of this same file) is a separate future port, not
// done here.
//
// Structural notes:
// - Not `tinyJoypadShim`/`obonoCoreShim` lineage by name, but - matching
//   every other `gametiny/` port here - needed no new shim: the same A0/A3/
//   fire-pin thresholds as every other game here, so button reads go
//   straight onto `isFirePressed()`/`isUpPressed()`/`isDownPressed()` from
//   `tinyJoypadShim`. Upstream's own fire/up/down handling is a genuine
//   input-redundancy quirk worth preserving: fire, up, *and* down are all
//   accepted as the "lock the row" trigger (`analogRead(upDownPin)<950`
//   matches either up or down), each via its own real busy-wait-for-
//   release - ported as a single edge-detected "any of the three, just
//   pressed" check instead of three separate blocking waits, since a
//   frame-stepped engine has no equivalent to "block until released" and
//   a plain level check would let a held button re-trigger every tick.
// - This game's own `font6x8AJ.h` (re-extracted and byte-diff verified,
//   not assumed identical to any other game's same-named file - this one
//   turned out to remap 'f' to an 'h'-shaped glyph *and* 'h' to a
//   'y'-shaped glyph, a different substitution pair than either Wren's or
//   Bat Bonanza's own copy of the "same" file) - credit strings
//   (`"andh jackson"`, `"inspired bh"`, `"/ebboggles.com"`) are ported
//   verbatim rather than "corrected", the same h->y/w->[slash] lesson
//   already found in Frogger's z->@ bug and Bat Bonanza's own font.
// - **A genuine VRAM-persistence gap, found by reasoning about the render
//   model rather than counting bytes this time**: upstream only ever
//   redraws the *current* moving row (`drawRow(0, stackRow)`) and the row
//   that was *just* locked (`drawRow(1, stackRow)`, right before
//   `stackRow` decrements) - every earlier locked row (including the
//   bottom "foundation" row) is drawn exactly once and never touched
//   again, relying on the real SSD1306's VRAM to keep showing it
//   indefinitely. This project's always-`clear_screen()`-then-redraw model
//   can't replicate that, so a persistent `stkLockedRows[8][16]` array
//   tracks every screen row's own locked-cell pattern (not just the
//   transient upstream `row[1]`), refreshed only at the two real mutation
//   points (a successful lock, and a fresh level's reset) and redrawn in
//   full every frame alongside the currently-moving row.
// - The live score digits (`doNumber(0,7,score)`) and the row-7 tower
//   foundation share the same screen row upstream, by simple draw-order
//   coincidence (the digits are redrawn over the foundation every tick,
//   since `doNumber` runs unconditionally each loop iteration) - ported
//   with the same priority (score digits win over the locked-row pattern
//   at row 7, not OR-combined, since OR-ing bit patterns would produce a
//   corrupted hybrid rather than either genuine image).
// - EEPROM high-score persistence restored (see the project-wide "Real
//   persistent high-score saving" section in CLAUDE.md - a 2-byte big-
//   endian score at address 2/3, not 0/1, since this game shares one
//   combined cartridge's own EEPROM layout with UFO's own addr 0/1) -
//   the "hold fire ~2s to mute/unmute" gesture is kept (in-memory flag);
//   the combined-cartridge-specific "hold fire+up/
//   down at boot to reset both games' high scores" gesture doesn't apply
//   to a single standalone menu entry and is dropped outright.
// - The `runCounter` idle-kill (ends the game after 20 full sweep cycles
//   with the tower never locked, upstream's own anti-battery-drain
//   safeguard) is kept faithfully - harmless and still a reasonable
//   anti-AFK safety net even without a battery to protect.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data: font (extracted + byte-diff verified against this game's own
//   font6x8AJ.h - a different remap pair than Wren's/Bat Bonanza's own
//   copy of a same-named file)
// -----------------------------------------------------------------------------

int[360] stkFONT =
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
84,24,0,127,8,4,4,120,0,24,164,164,164,124,0,28,
160,160,160,124,0,0,68,125,64,0,0,64,128,132,125,0,
0,127,16,40,68,0,0,0,65,127,64,0,0,124,4,24,
4,120,0,124,8,4,4,120,0,56,68,68,68,56,0,252,
36,36,36,24,0,24,36,36,24,252,0,124,8,4,4,8,
0,72,84,84,84,32,0,4,63,68,64,32,0,60,64,64,
32,124,0,28,32,64,32,28,
};

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

int stkMute;

void stkBeepOnce( int bCount, int bDelay )
{
    if( stkMute ) return;
    int scaled = bDelay;
    if( scaled > 1000 ) scaled = 1000;
    if( scaled < 0 ) scaled = 0;
    scaled = scaled * 250 / 1000;
    int freq = 255 - scaled;
    if( freq < 1 ) freq = 1;
    if( freq > 250 ) freq = 250;
    int dur = bCount + 1;
    if( dur > 40 ) dur = 40;
    Sound( freq, dur );
}

int stkSeqActive;
int* stkSeqNotes;
int stkSeqCount;
int stkSeqIndex;
int stkSeqWaitFrames;

void stkStartNoteSeq( int* notes, int count )
{
    stkSeqNotes = notes;
    stkSeqCount = count;
    stkSeqIndex = 0;
    stkSeqActive = 1;
    stkSeqWaitFrames = 0;
}

void stkAdvanceNoteSeq()
{
    if( !stkSeqActive ) return;
    if( stkSeqWaitFrames > 0 ) { stkSeqWaitFrames--; return; }
    if( stkSeqIndex >= stkSeqCount ) { stkSeqActive = 0; return; }
    int freq = stkSeqNotes[ stkSeqIndex * 2 ];
    int dur = stkSeqNotes[ stkSeqIndex * 2 + 1 ];
    if( !stkMute ) Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    stkSeqWaitFrames = waitFrames;
    stkSeqIndex++;
}

// for(i=800;i>200;i-=200) beep(30,i) - played inside levelUp()'s own screen
int[6] stkLevelUpNotes1 =
{
55,31,105,31,155,31,
};
#define STK_LEVELUP1_COUNT 3

// for(i=700;i>100;i-=100) beep(30,i) - played right after levelUp() returns
int[12] stkLevelUpNotes2 =
{
80,31,105,31,130,31,155,31,180,31,205,31,
};
#define STK_LEVELUP2_COUNT 6

// for(i=0;i<1000;i+=50) beep(50,i) - game over
int[40] stkGameOverNotes =
{
250,40,243,40,230,40,218,40,205,40,193,40,180,40,168,40,155,40,143,40,
130,40,118,40,105,40,93,40,80,40,68,40,55,40,43,40,30,40,18,40,
};
#define STK_GAMEOVER_COUNT 20

// for(i=700;i>200;i-=50) beep(30,i) - new high score
int[20] stkNewHighNotes =
{
80,31,93,31,105,31,118,31,130,31,143,31,155,31,168,31,180,31,193,31,
};
#define STK_NEWHIGH_COUNT 10

// -----------------------------------------------------------------------------
//   Font / number rendering
// -----------------------------------------------------------------------------

int stkCharIndex( int ch )
{
    int c = ch - 32;
    if( c > 0 ) c = c - 12;
    if( c > 15 ) c = c - 6;
    if( c > 40 ) c = c - 9;
    return c;
}

int stkTextByte( int x, int y, int startX, int pageY, int* str )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= strlen( str ) ) return 0;
    int ch = str[charIdx];
    if( ch == 0 ) return 0;
    int within = rel - charIdx * 6;
    int fontIdx = stkCharIndex( ch );
    return stkFONT[ fontIdx * 6 + within ];
}

int stkCountDigits( int value )
{
    if( value <= 0 ) return 1;
    int v = value;
    int n = 0;
    while( v > 0 ) { n++; v = v / 10; }
    return n;
}

int stkDigitAt( int value, int posFromLeft, int totalDigits )
{
    int power = 1;
    int i;
    for( i = 0; i < totalDigits - 1 - posFromLeft; i++ ) power = power * 10;
    return ( value / power ) % 10;
}

int stkNumberByte( int x, int y, int startX, int pageY, int value )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int totalDigits = stkCountDigits( value );
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= totalDigits ) return 0;
    int within = rel - charIdx * 6;
    int digit = stkDigitAt( value, charIdx, totalDigits );
    int fontIdx = stkCharIndex( 48 + digit );
    return stkFONT[ fontIdx * 6 + within ];
}

// Unlike Bat Bonanza's WINSCORE-capped single-digit score, Stacker's score
// grows unbounded - the live row-7 HUD digit is redrawn on all 128 columns
// every real gameplay frame, so recomputing its digit count via
// stkCountDigits() on every one of those calls (as a direct stkNumberByte()
// call would) is real, avoidable repeated work for a value that's constant
// for the whole frame. This variant takes the digit count pre-computed
// once per frame instead (see stkRenderFrame()'s own stkScoreDigits).
int stkScoreByte( int x, int y, int pageY, int value, int totalDigits )
{
    if( y != pageY ) return 0;
    if( x < 0 ) return 0;
    int rel = x;
    int charIdx = rel / 6;
    if( charIdx >= totalDigits ) return 0;
    int within = rel - charIdx * 6;
    int digit = stkDigitAt( value, charIdx, totalDigits );
    int fontIdx = stkCharIndex( 48 + digit );
    return stkFONT[ fontIdx * 6 + within ];
}

// -----------------------------------------------------------------------------
//   Game state (direct translation of upstream's own globals - all plain
//   int/bool upstream too, no narrow-type wraparound-reliance concerns)
// -----------------------------------------------------------------------------

int stkScore;
int stkTop;
int stkNewHigh;
int stkLevel;
int stkBlockDirection; // 1=right, 0=left
int stkStackRow;
int stkMoveCounter;
int stkMatchCount;
int stkPerfect;
int stkRunCounter;

int[16] stkMovingRow; // upstream's row[0]
int[16] stkTargetRow; // upstream's row[1] (transient - only meaningful
                      // during the comparison at the moment of a lock)
int[8][16] stkLockedRows; // persistent per-screen-row locked pattern - see
                          // header comment on the VRAM-persistence gap

int stkFire;
int stkFireLock;
int stkFireCounterActive;
int stkFireCounter;
int stkTriggerHeld; // for edge-detecting the combined fire/up/down trigger

// -----------------------------------------------------------------------------
//   Render byte helpers
// -----------------------------------------------------------------------------

// Mirrors sendBlock(1)'s own fixed 8-byte "box" tile (a filled cell with a
// 1px border on all 4 sides) - sendBlock(0) is just all-zero, i.e. blank.
int stkBoxByte( int within )
{
    if( within == 0 || within == 7 ) return 0;
    return 0x7E;
}

int stkRowByte( int* rowArray, int x )
{
    int cell = x / 8;
    int within = x % 8;
    if( rowArray[cell] == 0 ) return 0;
    return stkBoxByte( within );
}

int stkLockedByte( int page, int x )
{
    int cell = x / 8;
    int within = x % 8;
    if( stkLockedRows[page][cell] == 0 ) return 0;
    return stkBoxByte( within );
}

// -----------------------------------------------------------------------------
//   Game logic
// -----------------------------------------------------------------------------

void stkResetBlocks()
{
    int j;
    for( j = 0; j < 16; j++ ) stkMovingRow[j] = 0;
    for( j = 0; j < 16; j++ ) stkTargetRow[j] = 0;
    int i;
    for( i = 6; i < 6 + stkMatchCount; i++ ) stkTargetRow[i] = 1;
    for( i = 0; i < stkMatchCount; i++ ) stkMovingRow[i] = 1;
}

void stkClearLockedRows()
{
    int r, c;
    for( r = 0; r < 8; r++ )
        for( c = 0; c < 16; c++ )
            stkLockedRows[r][c] = 0;
}

void stkCopyRowToLocked( int page, int* rowArray )
{
    int c;
    for( c = 0; c < 16; c++ ) stkLockedRows[page][c] = rowArray[c];
}

// -----------------------------------------------------------------------------
//   Top-level render dispatch
// -----------------------------------------------------------------------------

#define STK_MODE_ATTRACT   0
#define STK_MODE_PLAYING   1
#define STK_MODE_LEVELUP   2
#define STK_MODE_GAMEOVER  3
#define STK_MODE_NEWHIGH   4

// The attract screen's own small decorative ascending-staircase graphic,
// screen rows 2-7 - a direct port of upstream's own accumulating column
// ranges (each row's own range extends the previous, matching upstream's
// own never-reset row[] array across the 6 drawRow() calls in its splash).
int[6] stkAttractStairStart = { 14, 13, 13, 13, 12, 12 };
int[6] stkAttractStairEnd   = { 14, 14, 15, 15, 15, 15 }; // inclusive

void stkRenderFrame( int mode )
{
    int x, y, val;
    int scoreDigits = 0;
    if( mode == STK_MODE_PLAYING ) scoreDigits = stkCountDigits( stkScore );
    md_beginFrame();
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
        {
            val = 0;
            if( mode == STK_MODE_ATTRACT )
            {
                if( y >= 2 && y <= 7 )
                {
                    int col = x / 8;
                    int within = x % 8;
                    int rowIdx = y - 2;
                    if( col >= stkAttractStairStart[rowIdx] && col <= stkAttractStairEnd[rowIdx] )
                        val = val | stkBoxByte( within );
                }
                if( y == 2 ) val = val | stkTextByte( x, y, 0, 2, "S T A C K E R" );
                else if( y == 4 ) val = val | stkTextByte( x, y, 0, 4, "andh jackson" );
                else if( y == 6 ) val = val | stkTextByte( x, y, 0, 6, "inspired bh" );
                else if( y == 7 ) val = val | stkTextByte( x, y, 0, 7, "/ebboggles.com" );
                else if( y == 0 && stkMute ) val = val | stkTextByte( x, y, 32, 0, "-- MUTE --" );
            }
            else if( mode == STK_MODE_PLAYING )
            {
                if( y == stkStackRow ) val = val | stkRowByte( stkMovingRow, x );
                else val = val | stkLockedByte( y, x );

                if( y == 7 )
                {
                    int scoreByte = stkScoreByte( x, y, 7, stkScore, scoreDigits );
                    if( scoreByte != 0 ) val = scoreByte;
                }
            }
            else if( mode == STK_MODE_LEVELUP )
            {
                if( y == 1 ) val = val | stkTextByte( x, y, 16, 1, "--------------" );
                else if( y == 2 ) val = val | stkTextByte( x, y, 16, 2, " L E V E L " ) | stkNumberByte( x, y, 85, 2, stkLevel );
                else if( y == 3 ) val = val | stkTextByte( x, y, 16, 3, "--------------" );
                else if( y == 5 && stkPerfect ) val = val | stkTextByte( x, y, 25, 5, "PERFECT LEVEL" );
                else if( y == 7 && stkPerfect ) val = val | stkTextByte( x, y, 25, 7, "  100 BONUS" );
            }
            else if( mode == STK_MODE_GAMEOVER )
            {
                if( y == 1 ) val = val | stkTextByte( x, y, 11, 1, "----------------" );
                else if( y == 2 ) val = val | stkTextByte( x, y, 11, 2, "G A M E  O V E R" );
                else if( y == 3 ) val = val | stkTextByte( x, y, 11, 3, "----------------" );
                else if( y == 5 ) val = val | stkTextByte( x, y, 37, 5, "SCORE:" ) | stkNumberByte( x, y, 75, 5, stkScore );
                else if( y == 7 && !stkNewHigh ) val = val | stkTextByte( x, y, 21, 7, "HIGH SCORE:" ) | stkNumberByte( x, y, 88, 7, stkTop );
            }
            else if( mode == STK_MODE_NEWHIGH )
            {
                if( y == 1 ) val = val | stkTextByte( x, y, 10, 1, "----------------" );
                else if( y == 3 ) val = val | stkTextByte( x, y, 10, 3, " NEW HIGH SCORE " );
                else if( y == 7 ) val = val | stkTextByte( x, y, 10, 7, "----------------" );
                else if( y == 5 ) val = val | stkNumberByte( x, y, 50, 5, stkTop );
            }
            md_drawColumn( x, y, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define STK_STATE_ATTRACT   0
#define STK_STATE_PLAYING   1
#define STK_STATE_LEVELUP   2
#define STK_STATE_GAMEOVER_WAIT 3
#define STK_STATE_NEWHIGH_WAIT  4

int stkState;
int stkWaitFrames;
int stkFireHeld;
int stkFireHoldTicks;
int stkMuteActionDone;

void stkBeginAttract()
{
    stkFireHeld = 0;
    stkFireHoldTicks = 0;
    stkMuteActionDone = 0;
    stkState = STK_STATE_ATTRACT;
}

void stkBeginGameOverWait()
{
    stkRenderFrame( STK_MODE_GAMEOVER );
    stkStartNoteSeq( stkGameOverNotes, STK_GAMEOVER_COUNT );
    stkWaitFrames = 120;
    stkState = STK_STATE_GAMEOVER_WAIT;
}

void stkBeginNewHighWait()
{
    stkRenderFrame( STK_MODE_NEWHIGH );
    stkStartNoteSeq( stkNewHighNotes, STK_NEWHIGH_COUNT );
    stkWaitFrames = 162;
    stkState = STK_STATE_NEWHIGH_WAIT;
}

void stkEndGame()
{
    // Direct translation of upstream's own topScoreB, stored at EEPROM
    // addr 2/3 (not 0/1) since this game shares one combined cartridge's
    // EEPROM layout with UFO_Stacker_Attiny's own UFO half.
    if( stkScore > stkTop ) { stkTop = stkScore; stkNewHigh = 1; eeprom_write_word( 2, stkTop ); } else stkNewHigh = 0;
    stkBeginGameOverWait();
}

void stkBeginLevel( int firstLevel )
{
    if( firstLevel )
    {
        stkLevel = 1;
        stkMatchCount = 6;
    }
    stkStackRow = 6;
    stkBlockDirection = 1;
    stkPerfect = 1;
    stkMoveCounter = 0;
    stkFireLock = 0;
    stkFireCounterActive = 0;
    stkFireCounter = 0;
    stkFire = 0;
    stkTriggerHeld = 0;
    stkResetBlocks();
    stkClearLockedRows();
    stkCopyRowToLocked( 7, stkTargetRow );
}

void stkBeginGame()
{
    stkScore = 0;
    stkRunCounter = 0;
    stkBeginLevel( 1 );
    stkState = STK_STATE_PLAYING;
}

void stkBeginLevelUp()
{
    if( stkLevel < 6 ) stkMatchCount = 6;
    else if( stkLevel < 9 ) stkMatchCount = 5;
    else if( stkLevel < 12 ) stkMatchCount = 4;
    else if( stkLevel < 15 ) stkMatchCount = 3;
    else stkMatchCount = 2;
    stkLevel++;

    stkRenderFrame( STK_MODE_LEVELUP );
    stkStartNoteSeq( stkLevelUpNotes1, STK_LEVELUP1_COUNT );
    stkWaitFrames = 90;
    stkState = STK_STATE_LEVELUP;
}

void stkPlayingTick()
{
    stkMoveCounter++;

    if( stkFireCounterActive ) stkFireCounter++;
    if( stkFireCounter >= 10 && stkFireCounterActive )
    {
        stkFireCounterActive = 0;
        stkFireLock = 0;
        stkFire = 0;
    }

    int triggerNow = isFirePressed() || isUpPressed() || isDownPressed();
    if( triggerNow && !stkTriggerHeld )
    {
        stkFire = 1;
    }
    stkTriggerHeld = triggerNow;

    if( stkFireLock == 0 && stkFire == 1 )
    {
        int matchTarget = stkMatchCount;
        stkRunCounter = 0;
        stkMatchCount = 0;
        int i;
        for( i = 0; i < 16; i++ )
        {
            if( stkTargetRow[i] == 1 )
            {
                if( stkMovingRow[i] == 1 ) { stkMatchCount++; stkTargetRow[i] = 1; }
                else stkTargetRow[i] = 0;
            }
        }
        if( stkMatchCount < matchTarget ) stkPerfect = 0;

        stkCopyRowToLocked( stkStackRow, stkTargetRow );
        stkStackRow--;

        if( stkMatchCount > 0 )
        {
            stkScore += ( 7 - stkStackRow );
            stkBeepOnce( 50, 300 );
        }
        stkFireLock = 1;
        stkFire = 0;
        stkFireCounter = 0;
        stkFireCounterActive = 1;

        if( stkStackRow >= 0 )
        {
            for( i = 0; i < stkMatchCount; i++ ) stkMovingRow[i] = 1;
            for( i = stkMatchCount; i < 16; i++ ) stkMovingRow[i] = 0;
        }
    }

    int speedNow = 13 - stkLevel;
    if( speedNow < 4 ) speedNow = 4;
    if( stkMoveCounter >= speedNow )
    {
        stkMoveCounter = 0;
        if( stkBlockDirection )
        {
            if( stkMovingRow[15] == 1 )
            {
                stkRunCounter++;
                if( stkRunCounter > 20 ) { stkEndGame(); return; }
                stkBlockDirection = 0;
            }
            else
            {
                int i;
                for( i = 15; i > 0; i-- ) stkMovingRow[i] = stkMovingRow[i-1];
                stkMovingRow[0] = 0;
            }
        }
        else
        {
            if( stkMovingRow[0] == 1 ) stkBlockDirection = 1;
            else
            {
                int i;
                for( i = 0; i < 15; i++ ) stkMovingRow[i] = stkMovingRow[i+1];
                stkMovingRow[15] = 0;
            }
        }
    }

    if( stkMatchCount == 0 ) { stkEndGame(); return; }

    if( stkStackRow < 0 ) { stkBeginLevelUp(); return; }

    stkRenderFrame( STK_MODE_PLAYING );
}

void gameStacker_init()
{
    stkMute = 0;
    // Direct translation of upstream's own topScoreB = EEPROM.read(2)<<8 |
    // EEPROM.read(3), guarded against a never-written slot's own virgin
    // 65535 read the same way as every other game in this pass.
    stkTop = eeprom_read_word( 2 );
    if( stkTop == 65535 ) stkTop = 0;
    stkSeqActive = 0;
    stkBeginAttract();
}

void gameStacker_forceRedraw()
{
    if( stkState == STK_STATE_ATTRACT ) stkRenderFrame( STK_MODE_ATTRACT );
    else if( stkState == STK_STATE_PLAYING ) stkRenderFrame( STK_MODE_PLAYING );
    else if( stkState == STK_STATE_LEVELUP ) stkRenderFrame( STK_MODE_LEVELUP );
    else if( stkState == STK_STATE_GAMEOVER_WAIT ) stkRenderFrame( STK_MODE_GAMEOVER );
    else stkRenderFrame( STK_MODE_NEWHIGH );
}

void gameStacker_update()
{
    stkAdvanceNoteSeq();

    if( stkState == STK_STATE_ATTRACT )
    {
        int fireDown = isFirePressed();
        if( fireDown )
        {
            stkFireHoldTicks++;
            if( stkFireHoldTicks >= 120 && !stkMuteActionDone )
            {
                stkMuteActionDone = 1;
                if( stkMute == 0 ) stkMute = 1; else stkMute = 0;
            }
        }
        else
        {
            if( stkFireHeld && !stkMuteActionDone )
            {
                stkFireHeld = fireDown;
                stkBeginGame();
                stkRenderFrame( STK_MODE_PLAYING );
                return;
            }
            stkFireHoldTicks = 0;
            stkMuteActionDone = 0;
        }
        stkFireHeld = fireDown;
        stkRenderFrame( STK_MODE_ATTRACT );
    }
    else if( stkState == STK_STATE_PLAYING )
    {
        stkPlayingTick();
    }
    else if( stkState == STK_STATE_LEVELUP )
    {
        if( stkSeqActive ) return;
        if( stkWaitFrames > 0 ) { stkWaitFrames--; return; }

        stkBeginLevel( 0 );
        stkRenderFrame( STK_MODE_PLAYING );
        stkStartNoteSeq( stkLevelUpNotes2, STK_LEVELUP2_COUNT );
        stkState = STK_STATE_PLAYING;
    }
    else if( stkState == STK_STATE_GAMEOVER_WAIT )
    {
        if( stkWaitFrames > 0 ) { stkWaitFrames--; return; }
        if( stkNewHigh ) stkBeginNewHighWait();
        else stkBeginAttract();
    }
    else // STK_STATE_NEWHIGH_WAIT
    {
        if( stkWaitFrames > 0 ) { stkWaitFrames--; return; }
        stkBeginAttract();
    }
}

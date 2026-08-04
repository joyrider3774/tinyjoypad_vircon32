// =============================================================================
// Pong (Andy Jackson, 2015-2017, non-commercial-with-attribution; ATtiny-
// Joypad port by Billy Cheung, 2018) - a classic Pong clone: a 1-pixel-wide
// bat on each edge of the screen, single-button/dual-button/2-player
// control modes, 4 difficulty levels, first to 7 points wins the match.
// From `more games/gametiny/BatBonanzaAttinyArcade/` (a badly-named folder -
// its own header credits this as "Pong game by Andy Jackson", nothing to do
// with bats or bonanzas).
//
// Structural notes:
// - Not `tinyJoypadShim`/`obonoCoreShim` lineage by name, but - matching
//   every other `gametiny/` port here (Lander/Wren/Frogger) - needed no new
//   shim: Billy Cheung's own header documents the identical A0/A3 500-750/
//   750-950 analog thresholds and digital-pin-1 fire button, so button
//   reads go straight onto `isLeftPressed()`/`isRightPressed()`/
//   `isUpPressed()`/`isDownPressed()`/`isFirePressed()` from
//   `tinyJoypadShim`.
// - This game's own font (`font6x8AJ.h`, re-extracted and byte-diff
//   verified rather than assumed identical to Wren's copy of a same-named
//   file) has a *different* character set than either Wren's or Frogger's
//   own truncated fonts - full A-Z this time, with lowercase 'h' remapped
//   to a 'y'-shaped glyph (`// y (in place of h)`) - the credit string
//   `"bh andh jackson"` deliberately exploits this (renders as "by andy
//   jackson") and is ported verbatim, not "corrected", matching the same
//   z->@ lesson from Frogger's own font substitution bug.
// - Upstream's main loop delay is **genuinely variable**, not a fixed FPS -
//   `factor` (2-30, computed from difficulty and the live score gap) feeds
//   a real `delay(factor)` in milliseconds, directly controlling how fast
//   the simulation runs (faster when a side is behind, to help it catch up
//   / make it harder, depending on difficulty). Rather than a fixed
//   `TICK_DIVISOR`, this needed a small variable-rate accumulator
//   (`pongAccumMs`, added every real frame, ticking the game forward once
//   it reaches the current `pongFactor`) - capped to at most one logic
//   tick per real 60fps frame (never bursts multiple ticks in one frame to
//   catch up). This is a **deliberate, documented simplification**: at the
//   most extreme setting (expert difficulty, big lead) `factor` can drop
//   below 16.67ms, meaning upstream's *intended* rate there slightly
//   exceeds Vircon32's native 60fps - capping at 60fps instead of adding a
//   multi-tick-per-frame catch-up loop accepts a minor speed cap only in
//   that one extreme corner, in exchange for much simpler, lower-risk
//   accumulator logic (every other combination of difficulty/score-gap
//   lands comfortably under 60fps, where the accumulator behaves exactly
//   as intended).
// - Round-scoring win-check moved earlier than upstream's own control
//   flow: upstream's own scoring branch only skips the round-flash-and-
//   restart when the *just-updated* score has already reached WINSCORE
//   (`if (score2 < WINSCORE) { ...blink...; startGame(); }`), but doesn't
//   actually end the *match* until a later, separate check at the bottom
//   of the main loop - reached only on a subsequent non-scoring tick,
//   since the scoring branch itself unconditionally `break`s out before
//   ever reaching that check. The practical effect is identical either
//   way (skip the round-flash on the match-winning point, show the win
//   screen) - ported as a single immediate check right at the scoring
//   point instead of upstream's own delayed/roundabout version, since the
//   *outcome* upstream clearly intends is the immediate one, and this
//   port's explicit state machine has no equivalent "keep falling through
//   to a later check" shape to replicate faithfully anyway.
// - EEPROM-persisted settings (mute/difficulty/gameMode) dropped, session-
//   only in memory - matching every other port's own precedent. The
//   "hold fire at boot to reset system settings" gesture is dropped
//   outright (no persisted settings for it to meaningfully reset); the
//   "hold up/down at boot to cycle control mode" gesture is kept, since
//   it's a genuinely useful in-session feature.
// - `random(0,5)` (the computer AI's perturbation walk) ported onto the
//   shared `arand(5)` helper, matching every other port's own precedent.
// - Every shift relying on AVR's implicit `uint8_t` narrowing
//   (`(0xFF<<player%8)`, `(0x7E>>(8-player%8))` in the paddle-drawing
//   byte math) got an explicit `&0xFF` mask - the same byte-truncation-
//   reliant-trick class of bug already found and fixed project-wide
//   (Tiny Pipe's `FADE_TPIPE()`, Tiny Arena's `VSlide`, etc).
// =============================================================================

// -----------------------------------------------------------------------------
//   Data: font (extracted + byte-diff verified against this game's own
//   font6x8AJ.h - NOT assumed identical to Wren's same-named file)
// -----------------------------------------------------------------------------

int[378] pongFONT =
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
64,56,64,63,0,99,20,8,20,99,0,7,8,112,8,7,
0,97,81,73,69,67,0,32,84,84,84,120,0,127,72,68,
68,56,0,56,68,68,68,32,0,56,68,68,72,127,0,56,
84,84,84,24,0,8,126,9,1,2,0,24,164,164,164,124,
0,28,160,160,160,124,0,0,68,125,64,0,0,64,128,132,
125,0,0,127,16,40,68,0,0,0,65,127,64,0,0,124,
4,24,4,120,0,124,8,4,4,120,0,56,68,68,68,56,
0,252,36,36,36,24,0,24,36,36,24,252,0,124,8,4,
4,8,0,72,84,84,84,32,0,4,63,68,64,32,0,60,
64,64,32,124,0,28,32,64,32,28,
};

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

int pongMute;

void pongBeepOnce( int bCount, int bDelay )
{
    if( pongMute ) return;
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

int pongSeqActive;
int* pongSeqNotes;
int pongSeqCount;
int pongSeqIndex;
int pongSeqWaitFrames;

void pongStartNoteSeq( int* notes, int count )
{
    pongSeqNotes = notes;
    pongSeqCount = count;
    pongSeqIndex = 0;
    pongSeqActive = 1;
    pongSeqWaitFrames = 0;
}

void pongAdvanceNoteSeq()
{
    if( !pongSeqActive ) return;
    if( pongSeqWaitFrames > 0 ) { pongSeqWaitFrames--; return; }
    if( pongSeqIndex >= pongSeqCount ) { pongSeqActive = 0; return; }
    int freq = pongSeqNotes[ pongSeqIndex * 2 ];
    int dur = pongSeqNotes[ pongSeqIndex * 2 + 1 ];
    if( !pongMute ) Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    pongSeqWaitFrames = waitFrames;
    pongSeqIndex++;
}

// for(i=0;i<1000;i+=100) beep(50,i) - played when a round is lost/won
int[20] pongRoundNotes =
{
250,40,230,40,205,40,180,40,155,40,130,40,105,40,80,40,55,40,30,40,
};
#define PONG_ROUND_COUNT 10

// for(i=800;i>200;i-=200) beep(30,i) - played at the start of each round
int[6] pongStartNotes =
{
55,31,105,31,155,31,
};
#define PONG_START_COUNT 3

// for(i=0;i<1000;i+=50) beep(50,i) - played on match win
int[40] pongWinNotes =
{
250,40,243,40,230,40,218,40,205,40,193,40,180,40,168,40,155,40,143,40,
130,40,118,40,105,40,93,40,80,40,68,40,55,40,43,40,30,40,18,40,
};
#define PONG_WIN_COUNT 20

// -----------------------------------------------------------------------------
//   Font / number rendering
// -----------------------------------------------------------------------------

int pongCharIndex( int ch )
{
    int c = ch - 32;
    if( c > 0 ) c = c - 12;
    if( c > 15 ) c = c - 6;
    if( c > 40 ) c = c - 6;
    return c;
}

int pongTextByte( int x, int y, int startX, int pageY, int* str )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= strlen( str ) ) return 0;
    int ch = str[charIdx];
    if( ch == 0 ) return 0;
    int within = rel - charIdx * 6;
    int fontIdx = pongCharIndex( ch );
    return pongFONT[ fontIdx * 6 + within ];
}

// Same as pongTextByte(), but takes the string's own length pre-computed
// instead of calling strlen() again on every one of the 128 columns in
// its row - the attract screen's own strings are both longer (~20 chars)
// and span more simultaneous rows (6) than any other game's attract text
// here, so this redundant per-pixel strlen() was a real, avoidable cost
// (the same "redundant per-pixel recompute of a frame-constant value"
// class already fixed elsewhere, e.g. Frogger's frgScoreDigits).
int pongTextByteLen( int x, int y, int startX, int pageY, int* str, int strLen )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= strLen ) return 0;
    int ch = str[charIdx];
    if( ch == 0 ) return 0;
    int within = rel - charIdx * 6;
    int fontIdx = pongCharIndex( ch );
    return pongFONT[ fontIdx * 6 + within ];
}

int pongCountDigits( int value )
{
    if( value <= 0 ) return 1;
    int v = value;
    int n = 0;
    while( v > 0 ) { n++; v = v / 10; }
    return n;
}

int pongDigitAt( int value, int posFromLeft, int totalDigits )
{
    int power = 1;
    int i;
    for( i = 0; i < totalDigits - 1 - posFromLeft; i++ ) power = power * 10;
    return ( value / power ) % 10;
}

int pongNumberByte( int x, int y, int startX, int pageY, int value )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int totalDigits = pongCountDigits( value );
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= totalDigits ) return 0;
    int within = rel - charIdx * 6;
    int digit = pongDigitAt( value, charIdx, totalDigits );
    int fontIdx = pongCharIndex( 48 + digit );
    return pongFONT[ fontIdx * 6 + within ];
}

// -----------------------------------------------------------------------------
//   Game state (direct translation of upstream's own globals - all plain
//   int upstream too, no narrow-type wraparound-reliance concerns)
// -----------------------------------------------------------------------------

#define PONG_WINSCORE 7

int pongPlayer;
int pongPlayer2;
int pongPlatformHeight; // upstream calls this "platformWidth" despite it
                        // being a vertical extent (paddles are vertical
                        // bats) - kept the same value, renamed for clarity
int pongScore;
int pongScore2;

int pongBallX;  // fixed-point *8
int pongBallY;  // fixed-point *4
int pongVDir;
int pongHDir;

int pongDifficulty; // 1-4
int pongGameMode;   // 0=one-button, 1=dual-button, 2=two-player
int pongPerturbation;
int pongPFactor;
int pongWaitCount;
int pongFactor;

float pongAccumMs;

// -----------------------------------------------------------------------------
//   Paddle / ball byte helpers
// -----------------------------------------------------------------------------

int pongPaddleByte( int page, int pos )
{
    int topPage = pos / 8;
    int off = pos % 8;
    if( off != 0 )
    {
        if( page == topPage ) return (0xFF << off) & 0xFF;
        if( page == topPage + 1 ) return 0xFF;
        if( page == topPage + 2 ) return (0x7E >> (8 - off)) & 0xFF;
        return 0;
    }
    if( page == topPage ) return 0xFF;
    if( page == topPage + 1 ) return 0xFF;
    return 0;
}

int pongDoDrawLS( int p1, int p2 ) { return ((0x03 | p1) << p2) & 0xFF; }
int pongDoDrawRS( int p1, int p2 ) { return ((0x03 | p1) >> p2) & 0xFF; }

int pongBallByte( int page, int ballPixelY )
{
    int topPage = ballPixelY / 8;
    int off = ballPixelY % 8;
    if( off != 0 )
    {
        if( page == topPage ) return pongDoDrawLS( 0, off );
        if( page == topPage + 1 ) return pongDoDrawRS( 0, 8 - off );
        return 0;
    }
    if( page == topPage ) return pongDoDrawLS( 0, 0 );
    return 0;
}

// -----------------------------------------------------------------------------
//   Top-level render dispatch
// -----------------------------------------------------------------------------

#define PONG_MODE_ATTRACT      0
#define PONG_MODE_COUNTDOWN    1
#define PONG_MODE_PLAYING      2
#define PONG_MODE_ROUND_FLASH  3
#define PONG_MODE_MATCH_WIN    4

int pongCountdownNumber;      // 3,2,1 shown during COUNTDOWN
int pongFlashSide;            // 1 or 2 - which score to blink during ROUND_FLASH
int pongFlashVisible;         // whether the blinking number is currently shown
int pongWinVisible;           // whether both scores are currently shown (MATCH_WIN blink)

void pongRenderFrame( int mode )
{
    int x, y, val;
    // Precomputed once per frame, not per pixel - pongBallX/pongBallY are
    // constant for the whole render pass, so recomputing their division
    // at every one of 1024 pixels (as an earlier draft did, inline in the
    // PLAYING branch below) was pure repeated work for an unchanging
    // result, the same class of waste Frogger's own frgScoreDigits fix
    // addressed.
    int ballPixelX = 0, ballPixelY = 0;
    if( mode == PONG_MODE_PLAYING )
    {
        ballPixelX = pongBallX / 8;
        ballPixelY = pongBallY / 4;
    }
    md_beginFrame();
    for( y = 0; y < 8; y++ )
    {
        // Attract-screen row string + its length selected once per row,
        // not re-selected (and re-strlen()'d) on every one of the 128
        // columns in that row - see pongTextByteLen()'s own header note.
        int* attractStr = "";
        int attractStrLen = 0;
        int attractStartX = 0;
        if( mode == PONG_MODE_ATTRACT )
        {
            if( y == 1 ) { attractStr = "   ---------------  "; attractStartX = 0; }
            else if( y == 2 ) { attractStr = "        B A T       "; attractStartX = 0; }
            else if( y == 4 ) { attractStr = "    B O N A N Z A   "; attractStartX = 0; }
            else if( y == 5 ) { attractStr = "   ---------------  "; attractStartX = 0; }
            else if( y == 7 ) { attractStr = "   bh andh jackson  "; attractStartX = 0; }
            else if( y == 0 )
            {
                attractStartX = 16;
                if( pongGameMode == 0 ) attractStr = "- ONE BUTTON  -";
                else if( pongGameMode == 1 ) attractStr = "- DUAL BUTTON -";
                else attractStr = "- 2 PLAYERS   -";
            }
            attractStrLen = strlen( attractStr );
        }

        for( x = 0; x < 128; x++ )
        {
            val = 0;
            if( mode == PONG_MODE_ATTRACT )
            {
                val = val | pongTextByteLen( x, y, attractStartX, y, attractStr, attractStrLen );
            }
            else if( mode == PONG_MODE_COUNTDOWN )
            {
                if( y == 3 ) val = val | pongTextByte( x, y, 16, 3, "-- GET READY --" );
                else if( y == 5 ) val = val | pongNumberByte( x, y, 60, 5, pongCountdownNumber );
            }
            else if( mode == PONG_MODE_PLAYING )
            {
                if( x == 0 ) val = val | pongPaddleByte( y, pongPlayer );
                else if( x == 127 ) val = val | pongPaddleByte( y, pongPlayer2 );
                if( x == ballPixelX || x == ballPixelX + 1 ) val = val | pongBallByte( y, ballPixelY );
                if( y == 0 ) val = val | pongNumberByte( x, y, 28, 0, pongScore ) | pongNumberByte( x, y, 92, 0, pongScore2 );
            }
            else if( mode == PONG_MODE_ROUND_FLASH )
            {
                if( y == 4 )
                {
                    if( pongFlashSide == 1 )
                    {
                        if( pongFlashVisible ) val = val | pongNumberByte( x, y, 46, 4, pongScore );
                        val = val | pongNumberByte( x, y, 78, 4, pongScore2 );
                    }
                    else
                    {
                        val = val | pongNumberByte( x, y, 46, 4, pongScore );
                        if( pongFlashVisible ) val = val | pongNumberByte( x, y, 78, 4, pongScore2 );
                    }
                }
            }
            else if( mode == PONG_MODE_MATCH_WIN )
            {
                if( y == 3 )
                {
                    if( pongScore > pongScore2 ) val = val | pongTextByte( x, y, 27, 3, "P L A Y E R 1" );
                    else val = val | pongTextByte( x, y, 27, 3, "P L A Y E R 2" );
                }
                else if( y == 5 ) val = val | pongTextByte( x, y, 27, 5, "   W I N S   " );
                else if( y == 0 && pongWinVisible )
                    val = val | pongNumberByte( x, y, 28, 0, pongScore ) | pongNumberByte( x, y, 92, 0, pongScore2 );
            }
            md_drawColumn( x, y, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine constants/vars (declared early - referenced by the
//   pongBegin*() helpers below, which the game-logic functions call)
// -----------------------------------------------------------------------------

#define PONG_STATE_ATTRACT     0
#define PONG_STATE_COUNTDOWN   1
#define PONG_STATE_PLAYING     2
#define PONG_STATE_ROUND_FLASH 3
#define PONG_STATE_MATCH_WIN   4

int pongState;
int pongWaitFrames;
int pongFlashStep;

int pongModeHoldTicks;
int pongModeHeld;
int pongDiffHoldTicks;
int pongDiffHeld;
int pongDiffLongDone;

// -----------------------------------------------------------------------------
//   Game logic
// -----------------------------------------------------------------------------

void pongComputeFactor()
{
    int diff = pongScore - pongScore2;
    if( pongDifficulty == 3 || pongDifficulty == 4 )
    {
        pongFactor = 10 - diff / 2;
        if( pongFactor < 2 ) pongFactor = 2;
    }
    else if( pongDifficulty == 2 )
    {
        pongFactor = 20 - diff / 2;
        if( pongFactor < 10 ) pongFactor = 10;
    }
    else
    {
        pongFactor = 30 - diff / 2;
        if( pongFactor < 15 ) pongFactor = 15;
    }
}

void pongBeginCountdown()
{
    pongCountdownNumber = 3;
    pongWaitFrames = 60;
    pongState = PONG_STATE_COUNTDOWN;
}

void pongBeginRoundFlash( int side )
{
    pongFlashSide = side;
    pongFlashVisible = 0;
    pongFlashStep = 0;
    pongWaitFrames = 21;
    pongStartNoteSeq( pongRoundNotes, PONG_ROUND_COUNT );
    pongState = PONG_STATE_ROUND_FLASH;
}

void pongBeginMatchWin()
{
    pongWinVisible = 1;
    pongFlashStep = 0;
    pongWaitFrames = 21;
    pongStartNoteSeq( pongWinNotes, PONG_WIN_COUNT );
    pongState = PONG_STATE_MATCH_WIN;
}

void pongBeginGame()
{
    pongBallX = 64 * 8;
    pongBallY = 32 * 4;
    pongHDir = -8;
    pongVDir = -4;
    pongPlayer = 64;
    pongPlayer2 = 64;
    pongScore = 0;
    pongScore2 = 0;
    pongPerturbation = 0;
    pongWaitCount = 0;
    pongAccumMs = 0.0;
    pongComputeFactor();
    pongStartNoteSeq( pongStartNotes, PONG_START_COUNT );
    pongBeginCountdown();
}

void pongRunOneTick()
{
    pongWaitCount++;

    // LEFT/RIGHT double as UP/DOWN for the player's own bat, by direct
    // request - added on top of the existing LEFT/RIGHT difficulty-cycle/
    // mute-toggle gesture (below) rather than replacing it, per the
    // user's own explicit choice, so releasing LEFT/RIGHT after moving
    // the bat will still also fire that gesture (a deliberate, accepted
    // tradeoff, not an oversight).
    int upDown = 0; // 0=none, 1=up, 2=down
    if( isUpPressed() || isLeftPressed() ) upDown = 1;
    else if( isDownPressed() || isRightPressed() ) upDown = 2;

    // Player 1's own bat always responds directly to up/down (and their
    // left/right aliases above), regardless of gameMode - by direct
    // request, since upstream's own one-button-mode (fire-only) and
    // 2-player-mode (down-only) schemes for player 1 didn't match the
    // expected "up moves it up, down moves it down" behavior. gameMode
    // is now only consulted below for player 2's own control (AI vs. a
    // second human on fire).
    if( upDown == 1 ) pongPlayer -= 2;
    else if( upDown == 2 ) pongPlayer += 2;
    if( pongPlayer > 48 ) pongPlayer = 48;
    if( pongPlayer < 0 ) pongPlayer = 0;

    if( pongGameMode == 2 )
    {
        if( isFirePressed() ) pongPlayer2 -= 2;
        else pongPlayer2 += 1;
    }
    else
    {
        if( pongWaitCount >= 3 )
        {
            pongWaitCount = 0;
            pongPerturbation = pongPerturbation - 2 + arand(5);
            if( pongPerturbation > pongPFactor ) pongPerturbation = pongPFactor - 2;
            if( pongPerturbation < pongPFactor * -1 ) pongPerturbation = ( pongPFactor * -1 ) + 2;
        }
        pongPlayer2 = ( pongBallY / 4 - 8 ) + pongPerturbation;
    }
    if( pongPlayer2 > 48 ) pongPlayer2 = 48;
    if( pongPlayer2 < 0 ) pongPlayer2 = 0;

    int actualy = pongBallY / 4;

    if( ( actualy + pongVDir < 63 && pongVDir > 0 ) || ( actualy - pongVDir > 6 && pongVDir < 0 ) )
        pongBallY += pongVDir;
    else
        pongVDir = pongVDir * -1;
    pongBallX += pongHDir;

    actualy = pongBallY / 4;
    int actualx = pongBallX / 8;

    if( actualx <= 4 )
    {
        if( actualy < pongPlayer - 1 || actualy > pongPlayer + pongPlatformHeight + 1 )
        {
            pongScore2++;
            pongBallX = 5 * 8;
            pongBallY = pongPlayer * 4;
            pongHDir = 13;
            if( pongVDir > 0 ) pongVDir = 2; else pongVDir = -2;
            pongPerturbation = 0;
            if( pongScore2 >= PONG_WINSCORE ) { pongBeginMatchWin(); return; }
            pongBeginRoundFlash( 2 );
            return;
        }
        else if( actualy < pongPlayer + 1 ) { pongVDir = -6; pongHDir = 7; }
        else if( actualy < pongPlayer + 4 ) { pongVDir = -4; pongHDir = 10; }
        else if( actualy < pongPlayer + 7 ) { pongVDir = -2; pongHDir = 13; }
        else if( actualy < pongPlayer + 9 ) { pongVDir = 0; pongHDir = 14; }
        else if( actualy < pongPlayer + 12 ) { pongVDir = 2; pongHDir = 13; }
        else if( actualy < pongPlayer + 15 ) { pongVDir = 4; pongHDir = 10; }
        else { pongVDir = 6; pongHDir = 7; }
        pongBeepOnce( 20, 600 );
    }

    if( actualx >= 122 )
    {
        if( actualy < pongPlayer2 - 1 || actualy > pongPlayer2 + pongPlatformHeight + 1 )
        {
            pongScore++;
            pongBallX = 120 * 8;
            pongBallY = pongPlayer2 * 4;
            pongHDir = -13;
            if( pongVDir > 0 ) pongVDir = 2; else pongVDir = -2;
            if( pongScore >= PONG_WINSCORE ) { pongBeginMatchWin(); return; }
            pongBeginRoundFlash( 1 );
            return;
        }
        else if( actualy < pongPlayer2 + 1 ) { pongVDir = -6; pongHDir = -7; }
        else if( actualy < pongPlayer2 + 4 ) { pongVDir = -4; pongHDir = -10; }
        else if( actualy < pongPlayer2 + 7 ) { pongVDir = -2; pongHDir = -13; }
        else if( actualy < pongPlayer2 + 9 ) { pongVDir = 0; pongHDir = -14; }
        else if( actualy < pongPlayer2 + 12 ) { pongVDir = 2; pongHDir = -13; }
        else if( actualy < pongPlayer2 + 15 ) { pongVDir = 4; pongHDir = -10; }
        else { pongVDir = 6; pongHDir = -7; }
        pongBeepOnce( 20, 300 );
    }

    pongComputeFactor();
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

void pongBeginAttract()
{
    pongModeHoldTicks = 0;
    pongModeHeld = 0;
    pongState = PONG_STATE_ATTRACT;
}

void gamePong_init()
{
    pongMute = 0;
    pongDifficulty = 1;
    pongPFactor = 12;
    pongGameMode = 0;
    pongPlatformHeight = 16; // upstream: int platformWidth = 16;
    pongSeqActive = 0;
    pongBeginAttract();
}

void gamePong_forceRedraw()
{
    if( pongState == PONG_STATE_ATTRACT ) pongRenderFrame( PONG_MODE_ATTRACT );
    else if( pongState == PONG_STATE_COUNTDOWN ) pongRenderFrame( PONG_MODE_COUNTDOWN );
    else if( pongState == PONG_STATE_PLAYING ) pongRenderFrame( PONG_MODE_PLAYING );
    else if( pongState == PONG_STATE_ROUND_FLASH ) pongRenderFrame( PONG_MODE_ROUND_FLASH );
    else pongRenderFrame( PONG_MODE_MATCH_WIN );
}

void gamePong_update()
{
    pongAdvanceNoteSeq();

    if( pongState == PONG_STATE_ATTRACT )
    {
        int held = isUpPressed() || isDownPressed();
        if( held )
        {
            pongModeHoldTicks++;
            if( pongModeHoldTicks >= 120 && !pongModeHeld )
            {
                pongModeHeld = 1;
                pongGameMode = ( pongGameMode + 1 ) % 3;
            }
        }
        else
        {
            pongModeHoldTicks = 0;
            pongModeHeld = 0;
        }

        if( isFirePressed() )
        {
            pongBeginGame();
            pongRenderFrame( PONG_MODE_COUNTDOWN );
            return;
        }
        pongRenderFrame( PONG_MODE_ATTRACT );
    }
    else if( pongState == PONG_STATE_COUNTDOWN )
    {
        if( pongWaitFrames > 0 ) { pongWaitFrames--; pongRenderFrame( PONG_MODE_COUNTDOWN ); return; }
        if( pongCountdownNumber > 1 )
        {
            pongCountdownNumber--;
            pongWaitFrames = 60;
            pongRenderFrame( PONG_MODE_COUNTDOWN );
            return;
        }
        pongState = PONG_STATE_PLAYING;
        pongAccumMs = 0.0;
        pongRenderFrame( PONG_MODE_PLAYING );
    }
    else if( pongState == PONG_STATE_PLAYING )
    {
        // Mid-game LEFT/RIGHT hold: short press cycles difficulty, long
        // press (~1.5s) toggles mute - matches upstream's own dual-gesture
        // on the same combined left-or-right analog read.
        int lr = isLeftPressed() || isRightPressed();
        if( lr )
        {
            pongDiffHoldTicks++;
            if( pongDiffHoldTicks >= 90 && !pongDiffLongDone )
            {
                pongDiffLongDone = 1;
                if( pongMute == 0 ) pongMute = 1; else pongMute = 0;
            }
            pongDiffHeld = 1;
        }
        else
        {
            if( pongDiffHeld && !pongDiffLongDone )
            {
                if( pongDifficulty == 1 ) { pongDifficulty = 2; pongPFactor = 12; }
                else if( pongDifficulty == 2 ) { pongDifficulty = 3; pongPFactor = 11; }
                else if( pongDifficulty == 3 ) { pongDifficulty = 4; pongPFactor = 10; }
                else { pongDifficulty = 1; pongPFactor = 12; }
                pongComputeFactor();
            }
            pongDiffHoldTicks = 0;
            pongDiffHeld = 0;
            pongDiffLongDone = 0;
        }

        pongAccumMs += 1000.0 / 60.0;
        if( pongAccumMs < (float)pongFactor ) { pongRenderFrame( PONG_MODE_PLAYING ); return; }
        pongAccumMs -= (float)pongFactor;
        if( pongAccumMs > (float)pongFactor ) pongAccumMs = (float)pongFactor;

        pongRunOneTick();
        if( pongState == PONG_STATE_PLAYING ) pongRenderFrame( PONG_MODE_PLAYING );
    }
    else if( pongState == PONG_STATE_ROUND_FLASH )
    {
        if( pongWaitFrames > 0 ) { pongWaitFrames--; return; }
        pongFlashStep++;
        if( pongFlashStep >= 6 )
        {
            pongComputeFactor();
            pongStartNoteSeq( pongStartNotes, PONG_START_COUNT );
            pongBeginCountdown();
            pongRenderFrame( PONG_MODE_COUNTDOWN );
            return;
        }
        if( pongFlashVisible ) pongFlashVisible = 0; else pongFlashVisible = 1;
        pongWaitFrames = 21;
        pongRenderFrame( PONG_MODE_ROUND_FLASH );
    }
    else // PONG_STATE_MATCH_WIN
    {
        if( pongWaitFrames > 0 ) { pongWaitFrames--; return; }
        pongFlashStep++;
        if( pongFlashStep >= 12 )
        {
            pongBeginAttract();
            pongRenderFrame( PONG_MODE_ATTRACT );
            return;
        }
        if( pongWinVisible ) pongWinVisible = 0; else pongWinVisible = 1;
        pongWaitFrames = 21;
        pongRenderFrame( PONG_MODE_MATCH_WIN );
    }
}

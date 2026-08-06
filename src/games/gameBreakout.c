// =============================================================================
// Breakout (Ilya Titov, 2015-2016, non-commercial-with-attribution; ATtiny-
// Joypad port by Billy Cheung, 2018; combined with Ilya Titov's own UFO into
// one cartridge by Andy Jackson, 2018) - a classic paddle-and-ball brick
// breaker: move the paddle left/right to keep a bouncing ball alive while it
// chips away a 3-row x 16-column block grid, scoring one point per block;
// clearing the whole grid (score reaching a multiple of 48) resets it and
// play continues; missing the ball ends the game.
//
// From `more games/gametiny/UFO_Breakout_Arduino/` - a genuinely **combined
// cartridge** (its own boot-time prompt lets the player choose UFO or
// Breakout, sharing font/sound/EEPROM-highscore/game-over-screen code
// between both), the same shape as `UFO_Stacker_Attiny` (already split into
// this project's own standalone UFO/Stacker entries). Matching that
// precedent, this is ported as its own standalone menu entry - the file's
// own `playUFO()` half was independently re-verified against the already-
// shipped UFO (a 34-line, all-whitespace/debounce-only diff confirms it's
// the same game, not a separate port) and is not touched here.
//
// This game, along with SpaceAttack and Falling Blocks (Andy Jackson's own
// falling-block puzzle clone, not Daniel C's Tiny Tris), was originally
// skipped from this project's `more games/gametiny/`
// survey purely because it shares a genre with an already-shipped game
// (Tiny Arkanoid) - the user directly pushed back on that reasoning and
// asked for a real code-level check rather than a genre-level one. Direct
// reading confirmed `playBreakout()` is a genuinely distinct implementation
// from Tiny Arkanoid (Daniel Champagne's own game) - a different author,
// different collision model (`row[3][16]` block grid + `hdir`/`vdir` ball-
// direction tracking vs. Arkanoid's own `TrackBary`/paddle-and-ball scheme),
// sharing nothing but the "brick breaker" genre label.
//
// Structural notes:
// - Not `tinyJoypadShim`/`obonoCoreShim` lineage by name, but - matching
//   every other `gametiny/` port here - needed no new shim: the same A0
//   left/right analog thresholds (500-750/750-950) as every other game in
//   this family map straight onto `isLeftPressed()`/`isRightPressed()`.
// - This game's own `font6x8AJ.h` was byte-diffed directly against the
//   already-shipped `UFO_Stacker_Attiny`/`gameStacker.c` copy (a 5-line,
//   comment-only diff) rather than assumed identical from the filename
//   alone (the lesson from Frogger's own differently-remapped copy of a
//   same-named file) - confirmed genuinely byte-identical, so the same
//   360-value table/remap formula/credit strings (`"mods bh andh
//   jackson"`->`"mods by andy jackson"`, `"/ebboggles.com"`->
//   `"webboggles.com"`) are reused here, each game still keeping its own
//   self-contained copy per this project's standing practice.
// - **A genuine, load-bearing upstream timing quirk, found by tracing the
//   control flow rather than assumed**: upstream's own `lastFrame` (a
//   `millis()`-based "don't run the frame-action block more than once
//   every 10ms" gate) is set exactly once, right before the outer
//   `while(stopAnimate==0){ delay(40); while(1==1){...} }` loop begins -
//   and is **never reassigned anywhere inside either loop**. Since the
//   inner `while(1==1)` only ever exits via the same `stopAnimate=1;
//   break;` that also ends the outer loop, `delay(40)` only ever executes
//   once, and `if (lastFrame+10<millis())` becomes permanently true after
//   the first 10ms and stays that way for the rest of the game - meaning
//   the "frame action" block (paddle-hit/game-over check, the block-reset-
//   at-score-48 check, and the block-collision-detection loop) actually
//   runs on **every** iteration of the uncapped inner loop, not gated at
//   all in practice. This isn't a bug worth "fixing" back to a real 10ms
//   gate (that would just be re-introducing behavior the original author
//   never actually shipped or tested) - ported as observed: the frame-
//   action block runs unconditionally every tick, same as the ball's own
//   unconditional wall-bounce movement check that precedes it. With no
//   other genuine real-time throttle left anywhere in the function (the
//   "no timing model whatsoever upstream" category, same as several other
//   Daniel-C/Andy-Jackson-family games in this project), this runs at the
//   engine's native 60fps with each tick corresponding to one iteration of
//   upstream's own uncapped inner loop - ball/paddle speed is a best-effort
//   approximation with no real-hardware reference to calibrate against,
//   same open-ended caveat as this project's other "no genuine rate to
//   match" ports.
// - The paddle's own upstream draw loop (`for(pw=1;pw<platformWidth;pw++)`)
//   sends exactly 15 bytes starting at column `player`, not 16 - a genuine
//   upstream off-by-one (the paddle is visually 15px wide despite
//   `platformWidth=16` driving every collision/movement-clamp calculation)
//   - ported literally rather than "corrected", since fixing it would
//   change the paddle's real hit-box-vs-visual-width relationship from
//   what was actually shipped.
// - The ball's own byte computation (`1<<((bally%8)+1)`) can produce bit 8
//   when `bally%8==7` - on real AVR `uint8_t` this silently truncates to 0
//   (the ball is invisible for that one specific sub-page row, a harmless
//   upstream quirk since nothing else depends on it), but Vircon32's
//   `int`s don't truncate - masked with `&0xFF` at the exact site, the
//   same byte-truncation fix class as this whole project's very first
//   documented bug.
// - `collision()`'s own `beep(30,300)` can in principle fire more than
//   once in a single tick if the block-collision-detection loop re-
//   triggers immediately (a ball corner clipping two blocks in the same
//   step) - Vircon32's audio channel has no queue, so a same-tick double
//   call would only ever be audible as the last one. Left as-is rather
//   than built into a full sequencer: unlike this project's other found
//   sound-burst bugs (computed sweeps firing tens to hundreds of calls
//   every single tick), this is a rare, at-most-two-calls edge case, not
//   a systematic multi-call burst - a deliberate, considered simplification
//   rather than an oversight.
// - The game-over sweep (`for(i=0;i<1000;i+=50)beep(50,i)`) and new-high
//   sweep (`for(i=700;i>200;i-=50)beep(30,i)`) are, byte-for-byte, the
//   same loop shape already fixed for Stacker's own identical sweeps (same
//   author/boilerplate lineage) - reused via the same frame-stepped
//   sequencer approach and the same derived note tables, rather than
//   re-deriving them from scratch.
// - EEPROM high-score persistence restored (see the project-wide "Real
//   persistent high-score saving" section in CLAUDE.md - a 2-byte big-
//   endian score at address 0, matching upstream exactly) - the "hold
//   fire ~2s to mute/unmute" gesture is
//   kept (in-memory flag, same shape as Stacker/UFO's own); the combined-
//   cartridge-specific "hold fire+up/down at boot to reset both games'
//   high scores" gesture doesn't apply to a single standalone menu entry
//   and is dropped outright, matching Stacker's own precedent exactly.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data: font (byte-diff-verified identical to gameStacker.c's own copy -
//   see this file's own header comment)
// -----------------------------------------------------------------------------

int[360] brkFONT =
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

int brkCharIndex( int ch )
{
    int c = ch - 32;
    if( c > 0 ) c = c - 12;
    if( c > 15 ) c = c - 6;
    if( c > 40 ) c = c - 9;
    return c;
}

int brkTextByte( int x, int y, int startX, int pageY, int* str )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= strlen( str ) ) return 0;
    int ch = str[charIdx];
    if( ch == 0 ) return 0;
    int within = rel - charIdx * 6;
    int fontIdx = brkCharIndex( ch );
    return brkFONT[ fontIdx * 6 + within ];
}

int brkCountDigits( int value )
{
    if( value <= 0 ) return 1;
    int v = value;
    int n = 0;
    while( v > 0 ) { n++; v = v / 10; }
    return n;
}

int brkDigitAt( int value, int posFromLeft, int totalDigits )
{
    int power = 1;
    int i;
    for( i = 0; i < totalDigits - 1 - posFromLeft; i++ ) power = power * 10;
    return ( value / power ) % 10;
}

int brkNumberByte( int x, int y, int startX, int pageY, int value )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int totalDigits = brkCountDigits( value );
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= totalDigits ) return 0;
    int within = rel - charIdx * 6;
    int digit = brkDigitAt( value, charIdx, totalDigits );
    int fontIdx = brkCharIndex( 48 + digit );
    return brkFONT[ fontIdx * 6 + within ];
}

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

int brkMute;

// Same beep(bCount,bDelay)->Sound(freq,dur) heuristic as gameStacker.c's own
// stkBeepOnce() (identical author/boilerplate lineage, same beep() call
// signature/semantics throughout this game too).
void brkBeepOnce( int bCount, int bDelay )
{
    if( brkMute ) return;
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

int brkSeqActive;
int* brkSeqNotes;
int brkSeqCount;
int brkSeqIndex;
int brkSeqWaitFrames;

void brkStartNoteSeq( int* notes, int count )
{
    brkSeqNotes = notes;
    brkSeqCount = count;
    brkSeqIndex = 0;
    brkSeqActive = 1;
    brkSeqWaitFrames = 0;
}

void brkAdvanceNoteSeq()
{
    if( !brkSeqActive ) return;
    if( brkSeqWaitFrames > 0 ) { brkSeqWaitFrames--; return; }
    if( brkSeqIndex >= brkSeqCount ) { brkSeqActive = 0; return; }
    int freq = brkSeqNotes[ brkSeqIndex * 2 ];
    int dur = brkSeqNotes[ brkSeqIndex * 2 + 1 ];
    if( !brkMute ) Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    brkSeqWaitFrames = waitFrames;
    brkSeqIndex++;
}

// for(i=0;i<1000;i+=50) beep(50,i) - game over (same shape as Stacker's own)
int[40] brkGameOverNotes =
{
250,40,243,40,230,40,218,40,205,40,193,40,180,40,168,40,155,40,143,40,
130,40,118,40,105,40,93,40,80,40,68,40,55,40,43,40,30,40,18,40,
};
#define BRK_GAMEOVER_COUNT 20

// for(i=700;i>200;i-=50) beep(30,i) - new high score
int[20] brkNewHighNotes =
{
80,31,93,31,105,31,118,31,130,31,143,31,155,31,168,31,180,31,193,31,
};
#define BRK_NEWHIGH_COUNT 10

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

#define BRK_PLATFORM_WIDTH 16
#define BRK_PLATFORM_HALF 8

// Added at direct user request (first 30fps, then corrected to 40fps).
// Upstream has no genuine real-time throttle at all (see this file's own
// header comment on the broken/always-true `lastFrame` gate - the "no
// timing model whatsoever upstream" category), so this is a deliberate
// slowdown rather than restoring an original rate. 60 does not divide
// evenly by 40, so a plain integer tick-skip counter (`60/BRK_FPS`, which
// would truncate to 1 and silently produce 60fps instead of 40) doesn't
// work here - uses the same Bresenham-style accumulator this project
// first needed for Tiny Gilbert's own 40fps target
// (`brkTickAccum += BRK_FPS; if >= 60 then -= 60 and run one tick`),
// producing exactly 40 ticks per 60 real frames long-term rather than a
// truncated approximation. Whole-tick gating (logic AND redraw together),
// matching Jump Slime/TinyRoG/TinY Fi's own precedent for this "no timing
// model upstream" category - this file has no pre-existing 60fps-tuned
// wait timer that would need to stay real-time accurate (brkWaitFrames is
// the only frame-counted constant, and per this project's own standing
// "one accumulator, no dual bookkeeping" practice it's deliberately left
// unrescaled - it simply now takes 1.5x as long in real time).
#define BRK_FPS 40
int brkTickAccum;

int brkPlayer;
int brkBallx;
int brkBally;
int brkHdir;
int brkVdir;
int brkScore;
int brkTop;
bool brkNewHigh;

int[3][16] brkRow;

void brkResetBlocks()
{
    int r, c;
    for( r = 0; r < 3; r++ )
      for( c = 0; c < 16; c++ )
        brkRow[r][c] = 1;
}

void brkBeginGame()
{
    brkResetBlocks();
    brkBallx = 64;
    brkBally = 50;
    brkHdir = -1;
    brkVdir = -1;
    brkScore = 0;
    brkPlayer = arand( 128 - BRK_PLATFORM_WIDTH );
    brkBallx = brkPlayer + BRK_PLATFORM_WIDTH / 2;
}

// matches upstream's own collision() - the block-hit check already found
// which side registered the hit; this works out the resulting bounce
// direction from which specific pixel-corner of the 8x8 cell was struck.
void brkCollision()
{
    int by = ( brkBally + brkVdir ) % 8;
    int bx = ( brkBallx + brkHdir ) % 8;

    if( by == 7 && bx == 7 ) // bottom right corner
    {
        if( brkVdir == 1 ) brkHdir = 1;
        else if( brkVdir == -1 && brkHdir == 1 ) brkVdir = 1;
        else { brkHdir = 1; brkVdir = 1; }
    }
    else if( by == 7 && bx == 0 ) // bottom left corner
    {
        if( brkVdir == 1 ) brkHdir = -1;
        else if( brkVdir == -1 && brkHdir == -1 ) brkVdir = 1;
        else { brkHdir = -1; brkVdir = 1; }
    }
    else if( by == 0 && bx == 0 ) // top left corner
    {
        if( brkVdir == -1 ) brkHdir = -1;
        else if( brkVdir == 1 && brkHdir == -1 ) brkVdir = -1;
        else { brkHdir = -1; brkVdir = -1; }
    }
    else if( by == 0 && bx == 7 ) // top right corner
    {
        if( brkVdir == -1 ) brkHdir = 1;
        else if( brkVdir == 1 && brkHdir == 1 ) brkVdir = -1;
        else { brkHdir = 1; brkVdir = -1; }
    }
    else if( by == 7 ) brkVdir = 1; // bottom side
    else if( by == 0 ) brkVdir = -1; // top side
    else if( bx == 7 ) brkHdir = 1; // right side
    else if( bx == 0 ) brkHdir = -1; // left side
    else { brkHdir = brkHdir * -1; brkVdir = brkVdir * -1; }

    brkBeepOnce( 30, 300 );
}

// matches upstream's own goto collisionCheck loop - re-checks after every
// destroyed block in case the new bounce direction also hits another.
void brkCollisionCheck()
{
    bool keepChecking = true;
    while( keepChecking )
    {
        keepChecking = false;
        int targetRow = ( brkBally + brkVdir ) / 8;
        int col = brkBallx / 8;
        if( targetRow >= 0 && targetRow <= 2 && col >= 0 && col < 16 )
        {
            if( brkRow[ targetRow ][ col ] == 1 )
            {
                brkRow[ targetRow ][ col ] = 0;
                brkScore = brkScore + 1;
                brkCollision();
                keepChecking = true;
            }
        }
    }
}

// Returns true (game over) if the ball just crossed the paddle row without
// being caught.
bool brkPlayingTick()
{
    if( isRightPressed() )
    {
        brkPlayer = brkPlayer + 3;
        if( brkPlayer > 128 - BRK_PLATFORM_WIDTH ) brkPlayer = 128 - BRK_PLATFORM_WIDTH;
    }
    else if( isLeftPressed() )
    {
        brkPlayer = brkPlayer - 3;
        if( brkPlayer < 0 ) brkPlayer = 0;
    }

    // vertical wall-bounce (top wall / paddle-row ceiling at y=54)
    if( ( brkVdir == 1 && brkBally + brkVdir < 54 ) || ( brkVdir == -1 && brkBally - brkVdir > 1 ) )
      brkBally = brkBally + brkVdir;
    else
      brkVdir = brkVdir * -1;

    // horizontal wall-bounce
    if( ( brkHdir == 1 && brkBallx + brkHdir < 127 ) || ( brkHdir == -1 && brkBallx - brkHdir > 1 ) )
      brkBallx = brkBallx + brkHdir;
    else
      brkHdir = brkHdir * -1;

    // frame action - see this file's own header comment on why this runs
    // unconditionally every tick, matching upstream's own real (if
    // unintended) behavior rather than a literal (never-actually-active)
    // 10ms gate.
    if( brkBally > 10 && brkBally + brkVdir >= 54 && ( brkBallx < brkPlayer || brkBallx > brkPlayer + BRK_PLATFORM_WIDTH ) )
    {
        return true; // missed the paddle - game over
    }
    else if( brkBallx < brkPlayer + BRK_PLATFORM_HALF && brkBally > 10 && brkBally + brkVdir >= 54 )
    {
        brkHdir = -1;
        brkBeepOnce( 20, 600 );
    }
    else if( brkBallx > brkPlayer + BRK_PLATFORM_HALF && brkBally > 10 && brkBally + brkVdir >= 54 )
    {
        brkHdir = 1;
        brkBeepOnce( 20, 600 );
    }
    else if( brkBally + brkVdir >= 54 )
    {
        brkHdir = 1;
        brkBeepOnce( 20, 600 );
    }

    if( ( brkScore % 48 ) == 0 ) brkResetBlocks();

    brkCollisionCheck();

    return false;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int brkComputeByte( int x, int page )
{
    int val = 0;

    if( page <= 2 )
    {
        int col = x / 8;
        if( col >= 0 && col < 16 && brkRow[ page ][ col ] == 1 )
        {
            int within = x % 8;
            if( within >= 1 && within <= 6 ) val = val | 0x7E;
        }
    }
    else if( page == 7 )
    {
        if( x >= brkPlayer && x < brkPlayer + 15 ) val = val | 0x03;
    }

    if( page == brkBally / 8 && x == brkBallx )
    {
        int bit = ( 1 << ( ( brkBally % 8 ) + 1 ) ) & 0xFF;
        val = val | bit;
    }

    return val;
}

#define BRK_MODE_ATTRACT  0
#define BRK_MODE_PLAYING  1
#define BRK_MODE_GAMEOVER 2
#define BRK_MODE_NEWHIGH  3

void brkRenderFrame( int mode )
{
    md_beginFrame();
    int x, y;
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
        {
            int val = 0;
            if( mode == BRK_MODE_ATTRACT )
            {
                if( y == 2 ) val = val | brkTextByte( x, y, 16, 2, "B R E A K O U T" );
                else if( y == 4 ) val = val | brkTextByte( x, y, 3, 4, "mods bh andh jackson" );
                else if( y == 6 ) val = val | brkTextByte( x, y, 22, 6, "game design bh" );
                else if( y == 7 ) val = val | brkTextByte( x, y, 22, 7, "/ebboggles.com" );
                else if( y == 0 && brkMute ) val = val | brkTextByte( x, y, 32, 0, "-- MUTE --" );
            }
            else if( mode == BRK_MODE_PLAYING )
            {
                val = brkComputeByte( x, y );
            }
            else if( mode == BRK_MODE_GAMEOVER )
            {
                if( y == 1 ) val = val | brkTextByte( x, y, 11, 1, "----------------" );
                else if( y == 2 ) val = val | brkTextByte( x, y, 11, 2, "G A M E  O V E R" );
                else if( y == 3 ) val = val | brkTextByte( x, y, 11, 3, "----------------" );
                else if( y == 5 ) val = val | brkTextByte( x, y, 37, 5, "SCORE:" ) | brkNumberByte( x, y, 75, 5, brkScore );
                else if( y == 7 && !brkNewHigh ) val = val | brkTextByte( x, y, 21, 7, "HIGH SCORE:" ) | brkNumberByte( x, y, 88, 7, brkTop );
            }
            else if( mode == BRK_MODE_NEWHIGH )
            {
                if( y == 1 ) val = val | brkTextByte( x, y, 10, 1, "----------------" );
                else if( y == 3 ) val = val | brkTextByte( x, y, 10, 3, " NEW HIGH SCORE " );
                else if( y == 7 ) val = val | brkTextByte( x, y, 10, 7, "----------------" );
                else if( y == 5 ) val = val | brkNumberByte( x, y, 50, 5, brkTop );
            }
            md_drawColumn( x, y, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define BRK_STATE_ATTRACT       0
#define BRK_STATE_PLAYING       1
#define BRK_STATE_GAMEOVER_WAIT 2
#define BRK_STATE_NEWHIGH_WAIT  3

int brkState;
int brkWaitFrames;
int brkFireHeld;
int brkFireHoldTicks;
int brkMuteActionDone;

void brkBeginAttract()
{
    brkFireHeld = 0;
    brkFireHoldTicks = 0;
    brkMuteActionDone = 0;
    brkState = BRK_STATE_ATTRACT;
}

void brkEndGame()
{
    if( brkScore > brkTop )
    {
        brkTop = brkScore;
        brkNewHigh = true;
        // Direct translation of upstream's own 2-byte big-endian
        // EEPROM.write(0,...)/EEPROM.write(1,...) topScoreB save (this
        // game's own Breakout half of the combined UFO_Breakout_Arduino
        // cartridge).
        eeprom_write_word( 0, brkTop );
    }
    else
      brkNewHigh = false;

    brkRenderFrame( BRK_MODE_GAMEOVER );
    brkStartNoteSeq( brkGameOverNotes, BRK_GAMEOVER_COUNT );
    brkWaitFrames = 120;
    brkState = BRK_STATE_GAMEOVER_WAIT;
}

void brkBeginNewHighWait()
{
    brkRenderFrame( BRK_MODE_NEWHIGH );
    brkStartNoteSeq( brkNewHighNotes, BRK_NEWHIGH_COUNT );
    brkWaitFrames = 162; // ~2.7s at 60fps, matches upstream's own delay(2700)
    brkState = BRK_STATE_NEWHIGH_WAIT;
}

void gameBreakout_init()
{
    brkMute = false;
    // Direct translation of upstream's own topScoreB = EEPROM.read(0)<<8 |
    // EEPROM.read(1), guarded against a never-written slot's own virgin
    // 65535 read the same way as every other game in this pass.
    brkTop = eeprom_read_word( 0 );
    if( brkTop == 65535 ) brkTop = 0;
    brkSeqActive = 0;
    brkBeginAttract();
}

void gameBreakout_forceRedraw()
{
    if( brkState == BRK_STATE_ATTRACT ) brkRenderFrame( BRK_MODE_ATTRACT );
    else if( brkState == BRK_STATE_PLAYING ) brkRenderFrame( BRK_MODE_PLAYING );
    else if( brkState == BRK_STATE_GAMEOVER_WAIT ) brkRenderFrame( BRK_MODE_GAMEOVER );
    else brkRenderFrame( BRK_MODE_NEWHIGH );
}

void gameBreakout_update()
{
    brkTickAccum = brkTickAccum + BRK_FPS;
    if( brkTickAccum < 60 )
      return;
    brkTickAccum = brkTickAccum - 60;

    brkAdvanceNoteSeq();

    if( brkState == BRK_STATE_ATTRACT )
    {
        int fireDown = isFirePressed();
        if( fireDown )
        {
            brkFireHoldTicks++;
            if( brkFireHoldTicks >= 120 && !brkMuteActionDone )
            {
                brkMuteActionDone = 1;
                if( brkMute == false ) brkMute = true; else brkMute = false;
            }
        }
        else
        {
            if( brkFireHeld && !brkMuteActionDone )
            {
                brkFireHeld = fireDown;
                brkBeginGame();
                brkState = BRK_STATE_PLAYING;
                brkRenderFrame( BRK_MODE_PLAYING );
                return;
            }
            brkFireHoldTicks = 0;
            brkMuteActionDone = 0;
        }
        brkFireHeld = fireDown;
        brkRenderFrame( BRK_MODE_ATTRACT );
    }
    else if( brkState == BRK_STATE_PLAYING )
    {
        bool gameOver = brkPlayingTick();
        if( gameOver ) { brkEndGame(); return; }
        brkRenderFrame( BRK_MODE_PLAYING );
    }
    else if( brkState == BRK_STATE_GAMEOVER_WAIT )
    {
        if( brkSeqActive ) return;
        if( brkWaitFrames > 0 ) { brkWaitFrames--; return; }

        if( brkNewHigh ) brkBeginNewHighWait();
        else brkBeginAttract();
    }
    else if( brkState == BRK_STATE_NEWHIGH_WAIT )
    {
        if( brkSeqActive ) return;
        if( brkWaitFrames > 0 ) { brkWaitFrames--; return; }

        brkBeginAttract();
    }
}

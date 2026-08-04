// =============================================================================
// SpaceAttack (Andy Jackson, 2015-2016, non-commercial-with-attribution;
// ATtiny-Joypad port by Billy Cheung, 2018) - a classic Space-Invaders-style
// shooter: move the fighter jet left/right and fire up at 3 rows of aliens
// (14 total) that step left-right-then-down as a block, occasionally
// launching their own fire back down; a passing "mothership" bonus target
// crosses the top of the screen for occasional big points. Clearing all 14
// aliens advances the level (faster alien movement/fire rate, up to level
// 15); getting hit by alien fire ends the game.
//
// From `more games/gametiny/SpaceAttackAttiny/` - the game this project's
// own CLAUDE.md had, until a direct user request, wrongly excluded on
// genre-similarity alone ("shares a genre with Tiny Invaders, skip it") -
// direct reading confirmed it's a genuinely distinct codebase by a
// different author (Andy Jackson, not Daniel C/Sven B), with its own
// mothership bonus mechanic, EEPROM highscore, and scoring model, sharing
// nothing with Tiny Invaders but the "aliens descend, shoot them" genre
// label. See `more games/gametiny/`'s own catalog entry and this file's
// own Status intro in CLAUDE.md for the full re-verification writeup, and
// this project's own Breakout port (the first of the 3 games found by that
// same re-check) for the shared "gametiny/Andy-Jackson-family" boilerplate
// this game also uses. `SpaceAttackAttiny2but` (a 2-button-hardware control
// variant of this same game, confirmed via a 119-line all-comment/control-
// mapping diff) is not a separate title and isn't ported separately -
// TinyJoypad has a real dedicated fire button, so the non-`2but` version
// (using it directly) is the natural fit, matching this project's own
// established preference for the native control mapping over a hardware-
// constrained alternate scheme.
//
// Structural notes:
// - Not `tinyJoypadShim`/`obonoCoreShim` lineage by name, but - matching
//   every other `gametiny/` port here - needed no new shim: the same A0
//   left/right analog thresholds as every other game in this family map
//   straight onto `isLeftPressed()`/`isRightPressed()`. The upstream fire
//   button is interrupt-driven (`ISR(PCINT0_vect)` sets a persistent `fire`
//   flag on a falling edge), but tracing every place `fire` is read and
//   cleared confirms it behaves as a plain **level** check in practice -
//   `fire` is only ever cleared exactly when a shot resolves (reaches the
//   top unhit, or hits something), never on button release - so holding
//   Fire genuinely auto-fires a new shot the instant the previous one
//   resolves. Ported as a direct `isFirePressed()` level read feeding the
//   same "start a new shot only if none is currently active" gate, with no
//   edge-detection needed for the core fire mechanic (edge-detection is
//   still used, separately, for the attract screen's own hold-to-mute
//   gesture, matching Stacker/Breakout's own precedent).
// - This game's own `font6x8AJ.h` was byte-diffed directly against the
//   already-shipped Stacker/Breakout copy (a 5-line, comment-only diff)
//   rather than assumed identical from the filename alone - confirmed
//   genuinely byte-identical, so the same 360-value table/remap formula is
//   reused, this game still keeping its own self-contained copy per this
//   project's standing practice. Credit strings (`"andh jackson"`,
//   `"inspired bh"`, `"/ebboggles.com"`) ported verbatim (h->y, w->[slash]
//   substitution), matching every other game built on this same font.
// - Upstream's own alien/mothership/fire timing (`aliencounter`,
//   `firecounter`, `mothercounter`) are plain tick counters incremented
//   once per iteration of a genuinely bare, uncapped inner `while(1)` loop
//   - no real `millis()`-based reference anywhere in this function (unlike
//   Breakout's own broken-but-load-bearing `lastFrame` gate) - the "no
//   timing model whatsoever upstream" category. Ported with a direct 1
//   real-engine-tick = 1 upstream-loop-iteration correspondence: alien
//   movement is gated by `aliencounter >= 92-((level-1)*5)` real ticks,
//   fire-handling by `firecounter >= 2` real ticks (a genuinely functional
//   divide-by-2 gate, unlike Breakout's own broken always-true one), and
//   mothership redraw/movement by `mothercounter >= 3` - all three ported
//   as direct tick-counter comparisons, no restructuring needed since none
//   of them are actual blocking waits.
// - Upstream's own "burn clock cycles" loop
//   (`for(burn=0;burn<burnLimit;burn+=2)drawPlatform();`) is a pure AVR
//   real-time-compensation hack - as fewer aliens remain, less per-
//   iteration work is done, so the bare loop would otherwise speed up;
//   redundantly redrawing the platform (with no visible effect, since it
//   draws the same sprite repeatedly) burns extra real microseconds to
//   keep iteration time roughly constant regardless of alien count. This
//   has no equivalent need on a fixed-tick-rate engine (the same class of
//   AVR-timing-artifact this project has dropped before - Tiny SQuest's
//   own `Skip_Frame` split, Tiny Doc/Bert's partial-redraw tricks) -
//   dropped entirely rather than ported as pointless extra `md_drawColumn`
//   calls that would only cost real Vircon32 CPU budget for zero effect.
// - The attract screen's own elaborate slide-in animation (a real ~91-
//   frame, `delay(20)`-paced sequence sliding a descending "fire" and the
//   platform into place before showing credits) is a decorative flourish
//   with no gameplay effect - simplified to a static attract screen
//   (title, the same small decorative 3-row alien-formation graphic
//   upstream also draws statically before that animation ever starts,
//   credits, and the platform at its fixed initial position) rather than
//   built out as its own multi-second animated state, a deliberate
//   effort/fidelity tradeoff for a purely cosmetic sequence - unlike
//   Stacker's own static "ascending staircase" decoration (ported in
//   full, since it needed no animation state at all to reproduce).
// - `fireXidx`/`fireYidx` (the player-shot-to-alien-grid collision mapping)
//   are computed upstream via `floor(intExpr)` - but since both inner
//   expressions are already plain `int`/`int` division (C's own `/`
//   between two ints truncates toward zero, it does not implicitly
//   promote to float), the `floor()` calls are no-ops applied to an
//   already-integer value, not a real floor-toward-negative-infinity
//   operation - ported as plain integer division with no special negative-
//   number handling, faithfully reproducing upstream's real (imperfect)
//   behavior rather than "fixing" it into a genuine floor division upstream
//   never actually performed.
// - Sound: the mothership-destroyed cue
//   (`beep(30,400);beep(30,300);beep(30,200);beep(30,100);`, 4 calls back-
//   to-back) is a genuine burst - Vircon32's audio channel has no queue, so
//   4 same-tick calls would only ever be audible as the last one - fixed
//   proactively with a small frame-stepped sequencer, the same treatment
//   as every other multi-call burst already found in this project. The
//   level-up cue (`for(i=800;i>200;i-=200)beep(30,i)`, 3 calls) and the
//   game-over/new-high sweeps are, byte-for-byte, the exact same loop
//   shapes already fixed for Stacker/Breakout's own identical cues (same
//   author/boilerplate lineage) - reused via the same derived note tables
//   rather than re-deriving them from scratch. The single alien-hit beep
//   needs no sequencing (only ever one call).
// - EEPROM high-score persistence dropped (session-only), matching every
//   other port's precedent - the "hold fire ~2s to mute/unmute" gesture is
//   kept (in-memory flag); the "hold fire+left/right to reset high score"
//   gesture doesn't apply to a session-only score and is dropped outright,
//   matching Stacker/Breakout's own precedent exactly.
// - `row[4][10]` is declared upstream but only rows 0-2 (and columns 0-8 of
//   10) are ever read or written (`resetAliens()`'s own init, every
//   `lastActiveRow`/`fireYidx` bound check) - row 3 and column 9 are
//   confirmed dead by inspection, ported as `int[3][9]` instead of the
//   full declared size.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data: font (byte-diff-verified identical to gameStacker.c/gameBreakout.c)
// -----------------------------------------------------------------------------

int[360] spaFONT =
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

int spaCharIndex( int ch )
{
    int c = ch - 32;
    if( c > 0 ) c = c - 12;
    if( c > 15 ) c = c - 6;
    if( c > 40 ) c = c - 9;
    return c;
}

int spaTextByte( int x, int y, int startX, int pageY, int* str )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= strlen( str ) ) return 0;
    int ch = str[charIdx];
    if( ch == 0 ) return 0;
    int within = rel - charIdx * 6;
    int fontIdx = spaCharIndex( ch );
    return spaFONT[ fontIdx * 6 + within ];
}

int spaCountDigits( int value )
{
    if( value <= 0 ) return 1;
    int v = value;
    int n = 0;
    while( v > 0 ) { n++; v = v / 10; }
    return n;
}

int spaDigitAt( int value, int posFromLeft, int totalDigits )
{
    int power = 1;
    int i;
    for( i = 0; i < totalDigits - 1 - posFromLeft; i++ ) power = power * 10;
    return ( value / power ) % 10;
}

int spaNumberByte( int x, int y, int startX, int pageY, int value )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int totalDigits = spaCountDigits( value );
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= totalDigits ) return 0;
    int within = rel - charIdx * 6;
    int digit = spaDigitAt( value, charIdx, totalDigits );
    int fontIdx = spaCharIndex( 48 + digit );
    return spaFONT[ fontIdx * 6 + within ];
}

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

int spaMute;

// Same beep(bCount,bDelay)->Sound(freq,dur) heuristic as gameStacker.c's own
// stkBeepOnce()/gameBreakout.c's own brkBeepOnce() (identical author/
// boilerplate lineage, same beep() call signature/semantics here too).
void spaBeepOnce( int bCount, int bDelay )
{
    if( spaMute ) return;
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

int spaSeqActive;
int* spaSeqNotes;
int spaSeqCount;
int spaSeqIndex;
int spaSeqWaitFrames;

void spaStartNoteSeq( int* notes, int count )
{
    spaSeqNotes = notes;
    spaSeqCount = count;
    spaSeqIndex = 0;
    spaSeqActive = 1;
    spaSeqWaitFrames = 0;
}

void spaAdvanceNoteSeq()
{
    if( !spaSeqActive ) return;
    if( spaSeqWaitFrames > 0 ) { spaSeqWaitFrames--; return; }
    if( spaSeqIndex >= spaSeqCount ) { spaSeqActive = 0; return; }
    int freq = spaSeqNotes[ spaSeqIndex * 2 ];
    int dur = spaSeqNotes[ spaSeqIndex * 2 + 1 ];
    if( !spaMute ) Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    spaSeqWaitFrames = waitFrames;
    spaSeqIndex++;
}

// for(i=800;i>200;i-=200) beep(30,i) - level up (same shape as Stacker's own)
int[6] spaLevelUpNotes = { 55,31,105,31,155,31, };
#define SPA_LEVELUP_COUNT 3

// beep(30,400);beep(30,300);beep(30,200);beep(30,100); - mothership destroyed
int[8] spaMothershipNotes = { 155,31,180,31,205,31,230,31, };
#define SPA_MOTHERSHIP_COUNT 4

// for(i=0;i<1000;i+=50) beep(50,i) - game over
int[40] spaGameOverNotes =
{
250,40,243,40,230,40,218,40,205,40,193,40,180,40,168,40,155,40,143,40,
130,40,118,40,105,40,93,40,80,40,68,40,55,40,43,40,30,40,18,40,
};
#define SPA_GAMEOVER_COUNT 20

// for(i=700;i>200;i-=50) beep(30,i) - new high score
int[20] spaNewHighNotes =
{
80,31,93,31,105,31,118,31,130,31,143,31,155,31,168,31,180,31,193,31,
};
#define SPA_NEWHIGH_COUNT 10

// -----------------------------------------------------------------------------
//   Sprite data
// -----------------------------------------------------------------------------

int[8] spaAlienSprite1 = { 0x98, 0x5C, 0xB6, 0x5F, 0x5F, 0xB6, 0x5C, 0x98, };
int[8] spaAlienSprite2 = { 0x30, 0x3E, 0xB3, 0x5D, 0x5D, 0xB3, 0x3E, 0x30, };
int[8] spaMothershipSprite = { 0x18, 0x38, 0x34, 0x34, 0x34, 0x34, 0x38, 0x18, };

int spaPlatformByte( int offset )
{
    if( offset < 2 || offset > 15 ) return 0;
    if( offset <= 6 ) return 0xC0;
    if( offset <= 10 ) return 0xF0;
    return 0xC0;
}

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

#define SPA_SCREEN_WIDTH 128
#define SPA_PLATFORM_WIDTH 16
#define SPA_MAX_ALIEN_FIRE 5
#define SPA_MAX_LEVEL 15

int spaAlienCounter;
int spaFireCounter;
int spaMotherCounter;
int spaLevel;
int spaMothershipX;
bool spaMothership;
int spaFireXidx, spaFireYidx;
int spaLeftLimit;
int spaPositionNow;
bool spaAlienDirection;
int spaAlienRow;
// upstream's own alienFire[5][3] - 5 concurrent shot slots, 3 fields each
// (active/x/y), split here into 3 parallel arrays. A real bug caught via
// direct user report ("enemy bullets... something is off"): an earlier
// draft declared these as int[3] instead of int[SPA_MAX_ALIEN_FIRE] (5) -
// mixing up the two dimensions - so every loop's own 5th iteration
// (indices 3 and 4, since every loop here iterates
// `for(i=0;i<SPA_MAX_ALIEN_FIRE;i++)`) read/wrote past the end of these
// arrays, a genuine out-of-bounds access rather than a logic bug.
int[5] spaAlienFireActive;
int[5] spaAlienFireX;
int[5] spaAlienFireY;
bool spaPlayerFireActive;
int spaPlayerFireX;
int spaPlayerFireY;
int[3][9] spaRow;
int spaFirstAlien;
int spaLastAlien;
int spaAliensDead;
int spaLastActiveRow;
int spaDeadOn1;
int spaDeadOn2;

int spaPlayer;
int spaScore;
int spaTop;
bool spaNewHigh;

void spaResetAliens()
{
    int i;
    for( i = 0; i < 9; i++ ) { spaRow[0][i] = 0; spaRow[1][i] = 0; spaRow[2][i] = 0; }
    for( i = 0; i <= 8; i = i + 2 ) { spaRow[0][i] = 1; spaRow[2][i] = 1; }
    for( i = 1; i <= 8; i = i + 2 ) { spaRow[1][i] = 1; }
}

// number isn't taken as a parameter here (unlike upstream's own
// levelUp(int number)) since the caller always sets spaLevel itself
// before calling this, and the LEVEL UP screen reads spaLevel directly.
void spaBeginLevel()
{
    spaPlayerFireActive = false;
    spaAlienCounter = 0;
    spaFireCounter = 0;
    spaMotherCounter = 0;
    spaFirstAlien = 0;
    spaLastAlien = 8;
    spaLastActiveRow = 2;
    spaDeadOn1 = 0;
    spaDeadOn2 = 0;
    spaAliensDead = 0;
    spaMothership = false;
    spaAlienRow = 0;
    spaPositionNow = 0;
    spaAlienDirection = true;
    spaPlayer = 64;

    int i;
    for( i = 0; i < SPA_MAX_ALIEN_FIRE; i++ ) spaAlienFireActive[i] = 0;

    spaStartNoteSeq( spaLevelUpNotes, SPA_LEVELUP_COUNT );
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int spaFireColumnByte( int page, int y )
{
    if( ( y % 8 ) != 0 )
    {
        if( page == y / 8 ) return ( 0x7E << ( y % 8 ) ) & 0xFF;
        if( page == y / 8 + 1 ) return 0x7E >> ( 8 - ( y % 8 ) );
        return 0;
    }
    if( page == y / 8 ) return 0x7E;
    return 0;
}

// A real CPU-budget problem found via a direct user report (reads of
// 93-98% during normal play, measured with the perf overlay) - the first
// draft's own `spaComputeByte()` queried every one of 1024 pixels/frame
// individually: an alien-grid lookup for every pixel, plus (for the fire
// layer specifically) a 5-slot alien-fire scan *for every pixel*
// (5120 checks/frame) to find columns that are almost always empty - the
// same O(pixels x objects) shape this project has repeatedly found and
// fixed elsewhere. Fixed by compositing each page's own real content
// (aliens/mothership/platform/fire) directly into a shared row buffer
// once per page, touching only each feature's own real, narrow column
// range instead of scanning all 128 columns per feature.
int[128] spaRowBuffer;

void spaComposeRow( int page )
{
    int x;
    for( x = 0; x < SPA_SCREEN_WIDTH; x++ ) spaRowBuffer[ x ] = 0;

    int inc = page - spaAlienRow;
    if( inc >= 0 && inc <= spaLastActiveRow )
    {
        int startX = spaPositionNow * 8;
        int count = spaLastAlien - spaFirstAlien + 1;
        int endX = startX + count * 8;
        int xx;
        for( xx = startX; xx < endX; xx++ )
        {
            if( xx < 0 || xx >= SPA_SCREEN_WIDTH ) continue;
            int bl = spaFirstAlien + ( xx - startX ) / 8;
            if( bl > spaLastAlien ) continue;
            if( spaRow[ inc ][ bl ] == 0 ) continue;
            int offsetInCell = ( xx - startX ) % 8;
            if( inc == 0 || inc == 2 ) spaRowBuffer[ xx ] = spaRowBuffer[ xx ] | spaAlienSprite1[ offsetInCell ];
            else spaRowBuffer[ xx ] = spaRowBuffer[ xx ] | spaAlienSprite2[ offsetInCell ];
        }
    }

    if( page == 0 && spaMothership )
    {
        int xx;
        for( xx = spaMothershipX; xx < spaMothershipX + 8; xx++ )
        {
            if( xx >= 0 && xx < SPA_SCREEN_WIDTH )
              spaRowBuffer[ xx ] = spaRowBuffer[ xx ] | spaMothershipSprite[ xx - spaMothershipX ];
        }
    }

    if( page == 7 )
    {
        int xx;
        for( xx = spaPlayer; xx < spaPlayer + 18; xx++ )
        {
            if( xx < 0 || xx >= SPA_SCREEN_WIDTH ) continue;
            int b = spaPlatformByte( xx - spaPlayer );
            if( b != 0 ) spaRowBuffer[ xx ] = spaRowBuffer[ xx ] | b;
        }
    }

    if( spaPlayerFireActive && spaPlayerFireX >= 0 && spaPlayerFireX < SPA_SCREEN_WIDTH )
    {
        int b = spaFireColumnByte( page, spaPlayerFireY );
        if( b != 0 ) spaRowBuffer[ spaPlayerFireX ] = spaRowBuffer[ spaPlayerFireX ] | b;
    }

    int i;
    for( i = 0; i < SPA_MAX_ALIEN_FIRE; i++ )
    {
        if( !spaAlienFireActive[ i ] ) continue;
        if( spaAlienFireX[ i ] < 0 || spaAlienFireX[ i ] >= SPA_SCREEN_WIDTH ) continue;
        int b = spaFireColumnByte( page, spaAlienFireY[ i ] );
        if( b != 0 ) spaRowBuffer[ spaAlienFireX[ i ] ] = spaRowBuffer[ spaAlienFireX[ i ] ] | b;
    }
}

int spaComputeByte( int x, int page )
{
    int val = spaRowBuffer[ x ];
    if( page == 6 ) val = val | spaNumberByte( x, page, 0, 6, spaScore );
    return val;
}

#define SPA_MODE_ATTRACT  0
#define SPA_MODE_PLAYING  1
#define SPA_MODE_LEVELUP  2
#define SPA_MODE_GAMEOVER 3
#define SPA_MODE_NEWHIGH  4

// The attract screen's own small decorative 3-row alien-formation graphic
// (upstream draws this once, statically, at x=85, before its own slide-in
// animation ever starts - see this file's own header comment on why only
// the static part is reproduced here). row1: alien,blank,alien. row2:
// blank,alien2,blank. row3: blank,blank,alien,blank,alien.
int spaAlienByteAttract( int row, int x )
{
    if( row == 1 )
    {
        if( x >= 85 && x < 93 ) return spaAlienSprite1[ x - 85 ];
        if( x >= 101 && x < 109 ) return spaAlienSprite1[ x - 101 ];
        return 0;
    }
    if( row == 2 )
    {
        if( x >= 93 && x < 101 ) return spaAlienSprite2[ x - 93 ];
        return 0;
    }
    // row 3
    if( x >= 101 && x < 109 ) return spaAlienSprite1[ x - 101 ];
    if( x >= 117 && x < 125 ) return spaAlienSprite1[ x - 117 ];
    return 0;
}

void spaRenderFrame( int mode )
{
    md_beginFrame();
    int x, y;
    for( y = 0; y < 8; y++ )
    {
        if( mode == SPA_MODE_PLAYING ) spaComposeRow( y );

        for( x = 0; x < SPA_SCREEN_WIDTH; x++ )
        {
            int val = 0;
            if( mode == SPA_MODE_ATTRACT )
            {
                if( y == 1 ) val = val | spaTextByte( x, y, 0, 1, "S P A C E" ) | spaAlienByteAttract( 1, x );
                else if( y == 2 ) val = val | spaTextByte( x, y, 4, 2, "A T T A C K" ) | spaAlienByteAttract( 2, x );
                else if( y == 3 ) val = val | spaAlienByteAttract( 3, x );
                else if( y == 4 ) val = val | spaTextByte( x, y, 0, 4, "andh jackson" );
                else if( y == 6 ) val = val | spaTextByte( x, y, 0, 6, "inspired bh" );
                else if( y == 7 ) val = val | spaTextByte( x, y, 0, 7, "/ebboggles.com" );
                else if( y == 0 && spaMute ) val = val | spaTextByte( x, y, 32, 0, "-- MUTE --" );
                if( y == 7 ) val = val | spaPlatformByte( x - 96 );
            }
            else if( mode == SPA_MODE_PLAYING )
            {
                val = spaComputeByte( x, y );
            }
            else if( mode == SPA_MODE_LEVELUP )
            {
                if( y == 3 ) val = val | spaTextByte( x, y, 16, 3, "--------------" );
                else if( y == 4 ) val = val | spaTextByte( x, y, 16, 4, " L E V E L " ) | spaNumberByte( x, y, 85, 4, spaLevel );
                else if( y == 5 ) val = val | spaTextByte( x, y, 16, 5, "--------------" );
            }
            else if( mode == SPA_MODE_GAMEOVER )
            {
                if( y == 1 ) val = val | spaTextByte( x, y, 11, 1, "----------------" );
                else if( y == 2 ) val = val | spaTextByte( x, y, 11, 2, "G A M E  O V E R" );
                else if( y == 3 ) val = val | spaTextByte( x, y, 11, 3, "----------------" );
                else if( y == 5 ) val = val | spaTextByte( x, y, 37, 5, "SCORE:" ) | spaNumberByte( x, y, 75, 5, spaScore );
                else if( y == 7 && !spaNewHigh ) val = val | spaTextByte( x, y, 21, 7, "HIGH SCORE:" ) | spaNumberByte( x, y, 88, 7, spaTop );
            }
            else if( mode == SPA_MODE_NEWHIGH )
            {
                if( y == 1 ) val = val | spaTextByte( x, y, 10, 1, "----------------" );
                else if( y == 3 ) val = val | spaTextByte( x, y, 10, 3, " NEW HIGH SCORE " );
                else if( y == 7 ) val = val | spaTextByte( x, y, 10, 7, "----------------" );
                else if( y == 5 ) val = val | spaNumberByte( x, y, 50, 5, spaTop );
            }
            md_drawColumn( x, y, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define SPA_STATE_ATTRACT       0
#define SPA_STATE_LEVELUP       1
#define SPA_STATE_PLAYING       2
#define SPA_STATE_GAMEOVER_WAIT 3
#define SPA_STATE_NEWHIGH_WAIT  4

int spaState;
int spaWaitFrames;
int spaFireHeld;
int spaFireHoldTicks;
int spaMuteActionDone;

void spaBeginAttract()
{
    spaFireHeld = 0;
    spaFireHoldTicks = 0;
    spaMuteActionDone = 0;
    spaPlayer = 96;
    spaState = SPA_STATE_ATTRACT;
}

void spaBeginGame()
{
    spaLevel = 1;
    spaScore = 0;
    spaResetAliens();
    spaBeginLevel();
    spaState = SPA_STATE_LEVELUP;
    spaWaitFrames = 42; // ~700ms at 60fps, matches upstream's own delay(700)
}

void spaEndGame()
{
    if( spaScore > spaTop )
    {
        spaTop = spaScore;
        spaNewHigh = true;
    }
    else
      spaNewHigh = false;

    spaRenderFrame( SPA_MODE_GAMEOVER );
    spaStartNoteSeq( spaGameOverNotes, SPA_GAMEOVER_COUNT );
    spaWaitFrames = 120;
    spaState = SPA_STATE_GAMEOVER_WAIT;
}

void spaBeginNewHighWait()
{
    spaRenderFrame( SPA_MODE_NEWHIGH );
    spaStartNoteSeq( spaNewHighNotes, SPA_NEWHIGH_COUNT );
    spaWaitFrames = 162; // ~2.7s at 60fps, matches upstream's own delay(2700)
    spaState = SPA_STATE_NEWHIGH_WAIT;
}

// Returns true if the player was just hit (game over).
bool spaPlayingTick()
{
    if( isRightPressed() )
    {
        if( spaPlayer < 127 - SPA_PLATFORM_WIDTH ) spaPlayer++;
    }
    else if( isLeftPressed() )
    {
        if( spaPlayer > 1 ) spaPlayer--;
    }

    spaAlienCounter++;
    spaFireCounter++;
    spaMotherCounter++;

    if( !spaMothership && arand( 1000 ) > 998 && spaAlienRow > 0 )
    {
        spaMothership = true;
        spaMothershipX = 127 - 16;
    }

    // Alien fire spawn - matches upstream's own per-alien-draw-loop check
    // exactly: upstream rolls this chance *independently for every live
    // alien in the bottom active row* (nested inside its own per-alien
    // draw loop, `if(inc==lastActiveRow){ if(random(0,1000)>(999-level))
    // {...} }`), not once per tick - an earlier draft of this port
    // collapsed it to a single per-tick roll with a randomized fire
    // position, which is a materially different (and much rarer, since a
    // full row of live aliens gives up to 9 independent rolls) mechanic -
    // caught and fixed by re-reading upstream's real loop structure.
    int spawnBl;
    for( spawnBl = spaFirstAlien; spawnBl <= spaLastAlien; spawnBl++ )
    {
        if( spaRow[ spaLastActiveRow ][ spawnBl ] != 1 ) continue;
        if( arand( 1000 ) <= ( 999 - spaLevel ) ) continue;

        int afIndex = 0;
        while( afIndex < SPA_MAX_ALIEN_FIRE && spaAlienFireActive[ afIndex ] == 1 ) afIndex++;
        if( afIndex < SPA_MAX_ALIEN_FIRE )
        {
            spaAlienFireActive[ afIndex ] = 1;
            spaAlienFireX[ afIndex ] = spaPositionNow * 8 + ( spawnBl - spaFirstAlien ) * 8 + 4;
            spaAlienFireY[ afIndex ] = ( spaLastActiveRow + spaAlienRow + 1 ) * 8;
        }
    }

    // Mothership redraw/move
    if( spaMotherCounter >= 3 )
    {
        spaMotherCounter = 0;
        if( spaMothership )
        {
            spaMothershipX--;
            if( spaMothershipX == 0 ) spaMothership = false;
        }
    }

    // Alien movement
    if( spaAlienCounter >= ( 92 - ( spaLevel - 1 ) * 5 ) )
    {
        spaAlienCounter = 0;
        if( spaAlienDirection )
        {
            if( spaPositionNow >= 6 + ( 8 - ( spaLastAlien - spaFirstAlien ) ) )
            {
                spaAlienDirection = false;
                spaAlienRow++;
            }
            else
              spaPositionNow++;
        }
        else
        {
            if( spaPositionNow <= 0 )
            {
                spaAlienDirection = true;
                spaAlienRow++;
            }
            else
              spaPositionNow--;
        }
    }

    // Fire! Upstream's own `fire` flag (set by a real interrupt on the
    // physical button, cleared only when a shot resolves - never on
    // button release) and `playerFire[0]` move in lockstep for a shot's
    // entire flight: both are set together when a new shot starts, both
    // are cleared together on resolution, and nothing else ever touches
    // either in between. So upstream's own `if(fire==1){...}` gate around
    // the mothership/alien collision checks (a few lines below) is
    // functionally "is a shot currently in flight", not "is the button
    // still physically held" - a held-vs-released distinction that
    // doesn't actually matter here, but matters a lot for a *level* read
    // like `isFirePressed()`: gating the collision check on the live
    // button state (an earlier draft of this port's own mistake) would
    // freeze an in-flight shot in place, un-checked, the instant the
    // player released Fire before it resolved. Fixed by using
    // `spaPlayerFireActive` (updated by the resolve step immediately
    // above) for that gate instead - the live button read is only ever
    // used to decide whether to *start* a new shot.
    if( isFirePressed() && !spaPlayerFireActive )
    {
        spaPlayerFireActive = true;
        spaPlayerFireX = spaPlayer + SPA_PLATFORM_WIDTH / 2;
        spaPlayerFireY = 56;
    }

    if( spaFireCounter >= 2 )
    {
        spaFireCounter = 0;

        if( spaPlayerFireActive )
        {
            if( spaPlayerFireY == 0 )
              spaPlayerFireActive = false;
            else
              spaPlayerFireY--;
        }

        spaLeftLimit = spaPositionNow * 8;
        if( spaPlayerFireActive )
        {
            spaFireXidx = spaFirstAlien + ( spaPlayerFireX - spaPositionNow * 8 ) / 8;
            spaFireYidx = spaPlayerFireY / 8 - spaAlienRow;

            if( spaMothership && spaPlayerFireX >= spaMothershipX && spaPlayerFireX <= spaMothershipX + 8 && spaPlayerFireY <= 8 )
            {
                int scm = 1 + arand( 99 );
                if( scm < 30 ) spaScore += 50;
                else if( scm < 60 ) spaScore += 100;
                else if( scm < 90 ) spaScore += 150;
                else spaScore += 300;

                spaStartNoteSeq( spaMothershipNotes, SPA_MOTHERSHIP_COUNT );
                spaMothership = false;
            }

            if( spaPlayerFireX >= spaLeftLimit && spaFireYidx >= 0 && spaFireYidx <= spaLastActiveRow && spaFireXidx >= 0 && spaFireXidx < 9 )
            {
                if( spaRow[ spaFireYidx ][ spaFireXidx ] == 1 )
                {
                    // upstream captures lastActiveRow here (as
                    // lastActiveToClear) purely to know which real hardware
                    // rows still need an explicit VRAM-erase call
                    // (clearAlienArea) after lastActiveRow itself is
                    // potentially narrowed below - this port's own query-
                    // based render model recomputes every row from live
                    // state each frame, so no equivalent erase step (or the
                    // variable that would drive it) is needed at all.
                    if( spaFireYidx == 2 ) spaDeadOn2++;
                    if( spaFireYidx == 1 ) spaDeadOn1++;

                    if( spaDeadOn2 == 5 ) spaLastActiveRow = 1;
                    if( spaDeadOn1 == 4 && spaDeadOn2 == 5 ) spaLastActiveRow = 0;

                    spaScore = spaScore + ( 3 - spaFireYidx ) * 10;
                    spaAliensDead++;
                    spaBeepOnce( 30, 100 );

                    spaPlayerFireActive = false;
                    spaPlayerFireY = 7;
                    spaRow[ spaFireYidx ][ spaFireXidx ] = 0;

                    if( spaFireXidx == spaFirstAlien )
                    {
                        int newFirst = spaFirstAlien;
                        int xi;
                        for( xi = spaLastAlien; xi >= spaFirstAlien; xi-- )
                        {
                            if( spaRow[0][xi] == 1 || spaRow[1][xi] == 1 || spaRow[2][xi] == 1 ) newFirst = xi;
                        }
                        spaPositionNow += newFirst - spaFirstAlien;
                        spaFirstAlien = newFirst;
                    }

                    if( spaFireXidx == spaLastAlien )
                    {
                        int newLast = spaLastAlien;
                        int xi;
                        for( xi = spaFirstAlien; xi <= spaLastAlien; xi++ )
                        {
                            if( spaRow[0][xi] == 1 || spaRow[1][xi] == 1 || spaRow[2][xi] == 1 ) newLast = xi;
                        }
                        spaLastAlien = newLast;
                    }
                }
            }
        }

        int afIndex;
        for( afIndex = 0; afIndex < SPA_MAX_ALIEN_FIRE; afIndex++ )
        {
            if( spaAlienFireActive[ afIndex ] == 1 )
            {
                spaAlienFireY[ afIndex ] = spaAlienFireY[ afIndex ] + 1;
                if( spaAlienFireY[ afIndex ] >= 56 )
                {
                    spaAlienFireActive[ afIndex ] = 0;
                    if( spaAlienFireX[ afIndex ] > spaPlayer && spaAlienFireX[ afIndex ] < spaPlayer + SPA_PLATFORM_WIDTH )
                      return true;
                }
            }
        }
    }

    if( spaAliensDead == 14 )
    {
        int ai;
        for( ai = 0; ai < SPA_MAX_ALIEN_FIRE; ai++ ) spaAlienFireActive[ ai ] = 0;

        spaLevel++;
        if( spaLevel > SPA_MAX_LEVEL ) spaLevel = SPA_MAX_LEVEL;
        spaResetAliens();
        spaBeginLevel();
        spaState = SPA_STATE_LEVELUP;
        spaWaitFrames = 42;
        return false;
    }

    if( ( spaAlienRow == 5 && spaLastActiveRow == 2 ) || ( spaAlienRow == 6 && spaLastActiveRow == 1 ) || ( spaAlienRow == 7 && spaLastActiveRow == 0 ) )
      return true;

    return false;
}

void gameSpaceAttack_init()
{
    spaMute = false;
    spaTop = 0;
    spaSeqActive = 0;
    spaBeginAttract();
}

void gameSpaceAttack_forceRedraw()
{
    if( spaState == SPA_STATE_ATTRACT ) spaRenderFrame( SPA_MODE_ATTRACT );
    else if( spaState == SPA_STATE_PLAYING ) spaRenderFrame( SPA_MODE_PLAYING );
    else if( spaState == SPA_STATE_LEVELUP ) spaRenderFrame( SPA_MODE_LEVELUP );
    else if( spaState == SPA_STATE_GAMEOVER_WAIT ) spaRenderFrame( SPA_MODE_GAMEOVER );
    else spaRenderFrame( SPA_MODE_NEWHIGH );
}

void gameSpaceAttack_update()
{
    spaAdvanceNoteSeq();

    if( spaState == SPA_STATE_ATTRACT )
    {
        int fireDown = isFirePressed();
        if( fireDown )
        {
            spaFireHoldTicks++;
            if( spaFireHoldTicks >= 120 && !spaMuteActionDone )
            {
                spaMuteActionDone = 1;
                if( spaMute == false ) spaMute = true; else spaMute = false;
            }
        }
        else
        {
            if( spaFireHeld && !spaMuteActionDone )
            {
                spaFireHeld = fireDown;
                spaBeginGame();
                spaRenderFrame( SPA_MODE_LEVELUP );
                return;
            }
            spaFireHoldTicks = 0;
            spaMuteActionDone = 0;
        }
        spaFireHeld = fireDown;
        spaRenderFrame( SPA_MODE_ATTRACT );
    }
    else if( spaState == SPA_STATE_LEVELUP )
    {
        if( spaSeqActive ) return;
        if( spaWaitFrames > 0 ) { spaWaitFrames--; return; }

        spaState = SPA_STATE_PLAYING;
        spaRenderFrame( SPA_MODE_PLAYING );
    }
    else if( spaState == SPA_STATE_PLAYING )
    {
        bool hit = spaPlayingTick();
        if( hit ) { spaEndGame(); return; }
        if( spaState == SPA_STATE_PLAYING ) spaRenderFrame( SPA_MODE_PLAYING );
    }
    else if( spaState == SPA_STATE_GAMEOVER_WAIT )
    {
        if( spaSeqActive ) return;
        if( spaWaitFrames > 0 ) { spaWaitFrames--; return; }

        if( spaNewHigh ) spaBeginNewHighWait();
        else spaBeginAttract();
    }
    else if( spaState == SPA_STATE_NEWHIGH_WAIT )
    {
        if( spaSeqActive ) return;
        if( spaWaitFrames > 0 ) { spaWaitFrames--; return; }

        spaBeginAttract();
    }
}

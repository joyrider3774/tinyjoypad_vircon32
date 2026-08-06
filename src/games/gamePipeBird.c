// =============================================================================
// Pipe Bird (Ioannis Lampropoulos, github.com/Lampropoulosss - the repo's
// own commit author, since no name is stated anywhere in the source itself
// or in a README/LICENSE file; no license is stated either, same "known
// author, unstated license" situation as several other ports in this
// project - see this file's own Licensing note in CLAUDE.md). From
// `more games/attiny85-flappy-bird/` - staged during the "very very deep
// scan" search batch, specifically flagged there as needing a side-by-side
// mechanic check against this project's already-shipped `gameFlappyBird.c`
// before porting: confirmed genuinely different, not a duplicate - this is
// a true continuous-position pipe-gap flyer (real gravity/velocity physics,
// hold-any-button-to-flap, a smoothly scrolling pipe), much closer to the
// original mobile game's own feel, versus the already-shipped port's
// discrete one-row-per-press stepping avoider with no gravity at all.
// Named "Pipe Bird" (not "Flappy Bird 2" or similar) specifically to avoid
// colliding with the already-shipped game's own menu title - the repo
// itself has no title screen/branding of its own to draw a name from.
//
// A plain AVR-GCC/Makefile project (not an Arduino `.ino` sketch) with its
// own minimal, from-scratch SSD1306 driver (`ssd1306.c`/`.h`) - the same
// toolchain shape already proven portable for Meteor Storm (the actual
// *build* toolchain never matters for the port itself, since the game
// logic is plain, portable C either way - only building upstream needs
// AVR-GCC specifically). `oled_show_page(page, buffer[128])` streams one
// full 128-byte page at a time, the exact same "one byte per (column,
// page)" model `md_drawColumn()` already handles - no new shim primitives
// needed.
//
// Not `tinyJoypadShim`/`obonoCoreShim` lineage - genuine bespoke hardware,
// a single-analog-pin (ADC3) voltage ladder decoding 4 discrete buttons
// (Up/Down/Left/Right, no separate Fire pin at all - confirmed by grep,
// there is no other digital-pin button read anywhere in the source).
// Upstream's own flap gesture is "ANY of the 4 buttons, newly pressed" -
// `read_buttons() != BUTTON_NONE && != last_button` doesn't care which
// specific button fired, only that one just transitioned from released to
// pressed. Ported as an edge-detected OR of
// `isUpPressed()`/`isDownPressed()`/`isLeftPressed()`/`isRightPressed()`,
// faithfully reproducing "any button flaps" rather than picking one
// arbitrary direction. `isFirePressed()` (unused by native gameplay, since
// this hardware has no such button) is used only for this port's own
// added attract-screen "press to start" gesture and the game-over-to-
// attract transition, matching every other beyond-scope port's own
// standing convention (Astro Barrier/ATtiny Snake/Meteor Storm/Flappy
// Bird) of adding a genuine attract screen where upstream has none at all
// (this game starts flying immediately at power-on).
//
// **Bird rendering is a genuine sub-page, arbitrary-pixel-Y sprite** (not
// page-aligned) - `bird_y` is a real 16.4 fixed-point value, and upstream's
// own render code already explicitly widens each 16-bit sprite column into
// a 32-bit temporary before shifting it by the sub-page offset, masking
// each of the 3 possible output bytes with an explicit `& 0xFF` - the
// exact byte-truncation-avoidance technique this whole project's history
// already established as necessary, just already present in the *original*
// AVR source this time (the author explicitly widened to `uint32_t` and
// commented "so no data falls off the edge") rather than something this
// port needed to add. Ported directly, unchanged in shape.
//
// **A genuine unsigned-integer-wraparound-reliance bug, the same bug
// family as this project's own documented int8-overflow/shift-wraparound/
// signed-sentinel history, just via a fourth mechanism (relying on a real
// `uint16_t`'s hardware wraparound-on-decrement instead of a narrower
// type's own overflow)**: `pipe_x` is declared `uint16_t` upstream, and
// `pipe_x -= 4 + (difficulty>>1);` is allowed to underflow below 0 - real
// unsigned hardware wraps that straight to a huge positive value (~65500),
// which the very next line's own `if (pipe_x > 128<<2) { pipe_x = 128<<2;
// score++; ... }` check relies on to detect "the pipe just went past the
// left edge, respawn it" (a wrapped value is always well above the 512
// threshold, given the decrement per frame is at most 11). `avrCompat.h`
// aliases `uint16_t` to a plain, non-wrapping 32-bit `int` here - a
// negative `pipbPipeX` just stays negative instead of wrapping, so the
// `> 512` reset trigger would never fire again once it happened, freezing
// the pipe off-screen forever and permanently halting scoring. **Fixed**
// by replacing the wraparound-reliant `> 128<<2` check with a direct
// `< 0` sign check instead - mathematically equivalent to "the moment the
// pipe's real position would go negative" without depending on unsigned
// overflow to detect it, avoiding the whole hazard rather than working
// around it (the same "don't trust an AVR-implicit behavior, reimplement
// the intended effect directly" resolution this project's other bugs in
// this family have used).
//
// **A second, smaller instance of the same bug family**: `prng_state` is
// `uint8_t` upstream, and `prng_state = (prng_state * 17) + 53;` is a
// classic 8-bit LCG relying on real hardware wraparound to stay bounded in
// [0,255] - harmless left unwrapped here (the `% validPositions` modulo
// still produces a valid, in-range result for any non-negative dividend,
// so gameplay wouldn't actually break), but would let the value grow
// unboundedly across a long session and eventually risk overflowing even
// a 32-bit `int`. **Fixed** with an explicit `& 0xFF` mask after each
// update, matching upstream's real intended range exactly. The real-
// hardware-timer entropy injection on flap (`prng_state ^= TCNT0;`, no
// Vircon32 equivalent) was replaced with `arand(256)`, the same "swap a
// hardware-timer-register entropy source for a call into the shared RNG"
// treatment already used elsewhere in this project.
//
// **`(bird_y >> 4) < 0` - a genuine logical-vs-arithmetic-right-shift
// hazard, the same bug class already found in HollowSeeker/Tiny Pipe/
// TinY Fi**, caught by inspection before ever compiling rather than by a
// report: `bird_y` can go genuinely negative for exactly the one frame a
// ceiling collision is detected (a fast, repeated flap can push it just
// past 0). AVR-GCC's `int16_t >> 4` sign-extends (arithmetic), correctly
// staying negative; Vircon32's own `>>` is documented as *logical* (zero-
// fill), which would turn a small negative `bird_y` into a huge positive
// shifted result, making the `< 0` check silently fail to register the
// ceiling collision. **Fixed** by testing `pipbBirdY < 0` directly instead
// of shifting first - mathematically equivalent for a pure sign check
// (floor division by a positive power of two never changes the sign),
// and avoids the shift-semantics question for this specific comparison
// entirely rather than needing a branch-on-sign helper. Confirmed this is
// the *only* site where a negative `bird_y` can ever reach a shift: the
// very next check that also shifts `bird_y` (`(bird_y>>4)+16 > 63`) only
// matters when `bird_y` is already known non-negative at that point
// (short-circuited after the `<0` branch), and rendering never runs
// against a negative `bird_y` at all (the ceiling check sets game-over the
// same tick, before the next render call, so the bird sprite's own
// sub-page compositing - which does its own separate, always-safe shift -
// never sees a negative value).
//
// EEPROM high-score persistence restored (see the project-wide "Real
// persistent high-score saving" section in CLAUDE.md - a single byte at
// address 0, matching upstream exactly). `tiny_font`/`bird_bitmap` were
// byte-diff-verified against upstream via a small Python script before
// ever being pasted in; the standard 95-char `ssd1306xled` font (already
// proven for Oroboros/Run Dude Run/Dino Game/Astro Barrier/ATtiny Snake/
// Flappy Bird) was reused for this port's own added attract screen, since
// upstream's own tiny 5-column font only covers digits plus the specific
// letters its own "GAME OVER"/"SCORE"/"HIGH" screen needs.
//
// **Genuine hardware-timer-driven ~30fps, not the "no timing model
// whatsoever upstream" category** - a real miss at initial port time,
// caught and fixed via a direct user request right after shipping rather
// than proactively: `main.c`'s own `ISR(TIMER0_OVF_vect)` is explicitly
// commented "automatically triggered ~30 times a second by the hardware",
// gating `frame_ready` (and therefore every real `update_physics()`/
// `render_frame()` call) to that genuine rate - unlike the several other
// beyond-scope ports in this project whose own upstream truly has no
// timing model at all. Shipping this port at the engine's native 60fps
// ran gravity/velocity/pipe-speed exactly 2x faster than the original
// hardware. **Fixed** with `PIPB_TICK_DIVISOR=2`, a whole-function tick-
// skip gating the entire `gamePipeBird_update()` body (including the
// attract/game-over screens, matching the majority "gate the whole tick"
// precedent in this project) rather than a movement-only split, since
// there were no pre-existing 60fps-tuned wait constants in this port that
// would need to stay unrescaled.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data
// -----------------------------------------------------------------------------

#define PIPB_WIDTH 128
#define PIPB_PIPE_WIDTH 12
#define PIPB_BIRD_WIDTH 16
#define PIPB_BIRD_X 16

// Byte-diff-verified against upstream's own bird_bitmap[BIRD_WIDTH].
int[16] pipbBirdBitmap =
{
    0x0300, 0x0300,
    0x0FC0, 0x0FC0,
    0x3FF0, 0x3FF0,
    0x3FFC, 0x3FFC,
    0x0F3C, 0x0F3C,
    0x0FFC, 0x0FFC,
    0x03F0, 0x03F0,
    0x03C0, 0x03C0
};

// Byte-diff-verified against upstream's own tiny_font[][5] - digits 0-9,
// then SCORE letters (S,C,O,R,E), then GAME OVER's own extra letters
// (G,A,M,V), then HIGH's own extra letters (H,I).
int[21][5] pipbTinyFont =
{
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, // 0
    { 0x00, 0x42, 0x7F, 0x40, 0x00 }, // 1
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, // 2
    { 0x21, 0x41, 0x45, 0x4B, 0x31 }, // 3
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, // 4
    { 0x27, 0x45, 0x45, 0x45, 0x39 }, // 5
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, // 6
    { 0x01, 0x71, 0x09, 0x05, 0x03 }, // 7
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, // 8
    { 0x06, 0x49, 0x49, 0x29, 0x1E }, // 9
    { 0x46, 0x49, 0x49, 0x49, 0x31 }, // S (10)
    { 0x3E, 0x41, 0x41, 0x41, 0x22 }, // C (11)
    { 0x3E, 0x41, 0x41, 0x41, 0x3E }, // O (12)
    { 0x7F, 0x09, 0x19, 0x29, 0x46 }, // R (13)
    { 0x7F, 0x49, 0x49, 0x49, 0x41 }, // E (14)
    { 0x3E, 0x41, 0x49, 0x49, 0x3A }, // G (15)
    { 0x7E, 0x11, 0x11, 0x11, 0x7E }, // A (16)
    { 0x7F, 0x02, 0x04, 0x02, 0x7F }, // M (17)
    { 0x1F, 0x20, 0x40, 0x20, 0x1F }, // V (18)
    { 0x7F, 0x08, 0x08, 0x08, 0x7F }, // H (19)
    { 0x00, 0x41, 0x7F, 0x41, 0x00 }  // I (20)
};

// Standard 95-char 6x8 ssd1306xled font, already proven for Oroboros/Run
// Dude Run/Dino Game/Astro Barrier/ATtiny Snake/Flappy Bird - reused
// verbatim, each game keeps its own self-contained copy per this
// project's convention. Used only for this port's own added attract
// screen, not for the upstream game-over screen (which uses the tiny
// font above, matching upstream's own real screen content).
int[570] pipbFont =
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

int pipbFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return pipbFont[ ( ch - 32 ) * 6 + col ];
}

int pipbTextByteAt( int* text, int textLen, int startX, int x )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= textLen ) return 0;
    return pipbFontByte( text[ charIdx ], rel % 6 );
}

// -----------------------------------------------------------------------------
//   Physics state (matching upstream's own fixed-point scales: bird_y is
//   16.4, pipe_x is 16.2)
// -----------------------------------------------------------------------------

int pipbBirdY;
int pipbBirdVelocity;
int pipbPipeX;
int pipbPipeGapY;
int pipbGapSize;
int pipbScore;
int pipbHighScore;
int pipbDifficulty;
int pipbPrngState;

bool pipbAnyPrev;

void pipbInitGame()
{
    pipbBirdY = 16 << 4;
    pipbBirdVelocity = 0;
    pipbPipeX = 128 << 2;
    pipbPipeGapY = 3;
    pipbGapSize = 5;
    pipbScore = 0;
    pipbDifficulty = 0;
    pipbPrngState = 42;
    pipbAnyPrev = false;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int[128] pipbPageBuffer;

void pipbClearBuffer()
{
    int i;
    for( i = 0; i < 128; i++ ) pipbPageBuffer[ i ] = 0;
}

// Composites one physical page - direct translation of render_frame()'s
// own playing-state branch (bird sprite sub-page shift-and-mask, then the
// pipe's solid-fill columns when this page isn't part of the gap). Both
// layers use OR (upstream's pipe fill is a plain assignment, but ORing
// with the all-1s 0xFF fill byte is bit-identical to assigning it
// regardless of what the bird layer already wrote there, so this is a
// strictly safer no-behavior-change default, matching this project's own
// established "OR-compositing as a defensive default" lesson).
void pipbComposeRow( int page )
{
    pipbClearBuffer();

    int realY = pipbBirdY >> 4;
    int birdPage = realY >> 3;
    int shift = realY & 7;

    int i;
    for( i = 0; i < 16; i++ )
    {
        int shifted = pipbBirdBitmap[ i ] << shift;
        int x = PIPB_BIRD_X + i;

        if( page == birdPage )
          pipbPageBuffer[ x ] = pipbPageBuffer[ x ] | ( shifted & 0xFF );
        else if( page == birdPage + 1 )
          pipbPageBuffer[ x ] = pipbPageBuffer[ x ] | ( ( shifted >> 8 ) & 0xFF );
        else if( page == birdPage + 2 )
          pipbPageBuffer[ x ] = pipbPageBuffer[ x ] | ( ( shifted >> 16 ) & 0xFF );
    }

    int realPipeX = pipbPipeX >> 2;
    bool inGap = ( page >= pipbPipeGapY ) && ( page < ( pipbPipeGapY + pipbGapSize ) );

    if( !inGap )
    {
        for( i = 0; i < PIPB_PIPE_WIDTH; i++ )
        {
            int drawX = realPipeX + i;
            if( drawX >= 0 && drawX < 128 )
              pipbPageBuffer[ drawX ] = pipbPageBuffer[ drawX ] | 0xFF;
        }
    }
}

void pipbDrawLetter( int letterIndex, int startX )
{
    int i;
    for( i = 0; i < 5; i++ )
    {
        int x = startX + i;
        if( x < 128 )
          pipbPageBuffer[ x ] = pipbPageBuffer[ x ] | pipbTinyFont[ letterIndex ][ i ];
    }
}

void pipbDrawNumber( int startX, int number )
{
    int hundreds = number / 100;
    int tens = ( number / 10 ) % 10;
    int ones = number % 10;
    int x = startX;

    if( hundreds > 0 )
    {
        pipbDrawLetter( hundreds, x );
        x = x + 6;
    }
    if( hundreds > 0 || tens > 0 )
    {
        pipbDrawLetter( tens, x );
        x = x + 6;
    }
    pipbDrawLetter( ones, x );
}

// Direct translation of render_frame()'s own game_over branch.
void pipbComposeGameOverRow( int page )
{
    pipbClearBuffer();

    if( page == 2 )
    {
        pipbDrawLetter( 15, 30 ); // G
        pipbDrawLetter( 16, 36 ); // A
        pipbDrawLetter( 17, 42 ); // M
        pipbDrawLetter( 14, 48 ); // E
    }
    if( page == 3 )
    {
        pipbDrawLetter( 12, 30 ); // O
        pipbDrawLetter( 18, 36 ); // V
        pipbDrawLetter( 14, 42 ); // E
        pipbDrawLetter( 13, 48 ); // R
    }
    if( page == 5 )
    {
        pipbDrawLetter( 10, 10 ); // S
        pipbDrawLetter( 11, 16 ); // C
        pipbDrawLetter( 12, 22 ); // O
        pipbDrawLetter( 13, 28 ); // R
        pipbDrawLetter( 14, 34 ); // E
        pipbDrawNumber( 46, pipbScore );
    }
    if( page == 6 )
    {
        pipbDrawLetter( 19, 10 ); // H
        pipbDrawLetter( 20, 16 ); // I
        pipbDrawLetter( 15, 22 ); // G
        pipbDrawLetter( 19, 28 ); // H
        pipbDrawNumber( 46, pipbHighScore );
    }
}

#define PIPB_MODE_ATTRACT 0
#define PIPB_MODE_PLAYING 1
#define PIPB_MODE_GAMEOVER 2

void pipbRenderFrame( int mode )
{
    md_beginFrame();
    int col, page;
    for( page = 0; page < 8; page++ )
    {
        if( mode == PIPB_MODE_GAMEOVER )
        {
            pipbComposeGameOverRow( page );
            for( col = 0; col < 128; col++ )
              md_drawColumn( col, page, pipbPageBuffer[ col ] );
            continue;
        }

        if( mode == PIPB_MODE_ATTRACT )
        {
            pipbClearBuffer();

            if( page == 2 )
              for( col = 0; col < 16; col++ )
                pipbPageBuffer[ 56 + col ] = pipbBirdBitmap[ col ] & 0xFF;
            if( page == 3 )
              for( col = 0; col < 16; col++ )
                pipbPageBuffer[ 56 + col ] = ( pipbBirdBitmap[ col ] >> 8 ) & 0xFF;

            if( page == 4 )
            {
                int* t = "PIPE BIRD";
                int col2;
                for( col2 = 0; col2 < 128; col2++ )
                  pipbPageBuffer[ col2 ] = pipbPageBuffer[ col2 ] | pipbTextByteAt( t, 9, 34, col2 );
            }
            if( page == 5 )
            {
                int* t = "BY I.LAMPROPOULOS";
                int col2;
                for( col2 = 0; col2 < 128; col2++ )
                  pipbPageBuffer[ col2 ] = pipbPageBuffer[ col2 ] | pipbTextByteAt( t, 17, 13, col2 );
            }
            if( page == 7 )
            {
                int* t = "PRESS FIRE";
                int col2;
                for( col2 = 0; col2 < 128; col2++ )
                  pipbPageBuffer[ col2 ] = pipbPageBuffer[ col2 ] | pipbTextByteAt( t, 10, 34, col2 );
            }

            for( col = 0; col < 128; col++ )
              md_drawColumn( col, page, pipbPageBuffer[ col ] );
            continue;
        }

        pipbComposeRow( page );
        for( col = 0; col < 128; col++ )
          md_drawColumn( col, page, pipbPageBuffer[ col ] );
    }
}

// -----------------------------------------------------------------------------
//   Physics / state machine (direct translation of update_physics(), split
//   across an attract/playing/gameover state machine per this project's
//   own standing convention for every beyond-scope port whose upstream
//   source has no attract screen or restart gesture of its own)
// -----------------------------------------------------------------------------

#define PIPB_STATE_ATTRACT  0
#define PIPB_STATE_PLAYING  1
#define PIPB_STATE_GAMEOVER 2

int pipbState;
bool pipbPrevFire;

void pipbBeginAttract()
{
    pipbPrevFire = false;
    pipbState = PIPB_STATE_ATTRACT;
}

void pipbBeginPlaying()
{
    pipbInitGame();
    pipbState = PIPB_STATE_PLAYING;
}

// Direct translation of update_physics()'s own game_over branch: only
// writes back to EEPROM on an actual new high score, matching upstream's
// own eeprom_update_byte((uint8_t*)0, high_score) call site exactly.
void pipbBeginGameOver()
{
    if( pipbScore > pipbHighScore )
    {
        pipbHighScore = pipbScore;
        eeprom_update_byte( 0, pipbHighScore );
    }
    pipbPrevFire = true; // arm against whatever press caused the collision
    pipbState = PIPB_STATE_GAMEOVER;
}

#define PIPB_TICK_DIVISOR 2
int pipbTickSkipCounter;

// Direct translation of upstream's own setup(): high_score =
// eeprom_read_byte((uint8_t*)0); if (high_score == 255) high_score = 0;
// - 255 is EEPROM's own real "never written" sentinel (see eepromShim.c's
// own header comment for why fresh cells default to 255, not 0).
void gamePipeBird_init()
{
    pipbHighScore = eeprom_read_byte( 0 );
    if( pipbHighScore == 255 ) pipbHighScore = 0;
    pipbTickSkipCounter = 0;
    pipbBeginAttract();
}

// No onResume needed - every state (ATTRACT/PLAYING/GAMEOVER) calls
// pipbRenderFrame() unconditionally on every real tick that isn't skipped
// by the tick-divisor below, and a skipped tick just leaves the previous
// frame's own pixels in place for one extra 60fps frame (self-correcting
// on the very next real tick) - confirmed correct, not an oversight,
// matching this project's own onResume audit convention for every game
// registered with a genuine reason either way.
void gamePipeBird_update()
{
    pipbTickSkipCounter++;
    if( pipbTickSkipCounter < PIPB_TICK_DIVISOR ) return;
    pipbTickSkipCounter = 0;

    if( pipbState == PIPB_STATE_ATTRACT )
    {
        bool fireNow = isFirePressed();
        if( fireNow && !pipbPrevFire )
        {
            pipbBeginPlaying();
            pipbRenderFrame( PIPB_MODE_PLAYING );
            return;
        }
        pipbPrevFire = fireNow;
        pipbRenderFrame( PIPB_MODE_ATTRACT );
        return;
    }

    if( pipbState == PIPB_STATE_GAMEOVER )
    {
        bool fireNow = isFirePressed();
        if( fireNow && !pipbPrevFire )
        {
            pipbBeginAttract();
            pipbRenderFrame( PIPB_MODE_ATTRACT );
            return;
        }
        pipbPrevFire = fireNow;
        pipbRenderFrame( PIPB_MODE_GAMEOVER );
        return;
    }

    // PIPB_STATE_PLAYING
    pipbBirdVelocity = pipbBirdVelocity + 4;

    bool anyNow = isUpPressed() || isDownPressed() || isLeftPressed() || isRightPressed();
    if( anyNow && !pipbAnyPrev )
    {
        pipbBirdVelocity = -32;
        pipbPrngState = ( pipbPrngState ^ arand( 256 ) ) & 0xFF;
    }
    pipbAnyPrev = anyNow;

    pipbBirdY = pipbBirdY + pipbBirdVelocity;

    bool outOfBounds = pipbBirdY < 0;
    if( !outOfBounds )
    {
        int realY = pipbBirdY >> 4;
        if( ( realY + 16 ) > 63 ) outOfBounds = true;
    }

    if( outOfBounds )
    {
        pipbBeginGameOver();
        pipbRenderFrame( PIPB_MODE_GAMEOVER );
        return;
    }

    pipbPipeX = pipbPipeX - ( 4 + ( pipbDifficulty >> 1 ) );

    if( pipbPipeX < 0 )
    {
        pipbPipeX = 128 << 2;
        pipbScore++;

        int newDifficulty = pipbScore >> 3;
        if( newDifficulty != pipbDifficulty ) pipbDifficulty = newDifficulty;

        int newGapSize = 5 - ( pipbDifficulty >> 2 );
        if( newGapSize > 3 ) pipbGapSize = newGapSize;
        else pipbGapSize = 3;

        pipbPrngState = ( ( pipbPrngState * 17 ) + 53 ) & 0xFF;

        int validPositions = 8 - pipbGapSize - 1;
        pipbPipeGapY = 1 + ( pipbPrngState % validPositions );
    }

    int realPipeX = pipbPipeX >> 2;
    if( ( realPipeX < ( 16 + PIPB_BIRD_WIDTH ) ) && ( ( realPipeX + PIPB_PIPE_WIDTH ) > 16 ) )
    {
        int realY = pipbBirdY >> 4;
        if( ( realY < ( pipbPipeGapY * 8 ) ) || ( ( realY + 16 ) > ( ( pipbPipeGapY + pipbGapSize ) * 8 ) ) )
        {
            pipbBeginGameOver();
            pipbRenderFrame( PIPB_MODE_GAMEOVER );
            return;
        }
    }

    pipbRenderFrame( PIPB_MODE_PLAYING );
}

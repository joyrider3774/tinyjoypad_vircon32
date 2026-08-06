// =============================================================================
// Meteor Storm (Albert Gonzalez, GitHub handle "theisolinearchip" - confirmed
// via the repo's own git commit author, not guessed from the handle alone;
// Unlicense/public domain) - the last of the
// 4 games staged during the wider ATtiny85/TinyJoypad search (see
// CLAUDE.md's own `more games/` catalog entry) - a single-button Flappy-
// Bird-style avoider: hold to rise, release to fall, dodge square
// "meteors" scrolling in from the right.
//
// **The outlier of the whole search batch, confirmed exactly as
// suspected at staging time**: a plain AVR-GCC/Makefile project (`main()`,
// no Arduino runtime at all), not an Arduino `.ino` sketch like every
// other game in this cartridge - and its own custom, hand-rolled minimal
// SSD1306 driver (`libs/ssd1306/ssd1306_attiny85.c`, the author's own
// from-scratch I2C command/data writer), not `ssd1306xled`/`tinyJoypadShim`/
// `obonoCoreShim`. Neither of those differences mattered for the actual
// port, though: `ssd1306_send_single_data()` still streams one real
// SSD1306 page/column byte at a time (confirmed by reading the driver
// directly), the exact same model `md_drawColumn()` already handles - and
// the *game logic* itself (`main.c`) is plain, portable C either way, the
// AVR-GCC-vs-Arduino distinction only ever mattered for *building* it, not
// for reading what it does. No sound of any kind exists in this game
// (confirmed by grep - no buzzer pin, no tone code anywhere), so this is
// also the first port in this whole project needing zero sound work.
//
// Genuinely single-button: `PIN_BUTTON_B`/`PIN_BUTTON_C` are configured as
// inputs with pull-ups but never actually read anywhere (confirmed dead by
// grep) - only `PIN_BUTTON_A` (hold to rise) is real. Mapped onto Fire.
//
// **Sub-page, non-page-aligned sprite positioning** (a first for the
// `gametiny`/AttinyArcade-lineage games in this project, though already
// proven for Falling Blocks'/Blocks Gold's own rotated engine and Run
// Dude Run's bomb trails) - `player_y`/each obstacle's own Y are arbitrary
// pixel values, not page-aligned, so `draw_player()`/`draw_obstacles()`
// both split their own sprite across up to 2 physical pages using real
// bit-shift math (`0x0F << offset` for the player, `0xFF << offset` for
// obstacles, plus a computed low-bit mask for whatever spills into the
// next page). Both shift expressions can overflow a real byte (e.g.
// `0x0F << 7 = 0x780`) - harmless on real AVR's implicit `char` (8-bit)
// truncation, a genuine byte-truncation bug on this dialect's plain,
// non-truncating `int`s (the very first bug class this whole project's
// own history documents) - fixed the same way as every prior instance:
// an explicit `& 0xFF` at each shift site (`metrPlayerColByte`/
// `metrObstacleColByte` below). Both Y values are also confirmed always
// non-negative before being used this way (`player_y` is explicitly
// clamped `>=0` in `move_player()`; every obstacle Y comes from
// `rand() % (SSD1306_HEIGHT - OBSTACLE_HEIGHT)`, never negative) - so,
// unlike a few other ports in this project, there's no logical-vs-
// arithmetic-right-shift hazard to guard against here at all.
//
// **Rendering uses real "last write wins" overwrite semantics, not OR-
// compositing** - a genuine difference from how most other ports in this
// project build a frame (`val |= ...` from every layer). Real SSD1306
// page-mode writes *replace* whatever was at that address, and upstream's
// own draw order (`clean_screen(); draw_borders(); draw_player();
// draw_obstacles(); draw_score(...);`) relies on that: the score digits
// are drawn *after* the border and genuinely overwrite it in their own
// column range (not blended with it), and an obstacle passing through the
// player's own screen position likewise overwrites the player's pixels
// there. `metrComposeRow()` below reproduces this by assigning (`=`), not
// OR-ing (`|=`), each layer's own contribution in that exact same order,
// rather than the OR-based compositing most other ports in this project
// use (which would have been a real, if subtle, behavioral difference
// here specifically).
//
// **One deliberate deviation, matching every other port from this same
// search batch**: upstream's own title screen just busy-waits once for
// the button to be *released* at boot (`while(is_button_pressed(...));`,
// to avoid an accidental instant-start), then checks the button as a
// plain level read every tick while `title==1` - no real "press to
// start" gate beyond that. Added a genuine edge-detected Fire press to
// start, matching the UX convention already established for Astro
// Barrier/ATtiny Snake (the other two ports from this same batch).
//
// Game-over's own real `ssd1306_send_single_command(SSD1306_INVERTDISPLAY)`
// (a genuine hardware inversion, held via `_delay_ms(1000)`) has no
// hardware equivalent here - reproduced by rendering the exact same frozen
// final-frame composite with every byte XORed against 0xFF for ~60 frames
// (`METR_STATE_GAMEOVER_FLASH`), then returning to the attract screen -
// the "blocking loop/delay -> resumable frame-counted state" treatment
// every port in this project needs, just applied to a real hardware
// inversion command instead of a software delay loop this time.
//
// `numbers[60]` (score digit font, 10 digits x 6 bytes) and `title_image`
// (1024 bytes, standard row-major page order) were byte-diff-verified via
// a small Python script against the upstream source (`ssd1306_attiny85_
// constants.h`) before ever being pasted in - the digit font's own header
// comment credits it as coming from Andy Jackson's own `font6x8AJ.h`
// (Bat Bonanza's own font family), already present in this project, but
// this game only ever uses the 10-digit subset, so the small subset
// table is kept standalone here rather than pulling in the full font.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data
// -----------------------------------------------------------------------------

// Score-digit font (10 digits x 6 bytes) - byte-diff-verified against
// upstream's own numbers[60] (itself credited there as sourced from Andy
// Jackson's font6x8AJ.h).
int[60] metrNumbers =
{
0x00, 0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
0x00, 0x00, 0x42, 0x7F, 0x40, 0x00, // 1
0x00, 0x42, 0x61, 0x51, 0x49, 0x46, // 2
0x00, 0x21, 0x41, 0x45, 0x4B, 0x31, // 3
0x00, 0x18, 0x14, 0x12, 0x7F, 0x10, // 4
0x00, 0x27, 0x45, 0x45, 0x45, 0x39, // 5
0x00, 0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
0x00, 0x01, 0x71, 0x09, 0x05, 0x03, // 7
0x00, 0x36, 0x49, 0x49, 0x49, 0x36, // 8
0x00, 0x06, 0x49, 0x49, 0x29, 0x1E, // 9
};

// Title screen (128x64, standard row-major page order).
int[1024] metrTitle =
{
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xfc, 0xe0, 0x00, 0x00, 0x00,
0x80, 0xc0, 0xf0, 0x7c, 0x1f, 0x1f, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xfc, 0x9c,
0x0e, 0x06, 0x06, 0x07, 0x07, 0x03, 0x03, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x08, 0x18, 0x18,
0x1c, 0x1c, 0x1c, 0x1c, 0x0c, 0x0c, 0x0e, 0x1e, 0xfe, 0xfe, 0xee, 0x06, 0x07, 0x07, 0x07, 0x07,
0x07, 0x07, 0x02, 0x00, 0x00, 0x00, 0xf8, 0xfc, 0x9c, 0x0e, 0x06, 0x06, 0x07, 0x07, 0x03, 0x03,
0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xf0, 0xf0, 0x38, 0x1c, 0x0c, 0x0e, 0x0e,
0x07, 0x07, 0x0f, 0x1f, 0xfe, 0xf8, 0xc0, 0x00, 0x00, 0x00, 0x1c, 0x1e, 0xff, 0xff, 0xe7, 0x07,
0x07, 0x07, 0x06, 0x06, 0xce, 0xee, 0x7e, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x01, 0x07, 0x0f, 0x0e, 0x0e,
0x07, 0x03, 0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff,
0x1c, 0x1c, 0x0c, 0x0c, 0x0c, 0x0e, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0x1c, 0x1c, 0x0c, 0x0c, 0x0c, 0x0e, 0x06,
0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xe7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x80, 0xf8, 0xff, 0x3f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x38, 0xff, 0xff, 0xff, 0x7c,
0x6c, 0xe6, 0xe6, 0xc3, 0xc3, 0x81, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x7f, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x1f, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x1f,
0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x0c, 0x0e, 0x0e, 0x07, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x3f, 0x3f, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x1f, 0x1c, 0x1c, 0x1c, 0x1c, 0x1c, 0x0c, 0x0e,
0x0e, 0x07, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x07, 0x0f, 0x1e, 0x1c, 0x1c, 0x1c, 0x1c,
0x0e, 0x0f, 0x07, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x1f, 0x1f, 0x00,
0x00, 0x00, 0x00, 0x01, 0x01, 0x03, 0x03, 0x07, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8,
0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0xfc, 0xfe, 0xcf, 0xc7, 0x87, 0x87, 0x87, 0x87, 0x1e,
0x3f, 0x1e, 0x00, 0x00, 0x00, 0x7f, 0x7f, 0x3f, 0x07, 0x07, 0x07, 0xff, 0xff, 0x07, 0x07, 0x07,
0x3f, 0x7f, 0x7f, 0x00, 0x00, 0xf0, 0xf8, 0x3c, 0x0e, 0x0f, 0x07, 0x07, 0x07, 0x07, 0x0e, 0x0e,
0x3c, 0xf8, 0xf0, 0x00, 0x02, 0x07, 0x07, 0xff, 0xff, 0x87, 0x87, 0x87, 0x87, 0x87, 0xcf, 0xfe,
0xfc, 0x78, 0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0x7f, 0xfc, 0xe0, 0x80, 0x80, 0xe0, 0xf8, 0x7f,
0xff, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xf8, 0x70, 0xe1, 0xe1, 0xe1, 0xe1, 0xe3, 0xe3, 0x77,
0x7f, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xe0, 0xe0, 0xe0, 0xff, 0xff, 0xe0, 0xe0, 0xe0,
0x40, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x1f, 0x3c, 0x70, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0xe0, 0x70,
0x3c, 0x1f, 0x0f, 0x00, 0x40, 0xe0, 0xe0, 0xff, 0xff, 0xe3, 0xe3, 0xc3, 0x03, 0x07, 0x1f, 0x3d,
0xf8, 0xf0, 0xe0, 0xe0, 0xe0, 0xff, 0xff, 0xff, 0xe0, 0xe1, 0x07, 0x0f, 0x0f, 0x0f, 0xc3, 0xe0,
0xff, 0xff, 0xff, 0xe0, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

#define METR_WIDTH  128
#define METR_HEIGHT 64

#define METR_PLAYER_X 25 // SSD1306_WIDTH/5, integer division - fixed, never moves
#define METR_PLAYER_WIDTH 8
#define METR_PLAYER_HEIGHT 4
#define METR_PLAYER_INITIAL_Y 40

#define METR_OBSTACLE_WIDTH 8
#define METR_OBSTACLE_HEIGHT 8
#define METR_OBSTACLE_SPEED 3
#define METR_MAX_OBSTACLES 6

int metrPlayerY;
int metrPlayerAcc;
bool metrButtonPressedLastStep;

int[6] metrObstacleX;
int[6] metrObstacleY;

int metrScore;
int metrMaxScore;

// -----------------------------------------------------------------------------
//   Game logic (direct translation of check_movement/move_player/
//   move_obstacles/check_collisions/init_game)
// -----------------------------------------------------------------------------

void metrCheckMovement()
{
    bool pressed = isFirePressed();
    if( pressed )
    {
        if( metrButtonPressedLastStep )
        {
            metrButtonPressedLastStep = false;
            metrPlayerAcc = 0;
        }
        else
        {
            metrPlayerAcc--;
            if( metrPlayerAcc < -5 ) metrPlayerAcc = -5;
        }
    }
    else
    {
        if( metrButtonPressedLastStep )
        {
            metrPlayerAcc++;
            if( metrPlayerAcc > 5 ) metrPlayerAcc = 5;
        }
        else
        {
            metrPlayerAcc = 0;
            metrButtonPressedLastStep = true;
        }
    }
}

void metrMovePlayer()
{
    metrPlayerY = metrPlayerY + metrPlayerAcc / 2;
    if( metrPlayerY < 0 ) metrPlayerY = 0;
    else if( metrPlayerY + METR_PLAYER_HEIGHT >= METR_HEIGHT ) metrPlayerY = METR_HEIGHT - METR_PLAYER_HEIGHT;
}

bool metrCheckCollisions()
{
    if( metrPlayerY <= 0 || metrPlayerY + METR_PLAYER_HEIGHT >= METR_HEIGHT ) return true;

    int i;
    for( i = 0; i < METR_MAX_OBSTACLES; i++ )
    {
        int ox = metrObstacleX[ i ];
        int oy = metrObstacleY[ i ];
        bool xOverlap = ( ox >= METR_PLAYER_X && ox <= METR_PLAYER_X + METR_PLAYER_WIDTH )
                      || ( ox + METR_OBSTACLE_WIDTH >= METR_PLAYER_X && ox + METR_OBSTACLE_WIDTH <= METR_PLAYER_X + METR_PLAYER_WIDTH );
        bool yOverlap = ( oy >= metrPlayerY && oy <= metrPlayerY + METR_PLAYER_HEIGHT )
                      || ( oy + METR_OBSTACLE_HEIGHT >= metrPlayerY && oy + METR_OBSTACLE_HEIGHT <= metrPlayerY + METR_PLAYER_HEIGHT );
        if( xOverlap && yOverlap ) return true;
    }
    return false;
}

void metrMoveObstacles()
{
    int i;
    for( i = 0; i < METR_MAX_OBSTACLES; i++ )
    {
        metrObstacleX[ i ] = metrObstacleX[ i ] - METR_OBSTACLE_SPEED;
        if( metrObstacleX[ i ] < 0 )
        {
            metrObstacleX[ i ] = METR_WIDTH + arand( 50 );
            metrObstacleY[ i ] = arand( METR_HEIGHT - METR_OBSTACLE_HEIGHT );
            metrScore++;
        }
    }
}

void metrInitGame()
{
    if( metrScore > metrMaxScore ) metrMaxScore = metrScore;
    metrScore = 0;

    metrPlayerY = METR_PLAYER_INITIAL_Y;
    metrPlayerAcc = 0;
    metrButtonPressedLastStep = false;

    int i;
    for( i = 0; i < METR_MAX_OBSTACLES; i++ )
    {
        metrObstacleX[ i ] = METR_WIDTH + arand( 50 ) + 10 * ( i * 2 );
        metrObstacleY[ i ] = arand( METR_HEIGHT - METR_OBSTACLE_HEIGHT );
    }
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

// Player is 4px tall, arbitrary (non-page-aligned) Y - splits across up to
// 2 physical pages via real bit-shift math, explicitly masked to a real
// byte (see this file's own header comment on why - the shift can
// otherwise overflow a real byte on this dialect's non-truncating ints).
int metrPlayerColByte( int page )
{
    int basePage = metrPlayerY / 8;
    int offset = metrPlayerY % 8;
    if( page == basePage ) return ( 0x0F << offset ) & 0xFF;
    int r = METR_PLAYER_HEIGHT - ( 8 - offset );
    if( r > 0 && page == basePage + 1 ) return ( 1 << r ) - 1;
    return 0;
}

int metrObstacleColByte( int obsY, int page )
{
    int basePage = obsY / 8;
    int offset = obsY % 8;
    if( page == basePage ) return ( 0xFF << offset ) & 0xFF;
    int r = METR_OBSTACLE_HEIGHT - ( 8 - offset );
    if( r > 0 && page == basePage + 1 ) return ( 1 << r ) - 1;
    return 0;
}

// Right-aligned multi-digit score rendering (least-significant digit
// rightmost, matching upstream's own do-while digit-splitting loop).
int metrScoreDigitCount( int value )
{
    int count = 1;
    int tmp = value / 10;
    while( tmp > 0 ) { count++; tmp = tmp / 10; }
    return count;
}

int[128] metrPageBuffer;

// Writes value's own right-aligned digits directly into metrPageBuffer,
// looping over DIGITS (not columns) as the outer loop - each digit's own
// decimal value is computed once and its 6 columns filled directly,
// rather than the column-outer version this replaced (which recomputed
// the same digit's value via repeated division on every one of its own
// 6 columns) - the same "hoist what's constant across a small inner
// span instead of recomputing it per pixel" lesson already applied
// elsewhere in this project (Tiny Doc/DDug's own row-scoped caching,
// TinY Fi's per-row sprite-constant hoisting).
void metrDrawScoreIntoBuffer( int value, int digitCount )
{
    int rightmostStart = METR_WIDTH - 6;
    int dv = value;
    int d;
    for( d = 0; d < digitCount; d++ )
    {
        int digit = dv % 10;
        dv = dv / 10;
        int start = rightmostStart - d * 6;
        int col;
        for( col = start; col < start + 6; col++ )
          if( col >= 0 && col < 128 ) metrPageBuffer[ col ] = metrNumbers[ digit * 6 + ( col - start ) ];
    }
}

// Composited with OR (`|=`), not upstream's own real "last write wins"
// full-byte replace (`=`) - a deliberate departure from upstream, found
// and confirmed necessary via direct user testing, not a bug ported over
// by accident. Upstream's real SSD1306 page-mode writes genuinely replace
// a whole byte at once, and since each of this game's own layers only
// ever occupies a THIN sub-range of a page's 8 real pixel rows (the
// player is 4px tall, an obstacle can span as little as 1px on a given
// page - see metrPlayerColByte()/metrObstacleColByte() above), two
// layers sharing the same (column, page) address but occupying genuinely
// DIFFERENT pixel rows within that one byte still fully clobber each
// other under plain assignment: the border's own single bit (row 0 or 63)
// vanishes into the middle of a much taller player/obstacle rectangle
// crossing that same column, and an obstacle passing a column can likewise
// blank out player pixels that don't actually occupy the same real row.
// Both were independently confirmed as real, reproducible visual problems
// by the user (border swallowed at the exact moment of a bottom-border
// death; obstacles' own lower rows erasing the player) - switching every
// layer to OR fixes both at the root, for any future layer combination
// too, rather than patching each pairwise symptom - matching the OR-based
// compositing most other ports in this project already use. The one
// place this doesn't change anything meaningfully: two layers that
// legitimately DO occupy the exact same real pixel (a genuine collision)
// still show as a solid "on" pixel either way, OR or replace - no visual
// difference for an actual overlap, only for two sprites' bits sharing a
// byte without truly overlapping in real screen rows.
void metrComposeRow( int page )
{
    int col;
    for( col = 0; col < 128; col++ ) metrPageBuffer[ col ] = 0;

    if( page == 0 ) for( col = 0; col < 127; col++ ) metrPageBuffer[ col ] |= 0x01;
    else if( page == 7 ) for( col = 0; col < 127; col++ ) metrPageBuffer[ col ] |= 0x80;

    int pb = metrPlayerColByte( page );
    if( pb != 0 )
      for( col = METR_PLAYER_X; col < METR_PLAYER_X + METR_PLAYER_WIDTH && col < 128; col++ )
        metrPageBuffer[ col ] |= pb;

    int i;
    for( i = 0; i < METR_MAX_OBSTACLES; i++ )
    {
        int ox = metrObstacleX[ i ];
        if( ox + METR_OBSTACLE_WIDTH < 0 || ox >= 128 ) continue;
        int ob = metrObstacleColByte( metrObstacleY[ i ], page );
        if( ob == 0 ) continue;
        for( col = ox; col < ox + METR_OBSTACLE_WIDTH; col++ )
          if( col >= 0 && col < 128 ) metrPageBuffer[ col ] |= ob;
    }

    if( page == 0 )
    {
        metrDrawScoreIntoBuffer( metrScore, metrScoreDigitCount( metrScore ) );
    }
    else if( page == 7 && metrMaxScore > 0 )
    {
        metrDrawScoreIntoBuffer( metrMaxScore, metrScoreDigitCount( metrMaxScore ) );
    }
}

#define METR_MODE_ATTRACT 0
#define METR_MODE_PLAYING 1
#define METR_MODE_FLASH   2

void metrRenderFrame( int mode )
{
    md_beginFrame();
    int col, page;
    for( page = 0; page < 8; page++ )
    {
        if( mode == METR_MODE_ATTRACT )
        {
            for( col = 0; col < 128; col++ )
              md_drawColumn( col, page, metrTitle[ page * 128 + col ] );
            continue;
        }

        metrComposeRow( page );
        for( col = 0; col < 128; col++ )
        {
            int val = metrPageBuffer[ col ];
            if( mode == METR_MODE_FLASH ) val = ( ~val ) & 0xFF;
            md_drawColumn( col, page, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define METR_STATE_ATTRACT        0
#define METR_STATE_PLAYING        1
#define METR_STATE_GAMEOVER_FLASH 2

// Upstream has no genuine real-time rate to match (just a bare _delay_ms(10)
// with no documented intent beyond "don't spin flat out") - this is a
// deliberate slowdown at direct user request, not a restored original
// rate. Gates the whole PLAYING tick (movement/collision/render together,
// not just rendering), matching this project's own standing "one
// divisor, no dual bookkeeping" practice - every existing frame-counted
// constant (metrFlashWaitFrames) is deliberately left unrescaled, so it
// simply now takes twice as long in real time.
#define METR_TICK_DIVISOR 2
int metrTickCounter;

int metrState;
bool metrPrevFire;
int metrFlashWaitFrames;

void metrBeginAttract()
{
    metrPrevFire = false;
    metrState = METR_STATE_ATTRACT;
}

void metrBeginPlaying()
{
    metrInitGame();
    metrTickCounter = 0;
    metrState = METR_STATE_PLAYING;
}

void metrBeginGameOverFlash()
{
    metrFlashWaitFrames = 60; // ~1s @ 60fps, matches upstream's own delay(1000)
    metrState = METR_STATE_GAMEOVER_FLASH;
}

void gameMeteorStorm_init()
{
    metrMaxScore = 0;
    metrBeginAttract();
}

void gameMeteorStorm_forceRedraw()
{
    if( metrState == METR_STATE_PLAYING ) metrRenderFrame( METR_MODE_PLAYING );
    else if( metrState == METR_STATE_GAMEOVER_FLASH ) metrRenderFrame( METR_MODE_FLASH );
    else metrRenderFrame( METR_MODE_ATTRACT );
}

void gameMeteorStorm_update()
{
    if( metrState == METR_STATE_ATTRACT )
    {
        bool fireNow = isFirePressed();
        if( fireNow && !metrPrevFire )
        {
            metrBeginPlaying();
            metrRenderFrame( METR_MODE_PLAYING );
            return;
        }
        metrPrevFire = fireNow;
        metrRenderFrame( METR_MODE_ATTRACT );
    }
    else if( metrState == METR_STATE_PLAYING )
    {
        metrTickCounter++;
        if( metrTickCounter < METR_TICK_DIVISOR ) return;
        metrTickCounter = 0;

        metrCheckMovement();
        metrMovePlayer();
        metrMoveObstacles();
        bool collided = metrCheckCollisions();

        metrRenderFrame( METR_MODE_PLAYING );

        if( collided )
        {
            metrBeginGameOverFlash();
            metrRenderFrame( METR_MODE_FLASH );
        }
    }
    else // METR_STATE_GAMEOVER_FLASH
    {
        metrFlashWaitFrames--;
        if( metrFlashWaitFrames <= 0 ) { metrBeginAttract(); metrRenderFrame( METR_MODE_ATTRACT ); return; }
        metrRenderFrame( METR_MODE_FLASH );
    }
}

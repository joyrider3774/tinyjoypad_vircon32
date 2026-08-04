// =============================================================================
// Vircon32-specific machine-dependent layer + main()
// =============================================================================
// Implements every md_* primitive declared in machineDependent.h, on top of
// the real video.h/input.h/audio.h/time.h. Also owns the top-level menu <->
// game dispatch loop.
//
// Video: TinyJoypad games stream their 128x64 OLED one SSD1306 "page" byte
// at a time (8 vertical pixels per byte, one column). There are only 256
// possible byte values, so instead of any per-pixel drawing, this file
// slices a single pre-baked 256-tile texture (assets/columns.png, built by
// tools/gen_column_atlas.py) into 256 regions - one per possible byte value,
// already rendered at final on-screen size - via define_region_matrix().
// md_drawColumn() then becomes one select_region()+draw_region_at() GPU
// blit per non-zero byte; zero bytes are skipped entirely since the frame
// was already clear_screen()-ed to black. See VIRCON32_C_DIALECT.md and
// CLAUDE.md for why this sidesteps the "no CPU-writable framebuffer" limit.
// =============================================================================

#include "machineDependent.h"
#include "menu.h"
#include "menuGameList.h"

#include "video.h"
#include "input.h"
#include "audio.h"
#include "time.h"
#include "../libs/PlayNote/playnote.h"

// -----------------------------------------------------------------------------
//   PRESENTATION LAYOUT
// -----------------------------------------------------------------------------

// 128 OLED px * 5 = 640 (exactly the screen width); 64 * 5 = 320, centered
// in the 360-tall screen with a 20px bar top and bottom.
#define TILE_SCALE   5
#define ATLAS_GRID   16
#define TILE_W       TILE_SCALE
#define TILE_H       ( 8 * TILE_SCALE )
#define ORIGIN_X     0
#define ORIGIN_Y     20

#define COLUMNS_TEXTURE_ID 0
#define WAVETABLE_SOUND_ID 0
// Was 2048 (a ~21.5Hz natural pitch at speed 1.0) - raised the SPU's own
// hardware speed-register clamp (WriteSPUChannelSpeed in the emulator's
// own V32SPUWriters.cpp: Clamp(speed, 0, 128)) into a real, audible-range
// problem: reaching a note a few kHz up needed a speed multiplier well
// past 128, silently clamping every such note to the exact same ceiling
// frequency (128 * 44100 / 2048 = 2756.25Hz) - found via a direct user
// report that Tiny Arkanoid's own well-known intro tune was completely
// unrecognizable here specifically (while sounding correct on this
// project's own SDL3/Playdate sibling ports), root-caused by computing
// every one of that tune's 5 distinct note pitches (freq bytes 105/125/
// 135/140/145, all in the 3.3-4.5kHz range once run through Sound()'s own
// `500000/(255-freq)` formula) and finding every single one exceeded the
// clamp, landing on that identical 2756.25Hz ceiling regardless of the
// intended pitch - correct rhythm, zero pitch variation, exactly matching
// the reported symptom. 256 raises the natural pitch to 44100/256 ≈
// 172.3Hz, and with it the ceiling to 128 * 44100 / 256 = 22050Hz -
// comfortably covering Arkanoid's own tune and the common freq=200/220
// values used elsewhere in this project's own games (a survey of every
// literal Sound() call across all 33 games found 200 used 27 times,
// resolving to ~9.1kHz) - see libs/PlayNote/sounds/wt_saw.wav's own
// regenerated-at-this-shorter-period asset.
#define WAVETABLE_PERIOD_SAMPLES 256

// Menu game-select thumbnails: one real 256x128 gameplay screenshot per
// game (assets/thumbnails.png, built offline from actual play captures -
// see CLAUDE.md), laid out as a grid rather than one long strip because
// Vircon32 caps texture dimensions at 1024x1024 - a 256x1792 vertical
// strip (14 straight down) would have exceeded that; 4 columns keeps the
// width at exactly 1024. Grew from 4x3 (1024x384, 12 games) to 4x4
// (1024x512, 13 games) when Tiny Minez was added, and stayed 4x4 (still
// comfortably inside the cap, room for 2 more before another row is
// needed) when Tiny Missile became the 14th. Region ids are assigned in
// the grid's own row-major reading order (0-13, left to right then
// down), matching addGames()'s registration order in menuGameList.c -
// the 2 unused trailing grid cells (14-15) are just background-filled
// filler in the source image and are never selected.
// Tiny DDug (22nd game, registration index 21) landed in cell 21 (row 5,
// col 1) of the already-existing 4x6 grid - the grid didn't need to grow.
// Wren Rollercoaster (24th game, registration index 23) landed in cell 23
// (row 5, col 3) - the LAST cell of that 4x6 grid, which was then exactly
// full. Frogger (25th game, registration index 24) needed a 7th row -
// canvas grew to 1024x896 (still comfortably inside the 1024x1024 cap),
// with Frogger's own thumbnail composited into the new row's first cell.
// Pong (26th game, registration index 25) landed in cell 25 (row 6, col
// 1) of that same 4x7 grid - still 2 cells free, no growth needed. Stacker
// (27th game, registration index 26) landed in cell 26 (row 6, col 2) -
// 1 cell still free. UFO (28th game, registration index 27) landed in
// cell 27 (row 6, col 3) - the LAST cell of this 4x7 grid, now exactly
// full. Tiny Dungeon (29th game) grew an 8th row (1024x1024, right at the
// cap). Oroboros/Run Dude Run/Four in a Row (30th-32nd games) filled
// cells 29/30/31 - the LAST cells of the whole 4x8 grid, which is now
// completely full AND already at Vircon32's hard 1024x1024 texture-
// dimension cap, so this single-texture atlas genuinely cannot grow any
// further. Dino Game (33rd game, registration index 32) needed a real
// SECOND texture (`assets/thumbnails2.png`, `obj/thumbnails2.vtex`,
// texture id 2) instead - a small 4x2 grid (1024x256, 8 cells of
// headroom for future games before a third texture is ever needed) -
// `md_drawGameThumbnail()`/`md_getThumbnailCount()` below dispatch
// between the two textures by gameIndex, treating them as one
// contiguous logical thumbnail space to every caller in `menu.c`.
#define THUMBNAILS_TEXTURE_ID 1
#define THUMBNAIL_W MD_THUMBNAIL_WIDTH
#define THUMBNAIL_H MD_THUMBNAIL_HEIGHT
#define THUMBNAIL_GRID_COLS 4
#define THUMBNAIL_GRID_ROWS 8
#define THUMBNAIL_COUNT 32

#define THUMBNAILS2_TEXTURE_ID 2
#define THUMBNAIL2_GRID_COLS 4
#define THUMBNAIL2_GRID_ROWS 2
#define THUMBNAIL2_COUNT 8

// Pixel-grid overlay (see drawPixelGridOverlay() below) - one pre-baked
// 640x320 texture (assets/pixelgrid.png, a transparent background with
// opaque black 1px lines every TILE_SCALE pixels in both directions,
// generated by tiling a single 5x5 corner-line tile across the whole
// game-area size) instead of 194 individual md_drawSolidRect() calls -
// since Vircon32's own screen size is fixed (unlike a resizable SDL
// window), the whole overlay can be exactly this one constant-size
// asset, blitted with a single draw_region_at() call relying on the
// GPU's own default alpha blending (blending_alpha, already the default
// mode - see video.h) to only actually affect the opaque grid-line
// pixels, leaving the transparent majority of the texture a no-op over
// whatever the game already drew. Confirmed png2vircon itself preserves
// a source PNG's real alpha channel (read directly from its own source,
// PNG2Vircon/png2vircon.cpp: png_set_tRNS_to_alpha()/only fills an
// opaque alpha channel when the source has none at all) rather than
// silently flattening transparency to opaque, which this design depends
// on working correctly.
#define PIXELGRID_TEXTURE_ID 3

// -----------------------------------------------------------------------------
//   VIDEO
// -----------------------------------------------------------------------------

void md_initVideo()
{
    select_texture( COLUMNS_TEXTURE_ID );

    define_region_matrix
    (
        0,                              // first_id (region id == byte value)
        0, 0, TILE_W - 1, TILE_H - 1,   // first tile bounds
        0, 0,                           // hotspot at top-left
        ATLAS_GRID, ATLAS_GRID,         // 16x16 = 256 tiles
        0                               // no gap between tiles
    );

    select_texture( THUMBNAILS_TEXTURE_ID );

    define_region_matrix
    (
        0,                                          // first_id == game index
        0, 0, THUMBNAIL_W - 1, THUMBNAIL_H - 1,     // first tile bounds
        0, 0,                                       // hotspot at top-left
        THUMBNAIL_GRID_COLS, THUMBNAIL_GRID_ROWS,
        0
    );

    select_texture( THUMBNAILS2_TEXTURE_ID );

    define_region_matrix
    (
        0,                                          // first_id == (game index - THUMBNAIL_COUNT)
        0, 0, THUMBNAIL_W - 1, THUMBNAIL_H - 1,
        0, 0,
        THUMBNAIL2_GRID_COLS, THUMBNAIL2_GRID_ROWS,
        0
    );

    select_texture( PIXELGRID_TEXTURE_ID );
    select_region( 0 );
    define_region( 0, 0, 639, 319, 0, 0 );

    set_multiply_color( color_white );
}

int md_getThumbnailCount()
{
    return THUMBNAIL_COUNT + THUMBNAIL2_COUNT;
}

// Dispatches across the two thumbnail textures, treating gameIndex as one
// contiguous logical space (0..THUMBNAIL_COUNT-1 in the first/original
// atlas, THUMBNAIL_COUNT..THUMBNAIL_COUNT+THUMBNAIL2_COUNT-1 in the
// second) - see this file's own comment above THUMBNAILS_TEXTURE_ID for
// why a second texture was needed at all.
void md_drawGameThumbnail( int gameIndex, int x, int y )
{
    if( gameIndex < 0 || gameIndex >= THUMBNAIL_COUNT + THUMBNAIL2_COUNT )
      return;

    if( gameIndex < THUMBNAIL_COUNT )
    {
        select_texture( THUMBNAILS_TEXTURE_ID );
        select_region( gameIndex );
    }
    else
    {
        select_texture( THUMBNAILS2_TEXTURE_ID );
        select_region( gameIndex - THUMBNAIL_COUNT );
    }
    draw_region_at( x, y );
}

// Solid-color filled rectangle - reuses the columns atlas's own region 255
// (byte value 0xFF - all 8 pixels lit, a plain TILE_W x TILE_H solid white
// tile already loaded for md_drawColumn) rather than adding a whole new
// texture asset just for this: video.h has no rectangle-fill primitive of
// its own, only region blits, so the standard trick (same one the sibling
// crisp-game-lib-portable_vircon32 project already uses for its own
// md_drawRect()) is to tint a solid region via set_multiply_color() and
// stretch it to any size via set_drawing_scale()+draw_region_zoomed_at().
// Used by the quit-confirmation dialog below (white outline, black
// interior) - restores the multiply color/scale afterward since every
// other draw call in this file (md_drawColumn, print_at) assumes the
// neutral white/1x state left by md_initVideo().
void md_drawSolidRect( int x, int y, int w, int h, int color )
{
    select_texture( COLUMNS_TEXTURE_ID );
    select_region( 255 );
    set_multiply_color( color );
    set_drawing_scale( (float)w / TILE_W, (float)h / TILE_H );
    draw_region_zoomed_at( x, y );
    set_multiply_color( color_white );
    set_drawing_scale( 1.0, 1.0 );
}

void md_beginFrame()
{
    clear_screen( color_black );
    select_texture( COLUMNS_TEXTURE_ID );
}

void md_drawColumn( int col, int page, int value )
{
    // Vircon32 ints are full 32-bit words with no implicit byte truncation
    // the way uint8_t gave the original AVR code - upstream draw code that
    // relies on a shift/OR "overflowing" out the top of a byte (e.g.
    // NumberPlace's sub-page digit compositing, obonoCoreShim's sprite/
    // string blending) leaves stray high bits set here instead of losing
    // them silently. Masking once at this single choke point (every
    // column value from every game/shim funnels through here) is more
    // robust than chasing down each individual shift site upstream.
    value &= 0xFF;

    if( value == 0 )
      return;

    select_region( value );
    draw_region_at( ORIGIN_X + col * TILE_SCALE, ORIGIN_Y + page * TILE_H );
}

void md_endFrame()
{
    end_frame();
}

// -----------------------------------------------------------------------------
//   INPUT
// -----------------------------------------------------------------------------

// Suppresses md_inputFire() until the physical button is actually released -
// armed every time a game is (re)launched from the menu (see main(), below).
// Without this, the same A-press a player used to *confirm* the menu
// selection is often still physically held on the game's very first real
// frame (isFirePressed()-style level reads have no idea it "belongs" to the
// menu, not the game) - games that treat fire as "skip the intro"/"start
// playing" would react to that leftover press immediately, before the
// player ever sees a title screen.
bool inputFireGateActive = false;

int md_inputLeftFrames()  { return gamepad_left();  }
int md_inputRightFrames() { return gamepad_right(); }
int md_inputUpFrames()    { return gamepad_up();    }
int md_inputDownFrames()  { return gamepad_down();  }

bool md_inputLeft()  { return md_inputLeftFrames()  > 0; }
bool md_inputRight() { return md_inputRightFrames() > 0; }
bool md_inputUp()    { return md_inputUpFrames()    > 0; }
bool md_inputDown()  { return md_inputDownFrames()  > 0; }

// While the fire gate is active, reports "long since released" (a large
// negative frames value) instead of the real raw reading - keeps this the
// single place the gate's own state transition happens, so md_inputFire()
// and any per-game md_recentlyPressed( md_inputFireFrames(), ... ) check
// agree on what "gated" looks like.
int md_inputFireFrames()
{
    int raw = gamepad_button_a();

    if( inputFireGateActive )
    {
        if( raw <= 0 )
          inputFireGateActive = false;
        return -3600;
    }

    return raw;
}

bool md_inputFire() { return md_inputFireFrames() > 0; }

bool md_inputStart() { return gamepad_button_start() > 0; }

bool md_inputFire2() { return gamepad_button_b() > 0; }

void md_armInputFireGate()
{
    inputFireGateActive = true;
}

// -----------------------------------------------------------------------------
//   AUDIO
// -----------------------------------------------------------------------------

// TinyJoypad's original hardware is a single piezo buzzer, so every game's
// own Sound()/playTone() call site was written assuming "this replaces
// whatever's currently sounding" - but that's a property of the ORIGINAL
// hardware, not something md_playTone() itself needs to enforce. PlayNote
// already manages up to 16 real simultaneous channels on its own
// (playnote_start() calls Vircon32's own play_sound(), which picks the
// first free SPU channel internally, per playnote.h's own doc) - the
// original version of this function got in its own way by forcing every
// call through one manually-tracked "audioVoice" and killing it before
// every new tone, so two genuinely concurrent cues (e.g. Tiny Pacman's
// continuously-retriggered power-pellet siren and its dot-eaten/ghost-
// eaten SFX) could never be heard at once even though the hardware
// supports it.
//
// Fixed by not fighting PlayNote's own channel picker at all: every call
// just asks for a fresh channel and tracks that specific channel's own
// expiry, instead of forcing everything through a single slot. This
// doesn't change the *audible* behavior of the common "one tone
// replacing the previous one" case - every existing frame-stepped
// sequencer in this project already gates its own next note to start
// only after the previous one's real duration has elapsed, so by the
// time a new call happens, the old one has normally already expired and
// freed its channel back up on its own. It only changes behavior when
// two calls are genuinely concurrent, which is exactly the case that was
// broken.
#define AUDIO_MAX_VOICES 16
int[AUDIO_MAX_VOICES] audioStopAtFrame;

void md_initAudio()
{
    playnote_init( WAVETABLE_SOUND_ID, WAVETABLE_PERIOD_SAMPLES );
    int i;
    for( i = 0; i < AUDIO_MAX_VOICES; i++ )
      audioStopAtFrame[ i ] = -1;
}

void md_playTone( float freqHz, float durationSeconds )
{
    if( freqHz <= 0.0 )
      return;

    int channel = playnote_start( freqHz, 0.6 );
    if( channel < 0 )
      return; // every channel already busy - drop the note rather than misbehave

    int durationFrames = (int)( durationSeconds * frames_per_second );
    if( durationFrames < 1 )
      durationFrames = 1;

    audioStopAtFrame[ channel ] = get_frame_counter() + durationFrames;
}

void md_stopTone()
{
    playnote_stop_all();
    int i;
    for( i = 0; i < AUDIO_MAX_VOICES; i++ )
      audioStopAtFrame[ i ] = -1;
}

void md_updateAudio()
{
    int i;
    for( i = 0; i < AUDIO_MAX_VOICES; i++ )
    {
        if( audioStopAtFrame[ i ] != -1 && get_frame_counter() >= audioStopAtFrame[ i ] )
        {
            playnote_stop( i );
            audioStopAtFrame[ i ] = -1;
        }
    }

    playnote_update();
}

// -----------------------------------------------------------------------------
//   TOP-LEVEL DISPATCH: menu <-> games
// -----------------------------------------------------------------------------

int currentGameIndex = -1;
bool prevStart = false;

// -----------------------------------------------------------------------------
//   Quit-confirmation dialog (Start, while a game is running)
// -----------------------------------------------------------------------------

// Confirms before actually leaving a game back to the menu - the previous
// behavior (Start instantly quit) risked losing progress on an accidental
// press. While this dialog is up the current game's own update() is not
// called at all (see main()'s dispatch loop below), so gameplay is fully
// frozen rather than continuing to run behind the dialog.
bool confirmingQuit = false;
int confirmSelection = 0; // 0 = NO (default - the safer choice), 1 = YES
bool prevConfirmLeft = false;
bool prevConfirmRight = false;
bool prevConfirmFire = false;

void drawConfirmQuitDialog()
{
    int boxX = 160, boxY = 110, boxW = 320, boxH = 140;
    int borderThickness = 6;

    md_drawSolidRect( boxX, boxY, boxW, boxH, color_white );
    md_drawSolidRect
    (
        boxX + borderThickness, boxY + borderThickness,
        boxW - ( borderThickness * 2 ), boxH - ( borderThickness * 2 ),
        color_black
    );

    print_at( boxX + 125, boxY + 20, "CONFIRM" );
    print_at( boxX + 95, boxY + 55, "QUIT TO MENU?" );

    int yesX = boxX + 100;
    int noX = boxX + 210;
    int optionsY = boxY + 95;

    if( confirmSelection == 1 )
      print_at( yesX - 15, optionsY, ">" );
    else
      print_at( noX - 15, optionsY, ">" );

    print_at( yesX, optionsY, "YES" );
    print_at( noX, optionsY, "NO" );
}

// -----------------------------------------------------------------------------
//   Pixel-grid overlay (Button X, while a game is running)
// -----------------------------------------------------------------------------

// A static "LCD pixel grid" toggle - thin (1px) black lines at every
// GAME_SCALE-multiple boundary, so each of the original 128x64 OLED
// pixels reads as its own distinct visible cell once scaled up, instead
// of blending into one smooth 5x5 block. Matches the sibling SDL2/SDL3
// ports' own identical pixel-grid presentation effect (pixelGridEffect.c
// there) - this is the *only* one of that trio ported here (no glow, no
// CRT scanlines): both of those need real per-frame recomputation
// (glow's own downscale-then-GPU-scale blur, CRT's own scroll position)
// redrawn fresh every frame to avoid accumulating on a persistent
// buffer, which has no equivalent one-off asset/rationale here; the
// pixel grid is a plain, static pattern that never changes frame to
// frame, so (unlike those two) it's just one pre-baked texture blitted
// unconditionally each frame it's enabled (see PIXELGRID_TEXTURE_ID's
// own comment above for the asset/alpha-blending design, chosen directly
// over 194 individual md_drawSolidRect() calls specifically because
// Vircon32's own screen size is fixed, unlike a resizable SDL window,
// so one constant-size pre-baked overlay covers every case).
//
// Off by default and only ever toggled/rendered while a game is actually
// running (never on the menu, and not drawn on top of the quit-
// confirmation dialog either) - matching the SDL ports' own `!isInMenu`-
// equivalent gating exactly, via the same currentGameIndex != -1 check
// every other per-game-only feature in this file already uses.
bool pixelGridEnabled = false;
bool prevGridButton = false;

// Sound on/off - Button Y (unused elsewhere: A=Fire, B=Fire2, X=pixel-grid
// toggle, Start=quit dialog), toggled globally rather than gated to
// gameplay-only the way the pixel-grid toggle is - matches the sibling
// SDL ports' own BUTTON_SOUNDSWITCH, which likewise works everywhere
// (menu, gameplay, dialog), not just mid-game. Implemented via Vircon32's
// own real SPU_GlobalVolume hardware register (set_global_volume(),
// range 0-2) rather than gating each md_playTone() call individually -
// simpler, and (like the SDL ports' own gMuted flag) leaves every game's
// own audioStopAtFrame[] duration bookkeeping untouched, so unmuting mid-
// tone resumes hearing whatever's still legitimately playing instead of
// needing anything to restart.
bool audioMuted = false;
bool prevMuteButton = false;

void drawPixelGridOverlay()
{
    select_texture( PIXELGRID_TEXTURE_ID );
    select_region( 0 );
    draw_region_at( ORIGIN_X, ORIGIN_Y );
}

void main()
{
    select_gamepad( 0 );

    md_initVideo();
    md_initAudio();

    addGames();
    menu_init();

    while( true )
    {
        bool start = md_inputStart();
        bool justStarted = ( start && !prevStart );
        prevStart = start;

        if( confirmingQuit )
        {
            bool left = md_inputLeft();
            bool right = md_inputRight();
            bool fire = md_inputFire();
            bool justLeft = ( left && !prevConfirmLeft );
            bool justRight = ( right && !prevConfirmRight );
            bool justFire = ( fire && !prevConfirmFire );
            prevConfirmLeft = left;
            prevConfirmRight = right;
            prevConfirmFire = fire;

            if( justLeft || justRight )
              confirmSelection = 1 - confirmSelection;

            if( justFire )
            {
                // The same physical press that just confirmed this dialog
                // must not also register as a fresh press once we're back
                // in the menu (instantly launching whatever's highlighted)
                // or back in gameplay (an unwanted in-game action the
                // instant it resumes) - md_inputFire() is the single
                // shared gate every isFirePressed()/menu fire-read goes
                // through, so arming it here covers both destinations.
                md_armInputFireGate();
                if( confirmSelection == 1 )
                {
                    md_stopTone();
                    currentGameIndex = -1;
                    menu_init();
                }
                else if( menu_getGame( currentGameIndex )->onResume != NULL )
                  menu_getGame( currentGameIndex )->onResume();
                confirmingQuit = false;
            }
            else if( justStarted )
            {
                // pressing Start again cancels, same as selecting NO
                if( menu_getGame( currentGameIndex )->onResume != NULL )
                  menu_getGame( currentGameIndex )->onResume();
                confirmingQuit = false;
            }

            drawConfirmQuitDialog();
        }
        else if( currentGameIndex != -1 && justStarted )
        {
            confirmingQuit = true;
            confirmSelection = 0;
            // Arm against whatever Left/Right/Fire happen to already be
            // held at this exact moment, the same reasoning as
            // md_armInputFireGate() - otherwise a leftover press from
            // gameplay could immediately register as a dialog input.
            prevConfirmLeft = md_inputLeft();
            prevConfirmRight = md_inputRight();
            prevConfirmFire = md_inputFire();
            drawConfirmQuitDialog();
        }
        else if( currentGameIndex == -1 )
        {
            int chosen = menu_update();
            if( chosen != -1 )
            {
                currentGameIndex = chosen;
                md_armInputFireGate();

                // Clear to black once, immediately on selection and before
                // the chosen game's own init() runs any of its own code -
                // some games' init() doesn't necessarily draw a full frame
                // of its own right away (state setup only, first real draw
                // deferred to the next update() call), and Vircon32's
                // screen persists between frames exactly like real SSD1306
                // VRAM does when nothing redraws it - without this, the
                // last menu frame would otherwise still be sitting on
                // screen for that one gap tick instead of a clean black
                // transition.
                md_beginFrame();

                menu_getGame( chosen )->init();
            }
        }
        else
        {
            menu_getGame( currentGameIndex )->update();
        }

        // Button X toggles the pixel-grid overlay - only meaningful during
        // actual gameplay (matching drawPixelGridOverlay()'s own render
        // gate below), so the press is only even looked at then; toggling
        // it while on the menu or mid quit-dialog would otherwise be a
        // silent, confusing no-op.
        if( currentGameIndex != -1 && !confirmingQuit )
        {
            bool gridButton = ( gamepad_button_x() > 0 );
            if( gridButton && !prevGridButton )
            {
                pixelGridEnabled = !pixelGridEnabled;

                // Turning the grid OFF doesn't erase itself: the grid's own
                // opaque black lines were drawn directly into Vircon32's
                // persistent GPU display buffer, permanently overwriting
                // whatever game pixels used to be there - simply not
                // calling drawPixelGridOverlay() next frame does nothing
                // to restore them. The only thing that actually repaints
                // those pixels is the game's own next real redraw - but
                // several games (this shim-lineage one included) skip
                // their own redraw on frames where nothing changed
                // internally (obonoCoreShim's own isInvalid-gated skip),
                // so without forcing one, a toggle-off could be stuck
                // showing the stale grid-baked-in frame indefinitely -
                // exactly the same class of bug already documented for
                // the quit-confirmation dialog's own resume path (see
                // this file's own onResume calls above). Same fix here:
                // force one fresh full redraw the instant the toggle
                // changes state (both directions, not just off - turning
                // it ON while the game also isn't currently redrawing
                // needs the same nudge to composite the grid over
                // genuinely current content instead of an equally stale
                // frame).
                if( menu_getGame( currentGameIndex )->onResume != NULL )
                  menu_getGame( currentGameIndex )->onResume();
            }
            prevGridButton = gridButton;
        }

        if( currentGameIndex != -1 && !confirmingQuit && pixelGridEnabled )
          drawPixelGridOverlay();

        // Button Y toggles sound globally - unlike the pixel-grid toggle
        // above, deliberately NOT gated to "a game is running": the menu
        // itself is silent today, but gating this to gameplay-only would
        // be a surprising inconsistency with the sibling SDL ports' own
        // always-available mute, and would mean muting mid-game then
        // returning to the menu and back couldn't be done from the menu
        // screen itself.
        bool muteButton = ( gamepad_button_y() > 0 );
        if( muteButton && !prevMuteButton )
        {
            audioMuted = !audioMuted;
            if( audioMuted )
              set_global_volume( 0.0 );
            else
              set_global_volume( 1.0 );
        }
        prevMuteButton = muteButton;

        md_updateAudio();
        obonoCoreShimUpdateSound();
        md_endFrame();
    }
}

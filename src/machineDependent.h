#ifndef MACHINE_DEPENDENT_H
#define MACHINE_DEPENDENT_H

// -----------------------------------------------------------------------------
// The per-port interface every TinyJoypad compatibility shim (tinyJoypadShim.h,
// obonoCoreShim.h) is built on top of. Bodies live in portVircon32.c (Vircon32
// has no linker - see VIRCON32_C_DIALECT.md section 11 - so these are plain
// forward declarations; the real definitions just need to appear somewhere
// later in the single compiled file, which they do via main.c's include
// order).
//
// Both upstream driver lineages (Lorandil/phoenixbozo's tinyJoypadUtils, and
// Obono's TinyJoypadWorks core) ultimately stream the real SSD1306 display
// one byte at a time, one hardware "page" at a time - each byte packs 8
// vertical pixels of a single column (bit 0 = top, bit 7 = bottom). There are
// only 256 possible byte values, so md_drawColumn() below takes that byte
// value directly and blits one of 256 pre-baked texture regions (see
// tools/gen_column_atlas.py) instead of writing any pixels - Vircon32's GPU
// is a texture-region blitter with no CPU-writable framebuffer.
// -----------------------------------------------------------------------------

#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define OLED_PAGES  8

// =============================================================================
//   VIDEO
// =============================================================================

void md_initVideo();

// clears the screen to black - called once at the start of every game frame,
// before that frame's md_drawColumn() calls
void md_beginFrame();

// col: 0..127 (OLED x). page: 0..7 (OLED y / 8). value: the raw SSD1306
// column byte (0-255) - a value of 0 means "all 8 pixels off" and is a no-op,
// since the frame was already cleared to black by md_beginFrame()
void md_drawColumn( int col, int page, int value );

// waits for vsync (wraps time.h's end_frame())
void md_endFrame();

// Draws a solid-color filled rectangle with its top-left corner at (x, y)
// - used for the quit-confirmation dialog's box (see portVircon32.c's
// main() dispatch loop), not something individual games call.
void md_drawSolidRect( int x, int y, int w, int h, int color );

// Pixel size of a game's menu thumbnail (assets/thumbnails.png) - shared
// here so callers (menu.c) can lay out around it (e.g. centering it
// vertically) without duplicating the actual asset dimensions.
#define MD_THUMBNAIL_WIDTH  256
#define MD_THUMBNAIL_HEIGHT 128

// How many games have a pre-baked gameplay thumbnail (assets/thumbnails.png,
// one 256x128 real-gameplay screenshot per game, in the same order as
// addGames()). The menu uses this to skip drawing a thumbnail for any game
// index at or past it (e.g. a newly-added game before a thumbnail exists
// for it), rather than assuming every menu entry has one.
int md_getThumbnailCount();

// Draws gameIndex's pre-baked gameplay screenshot (256x128) with its
// top-left corner at (x, y). No-op if gameIndex is out of the thumbnail
// atlas's range - callers should still gate on md_getThumbnailCount()
// first rather than relying on this no-op alone, since drawing nothing
// there is a silent no-op, not an error.
void md_drawGameThumbnail( int gameIndex, int x, int y );

// =============================================================================
//   INPUT
// =============================================================================

bool md_inputLeft();
bool md_inputRight();
bool md_inputUp();
bool md_inputDown();

// A level read like the rest, EXCEPT immediately after md_armInputFireGate()
// is called: from then until the physical button is actually released,
// this always reports false - see md_armInputFireGate()'s own comment.
bool md_inputFire();

bool md_inputStart();

// A second, independent action button (Vircon32's B) - only TinyMinez
// needs this so far. Unlike Fire, it's never involved in the menu->game
// launch handoff, so it needs no fire-gate equivalent.
bool md_inputFire2();

// Call once, right when a game is (re)launched from the menu, to suppress
// md_inputFire() until the confirm press that launched it is physically
// released - otherwise that same press can bleed into the game's very
// first frame and be misread as the player's own input.
void md_armInputFireGate();

// Raw held-frame counters, straight from Vircon32's own gamepad_*()
// registers: positive N means "held for N real frames" (N==1 the instant
// it was pressed), negative N means "released N real frames ago". Games
// that only tick their own logic once every few real frames (see the
// TICKS_PER_FRAME-style frame-skip pattern used by the reduced-fps ports)
// must not rely on md_inputXXX()'s plain bool for one-shot "just pressed"
// detection: a tap that both started and ended during a skipped real
// frame would already show a *negative* frames value again by the time
// the next logic tick finally reads it, but a tap that started during a
// skipped frame and is still held now would read as N==2 (or more)
// instead of the N==1 an unthrottled game's edge-check expects - so a
// naive "== 1" check silently misses it. Use md_recentlyPressed() below
// against these raw values instead, sized to the game's own frame-skip
// window.
int md_inputLeftFrames();
int md_inputRightFrames();
int md_inputUpFrames();
int md_inputDownFrames();
int md_inputFireFrames();

// True if a button's raw held-frame counter shows it became newly pressed
// at any point within the last `window` real frames (inclusive) - the
// safe replacement for a plain "== 1" edge check in any game whose logic
// only ticks once every `window` real frames, so a press landing on one
// of the skipped frames in between still gets recognized as "just
// pressed" on the next tick that actually runs. window == 1 (an
// unthrottled game, ticking every real frame) reduces this to exactly the
// traditional single-frame edge check.
#define md_recentlyPressed(framesValue, window) ( (framesValue) >= 1 && (framesValue) <= (window) )

// =============================================================================
//   AUDIO
// =============================================================================

void md_initAudio();

// Starts playing freqHz for durationSeconds on the next free hardware
// channel (up to 16 - see portVircon32.c's own header comment on
// md_playTone() for why this doesn't force everything onto one shared
// voice the way it used to). freqHz <= 0 is treated as silence (used by
// ports of Sound(0, dur) rest/pause calls). Unlike the original AVR
// Sound(), this does not block: it returns immediately and the tone is
// stopped automatically by md_updateAudio() once its duration elapses,
// so gameplay/animation keeps running during a sound effect instead of
// freezing for it. Two or more genuinely concurrent calls (e.g. Tiny
// Pacman's continuously-retriggered power-pellet siren alongside its
// dot-eaten/ghost-eaten SFX) are now both actually audible instead of
// cutting each other off - a plain sequential burst of calls with no
// real time between them, though, is still only ever heard as a chord of
// near-identical short notes at best, not a melody: use a frame-stepped
// sequencer (see any game's own *AdvanceSfx()/*AdvanceNoteSeq() for the
// established pattern) for anything meant to be heard as several
// distinct notes in sequence.
void md_playTone( float freqHz, float durationSeconds );

// stops every currently-playing tone immediately (no fade) - used when
// leaving a game (returning to the menu) so no audio survives into the
// next screen
void md_stopTone();

// advances every channel's own scheduled auto-stop and PlayNote's fade
// processing - call exactly once per frame, regardless of which game (if
// any) is running
void md_updateAudio();

// =============================================================================
//   MEMORY CARD (backs eepromShim.h's persistent per-game EEPROM emulation)
// =============================================================================
// Thin wrappers around Vircon32's own memcard.h (card_is_connected(),
// card_read_signature()/card_write_signature()/card_signature_matches(),
// card_read_data()/card_write_data()) - kept in this machine-dependent layer
// rather than called directly from eepromShim.c, the same reasoning as every
// other Vircon32-specific primitive here. offsetWords/sizeWords are in
// words (Vircon32 ints), not bytes - matching how card_read_data()/
// card_write_data() and sizeof() already work on this platform project-wide.

bool md_cardIsConnected();

// true only if the connected card's own 20-word signature matches this
// project's fixed signature (see eepromShim.c) - a card written by an
// unrelated program, or a blank card, both read as false here rather than
// risking a misread of foreign data.
bool md_cardHasOurSignature();

// stamps this project's fixed signature onto the connected card - called
// once, the first time anything is ever written to a fresh/foreign card.
void md_cardWriteSignature();

void md_cardReadData( void* dest, int offsetWords, int sizeWords );
void md_cardWriteData( void* src, int offsetWords, int sizeWords );

#endif

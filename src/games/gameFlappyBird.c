// =============================================================================
// Flappy Bird (Alex Wulff, www.AlexWulff.com - the only attribution anywhere
// in the source, a plain header comment; no license statement anywhere in
// the game's own code - see this file's own Licensing note in CLAUDE.md).
// From `more games/FlappyBird/` - a single-button-per-direction Flappy Bird
// clone: two buttons step the bird up/down by exactly one row (no gravity/
// physics at all, unlike the "hold to rise, release to fall" mechanic this
// project's own UFO/Meteor Storm ports use), dodging a stream of wall gaps
// scrolling in from the right, speeding up gradually over time.
//
// **The simplest rendering model of any port in this project so far**:
// every draw call upstream (`drawWallSequence()`'s own `oled.drawImage(...,
// column*8, page, 8, 1)`) is exactly one 8x8 grid cell, fully page-and-
// column-aligned - there is no sub-pixel/sub-page positioning anywhere in
// this game at all (unlike Meteor Storm/Run Dude Run's own sub-page sprite
// math), so no byte-truncation/shift-safety concerns apply here. The whole
// game logically operates on a 16-column x 8-page grid; `flpyComposeRow()`
// below just resolves, for each of the 128 real columns, which 8x8 grid
// cell it belongs to and what that cell should currently show.
//
// Not `tinyJoypadShim`/`obonoCoreShim` lineage - genuine bespoke #AttinyArcade-
// style hardware (`leftButton`=pin 3, `rightButton`=pin 4, both real digital
// pins read via a pin-change interrupt, not TinyJoypad's own analog ladder).
// `topButton` (pin 1) is declared and configured as an input but never
// actually read anywhere in the source (confirmed by grep) - dead, matching
// this project's own precedent for confirmed-unused declarations. No new
// shim was needed: `isUpPressed()`/`isDownPressed()` (left=up, right=down,
// matching the buttons' own real effect on `currentSpritePosition`) and
// `arand()` (for the wall's random hole position) already cover the whole
// input/RNG surface. No sound of any kind exists anywhere in this game
// (confirmed by grep - no buzzer, no tone/beep call) - matching Meteor
// Storm's own precedent, the second port in this project needing zero
// sound work.
//
// **Upstream's own interrupt-driven "one press = one step" input is
// reproduced with plain per-frame edge detection** (`flpyPrevUp`/
// `flpyPrevDown`), not gated behind the wall-movement tick at all - matches
// upstream's own real behavior, where `ISR(PCINT0_vect)` fires and updates
// `currentSpritePosition` completely independently of, and much faster
// than, the wall-movement timer. Upstream's own real 200ms software
// debounce (`buttonDebounceTime`, a genuine noisy-real-button workaround)
// is dropped, matching this project's established treatment of every other
// game's own real-hardware debounce code - a clean digital gamepad read
// doesn't need it.
//
// **Wall movement timing is a genuine, real accelerating rate** (`300 -
// 10*elapsedSeconds`, floored at 50ms) - ported as an equivalent frame-
// counted version (`flpyElapsedFrames` standing in for `millis()`,
// `flpyTicksPerMove` recomputed from the same formula converted to frames
// at 60fps) rather than approximated with a single representative rate,
// since this is a real, deliberate game-design element (the game visibly
// gets harder over a real play session) worth preserving faithfully.
// Upstream's own "every 4th wall-move spawns one new wall, cycling through
// a 4-slot array" spawn cadence is ported directly against the same frame-
// counted clock. Upstream also has a second real timing gate - an 8-second
// "grace period" after which the very first wall becomes able to trigger a
// collision - initially ported faithfully too, but removed after direct
// user testing (see flpyMoveWallsStep()'s own comment for why).
//
// **A render-order detail preserved deliberately, not simplified away**:
// upstream's own `drawWallSequence()` has an explicit comment - "we don't
// want our bird to disappear when a wall goes over it, so it's re-drawn
// every time there is a wall at column 0" - meaning the bird is always
// drawn *after*, and on top of, whatever a wall drew at column 0. This
// port's own full-screen-every-frame redraw model reproduces the same
// effect structurally: `flpyComposeRow()` always resolves the bird's own
// cell last, unconditionally overwriting whatever a wall byte computed for
// that exact (column 0, page) position - the same real-hardware-driven
// design intent (let the player see their own exact row against the gap),
// just achieved via a full redraw instead of upstream's own incremental
// one-cell-at-a-time overwrite (which relied on real SSD1306 VRAM
// persisting every other cell between calls - this port always recomputes
// the whole grid fresh each frame instead, avoiding that whole class of
// bug proactively rather than needing a later fix, per this project's own
// now-standing lesson from several earlier ports).
//
// **One deliberate UX deviation, matching every other port in this
// project**: upstream's own `gameOver()` shows the GameOver bitmap and
// then loops forever (`while(1==1)`) with no way back to play again short
// of a hard power cycle. Added a genuine attract screen (upstream has none
// at all - the real game starts immediately at power-on) with an edge-
// detected Fire-to-start gate, and a Fire-to-return-to-attract gesture on
// the game-over screen, matching the UX convention already established for
// every other port from this same "wider `more games/` search" batch
// (Astro Barrier/ATtiny Snake/Meteor Storm).
//
// `wall`/`BirdSprite` (8 bytes each, one full 8x8 grid cell) and `GameOver`
// (1024 bytes, standard row-major page order) were byte-diff-verified via
// a small Python script against the upstream source before ever being
// pasted in, matching this project's own established "byte-diff
// transcribed tables" discipline.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data
// -----------------------------------------------------------------------------

// One full 8x8 grid cell each - byte-diff-verified against upstream's own
// wall[]/BirdSprite[] (each already the exact byte values sent to 8
// consecutive real screen columns on one page, no shift/mask math needed).
int[8] flpyWallTile = { 0xFF, 0x81, 0x95, 0xA9, 0x89, 0xD1, 0x85, 0xFF };
int[8] flpyBirdTile = { 0x78, 0x84, 0x82, 0x81, 0xC9, 0x22, 0x24, 0x18 };

// Game-over screen (128x64, standard row-major page order) - byte-diff-
// verified against upstream's own GameOver[].
int[1024] flpyGameOver =
{
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xE0, 0xF0, 0xF8, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0x7F, 0x7F, 0x3F, 0x1F, 0x1F, 0x0F, 0x0F, 0x0F, 0x07, 0x0F, 0x07, 0x07, 0x07,
0x07, 0x07, 0x07, 0x07, 0x0F, 0x07, 0x07, 0x0F, 0x0F, 0x1F, 0x1F, 0x3F, 0x3F, 0x7F, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xFE, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x80, 0x00, 0x00, 0x00, 0xC0, 0x7A,
0x46, 0x78, 0xC0, 0x00, 0x00, 0x00, 0x02, 0xFE, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x28, 0x28, 0x28,
0x30, 0x00, 0x08, 0x98, 0x60, 0x60, 0x98, 0x08, 0x00, 0x02, 0xFE, 0x02, 0xF0, 0x02, 0xFE, 0x02,
0x08, 0xF8, 0x00, 0x00, 0x88, 0xF8, 0x00, 0x00, 0x00, 0x02, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x08,
0xFC, 0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x08, 0xFC, 0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x80, 0x80,
0x00, 0x00, 0xC0, 0xE8, 0xF8, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x1F,
0x07, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
0x03, 0x07, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFC, 0xF9, 0xE1, 0x81,
0x00, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x79, 0x85, 0x84, 0x84, 0x4C, 0x01, 0x00, 0x79, 0x84, 0x84,
0x84, 0x78, 0x01, 0x85, 0xFC, 0x85, 0xF9, 0x84, 0xF9, 0x81, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01,
0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01,
0xF4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00,
0x00, 0x10, 0x50, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xB0,
0x00, 0x00, 0x00, 0x45, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFC, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0xE0, 0xE0, 0xF0, 0x70, 0x70, 0x70,
0x70, 0xF0, 0xE0, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0xE0,
0xC0, 0x18, 0x0B, 0x10, 0xF8, 0xFE, 0xFF, 0xFF, 0xFE, 0xFC, 0xFC, 0xFC, 0x18, 0x00, 0x00, 0x80,
0xC0, 0xC0, 0x00, 0x00, 0x00, 0x30, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF, 0xFF, 0xFC, 0x78, 0x00, 0x16,
0x20, 0x40, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x7F, 0xFF, 0xF1, 0xE0, 0xC0, 0xCE, 0xCE,
0xCE, 0xFE, 0xFE, 0xFE, 0x00, 0x00, 0xE2, 0xF6, 0xFF, 0x9B, 0x9B, 0x9B, 0xFF, 0xFE, 0xFC, 0x00,
0x00, 0xFE, 0xFE, 0xFE, 0x06, 0x07, 0x07, 0xFF, 0xFF, 0xFC, 0x06, 0x07, 0x07, 0xFF, 0xFF, 0xFC,
0x00, 0x00, 0x7C, 0xFE, 0xFE, 0xDB, 0x9B, 0x9B, 0x9B, 0xDF, 0x9E, 0x9C, 0x00, 0x00, 0x00, 0x00,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xF0, 0xE0, 0xE0, 0x20, 0x60, 0xC1, 0x81, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x20, 0x7E, 0x7F,
0xFF, 0x7F, 0x7F, 0x20, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0xC1, 0xC0, 0x60, 0x20, 0xB0,
0xF0, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x01, 0x00,
0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x1F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xBF, 0xFF, 0xDC, 0xC0, 0xD8, 0x80, 0x98, 0x00, 0x18, 0x50, 0x08,
0x10, 0x58, 0x00, 0x98, 0x10, 0x08, 0xD0, 0x88, 0xA8, 0xC0, 0xDF, 0xFF, 0xBF, 0x00, 0x00, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0x3F, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xE0, 0xF0, 0xF0,
0x78, 0x38, 0x38, 0x38, 0x38, 0x78, 0xF0, 0xF0, 0xE0, 0x80, 0x00, 0x80, 0x80, 0x80, 0x80, 0x00,
0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00,
0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x03, 0x07, 0x1F, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0xFF, 0xF9, 0xC0, 0x80, 0x03, 0x07, 0x08, 0x19, 0x13, 0x27, 0x27, 0x27, 0x47, 0x27,
0x47, 0x27, 0x4E, 0x27, 0x47, 0x67, 0x17, 0x13, 0x09, 0x09, 0x07, 0x03, 0x80, 0xC0, 0xFA, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x3F, 0x0F, 0x03, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x3F, 0x7F, 0x78,
0xF0, 0xE0, 0xE0, 0xE0, 0xE0, 0xF0, 0x78, 0x7F, 0x3F, 0x0F, 0x00, 0x00, 0x03, 0x1F, 0x7F, 0xFC,
0xE0, 0xFC, 0x7F, 0x0F, 0x03, 0x00, 0x00, 0x3E, 0x7F, 0x7F, 0xED, 0xCD, 0xCD, 0xCD, 0xEF, 0x4F,
0x4E, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x07, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0x7F, 0xFF,
0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFC, 0xFC, 0xF8, 0xF0, 0xF0, 0xE0, 0xE0, 0xE0,
0xC0, 0xE0, 0xE0, 0xE0, 0xF0, 0xF0, 0xF8, 0xF8, 0xFC, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
0xFF, 0xFF, 0x7F, 0x7F, 0x3F, 0x1F, 0x1F, 0x07, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Standard 95-char 6x8 ssd1306xled font, already proven for Oroboros/Run
// Dude Run/Dino Game/Astro Barrier/ATtiny Snake - reused verbatim, each
// game keeps its own self-contained copy per this project's convention.
int[570] flpyFont =
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

int flpyFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return flpyFont[ ( ch - 32 ) * 6 + col ];
}

int flpyTextByteAt( int* text, int textLen, int startX, int x )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= textLen ) return 0;
    return flpyFontByte( text[ charIdx ], rel % 6 );
}

// -----------------------------------------------------------------------------
//   Constants / state
// -----------------------------------------------------------------------------

#define FLPY_GRID_COLS 16
#define FLPY_GRID_PAGES 8
#define FLPY_WALL_COUNT 4
#define FLPY_HOLE_COUNT 7

int flpyBirdRow;

// Parallel arrays instead of a Wall struct (matching this project's own
// established "flatten a trivial upstream struct to parallel arrays"
// precedent) - column in [0,15] is on-screen, negative/>15 means inactive/
// off-screen (upstream's own convention, preserved directly).
int[4] flpyWallColumn;
int[4] flpyWallHole;
int flpyNextWallSlot;

int flpyElapsedFrames; // stands in for upstream's own millis()
int flpyTicksPerMove;
int flpyMoveCountdown;
int flpyMoveCount; // upstream's own gameCounter

bool flpyPrevUp;
bool flpyPrevDown;

// -----------------------------------------------------------------------------
//   Game logic (direct translation of moveWalls()/the main loop's own
//   timing derivation)
// -----------------------------------------------------------------------------

// currentTiming = 300 - 10*elapsedSeconds, floored at 50ms; converted to a
// frame count at the engine's real 60fps.
int flpyComputeTicksPerMove()
{
    int elapsedSeconds = flpyElapsedFrames / 60;
    int timingMs = 300 - 10 * elapsedSeconds;
    if( timingMs < 50 ) timingMs = 50;
    int ticks = timingMs * 60 / 1000;
    if( ticks < 1 ) ticks = 1;
    return ticks;
}

bool flpyIsHoleRow( int holePos, int page )
{
    return ( page == holePos ) || ( page == holePos + 1 );
}

void flpySpawnWall()
{
    if( flpyNextWallSlot > 3 ) flpyNextWallSlot = 0;
    flpyWallColumn[ flpyNextWallSlot ] = 15;
    flpyWallHole[ flpyNextWallSlot ] = arand( FLPY_HOLE_COUNT );
    flpyNextWallSlot++;
}

// Returns true on a collision - "reaching column 0 outside the hole rows",
// checked per wall, every move tick.
//
// Upstream's own moveWalls() also gates this behind `millis() > 8000L` - a
// genuine 8-second immunity window every fresh game starts with, during
// which a wall reaching column 0 can never trigger a collision at all,
// regardless of the bird's position. Ported faithfully at first, but
// dropped after direct user testing showed this reads as a real bug
// ("the bird can fly through walls without game over happening") rather
// than a deliberate mercy window - with walls starting to scroll in
// almost immediately at the very start of a game, several of them can
// reach column 0 completely for free inside those first 8 seconds, which
// is very easy to misread as broken collision detection rather than an
// intentional grace period. Removed at direct user request.
bool flpyMoveWallsStep()
{
    int i;
    for( i = 0; i < FLPY_WALL_COUNT; i++ )
    {
        if( flpyWallColumn[ i ] < 0 || flpyWallColumn[ i ] > 16 ) continue;
        flpyWallColumn[ i ] = flpyWallColumn[ i ] - 1;

        if( flpyWallColumn[ i ] == 0 )
        {
            if( !flpyIsHoleRow( flpyWallHole[ i ], flpyBirdRow ) ) return true;
        }
    }
    return false;
}

void flpyInitGame()
{
    flpyBirdRow = 0;
    int i;
    for( i = 0; i < FLPY_WALL_COUNT; i++ ) flpyWallColumn[ i ] = -1;
    flpyNextWallSlot = 0;
    flpyElapsedFrames = 0;
    flpyMoveCount = 0;
    flpyTicksPerMove = flpyComputeTicksPerMove();
    flpyMoveCountdown = flpyTicksPerMove;
    flpyPrevUp = false;
    flpyPrevDown = false;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int[128] flpyPageBuffer;

// Composites one physical page (8 real screen rows) across all 128 real
// columns - resolves each of the 16 grid columns' own wall state once
// (O(walls) per grid column, not per pixel), then always finishes by
// overwriting the bird's own cell last, matching upstream's own explicit
// "redraw the bird on top of any wall at column 0" design intent (see this
// file's own header comment).
void flpyComposeRow( int page )
{
    int gridCol;
    for( gridCol = 0; gridCol < FLPY_GRID_COLS; gridCol++ )
    {
        int wallIdx = -1;
        int i;
        for( i = 0; i < FLPY_WALL_COUNT; i++ )
          if( flpyWallColumn[ i ] == gridCol ) wallIdx = i;

        if( wallIdx >= 0 && !flpyIsHoleRow( flpyWallHole[ wallIdx ], page ) )
        {
            int col;
            for( col = 0; col < 8; col++ )
              flpyPageBuffer[ gridCol * 8 + col ] = flpyWallTile[ col ];
        }
        else
        {
            int col;
            for( col = 0; col < 8; col++ )
              flpyPageBuffer[ gridCol * 8 + col ] = 0;
        }
    }

    if( page == flpyBirdRow )
    {
        int col;
        for( col = 0; col < 8; col++ )
          flpyPageBuffer[ col ] = flpyBirdTile[ col ];
    }
}

#define FLPY_MODE_ATTRACT 0
#define FLPY_MODE_PLAYING 1
#define FLPY_MODE_GAMEOVER 2

void flpyRenderFrame( int mode )
{
    md_beginFrame();
    int col, page;
    for( page = 0; page < 8; page++ )
    {
        if( mode == FLPY_MODE_GAMEOVER )
        {
            for( col = 0; col < 128; col++ )
              md_drawColumn( col, page, flpyGameOver[ page * 128 + col ] );
            continue;
        }

        if( mode == FLPY_MODE_ATTRACT )
        {
            for( col = 0; col < 128; col++ ) flpyPageBuffer[ col ] = 0;

            if( page == 2 )
              for( col = 0; col < 8; col++ ) flpyPageBuffer[ col ] = flpyBirdTile[ col ];
            if( page == 2 )
              for( col = 0; col < 8; col++ ) flpyPageBuffer[ 40 + col ] = flpyWallTile[ col ];

            if( page == 4 )
            {
                int* t = "FLAPPY BIRD";
                int col2;
                for( col2 = 0; col2 < 128; col2++ )
                  flpyPageBuffer[ col2 ] = flpyPageBuffer[ col2 ] | flpyTextByteAt( t, 11, 22, col2 );
            }
            if( page == 5 )
            {
                int* t = "BY ALEX WULFF";
                int col2;
                for( col2 = 0; col2 < 128; col2++ )
                  flpyPageBuffer[ col2 ] = flpyPageBuffer[ col2 ] | flpyTextByteAt( t, 13, 19, col2 );
            }
            if( page == 7 )
            {
                int* t = "PRESS FIRE";
                int col2;
                for( col2 = 0; col2 < 128; col2++ )
                  flpyPageBuffer[ col2 ] = flpyPageBuffer[ col2 ] | flpyTextByteAt( t, 10, 34, col2 );
            }

            for( col = 0; col < 128; col++ )
              md_drawColumn( col, page, flpyPageBuffer[ col ] );
            continue;
        }

        flpyComposeRow( page );
        for( col = 0; col < 128; col++ )
          md_drawColumn( col, page, flpyPageBuffer[ col ] );
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define FLPY_STATE_ATTRACT   0
#define FLPY_STATE_PLAYING   1
#define FLPY_STATE_GAMEOVER  2

int flpyState;
bool flpyPrevFire;

void flpyBeginAttract()
{
    flpyPrevFire = false;
    flpyState = FLPY_STATE_ATTRACT;
}

void flpyBeginPlaying()
{
    flpyInitGame();
    flpyState = FLPY_STATE_PLAYING;
}

void flpyBeginGameOver()
{
    flpyPrevFire = true; // arm against the same press that caused the collision
    flpyState = FLPY_STATE_GAMEOVER;
}

void gameFlappyBird_init()
{
    flpyBeginAttract();
}

void gameFlappyBird_forceRedraw()
{
    if( flpyState == FLPY_STATE_PLAYING ) flpyRenderFrame( FLPY_MODE_PLAYING );
    else if( flpyState == FLPY_STATE_GAMEOVER ) flpyRenderFrame( FLPY_MODE_GAMEOVER );
    else flpyRenderFrame( FLPY_MODE_ATTRACT );
}

void gameFlappyBird_update()
{
    if( flpyState == FLPY_STATE_ATTRACT )
    {
        bool fireNow = isFirePressed();
        if( fireNow && !flpyPrevFire )
        {
            flpyBeginPlaying();
            flpyRenderFrame( FLPY_MODE_PLAYING );
            return;
        }
        flpyPrevFire = fireNow;
        flpyRenderFrame( FLPY_MODE_ATTRACT );
    }
    else if( flpyState == FLPY_STATE_PLAYING )
    {
        flpyElapsedFrames++;

        bool upNow = isUpPressed();
        if( upNow && !flpyPrevUp && flpyBirdRow > 0 ) flpyBirdRow--;
        flpyPrevUp = upNow;

        bool downNow = isDownPressed();
        if( downNow && !flpyPrevDown && flpyBirdRow < 7 ) flpyBirdRow++;
        flpyPrevDown = downNow;

        flpyMoveCountdown--;
        bool collided = false;
        if( flpyMoveCountdown <= 0 )
        {
            collided = flpyMoveWallsStep();
            flpyMoveCount++;
            if( flpyMoveCount % 4 == 0 ) flpySpawnWall();
            flpyTicksPerMove = flpyComputeTicksPerMove();
            flpyMoveCountdown = flpyTicksPerMove;
        }

        flpyRenderFrame( FLPY_MODE_PLAYING );

        if( collided )
        {
            flpyBeginGameOver();
            flpyRenderFrame( FLPY_MODE_GAMEOVER );
        }
    }
    else // FLPY_STATE_GAMEOVER
    {
        bool fireNow = isFirePressed();
        if( fireNow && !flpyPrevFire )
        {
            flpyBeginAttract();
            flpyRenderFrame( FLPY_MODE_ATTRACT );
            return;
        }
        flpyPrevFire = fireNow;
        flpyRenderFrame( FLPY_MODE_GAMEOVER );
    }
}

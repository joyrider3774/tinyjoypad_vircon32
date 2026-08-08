// Helicopter (Finn Harms / "innif", GPLv3). A classic "hold to fly"
// cave-dodging game - hold Up (or Right) to apply upward thrust against
// constant gravity, weaving the helicopter through a smoothly, randomly
// varying cave silhouette that scrolls past faster the longer you
// survive.
//
// Ported from `more games/Arduino-Game-System/GameSystem/helicopter.cpp`/
// `.h` - part of Finn Harms' own "Mini Arcade Gaming System" (a real
// ESP8266 D1 Mini + 128x64 SSD1306 handheld with 8 built-in games) -
// picked from this project's own "sixth beyond-scope discovery pass"
// candidate list (see that section of this file's own history for how
// it was found and originally flagged as needing a real code-level
// check before committing, rather than dismissed or ported on genre
// similarity alone).
//
// **Confirmed genuinely distinct from both of this project's own
// already-shipped "navigate a scrolling cave" games, by direct reading
// rather than genre-name comparison alone** (the exact discipline this
// project has applied to every prior "looks like a duplicate" candidate):
// HollowSeeker (Obono) moves its player by *hopping between fixed ledge
// heights* as the cave's own floor rises and falls beneath a fixed
// forward-scroll rate - never continuous gravity/velocity physics at
// all. UFO (Ilya Titov, already shipped) *does* use continuous hold-to-
// fly gravity physics, but its obstacles are discrete walls with a gap
// to weave through, separated by open clear space - not a continuous
// top-and-bottom cave boundary present at every single x position the
// way Helicopter's own `CaveSegment[32]` array is. Helicopter is a real
// hybrid of the two shapes (UFO's own control model + a HollowSeeker-
// style always-present cave silhouette) with its own genuinely different
// generation algorithm (a smooth random walk - each new segment's own
// gap position/size offset by a small random delta from the *previous*
// segment, `generateCave()`/`updateCave()`) - a real, distinct codebase,
// not a reskin of either.
//
// Same `int[1024]` page-byte framebuffer layout as every other port in
// this project (`heliFrameBuffer`). Every draw primitive writes directly
// into the framebuffer via inlined page/bit math from the start,
// `heliDisplayClear()` is a flat 1024-word fill, and `heliDrawChar()`
// inlines its own font lookup - applying this project's own now-standard
// "optimize proactively, don't wait for a CPU report" lessons from the
// first draft, per standing instruction. Font data is copied verbatim
// from this project's own already-verified myfont-family extraction
// (Road Rush/DFlight/MRunnr/Asteroid) rather than retyped - upstream
// itself has no bitmap font at all (`Adafruit_GFX`'s own real library
// provides `display.print()`'s built-in font, not present anywhere in
// this repo's own source, so there was nothing to extract even if
// wanted) - a deliberate, documented substitution rather than an attempt
// at pixel-for-pixel fidelity to a font this repo never actually ships
// in source form.
//
// **Every `Adafruit_GFX` draw call this game actually uses** -
// `fillRect()`, a single `drawLine()` call that is always horizontal
// (same y at both endpoints, confirmed by reading the one real call
// site rather than assumed), `drawPixel()`, and `print()` for the score
// - maps directly onto this project's own already-proven inlined
// framebuffer primitives; no general Bresenham line algorithm was
// needed since the one real line this game ever draws (the rotor) never
// has a slope.
//
// Not `tinyJoypadShim`/`obonoCoreShim` lineage - genuine bespoke ESP8266
// hardware with 4 plain digital buttons (Up/Down/Left/Right, no
// dedicated fire button at all - confirmed by reading `config.h`
// directly), needing no new shim - `isUpPressed()`/`isRightPressed()`
// (both apply lift, matching upstream's own `buttons.up || buttons.right`
// check exactly) already cover the whole real input surface Helicopter
// itself reads.
//
// **A genuine, real-millisecond internal tick gate**, decoupled from the
// outer game-manager's own `delay(16)` (~60fps) loop: upstream's own
// `update()` only actually advances physics/collision/score once every
// ~30ms (`currentTime - lastUpdate > 30`, ~33fps), independent of the
// outer loop's own faster real rate. `HELI_TICK_DIVISOR` (60/30, a clean
// round number close enough to upstream's own already-approximate
// "~33 FPS" comment, matching this project's own standing practice of
// not chasing an exact non-round real-hardware rate when the original
// comment itself is only approximate) gates the *entire* tick here -
// input, physics, and the redraw together - rather than only the
// physics step. Upstream itself keeps redrawing every outer-loop
// iteration regardless (a real per-hardware-cost decision that doesn't
// carry over to this platform's own budget - see the CPU-optimization
// note on `heliUpdatePlaying()` below for why a plain "physics-only
// throttle, redraw every real frame" port of that shape turned out to
// be a real, fixable waste here).
//
// **A real, deliberate upstream simplification, preserved rather than
// "fixed"**: `checkCollisions()` always checks the cave segment at a
// single *fixed* array index (`HELI_X / 4`), not one recomputed from
// the helicopter's own true on-screen position relative to the current
// scroll offset - meaning the exact collision boundary can be off by up
// to a few pixels from where the cave visually appears to be at any
// given instant of the scroll cycle. Ported exactly as observed, not
// "corrected" into a more precise recomputation upstream itself never
// performs.
//
// **Real EEPROM high-score persistence restored**, matching this
// project's own established precedent (confirmed via direct reading
// that `highscore.cpp`/`.h` genuinely wires `GAME_HELICOPTER`'s own
// score into a real magic-number-and-checksum-guarded EEPROM read/write,
// not dead scaffolding) - a plain 2-byte score via
// `eeprom_read_word`/`eeprom_write_word` at address 0 within this game's
// own eepromShim-resolved slot, the same "simple 2-byte high score"
// shape already used for 9+ other games in this project, including the
// standard 65535 virgin-slot guard.
//
// A genuine attract screen was added (upstream has none of its own -
// Helicopter is only ever reached through the game-manager's own
// separate menu system, which is out of scope here, matching every
// other beyond-scope port's own now-standard "add a title/attract gate"
// convention) alongside a faithful port of upstream's own real "GAME
// OVER" screen (Score/Best/optional "NEW HIGHSCORE!"/restart prompt,
// `GameManager::showGameOver()`).

// -----------------------------------------------------------------------------
// Data tables (font data reused verbatim from Road Rush/DFlight/MRunnr/
// Asteroid's own already-verified myfont.hpp extraction - see header
// comment for why no game-specific font table exists to extract here)
// -----------------------------------------------------------------------------

int[6144] heliFontData = {
0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
0,1,1,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,
0,0,0,1,0,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,1,0,0,0,
0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,1,1,1,0,
1,0,0,1,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,1,0,1,0,0,0,
0,1,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,0,0,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,1,1,0,0,
0,0,0,1,0,1,1,0,0,0,0,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,0,0,1,0,0,0,0,0,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,1,0,1,0,
0,0,1,0,1,0,0,1,0,0,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,1,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,1,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,1,1,1,0,0,
0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,0,1,0,0,0,
0,1,1,1,1,1,1,0,0,0,1,1,1,1,0,0,0,1,1,1,1,1,1,0,0,0,1,1,1,1,0,0,
0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,0,1,1,0,0,0,
0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,
0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,
0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,1,1,1,1,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,1,0,1,0,0,0,
0,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,1,1,1,1,0,0,
0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,1,0,0,0,
0,1,1,1,1,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,
0,0,0,0,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,0,0,0,1,0,0,1,0,0,0,
0,0,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,
0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,1,0,0,1,1,1,0,0,
0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,1,1,1,1,0,0,
0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
0,1,1,1,1,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,1,0,0,
0,0,0,1,1,1,0,0,0,1,1,1,1,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,1,0,0,0,
0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,1,1,1,1,0,0,
0,0,1,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,1,0,
0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1,1,1,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
0,0,0,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,1,1,1,0,0,0,0,1,1,1,1,0,0,0,
0,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,1,1,1,0,0,0,0,1,0,0,0,0,1,0,
0,0,1,1,1,1,1,0,0,0,0,1,1,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,
0,1,0,0,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,1,1,1,0,0,0,1,1,1,1,0,0,0,
0,0,0,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,
0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,0,0,0,
0,1,1,0,0,0,1,1,0,1,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,
0,0,1,0,0,1,0,0,0,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
0,1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,
0,1,0,1,0,1,0,1,0,1,0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,1,1,1,1,0,0,0,
0,0,1,1,1,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,
0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,1,1,1,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,0,0,0,0,0,
0,1,0,0,1,0,0,1,0,1,0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,0,0,
0,1,0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,
0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,
0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,0,0,0,
0,1,0,0,0,0,0,1,0,1,0,0,0,1,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,0,0,
0,1,0,0,0,0,1,0,0,1,1,1,1,0,0,0,0,0,1,1,1,0,0,0,0,1,1,1,1,0,0,0,
0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,1,0,0,0,0,1,0,
0,0,1,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,1,1,1,1,1,0,
0,1,0,0,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,1,1,1,0,0,0,1,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,1,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,1,1,1,0,0,0,1,1,1,1,1,1,1,
0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,1,0,0,0,0,1,0,
0,1,0,0,0,0,0,1,0,1,1,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,0,0,0,0,0,0,
0,0,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,
0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,0,1,0,0,1,0,0,
0,0,1,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,1,0,0,1,1,1,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,
0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,1,0,0,1,0,0,1,0,0,0,1,1,0,0,0,
0,0,0,1,0,1,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,1,0,0,1,0,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,0,0,0,
0,1,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,1,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,1,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,
0,1,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,1,1,0,0,0,1,1,0,0,1,0,0,1,0,0,
0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,1,1,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1,0,0,0,
0,0,1,1,1,1,0,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,0,1,0,
0,0,0,0,1,0,0,0,0,1,1,1,1,1,1,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,1,0,
0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,1,0,0,0,1,1,1,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,
0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1,1,1,1,0,0,0,0,0,1,1,1,0,0,
0,0,1,1,1,1,1,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,1,1,0,1,0,0,
0,0,1,1,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,1,0,0,0,0,0,1,0,0,1,0,0,
0,0,0,1,0,0,0,0,0,1,1,0,0,1,1,0,0,0,1,0,1,1,1,0,0,0,1,1,1,1,0,0,
0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,
0,1,0,0,0,0,1,0,0,1,1,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,0,0,1,1,0,0,
0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,
0,0,0,1,0,0,0,0,0,1,0,1,1,0,1,0,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,0,
0,0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0,0,
0,1,0,0,0,1,1,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,
0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,1,0,0,0,0,
0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,1,0,
0,0,0,0,0,0,0,0,0,0,1,1,1,0,1,0,0,1,0,1,1,1,0,0,0,0,0,1,1,1,0,0,
0,0,1,1,1,0,1,0,0,0,1,1,1,1,0,0,0,0,0,1,0,0,0,0,0,0,1,1,0,1,0,0,
0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1,1,0,0,
0,0,0,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,1,1,1,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
0,0,0,0,1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
0,1,0,1,1,1,0,0,0,0,1,1,1,0,1,0,0,0,1,0,1,1,0,0,0,0,1,0,0,0,0,0,
0,0,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,1,
0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,1,1,1,1,0,0,0,0,1,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
0,1,1,0,0,0,1,0,0,1,0,0,0,1,1,0,0,0,1,1,0,0,1,0,0,0,0,1,1,0,0,0,
0,0,0,1,0,0,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,1,
0,0,1,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,1,0,0,1,1,1,1,1,1,0,
0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,
0,0,0,1,0,0,0,0,0,0,1,0,0,1,1,0,0,0,1,0,0,1,0,0,0,1,0,1,1,0,1,0,
0,0,0,1,1,0,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,
0,1,1,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,1,1,1,0,0,0,
0,0,0,1,1,0,0,0,0,0,0,1,1,0,1,0,0,0,0,1,1,0,0,0,0,1,1,0,0,1,1,0,
0,1,1,0,0,1,1,0,0,0,0,0,1,0,0,0,0,0,1,1,1,1,0,0,0,0,0,1,1,1,0,0,
0,0,0,0,1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// -----------------------------------------------------------------------------
// Font index (same 16x6-glyph, 8x8-cell layout/table as every other
// tonym128-family port in this project)
// -----------------------------------------------------------------------------

int heliFontIndex( int ch )
{
    if( ch == 32 ) return 99; // space
    if( ch == 33 ) return 0; if( ch == 34 ) return 1; if( ch == 35 ) return 2;
    if( ch == 36 ) return 3; if( ch == 37 ) return 4; if( ch == 38 ) return 5;
    if( ch == 39 ) return 6; if( ch == 40 ) return 7; if( ch == 41 ) return 8;
    if( ch == 42 ) return 9; if( ch == 43 ) return 10; if( ch == 44 ) return 11;
    if( ch == 45 ) return 12; if( ch == 46 ) return 13; if( ch == 47 ) return 14;
    if( ch == 60 ) return 27; if( ch == 61 ) return 28; if( ch == 62 ) return 28;
    if( ch >= 48 && ch <= 57 ) return 15 + ( ch - 48 );
    if( ch >= 65 && ch <= 90 ) return 32 + ( ch - 65 );
    if( ch >= 97 && ch <= 122 ) return 65 + ( ch - 97 );
    return 0; // '!' fallback, matching every other port's own default
}

// -----------------------------------------------------------------------------
// Pixel framebuffer + draw primitives, all writing directly into
// heliFrameBuffer via inlined page/bit math from the start (see header
// comment).
// -----------------------------------------------------------------------------

int[1024] heliFrameBuffer;

void heliDisplayClear( void )
{
    int i;
    for( i = 0; i < 1024; i++ ) heliFrameBuffer[ i ] = 0;
}

void heliSetPixel( int x, int y )
{
    if( x < 0 || x > 127 || y < 0 || y > 63 ) return;
    int idx = x + ( ( y >> 3 ) * 128 );
    int bit = 1 << ( y & 7 );
    heliFrameBuffer[ idx ] = heliFrameBuffer[ idx ] | bit;
}

// fillRect() - matches Adafruit_GFX's own fillRect(x,y,w,h) semantics
// (a solid rectangle, w/h counted from x/y inclusive of the start edge).
// Rewritten to compute one bitmask per (column,page) instead of setting
// each pixel's own bit individually - a real, structural CPU
// optimization found via direct user report ("optimize the game
// itself"). heliDrawCave()'s own walls are tall, solid, single-color
// rectangles (up to ~40 rows), so the original per-row loop was doing
// up to ~40 individual bit-set operations per column where at most 8
// (one per overlapping hardware page) are actually needed - the same
// pixel count ends up set either way, just computed in one masked OR
// per page instead of one shift-and-OR per row.
void heliFillRect( int x, int y, int w, int h )
{
    if( h <= 0 ) return;
    int y2 = y + h - 1; // inclusive bottom row
    int i, p, idx, pageTop, pageBottom, lo, hi, mask;
    for( i = x; i < x + w; i++ )
    {
        if( i < 0 || i > 127 ) continue;
        for( p = 0; p < 8; p++ )
        {
            pageTop = p * 8;
            pageBottom = pageTop + 7;
            lo = y; if( lo < pageTop ) lo = pageTop;
            hi = y2; if( hi > pageBottom ) hi = pageBottom;
            if( lo > hi ) continue;
            mask = ( ( 1 << ( hi - lo + 1 ) ) - 1 ) << ( lo - pageTop );
            idx = i + p * 128;
            heliFrameBuffer[ idx ] = heliFrameBuffer[ idx ] | mask;
        }
    }
}

// drawLine(), specialized to the one shape this game ever actually draws
// (a horizontal line, same y at both endpoints - confirmed by reading
// the single real call site rather than assumed) - not a general
// Bresenham implementation, since nothing here ever needs one.
void heliDrawHLine( int x1, int x2, int y )
{
    int i, lo, hi, idx, bit;
    if( y < 0 || y > 63 ) return;
    lo = x1; hi = x2;
    if( lo > hi ) { i = lo; lo = hi; hi = i; }
    bit = 1 << ( y & 7 );
    for( i = lo; i <= hi; i++ )
    {
        if( i < 0 || i > 127 ) continue;
        idx = i + ( ( y >> 3 ) * 128 );
        heliFrameBuffer[ idx ] = heliFrameBuffer[ idx ] | bit;
    }
}

void heliDrawChar( int ch, int x, int y, int backFill )
{
    int col, row, glyph, idx, bit, py, line, gcol, isSpace, rowBase, pageBase;
    glyph = heliFontIndex( ch );
    isSpace = glyph == 99;
    if( isSpace && !backFill ) return;
    line = ( glyph / 16 ) * 8;
    gcol = ( glyph % 16 ) * 8;
    for( row = 0; row < 8; row++ )
    {
        py = y + row;
        if( py < 0 || py > 63 ) continue;
        bit = 1 << ( py & 7 );
        rowBase = ( line + row ) * 128 + gcol;
        pageBase = ( py >> 3 ) * 128 + x;
        for( col = 0; col < 8; col++ )
        {
            if( x + col < 0 || x + col > 127 ) continue;
            idx = pageBase + col;
            if( !isSpace && heliFontData[ rowBase + col ] ) heliFrameBuffer[ idx ] = heliFrameBuffer[ idx ] | bit;
            else if( backFill ) heliFrameBuffer[ idx ] = heliFrameBuffer[ idx ] & ( 0xFF - bit );
        }
    }
}

void heliDrawString( int* text, int x, int y, int backFill )
{
    int i;
    i = 0;
    while( text[ i ] != 0 )
    {
        heliDrawChar( text[ i ], x + ( 8 * i ), y, backFill );
        i = i + 1;
    }
}

// Centered text, matching upstream's own drawCenteredText() - text
// length via the same null-terminated walk heliDrawString() already
// uses, not a separate strlen() call.
void heliDrawCentered( int* text, int y, int backFill )
{
    int len = 0;
    while( text[ len ] != 0 ) len = len + 1;
    int x = ( 128 - len * 8 ) / 2;
    heliDrawString( text, x, y, backFill );
}

// -----------------------------------------------------------------------------
// Game state
// -----------------------------------------------------------------------------

enum HeliState
{
    HELI_STATE_ATTRACT = 0,
    HELI_STATE_PLAY = 1,
    HELI_STATE_GAME_OVER = 2
};

#define HELI_TICK_DIVISOR 2 // ~30fps, matching upstream's own already-approximate "~33 FPS" internal gate - see header comment
#define HELI_SIZE 4
#define HELI_X 15
#define HELI_CAVE_SEGMENTS 32
#define HELI_GRAVITY 0.15
#define HELI_LIFT -1.0
// Real, throttled ticks (see HELI_TICK_DIVISOR above) run at ~30fps, so
// 30 of them is a 1-second "ready" hold - added at direct user request:
// draw the very first gameplay frame (starting cave layout + helicopter
// at rest) and hold it visibly for a full second before gravity/physics
// actually begin, giving the player a moment to see the starting layout
// rather than falling immediately on the very first tick after leaving
// the attract screen.
#define HELI_READY_TICKS 30

int heliState;
int heliTickSkipCounter;
int heliKeyUp, heliKeyRight, heliFireEdge, heliPrevFire;

float heliY, heliVelocity;
int heliScore;
int heliGameOver;
float heliGameSpeed;
float heliCaveOffset;
int heliHiScore;
int heliNewHighscore;
int heliReadyTicks;

int[ HELI_CAVE_SEGMENTS ] heliCaveTop;
int[ HELI_CAVE_SEGMENTS ] heliCaveGap;
// heliCaveBottom isn't tracked separately - it's always
// (64 - top - gap), derived on demand, matching upstream's own
// bottomHeight field exactly but without the redundant storage.

int heliCaveBottomOf( int i )
{
    return 64 - heliCaveTop[ i ] - heliCaveGap[ i ];
}

// -----------------------------------------------------------------------------
// Cave generation - direct port of generateCave()/updateCave(), using
// arand() in place of Arduino's own random(min,max) (exclusive of max,
// so random(-3,4) becomes arand(7)-3, and random(-2,3) becomes
// arand(5)-2 - matching the exact same inclusive range either way).
// -----------------------------------------------------------------------------

void heliGenerateCave( void )
{
    int currentGap = 25;
    int currentTop = 15;
    int i;
    for( i = 0; i < HELI_CAVE_SEGMENTS; i++ )
    {
        heliCaveGap[ i ] = currentGap + ( arand( 7 ) - 3 );
        if( heliCaveGap[ i ] < 18 ) heliCaveGap[ i ] = 18;
        if( heliCaveGap[ i ] > 35 ) heliCaveGap[ i ] = 35;

        heliCaveTop[ i ] = currentTop + ( arand( 5 ) - 2 );
        if( heliCaveTop[ i ] < 5 ) heliCaveTop[ i ] = 5;
        if( heliCaveTop[ i ] > 64 - heliCaveGap[ i ] - 5 ) heliCaveTop[ i ] = 64 - heliCaveGap[ i ] - 5;

        currentGap = heliCaveGap[ i ];
        currentTop = heliCaveTop[ i ];
    }
}

void heliUpdateCave( void )
{
    heliCaveOffset = heliCaveOffset + heliGameSpeed;

    if( heliCaveOffset >= 4 )
    {
        heliCaveOffset = 0;

        int i;
        for( i = 0; i < HELI_CAVE_SEGMENTS - 1; i++ )
        {
            heliCaveTop[ i ] = heliCaveTop[ i + 1 ];
            heliCaveGap[ i ] = heliCaveGap[ i + 1 ];
        }

        int last = HELI_CAVE_SEGMENTS - 1;
        heliCaveGap[ last ] = heliCaveGap[ last - 1 ] + ( arand( 7 ) - 3 );
        if( heliCaveGap[ last ] < 18 ) heliCaveGap[ last ] = 18;
        if( heliCaveGap[ last ] > 35 ) heliCaveGap[ last ] = 35;

        heliCaveTop[ last ] = heliCaveTop[ last - 1 ] + ( arand( 5 ) - 2 );
        if( heliCaveTop[ last ] < 5 ) heliCaveTop[ last ] = 5;
        if( heliCaveTop[ last ] > 64 - heliCaveGap[ last ] - 5 ) heliCaveTop[ last ] = 64 - heliCaveGap[ last ] - 5;
    }
}

void heliUpdateHelicopter( void )
{
    heliVelocity = heliVelocity + HELI_GRAVITY;
    heliY = heliY + heliVelocity;

    if( heliY < 0 ) { heliY = 0; heliVelocity = 0; }
    if( heliY > 64 - HELI_SIZE ) { heliY = 64 - HELI_SIZE; heliVelocity = 0; }
}

int heliCheckCollisions( void )
{
    int segmentIndex = HELI_X / 4;
    if( segmentIndex >= HELI_CAVE_SEGMENTS ) segmentIndex = HELI_CAVE_SEGMENTS - 1;

    if( heliY < heliCaveTop[ segmentIndex ] ) return 1;
    if( heliY + HELI_SIZE > heliCaveTop[ segmentIndex ] + heliCaveGap[ segmentIndex ] ) return 1;
    return 0;
}

// -----------------------------------------------------------------------------
// Rendering - direct port of drawCave()/drawHelicopter()/drawUI()
// -----------------------------------------------------------------------------

void heliDrawCave( void )
{
    int i, x, bottomY;
    for( i = 0; i < HELI_CAVE_SEGMENTS; i++ )
    {
        x = i * 4 - (int)heliCaveOffset;
        if( x >= -4 && x < 128 )
        {
            heliFillRect( x, 0, 4, heliCaveTop[ i ] );
            bottomY = heliCaveTop[ i ] + heliCaveGap[ i ];
            heliFillRect( x, bottomY, 4, heliCaveBottomOf( i ) );
        }
    }
}

void heliDrawHelicopter( void )
{
    int iy = (int)heliY;
    heliFillRect( HELI_X, iy, HELI_SIZE, HELI_SIZE );
    heliDrawHLine( HELI_X - 1, HELI_X + HELI_SIZE + 1, iy - 1 );
    heliSetPixel( HELI_X + HELI_SIZE, iy + 2 );
}

void heliDrawUI( void )
{
    int[16] numText;
    itoa( heliScore / 10, numText, 10 );
    heliDrawString( numText, 128 - 30, 0, 0 );
}

// -----------------------------------------------------------------------------
// Play state - direct port of HelicopterGame::init()/update()/draw()/
// handleInput()
// -----------------------------------------------------------------------------

void heliInitGame( void )
{
    heliY = 32; // SCREEN_HEIGHT/2
    heliVelocity = 0;
    heliScore = 0;
    heliGameOver = 0;
    heliGameSpeed = 1.5;
    heliCaveOffset = 0;
    heliGenerateCave();
}

void heliUpdatePlaying( void )
{
    // CPU optimization, applied per direct user report ("during
    // gameplay 100% cpu is reached"): originally only this function's
    // own physics/collision step was throttled, while input and the
    // full cave/helicopter/HUD redraw ran every real 60fps engine
    // frame - matching the "movement-only throttle, redraw stays at
    // native rate" shape this project uses for a handful of other
    // games. That shape only earns its keep when something *else* needs
    // to keep animating smoothly between throttled ticks (e.g. Tiny
    // Bert's own cosmetic lift/interlace counters) - Helicopter has
    // nothing like that: the cave scroll offset, the helicopter's own
    // position, and the score are all only ever touched inside this one
    // throttled step, so the whole scene is provably identical on every
    // "skipped" real frame. The real cost driver, found by inspection:
    // heliDrawCave()'s own up to 64 fillRect() calls (32 segments x 2
    // walls each) can together touch up to ~10,000 pixels in a single
    // frame when the cave's own random walk happens to produce a narrow
    // gap (tall top+bottom walls) - genuinely more raw pixel-writes than
    // this project's own other dense scenes (Asteroid's worst-case
    // rotation load, MRunnr's own close-wall raycaster case) - so
    // running that at a full, doubled 60fps instead of the genuine
    // 30fps this game's own physics actually ticks at was real,
    // unconditional waste. The whole tick (input read, physics, and the
    // redraw) is now gated together at the call site in
    // gameHelicopter_update() instead, the same "gate everything, skip
    // the whole tick" shape already used for the majority of throttled
    // games in this project (NumberPlace/HollowSeeker/t2048/Doc/Pacman/
    // Pipe/Tiny Mania/Jump Slime/TinyRoG/TinY Fi) - halving the redraw
    // rate outright rather than only halving the physics rate while
    // still paying the full redraw cost every real frame.
    if( heliKeyUp || heliKeyRight ) heliVelocity = HELI_LIFT;

    heliUpdateHelicopter();
    heliUpdateCave();

    if( heliCheckCollisions() ) heliGameOver = 1;

    heliScore = heliScore + 1;

    if( heliScore % 500 == 0 && heliGameSpeed < 3.0 ) heliGameSpeed = heliGameSpeed + 0.2;
}

void heliDisplayPlaying( void )
{
    heliDisplayClear();
    heliDrawCave();
    heliDrawHelicopter();
    heliDrawUI();
}

// -----------------------------------------------------------------------------
// Attract screen (added - upstream has none, reached only through the
// out-of-scope game-manager menu - same convention as every other
// beyond-scope port in this project)
// -----------------------------------------------------------------------------

void heliDrawAttract( void )
{
    heliDisplayClear();
    heliDrawCentered( "HELICOPTER", 16, 0 );
    heliDrawCentered( "BY FINN HARMS", 32, 0 );
    heliDrawCentered( "PRESS FIRE", 48, 0 );
}

// -----------------------------------------------------------------------------
// Game Over screen - direct port of GameManager::showGameOver()'s own
// real Score/Best/"NEW HIGHSCORE!"/restart-prompt layout.
// -----------------------------------------------------------------------------

void heliDrawGameOver( void )
{
    heliDisplayClear();
    heliDrawCentered( "GAME OVER", 0, 0 );

    int[16] numText;
    int[24] line;

    // No literal ':' here - this project's own shared myfont table (see
    // header comment) has no glyph mapped for it, and every other game
    // in this project happened to never need one - the first port to
    // actually try one found it silently falling back to '!' instead.
    strcpy( line, "Score " );
    itoa( heliScore, numText, 10 );
    strcat( line, numText );
    heliDrawCentered( line, 17, 0 );

    strcpy( line, "Best " );
    itoa( heliHiScore, numText, 10 );
    strcat( line, numText );
    heliDrawCentered( line, 27, 0 );

    if( heliNewHighscore ) heliDrawCentered( "NEW HIGHSCORE!", 37, 0 );

    heliDrawCentered( "FIRE TO RESTART", 55, 0 );
}

// -----------------------------------------------------------------------------
// State setup / transitions
// -----------------------------------------------------------------------------

void heliBeginAttract( void )
{
    heliState = HELI_STATE_ATTRACT;
}

void heliBeginPlay( void )
{
    heliInitGame();
    heliReadyTicks = HELI_READY_TICKS;
    heliState = HELI_STATE_PLAY;
}

void heliBeginGameOver( void )
{
    heliNewHighscore = 0;
    if( heliScore > heliHiScore )
    {
        heliHiScore = heliScore;
        heliNewHighscore = 1;
        eeprom_write_word( 0, heliHiScore );
    }
    heliState = HELI_STATE_GAME_OVER;
}

// -----------------------------------------------------------------------------
// Per-tick state dispatch
// -----------------------------------------------------------------------------

void heliUpdateAttract( void )
{
    heliDrawAttract();
    if( heliFireEdge ) heliBeginPlay();
}

void heliUpdatePlay( void )
{
    if( heliReadyTicks > 0 )
    {
        // Hold the very first gameplay frame (starting cave layout,
        // helicopter at rest) for a full second before physics starts -
        // see HELI_READY_TICKS' own comment. Render only, no gravity/
        // collision/score this tick.
        heliReadyTicks = heliReadyTicks - 1;
        heliDisplayPlaying();
        return;
    }

    heliUpdatePlaying();
    heliDisplayPlaying();
    if( heliGameOver ) heliBeginGameOver();
}

void heliUpdateGameOver( void )
{
    heliDrawGameOver();
    if( heliFireEdge ) heliBeginPlay();
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void heliRenderFrame( void )
{
    md_beginFrame();
    int col, page;
    for( page = 0; page < 8; page++ )
      for( col = 0; col < 128; col++ )
        md_drawColumn( col, page, heliFrameBuffer[ col + ( page * 128 ) ] );
}

void gameHelicopter_init( void )
{
    InitTinyJoypad();
    heliTickSkipCounter = 0;
    heliPrevFire = 0;
    heliFireEdge = 0;

    // Real EEPROM high-score restore - see header comment. Matching the
    // standard "simple 2-byte score" shape/virgin-slot guard already
    // used for 9+ other games in this project.
    heliHiScore = eeprom_read_word( 0 );
    if( heliHiScore == 65535 ) heliHiScore = 0;

    heliBeginAttract();
    heliDisplayClear();
}

void gameHelicopter_update( void )
{
    heliTickSkipCounter = heliTickSkipCounter + 1;
    if( heliTickSkipCounter < HELI_TICK_DIVISOR ) return;
    heliTickSkipCounter = 0;

    int fireNow = isFirePressed();
    heliFireEdge = fireNow && !heliPrevFire;
    heliPrevFire = fireNow;

    heliKeyUp = isUpPressed();
    heliKeyRight = isRightPressed();

    if( heliState == HELI_STATE_ATTRACT ) heliUpdateAttract();
    else if( heliState == HELI_STATE_PLAY ) heliUpdatePlay();
    else heliUpdateGameOver();

    heliRenderFrame();
}

void gameHelicopter_forceRedraw( void )
{
    if( heliState == HELI_STATE_ATTRACT ) heliDrawAttract();
    else if( heliState == HELI_STATE_PLAY ) heliDisplayPlaying();
    else heliDrawGameOver();
    heliRenderFrame();
}

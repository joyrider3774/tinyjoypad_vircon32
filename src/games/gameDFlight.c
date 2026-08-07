// dFlight (Tony M / "tonym128", GPLv3). A free-flight dodging game -
// steer a ship freely in all 4 directions through a field of scrolling
// cloud-shaped obstacles ("stars" in the upstream source, despite their
// real cloud-like sprite shapes), grazing one pushes you toward the
// bottom of the screen each tick until you either fly clear or get
// pushed off the bottom edge entirely, across levels of increasing
// obstacle count/speed/distance.
//
// Ported from `more games/BFlight/bsideFly.cpp`/`.hpp` - the second game
// pulled from tonym128's own "BFlight" bundle (after Road Rush/
// driveGame) - see this project's CLAUDE.md "sixth beyond-scope
// discovery pass" section for how the whole bundle was found, and the
// Road Rush section for why a multi-game bundle gets split into separate
// cartridge entries rather than ported as one lump (matching this
// project's own long-standing precedent - Obono's TinyJoypadWorks/UFO_
// Stacker_Attiny/UFO_Breakout_Arduino were all split the same way).
// `mazeRunner`/`mazeGenerator` remain the one still-unported real game
// left in this bundle.
//
// Menu title "DFLIGHT", lifted directly from the game's own in-source
// credit text (the intro scroller's own " -= dFlight =-" line), the same
// "read the game's own on-screen text for its real name" approach
// already used for Road Rush/Bat Bonanza/Falling Blocks. Credited "TONY
// M (TONYM128)" and MCU "ESP8266", matching Road Rush's own credit
// (confirmed the same repo/author/hardware, not re-derived).
//
// Every optimization lesson learned the hard way while shipping Road
// Rush was applied here from the very start, not retrofitted after a
// report: every draw primitive writes directly into the framebuffer via
// inlined page/bit math (no separate flySetPixel() call layer in the hot
// path), `flyDisplayClear()` is a flat 1024-word buffer fill (not 8192
// per-pixel calls), and `flyDrawChar()` inlines its own font-data lookup
// with a hoisted per-row constant and an early return for a space
// glyph with no backfill (a guaranteed no-op, skipped entirely rather
// than looping 64 times to find that out). Same `int[1024]` page-byte
// framebuffer layout as Road Rush/Gilbert in the Downland/Tiny Arena -
// upstream's own `ScreenBuff.consoleBuffer[8192]` is a full pixel-
// addressable `bool` array, converted to real hardware page-bytes only
// at the very end of a frame by the real SSD1306Brzo library, the same
// rendering foundation Road Rush's own header comment covers in full.
//
// Genuinely simpler than Road Rush's own render loop - this game has no
// equivalent of the "background gets almost entirely overdrawn by
// something drawn right after it" pattern that needed real restructuring
// there (`drawObject()` only ever sets pixels where a sprite is
// nonzero, never touches the rest of the buffer, so there is no
// redundant-overdraw case at all to guard against here).
//
// Not `tinyJoypadShim`/`obonoCoreShim` lineage - genuine bespoke ESP8266
// hardware (`SSD1306Brzo` + the same 6-discrete-button analog-touch-pin
// scheme as Road Rush) - needed no new shim, upstream's own direct
// `P1_Top/Bottom/Left/Right` mapping onto up/down/left/right ports
// straight onto `isUpPressed()`/etc with no remap needed this time
// (unlike Road Rush's own backwards brake/accelerate mapping, this is
// genuine 4-directional flight with no ambiguity). `P2_Left/Right/
// Bottom` (kick/punch/jump) are read into `Player1Keys` but never
// actually consulted anywhere else in the real game logic - confirmed
// dead by grep before dropping them, not wired to anything here.
//
// A real, if inert-looking, rejection-sampling pattern in every one of
// upstream's own `initStar()` random rolls (`x = max+1; while(x>max) x =
// 1+rand()%max;`) was traced through rather than ported literally: the
// loop's own first (and, by construction, only ever) iteration already
// computes a value in `[1,max]`, so the `while` condition can never
// actually re-trigger - ported as a direct `1 + arand(max)` call, the
// same "simplify a provably-equivalent construct" precedent already used
// for Wren Rollercoaster's own already-integer `floor()` calls.
//
// A genuine upstream oddity, preserved faithfully rather than "fixed":
// winning level 2 (`level` becomes 3 via `level += 1`) immediately
// truncates back down to `level = 2` and jumps straight to the outro
// scroller - meaning the real level-3 stage configuration defined in the
// scene-6 dispatch (20 obstacles, higher max velocity, a longer distance
// target) is dead code, never actually reached through normal play. Kept
// exactly as upstream has it, not "fixed" to actually let the player
// reach it, since there's no clear signal this was ever meant to be
// reachable rather than an intentional 2-level game with unused
// scaffolding for a level that was never wired up.
//
// Scene 5 ("Crash") is declared in upstream's own scene-dispatch switch
// but no code path anywhere in the file ever sets `scene = 5` - confirmed
// dead by a full-file grep, not implemented here (an empty case would
// have no observable behavior to port in the first place).
//
// A genuine attract/title screen was added (upstream has none, looping
// forever between intro/outro exactly like Road Rush's own upstream
// did) - matching this project's now-standard convention for every
// beyond-scope port with no native title gate.
//
// `restartFrameCounter`'s own post-death wait is already tick-counted in
// upstream (`frameCounter += 1` compared against a plain integer, no
// real-millisecond `checkTime()` call anywhere in this file at all,
// unlike Road Rush's own FLAG/LEVEL_SLIDER/WIN_LOSE states) - no
// real-time-to-tick conversion was needed for this port at all.
//
// A genuine ~30fps whole-tick throttle (`FLY_TICK_DIVISOR = 60/30`)
// applied from the start, matching Road Rush's own real
// `updateMinTime(33)` cap - both games share the exact same outer
// `game.cpp` loop in the original BFlight bundle, so the same real rate
// applies here too.

// -----------------------------------------------------------------------------
// Data tables (extracted via script from bsideFly.hpp, byte-diff
// verified against the real counts before use; font data reused
// verbatim from Road Rush's own already-verified extraction of the same
// upstream myfont.hpp - no cross-game-file sharing mechanism exists in
// this project, so each game keeps its own self-contained copy)
// -----------------------------------------------------------------------------

int[6144] flyFontData = {
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
// player: 192 values
int[192] flyPlayerSprite = {
1,1,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
1,1,1,0,0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,0,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,1,
0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
};

// star30x11: 330 values
int[330] flyStar30x11 = {
0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,1,1,0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,0,0,
};

// star20x6: 120 values
int[120] flyStar20x6 = {
0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,1,
0,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,1,0,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};

// star10x4: 40 values
int[40] flyStar10x4 = {
0,1,1,1,0,0,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,
1,1,1,1,1,1,0,0,
};


// -----------------------------------------------------------------------------
// Pixel framebuffer (same layout/technique as Road Rush/Gilbert in the
// Downland - one int per SSD1306 page-byte, bit N = row N within page)
// -----------------------------------------------------------------------------

int[1024] flyFrameBuffer;

void flyClearBuffer( void )
{
    int i;
    for( i = 0; i < 1024; i++ ) flyFrameBuffer[ i ] = 0;
}

int flyGetPixel( int x, int y )
{
    if( x < 0 || x > 127 || y < 0 || y > 63 ) return 0;
    int idx = x + ( ( y >> 3 ) * 128 );
    int bit = 1 << ( y & 7 );
    if( flyFrameBuffer[ idx ] & bit ) return 1;
    return 0;
}

// -----------------------------------------------------------------------------
// Font (shared 16x6-glyph, 8x8-cell layout, same table/index scheme as
// Road Rush's own already-verified myfont.hpp extraction)
// -----------------------------------------------------------------------------

int flyFontIndex( int ch )
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
    return 0; // '!' fallback, matching upstream's own font('!') default
}

// -----------------------------------------------------------------------------
// Draw primitives - every one writes directly into flyFrameBuffer via
// inlined page/bit math from the start (the exact fix Road Rush needed
// two rounds of user-reported CPU trouble to arrive at - applied here
// proactively instead).
// -----------------------------------------------------------------------------

void flyDisplayClear( int colour )
{
    int i, val;
    if( colour ) val = 0xFF; else val = 0;
    for( i = 0; i < 1024; i++ ) flyFrameBuffer[ i ] = val;
}

void flyDisplayInvert( void )
{
    int i;
    for( i = 0; i < 1024; i++ ) flyFrameBuffer[ i ] = ( 0xFF - flyFrameBuffer[ i ] ) & 0xFF;
}

// drawObject() - sets pixels only where the sprite is nonzero, leaves
// everything else untouched (no backfill variant needed anywhere in
// this game - unlike Road Rush's font/HUD drawing, nothing here ever
// needs to punch a background hole through a sprite's own zero cells).
void flyDrawObject( int dx, int dy, int w, int h, int* sprite )
{
    int i, j, counter, idx, bit;
    counter = 0;
    for( j = dy; j < dy + h; j++ )
    {
        if( j < 0 || j > 63 ) { counter = counter + w; continue; }
        bit = 1 << ( j & 7 );
        for( i = dx; i < dx + w; i++ )
        {
            if( i >= 0 && i <= 127 && sprite[ counter ] )
            {
                idx = i + ( ( j >> 3 ) * 128 );
                flyFrameBuffer[ idx ] = flyFrameBuffer[ idx ] | bit;
            }
            counter = counter + 1;
        }
    }
}

void flyDisplayNoise( int amountInverse )
{
    int x, y, idx, bit;
    for( y = 0; y < 64; y++ )
    {
        bit = 1 << ( y & 7 );
        for( x = 0; x < 128; x++ )
        {
            if( amountInverse == 0 || ( x * y ) % amountInverse == 0 )
            {
                idx = x + ( ( y >> 3 ) * 128 );
                if( arand( 2 ) ) flyFrameBuffer[ idx ] = flyFrameBuffer[ idx ] | bit;
                else flyFrameBuffer[ idx ] = flyFrameBuffer[ idx ] & ( 0xFF - bit );
            }
        }
    }
}

// Raw linear-index pixel write, matching upstream's own
// consoleBuffer[WIDTH*y+x] direct indexing (the progress-bar/slider
// drawing) rather than a dim/object-based helper.
void flySetPixelIdx( int idx, int val )
{
    int x = idx % 128;
    int y = idx / 128;
    if( x < 0 || x > 127 || y < 0 || y > 63 ) return;
    int fbIdx = x + ( ( y >> 3 ) * 128 );
    int bit = 1 << ( y & 7 );
    if( val ) flyFrameBuffer[ fbIdx ] = flyFrameBuffer[ fbIdx ] | bit;
    else flyFrameBuffer[ fbIdx ] = flyFrameBuffer[ fbIdx ] & ( 0xFF - bit );
}

void flyDrawChar( int ch, int x, int y, int backFill )
{
    int col, row, glyph, idx, bit, py, line, gcol, isSpace, rowBase, pageBase;
    glyph = flyFontIndex( ch );
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
            if( !isSpace && flyFontData[ rowBase + col ] ) flyFrameBuffer[ idx ] = flyFrameBuffer[ idx ] | bit;
            else if( backFill ) flyFrameBuffer[ idx ] = flyFrameBuffer[ idx ] & ( 0xFF - bit );
        }
    }
}

void flyDrawString( int* text, int x, int y, int backFill )
{
    int i;
    i = 0;
    while( text[ i ] != 0 )
    {
        flyDrawChar( text[ i ], x + ( 8 * i ), y, backFill );
        i = i + 1;
    }
}

int flyRectCollision( int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh )
{
    if( ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by ) return 1;
    return 0;
}

int flyMaskCollision( int ax, int ay, int aw, int ah, int* aObj, int bx, int by, int bw, int bh, int* bObj )
{
    int x1, x2, y1, y2, y1Loc, y2Loc;
    for( x1 = ax; x1 < ax + aw; x1++ )
      for( x2 = bx; x2 < bx + bw; x2++ )
        if( x2 == x1 )
          for( y1 = ay; y1 < ay + ah; y1++ )
            for( y2 = by; y2 < by + bh; y2++ )
              if( y2 == y1 )
              {
                  y1Loc = ( y1 - ay ) * aw;
                  y2Loc = ( y2 - by ) * bw;
                  if( aObj[ ( x1 - ax ) + y1Loc ] == 1 && aObj[ ( x1 - ax ) + y1Loc ] == bObj[ ( x2 - bx ) + y2Loc ] ) return 1;
              }
    return 0;
}

// -----------------------------------------------------------------------------
// Game state
// -----------------------------------------------------------------------------

enum FlyState
{
    FLY_STATE_ATTRACT = 0,
    FLY_STATE_INTRO_SCROLL = 1,
    FLY_STATE_OUTRO_SCROLL = 2,
    FLY_STATE_TAKEOFF = 3,
    FLY_STATE_LAND = 4,
    FLY_STATE_STAGE = 5
};

#define FLY_FPS 30
#define FLY_TICK_DIVISOR ( 60 / FLY_FPS )
#define FLY_MAX_STARS 20
#define FLY_STARTYPES 3
#define FLY_STARMAXSPAWNDELAY 300
#define FLY_PLAYER_W 16
#define FLY_PLAYER_H 12

struct FlyStar
{
    int x;
    int y;
    int width;
    int height;
    int velocity;
    int spawnDelay;
    int collider;
};

int flyState;
int flyTickSkipCounter;
int flyPrevFire;
int flyFireEdge;

int flyKeyUp, flyKeyDown, flyKeyLeft, flyKeyRight;

int flyDistanceTarget;
int flyStarCount;
int flyStarMaxVelocity;
int flyLevel;
int flyWin;
int flyRestartTimer;
int flyRestartFrameCounter;

int flyFrameCounter;
int flyCollision;
int flyStarField;
int flyRunning;
int flyRestart;

int flyPlayerX, flyPlayerY;
int flyPlayerInPlay;

FlyStar[ FLY_MAX_STARS ] flyStars;

// -----------------------------------------------------------------------------
// Sound - no exact real equivalent exists (upstream plays a real WAV file
// via ESP8266Audio/I2S on collision, not a tone formula) - approximated
// with a short representative cue, matching Road Rush's own precedent
// for un-derivable hardware audio.
// -----------------------------------------------------------------------------

void flyCollisionSound( void )
{
    Sound( 90, 30 );
}

// -----------------------------------------------------------------------------
// Stars (obstacles)
// -----------------------------------------------------------------------------

void flyInitStar( int i )
{
    // Upstream's own "roll until in range" retry loops here always
    // resolve on their first iteration (1+rand()%max is already in
    // [1,max]) - simplified to a direct arand() call, the same
    // provably-equivalent-construct simplification already used for
    // Wren Rollercoaster's own already-integer floor() calls.
    flyStars[ i ].y = 1 + arand( 64 );
    // allowX is always false at every real call site in upstream
    // (confirmed by grep) - the dead branch (spawning at a random X) is
    // not ported, matching this project's "confirmed dead, don't port"
    // precedent.
    flyStars[ i ].x = 127;
    flyStars[ i ].velocity = 1 + arand( flyStarMaxVelocity );

    int t;
    t = 1 + arand( FLY_STARTYPES );
    if( t == 1 ) { flyStars[ i ].width = 30; flyStars[ i ].height = 11; }
    else if( t == 2 ) { flyStars[ i ].width = 20; flyStars[ i ].height = 6; }
    else { flyStars[ i ].width = 10; flyStars[ i ].height = 4; }

    flyStars[ i ].collider = 1;
    flyStars[ i ].spawnDelay = 1 + arand( FLY_STARMAXSPAWNDELAY );
}

void flyUpdateStar( int i )
{
    if( flyStars[ i ].velocity == 0 ) flyInitStar( i );

    if( flyStars[ i ].spawnDelay > 0 )
    {
        flyStars[ i ].spawnDelay = flyStars[ i ].spawnDelay - 1;
    }
    else if( flyFrameCounter % flyStars[ i ].velocity == 0 )
    {
        flyStars[ i ].x = flyStars[ i ].x - 1;
        if( flyStars[ i ].x + flyStars[ i ].width == 0 ) flyInitStar( i );
    }
}

// -----------------------------------------------------------------------------
// State setup / transitions
// -----------------------------------------------------------------------------

void flyResetGameState( void )
{
    flyStarField = 1;
    flyPlayerInPlay = 1;
    flyPlayerX = 0;
    flyPlayerY = ( 64 - FLY_PLAYER_H ) / 2;

    flyFrameCounter = 0;
    flyRunning = 1;
    flyRestart = 0;
}

void flyBeginTakeoff( void )
{
    flyStarCount = 0;
    flyResetGameState();
    flyPlayerX = -FLY_PLAYER_W;
    flyPlayerY = 64 + flyPlayerY;
    flyState = FLY_STATE_TAKEOFF;
}

void flyBeginLand( void )
{
    flyState = FLY_STATE_LAND;
}

void flyBeginStage( void )
{
    if( flyLevel == 4 )
    {
        // "Game has ended" per upstream's own case 4 - straight to outro.
        flyFrameCounter = 0;
        flyState = FLY_STATE_OUTRO_SCROLL;
        return;
    }

    if( flyLevel == 1 ) { flyStarCount = 5; flyStarMaxVelocity = 20; flyDistanceTarget = 2000; }
    else if( flyLevel == 2 ) { flyStarCount = 10; flyStarMaxVelocity = 30; flyDistanceTarget = 3000; }
    else if( flyLevel == 3 ) { flyStarCount = 20; flyStarMaxVelocity = 50; flyDistanceTarget = 4000; }
    else
    {
        // Upstream's own "default" fallback - provably unreachable in
        // real play (see this file's own header comment on the level==3
        // truncation quirk), but ported faithfully with a defensive
        // array-size clamp anyway, matching this project's own standing
        // practice of guarding a fixed-size array even against a
        // theoretically-unreachable case.
        flyStarCount = 10 * flyLevel;
        if( flyStarCount > FLY_MAX_STARS ) flyStarCount = FLY_MAX_STARS;
        flyStarMaxVelocity = 20 + 10 * flyLevel;
        flyDistanceTarget = 1000 * flyLevel;
    }

    int i;
    for( i = 0; i < flyStarCount; i++ ) flyInitStar( i );
    flyResetGameState();
    flyState = FLY_STATE_STAGE;
}

// -----------------------------------------------------------------------------
// Core per-tick stage logic (direct port of updateFly())
// -----------------------------------------------------------------------------

void flyUpdateFly( void )
{
    if( flyRestartTimer )
    {
        flyFrameCounter = flyFrameCounter + 1;
        if( flyFrameCounter > flyRestartFrameCounter ) flyRestart = 1;
    }

    if( flyRestart )
    {
        flyLevel = 1;
        flyRestart = 0;
        flyPlayerInPlay = 1;
        flyCollision = 0;
        flyRestartTimer = 0;
        flyBeginTakeoff();
        return;
    }

    if( !flyPlayerInPlay || !flyRunning ) return;

    flyFrameCounter = flyFrameCounter + 1;
    flyCollision = 0;

    if( flyStarField )
    {
        int i;
        for( i = 0; i < flyStarCount; i++ )
        {
            flyUpdateStar( i );
            if( !flyCollision && flyStars[ i ].collider )
            {
                if( flyRectCollision( flyPlayerX, flyPlayerY, FLY_PLAYER_W, FLY_PLAYER_H, flyStars[ i ].x, flyStars[ i ].y, flyStars[ i ].width, flyStars[ i ].height ) )
                {
                    int* starSprite;
                    if( flyStars[ i ].width == 30 ) starSprite = flyStar30x11;
                    else if( flyStars[ i ].width == 20 ) starSprite = flyStar20x6;
                    else starSprite = flyStar10x4;

                    if( flyMaskCollision( flyPlayerX, flyPlayerY, FLY_PLAYER_W, FLY_PLAYER_H, flyPlayerSprite, flyStars[ i ].x, flyStars[ i ].y, flyStars[ i ].width, flyStars[ i ].height, starSprite ) )
                    {
                        flyCollision = 1;
                        flyCollisionSound();
                        break;
                    }
                }
            }
        }
    }

    int completion;
    completion = ( ( flyFrameCounter * 100 ) / flyDistanceTarget ) / 25;

    if( flyCollision )
    {
        if( flyPlayerY + FLY_PLAYER_H >= 64 )
        {
            flyPlayerInPlay = 0;
            flyWin = 0;
            flyRestartTimer = 1;
            flyRestartFrameCounter = flyRestartFrameCounter + flyFrameCounter;
        }
    }

    if( completion == 4 )
    {
        flyPlayerInPlay = 0;
        flyWin = 1;
        flyLevel = flyLevel + 1;

        if( flyLevel == 3 )
        {
            // The real, faithfully-preserved upstream quirk documented in
            // this file's own header comment - winning level 2 truncates
            // straight to the outro instead of ever reaching a real
            // level-3 stage.
            flyLevel = 2;
            flyFrameCounter = 0;
            flyState = FLY_STATE_OUTRO_SCROLL;
            return;
        }
        else
        {
            flyBeginLand();
            return;
        }
    }

    if( flyCollision )
    {
        flyPlayerY = flyPlayerY + 1;
        flyPlayerX = flyPlayerX + 1;
        if( flyPlayerY >= 64 )
        {
            flyPlayerInPlay = 0;
            flyWin = 0;
        }
    }
    else
    {
        if( flyKeyDown ) flyPlayerY = flyPlayerY + 1;
        if( flyKeyUp ) flyPlayerY = flyPlayerY - 1;
        if( flyKeyLeft ) flyPlayerX = flyPlayerX - 1;
        if( flyKeyRight ) flyPlayerX = flyPlayerX + 1;
    }

    if( flyPlayerX + FLY_PLAYER_W > 128 ) flyPlayerX = 128 - FLY_PLAYER_W;
    if( flyPlayerY + FLY_PLAYER_H > 64 ) flyPlayerY = 64 - FLY_PLAYER_H;
    if( flyPlayerX < 0 ) flyPlayerX = 0;
    if( flyPlayerY < 0 ) flyPlayerY = 0;
}

// -----------------------------------------------------------------------------
// Rendering (direct port of displayFly()) - the game's own display
// function reads whichever state the just-run update logic left it in,
// the same real quirk upstream has (scene can change mid-tick and the
// very same tick's own render call already reflects it) - preserved
// faithfully rather than restructured away.
// -----------------------------------------------------------------------------

void flyDisplayFly( void )
{
    if( !flyRunning ) return;

    flyDisplayClear( 0 );
    flyDrawObject( flyPlayerX, flyPlayerY, FLY_PLAYER_W, FLY_PLAYER_H, flyPlayerSprite );

    if( flyStarField )
    {
        int i;
        for( i = 0; i < flyStarCount; i++ )
        {
            if( flyStars[ i ].width == 30 ) flyDrawObject( flyStars[ i ].x, flyStars[ i ].y, 30, 11, flyStar30x11 );
            else if( flyStars[ i ].width == 20 ) flyDrawObject( flyStars[ i ].x, flyStars[ i ].y, 20, 6, flyStar20x6 );
            else flyDrawObject( flyStars[ i ].x, flyStars[ i ].y, 10, 4, flyStar10x4 );
        }
    }

    if( flyState == FLY_STATE_STAGE )
    {
        if( flyRunning && flyCollision )
        {
            flyDisplayNoise( 9 );
            flyDisplayInvert();
        }

        int percent, drawline, draw;
        percent = ( flyFrameCounter * 100 ) / flyDistanceTarget;
        drawline = ( percent * 128 ) / 100;
        for( draw = 0; draw < drawline; draw++ )
        {
            flySetPixelIdx( draw, 1 );
            flySetPixelIdx( draw + 128, 1 );
            flySetPixelIdx( draw + 256, 1 );
        }

        if( !flyPlayerInPlay )
        {
            if( flyWin ) flyDrawString( "YOU WIN!", 32, 30, 0 );
            else flyDrawString( "GAME OVER", 32, 30, 0 );
            flyPlayerInPlay = 0;
            flyRunning = 0;
        }
    }
    if( flyState == FLY_STATE_TAKEOFF )
    {
        int[16] numText;
        int[24] line;
        strcpy( line, "Level " );
        itoa( flyLevel, numText, 10 );
        strcat( line, numText );
        flyDrawString( line, 40, 10, 1 );

        int counter, i;
        counter = 0;
        for( i = flyFrameCounter; i > 0; i-- )
        {
            if( counter < 128 / 2 )
            {
                counter = counter + 1;
                flySetPixelIdx( ( 128 * 5 ) + i, 1 );
                flySetPixelIdx( ( 128 * 20 ) - i, 1 );
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Takeoff / land animations
// -----------------------------------------------------------------------------

int flyFlyin( void )
{
    flyFrameCounter = flyFrameCounter + 1;
    if( flyPlayerY == ( 64 - FLY_PLAYER_H ) / 2 )
    {
        if( flyPlayerX == 0 ) return 0;
        flyPlayerX = flyPlayerX - 1;
        return 1;
    }
    flyPlayerX = flyPlayerX + 1;
    flyPlayerY = flyPlayerY - 1;
    return 1;
}

int flyFlyout( void )
{
    flyFrameCounter = flyFrameCounter + 1;
    if( flyPlayerY > 64 ) return 0;
    flyPlayerX = flyPlayerX + 1;
    flyPlayerY = flyPlayerY + 1;
    return 1;
}

// -----------------------------------------------------------------------------
// Intro/outro scrollers - same drawScroller() shape/quirk as Road Rush
// (see that file's own header comment for the full explanation): all 7
// lines always scroll, the 8th text slot is drawn TWICE at two different
// fixed y positions, and the 9th slot draws once as a fixed backfilled
// title line. Reproduced exactly, not "fixed".
// -----------------------------------------------------------------------------

int flyDrawScrollerIntro( void )
{
    flyDisplayClear( 0 );
    int y;
    y = 64 - flyFrameCounter + 10;

    flyDrawString( "The journey to", 0, y, 0 ); y = y + 8;
    flyDrawString( "DefCon starts", 0, y, 0 ); y = y + 8;
    flyDrawString( "with a single", 0, y, 0 ); y = y + 8;
    flyDrawString( "step.", 0, y, 0 ); y = y + 8;
    flyDrawString( "", 0, y, 0 ); y = y + 8;
    flyDrawString( "  Fly To Vegas  ", 0, y, 0 ); y = y + 8;
    flyDrawString( "   Good Luck!  ", 0, y, 0 ); y = y + 8;

    flyDrawString( "", 0, 0, 0 );
    flyDrawString( "", 0, 8, 0 );
    flyDrawString( " -= dFlight =-  ", 5, 2, 1 );

    return y >= -8;
}

int flyDrawScrollerOutro( void )
{
    flyDisplayClear( 0 );
    int y;
    y = 64 - flyFrameCounter + 10;

    flyDrawString( "Well done on", 0, y, 0 ); y = y + 8;
    flyDrawString( "completing the", 0, y, 0 ); y = y + 8;
    flyDrawString( "first leg of", 0, y, 0 ); y = y + 8;
    flyDrawString( "your journey.", 0, y, 0 ); y = y + 8;
    flyDrawString( "", 0, y, 0 ); y = y + 8;
    flyDrawString( "Keep your head", 0, y, 0 ); y = y + 8;
    flyDrawString( "high & go onward", 0, y, 0 ); y = y + 8;

    flyDrawString( "", 0, 0, 0 );
    flyDrawString( "", 0, 8, 0 );
    flyDrawString( " -= Congrats =- ", 5, 2, 1 );

    return y >= -8;
}

// -----------------------------------------------------------------------------
// Attract screen (added - upstream has none, see header comment)
// -----------------------------------------------------------------------------

void flyDrawAttract( void )
{
    flyDisplayClear( 0 );
    flyDrawString( "DFLIGHT", 32, 16, 0 );
    flyDrawString( "BY TONYM128", 20, 32, 0 );
    flyDrawString( "PRESS FIRE", 24, 48, 0 );
}

// -----------------------------------------------------------------------------
// State dispatch
// -----------------------------------------------------------------------------

void flyUpdateAttract( void )
{
    flyDrawAttract();
    if( flyFireEdge )
    {
        flyFrameCounter = 0;
        flyLevel = 1;
        flyState = FLY_STATE_INTRO_SCROLL;
    }
}

void flyUpdateIntroScroll( void )
{
    flyFrameCounter = flyFrameCounter + 1;
    if( !flyDrawScrollerIntro() )
    {
        flyBeginTakeoff();
    }
}

void flyUpdateOutroScroll( void )
{
    flyFrameCounter = flyFrameCounter + 1;
    if( !flyDrawScrollerOutro() )
    {
        // Matches upstream's own "return true" (whole game complete) -
        // back to a fresh title screen instead of Road Rush's own
        // outer-BFlight-chain handoff, since this port has no next
        // mini-game to hand control to.
        flyFrameCounter = 0;
        flyLevel = 1;
        flyState = FLY_STATE_ATTRACT;
    }
}

void flyUpdateTakeoff( void )
{
    if( !flyFlyin() )
    {
        flyBeginStage();
    }
    flyDisplayFly();
}

void flyUpdateLand( void )
{
    if( !flyFlyout() )
    {
        flyBeginTakeoff();
    }
    flyDisplayFly();
}

void flyUpdateStage( void )
{
    flyUpdateFly();
    flyDisplayFly();
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void flyRenderFrame( void )
{
    md_beginFrame();
    int col, page;
    for( page = 0; page < 8; page++ )
      for( col = 0; col < 128; col++ )
        md_drawColumn( col, page, flyFrameBuffer[ col + ( page * 128 ) ] );
}

void gameDFlight_init( void )
{
    InitTinyJoypad();
    flyState = FLY_STATE_ATTRACT;
    flyTickSkipCounter = 0;
    flyPrevFire = 0;
    flyFireEdge = 0;
    flyLevel = 1;
    // Matches upstream's own C++ in-class default member initializer
    // (`int restartFrameCounter = 100;`) - a real, one-time-only value
    // that then only ever grows across the whole session (see this
    // file's own header/flyUpdateFly() comments), never reset again.
    flyRestartFrameCounter = 100;
    flyRestartTimer = 0;
    flyRestart = 0;
    flyClearBuffer();
}

void gameDFlight_update( void )
{
    flyTickSkipCounter = flyTickSkipCounter + 1;
    if( flyTickSkipCounter < FLY_TICK_DIVISOR ) return;
    flyTickSkipCounter = 0;

    int fireNow = isFirePressed();
    flyFireEdge = fireNow && !flyPrevFire;
    flyPrevFire = fireNow;

    flyKeyUp = isUpPressed();
    flyKeyDown = isDownPressed();
    flyKeyLeft = isLeftPressed();
    flyKeyRight = isRightPressed();

    if( flyState == FLY_STATE_ATTRACT ) flyUpdateAttract();
    else if( flyState == FLY_STATE_INTRO_SCROLL ) flyUpdateIntroScroll();
    else if( flyState == FLY_STATE_OUTRO_SCROLL ) flyUpdateOutroScroll();
    else if( flyState == FLY_STATE_TAKEOFF ) flyUpdateTakeoff();
    else if( flyState == FLY_STATE_LAND ) flyUpdateLand();
    else flyUpdateStage();

    flyRenderFrame();
}

void gameDFlight_forceRedraw( void )
{
    if( flyState == FLY_STATE_ATTRACT ) flyDrawAttract();
    else if( flyState == FLY_STATE_INTRO_SCROLL ) flyDrawScrollerIntro();
    else if( flyState == FLY_STATE_OUTRO_SCROLL ) flyDrawScrollerOutro();
    else flyDisplayFly();
    flyRenderFrame();
}

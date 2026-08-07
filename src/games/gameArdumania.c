// Ardumania (Daniel C, 2024, GPLv3 for this ESP port - "Based on Ardumania
// on ARDUBOY originally on (MIT) License"). An isometric-scrolling
// Pac-Man-style maze chase: steer through a diamond-tiled maze eating dots
// and big dots, dodging up to 7 ghosts (frightened/eaten modes via big
// dots), collecting a bonus fruit that appears periodically, across 9
// levels (5 distinct layouts, the last 4 reusing the first 4 at higher
// ghost counts/speeds). 3 lives per game, a persisted top-3 high-score
// leaderboard with a selectable avatar.
//
// Ported from `more games/MEGAcompilation_ESP/DATA/ARDUMANIA/` - the third
// and last of the 3 Arduboy-exclusive titles staged from Daniel C's own
// ESP8285/ESP8266 "MEGA TinyJoypad" compilation (see this project's
// CLAUDE.md "beyond the original scope" entry for how this compilation was
// found; Nohzdyve and Gilbert in the Downland, from the same compilation,
// already shipped earlier). Same `ESPCOMPATIBILITY`/`ESPKIT.h` shim as
// those two - `[width, raw_pixel_height, ...]`-headed PROGMEM sprite
// tables, the same `255-freq` tone formula as `ELECTROLIB.h`'s own
// `Sound()`, and `TINYJOYPAD_LEFT/RIGHT/UP/DOWN`/`BUTTON_DOWN`(=held)/
// `BUTTON_UP`(=released) input macros mapping directly onto
// `isLeftPressed()`/etc and `isFirePressed()`.
//
// **A real, persistent pixel framebuffer, matching Gilbert in the
// Downland's own architecture rather than Nohzdyve's per-page-buffer one**:
// `ESPKIT.h`'s own `drawSelfMasked`/`drawErase`/`drawOverwrite` all
// accumulate directly into one persistent `Disp_1->buffer[1024]`, called in
// an intricate, order-dependent sequence scattered through a large nested
// per-tile render loop (background walls/dots, gate, fruit, each ghost,
// the player - each its own OR/AND-NOT/clear-then-stamp call, sometimes
// several per grid cell). Unlike Nohzdyve's simpler sprite list (cleanly
// restructurable into "for each page, composite once"), this game's
// isometric per-tile scan has no such clean decomposition - reusing
// Gilbert's own `gitdFrameBuffer`-style full 1024-word buffer (here
// `amaniaFrameBuffer`) was the lower-risk choice: every draw call just
// walks its own real column/page footprint and combines into the buffer
// exactly once, in the same order upstream does, with no restructuring
// needed to get composition order right.
//
// Compositing modes needed: OR (`amaniaDrawSelfMasked`, most sprites/
// walls), AND-NOT (`amaniaDrawErase`, clearing a sprite's own silhouette
// before redrawing it moved), and clear-then-stamp (`amaniaDrawOverwrite`,
// used for the two real per-frame HUD icons in `Pannel()` - `Digital`/
// `Dlive` - reproducing `BlackSquare()`'s own partial-page clear-mask
// math). `drawInvertPixel` (XOR) is never actually called anywhere in this
// game - not implemented.
//
// **Two genuine, proactively-fixed logical-vs-arithmetic-right-shift
// risks** (Vircon32's `>>` is documented *logical*, not arithmetic - the
// same bug family already found and fixed in HollowSeeker/Tiny Pipe/
// Nohzdyve/TinY Fi/Gilbert): `SpriteAmania::GoUp()`/`GoDown()`'s own
// `DecX = -(DecY >> 1)` shifts `DecY` while it's still genuinely negative
// (its own real range is [-7, 0]) - fixed with a shared sign-safe
// `amaniaShiftR1()` halving helper. `Center_Screen()`'s own
// `IsoScrollY >> 1` was checked too and confirmed always safe (`IsoScrollY
// = -DecY` is always >= 0 given `DecY`'s own range), so left as a plain
// shift there.
//
// **A genuine out-of-bounds-Y gap in upstream's own `ExploreMap()`, found
// by inspection before ever compiling, not a report**: `Read2Bits()`/
// `Define2Bits()`/`Initialize2Bits()` all explicitly guard `y_ < 0 ||
// y_ > H-1` before indexing, but `ExploreMap()` (the real wall-collision
// check, called via `ExploreMapChose()` from every `CheckPriorityX/Y()`/
// `GoUp/Down/Left/Right()` call with a raw `GridY +/- 1`, reachable at
// `GridY == 0`) has no such guard - harmless on real AVR flash (an
// adjacent-PROGMEM-byte read), a genuine out-of-bounds global read here.
// Fixed by adding the same bounds guard as `Read2Bits()` (return 1/"wall",
// matching what a border row would represent) directly in
// `amaniaExploreMap()`, safe-by-construction rather than guessed.
//
// **Data tables byte-diff-extracted via a small Python script** (parsing
// the real PROGMEM array literals directly out of `SpriteAMania.h`, not
// hand-transcribed) - all 56 tables, matching this project's own
// established anti-Bomber-dropped-byte discipline.
//
// **Deliberate simplifications, the same "effort/fidelity tradeoff for a
// purely decorative, non-gameplay sequence" precedent already used
// elsewhere in this project** (Nohzdyve's own splash, Space Attack's
// attract slide-in): upstream's own boot sequence (`FakeLoad()`/`Fail()`)
// is an elaborate multi-second LED-flash/beep-cadence animation with a
// random 10% "loading failure" easter egg with zero gameplay effect -
// collapsed to a single static boot-logo screen, press Fire to continue.
// `AnimLvlChange()`'s own walking-conga-line level-transition animation
// (the player + trailing ghosts walking across the screen) is similarly
// collapsed to a brief fixed-duration wipe/hold, matching this project's
// own `RoomTransition`-style level-change convention (e.g. Gilbert's own
// room wipe) rather than reproducing the full animated walk. The avatar-
// select `Menu()` and the 3-slot `ScoreMenu()` leaderboard are both real,
// functional screens (not decorative) and are ported at full fidelity,
// just converted from a blocking `while(1)` to explicit per-frame state
// dispatch like every other port in this project.
//
// **EEPROM**: upstream's own 3-slot leaderboard (`{avatar, score}` per
// slot, a marker byte to detect a virgin card) is restored through this
// project's own `eepromShim.h` - the marker-byte trick is dropped
// entirely, since this shim's own magic/checksum system already serves
// that exact purpose; each slot is just `eeprom_read_byte`(avatar) +
// `eeprom_read_word`(score) at a fixed 3-word stride, with the standard
// 65535 virgin-word guard already established project-wide.
//
// `rand()%n`/`rand()%7` routed through the shared `arand()` helper,
// matching this project's own established precedent; the fixed 24-entry
// `rndmove[]` cycling table (`RandVar()`, upstream's own *deterministic*
// pseudo-random source, not a real RNG call) is kept as a literal
// translation, not replaced.
//
// Sound: every multi-call `SoundSystem()`/`deadSound()` burst (Vircon32's
// audio channel has no queue - this project's own well-documented "N
// synchronous Sound() calls collapse to just the last tone" bug family)
// is routed through one small shared frame-stepped note-pair sequencer,
// with the two largest bursts (the ~62-note "ghost eaten" cue and the
// ~68-note death cue) downsampled to a representative handful of notes
// rather than reproduced literally, matching this project's own
// established fix for oversized computed sweeps.
//
// `switch`/ternary avoided proactively throughout (this dialect's own
// well-documented lack of support, per this project's standing caution -
// every one of upstream's many `switch` blocks and `?:` expressions
// rewritten as `if`/`else` chains); binary literals (`0b11`) rewritten as
// decimal.

#define AMANIA_FINETUNE_X -8
#define AMANIA_FINETUNE_Y 0
#define YSTEP 8
#define XSTEP 14
#define SCREEN_BLOCK_W 13
#define SCREEN_BLOCK_H 9
#define TOTAL_LEVEL 9
#define FRUITTIMELEFT 500
#define ABSOLUTEMAXGHOST 8

// Split-rate throttle, matching upstream's own real dual-rate timing model
// exactly (MEGA82XX.SetFPS()/FPS_Temper()): SetFPS(30) specifically for
// Menu()/ScoreMenu(), SetFPS(50) for everything else (BootIntro, the level-
// transition walk animation, and real gameplay) - found only when checked
// for this request, not accounted for during the initial port (which had
// assumed no timing model existed at all, the "no genuine rate to match"
// category several other ports in this project fall into). An initial
// uniform 30fps lock was tried first (matching this project's own "just
// lock it to the requested rate" precedent - Tiny Mania, Jump Slime,
// TinyRoG, TinY Fi) but the user asked for the real split instead: Menu/
// ScoreMenu throttled to 30fps via a plain exact divisor (60/30=2), every
// other state throttled to 50fps via a Bresenham-style accumulator (the
// same technique already established for Tiny Gilbert's own 40fps target,
// needed here too since 60 doesn't divide evenly by 50). Every existing
// frame-counted timer in this file (amaniaShortStartTimer/
// amaniaLowCheckTimer/amaniaTransTimer, the sfx sequencer's own wait-
// frames, amaniaDeathPauseFrames/amaniaExitPauseFrames, every GhostTimer/
// HighSpeedTimer-style counter) is deliberately left unrescaled, matching
// this project's own standing "one divisor, no dual bookkeeping" practice
// - they simply now take longer in real time, proportional to each state's
// own real rate.
#define AMANIA_TICK_DIVISOR ( 60 / 30 )
#define AMANIA_GAMEPLAY_FPS 50

// -----------------------------------------------------------------------------
// Data tables (extracted via script from SpriteAMania.h, byte-diff
// verified against the real source before use)
// -----------------------------------------------------------------------------

// Restored (not originally dropped as dead code) - upstream's own
// AnimLvlChange() wraps its MEGA_PLAY_MUSIC(&score[0]) call in a real
// /* */ comment, gated behind a FirstLoad flag that only ever runs it
// once at the very start of the transition - a genuine 59-note walking
// melody (freq/dur pairs, several genuine freq=0 rests mixed with real
// tones), not decorative filler. Played via the shared frame-stepped
// sequencer (see amaniaAdvanceSfx()) queued in full when the transition
// begins, alongside (not instead of) the 2 direct confirm-tone calls
// AnimLvlChange() also fires unconditionally at that same moment.
int[119] amaniascore = { 118, 1, 255, 0, 50, 100, 255, 0, 1, 60, 255, 0, 1, 1, 255, 0, 1, 100, 255, 0, 1, 60, 255, 0, 1, 1, 250, 1, 250, 0, 50, 10, 255, 0, 50, 110, 255, 0, 1, 70, 255, 0, 1, 10, 255, 0, 1, 110, 255, 0, 1, 70, 255, 0, 1, 10, 255, 10, 255, 0, 50, 1, 255, 0, 50, 100, 255, 0, 1, 60, 255, 0, 1, 1, 255, 0, 1, 100, 255, 0, 1, 60, 255, 0, 1, 1, 255, 1, 255, 0, 50, 0, 25, 1, 180, 0, 100, 1, 180, 0, 25, 1, 180, 0, 25, 20, 180, 0, 25, 40, 180, 0, 25, 60, 180, 60, 180, 60, 100 };

int[131] amaniaplayer1ready = { 43, 22, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x22, 0x22, 0x1C, 0x00, 0x00, 0xFE, 0x00, 0x40, 0xA8, 0xA8, 0xF0, 0x00, 0x18, 0x60, 0xC0, 0x38, 0x00, 0x70, 0xA8, 0xA8, 0xB0, 0x00, 0x00, 0xF8, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x82, 0xFE, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x90, 0x90, 0x60, 0x00, 0x00, 0x80, 0x40, 0x40, 0x80, 0x02, 0x01, 0x40, 0x40, 0x80, 0x00, 0x80, 0xC0, 0x40, 0xF0, 0x00, 0xC0, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x07, 0x04, 0x00, 0x03, 0x05, 0x05, 0x01, 0x00, 0x02, 0x05, 0x05, 0x07, 0x00, 0x03, 0x04, 0x04, 0x07, 0x00, 0x00, 0x13, 0x0E, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
int[131] amaniaplayer1readyMask = { 43, 22, 0x00, 0x00, 0x00, 0xFF, 0x01, 0xDD, 0x5D, 0x63, 0x3F, 0xFF, 0x01, 0xFF, 0xBC, 0x54, 0x54, 0x0C, 0xFC, 0xE4, 0x9C, 0x3C, 0xC4, 0xFC, 0x8C, 0x54, 0x54, 0x4C, 0xF8, 0xFC, 0x04, 0xEC, 0x34, 0x1C, 0x00, 0x00, 0xC7, 0x7D, 0x01, 0x7F, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0xF8, 0x08, 0x69, 0x69, 0x99, 0xF1, 0xC1, 0x61, 0xA1, 0xA1, 0x67, 0xC5, 0xE6, 0xA3, 0xA1, 0x61, 0xC1, 0x61, 0x21, 0xB9, 0x09, 0xF9, 0x21, 0xE0, 0xE0, 0x20, 0xE0, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x08, 0x0F, 0x0F, 0x08, 0x0B, 0x0F, 0x0C, 0x0A, 0x0A, 0x0E, 0x07, 0x0D, 0x0A, 0x0A, 0x08, 0x0F, 0x0C, 0x0B, 0x0B, 0x08, 0x0F, 0x3F, 0x2C, 0x31, 0x1E, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
int[150] amaniaHighScore = { 74, 15, 0x00, 0xC0, 0x7E, 0x02, 0xCA, 0xFA, 0x82, 0x8E, 0xE2, 0x3A, 0x0A, 0x02, 0x02, 0x0A, 0xEA, 0x3A, 0x0A, 0x0A, 0xE2, 0x33, 0x19, 0x8D, 0xDD, 0x19, 0x03, 0x02, 0xCA, 0xFA, 0x82, 0x8E, 0xE2, 0x3A, 0x8A, 0xE2, 0x3E, 0xC0, 0x40, 0x7E, 0x03, 0x39, 0x6D, 0xC5, 0x8D, 0x19, 0xE3, 0x32, 0x1A, 0x0A, 0x1A, 0x02, 0xE6, 0x32, 0x3A, 0x1A, 0x0A, 0x0A, 0x9A, 0xF2, 0x06, 0x02, 0xCA, 0xBA, 0x8A, 0xCA, 0x7A, 0x02, 0x0E, 0xE2, 0xFA, 0x4A, 0x4A, 0x1A, 0x02, 0xFE, 0x3E, 0x23, 0x28, 0x2F, 0x23, 0x38, 0x20, 0x2F, 0x29, 0x20, 0x30, 0x14, 0x14, 0x16, 0x15, 0x34, 0x60, 0x48, 0x59, 0x59, 0x4D, 0x67, 0x33, 0x20, 0x28, 0x2F, 0x23, 0x38, 0x20, 0x2F, 0x29, 0x20, 0x3F, 0x00, 0x00, 0x0F, 0x18, 0x13, 0x16, 0x14, 0x14, 0x16, 0x13, 0x18, 0x13, 0x16, 0x14, 0x16, 0x13, 0x18, 0x13, 0x16, 0x14, 0x14, 0x16, 0x12, 0x1B, 0x10, 0x16, 0x13, 0x19, 0x09, 0x19, 0x13, 0x16, 0x10, 0x10, 0x17, 0x14, 0x14, 0x12, 0x1B, 0x08, 0x0F };
int[29] amaniawave = { 0, 0, 1, 2, 4, 6, 8, 10, 13, 15, 17, 18, 19, 20, 20, 21, 20, 20, 19, 18, 17, 15, 13, 10, 8, 6, 4, 2, 1 };
int[50] amaniaPlate = { 12, 29, 0xE7, 0x73, 0xB7, 0x53, 0xA7, 0x53, 0xA7, 0x53, 0xA7, 0x53, 0xA7, 0xD3, 0xAA, 0x55, 0xAA, 0xD5, 0xEA, 0x75, 0xBA, 0x5D, 0xAE, 0x57, 0xAB, 0x55, 0xAE, 0xD7, 0xAB, 0xD5, 0xAA, 0xD5, 0xAA, 0xD5, 0xEA, 0xF5, 0xBA, 0xDD, 0x03, 0x11, 0x13, 0x15, 0x03, 0x11, 0x13, 0x15, 0x03, 0x11, 0x13, 0x15 };
int[3] amaniaBlack = { 1, 8, 0xff };
int[42] amaniascroll = { 5, 8, 0xFF, 0x03, 0x06, 0x0C, 0xFF, 0xFF, 0x06, 0x0C, 0x18, 0xFF, 0xFF, 0x0C, 0x18, 0x30, 0xFF, 0xFF, 0x18, 0x30, 0x60, 0xFF, 0xFF, 0x30, 0x60, 0xC0, 0xFF, 0xFF, 0x60, 0xC0, 0x81, 0xFF, 0xFF, 0xC0, 0x81, 0x03, 0xFF, 0xFF, 0x81, 0x03, 0x06, 0xFF };
int[18] amaniaaudio = { 8, 9, 0x00, 0x00, 0x10, 0x44, 0x29, 0x92, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };
int[226] amaniabar = { 112, 5, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x19, 0x13, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C, 0x06, 0x0C };
int[290] amaniaArduMania = { 72, 14, 0x00, 0xC0, 0x30, 0x8C, 0xE2, 0x3A, 0x7A, 0xE2, 0x0C, 0x08, 0xE8, 0xE8, 0x28, 0x28, 0xC8, 0xE8, 0x2E, 0x22, 0xFA, 0xFA, 0x02, 0xF2, 0x7A, 0xF2, 0xC4, 0x18, 0x60, 0x18, 0xC4, 0xF2, 0xFA, 0xF2, 0x04, 0xF8, 0xFC, 0x02, 0xFA, 0xFA, 0x7A, 0xE2, 0x8E, 0xE2, 0x7A, 0xFA, 0xFA, 0x02, 0xAE, 0xA8, 0xA8, 0xE8, 0xE8, 0xC8, 0x08, 0xE8, 0xE8, 0x28, 0x28, 0xE8, 0xEF, 0x01, 0xED, 0xED, 0x01, 0xAF, 0xA8, 0xA8, 0xE8, 0xE8, 0xC8, 0x10, 0xE0, 0x00, 0x1F, 0x10, 0x16, 0x17, 0x11, 0x0D, 0x09, 0x13, 0x17, 0x10, 0x17, 0x17, 0x10, 0x08, 0x13, 0x17, 0x14, 0x14, 0x17, 0x17, 0x10, 0x09, 0x13, 0x27, 0x2F, 0x2F, 0x2E, 0x2F, 0x2F, 0x27, 0x13, 0x09, 0x04, 0x03, 0x1F, 0x10, 0x17, 0x17, 0x10, 0x19, 0x0B, 0x19, 0x10, 0x17, 0x17, 0x10, 0x17, 0x15, 0x14, 0x16, 0x17, 0x17, 0x10, 0x17, 0x17, 0x10, 0x10, 0x17, 0x17, 0x10, 0x17, 0x17, 0x10, 0x17, 0x15, 0x14, 0x16, 0x17, 0x17, 0x10, 0x1F, 0x00, 0x00, 0x00, 0xC0, 0x70, 0x1C, 0xC4, 0x84, 0x1C, 0xF0, 0xF0, 0x10, 0x10, 0xD0, 0xD0, 0x30, 0x10, 0xD0, 0xDC, 0x04, 0x04, 0xFC, 0x0C, 0x84, 0x0C, 0x38, 0xE0, 0x80, 0xE0, 0x38, 0x0C, 0x04, 0x0C, 0xF8, 0x00, 0x00, 0xFC, 0x04, 0x04, 0x84, 0x1C, 0x70, 0x1C, 0x84, 0x04, 0x04, 0xFC, 0x50, 0x50, 0x50, 0x10, 0x10, 0x30, 0xF0, 0x10, 0x10, 0xD0, 0xD0, 0x10, 0x10, 0xFE, 0x12, 0x12, 0xFE, 0x50, 0x50, 0x50, 0x10, 0x10, 0x30, 0xE0, 0x00, 0x00, 0x00, 0x0F, 0x09, 0x08, 0x0E, 0x02, 0x06, 0x0C, 0x08, 0x0F, 0x08, 0x08, 0x0F, 0x07, 0x0C, 0x08, 0x0B, 0x0B, 0x08, 0x08, 0x0F, 0x06, 0x0C, 0x18, 0x10, 0x10, 0x11, 0x10, 0x10, 0x18, 0x0C, 0x06, 0x03, 0x00, 0x00, 0x0F, 0x08, 0x08, 0x0F, 0x06, 0x04, 0x06, 0x0F, 0x08, 0x08, 0x0F, 0x08, 0x0A, 0x0B, 0x09, 0x08, 0x08, 0x0F, 0x08, 0x08, 0x0F, 0x0F, 0x08, 0x08, 0x0F, 0x08, 0x08, 0x0F, 0x08, 0x0A, 0x0B, 0x09, 0x08, 0x08, 0x0F, 0x00, 0x00 };
int[278] amaniaBOOTINTRO = { 46, 46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0xAB, 0x55, 0xAB, 0x55, 0xAB, 0x55, 0xAB, 0x55, 0xAB, 0x55, 0xAB, 0x7D, 0xC7, 0x45, 0xFF, 0x55, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x80, 0x40, 0x20, 0x18, 0x26, 0x41, 0xC1, 0x40, 0x7F, 0x3F, 0x3F, 0x1F, 0x0F, 0x09, 0x0F, 0x89, 0x48, 0x2C, 0x13, 0x0F, 0x01, 0x7D, 0x15, 0x61, 0x01, 0x7D, 0x45, 0x7D, 0x01, 0x39, 0x55, 0x7D, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0xE3, 0xA0, 0x60, 0x60, 0xA0, 0x20, 0x20, 0xE0, 0x60, 0x20, 0x2E, 0x21, 0xEF, 0x20, 0x26, 0x29, 0xAF, 0x20, 0xAD, 0xAE, 0xAF, 0x20, 0xAF, 0x2A, 0x26, 0x21, 0xBF, 0xBF, 0xBF, 0x3F, 0x3F, 0x07, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x20, 0x20, 0x10, 0x0F, 0x01, 0x02, 0x02, 0x02, 0x07, 0x99, 0x72, 0x0C, 0x08, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07, 0x02, 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x04, 0x00, 0x04, 0x05, 0x07, 0x00, 0x00, 0x00, 0x3F, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3F, 0x20, 0x20, 0x20, 0x20, 0x20, 0x38, 0x04, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
int[40] amaniaSaut = { 0, 3, 6, 8, 12, 13, 14, 15, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 16, 16, 15, 14, 13, 12, 10, 8, 6, 5, 4, 2, 1, 0, 0, 1, 2, 1, 0 };
// amaniaTEXT/amaniaFRAME (Fail()'s own "insert coin"-style failure screen)
// dropped along with the rest of the elaborate boot sequence they were
// only ever used by - see header comment.
int[42] amaniapolice = { 4, 8, 0x1F, 0x11, 0x1F, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x1D, 0x15, 0x17, 0x00, 0x11, 0x15, 0x1F, 0x00, 0x07, 0x04, 0x1F, 0x00, 0x17, 0x15, 0x1D, 0x00, 0x1F, 0x15, 0x1D, 0x00, 0x01, 0x01, 0x1F, 0x00, 0x1F, 0x15, 0x1F, 0x00, 0x17, 0x15, 0x1F, 0x00 };
int[54] amaniaDigital = { 26, 10, 0x00, 0xFE, 0x06, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x06, 0xFE, 0x00, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03 };
int[36] amaniaDlive = { 17, 10, 0xFE, 0x06, 0x02, 0x72, 0xFA, 0xDA, 0x8A, 0x02, 0x52, 0x02, 0x02, 0x02, 0x02, 0x02, 0x06, 0xFE, 0x00, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x00 };
int[24] amaniarndmove = { 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0 };
int[36] amaniaGate = { 17, 15, 0x00, 0x00, 0x50, 0x00, 0x50, 0x50, 0x00, 0x50, 0x00, 0x00, 0x50, 0x50, 0x00, 0x50, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
int[36] amaniaBlock0 = { 17, 15, 0xC0, 0xF0, 0xFC, 0xFF, 0xFF, 0xE7, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xE7, 0xFF, 0xFF, 0x3F, 0x0F, 0xFF, 0x7F, 0x55, 0x6A, 0x55, 0x6A, 0x55, 0x6A, 0x55, 0x6A, 0x55, 0x6A, 0x55, 0x6A, 0x7F, 0x18, 0x06, 0x01 };
int[36] amaniaBlock0m = { 17, 15, 0xC0, 0xF0, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x1F, 0x07, 0x01 };
int[36] amaniaBlock1 = { 17, 15, 0x00, 0x00, 0xC0, 0x60, 0x30, 0x98, 0x24, 0x42, 0x13, 0x8F, 0x3E, 0x7C, 0xFC, 0xF8, 0xF0, 0x30, 0xC0, 0x7C, 0x4B, 0x69, 0x5A, 0x69, 0x5A, 0x69, 0x5A, 0x69, 0x5A, 0x69, 0x5A, 0x49, 0x7B, 0x1C, 0x07, 0x01 };
int[36] amaniaBlock1m = { 17, 15, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFE, 0xFC, 0xF8, 0xF8, 0xE0, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x1F, 0x07 };
int[36] amaniaBlock2 = { 17, 15, 0x40, 0x70, 0x4C, 0x43, 0x41, 0x49, 0x55, 0x49, 0x55, 0x49, 0x55, 0x49, 0x41, 0x41, 0x31, 0x0D, 0x03, 0x55, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x55, 0x00, 0x08, 0x02 };
int[36] amaniaBlock2m = { 17, 15, 0x00, 0xC0, 0xF0, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x1F, 0x0F, 0x03 };
int[36] amaniaBlock3 = { 17, 15, 0x00, 0x00, 0x00, 0x80, 0x40, 0xC0, 0x40, 0xC0, 0x40, 0xC0, 0x40, 0xC0, 0x40, 0xC0, 0x40, 0x40, 0xC0, 0x78, 0x5C, 0x76, 0x59, 0x76, 0x5F, 0x77, 0x5F, 0x77, 0x5F, 0x77, 0x5F, 0x77, 0x11, 0x08, 0x06, 0x01 };
int[36] amaniaBlock3m = { 17, 15, 0x00, 0x00, 0x80, 0xC0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0x7C, 0x7E, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x1F, 0x0F, 0x07 };
int[36] amaniaBlock4 = { 17, 15, 0x00, 0xF8, 0xFC, 0xCE, 0x86, 0x13, 0x03, 0x0B, 0x03, 0x03, 0x0B, 0x03, 0x86, 0xCE, 0xFC, 0xF8, 0x00, 0x00, 0x0F, 0x1E, 0x3B, 0x3F, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x3F, 0x3B, 0x1E, 0x0F, 0x00 };
int[36] amaniaBlock4m = { 17, 15, 0x00, 0x00, 0x00, 0x30, 0x78, 0xEC, 0xFC, 0xF4, 0xFC, 0xFC, 0xF4, 0xFC, 0x78, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x04, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x04, 0x01, 0x00, 0x00 };
int[7] amaniadotmask = { 5, 8, 0x0E, 0x1F, 0x1F, 0x1F, 0x0E };
int[7] amaniatidot = { 5, 8, 0x00, 0x44, 0x0E, 0x44, 0x00 };
int[19] amanialevel0_Add = { 2, 4, 12, 4, 2, 16, 12, 16, 7, 9, 7, 13, 7, 7, 7, 6, 2, 97, 112 };
int[47] amanialevel0 = { 15, 21, 0xFF, 0xFF, 0x83, 0x3B, 0xBB, 0x03, 0xDB, 0x03, 0xDB, 0x03, 0xBB, 0x3B, 0x83, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0xDC, 0xC5, 0x10, 0xDD, 0x01, 0xDD, 0x10, 0xC5, 0xDC, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0xFB, 0xFB, 0xF8, 0xFD, 0xFC, 0xFD, 0xF8, 0xFB, 0xFB, 0xF8, 0xFF, 0xFF };
int[28] amanialevel1_Add = { 6, 4, 14, 4, 6, 17, 14, 17, 10, 12, 10, 14, 10, 8, 10, 6, 11, 136, 156, 157, 158, 177, 178, 179, 219, 220, 221, 190 };
int[65] amanialevel1 = { 21, 22, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x43, 0x1B, 0xD3, 0x47, 0x17, 0x47, 0xD3, 0x1B, 0x43, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0xF7, 0xF5, 0xF7, 0xF0, 0xF7, 0xF4, 0x15, 0x40, 0x6F, 0x0A, 0xAA, 0x0A, 0x6F, 0x40, 0x15, 0xF4, 0xF7, 0xF0, 0xF5, 0xF5, 0xF5, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xF5, 0xF0, 0xF5, 0xF5, 0xF5, 0xF0, 0xF5, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
int[18] amanialevel2_Add = { 2, 4, 16, 4, 2, 19, 16, 19, 9, 13, 9, 17, 9, 11, 9, 11, 1, 218 };
int[59] amanialevel2 = { 19, 24, 0xFF, 0xFF, 0x03, 0xBB, 0xBB, 0x03, 0xBB, 0x3B, 0x83, 0xBF, 0x83, 0x3B, 0xBB, 0x03, 0xBB, 0xBB, 0x03, 0xFF, 0xFF, 0xDB, 0xDB, 0x5A, 0x42, 0x6E, 0x00, 0x6F, 0x02, 0x58, 0xD3, 0x58, 0x02, 0x6F, 0x00, 0x6E, 0x42, 0x5A, 0xDB, 0xDB, 0xFF, 0xFF, 0xC4, 0xD1, 0xD7, 0xD0, 0xDD, 0xD1, 0xC4, 0xDD, 0xC4, 0xD1, 0xDD, 0xD0, 0xD7, 0xD1, 0xC4, 0xFF, 0xFF };
int[20] amanialevel3_Add = { 2, 4, 14, 4, 2, 23, 14, 23, 8, 13, 8, 17, 8, 11, 8, 9, 3, 161, 178, 195 };
int[70] amanialevel3 = { 17, 28, 0xFF, 0xFF, 0x03, 0xBB, 0x0B, 0xE3, 0xEF, 0x07, 0xB7, 0x07, 0xEF, 0xE3, 0x0B, 0xBB, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x70, 0x77, 0x00, 0x56, 0x40, 0xDE, 0x10, 0xDE, 0x40, 0x56, 0x00, 0x77, 0x70, 0xFF, 0xFF, 0xFF, 0xFF, 0x04, 0x71, 0x75, 0x01, 0xF4, 0xF5, 0x04, 0xF5, 0xF4, 0x01, 0x75, 0x71, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0xFD, 0xFC, 0xFD, 0xFD, 0xFD, 0xFC, 0xFD, 0xFD, 0xFD, 0xFC, 0xFD, 0xFC, 0xFF, 0xFF };
int[24] amanialevel4_Add = { 1, 3, 19, 3, 1, 21, 19, 21, 10, 11, 10, 13, 10, 9, 10, 7, 7, 157, 177, 178, 179, 198, 199, 200 };
int[65] amanialevel4 = { 21, 23, 0xDF, 0xD5, 0x15, 0xC5, 0xD1, 0x07, 0xF5, 0x35, 0xA1, 0x8F, 0x21, 0x8F, 0xA1, 0x35, 0xF5, 0x07, 0xD1, 0xC5, 0x15, 0xD5, 0xDF, 0xD5, 0x51, 0x04, 0x76, 0x16, 0xC0, 0x1E, 0xD0, 0x07, 0xD4, 0x14, 0xD4, 0x07, 0xD0, 0x1E, 0xC0, 0x16, 0x76, 0x04, 0x51, 0xD5, 0x7D, 0x4D, 0x60, 0x6F, 0x64, 0x75, 0x40, 0x7A, 0x42, 0x5B, 0x58, 0x5B, 0x42, 0x7A, 0x40, 0x75, 0x64, 0x6F, 0x60, 0x4D, 0x7D };
int[80] amaniaardumania_m = { 13, 13, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0x01, 0x03, 0x07, 0x0E, 0x1C, 0x1C, 0x1C, 0x1E, 0x1E, 0x0F, 0x07, 0x03, 0x01, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
int[41] amaniaghostmask = { 13, 13, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0x0F, 0x1F, 0x0F, 0x07, 0x0F, 0x1F, 0x1F, 0x0F, 0x07, 0x07, 0x0F, 0x1F, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
int[314] amaniaGhost = { 13, 13, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xEA, 0xCE, 0xFE, 0xEC, 0xC8, 0xF0, 0x00, 0x00, 0x0F, 0x07, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x03, 0x07, 0x0F, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xEA, 0xCE, 0xFE, 0xEC, 0xC8, 0xF0, 0x00, 0x00, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x03, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xEA, 0xCE, 0xFE, 0xEC, 0xC8, 0xF0, 0x00, 0x00, 0x0F, 0x07, 0x03, 0x03, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x07, 0x00, 0x00, 0xF0, 0xC8, 0xEC, 0xFE, 0xFE, 0xFA, 0xFE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x0F, 0x07, 0x03, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x07, 0x0F, 0x00, 0x00, 0xF0, 0xC8, 0xEC, 0xFE, 0xFE, 0xFA, 0xFE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x03, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x00, 0x00, 0xF0, 0xC8, 0xEC, 0xFE, 0xFE, 0xFA, 0xFE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x07, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x03, 0x03, 0x07, 0x0F, 0x00, 0x00, 0xF0, 0xF8, 0x9C, 0xDE, 0xFE, 0x9A, 0xDE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x0F, 0x07, 0x03, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x07, 0x0F, 0x00, 0x00, 0xF0, 0xF8, 0x9C, 0xDE, 0xFE, 0x9A, 0xDE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x03, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x00, 0x00, 0xF0, 0xF8, 0x9C, 0xDE, 0xFE, 0x9A, 0xDE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x07, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x03, 0x03, 0x07, 0x0F, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xFA, 0xFE, 0xFE, 0xFC, 0xF8, 0xF0, 0x00, 0x00, 0x0F, 0x07, 0x03, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x07, 0x0F, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xFA, 0xFE, 0xFE, 0xFC, 0xF8, 0xF0, 0x00, 0x00, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x03, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xFA, 0xFE, 0xFE, 0xFC, 0xF8, 0xF0, 0x00, 0x00, 0x07, 0x03, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x03, 0x03, 0x07, 0x0F, 0x00 };
int[314] amaniaGhostBlack = { 13, 13, 0x00, 0xF0, 0x08, 0x54, 0x0A, 0x12, 0x32, 0x02, 0x12, 0x34, 0x08, 0xF0, 0x00, 0x00, 0x0F, 0x04, 0x02, 0x04, 0x08, 0x08, 0x04, 0x02, 0x02, 0x04, 0x0F, 0x00, 0x00, 0xF0, 0x08, 0x54, 0x0A, 0x12, 0x32, 0x02, 0x12, 0x34, 0x08, 0xF0, 0x00, 0x00, 0x07, 0x08, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x08, 0x07, 0x00, 0x00, 0xF0, 0x08, 0x54, 0x0A, 0x12, 0x32, 0x02, 0x12, 0x34, 0x08, 0xF0, 0x00, 0x00, 0x0F, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x08, 0x04, 0x02, 0x07, 0x00, 0x00, 0xF0, 0x38, 0x14, 0x02, 0x02, 0x02, 0x02, 0x0A, 0x54, 0x08, 0xF0, 0x00, 0x00, 0x0F, 0x04, 0x02, 0x02, 0x04, 0x08, 0x08, 0x04, 0x02, 0x04, 0x0F, 0x00, 0x00, 0xF0, 0x38, 0x14, 0x02, 0x02, 0x02, 0x02, 0x0A, 0x54, 0x08, 0xF0, 0x00, 0x00, 0x07, 0x08, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x08, 0x07, 0x00, 0x00, 0xF0, 0x38, 0x14, 0x02, 0x02, 0x02, 0x02, 0x0A, 0x54, 0x08, 0xF0, 0x00, 0x00, 0x07, 0x02, 0x04, 0x08, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x0F, 0x00, 0x00, 0xF0, 0x08, 0x64, 0x22, 0x02, 0x62, 0x22, 0x0A, 0x54, 0x08, 0xF0, 0x00, 0x00, 0x0F, 0x04, 0x02, 0x02, 0x04, 0x08, 0x08, 0x04, 0x02, 0x04, 0x0F, 0x00, 0x00, 0xF0, 0x08, 0x64, 0x22, 0x02, 0x62, 0x22, 0x0A, 0x54, 0x08, 0xF0, 0x00, 0x00, 0x07, 0x08, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x08, 0x07, 0x00, 0x00, 0xF0, 0x08, 0x64, 0x22, 0x02, 0x62, 0x22, 0x0A, 0x54, 0x08, 0xF0, 0x00, 0x00, 0x07, 0x02, 0x04, 0x08, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x0F, 0x00, 0x00, 0xF0, 0x08, 0x54, 0x0A, 0x02, 0x02, 0x02, 0x02, 0x04, 0x08, 0xF0, 0x00, 0x00, 0x0F, 0x04, 0x02, 0x02, 0x04, 0x08, 0x08, 0x04, 0x02, 0x04, 0x0F, 0x00, 0x00, 0xF0, 0x08, 0x54, 0x0A, 0x02, 0x02, 0x02, 0x02, 0x04, 0x08, 0xF0, 0x00, 0x00, 0x07, 0x08, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x08, 0x07, 0x00, 0x00, 0xF0, 0x08, 0x54, 0x0A, 0x02, 0x02, 0x02, 0x02, 0x04, 0x08, 0xF0, 0x00, 0x00, 0x07, 0x02, 0x04, 0x08, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x0F, 0x00 };
int[314] amaniaGhostEyes = { 13, 13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x60, 0x00, 0x10, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x60, 0x00, 0x10, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x60, 0x00, 0x10, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x10, 0x00, 0x18, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x10, 0x00, 0x18, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x10, 0x00, 0x18, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x20, 0x00, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x20, 0x00, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x20, 0x00, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x18, 0x00, 0x08, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x0C, 0x00, 0x04, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x0C, 0x00, 0x04, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
int[314] amaniaGhostEyeMask = { 13, 13, 0x00, 0x00, 0x00, 0x00, 0x20, 0x70, 0xF0, 0x70, 0x38, 0x78, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x70, 0xF0, 0x70, 0x38, 0x78, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x70, 0xF0, 0x70, 0x38, 0x78, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x78, 0x38, 0x18, 0x3C, 0x1C, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x78, 0x38, 0x18, 0x3C, 0x1C, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x78, 0x38, 0x18, 0x3C, 0x1C, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xF0, 0x70, 0x60, 0xF0, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xF0, 0x70, 0x60, 0xF0, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xF0, 0x70, 0x60, 0xF0, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x1C, 0x3C, 0x18, 0x1C, 0x3C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x0E, 0x1E, 0x0C, 0x0E, 0x1E, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x0E, 0x1E, 0x0C, 0x0E, 0x1E, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
int[652] amaniaardumania = { 13, 13, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xEE, 0xCE, 0xF6, 0xE4, 0xF8, 0x70, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x0F, 0x0D, 0x0D, 0x0D, 0x06, 0x02, 0x01, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xEE, 0xCE, 0xF6, 0x64, 0xF8, 0x30, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x0F, 0x0D, 0x0C, 0x0C, 0x04, 0x06, 0x01, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xEE, 0x4E, 0x76, 0xA4, 0x18, 0x10, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x0C, 0x08, 0x08, 0x08, 0x0C, 0x07, 0x00, 0x00, 0x00, 0xF0, 0xC8, 0xEC, 0xFE, 0xFE, 0xFE, 0xFE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x00, 0x02, 0x05, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x07, 0x03, 0x01, 0x00, 0x00, 0x70, 0xC8, 0xEC, 0xFE, 0xFE, 0xFE, 0xFE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x00, 0x02, 0x04, 0x0D, 0x0F, 0x0F, 0x0F, 0x0F, 0x07, 0x03, 0x01, 0x00, 0x00, 0x30, 0x48, 0xEC, 0xFE, 0xFE, 0xFE, 0xFE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x04, 0x0D, 0x0F, 0x0F, 0x0F, 0x0F, 0x07, 0x03, 0x01, 0x00, 0x00, 0x70, 0xC8, 0xEC, 0xFE, 0xFE, 0xFE, 0xCE, 0xEA, 0x74, 0xE8, 0xF0, 0x00, 0x00, 0x01, 0x02, 0x06, 0x0D, 0x0D, 0x0D, 0x0E, 0x0E, 0x07, 0x03, 0x01, 0x00, 0x00, 0x70, 0xC8, 0xEC, 0xFE, 0xFE, 0xFE, 0xCE, 0xEA, 0xF4, 0x68, 0xF0, 0x00, 0x00, 0x01, 0x02, 0x04, 0x0C, 0x0C, 0x0C, 0x0C, 0x0E, 0x06, 0x03, 0x01, 0x00, 0x00, 0xB0, 0x48, 0x6C, 0x7E, 0x7E, 0x7E, 0x4E, 0x6A, 0x74, 0xE8, 0xF0, 0x00, 0x00, 0x01, 0x06, 0x04, 0x08, 0x08, 0x08, 0x0C, 0x0C, 0x06, 0x03, 0x01, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xFE, 0xFE, 0xFE, 0xF4, 0xF8, 0xB0, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x07, 0x03, 0x01, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xFE, 0xFE, 0xFE, 0xF4, 0xB8, 0x90, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x07, 0x03, 0x01, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xFE, 0xFE, 0xFE, 0x34, 0x18, 0x00, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x07, 0x02, 0x00, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xEE, 0x4E, 0x76, 0xA4, 0x18, 0x10, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x0C, 0x08, 0x08, 0x08, 0x0C, 0x07, 0x00, 0x00, 0x00, 0xB0, 0x48, 0x6C, 0x7E, 0x7E, 0x7E, 0x4E, 0x6A, 0x74, 0xE8, 0xF0, 0x00, 0x00, 0x01, 0x06, 0x04, 0x08, 0x08, 0x08, 0x0C, 0x0C, 0x06, 0x03, 0x01, 0x00, 0x00, 0x30, 0x48, 0xEC, 0xFE, 0xFE, 0xFE, 0xFE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x04, 0x0D, 0x0F, 0x0F, 0x0F, 0x0F, 0x07, 0x03, 0x01, 0x00, 0x00, 0xF0, 0xA8, 0xF4, 0xFA, 0xFE, 0xFE, 0xFE, 0xFE, 0x34, 0x18, 0x00, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x07, 0x02, 0x00, 0x00, 0x00, 0xF0, 0xF8, 0xFC, 0xFE, 0xFE, 0xFE, 0xFE, 0xFA, 0xF4, 0xA8, 0xF0, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x07, 0x03, 0x01, 0x00, 0x00, 0x00, 0xF0, 0xF8, 0xFC, 0xFC, 0xFC, 0xF4, 0xEC, 0x98, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x07, 0x07, 0x07, 0x07, 0x07, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0xF0, 0xF8, 0xF8, 0xE8, 0xD0, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x03, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0xF0, 0xD0, 0xB0, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xE0, 0xD0, 0xA0, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xE0, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
int[10] amaniabigdot = { 8, 8, 0x00, 0x00, 0x9C, 0x3E, 0xBA, 0x32, 0x9C, 0x00 };
int[10] amaniaBigDotMask = { 8, 8, 0x00, 0x1C, 0x22, 0x41, 0x45, 0x4D, 0x22, 0x1C };
int[210] amaniafruit = { 13, 13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x40, 0x20, 0xD0, 0x38, 0x04, 0x04, 0x08, 0x00, 0x00, 0x06, 0x0F, 0x0D, 0x06, 0x00, 0x00, 0x06, 0x0F, 0x0D, 0x06, 0x00, 0x00, 0x00, 0x04, 0xCC, 0xF8, 0xF0, 0xFC, 0x70, 0xB8, 0xCC, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x07, 0x0F, 0x0A, 0x0D, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC0, 0xE0, 0xE0, 0xFC, 0xE8, 0x6C, 0xC4, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x07, 0x07, 0x07, 0x05, 0x06, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xE0, 0xD0, 0xE8, 0xF4, 0xE0, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x1F, 0x3F, 0x3F, 0x3F, 0x3F, 0x37, 0x18, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x80, 0x40, 0xE4, 0xA8, 0x78, 0xA8, 0x68, 0xC0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0D, 0x17, 0x1D, 0x17, 0x1D, 0x17, 0x08, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xE0, 0x3C, 0xF0, 0x00, 0x00, 0x00, 0x04, 0x0C, 0x0C, 0x1C, 0x1E, 0x1F, 0x0B, 0x0D, 0x07, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0xF8, 0xFC, 0xF4, 0xE4, 0xC8, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x8C, 0x06, 0x02, 0x0A, 0x1A, 0x36, 0x8C, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x02, 0x02, 0x02, 0x03, 0x01, 0x00, 0x00, 0x00 };
int[210] amaniafruitMask = { 13, 13, 0x00, 0x00, 0x80, 0x80, 0xC0, 0x60, 0xB0, 0xD8, 0x2C, 0xC6, 0xFA, 0x9A, 0x16, 0x00, 0x0F, 0x19, 0x10, 0x12, 0x19, 0x0F, 0x0F, 0x19, 0x10, 0x12, 0x19, 0x0F, 0x00, 0x0E, 0xFA, 0x32, 0x06, 0x0E, 0x02, 0x8E, 0x46, 0x32, 0xFA, 0x0E, 0x00, 0x00, 0x00, 0x03, 0x0E, 0x18, 0x10, 0x15, 0x12, 0x19, 0x0E, 0x03, 0x00, 0x00, 0x00, 0xC0, 0x60, 0x30, 0x10, 0x1E, 0x02, 0x16, 0x92, 0x3A, 0x6E, 0xC0, 0x00, 0x00, 0x03, 0x06, 0x0C, 0x08, 0x08, 0x08, 0x0A, 0x09, 0x0C, 0x06, 0x03, 0x00, 0x00, 0xE0, 0x30, 0x18, 0x08, 0x18, 0x2C, 0x16, 0x0A, 0x1E, 0x30, 0xE0, 0x00, 0x00, 0x1F, 0x30, 0x60, 0x40, 0x40, 0x40, 0x40, 0x48, 0x67, 0x30, 0x1F, 0x00, 0x00, 0xC0, 0x60, 0xBE, 0x1A, 0x56, 0x84, 0x54, 0x94, 0x3C, 0x60, 0xC0, 0x00, 0x00, 0x0F, 0x18, 0x32, 0x28, 0x22, 0x28, 0x22, 0x28, 0x37, 0x18, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC0, 0x70, 0x1E, 0xC2, 0x0E, 0xF8, 0x00, 0x0E, 0x1A, 0x12, 0x32, 0x23, 0x21, 0x20, 0x34, 0x12, 0x18, 0x0C, 0x07, 0x00, 0x00, 0x00, 0xF8, 0x8C, 0x06, 0x02, 0x0A, 0x1A, 0x36, 0x8C, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x02, 0x02, 0x02, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x06, 0x73, 0xF9, 0xFD, 0xF5, 0xE5, 0xC9, 0x73, 0x06, 0xFC, 0x00, 0x00, 0x01, 0x03, 0x06, 0x04, 0x05, 0x05, 0x05, 0x04, 0x06, 0x03, 0x01, 0x00 };
int[930] amaniaMAIN = { 116, 64, 0x00, 0xFE, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0xFE, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xEF, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0xEF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x24, 0x24, 0xC4, 0x00, 0x08, 0xFC, 0x08, 0x08, 0x00, 0xE0, 0x10, 0x10, 0xE0, 0x00, 0xF0, 0x20, 0x10, 0x00, 0x08, 0xFC, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x04, 0x24, 0x24, 0xC0, 0x00, 0xE0, 0x10, 0x10, 0xE0, 0x00, 0xF0, 0x20, 0x40, 0x20, 0xF0, 0x00, 0xE0, 0x50, 0x50, 0x20, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x04, 0x32, 0x11, 0x01, 0x31, 0x11, 0x05, 0x2A, 0x04, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0xF9, 0x40, 0x40, 0xF8, 0x01, 0x00, 0xD0, 0x00, 0xC0, 0x21, 0x21, 0xC1, 0x00, 0xF1, 0x40, 0x40, 0x80, 0x00, 0x01, 0x00, 0x00, 0x30, 0x48, 0x48, 0x88, 0x00, 0x01, 0xC1, 0x21, 0x20, 0x00, 0xC0, 0x21, 0x21, 0xC1, 0x00, 0xE1, 0x40, 0x20, 0x00, 0xC1, 0xA0, 0xA0, 0x41, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x02, 0x04, 0x04, 0x02, 0x01, 0x01, 0x01, 0x02, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xE0, 0x50, 0xE8, 0xF4, 0xFC, 0xD4, 0x9C, 0xFC, 0xD8, 0x90, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x93, 0x90, 0x10, 0x03, 0x80, 0x40, 0x43, 0x80, 0x00, 0xC3, 0x03, 0x03, 0xC0, 0x03, 0xC0, 0x80, 0x43, 0x80, 0x00, 0x80, 0x40, 0x42, 0xF2, 0x02, 0x01, 0x00, 0x00, 0x01, 0x02, 0xE2, 0x20, 0xC1, 0x22, 0x12, 0xF9, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x40, 0x20, 0xD0, 0x38, 0x04, 0x04, 0x08, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x7F, 0x40, 0x5F, 0x4F, 0x47, 0x4F, 0x5F, 0x5F, 0x4F, 0x47, 0x47, 0x4F, 0x5F, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x44, 0x44, 0x44, 0x43, 0x40, 0x43, 0x44, 0x44, 0x43, 0x40, 0x43, 0x44, 0x44, 0x43, 0x40, 0x47, 0x40, 0x40, 0x47, 0x40, 0x43, 0x44, 0x44, 0x43, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x43, 0x42, 0x41, 0x42, 0x44, 0x4F, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x46, 0x4F, 0x4D, 0x46, 0x40, 0x40, 0x46, 0x4F, 0x4D, 0x46, 0x40, 0x40, 0x40, 0x7F, 0x00 };
int[10] amaniafadescreen = { 1, 8, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF };

// -----------------------------------------------------------------------------
// Level table selectors - Level_Use/Level_Add/Blocks/BlocksM upstream are
// each a [level0..level3,level0..level3,level4] 9-entry lookup indexed by
// "leveluse" (0-8, one per real difficulty tier - the first 4 layouts are
// reused twice each at rising ghost speed/count, the 5th only once). Kept
// as a resolve-by-id function rather than a stored raw pointer, matching
// this project's own established pattern (e.g. Tiny Dungeon's
// `tdngResolveBitmapArray`).
// -----------------------------------------------------------------------------

int amaniaLevelSlot( int leveluse )
{
    if( leveluse == 0 || leveluse == 4 ) return 0;
    if( leveluse == 1 || leveluse == 5 ) return 1;
    if( leveluse == 2 || leveluse == 6 ) return 2;
    if( leveluse == 3 || leveluse == 7 ) return 3;
    return 4;
}

int* amaniaLevelTable( int leveluse )
{
    int slot = amaniaLevelSlot( leveluse );
    if( slot == 0 ) return amanialevel0;
    if( slot == 1 ) return amanialevel1;
    if( slot == 2 ) return amanialevel2;
    if( slot == 3 ) return amanialevel3;
    return amanialevel4;
}

int* amaniaLevelAddTable( int leveluse )
{
    int slot = amaniaLevelSlot( leveluse );
    if( slot == 0 ) return amanialevel0_Add;
    if( slot == 1 ) return amanialevel1_Add;
    if( slot == 2 ) return amanialevel2_Add;
    if( slot == 3 ) return amanialevel3_Add;
    return amanialevel4_Add;
}

int* amaniaBlockTable( int leveluse )
{
    int slot = amaniaLevelSlot( leveluse );
    if( slot == 0 ) return amaniaBlock0;
    if( slot == 1 ) return amaniaBlock1;
    if( slot == 2 ) return amaniaBlock2;
    if( slot == 3 ) return amaniaBlock3;
    return amaniaBlock4;
}

int* amaniaBlockMaskTable( int leveluse )
{
    int slot = amaniaLevelSlot( leveluse );
    if( slot == 0 ) return amaniaBlock0m;
    if( slot == 1 ) return amaniaBlock1m;
    if( slot == 2 ) return amaniaBlock2m;
    if( slot == 3 ) return amaniaBlock3m;
    return amaniaBlock4m;
}

int* amaniaGhostModeTable( int mode )
{
    if( mode == 0 ) return amaniaGhost;
    if( mode == 1 ) return amaniaGhostBlack;
    return amaniaGhostEyes;
}

int* amaniaGhostMaskTable( int mode )
{
    if( mode == 2 ) return amaniaGhostEyeMask;
    return amaniaghostmask;
}

int* amaniaAvatarTable( int avatar )
{
    if( avatar == 0 ) return amaniaardumania;
    if( avatar == 1 ) return amaniaGhost;
    return amaniafruit;
}

// -----------------------------------------------------------------------------
// Globals - flattened from upstream's ALVL/GameVar/VAR_GAME_PLAY/FRUIT_VAR/
// NUM/SEQUENTIALANIM/SEQUENTIALFRAME/CAM_AMANIA/HIGH_SCORE structs, and the
// SpriteAmania/TIMER classes below.
// -----------------------------------------------------------------------------

enum AmaniaState
{
    AMANIA_STATE_SPLASH = 0,
    AMANIA_STATE_MENU = 1,
    AMANIA_STATE_SCORE_MENU = 2,
    AMANIA_STATE_LEVEL_TRANSITION = 3,
    AMANIA_STATE_PLAYING = 4
};

int amaniaState;
int amaniaTickSkipCounter;
int amaniaPlayTickAccum;
int amaniaPrevFire;
int amaniaFireEdge;

// Level
int amaniaLeveluse;
int amaniaLvlInvertBlock;
int amaniaLvlW;
int amaniaLvlH;
int[4] amaniaBigDotX;
int[4] amaniaBigDotY;
int amaniaLvlTotalDot;
int amaniaLvlDotCollected;
int amaniaFruitX;
int amaniaFruitY;
int amaniaMainX;
int amaniaMainY;
int amaniaGateExitX;
int amaniaGateExitY;

// GameVar
int amaniaAvatar;
int amaniaAvatarOneClick;
int amaniaAudio;
int amaniaLive;
int amaniaHighSpeed;
int amaniaHighSpeedTimer;
int amaniaTimer1;
int amaniaFruitTimeLeft;
int amaniaSpeedJump;
int amaniaSpeedGhost;
int amaniaTotalGhost;
int amaniaRolling;
int amaniaFrameAnim;
int amaniaGhostTimer;
int amaniaDemiTimer;
int amaniaGateX;
int amaniaGateY;

// SEQFRM (fade sequence)
int amaniaSeqActivateSequence;
int amaniaSeqExitTrigger;
int amaniaSeqExitCounter;
int amaniaSeqExitType;
int amaniaSeqFadeInOut;

// VAR_GAME_PLAY
int amaniaActivateGJump;
int amaniaGJump;
int amaniaGSpeed;
int amaniaGDuration;

// FRUIT_VAR
int[3] amaniaFruitUse;
int amaniaFvFlip;
int amaniaFvRF;

// NUM
int amaniaNumAM10000;
int amaniaNumAM1000;
int amaniaNumAM100;
int amaniaNumAM10;
int amaniaNumAM1;
int amaniaScores;
int amaniaScoresFruit;
int amaniaOneUp;

// SEQUENTIALANIM (death spin)
int amaniaAnimFrameDraw;
int amaniaAnimLatch1;
int amaniaAnimNumPass;

// CAM_AMANIA
int amaniaCamIsoShift;
int amaniaCamIsoScrollX;
int amaniaCamIsoScrollY;
int amaniaCamDriftGridX;
int amaniaCamDriftGridY;
int amaniaCamCamScanX;
int amaniaCamCamScanY;

// HIGH_SCORE
int[3] amaniaHSAvatar;
int[3] amaniaHSScore;

// LevelBuffer (2-bit-per-cell level state, flattened per Load_Selected_Level)
int[256] amaniaLevelBuffer;

// Sprites: 0 = player, 1..amaniaTotalGhost-1 = ghosts
struct AmaniaSprite
{
    int spriteType;
    int spriteDirection;
    int directionX;
    int directionY;
    int gridX;
    int gridY;
    int decX;
    int decY;
    int priority;
    int padX;
    int padY;
    int jmpPos;
    int jmpTrig;
    int jmpSeq;
    int spriteMode;
};
AmaniaSprite[8] amaniaSpk;

struct AmaniaTimer
{
    int activ;
    int startTime;
    int interval;
};

// Per-level "player ready" banner + one-up/score-refresh poll timers
AmaniaTimer amaniaShortStartTimer;
int amaniaShortStart;
AmaniaTimer amaniaLowCheckTimer;

// Death-pause / exit-pause sub-phase within PLAYING (see header comment -
// converts upstream's own synchronous My_delay_ms(1000)/My_delay_ms(400)
// calls into explicit multi-frame counts)
int amaniaDeathPauseFrames;
int amaniaExitPauseFrames;
int amaniaExitPendingType;

// Level-transition state (AnimLvlChange - the player walks off one edge of
// the screen, then walks back across trailing the level's own ghost count
// behind them, matching upstream's real animated sequence)
int amaniaTransA;
int amaniaTransD;
int amaniaTransAnimFrame;
int amaniaTransX;
AmaniaTimer amaniaTransTimer;

// Sound sequencer (shared small frame-stepped note-pair player)
// Sized 64 (not 24) specifically to hold the level-transition's own real
// 59-note walking melody (amaniascore, restored below) in full - every
// other cue in this file uses well under 24 notes.
int[64] amaniaSfxFreq;
int[64] amaniaSfxDur;
int amaniaSfxLen;
int amaniaSfxPos;
int amaniaSfxWaitFrames;

// Menu state
int amaniaMenuCursorPos;
int amaniaMenuPos;
int amaniaMenuX;
int amaniaMenuScr;
int amaniaMenuScr2;

// ScoreMenu state
int amaniaScoreMenuX;

// -----------------------------------------------------------------------------
// Framebuffer + blit primitives (see header comment - a real persistent
// pixel buffer, matching Gilbert in the Downland's own architecture)
// -----------------------------------------------------------------------------

int[1024] amaniaFrameBuffer;

// Per-frame ghost-position lookup for the isometric render loop below -
// same bucket-linked-list technique already proven in Tiny Mania
// (tmnGhostCellHead/tmnGhostCellNext) for this exact shape of problem: a
// per-cell scan that used to check every ghost's position against every
// scanned cell (O(cells x ghosts), up to ~130 cells x 7 ghosts = ~900
// comparisons/frame) is replaced with building this small index once per
// frame in O(ghosts), then doing an O(1) lookup per cell. Needs the
// *list* shape (head+next), not a single-slot-per-cell array, since
// several ghosts can genuinely share one cell right after a level/life
// starts (every ghost spawns at the same gate position).
int[143] amaniaGhostCellHead;
int[8] amaniaGhostCellNext;

void amaniaClearBuffer( void )
{
    int i;
    for( i = 0; i < 1024; i++ ) amaniaFrameBuffer[ i ] = 0;
}

int amaniaAbsI( int v )
{
    if( v < 0 ) return -v;
    return v;
}

// Sign-safe halve, matching AVR's arithmetic (floor-toward-negative-
// infinity) right-shift-by-1 - Vircon32's >> is logical (zero-fill), so a
// raw shift on a negative operand would be wrong. See header comment.
int amaniaShiftR1( int v )
{
    if( v >= 0 ) return v >> 1;
    return -( ( -v + 1 ) >> 1 );
}

int amaniaRecupeLineY( int valeur )
{
    if( valeur >= 0 ) return valeur >> 3;
    return -( ( -valeur + 7 ) >> 3 );
}

int amaniaRecupeDecalageY( int valeur )
{
    return valeur - ( amaniaRecupeLineY( valeur ) << 3 );
}

int amaniaSplitDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown ) return ( input << decalage ) & 0xFF;
    return input >> ( 8 - decalage );
}

int amaniaPageCount( int* table )
{
    int rawH = table[ 1 ];
    int pages = rawH >> 3;
    if( ( rawH - ( pages << 3 ) ) != 0 ) pages = pages + 1;
    return pages;
}

// Computes one composited output byte for a given sprite table/frame at a
// specific (col,page) - shared by every compositing mode below. wMax/
// picByte/recupeLineY/spriteYDecalage/w are all row-invariant (only depend
// on x/y/frame/table, not which column/page is being read), hoisted out by
// every caller rather than recomputed per pixel - the same lesson already
// applied proactively in Nohzdyve/Gilbert's own blit functions.
int amaniaSpriteByte( int x, int col, int page, int w, int wMax, int picByte, int recupeLineY, int spriteYDecalage, int* table )
{
    int spriteYLine = page - recupeLineY;
    int scanA = ( col - x ) + ( spriteYLine * w ) + 2;
    int outByte;
    if( scanA > wMax ) outByte = 0x00;
    else outByte = amaniaSplitDecalageY( spriteYDecalage, table[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int scanB = ( col - x ) + ( ( spriteYLine - 1 ) * w ) + 2;
        if( scanB <= wMax )
          outByte = outByte | amaniaSplitDecalageY( spriteYDecalage, table[ scanB + picByte ], 0 );
    }
    return outByte;
}

void amaniaDrawSelfMasked( int x, int y, int* table, int frame )
{
    if( y > 63 || x < -16 || y < -16 ) return;
    int w = table[ 0 ];
    int pages = amaniaPageCount( table );
    int wMax = ( pages * w ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = amaniaRecupeLineY( y );
    int spriteYDecalage = amaniaRecupeDecalageY( y );

    int firstPage = recupeLineY;
    int pageMax = firstPage + pages;
    if( firstPage < 0 ) firstPage = 0;
    if( pageMax > 7 ) pageMax = 7;
    int cMin = x, cMax = x + w - 1;
    if( cMin < 0 ) cMin = 0;
    if( cMax > 127 ) cMax = 127;

    // Inlined (no amaniaSpriteByte() call in the hot per-column loop) -
    // same "raw per-call overhead is the real cost without v32opt's own
    // inlining" reasoning as amaniaDrawEraseThenMask() above.
    int page, col;
    for( page = firstPage; page <= pageMax; page++ )
    {
        int spriteYLine = page - recupeLineY;
        int hasUpper = spriteYLine > 0;
        for( col = cMin; col <= cMax; col++ )
        {
            int scanA = ( col - x ) + ( spriteYLine * w ) + 2;
            int outByte;
            if( scanA > wMax ) outByte = 0;
            else if( spriteYDecalage ) outByte = ( table[ scanA + picByte ] << spriteYDecalage ) & 0xFF;
            else outByte = table[ scanA + picByte ];
            if( hasUpper && spriteYDecalage )
            {
                int scanB = ( col - x ) + ( ( spriteYLine - 1 ) * w ) + 2;
                if( scanB <= wMax ) outByte = outByte | ( table[ scanB + picByte ] >> ( 8 - spriteYDecalage ) );
            }
            if( outByte )
            {
                int idx = col + ( page * 128 );
                amaniaFrameBuffer[ idx ] = amaniaFrameBuffer[ idx ] | outByte;
            }
        }
    }
}

void amaniaDrawErase( int x, int y, int* table, int frame )
{
    if( y > 63 || x < -16 || y < -16 ) return;
    int w = table[ 0 ];
    int pages = amaniaPageCount( table );
    int wMax = ( pages * w ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = amaniaRecupeLineY( y );
    int spriteYDecalage = amaniaRecupeDecalageY( y );

    int firstPage = recupeLineY;
    int pageMax = firstPage + pages;
    if( firstPage < 0 ) firstPage = 0;
    if( pageMax > 7 ) pageMax = 7;
    int cMin = x, cMax = x + w - 1;
    if( cMin < 0 ) cMin = 0;
    if( cMax > 127 ) cMax = 127;

    int page, col;
    for( page = firstPage; page <= pageMax; page++ )
    {
        int spriteYLine = page - recupeLineY;
        int hasUpper = spriteYLine > 0;
        for( col = cMin; col <= cMax; col++ )
        {
            int scanA = ( col - x ) + ( spriteYLine * w ) + 2;
            int outByte;
            if( scanA > wMax ) outByte = 0;
            else if( spriteYDecalage ) outByte = ( table[ scanA + picByte ] << spriteYDecalage ) & 0xFF;
            else outByte = table[ scanA + picByte ];
            if( hasUpper && spriteYDecalage )
            {
                int scanB = ( col - x ) + ( ( spriteYLine - 1 ) * w ) + 2;
                if( scanB <= wMax ) outByte = outByte | ( table[ scanB + picByte ] >> ( 8 - spriteYDecalage ) );
            }
            if( outByte )
            {
                int idx = col + ( page * 128 );
                amaniaFrameBuffer[ idx ] = amaniaFrameBuffer[ idx ] & ( 0xFF - outByte );
            }
        }
    }
}

// Direct translation of ESPKIT.h's own BlackSquare() - a per-(sprite,page)
// clear mask used only by drawOverwrite, so a page entirely inside the
// sprite's real vertical span clears in full (0x00), a page at the
// sprite's own top/bottom edge only clears the bits the sprite actually
// covers there, and any other page is untouched (0xFF, "keep everything").
int amaniaBlackSquare( int rawY, int pageIndex, int* table )
{
    int result = 0xFF;
    int line = amaniaRecupeLineY( rawY );
    int dec = amaniaRecupeDecalageY( rawY );
    int mh = table[ 1 ];
    int mhLine = amaniaRecupeLineY( mh + rawY );
    int decH = amaniaRecupeDecalageY( mh + rawY );

    if( ( pageIndex * 8 ) > rawY && ( ( pageIndex * 8 ) + 7 ) < ( mh + rawY ) ) return 0x00;

    if( line == pageIndex ) result = ( result >> ( 8 - dec ) ) & 0xFF;
    if( mhLine == pageIndex ) result = ( result << decH ) & 0xFF;
    return result;
}

void amaniaDrawOverwrite( int x, int y, int* table, int frame )
{
    if( y > 63 || x < -16 || y < -16 ) return;
    int w = table[ 0 ];
    int pages = amaniaPageCount( table );
    int wMax = ( pages * w ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = amaniaRecupeLineY( y );
    int spriteYDecalage = amaniaRecupeDecalageY( y );

    int firstPage = recupeLineY;
    int pageMax = firstPage + pages;
    if( firstPage < 0 ) firstPage = 0;
    if( pageMax > 7 ) pageMax = 7;
    int cMin = x, cMax = x + w - 1;
    if( cMin < 0 ) cMin = 0;
    if( cMax > 127 ) cMax = 127;

    // Inlined (no amaniaSpriteByte() call in the hot per-column loop) -
    // this is the HUD panel's own primitive (amaniaPannel()'s "Digital"/
    // "Dlive" icons), called every single gameplay frame - the one blit
    // primitive in this file that had been left un-inlined until a direct
    // user question about UI/per-pixel costs prompted checking it too.
    int page, col;
    for( page = firstPage; page <= pageMax; page++ )
    {
        int mask = amaniaBlackSquare( y, page, table );
        int spriteYLine = page - recupeLineY;
        int hasUpper = spriteYLine > 0;
        for( col = cMin; col <= cMax; col++ )
        {
            int scanA = ( col - x ) + ( spriteYLine * w ) + 2;
            int outByte;
            if( scanA > wMax ) outByte = 0;
            else if( spriteYDecalage ) outByte = ( table[ scanA + picByte ] << spriteYDecalage ) & 0xFF;
            else outByte = table[ scanA + picByte ];
            if( hasUpper && spriteYDecalage )
            {
                int scanB = ( col - x ) + ( ( spriteYLine - 1 ) * w ) + 2;
                if( scanB <= wMax ) outByte = outByte | ( table[ scanB + picByte ] >> ( 8 - spriteYDecalage ) );
            }
            int idx = col + ( page * 128 );
            amaniaFrameBuffer[ idx ] = ( amaniaFrameBuffer[ idx ] & mask ) | outByte;
        }
    }
}

// Every wall/dot/fruit/ghost/player draw in the gameplay render loop is a
// paired "erase this footprint's mask, then self-mask-draw the real
// sprite" call at the *same* (x,y) - amaniaDrawErase()+amaniaDrawSelfMasked()
// back to back. Since the mask/sprite table in every one of these pairs
// share the same [width,height] header (confirmed for every real pair in
// this file: Block/BlockMask, Ghost/ghostmask, ardumania/ardumania_m,
// fruit/fruitMask, dotmask/tidot, BigDotMask/bigdot,
// player1ready/player1readyMask), the row/page/column bounds and every
// row-invariant value (wMax/recupeLineY/spriteYDecalage) are identical for
// both halves - computing them once and running one combined column loop
// (instead of two separate general-purpose calls each redoing that same
// setup and re-walking the same footprint) roughly halves the real
// per-cell rendering cost for every one of these paired draws, applying
// this project's own established "don't do the same per-call setup twice"
// lesson to the single largest remaining cost in gameplay (a dense maze
// is mostly wall cells, and every wall cell is exactly this pair).
void amaniaDrawEraseThenMask( int x, int y, int* maskTable, int maskFrame, int* spriteTable, int spriteFrame )
{
    if( y > 63 || x < -16 || y < -16 ) return;
    int w = spriteTable[ 0 ];
    int pages = amaniaPageCount( spriteTable );
    int wMax = ( pages * w ) + 1;
    int recupeLineY = amaniaRecupeLineY( y );
    int spriteYDecalage = amaniaRecupeDecalageY( y );
    int maskPicByte = maskFrame * ( wMax - 1 );
    int spritePicByte = spriteFrame * ( wMax - 1 );

    int firstPage = recupeLineY;
    int pageMax = firstPage + pages;
    if( firstPage < 0 ) firstPage = 0;
    if( pageMax > 7 ) pageMax = 7;
    int cMin = x, cMax = x + w - 1;
    if( cMin < 0 ) cMin = 0;
    if( cMax > 127 ) cMax = 127;

    // Fully inlined (no amaniaSpriteByte()/amaniaSplitDecalageY() calls in
    // the hot per-column loop) - without v32opt's own inlining pass
    // (this project's own standing test builds run with SKIP_V32OPT=1),
    // each nested function call in a loop this hot costs real, measurable
    // overhead on its own, on top of the work it actually does - the same
    // "raw per-call overhead was the dominant cost" finding already made
    // for Tiny Arkanoid/Run Dude Run/Tiny Arena's own arVBuf() elsewhere
    // in this project. spriteYLine only depends on page (not col), so
    // it's hoisted to the outer loop too.
    int page, col;
    for( page = firstPage; page <= pageMax; page++ )
    {
        int spriteYLine = page - recupeLineY;
        int hasUpper = spriteYLine > 0;
        for( col = cMin; col <= cMax; col++ )
        {
            int scanA = ( col - x ) + ( spriteYLine * w ) + 2;
            int scanB = ( col - x ) + ( ( spriteYLine - 1 ) * w ) + 2;
            int idx = col + ( page * 128 );

            int eraseByte;
            if( scanA > wMax ) eraseByte = 0;
            else if( spriteYDecalage ) eraseByte = ( maskTable[ scanA + maskPicByte ] << spriteYDecalage ) & 0xFF;
            else eraseByte = maskTable[ scanA + maskPicByte ];
            if( hasUpper && scanB <= wMax && spriteYDecalage )
              eraseByte = eraseByte | ( maskTable[ scanB + maskPicByte ] >> ( 8 - spriteYDecalage ) );
            if( eraseByte ) amaniaFrameBuffer[ idx ] = amaniaFrameBuffer[ idx ] & ( 0xFF - eraseByte );

            int drawByte;
            if( scanA > wMax ) drawByte = 0;
            else if( spriteYDecalage ) drawByte = ( spriteTable[ scanA + spritePicByte ] << spriteYDecalage ) & 0xFF;
            else drawByte = spriteTable[ scanA + spritePicByte ];
            if( hasUpper && scanB <= wMax && spriteYDecalage )
              drawByte = drawByte | ( spriteTable[ scanB + spritePicByte ] >> ( 8 - spriteYDecalage ) );
            if( drawByte ) amaniaFrameBuffer[ idx ] = amaniaFrameBuffer[ idx ] | drawByte;
        }
    }
}

// amaniaMAIN is the Menu screen's own full 116x64 background artwork - by
// far the widest/tallest sprite in this whole port (~928 real inner-loop
// iterations per amaniaDrawSelfMasked() call, roughly the size of a full-
// screen redraw crammed into one call), yet its own content never
// actually changes frame to frame. Computed into this cache exactly once
// (the first time the menu is ever shown) and merged into the real
// framebuffer every frame after with a plain OR loop instead of
// recomputing the whole sprite-blit each time - the same "cache what
// doesn't change every frame" lesson already established elsewhere in
// this project (e.g. Tiny Doc's row-scoped dirty tracking), just for a
// permanently-static asset rather than a conditionally-static one.
int[1024] amaniaMainCache;
int amaniaMainCacheReady;

void amaniaEnsureMainCache( void )
{
    if( amaniaMainCacheReady ) return;
    amaniaClearBuffer();
    amaniaDrawSelfMasked( 6, 0, amaniaMAIN, 0 );
    int i;
    for( i = 0; i < 1024; i++ ) amaniaMainCache[ i ] = amaniaFrameBuffer[ i ];
    amaniaMainCacheReady = 1;
}

void amaniaRenderFrame( void )
{
    md_beginFrame();
    int col, page;
    for( page = 0; page < 8; page++ )
      for( col = 0; col < 128; col++ )
        md_drawColumn( col, page, amaniaFrameBuffer[ col + ( page * 128 ) ] );
}

// -----------------------------------------------------------------------------
// Timer (flattened from upstream's TIMER class)
// -----------------------------------------------------------------------------

void amaniaTimerInit( AmaniaTimer* t, int interval )
{
    t->startTime = 0;
    t->interval = interval;
    t->activ = 0;
}

void amaniaTimerActivate( AmaniaTimer* t ) { t->activ = 1; }

int amaniaTimerTrigger( AmaniaTimer* t )
{
    if( t->activ == 0 ) return 1;
    if( t->startTime < t->interval ) { t->startTime = t->startTime + 1; return 0; }
    t->startTime = 0;
    return 1;
}

// -----------------------------------------------------------------------------
// Level grid (2-bit-per-cell occupancy: 0=empty/gate, 1=wall, 2=dot,
// 3=big dot) - direct translation of Read2Bits/Define2Bits/Initialize2Bits/
// Select_Byte/Select_Byte_2Bits. Binary literals (0b11) rewritten as
// decimal (3), matching this dialect's own lack of 0b support.
// -----------------------------------------------------------------------------

int amaniaSelectByte( int y_ ) { return y_ >> 3; }
int amaniaSelectByte2Bits( int y_ ) { return y_ >> 2; }

void amaniaInitialize2Bits( int x_, int y_ )
{
    if( x_ > amaniaLvlW - 1 || y_ > amaniaLvlH - 1 || x_ < 0 || y_ < 0 ) return;
    int byteNo = amaniaSelectByte2Bits( y_ );
    int bitScrool = ( y_ - ( byteNo << 2 ) ) << 1;
    int idx = x_ + ( byteNo * amaniaLvlW );
    amaniaLevelBuffer[ idx ] = amaniaLevelBuffer[ idx ] & ( 0xFF - ( 3 << bitScrool ) );
}

int amaniaRead2Bits( int x_, int y_ )
{
    int x1 = x_;
    if( x_ < 0 ) x1 = amaniaLvlW + x_;
    else if( x_ > amaniaLvlW - 1 ) x1 = x_ - amaniaLvlW;

    if( y_ > amaniaLvlH - 1 || y_ < 0 ) return 1;
    int byteNo = amaniaSelectByte2Bits( y_ );
    int byte_ = amaniaLevelBuffer[ x1 + ( byteNo * amaniaLvlW ) ];
    int bitScrool = ( y_ - ( byteNo << 2 ) ) << 1;
    int bitValue = byte_ & ( 3 << bitScrool );
    return bitValue >> bitScrool;
}

void amaniaDefine2Bits( int x_, int y_, int initValue )
{
    if( x_ > amaniaLvlW - 1 || y_ > amaniaLvlH - 1 || x_ < 0 || y_ < 0 ) return;
    int byteNo = amaniaSelectByte2Bits( y_ );
    int bitScrool = ( y_ - ( byteNo << 2 ) ) << 1;
    int mask = 0xFF - ( 3 << bitScrool );
    int valueToSet = ( initValue << bitScrool ) & ( 3 << bitScrool );
    int idx = x_ + ( byteNo * amaniaLvlW );
    amaniaLevelBuffer[ idx ] = ( amaniaLevelBuffer[ idx ] & mask ) | valueToSet;
}

// Real wall/collision check - a genuine out-of-bounds-Y gap in upstream's
// own ExploreMap() (see header comment), fixed with the same Y-bounds
// guard Read2Bits already has (return 1 = wall/blocked).
int amaniaExploreMap( int x_, int y_ )
{
    if( y_ < 0 || y_ > amaniaLvlH - 1 ) return 1;
    int x1 = x_;
    if( x_ < 0 ) x1 = amaniaLvlW + x_;
    else if( x_ > amaniaLvlW - 1 ) x1 = x_ - amaniaLvlW;

    int byteNo = amaniaSelectByte( y_ );
    int* levelTable = amaniaLevelTable( amaniaLeveluse );
    int byte_ = levelTable[ ( x1 + ( byteNo * amaniaLvlW ) ) + 2 ];
    if( ( byte_ & ( 1 << ( y_ - ( byteNo << 3 ) ) ) ) == 0 ) return 0;
    return 1;
}

int amaniaExploreMapChose( int x_, int y_, int spriteType_ )
{
    if( spriteType_ && x_ == amaniaGateX && y_ == amaniaGateY ) return 1;
    return amaniaExploreMap( x_, y_ );
}

void amaniaLoopScreen( int* x_ )
{
    if( *x_ > amaniaLvlW - 1 ) *x_ = *x_ - amaniaLvlW;
    if( *x_ < 0 ) *x_ = amaniaLvlW + *x_;
}

void amaniaOutCheck( int* val, int y )
{
    if( y < 0 || y > amaniaLvlH - 1 ) *val = 0;
}

// -----------------------------------------------------------------------------
// Sprite (flattened from upstream's SpriteAmania class) - one array slot
// per moving actor, SPK[0]=player, SPK[1..amaniaTotalGhost-1]=ghosts.
// -----------------------------------------------------------------------------

void amaniaSpriteInit( AmaniaSprite* s, int x_, int y_, int spriteType_ )
{
    s->spriteType = spriteType_;
    s->spriteDirection = 0;
    s->directionX = 2;
    s->directionY = 2;
    s->gridX = x_;
    s->gridY = y_;
    s->decX = 0;
    s->decY = 0;
    s->priority = 1;
    s->padX = 2;
    s->padY = 2;
    s->spriteMode = 0;
    s->jmpPos = 0;
    s->jmpTrig = 0;
    s->jmpSeq = 0;
}

int amaniaCheckPriorityX( AmaniaSprite* s )
{
    if( s->directionX == 0 )
    {
        if( amaniaExploreMapChose( s->gridX - 1, s->gridY, s->spriteType ) ) { s->directionX = 2; return 1; }
        s->directionY = 2;
        return 0;
    }
    if( s->directionX == 1 )
    {
        if( amaniaExploreMapChose( s->gridX + 1, s->gridY, s->spriteType ) ) { s->directionX = 2; return 1; }
        s->directionY = 2;
        return 0;
    }
    return 0;
}

int amaniaCheckPriorityY( AmaniaSprite* s )
{
    if( s->directionY == 0 )
    {
        if( amaniaExploreMapChose( s->gridX, s->gridY - 1, s->spriteType ) ) { s->directionY = 2; return 1; }
        s->directionX = 2;
        return 0;
    }
    if( s->directionY == 1 )
    {
        if( amaniaExploreMapChose( s->gridX, s->gridY + 1, s->spriteType ) ) { s->directionY = 2; return 1; }
        s->directionX = 2;
        return 0;
    }
    return 0;
}

void amaniaAdjustControl( AmaniaSprite* s )
{
    if( s->directionX == 1 ) { if( s->padX == 0 ) s->directionX = 0; }
    if( s->directionX == 0 ) { if( s->padX == 1 ) s->directionX = 1; }
    if( s->directionY == 1 ) { if( s->padY == 0 ) s->directionY = 0; }
    if( s->directionY == 0 ) { if( s->padY == 1 ) s->directionY = 1; }
    if( s->decX == 0 && s->decY == 0 )
    {
        if( s->padX != 2 ) s->directionX = s->padX;
        if( s->padY != 2 ) s->directionY = s->padY;
        if( s->priority == 0 ) { if( amaniaCheckPriorityX( s ) ) amaniaCheckPriorityY( s ); }
        else if( s->priority == 1 ) { if( amaniaCheckPriorityY( s ) ) amaniaCheckPriorityX( s ); }
    }
    s->padX = 2;
    s->padY = 2;
}

void amaniaGoUp( AmaniaSprite* s )
{
    if( s->decX == 0 && s->decY == 0 )
    {
        if( amaniaExploreMapChose( s->gridX, s->gridY - 1, s->spriteType ) ) { s->directionY = 2; return; }
    }
    if( s->decY > ( -YSTEP ) + 1 ) s->decY = s->decY - 1;
    else { s->decY = 0; s->gridY = s->gridY - 1; }
    s->decX = -amaniaShiftR1( s->decY );
    s->directionX = 2;
    s->spriteDirection = 9;
}

void amaniaGoDown( AmaniaSprite* s )
{
    if( s->decX == 0 && s->decY == 0 )
    {
        if( amaniaExploreMapChose( s->gridX, s->gridY + 1, s->spriteType ) ) { s->directionY = 2; return; }
    }
    if( s->decY < 0 ) s->decY = s->decY + 1;
    else { s->decY = ( -YSTEP ) + 1; s->gridY = s->gridY + 1; }
    s->decX = -amaniaShiftR1( s->decY );
    s->directionX = 2;
    s->spriteDirection = 6;
}

void amaniaGoRight( AmaniaSprite* s )
{
    if( s->decX == 0 && s->decY == 0 )
    {
        if( amaniaExploreMapChose( s->gridX + 1, s->gridY, s->spriteType ) ) { s->directionX = 2; return; }
    }
    if( s->decX < 0 ) s->decX = s->decX + 1;
    else
    {
        s->decX = ( -XSTEP ) + 1;
        if( s->gridX == amaniaLvlW - 1 ) s->gridX = 0; else s->gridX = s->gridX + 1;
    }
    s->directionY = 2;
    s->spriteDirection = 0;
}

void amaniaGoLeft( AmaniaSprite* s )
{
    if( s->decX == 0 && s->decY == 0 )
    {
        if( amaniaExploreMapChose( s->gridX - 1, s->gridY, s->spriteType ) ) { s->directionX = 2; return; }
    }
    if( s->decX > ( -XSTEP ) + 1 ) s->decX = s->decX - 1;
    else
    {
        s->decX = 0;
        if( s->gridX == 0 ) s->gridX = amaniaLvlW - 1; else s->gridX = s->gridX - 1;
    }
    s->directionY = 2;
    s->spriteDirection = 3;
}

void amaniaRefreshJump( AmaniaSprite* s )
{
    if( s->jmpTrig == 1 )
    {
        s->jmpPos = amaniaSaut[ s->jmpSeq ];
        if( s->jmpSeq < 39 ) s->jmpSeq = s->jmpSeq + 1;
        else { s->jmpSeq = 0; s->jmpTrig = 2; }
    }
}

void amaniaRefreshMove( AmaniaSprite* s )
{
    if( s->jmpTrig == 1 )
    {
        s->jmpPos = amaniaSaut[ s->jmpSeq ];
        if( s->jmpSeq < 39 ) s->jmpSeq = s->jmpSeq + 1;
        else { s->jmpSeq = 0; s->jmpTrig = 2; }
    }
    if( s->directionX == 0 ) { amaniaGoLeft( s ); s->priority = 1; return; }
    if( s->directionX == 1 ) { amaniaGoRight( s ); s->priority = 1; return; }
    if( s->directionY == 0 ) { amaniaGoUp( s ); s->priority = 0; return; }
    if( s->directionY == 1 ) { amaniaGoDown( s ); s->priority = 0; return; }
}

// Forward declarations for the handful of "begin state"/cross-referenced
// functions defined later in the file but called earlier - matching this
// project's own established precedent (e.g. Tiny Pipe's forward
// declarations, Gilbert in the Downland's tdngResolveBitmapArray-style
// forward refs) rather than hand-verifying exact define-before-use order
// across this file's own ~100 functions.
void amaniaProgramExitMode( int exitMode, int fadeInOut );
void amaniaBeginLevel( int leveluse );
void amaniaBeginResetPos( void );
void amaniaCheckNewHighScore( void );
void amaniaSoundSystem( int val_ );

// -----------------------------------------------------------------------------
// RandVar - upstream's own *deterministic* 24-entry cycling table, not a
// real RNG call - kept as a literal translation.
// -----------------------------------------------------------------------------

int amaniaRandVar( void )
{
    if( amaniaRolling > 0 ) amaniaRolling = amaniaRolling - 1;
    else amaniaRolling = 23;
    return amaniarndmove[ amaniaRolling ];
}

// -----------------------------------------------------------------------------
// Sound sequencer - Vircon32's audio channel has no queue (this project's
// own well-documented "N synchronous Sound() calls collapse to just the
// last tone" bug family), so every multi-call SoundSystem()/deadSound()
// burst is routed through this shared frame-stepped note player instead.
// The two largest bursts (SoundSystem(1)'s ~62-note ghost-eaten cue and
// SoundSystem(4)/deadSound()'s ~68-note death cue) are downsampled to a
// representative handful of notes, matching this project's established
// fix for oversized computed sweeps (e.g. Tiny Missile/Tiny Pipe).
// -----------------------------------------------------------------------------

void amaniaSfxClear( void ) { amaniaSfxLen = 0; amaniaSfxPos = 0; amaniaSfxWaitFrames = 0; }

void amaniaSfxAdd( int freq, int dur )
{
    if( amaniaSfxLen < 64 )
    {
        amaniaSfxFreq[ amaniaSfxLen ] = freq;
        amaniaSfxDur[ amaniaSfxLen ] = dur;
        amaniaSfxLen = amaniaSfxLen + 1;
    }
}

// Waits out each note's own real duration before advancing, rather than
// firing exactly one note per real frame regardless of length. For every
// existing burst in this file (fruit/ghost-eaten/dot/dead - all built
// from very short dur values, ~1-10) that real duration rounds down to
// under one frame, so this is a no-op there and those still sound exactly
// as before. It matters for AnimLvlChange's own confirm chime
// (Snd(100,255);Snd(60,255);) - each of those genuinely lasts ~80-100ms
// (matching the same freq/dur -> real-seconds formula every other
// ELECTROLIB.h-lineage game in this project already uses:
// durationSeconds = dur*2*(255-freq)/1e6) - without this wait, the first
// note was only ever audible for a single ~16.7ms frame before the
// second note's own Sound() call silently replaced it (Vircon32's audio
// channel has no queue), clipping both tones almost to nothing.
void amaniaAdvanceSfx( void )
{
    if( !amaniaAudio ) return;
    if( amaniaSfxPos >= amaniaSfxLen ) return;
    if( amaniaSfxWaitFrames > 0 ) { amaniaSfxWaitFrames = amaniaSfxWaitFrames - 1; return; }
    int freq = amaniaSfxFreq[ amaniaSfxPos ];
    int dur = amaniaSfxDur[ amaniaSfxPos ];
    Sound( freq, dur );
    int microseconds = dur * 2 * ( 255 - freq );
    int frames = microseconds / 16667;
    if( frames > 0 ) amaniaSfxWaitFrames = frames - 1;
    amaniaSfxPos = amaniaSfxPos + 1;
}

void amaniaSoundSystem( int val_ )
{
    amaniaSfxClear();
    if( !amaniaAudio ) return;
    if( val_ == 0 )
    {
        int f;
        for( f = 150; f < 250 && amaniaSfxLen < 24; f = f + 8 ) amaniaSfxAdd( f, 1 );
    }
    else if( val_ == 1 )
    {
        int f;
        for( f = 1; f < 125 && amaniaSfxLen < 22; f = f + 16 ) { amaniaSfxAdd( f, 3 ); amaniaSfxAdd( f + 125, 2 ); }
    }
    else if( val_ == 2 )
    {
        amaniaSfxAdd( 250, 1 ); amaniaSfxAdd( 120, 2 ); amaniaSfxAdd( 250, 1 );
    }
    else if( val_ == 3 )
    {
        amaniaSfxAdd( 140, 6 ); amaniaSfxAdd( 40, 3 ); amaniaSfxAdd( 140, 6 );
    }
    else if( val_ == 4 )
    {
        int f;
        for( f = 1; f < 200 && amaniaSfxLen < 22; f = f + 24 ) { amaniaSfxAdd( f, 4 ); amaniaSfxAdd( f + 50, 2 ); }
    }
    else if( val_ == 5 )
    {
        Sound( 200, 10 );
    }
}

// -----------------------------------------------------------------------------
// EEPROM - upstream's own 3-slot leaderboard, restored through this
// project's own eepromShim.h - see header comment for why the marker byte
// is dropped (the shim's own magic/checksum already covers that role).
// -----------------------------------------------------------------------------

void amaniaLoadHighScores( void )
{
    int t;
    for( t = 0; t < 3; t++ )
    {
        amaniaHSAvatar[ t ] = eeprom_read_byte( t * 3 );
        amaniaHSScore[ t ] = eeprom_read_word( ( t * 3 ) + 1 );
        if( amaniaHSScore[ t ] == 65535 ) amaniaHSScore[ t ] = 0;
        if( amaniaHSAvatar[ t ] < 0 || amaniaHSAvatar[ t ] > 2 ) amaniaHSAvatar[ t ] = 0;
    }
}

void amaniaSaveHighScores( void )
{
    int t;
    for( t = 0; t < 3; t++ )
    {
        eeprom_write_byte( t * 3, amaniaHSAvatar[ t ] );
        eeprom_write_word( ( t * 3 ) + 1, amaniaHSScore[ t ] );
    }
}

void amaniaClassementHighScore( void )
{
    int i, j;
    for( i = 0; i < 3; i++ )
    {
        for( j = i + 1; j < 3; j++ )
        {
            if( amaniaHSScore[ j ] > amaniaHSScore[ i ] )
            {
                int ta = amaniaHSAvatar[ i ]; int ts = amaniaHSScore[ i ];
                amaniaHSAvatar[ i ] = amaniaHSAvatar[ j ]; amaniaHSScore[ i ] = amaniaHSScore[ j ];
                amaniaHSAvatar[ j ] = ta; amaniaHSScore[ j ] = ts;
            }
        }
    }
}

void amaniaCheckNewHighScore( void )
{
    if( amaniaScores > amaniaHSScore[ 2 ] )
    {
        amaniaHSScore[ 2 ] = amaniaScores;
        amaniaHSAvatar[ 2 ] = amaniaAvatar;
        amaniaClassementHighScore();
        amaniaSaveHighScores();
    }
}

// -----------------------------------------------------------------------------
// Level loading
// -----------------------------------------------------------------------------

void amaniaInitLevelBuffer( void )
{
    int t;
    for( t = 0; t < 256; t++ ) amaniaLevelBuffer[ t ] = 0;
}

int amaniaTotalDotCountTest( void )
{
    int add_ = 0;
    int y_, x_;
    for( y_ = 0; y_ < amaniaLvlH; y_++ )
      for( x_ = 0; x_ < amaniaLvlW; x_++ )
      {
          int val_ = amaniaRead2Bits( x_, y_ );
          if( val_ == 2 || val_ == 3 ) add_ = add_ + 1;
      }
    return add_;
}

void amaniaBigDotAssign( void )
{
    int* addTable = amaniaLevelAddTable( amaniaLeveluse );
    int t; int off = 0;
    for( t = 0; t < 4; t++ )
    {
        amaniaBigDotX[ t ] = addTable[ off ];
        amaniaBigDotY[ t ] = addTable[ off + 1 ];
        amaniaDefine2Bits( amaniaBigDotX[ t ], amaniaBigDotY[ t ], 3 );
        off = off + 2;
    }
}

int amaniaInvertBlock( int lvl_ )
{
    if( lvl_ == 3 || lvl_ == 7 ) return 1;
    return 0;
}

void amaniaCopyLevelToMem( void )
{
    int y, x;
    for( y = 0; y < amaniaLvlH; y++ )
      for( x = 0; x < amaniaLvlW; x++ )
      {
          int v;
          if( amaniaExploreMap( x, y ) == 1 ) v = 1; else v = 2;
          amaniaDefine2Bits( x, y, v );
      }
}

void amaniaDeleteSerialDot( void )
{
    int* addTable = amaniaLevelAddTable( amaniaLeveluse );
    int max_ = addTable[ 16 ];
    int x;
    for( x = 0; x < max_; x++ )
    {
        int val_ = addTable[ 17 + x ];
        int vertical = val_ / amaniaLvlW;
        int horizontal = val_ - ( vertical * amaniaLvlW );
        amaniaDefine2Bits( horizontal, vertical, 0 );
    }
}

void amaniaSetGhost( void )
{
    int* addTable = amaniaLevelAddTable( amaniaLeveluse );
    int t;
    for( t = 1; t < amaniaTotalGhost; t++ )
      amaniaSpriteInit( &amaniaSpk[ t ], addTable[ 12 ], addTable[ 13 ], 0 );
}

void amaniaResetSpritesPos( void )
{
    amaniaSpriteInit( &amaniaSpk[ 0 ], amaniaMainX, amaniaMainY, 1 );
    amaniaSetGhost();
    amaniaGhostTimer = 0;
}

void amaniaRemoveDotNotUse( void )
{
    amaniaDefine2Bits( amaniaMainX, amaniaMainY, 0 );
}

// Integer port of upstream's ArduMap() (a linear-interpolation-plus-round
// helper) - avoids float casts entirely (every real call site here only
// ever passes plain integers), doing the rounding via integer arithmetic
// (round-half-up on the absolute value) instead of relying on roundf().
int amaniaMapI( int x, int inMin, int inMax, int outMin, int outMax )
{
    int num = ( x - inMin ) * ( outMax - outMin );
    int den = inMax - inMin;
    if( den == 0 ) return outMin;
    int absNum = amaniaAbsI( num );
    int absDen = amaniaAbsI( den );
    int q = ( absNum + ( absDen / 2 ) ) / absDen;
    if( ( num < 0 ) != ( den < 0 ) ) q = -q;
    return q + outMin;
}

void amaniaInitGamePlay( int lvl_ )
{
    int ml = TOTAL_LEVEL - 1;
    if( lvl_ > 1 ) amaniaActivateGJump = 1; else amaniaActivateGJump = 0;
    amaniaGJump = amaniaMapI( lvl_, 0, ml, 80, 10 );
    amaniaGSpeed = amaniaMapI( lvl_, 0, ml, 2, 0 );
    amaniaGDuration = amaniaMapI( lvl_, 0, ml, 700, 200 );
    amaniaTotalGhost = amaniaMapI( lvl_, 0, ml, 4, ABSOLUTEMAXGHOST );
    if( lvl_ < 6 ) amaniaFruitUse[ 0 ] = lvl_; else amaniaFruitUse[ 0 ] = arand( 7 );
    amaniaFruitUse[ 1 ] = 6;
    amaniaFruitUse[ 2 ] = 7;
}

void amaniaLoadSelectedLevel( int levelToLoad )
{
    amaniaInitLevelBuffer();
    amaniaLvlTotalDot = 0;
    amaniaLvlDotCollected = 0;
    amaniaLeveluse = levelToLoad;
    int* levelTable = amaniaLevelTable( levelToLoad );
    amaniaLvlW = levelTable[ 0 ];
    amaniaLvlH = levelTable[ 1 ];
    amaniaInitGamePlay( levelToLoad );
    amaniaLvlInvertBlock = amaniaInvertBlock( levelToLoad );
    amaniaCopyLevelToMem();
    int* addTable = amaniaLevelAddTable( levelToLoad );
    amaniaFruitX = addTable[ 8 ];
    amaniaFruitY = addTable[ 9 ];
    amaniaMainX = addTable[ 10 ];
    amaniaMainY = addTable[ 11 ];
    amaniaBigDotAssign();
    amaniaResetSpritesPos();
    amaniaDeleteSerialDot();
    amaniaRemoveDotNotUse();
    amaniaLvlTotalDot = amaniaTotalDotCountTest();
    amaniaGateX = addTable[ 14 ];
    amaniaGateY = addTable[ 15 ];
    amaniaGateExitX = amaniaGateX;
    amaniaGateExitY = amaniaGateY + 1;
}

// -----------------------------------------------------------------------------
// Camera / HUD helpers
// -----------------------------------------------------------------------------

void amaniaCenterScreen( void )
{
    amaniaCamIsoScrollY = -amaniaSpk[ 0 ].decY;
    amaniaCamDriftGridY = amaniaSpk[ 0 ].gridY - 3;
    if( amaniaCamIsoScrollY != 0 ) amaniaCamIsoScrollX = -( amaniaCamIsoScrollY >> 1 );
    amaniaCamIsoScrollX = -amaniaSpk[ 0 ].decX;
    amaniaCamDriftGridX = amaniaSpk[ 0 ].gridX - 6;
    amaniaRandVar();
}

int amaniaRCupAnim( int d )
{
    return amaniaFrameAnim + amaniaSpk[ d ].spriteDirection;
}

int amaniaRCupAnimMask( int d )
{
    if( amaniaSpk[ d ].spriteMode == 2 ) return amaniaRCupAnim( d );
    return 0;
}

int amaniaGhostTime( int t )
{
    if( amaniaGhostTimer > 0 && amaniaGhostTimer < 125 && amaniaSpk[ t ].spriteMode == 1 )
    {
        if( amaniaDemiTimer < 3 ) return 0;
    }
    return amaniaSpk[ t ].spriteMode;
}

int amaniaMaskUse( void )
{
    if( amaniaAnimLatch1 > 4 ) return 2;
    return amaniaLvlInvertBlock;
}

// -----------------------------------------------------------------------------
// Ghost AI
// -----------------------------------------------------------------------------

int amaniaGateExit( int val_ )
{
    if( amaniaSpk[ val_ ].gridX == amaniaGateExitX && amaniaSpk[ val_ ].gridY == amaniaGateExitY ) return 0;
    return 1;
}

int amaniaRNDGhost( int t_ )
{
    if( amaniaSpk[ t_ ].spriteMode == 1 ) return amaniaRandVar();
    return 0;
}

int amaniaTrackObjectifX( int val_ )
{
    if( amaniaSpk[ val_ ].spriteMode == 2 || amaniaRNDGhost( val_ ) )
    {
        int* addTable = amaniaLevelAddTable( amaniaLeveluse );
        return addTable[ 12 ];
    }
    return amaniaSpk[ 0 ].gridX;
}

int amaniaTrackObjectifY( int val_ )
{
    if( amaniaSpk[ val_ ].spriteMode == 2 || amaniaRNDGhost( val_ ) )
    {
        int* addTable = amaniaLevelAddTable( amaniaLeveluse );
        return addTable[ 13 ];
    }
    return amaniaSpk[ 0 ].gridY;
}

void amaniaGhostUturn( int t )
{
    if( amaniaSpk[ t ].spriteDirection == 0 ) amaniaSpk[ t ].padX = 0;
    else if( amaniaSpk[ t ].spriteDirection == 3 ) amaniaSpk[ t ].padX = 1;
    else if( amaniaSpk[ t ].spriteDirection == 6 ) amaniaSpk[ t ].padY = 0;
    else if( amaniaSpk[ t ].spriteDirection == 9 ) amaniaSpk[ t ].padY = 1;
}

void amaniaSwitchModeGhost( int tested_, int set_ )
{
    int t;
    for( t = 1; t < amaniaTotalGhost; t++ )
      if( amaniaSpk[ t ].spriteMode == tested_ )
      {
          amaniaSpk[ t ].spriteMode = set_;
          amaniaGhostUturn( t );
      }
}

void amaniaGhostRespawn( int val_ )
{
    int* addTable = amaniaLevelAddTable( amaniaLeveluse );
    if( amaniaSpk[ val_ ].gridX == addTable[ 14 ] && amaniaSpk[ val_ ].gridY == addTable[ 15 ] )
      if( amaniaSpk[ val_ ].spriteMode == 2 ) amaniaSpk[ val_ ].spriteMode = 0;
}

void amaniaGhostRight( int t )
{
    int add_ = 0;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX + 1, amaniaSpk[ t ].gridY ) == 1 ) add_ = add_ + 1;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX, amaniaSpk[ t ].gridY - 1 ) == 1 ) add_ = add_ + 3;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX, amaniaSpk[ t ].gridY + 1 ) == 1 ) add_ = add_ + 5;

    if( add_ == 9 ) { amaniaSpk[ t ].padX = 0; return; }
    if( add_ == 8 ) { amaniaSpk[ t ].padX = 1; return; }
    if( add_ == 4 ) { amaniaSpk[ t ].padY = 1; return; }
    if( add_ == 6 ) { amaniaSpk[ t ].padY = 0; return; }
    if( add_ == 1 ) { amaniaSpk[ t ].padY = amaniaRandVar(); return; }
    if( add_ == 0 )
    {
        if( amaniaRandVar() )
        {
            if( amaniaSpk[ t ].gridY < amaniaTrackObjectifY( t ) ) amaniaSpk[ t ].padY = amaniaGateExit( t );
            else amaniaSpk[ t ].padY = 0;
        }
    }
    if( add_ == 3 )
    {
        int cond = 1;
        if( amaniaSpk[ t ].gridY < amaniaTrackObjectifY( t ) ) cond = 0;
        if( cond ) { amaniaSpk[ t ].padX = 1; return; }
        amaniaSpk[ t ].padY = 1;
        return;
    }
    if( add_ == 5 )
    {
        int cond = 1;
        if( amaniaSpk[ t ].gridY > amaniaTrackObjectifY( t ) ) cond = 0;
        if( cond ) { amaniaSpk[ t ].padX = 1; return; }
        amaniaSpk[ t ].padY = 0;
        return;
    }
}

void amaniaGhostLeft( int t )
{
    int add_ = 0;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX - 1, amaniaSpk[ t ].gridY ) == 1 ) add_ = add_ + 1;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX, amaniaSpk[ t ].gridY - 1 ) == 1 ) add_ = add_ + 3;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX, amaniaSpk[ t ].gridY + 1 ) == 1 ) add_ = add_ + 5;

    if( add_ == 9 ) { amaniaSpk[ t ].padX = 1; return; }
    if( add_ == 8 ) { amaniaSpk[ t ].padX = 0; return; }
    if( add_ == 4 ) { amaniaSpk[ t ].padY = 1; return; }
    if( add_ == 6 ) { amaniaSpk[ t ].padY = 0; return; }
    if( add_ == 1 ) { amaniaSpk[ t ].padY = amaniaRandVar(); }
    if( add_ == 0 )
    {
        if( amaniaRandVar() )
        {
            if( amaniaSpk[ t ].gridY < amaniaTrackObjectifY( t ) ) amaniaSpk[ t ].padY = amaniaGateExit( t );
            else amaniaSpk[ t ].padY = 0;
        }
    }
    if( add_ == 3 )
    {
        int cond = 1;
        if( amaniaSpk[ t ].gridY < amaniaTrackObjectifY( t ) ) cond = 0;
        if( cond ) { amaniaSpk[ t ].padX = 0; return; }
        amaniaSpk[ t ].padY = 1;
        return;
    }
    if( add_ == 5 )
    {
        int cond = 1;
        if( amaniaSpk[ t ].gridY > amaniaTrackObjectifY( t ) ) cond = 0;
        if( cond ) { amaniaSpk[ t ].padX = 0; return; }
        amaniaSpk[ t ].padY = 0;
        return;
    }
}

void amaniaGhostUp( int t )
{
    int add_ = 0;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX, amaniaSpk[ t ].gridY - 1 ) == 1 ) add_ = add_ + 1;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX - 1, amaniaSpk[ t ].gridY ) == 1 ) add_ = add_ + 3;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX + 1, amaniaSpk[ t ].gridY ) == 1 ) add_ = add_ + 5;

    if( add_ == 9 ) { amaniaSpk[ t ].padY = 1; return; }
    if( add_ == 8 ) { amaniaSpk[ t ].padY = 0; return; }
    if( add_ == 4 ) { amaniaSpk[ t ].padX = 1; return; }
    if( add_ == 6 ) { amaniaSpk[ t ].padX = 0; return; }
    if( add_ == 1 ) { amaniaSpk[ t ].padX = amaniaRandVar(); }
    if( add_ == 0 )
    {
        if( amaniaRandVar() )
        {
            if( amaniaSpk[ t ].gridX < amaniaTrackObjectifX( t ) ) amaniaSpk[ t ].padX = 1;
            else amaniaSpk[ t ].padX = 0;
        }
    }
    if( add_ == 3 )
    {
        int cond = 1;
        if( amaniaSpk[ t ].gridX < amaniaTrackObjectifX( t ) ) cond = 0;
        if( cond ) { amaniaSpk[ t ].padY = 0; return; }
        amaniaSpk[ t ].padX = 1;
        return;
    }
    if( add_ == 5 )
    {
        int cond = 1;
        if( amaniaSpk[ t ].gridX > amaniaTrackObjectifX( t ) ) cond = 0;
        if( cond ) { amaniaSpk[ t ].padY = 0; return; }
        amaniaSpk[ t ].padX = 0;
        return;
    }
}

void amaniaGhostDown( int t )
{
    int add_ = 0;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX, amaniaSpk[ t ].gridY + 1 ) == 1 ) add_ = add_ + 1;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX - 1, amaniaSpk[ t ].gridY ) == 1 ) add_ = add_ + 3;
    if( amaniaRead2Bits( amaniaSpk[ t ].gridX + 1, amaniaSpk[ t ].gridY ) == 1 ) add_ = add_ + 5;

    if( add_ == 9 ) { amaniaSpk[ t ].padY = 0; return; }
    if( add_ == 8 ) { amaniaSpk[ t ].padY = 1; return; }
    if( add_ == 4 ) { amaniaSpk[ t ].padX = 1; return; }
    if( add_ == 6 ) { amaniaSpk[ t ].padX = 0; return; }
    if( add_ == 1 ) { amaniaSpk[ t ].padX = amaniaRandVar(); }
    if( add_ == 0 )
    {
        if( amaniaRandVar() )
        {
            if( amaniaSpk[ t ].gridX < amaniaTrackObjectifX( t ) ) amaniaSpk[ t ].padX = 1;
            else amaniaSpk[ t ].padX = 0;
        }
    }
    if( add_ == 3 )
    {
        int cond = 1;
        if( amaniaSpk[ t ].gridX < amaniaTrackObjectifX( t ) ) cond = 0;
        if( cond ) { amaniaSpk[ t ].padY = 1; return; }
        amaniaSpk[ t ].padX = 1;
        return;
    }
    if( add_ == 5 )
    {
        int cond = 1;
        if( amaniaSpk[ t ].gridX > amaniaTrackObjectifX( t ) ) cond = 0;
        if( cond ) { amaniaSpk[ t ].padY = 1; return; }
        amaniaSpk[ t ].padX = 0;
        return;
    }
}

void amaniaGhostDirectionChoser( void )
{
    int t;
    for( t = 1; t < amaniaTotalGhost; t++ )
    {
        if( amaniaSpk[ t ].decX == 0 && amaniaSpk[ t ].decY == 0 )
        {
            amaniaGhostRespawn( t );
            if( amaniaSpk[ t ].spriteDirection == 0 ) amaniaGhostRight( t );
            else if( amaniaSpk[ t ].spriteDirection == 3 ) amaniaGhostLeft( t );
            else if( amaniaSpk[ t ].spriteDirection == 6 ) amaniaGhostDown( t );
            else if( amaniaSpk[ t ].spriteDirection == 9 ) amaniaGhostUp( t );
        }
    }
}

void amaniaRefreshMovingGhost( void )
{
    int l;
    for( l = 1; l < amaniaTotalGhost; l++ )
    {
        if( amaniaSpeedGhost == 0 || amaniaSpk[ l ].spriteMode == 2 )
        {
            amaniaAdjustControl( &amaniaSpk[ l ] );
            amaniaRefreshMove( &amaniaSpk[ l ] );
        }
        else
        {
            amaniaRefreshJump( &amaniaSpk[ l ] );
        }
    }
}

void amaniaGhostJumpCalculate( void )
{
    if( amaniaActivateGJump == 0 ) return;
    if( amaniaSpeedJump != 0 ) return;
    int t;
    for( t = 1; t < 3; t++ )
    {
        int val_ = amaniaAbsI( amaniaSpk[ t ].gridX - amaniaSpk[ 0 ].gridX );
        int val2_ = amaniaAbsI( amaniaSpk[ t ].gridY - amaniaSpk[ 0 ].gridY );
        if( val_ <= 1 && val2_ <= 1 && amaniaSpk[ 0 ].jmpSeq > 3 )
        {
            if( amaniaSpk[ t ].jmpTrig == 0 ) { amaniaSpk[ t ].jmpTrig = 1; return; }
        }
        if( amaniaSpk[ t ].jmpTrig == 2 ) amaniaSpk[ t ].jmpTrig = 0;
    }
}

// -----------------------------------------------------------------------------
// Player input/movement, collision, fruit, scoring
// -----------------------------------------------------------------------------

void amaniaCheckPadFunction( void )
{
    if( isUpPressed() ) amaniaSpk[ 0 ].padY = 0;
    else if( isDownPressed() ) amaniaSpk[ 0 ].padY = 1;
    else amaniaSpk[ 0 ].padY = 2;

    if( isRightPressed() ) amaniaSpk[ 0 ].padX = 1;
    else if( isLeftPressed() ) amaniaSpk[ 0 ].padX = 0;
    else amaniaSpk[ 0 ].padX = 2;

    if( isFirePressed() )
    {
        if( amaniaSpk[ 0 ].jmpTrig == 0 ) amaniaSpk[ 0 ].jmpTrig = 1;
    }
    else
    {
        if( amaniaSpk[ 0 ].jmpTrig == 2 ) amaniaSpk[ 0 ].jmpTrig = 0;
    }
}

void amaniaRefreshMovingArdu( void )
{
    amaniaAdjustControl( &amaniaSpk[ 0 ] );
    amaniaRefreshMove( &amaniaSpk[ 0 ] );
}

// A same-family simplification as the fade-sequence's own dropped leading
// pause (see amaniaProgramExitMode below): upstream's own collision()
// blocks for a real My_delay_ms(250) right after playing the ghost-eaten
// cue, before awarding points - dropped here (the sound/score/mode-switch
// all still happen on the exact same frame, just without the extra
// quarter-second freeze), matching this project's own "effort/fidelity
// tradeoff for a purely decorative pause" precedent.
int amaniaCollision( void )
{
    int t;
    for( t = 1; t < amaniaTotalGhost; t++ )
    {
        int sprite1x = ( amaniaSpk[ 0 ].gridX * ( XSTEP - 1 ) ) + amaniaSpk[ 0 ].decX;
        int sprite1y = ( amaniaSpk[ 0 ].gridY * ( YSTEP - 1 ) ) + amaniaSpk[ 0 ].decY;
        int sprite2x = ( amaniaSpk[ t ].gridX * ( XSTEP - 1 ) ) + amaniaSpk[ t ].decX;
        int sprite2y = ( amaniaSpk[ t ].gridY * ( YSTEP - 1 ) ) + amaniaSpk[ t ].decY;
        int xDist = sprite1x - sprite2x;
        int yDist = sprite1y - sprite2y;
        int zDist = amaniaSpk[ 0 ].jmpPos - amaniaSpk[ t ].jmpPos;
        if( amaniaAbsI( xDist ) <= 6 && amaniaAbsI( yDist ) <= 6 )
        {
            if( amaniaAbsI( zDist ) <= 4 )
            {
                if( amaniaSpk[ t ].spriteMode == 0 ) return 1;
                if( amaniaSpk[ t ].spriteMode != 2 )
                {
                    amaniaSoundSystem( 1 );
                    amaniaScores = amaniaScores + 125;
                    amaniaSpk[ t ].spriteMode = 2;
                    return 0;
                }
            }
        }
    }
    return 0;
}

void amaniaGobFruit( void )
{
    amaniaFruitTimeLeft = 0;
    amaniaSoundSystem( 0 );
    amaniaScores = amaniaScores + 250;
    if( amaniaFvRF == 1 )
    {
        amaniaSwitchModeGhost( 0, 1 );
        amaniaGhostTimer = amaniaGDuration;
    }
    else if( amaniaFvRF == 2 )
    {
        amaniaHighSpeed = 2;
        amaniaHighSpeedTimer = 500;
    }
}

void amaniaCheckGob( int a, int b )
{
    if( !a ) return;
    if( !b ) return;
    if( amaniaFruitTimeLeft ) amaniaGobFruit();
}

void amaniaRemoveDot( void )
{
    amaniaInitialize2Bits( amaniaCamCamScanX, amaniaCamCamScanY );
    amaniaLvlDotCollected = amaniaLvlDotCollected + 1;
}

void amaniaONEUP( void )
{
    if( amaniaOneUp != amaniaNumAM10000 )
    {
        amaniaOneUp = amaniaNumAM10000;
        amaniaLive = amaniaLive + 1;
    }
}

void amaniaLiberateFruit( void )
{
    amaniaFruitTimeLeft = FRUITTIMELEFT;
    if( amaniaFvFlip == 0 ) { amaniaFvRF = 0; amaniaFvFlip = 1; }
    else
    {
        if( amaniaRandVar() ) amaniaFvRF = 2; else amaniaFvRF = 1;
        amaniaFvFlip = 0;
    }
}

void amaniaUpdateFruitDelivery( void )
{
    if( ( amaniaLvlDotCollected - amaniaScoresFruit ) > 35 )
    {
        amaniaScoresFruit = amaniaScoresFruit + 35;
        amaniaLiberateFruit();
    }
}

int amaniaAnimateDeadMania( void )
{
    if( amaniaAnimLatch1 == 0 ) return amaniaRCupAnim( 0 );
    if( amaniaSeqExitCounter == 0 && amaniaAudio ) Sound( ( 20 - amaniaAnimFrameDraw ) * 10, 30 );
    return amaniaAnimFrameDraw + 12;
}

void amaniaRCupDeadAnim( void )
{
    if( amaniaAnimNumPass < 25 )
    {
        if( amaniaAnimLatch1 < 4 ) amaniaAnimLatch1 = amaniaAnimLatch1 + 1; else amaniaAnimLatch1 = 1;
        amaniaAnimFrameDraw = amaniaAnimLatch1 - 1;
        amaniaAnimNumPass = amaniaAnimNumPass + 1;
        return;
    }
    if( amaniaAnimLatch1 < 13 ) amaniaAnimLatch1 = amaniaAnimLatch1 + 1;
    amaniaAnimFrameDraw = amaniaAnimLatch1 - 1;
    if( amaniaAnimLatch1 == 13 ) amaniaProgramExitMode( 1, 0 );
}

// -----------------------------------------------------------------------------
// Score / HUD panel
// -----------------------------------------------------------------------------

void amaniaCalculateNum( void )
{
    amaniaNumAM10000 = amaniaScores / 10000;
    amaniaNumAM1000 = ( amaniaScores - ( amaniaNumAM10000 * 10000 ) ) / 1000;
    amaniaNumAM100 = ( amaniaScores - ( amaniaNumAM1000 * 1000 ) - ( amaniaNumAM10000 * 10000 ) ) / 100;
    amaniaNumAM10 = ( amaniaScores - ( amaniaNumAM100 * 100 ) - ( amaniaNumAM1000 * 1000 ) - ( amaniaNumAM10000 * 10000 ) ) / 10;
    amaniaNumAM1 = amaniaScores - ( amaniaNumAM10 * 10 ) - ( amaniaNumAM100 * 100 ) - ( amaniaNumAM1000 * 1000 ) - ( amaniaNumAM10000 * 10000 );
}

void amaniaScorePannel( int x_, int y_ )
{
    amaniaCalculateNum();
    amaniaDrawSelfMasked( 22 + x_, y_, amaniapolice, amaniaNumAM1 );
    amaniaDrawSelfMasked( 18 + x_, y_, amaniapolice, amaniaNumAM10 );
    amaniaDrawSelfMasked( 14 + x_, y_, amaniapolice, amaniaNumAM100 );
    amaniaDrawSelfMasked( 10 + x_, y_, amaniapolice, amaniaNumAM1000 );
    amaniaDrawSelfMasked( 6 + x_, y_, amaniapolice, amaniaNumAM10000 );
}

void amaniaPannel( void )
{
    amaniaDrawOverwrite( 102, 54, amaniaDigital, 0 );
    amaniaDrawOverwrite( 0, 54, amaniaDlive, 0 );
    amaniaDrawSelfMasked( 10, 57, amaniapolice, amaniaLive );
    amaniaScorePannel( 100, 57 );
    amaniaUpdateFruitDelivery();
}

// -----------------------------------------------------------------------------
// Fade sequence (level-clear / life-lost transition). Upstream's own
// ProgramExitMode() blocks for a real My_delay_ms(400) before the fade
// even begins animating - dropped here (same "effort/fidelity tradeoff
// for a purely decorative pause" as collision()'s own dropped delay
// above), the fade itself (7 real frames, Fade_SCR/FrameRedundancy) is
// ported at full fidelity.
// -----------------------------------------------------------------------------

void amaniaProgramExitMode( int exitMode, int fadeInOut )
{
    if( amaniaSeqActivateSequence == 0 )
    {
        amaniaSeqActivateSequence = 1;
        amaniaSeqExitType = exitMode;
        if( fadeInOut ) amaniaSeqExitCounter = 7; else amaniaSeqExitCounter = 0;
        amaniaSeqFadeInOut = fadeInOut;
    }
}

int amaniaExitTime( void )
{
    if( amaniaSeqExitTrigger == 1 ) return amaniaSeqExitType;
    return 0;
}

void amaniaFadeScr( void )
{
    if( amaniaSeqActivateSequence )
    {
        // fadescreen is a 1-pixel-wide, 1-page-tall sprite (8 frames, each
        // frame's own real byte is table[2+frame] directly - no column-
        // dependent sprite math applies at all, since width=1) - calling
        // the general per-column amaniaDrawErase() for all 8x128=1024
        // (page,col) cells would cost 1024 real sprite-blit calls/frame
        // for a mask that's actually identical across the whole screen.
        // AND-NOT the same known mask directly into every buffer cell
        // instead, matching this project's own established "self-gated
        // call still costs a full call every time it's invoked" lesson -
        // applied proactively here, not after a CPU report.
        int mask = amaniafadescreen[ 2 + amaniaSeqExitCounter ];
        int eraseMask = 0xFF - mask;
        int idx;
        for( idx = 0; idx < 1024; idx++ )
          amaniaFrameBuffer[ idx ] = amaniaFrameBuffer[ idx ] & eraseMask;
    }
}

void amaniaFrameRedundancy( void )
{
    if( amaniaSeqActivateSequence )
    {
        if( amaniaSeqFadeInOut == 0 )
        {
            if( amaniaSeqExitCounter < 7 ) amaniaSeqExitCounter = amaniaSeqExitCounter + 1;
            else amaniaSeqExitTrigger = 1;
        }
        else
        {
            if( amaniaSeqExitCounter > 0 ) amaniaSeqExitCounter = amaniaSeqExitCounter - 1;
            else amaniaSeqExitTrigger = 1;
        }
        amaniaFadeScr();
    }
}

void amaniaRefreshTimers( void )
{
    if( amaniaSpeedJump < amaniaGJump ) amaniaSpeedJump = amaniaSpeedJump + 1;
    else amaniaSpeedJump = 0;

    if( amaniaSpeedGhost < amaniaGSpeed ) amaniaSpeedGhost = amaniaSpeedGhost + 1;
    else amaniaSpeedGhost = 0;

    if( amaniaTimer1 < 3 ) amaniaTimer1 = amaniaTimer1 + 1;
    else
    {
        amaniaTimer1 = 0;
        if( amaniaFrameAnim != 2 ) amaniaFrameAnim = amaniaFrameAnim + 1;
        else amaniaFrameAnim = 0;
    }

    if( amaniaGhostTimer > 1 ) amaniaGhostTimer = amaniaGhostTimer - 1;
    if( amaniaGhostTimer == 1 ) amaniaSwitchModeGhost( 1, 0 );

    if( amaniaDemiTimer < 5 ) amaniaDemiTimer = amaniaDemiTimer + 1; else amaniaDemiTimer = 0;

    if( amaniaFruitTimeLeft > 0 ) amaniaFruitTimeLeft = amaniaFruitTimeLeft - 1;

    if( amaniaHighSpeedTimer > 0 )
    {
        amaniaHighSpeedTimer = amaniaHighSpeedTimer - 1;
        if( amaniaHighSpeedTimer == 0 ) amaniaHighSpeed = 1;
    }
}

// Builds amaniaGhostCellHead/amaniaGhostCellNext (see their own comment)
// by inverting the forward scanX/scanY -> camScanX/camScanY mapping the
// main render loop below already uses: camScanY = scanY + DriftGridY has
// no wraparound, so scanY = ghost.gridY - DriftGridY directly; camScanX =
// wrap(scanX + DriftGridX) wraps by at most one level's width in either
// direction (the scan window is only ever 13 cells wide, far smaller
// than any real level), so of the 3 candidate un-wraps (rawX, rawX+W,
// rawX-W) at most one ever lands inside the actual scan window.
void amaniaBuildGhostCellIndex( void )
{
    int i;
    for( i = 0; i < 143; i++ ) amaniaGhostCellHead[ i ] = -1;

    int l;
    for( l = 1; l < amaniaTotalGhost; l++ )
    {
        int scanY = amaniaSpk[ l ].gridY - amaniaCamDriftGridY;
        if( scanY < -2 || scanY > 8 ) continue;

        int rawX = amaniaSpk[ l ].gridX - amaniaCamDriftGridX;
        int scanX = -1;
        if( rawX >= 0 && rawX < SCREEN_BLOCK_W ) scanX = rawX;
        else if( rawX + amaniaLvlW >= 0 && rawX + amaniaLvlW < SCREEN_BLOCK_W ) scanX = rawX + amaniaLvlW;
        else if( rawX - amaniaLvlW >= 0 && rawX - amaniaLvlW < SCREEN_BLOCK_W ) scanX = rawX - amaniaLvlW;
        if( scanX < 0 ) continue;

        int cellIdx = ( ( scanY + 2 ) * SCREEN_BLOCK_W ) + scanX;
        amaniaGhostCellNext[ l ] = amaniaGhostCellHead[ cellIdx ];
        amaniaGhostCellHead[ cellIdx ] = l;
    }
}

// -----------------------------------------------------------------------------
// Isometric camera-grid render - the whole per-frame scan/composite loop,
// a direct structural translation of loop_ARDUMANIA()'s own render
// section (see header comment for why this needed a real framebuffer
// rather than a per-page composite).
// -----------------------------------------------------------------------------

void amaniaRenderPlayingFrame( void )
{
    amaniaCamIsoShift = 0;
    amaniaBuildGhostCellIndex();

    // Hoisted out of the per-cell loop below (up to 143 cells/frame) -
    // amaniaLeveluse is fixed for the whole frame, so re-resolving these
    // two table pointers on every wall cell was pure repeated work,
    // matching this project's own established row/call-invariant-hoisting
    // lesson (see Nohzdyve's ndvOrBlit()/ndvXorBlit() history).
    int* blockTable = amaniaBlockTable( amaniaLeveluse );
    int* blockMaskTable = amaniaBlockMaskTable( amaniaLeveluse );

    int scanY, scanX;
    for( scanY = -2; scanY < SCREEN_BLOCK_H; scanY++ )
    {
        // A whole-row equivalent of the per-cell off-screen skip below:
        // tpy only depends on scanY (not scanX, since IsoScrollX/IsoShift
        // are the only X-varying terms), so a row that's fully off the
        // real 64-row screen wastes its *entire* inner scanX loop (up to
        // 13 cells' worth of Read2Bits/LoopScreen/draw calls) every single
        // frame, not just conditionally - unlike the per-cell skip, this
        // is guaranteed waste, not camera-position-dependent. Confirmed
        // directly: SCREEN_BLOCK_H's own scanY=8 always computes
        // tpy=64+IsoScrollY (IsoScrollY only ever 0-7), i.e. always >=64,
        // already past the last valid page (RecupeLineY(64)=8) - that row
        // can never contribute a single visible pixel, regardless of
        // camera position, and was still being fully scanned every frame.
        int rowTpy = ( scanY * YSTEP ) + amaniaCamIsoScrollY + AMANIA_FINETUNE_Y;
        if( rowTpy > 63 || rowTpy < -30 ) { amaniaCamIsoShift = amaniaCamIsoShift - 4; continue; }

        for( scanX = 0; scanX < SCREEN_BLOCK_W; scanX++ )
        {
            // Computed and checked *before* LoopScreen()/Read2Bits()/
            // OutCheck() even run - tpx only depends on scanX/scanY/
            // IsoShift/IsoScrollX, not on the cell's own wall/dot value,
            // so an off-screen column can skip the grid-lookup work too,
            // not just the draw calls (see the fuller explanation this
            // comment used to carry, now below with the rest of the
            // reasoning intact). SCREEN_BLOCK_W (13 cols x XSTEP 14 =
            // 182px) deliberately overscans past the real 128px screen
            // width for smooth camera scrolling; exactly which columns
            // are off-screen shifts per row (IsoShift skews each row
            // diagonally). Safe to skip entirely: only the player's own
            // grid cell carries real gameplay side effects (dot pickup/
            // scoring), and Center_Screen() always keeps the camera
            // centered on the player, so that cell is never among the
            // ones this removes. 24 covers the widest sprite drawn here
            // (Block, 17px) plus the largest position offset (+6) with
            // margin.
            int posX = ( scanX * XSTEP ) + amaniaCamIsoShift + amaniaCamIsoScrollX;
            int tpx = posX + AMANIA_FINETUNE_X;
            if( tpx > 127 || tpx < -24 ) continue;

            amaniaCamCamScanX = scanX + amaniaCamDriftGridX;
            amaniaCamCamScanY = scanY + amaniaCamDriftGridY;
            amaniaLoopScreen( &amaniaCamCamScanX );
            int spkScan = amaniaRead2Bits( amaniaCamCamScanX, amaniaCamCamScanY );
            int posY = ( scanY * YSTEP ) + amaniaCamIsoScrollY;
            int tpy = posY + AMANIA_FINETUNE_Y;
            amaniaOutCheck( &spkScan, amaniaCamCamScanY );

            if( amaniaLvlInvertBlock )
            {
                if( spkScan != 1 ) amaniaDrawSelfMasked( tpx, tpy, blockTable, 0 );
            }

            if( spkScan == 0 )
            {
                if( amaniaGateX == amaniaCamCamScanX && amaniaGateY == amaniaCamCamScanY )
                  amaniaDrawSelfMasked( tpx, tpy, amaniaGate, 0 );
            }
            else if( spkScan == 1 )
            {
                if( !amaniaLvlInvertBlock )
                {
                    amaniaDrawEraseThenMask( tpx, tpy, blockMaskTable, 0, blockTable, 0 );
                }
            }
            else if( spkScan == 2 )
            {
                amaniaDrawEraseThenMask( tpx + 6, tpy + 4, amaniadotmask, 0, amaniatidot, 0 );
            }
            else if( spkScan == 3 )
            {
                amaniaDrawEraseThenMask( tpx + 4, tpy + 2, amaniaBigDotMask, 0, amaniabigdot, 0 );
            }

            if( amaniaFruitTimeLeft != 0 && amaniaCamCamScanY == amaniaFruitY && amaniaCamCamScanX == amaniaFruitX )
            {
                amaniaDrawEraseThenMask( tpx, tpy, amaniafruitMask, amaniaFruitUse[ amaniaFvRF ], amaniafruit, amaniaFruitUse[ amaniaFvRF ] );
            }

            // O(1) lookup instead of scanning all amaniaTotalGhost ghosts
            // against every cell (see amaniaBuildGhostCellIndex()'s own
            // comment) - same bucket-linked-list technique already proven
            // in Tiny Mania for this exact shape of problem.
            int ghostCellIdx = ( ( scanY + 2 ) * SCREEN_BLOCK_W ) + scanX;
            int l = amaniaGhostCellHead[ ghostCellIdx ];
            while( l >= 0 )
            {
                int gmode = amaniaGhostTime( l );
                amaniaDrawEraseThenMask( tpx + amaniaSpk[ l ].decX + 2, tpy + amaniaSpk[ l ].decY - amaniaSpk[ l ].jmpPos, amaniaGhostMaskTable( gmode ), amaniaRCupAnimMask( l ), amaniaGhostModeTable( gmode ), amaniaRCupAnim( l ) );
                l = amaniaGhostCellNext[ l ];
            }

            if( amaniaCamCamScanY == amaniaSpk[ 0 ].gridY && amaniaCamCamScanX == amaniaSpk[ 0 ].gridX )
            {
                if( amaniaSpk[ 0 ].decX == 0 && amaniaSpk[ 0 ].decY == 0 )
                {
                    if( amaniaSpk[ 0 ].jmpPos < 3 )
                    {
                        if( spkScan == 0 )
                        {
                            amaniaCheckGob( amaniaSpk[ 0 ].gridX == amaniaFruitX, amaniaSpk[ 0 ].gridY == amaniaFruitY );
                        }
                        else if( spkScan == 2 )
                        {
                            amaniaCheckGob( amaniaSpk[ 0 ].gridX == amaniaFruitX, amaniaSpk[ 0 ].gridY == amaniaFruitY );
                            amaniaRemoveDot();
                            amaniaSoundSystem( 2 );
                            amaniaScores = amaniaScores + 15;
                        }
                        else if( spkScan == 3 )
                        {
                            amaniaScores = amaniaScores + 37;
                            amaniaSwitchModeGhost( 0, 1 );
                            amaniaGhostTimer = amaniaGDuration;
                            amaniaRemoveDot();
                            amaniaSoundSystem( 3 );
                        }
                    }
                }
                amaniaDrawEraseThenMask( tpx + amaniaSpk[ 0 ].decX + 2, ( tpy + amaniaSpk[ 0 ].decY ) - amaniaSpk[ 0 ].jmpPos, amaniaardumania_m, amaniaMaskUse(), amaniaardumania, amaniaAnimateDeadMania() );
            }
        }
        amaniaCamIsoShift = amaniaCamIsoShift - 4;
    }

    if( amaniaFruitTimeLeft != 0 )
    {
        amaniaDrawEraseThenMask( 114, 0, amaniafruitMask, amaniaFruitUse[ amaniaFvRF ], amaniafruit, amaniaFruitUse[ amaniaFvRF ] );
    }
}

// -----------------------------------------------------------------------------
// State machine. Matches upstream's own NEXTLEVEL/RESETPOS/MAINMENU goto
// labels as explicit function calls/state transitions instead - see header
// comment for the AnimLvlChange() simplification and the death-pause/
// exit-pause frame-counter translation of upstream's own synchronous
// My_delay_ms() calls.
// -----------------------------------------------------------------------------

void amaniaBeginLevel( int leveluse )
{
    amaniaShortStart = 1;
    amaniaTimerInit( &amaniaShortStartTimer, 100 );
    amaniaTimerActivate( &amaniaShortStartTimer );
    amaniaTimerInit( &amaniaLowCheckTimer, 100 );
    amaniaTimerActivate( &amaniaLowCheckTimer );

    amaniaLoadSelectedLevel( leveluse );
    amaniaScoresFruit = 0;
    amaniaCamIsoShift = 0;
    amaniaCamIsoScrollX = 0;
    amaniaCamIsoScrollY = 0;
    amaniaCamDriftGridX = 0;
    amaniaCamDriftGridY = 0;

    amaniaTransA = 127;
    amaniaTransD = 0;
    amaniaTransAnimFrame = 0;
    amaniaTransX = 0;
    amaniaTimerInit( &amaniaTransTimer, 3 );
    amaniaTimerActivate( &amaniaTransTimer );

    // md_playTone() is genuinely multi-voice (each Sound() call claims its
    // own free SPU channel rather than replacing a single shared one - see
    // this project's own CLAUDE.md), so unlike the burst-collapse bug
    // class documented extensively elsewhere in this file's own comments,
    // two back-to-back calls here can't step on each other - ported as a
    // direct, literal match of upstream's own Snd(100,255);Snd(60,255);
    // rather than routed through the shared single-note-per-tick sequencer.
    if( amaniaAudio )
    {
        Sound( 100, 255 );
        Sound( 60, 255 );
    }

    // The real walking melody (see amaniascore's own header comment) -
    // queued into the shared sequencer so it plays one note at a time,
    // each for its own real duration, across the transition's own several
    // real seconds, alongside (not blocked by) the two confirm tones above.
    amaniaSfxClear();
    int scoreT;
    for( scoreT = 1; scoreT < amaniascore[ 0 ]; scoreT = scoreT + 2 )
      amaniaSfxAdd( amaniascore[ scoreT ], amaniascore[ scoreT + 1 ] );

    amaniaState = AMANIA_STATE_LEVEL_TRANSITION;
}

void amaniaBeginResetPos( void )
{
    amaniaFrameAnim = 0;
    amaniaSpeedGhost = 0;
    amaniaSpeedJump = 0;
    amaniaTimer1 = 0;
    amaniaFruitTimeLeft = 0;
    amaniaSeqActivateSequence = 0;
    amaniaSeqExitTrigger = 0;
    amaniaSeqExitCounter = 0;
    amaniaSeqExitType = 0;
    amaniaSeqFadeInOut = 0;

    amaniaAnimFrameDraw = 0;
    amaniaAnimLatch1 = 0;
    amaniaAnimNumPass = 0;

    amaniaResetSpritesPos();

    amaniaFvFlip = 0;
    amaniaHighSpeed = 1;
    amaniaHighSpeedTimer = 0;

    amaniaDeathPauseFrames = 0;
    amaniaExitPauseFrames = 0;
    amaniaExitPendingType = 0;

    amaniaProgramExitMode( 4, 1 );
    amaniaCenterScreen();

    amaniaState = AMANIA_STATE_PLAYING;
}

// AnimLvlChange - the player walks off the left edge of the screen, then
// reverses and walks back across trailing the level's own ghost count
// behind them (D=0 -> walking off, D=1 -> walking back with ghosts),
// against a scrolling border and a row of decorative "Plate" platforms.
// Player/ghost draws already route through the fully-inlined
// amaniaDrawEraseThenMask() from the start (per direct request) - the
// same combined-pass optimization already proven for the main gameplay
// render loop, not a later retrofit. Everything else here (10 Plate
// draws, 16 border-bar erases, 16 scroll-overwrite draws) is cheap/low-
// count and not worth the same treatment.
void amaniaUpdateLevelTransition( void )
{
    amaniaClearBuffer();
    amaniaTransX = amaniaTransX + 1; if( amaniaTransX > 7 ) amaniaTransX = 0;

    int y_;
    for( y_ = 0; y_ < 10; y_++ )
      amaniaDrawSelfMasked( 5 + ( y_ * 12 ), 20, amaniaPlate, 0 );

    for( y_ = 0; y_ < 8; y_++ )
    {
        amaniaDrawErase( 5, y_ << 3, amaniaBlack, 0 );
        amaniaDrawErase( 122, y_ << 3, amaniaBlack, 0 );
    }

    int walkFrame = amaniaTransAnimFrame;
    if( amaniaTransD == 0 ) walkFrame = walkFrame + 3;
    amaniaDrawEraseThenMask( amaniaTransA, 23, amaniaardumania_m, 0, amaniaardumania, walkFrame );

    if( amaniaTransD == 1 )
    {
        int y2;
        for( y2 = 0; y2 < amaniaTotalGhost - 1; y2++ )
          amaniaDrawEraseThenMask( ( amaniaTransA - 30 ) - ( y2 * 20 ), 23, amaniaghostmask, 0, amaniaGhost, walkFrame );
    }

    for( y_ = 0; y_ < 63; y_ = y_ + 8 )
    {
        amaniaDrawOverwrite( 0, y_, amaniascroll, amaniaTransX );
        amaniaDrawOverwrite( 123, y_, amaniascroll, amaniaTransX );
    }

    if( amaniaTransD == 0 )
    {
        if( amaniaTransA > -16 ) amaniaTransA = amaniaTransA - 1;
        else amaniaTransD = 1;
    }
    else
    {
        if( amaniaTransA < 157 + ( ( amaniaTotalGhost - 1 ) * 20 ) ) amaniaTransA = amaniaTransA + 2;
        else amaniaBeginResetPos();
    }

    if( amaniaTimerTrigger( &amaniaTransTimer ) )
    {
        if( amaniaTransAnimFrame < 2 ) amaniaTransAnimFrame = amaniaTransAnimFrame + 1;
        else amaniaTransAnimFrame = 0;
    }

    amaniaAdvanceSfx();
    amaniaRenderFrame();
}

void amaniaUpdatePlaying( void )
{
    amaniaClearBuffer();
    amaniaRenderPlayingFrame();

    if( amaniaDeathPauseFrames > 0 )
    {
        amaniaDeathPauseFrames = amaniaDeathPauseFrames - 1;
    }
    else if( amaniaSeqActivateSequence == 0 )
    {
        if( amaniaCollision() )
        {
            if( amaniaAnimLatch1 == 0 )
            {
                amaniaCalculateNum();
                amaniaPannel();
                amaniaSoundSystem( 4 );
                amaniaAnimLatch1 = 1;
                amaniaDeathPauseFrames = 60;
            }
        }
        if( amaniaAnimLatch1 == 0 )
        {
            int r;
            for( r = 0; r < amaniaHighSpeed; r++ )
            {
                amaniaCheckPadFunction();
                amaniaRefreshMovingArdu();
            }
            amaniaGhostDirectionChoser();
            amaniaRefreshMovingGhost();
            amaniaGhostJumpCalculate();
        }
        else
        {
            amaniaRCupDeadAnim();
        }
    }

    amaniaCenterScreen();
    amaniaRefreshTimers();
    if( amaniaTimerTrigger( &amaniaLowCheckTimer ) )
    {
        amaniaONEUP();
        amaniaCalculateNum();
    }
    if( amaniaShortStart )
    {
        amaniaDrawEraseThenMask( 43, 36, amaniaplayer1readyMask, 0, amaniaplayer1ready, 0 );
        if( amaniaTimerTrigger( &amaniaShortStartTimer ) ) amaniaShortStart = 0;
    }
    amaniaPannel();
    amaniaFrameRedundancy();

    if( amaniaLvlDotCollected == amaniaLvlTotalDot ) amaniaProgramExitMode( 2, 0 );

    amaniaAdvanceSfx();

    if( amaniaExitPauseFrames > 0 )
    {
        amaniaExitPauseFrames = amaniaExitPauseFrames - 1;
        if( amaniaExitPauseFrames == 0 )
        {
            if( amaniaExitPendingType == 1 )
            {
                if( amaniaLive > 0 ) { amaniaLive = amaniaLive - 1; amaniaBeginResetPos(); }
                else { amaniaCheckNewHighScore(); amaniaState = AMANIA_STATE_MENU; }
            }
            else if( amaniaExitPendingType == 2 )
            {
                if( amaniaLeveluse < TOTAL_LEVEL - 1 )
                {
                    amaniaLeveluse = amaniaLeveluse + 1;
                    amaniaBeginLevel( amaniaLeveluse );
                }
                else
                {
                    amaniaCheckNewHighScore();
                    amaniaState = AMANIA_STATE_MENU;
                }
            }
        }
    }
    else
    {
        int et = amaniaExitTime();
        if( et == 1 || et == 2 )
        {
            amaniaExitPendingType = et;
            amaniaExitPauseFrames = 24;
            amaniaSeqActivateSequence = 0;
        }
        else if( et == 4 )
        {
            amaniaSeqActivateSequence = 0;
            amaniaSeqExitTrigger = 0;
            amaniaSeqExitCounter = 0;
            amaniaSeqExitType = 0;
            amaniaSeqFadeInOut = 0;
        }
    }

    amaniaRenderFrame();
}

// -----------------------------------------------------------------------------
// Splash (simplified - see header comment), Menu (full, real avatar-select
// + start/scores/audio cursor), ScoreMenu (full, 3-slot leaderboard)
// -----------------------------------------------------------------------------

void amaniaUpdateSplash( void )
{
    int i;
    for( i = 0; i < 1024; i++ ) amaniaFrameBuffer[ i ] = 0xFF;
    amaniaDrawErase( 41, 9, amaniaBOOTINTRO, 0 );
    if( amaniaFireEdge ) amaniaState = AMANIA_STATE_MENU;
    amaniaRenderFrame();
}

void amaniaDrawBar( int scr )
{
    amaniaDrawSelfMasked( 8, amaniawave[ scr ] + 2, amaniabar, 0 );
    amaniaDrawErase( 8, amaniawave[ scr ] + 2, amaniabar, 1 );
}

void amaniaDrawArduManiaLogo( int scr )
{
    amaniaDrawSelfMasked( 26 + ( amaniawave[ scr ] >> 2 ), 6, amaniaArduMania, 0 );
    amaniaDrawErase( 26 + ( amaniawave[ scr ] >> 2 ), 6, amaniaArduMania, 1 );
}

void amaniaUpdateMenu( void )
{
    // A direct copy of the cache does the "clear the buffer" job and the
    // "put the static background back" job in one 1024-iteration pass
    // instead of two (amaniaClearBuffer()'s own 1024-iteration zero-fill
    // followed by a separate 1024-iteration OR-merge) - the exact same
    // "don't touch all 1024 cells twice when once suffices" lesson this
    // project applies throughout, just at the whole-buffer level here.
    amaniaEnsureMainCache();
    int mi;
    for( mi = 0; mi < 1024; mi++ ) amaniaFrameBuffer[ mi ] = amaniaMainCache[ mi ];

    amaniaMenuX = amaniaMenuX + 1; if( amaniaMenuX > 7 ) amaniaMenuX = 0;

    int y_;
    for( y_ = 0; y_ < 63; y_ = y_ + 8 )
    {
        amaniaDrawSelfMasked( 0, y_, amaniascroll, amaniaMenuX );
        amaniaDrawSelfMasked( 123, y_, amaniascroll, amaniaMenuX );
    }

    int order0, order1, order2;
    if( amaniaMenuScr < 14 && amaniaMenuScr2 < 14 ) { order0 = 1; order1 = 2; order2 = 0; }
    else if( amaniaMenuScr < 14 ) { order0 = 1; order1 = 0; order2 = 2; }
    else if( amaniaMenuScr2 < 14 ) { order0 = 2; order1 = 0; order2 = 1; }
    else { order0 = 0; order1 = 1; order2 = 2; }

    int oi, order;
    for( oi = 0; oi < 3; oi++ )
    {
        if( oi == 0 ) order = order0; else if( oi == 1 ) order = order1; else order = order2;
        if( order == 0 ) amaniaDrawArduManiaLogo( amaniaMenuScr );
        else if( order == 1 ) amaniaDrawBar( amaniaMenuScr );
        else amaniaDrawBar( amaniaMenuScr2 );
    }

    amaniaMenuScr = amaniaMenuScr + 1; if( amaniaMenuScr > 28 ) amaniaMenuScr = 0;
    amaniaMenuScr2 = amaniaMenuScr2 + 1; if( amaniaMenuScr2 > 28 ) amaniaMenuScr2 = 0;
    amaniaRandVar();

    if( amaniaMenuPos == amaniaMenuCursorPos )
    {
        if( isDownPressed() )
        {
            if( amaniaMenuCursorPos < 49 ) { amaniaSoundSystem( 5 ); amaniaMenuCursorPos = amaniaMenuCursorPos + 9; }
        }
        if( isUpPressed() )
        {
            if( amaniaMenuCursorPos > 31 ) { amaniaSoundSystem( 5 ); amaniaMenuCursorPos = amaniaMenuCursorPos - 9; }
        }
    }
    else
    {
        if( amaniaMenuPos < amaniaMenuCursorPos ) amaniaMenuPos = amaniaMenuPos + 1;
        if( amaniaMenuPos > amaniaMenuCursorPos ) amaniaMenuPos = amaniaMenuPos - 1;
    }

    amaniaDrawSelfMasked( 26, amaniaMenuPos, amaniaAvatarTable( amaniaAvatar ), 0 );
    if( amaniaAudio ) amaniaDrawSelfMasked( 77, 51, amaniaaudio, 0 );

    if( amaniaFireEdge )
    {
        amaniaSoundSystem( 5 );
        if( amaniaMenuPos == 31 )
        {
            amaniaScores = 0;
            amaniaLive = 4;
            amaniaOneUp = 0;
            amaniaBeginLevel( 0 );
        }
        else if( amaniaMenuPos == 40 )
        {
            amaniaState = AMANIA_STATE_SCORE_MENU;
        }
        else if( amaniaMenuPos == 49 )
        {
            amaniaSoundSystem( 5 );
            if( amaniaAudio ) amaniaAudio = 0; else amaniaAudio = 1;
        }
    }

    // Upstream gates this with a real one-shot debounce (Menu()'s own local
    // "OneClick2": consumed on the first press, only re-armed once BOTH
    // directions read released) - a plain level read here means the
    // avatar advances once per real tick for as long as the button stays
    // held, which at this screen's own 30fps throttle is easily more than
    // one tick per physical tap, silently skipping over the middle choice
    // (ghost) straight to the last one (cherry/fruit) - exactly the bug a
    // direct user report caught. Reproduced with a persistent
    // amaniaAvatarOneClick flag matching upstream's own arm/disarm shape.
    if( isRightPressed() && amaniaAvatarOneClick )
    {
        amaniaAvatarOneClick = 0;
        if( amaniaAvatar < 2 ) { amaniaSoundSystem( 5 ); amaniaAvatar = amaniaAvatar + 1; }
    }
    else if( isLeftPressed() && amaniaAvatarOneClick )
    {
        amaniaAvatarOneClick = 0;
        if( amaniaAvatar > 0 ) { amaniaSoundSystem( 5 ); amaniaAvatar = amaniaAvatar - 1; }
    }
    if( !isLeftPressed() && !isRightPressed() ) amaniaAvatarOneClick = 1;

    amaniaAdvanceSfx();
    amaniaRenderFrame();
}

void amaniaUpdateScoreMenu( void )
{
    amaniaClearBuffer();
    amaniaScoreMenuX = amaniaScoreMenuX - 1; if( amaniaScoreMenuX < 0 ) amaniaScoreMenuX = 7;

    int y_;
    for( y_ = 0; y_ < 63; y_ = y_ + 8 )
    {
        amaniaDrawSelfMasked( 0, y_, amaniascroll, amaniaScoreMenuX );
        amaniaDrawSelfMasked( 123, y_, amaniascroll, amaniaScoreMenuX );
    }
    amaniaDrawSelfMasked( 26, 4, amaniaHighScore, 0 );

    amaniaScores = amaniaHSScore[ 0 ];
    amaniaScorePannel( 49, 26 );
    amaniaScores = amaniaHSScore[ 1 ];
    amaniaScorePannel( 49, 40 );
    amaniaScores = amaniaHSScore[ 2 ];
    amaniaScorePannel( 49, 54 );

    if( amaniaHSScore[ 0 ] != 0 ) amaniaDrawSelfMasked( 36, 22, amaniaAvatarTable( amaniaHSAvatar[ 0 ] ), 0 );
    if( amaniaHSScore[ 1 ] != 0 ) amaniaDrawSelfMasked( 36, 36, amaniaAvatarTable( amaniaHSAvatar[ 1 ] ), 0 );
    if( amaniaHSScore[ 2 ] != 0 ) amaniaDrawSelfMasked( 36, 49, amaniaAvatarTable( amaniaHSAvatar[ 2 ] ), 0 );

    if( amaniaFireEdge )
    {
        amaniaSoundSystem( 5 );
        amaniaState = AMANIA_STATE_MENU;
    }

    amaniaAdvanceSfx();
    amaniaRenderFrame();
}

// -----------------------------------------------------------------------------
// Entry points
// -----------------------------------------------------------------------------

void gameArdumania_init( void )
{
    InitTinyJoypad();
    amaniaState = AMANIA_STATE_SPLASH;
    amaniaTickSkipCounter = 0;
    amaniaPlayTickAccum = 0;
    amaniaPrevFire = 0;
    amaniaFireEdge = 0;
    amaniaAvatar = 0;
    amaniaAvatarOneClick = 1;
    amaniaAudio = 1;
    amaniaRolling = 0;
    amaniaMenuCursorPos = 31;
    amaniaMenuPos = 31;
    amaniaMenuX = 0;
    amaniaMenuScr = 0;
    amaniaMenuScr2 = 11;
    amaniaScoreMenuX = 0;
    amaniaSfxLen = 0;
    amaniaSfxPos = 0;
    amaniaLoadHighScores();
    amaniaClearBuffer();
}

void gameArdumania_update( void )
{
    int menuThrottled = ( amaniaState == AMANIA_STATE_MENU ) || ( amaniaState == AMANIA_STATE_SCORE_MENU );

    if( menuThrottled )
    {
        amaniaPlayTickAccum = 0;
        amaniaTickSkipCounter = amaniaTickSkipCounter + 1;
        if( amaniaTickSkipCounter < AMANIA_TICK_DIVISOR ) return;
        amaniaTickSkipCounter = 0;
    }
    else
    {
        amaniaTickSkipCounter = 0;
        amaniaPlayTickAccum = amaniaPlayTickAccum + AMANIA_GAMEPLAY_FPS;
        if( amaniaPlayTickAccum < 60 ) return;
        amaniaPlayTickAccum = amaniaPlayTickAccum - 60;
    }

    int fireNow = isFirePressed();
    amaniaFireEdge = fireNow && !amaniaPrevFire;
    amaniaPrevFire = fireNow;

    if( amaniaState == AMANIA_STATE_SPLASH ) amaniaUpdateSplash();
    else if( amaniaState == AMANIA_STATE_MENU ) amaniaUpdateMenu();
    else if( amaniaState == AMANIA_STATE_SCORE_MENU ) amaniaUpdateScoreMenu();
    else if( amaniaState == AMANIA_STATE_LEVEL_TRANSITION ) amaniaUpdateLevelTransition();
    else amaniaUpdatePlaying();
}

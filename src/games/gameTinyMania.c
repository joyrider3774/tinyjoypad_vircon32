// =============================================================================
// Tiny Mania (Daniel C, 2026, GPLv3 - same `ELECTROLIB.h`/`FastTinyDriver.h`
// lineage as most other Daniel-C titles in this project, credited "DANIEL C"
// in the menu). The newest tinyjoypad.com release at the time this port was
// made (staged into `more games/Tiny Mania/` on 2026-08-05, first listed on
// the site itself as of 2026-08-04). A Pac-Man-style maze/ghost-chase game
// with one genuine mechanical addition over the already-shipped Tiny Pacman:
// a jump (pressing Fire triggers a fixed jump-height animation sequence,
// `Jump[]`/`JmpSeq`/`JmpPos`) that lets the player pass safely over a normal
// ghost mid-air - the signature mechanic of the real arcade game "Pac-Mania"
// this name references. Confirmed via direct reading to be a genuinely
// distinct codebase from Tiny Pacman (different maze/sprite data, different
// movement/collision model, no shared code beyond both using the same
// `ELECTROLIB.h`/`FastTinyDriver.h` driver family every Daniel-C game here
// already uses) - not a duplicate.
//
// Not `tinyJoypadShim`/`obonoCoreShim` lineage by name, but needed no new
// shim: `ELECTROLIB.h`'s own `TINYJOYPAD_LEFT/RIGHT/UP/DOWN` macros are the
// exact same A0/A3 500-750/750-950 analog thresholds every other Daniel-C
// game here already uses, and `BUTTON_DOWN`/`BUTTON_UP` are just the same
// shared Fire button's digital pin read (active-low) - `isFirePressed()`
// covers both (`BUTTON_DOWN` == pressed, `BUTTON_UP` == released).
//
// **Rendering reuses Tiny Arena's own half-resolution-buffer technique
// directly, not a new one**: this game's own `VBuffer[4][64]` is a 64x32
// monochrome buffer (4 hardware-page-equivalents tall), doubled 2x both
// ways by `Tiny_Flip()`'s own nibble-expansion trick to fill the real
// 128x64 display - structurally identical to Tiny Arena's own raycaster
// buffer (`arVBuffer`/`arSliceByte`/`arExpand` - same 16-entry expand
// table, confirmed byte-identical, reused here as `tmnExpand`, each game
// keeping its own self-contained copy per this project's standing
// practice). `tmnDrawSprite2Bit()` is a direct port of upstream's own
// `drawSprite2Bit()` - a real two-plane (white/black-mask) sprite blitter
// writing into the half-res buffer, with a `mono` flag selecting whether
// the black plane actually erases (silhouette-cutout compositing, used
// for all real gameplay sprites) or is treated as fully transparent (used
// for the attract screen's own overlapping decorative art, which
// shouldn't punch holes in whatever it's drawn over).
//
// **The bottom hardware row (physical page 7) is a genuine exception to
// the plain 2x-doubled render model**: upstream's own `Tiny_Flip()`
// doesn't just double the level graphics there while INGAME - it
// entirely replaces specific column ranges with a *stateful, sequential*
// score-digit/lives-icon reader (`Recup_Digital`/`Recup_Lives`, each
// advancing its own persistent column/digit cursor once per call, called
// twice per half-res column to produce two *independently-resolved* real
// columns rather than a naive horizontal doubling of one value - the
// mechanism that lets a half-res-buffer game still show full-resolution
// HUD text). Ported as `tmnComposeRow7()`, computing the entire 128-byte
// real row into a `tmnRow7Buffer[128]` cache once per frame by walking
// the exact same half-res-column-then-twice-per-column loop shape as
// upstream (not attempting to make it a stateless per-column query
// function, since the whole point of the original algorithm is that the
// cursor's value at a given real column depends on how many prior calls
// already advanced it) - the same "reproduce an intricate stateful
// upstream algorithm's own shape rather than re-derive a closed form"
// reasoning already used for Frogger's own row-buffer compositing. The
// fade-mask AND (`i2c_Mixers()`'s own `out_ & FD[Fade.Frame]`) is applied
// once, centrally, at the point every byte (level pixel or HUD byte
// alike) is actually handed to `md_drawColumn()`, rather than duplicated
// at each of upstream's three separate emission call sites - upstream
// itself applies it uniformly through one shared helper, so this just
// keeps that one central point.
//
// **Every blocking `_delay_ms()` call, and one genuine blocking
// `while(1)` busy-wait, converted to an explicit frame-stepped state -**
// this file needed by far the largest number of these conversions of any
// port in this project to date, since nearly every non-trivial game event
// (starting a game, restarting a level, advancing a level, clearing every
// dot, eating a ghost, dying) is followed by a real, multi-hundred-
// millisecond blocking pause upstream:
// - The attract screen's own confirm gesture (`if (BUTTON_DOWN &&
//   Fade.Active==0) { Sound(...); while(1) { if (BUTTON_UP) { Sound(...);
//   break; } } Fade2Black(1); }`) - a genuine busy-wait for the button to
//   be *released* before the fade-out (and eventually a fresh game) can
//   begin - converted to a plain press/release edge check
//   (`tmnAttractFireHeld`) spread across real frames instead, the same
//   effective one-shot-per-press-cycle result without ever blocking.
//   Explicitly re-armed against whatever the Fire button's *current*
//   physical state is (not assumed released) every time the attract
//   screen is (re)entered - matching this project's own established
//   `md_armInputFireGate()` reasoning for exactly this class of
//   carried-over-press bug, just applied here as a plain local flag
//   rather than the shared menu-handoff gate.
// - The fade-out/fade-in sequence (`Fade.Active`/`Fade.Frame`, 0-8) was
//   *already* a real frame-stepped effect upstream (no restructuring
//   needed for the dissolve itself) - but the moment the screen goes
//   fully black, upstream's own `MENU_FADESELECT()` dispatch calls
//   straight into `InitNewGame()`/`RESTART_LEVEL()`/`NEXT_LEVEL()` -
//   each of which blocks for a further 500/800/1000ms respectively
//   *while still fully black*, before the fade-in even starts.
//   Reproduced with a third `tmnFadeActive` value (3, "holding fully
//   black") sitting between the fade-out and fade-in phases, counting
//   down a `tmnPostBlackWaitFrames` value set by whichever of the three
//   (now instantaneous, non-blocking) setup functions just ran, before
//   `tmnFadeActive` advances to the real fade-in value (2) - every other
//   place in the file that already checked `Fade.Active==0` to mean "not
//   currently transitioning" needed no change, since this new
//   intermediate value is just as non-zero/paused as the fade-out and
//   fade-in values it sits between.
//   A genuinely subtle real upstream mechanic lives inside this same
//   transition: `RESTART_LEVEL()`'s own `if (GP.PacLives > 0)
//   GP.PacLives--; else Fade2Black(0);` re-triggers *another* fade-out-
//   then-hold-then-fade-in cycle from within the *middle* of the one
//   already in progress once the player's very last life is spent -
//   ending in `IngameTrigger=0` (attract) instead of a restarted level.
//   The naive port of this (setting `tmnFadeActive=3` unconditionally
//   right after calling the trigger's own setup function) would silently
//   clobber that nested re-trigger's own `tmnFadeActive=1` the same way
//   this project's own documented "shared 'wait complete' dispatcher
//   clobbering its own callee's new state" bug once broke Tiny Invaders'
//   level-start sequence - avoided here the same way, by moving the
//   `tmnFadeActive=3` assignment inside `tmnMenuFadeSelect()` itself,
//   only reached when a nested re-trigger hasn't already changed it.
// - Eating a frightened ghost, and dying to a normal one, both start
//   from the *same* upstream sound cue (`SoundSystem(1)`, a real ~31-
//   step descending-then-ascending sweep with its own genuine per-step
//   real timing, not a burst) - downsampled to a 16-note frame-stepped
//   sequencer, `tmnGubFreq_Dur[16]` (see its own declaration for the full
//   story on why - this project's frame-stepped sequencer can't
//   represent upstream's own sub-millisecond note durations, so a literal
//   62-note port would have taken over a full second for a sound whose
//   real duration is ~76ms) - eating continues into a further genuine
//   fixed ~250ms pause (`TMN_PLAY_EAT_WAIT`) before the score is
//   actually added and play resumes; dying continues into a ~1000ms
//   pause, `PLAY_MUSIC(DeadSong)` (a real 27-note tune, its own frame-
//   stepped sequencer reading the extracted `tmnDeadSongPairs[54]` table
//   with the leading count byte already stripped), then a further
//   ~1000ms pause before the fade-to-black-then-restart sequence above
//   finally begins. Both of these genuinely freeze gameplay (matching
//   upstream's own real single-threaded blocking behavior) via the
//   `tmnPlayState` sub-state machine below.
// - Clearing every dot in the level (`CheckCollectible()`'s own
//   `_delay_ms(500); SoundSystem(5); _delay_ms(500); Fade2Black(3);`) -
//   `SoundSystem(5)`'s own `for(q=1;q<30;q++){Sound(20,35);Sound(200,
//   45);}` is a genuine 58-note alternating fanfare (each `Sound()` call
//   itself real-time-consuming via its own bit-bang delay loop, not an
//   instant burst - unlike several other oversized sweeps found
//   elsewhere in this project, this one's real total duration, ~1.3s,
//   is already reasonable to reproduce in full rather than needing to be
//   downsampled) - reproduced as `TMN_PLAY_LEVELCLEAR_WAIT1` (~500ms) ->
//   the fanfare sequencer -> `TMN_PLAY_LEVELCLEAR_WAIT2` (~500ms) ->
//   the same generic fade-to-black-then-advance-level sequence.
// By contrast, the *small*, frequent per-dot/per-fruit sound cues
// (`SoundSystem(2)`/`SoundSystem(3)`, and `SoundSystem(1)` when it's a
// plain fruit pickup rather than a ghost/death event) have no explicit
// `_delay_ms()` anywhere around them upstream - only the three sequences
// above ever get a deliberate extra pause, a real signal of the original
// author's own intent (freeze the game for a deliberate dramatic beat,
// vs. just let a quick cue play out in the background). Matching that
// distinction - and this project's own established precedent from Tiny
// Pacman's own dot/ghost-eaten sound fix, a small standalone SFX player
// that "advances once per logic tick alongside normal play" rather than
// freezing anything - every dot/fruit pickup cue here runs through a
// second, independent, non-blocking sequencer (`tmnBgNotes`/etc) that
// never pauses movement, while only the three genuinely-paced sequences
// above use the state-machine-driving one.
// A `DrawLevel()` call happens every single real engine frame throughout
// every one of the pauses above (matching upstream's own real control
// flow, where `DrawLevel()` is called once per `loop()` iteration
// regardless of `Fade.Active`/collision state, so a paused scene simply
// keeps re-rendering identical, unchanging sprite positions) - this also
// sidesteps the VRAM-persistence class of bug this project has hit
// repeatedly elsewhere, since Vircon32 has no real display memory to
// keep showing a frame nothing redraws.
//
// **A deliberate, low-risk simplification**: upstream's own ghost-
// collision scan (`Pac_collision()`) can, in a single synchronous call,
// eat *several* frightened ghosts in a row (each with its own inline
// 250ms delay) before either finishing the scan or hitting a lethal
// (non-frightened) ghost later in the same index-ordered pass. Since
// this port can only ever process one such collision event per real
// frame (freezing movement for its own pause first), a frame where two
// ghosts are simultaneously within range - one frightened, one not -
// would eat the frightened one first and only detect the lethal one on
// the very next resumed frame, rather than within the exact same
// original scan. Movement is fully frozen during the pause in both
// cases, so the collision geometry is unchanged by the time the scan
// resumes - the same eventual outcome (both events happen) just spread
// across a couple of extra real frames in this specific, narrow,
// simultaneous-multi-ghost-proximity case, rather than a gameplay or
// scoring difference.
//
// Data tables extracted and byte-diff-verified via a small Python script
// against the real upstream source before ever being pasted in, matching
// this project's own established "byte-diff transcribed tables"
// discipline. Every ternary operator and `switch` statement in the file
// (there are many of both) was rewritten as plain `if`/`else` chains,
// matching this project's own standing dialect caution. `GP`/`Ctrl`/
// `Bonus`/`Fade` (each only ever a single global instance upstream, never
// passed by pointer) were flattened to individual global variables
// instead of porting their own upstream `struct` types verbatim - only
// `SPRITES` (used both as `MainSprite` and as the `Ghosts[NUM_GHOST]`
// array, and passed by pointer throughout) genuinely needed a real
// struct type, ported as `TmnSprite`. The dot-presence grid
// (`LvLMem`/`WriteBit`/`ReadBit`) was a bit-packed byte array upstream (a
// real RAM-saving trick with no benefit on Vircon32) - ported instead as
// a plain `bool[15][21]` grid with the identical read/write interface,
// avoiding that shift-arithmetic entirely rather than working around it,
// matching this project's own established treatment of every other
// upstream bit-packed structure with no Vircon32-side justification to
// keep (Falling Blocks' own `blockArray`/`ghostArray`, etc). `fast_abs()`
// (a branchless `int8_t`-arithmetic-shift-based helper) is declared but
// never actually called anywhere in the file - confirmed dead by grep,
// dropped rather than ported, since its own AVR-arithmetic-right-shift
// reliance would have been the exact class of bug this project has
// repeatedly had to fix elsewhere had it ever been reachable; the real
// (and already-safe) `abs()` used at the one real call site is Vircon32's
// own built-in, already confirmed working elsewhere in this project
// (t2048, Tiny RoG). `RD_4()`'s own "randomness" is a fixed 16-entry
// cycling table (`Rnd_Array`), not a real `rand()`/`random()` call
// anywhere in this file at all - ported verbatim, no `arand()` conversion
// needed. `Blocks[2]`/`GhostPic[3]` (upstream C arrays-of-pointers-to-
// PROGMEM-tables) were replaced with small integer-mode resolver
// functions (`tmnResolveBlocks()`/`tmnResolveGhostPic()`) instead,
// matching this project's own established Tiny Minez/Tiny Dungeon
// precedent - a global array of pointers as its own static initializer
// is unproven/untested under this dialect, while a resolver function
// returning the real runtime pointer is already confirmed safe.
// A genuine, faithfully-preserved upstream quirk: `UpdateMove(SPK_)`
// checks `NeutralPosition(&MainSprite)` - always the *player*, never the
// sprite actually passed in - so every ghost's own movement update also
// re-checks (and, if true, redundantly re-runs) the player's own dot-
// collection logic each tick. Harmless in practice (a dot/bonus already
// collected on an earlier call that same tick simply no-ops on every
// later one), so ported exactly as found rather than "fixed".
// =============================================================================

// -----------------------------------------------------------------------------
//   Data (byte-diff-verified via a small Python script against the
//   upstream source before ever being pasted in)
// -----------------------------------------------------------------------------

int[21][15] tmnLevel =
{
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,0,0,1,0,1,0,1,0,0,1,0,0},
    {0,0,2,0,0,1,0,1,0,1,0,0,2,0,0},
    {0,0,1,0,0,1,1,1,1,1,0,0,1,0,0},
    {0,0,1,1,1,1,0,4,0,1,1,1,1,0,0},
    {0,0,0,1,0,1,0,3,0,1,0,1,0,0,0},
    {0,0,0,1,0,1,0,0,0,1,0,1,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,1,0,0,1,0,1,0,1,0,0,1,0,0},
    {0,0,1,0,1,1,0,1,0,1,1,0,1,0,0},
    {0,0,1,0,1,0,0,1,0,0,1,0,1,0,0},
    {0,0,1,1,1,1,1,3,1,1,1,1,1,0,0},
    {0,0,1,0,0,1,0,1,0,1,0,0,1,0,0},
    {0,0,1,0,0,1,0,1,0,1,0,0,1,0,0},
    {0,0,2,0,0,1,0,1,0,1,0,0,2,0,0},
    {0,0,1,0,0,1,1,1,1,1,0,0,1,0,0},
    {0,0,1,1,1,1,0,0,0,1,1,1,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

// Vertical2/Horizon2/Pix (the attract screen's border-line/marching-dash
// decorations) no longer need their own data tables at all - `tmnIntro()`
// now writes their known-constant/computed pixel patterns directly into
// `tmnVBuffer`, since going through a full `tmnDrawSprite2Bit()` call for
// each of 286 individual 1-pixel-wide sprite blits was a real, measured
// CPU hotspot (see that function's own comment).
int[21] tmnStart2 = { 19, 8, 23,21,29,0,1,31,1,0,31,5,31,0,31,5,27,0,1,31,1 };
int[45] tmnTinyMania2 =
{
    43, 8,
    3,3,255,3,3,244,0,248,12,4,252,0,15,26,220,220,30,15,0,255,15,60,112,60,15,255,0,244,148,220,252,0,248,12,4,252,0,244,0,244,148,220,252,
};
int[8] tmnAnimFruits = { 0, 1, 2, 3, 3, 2, 1, 0 };
int[86] tmnFruits =
{
    7, 8,
    0,32,48,8,44,50,0,32,80,72,116,82,77,50,0,32,112,124,84,32,0,32,80,140,130,170,86,32,0,64,64,112,88,60,0,192,160,176,136,164,66,62,0,56,80,38,82,56,0,56,68,174,217,173,70,56,0,56,124,116,100,56,0,124,198,130,138,154,198,124,0,56,68,68,76,56,0,124,198,186,186,178,198,124,
};
int[20] tmnBlock1 = { 9, 7, 120,94,107,89,105,89,125,31,7, 0,32,20,38,22,38,2,0,0 };
int[20] tmnBlock2 = { 9, 7, 120,100,98,97,103,126,60,12,0, 0,24,28,30,24,1,66,114,28 };
int[20] tmnDot   = { 9, 7, 0,0,0,0,8,0,0,0,0, 0,0,0,8,20,8,0,0,0 };
int[20] tmnBigDot = { 9, 7, 0,0,0,8,28,8,0,0,0, 0,0,8,20,34,20,8,0,0 };
int[24] tmnJump = { 0,2,4,7,8,8,9,9,9,9,9,9,9,9,8,8,6,5,3,2,1,0,1,0 };

int[170] tmnPac =
{
    7, 8,
    0,28,62,46,42,12,0, 28,34,65,81,85,50,28,
    0,28,62,46,42,4,0, 28,34,65,81,85,42,4,
    0,28,62,38,2,0,0, 28,34,65,89,117,34,0,
    0,28,42,46,58,28,0, 28,34,85,81,69,34,28,
    0,12,42,46,42,28,0, 28,50,85,81,85,34,28,
    0,12,10,14,42,28,0, 28,50,117,113,85,34,28,
    0,12,42,46,62,28,0, 28,50,85,81,65,34,28,
    0,4,42,46,62,28,0, 4,42,85,81,65,34,28,
    0,0,2,38,62,28,0, 0,34,117,89,65,34,28,
    0,28,62,62,58,28,0, 28,34,65,65,69,34,28,
    0,28,62,62,58,12,0, 28,34,65,65,69,50,28,
    0,28,62,62,58,4,0, 28,34,65,65,69,58,28,
};

int[170] tmnGhost =
{
    7, 8,
    0,60,30,62,58,28,0, 60,66,33,65,69,34,124,
    0,28,62,30,58,60,0, 124,34,65,33,69,66,60,
    0,60,30,62,26,60,0, 60,66,33,65,37,66,124,
    0,60,26,62,58,28,0, 60,66,37,65,69,34,124,
    0,28,58,30,58,60,0, 124,34,69,33,69,66,60,
    0,60,26,62,26,60,0, 60,66,37,65,37,66,124,
    0,60,26,62,62,28,0, 60,66,37,65,65,34,124,
    0,28,58,30,62,60,0, 124,34,69,33,65,66,60,
    0,60,26,62,30,60,0, 60,66,37,65,33,66,124,
    0,60,30,62,62,28,0, 60,66,33,65,65,34,124,
    0,28,62,30,62,60,0, 124,34,65,33,65,66,60,
    0,60,30,62,30,60,0, 60,66,33,65,33,66,124,
};

int[170] tmnGhostsGobTime =
{
    7, 8,
    60,66,33,65,69,34,124, 0,60,30,62,58,28,0,
    124,34,65,33,69,66,60, 0,28,62,30,58,60,0,
    60,66,33,65,37,66,124, 0,60,30,62,26,60,0,
    60,66,37,65,69,34,124, 0,60,26,62,58,28,0,
    124,34,69,33,69,66,60, 0,28,58,30,58,60,0,
    60,66,37,65,37,66,124, 0,60,26,62,26,60,0,
    60,66,37,65,65,34,124, 0,60,26,62,62,28,0,
    124,34,69,33,65,66,60, 0,28,58,30,62,60,0,
    60,66,37,65,33,66,124, 0,60,26,62,30,60,0,
    60,66,33,65,65,34,124, 0,60,30,62,62,28,0,
    124,34,65,33,65,66,60, 0,28,62,30,62,60,0,
    60,66,33,65,33,66,124, 0,60,30,62,30,60,0,
};

int[170] tmnGhostsGobbed =
{
    7, 8,
    0,0,0,0,4,0,0, 0,0,0,4,10,4,0,
    0,0,0,0,4,0,0, 0,0,0,4,10,4,0,
    0,0,0,0,4,0,0, 0,0,0,4,10,4,0,
    0,0,4,0,4,0,0, 0,4,10,4,10,4,0,
    0,0,4,0,4,0,0, 0,4,10,4,10,4,0,
    0,0,4,0,4,0,0, 0,4,10,4,10,4,0,
    0,0,4,0,0,0,0, 0,4,10,4,0,0,0,
    0,0,4,0,0,0,0, 0,4,10,4,0,0,0,
    0,0,4,0,0,0,0, 0,4,10,4,0,0,0,
    0,0,4,0,4,0,0, 0,4,10,4,10,4,0,
    0,0,4,0,4,0,0, 0,4,10,4,10,4,0,
    0,0,4,0,4,0,0, 0,4,10,4,10,4,0,
};

int[40] tmnPolice =
{
    0,124,68,124,
    0,0,0,124,
    0,116,84,92,
    0,68,84,124,
    0,28,16,124,
    0,92,84,116,
    0,124,84,116,
    0,4,4,124,
    0,124,84,124,
    0,92,84,124,
};

int[9] tmnFD = { 0,16,24,56,60,124,126,254,255 };
int[6] tmnMiniPac = { 60,126,102,66,0,0 };

// DeadSong[] with its own leading upstream "count" byte (54) already
// stripped - PLAY_MUSIC(DeadSong) itself never reads that byte as a real
// note, only as its own loop bound, so this table is just the 27 real
// note pairs.
int[54] tmnDeadSongPairs =
{
    102,255,0,255,90,255,0,255,80,255,0,255,72,50,62,50,72,50,62,50,72,50,62,50,72,50,
    62,50,72,50,62,50,72,50,62,50,72,50,62,50,72,50,62,50,72,50,62,50,72,50,62,50,72,50,
};

int[16] tmnRndArray = { 0,2,1,3,3,2,1,0,3,2,0,1,1,3,2,2 };

// SliceByte's own nibble-expansion table - byte-identical to Tiny Arena's
// own `arExpand`, reused here under its own self-contained copy per this
// project's standing per-game-prefix practice.
int[16] tmnExpand =
{
    0,3,12,15,       // 0b00000000, 0b00000011, 0b00001100, 0b00001111
    48,51,60,63,     // 0b00110000, 0b00110011, 0b00111100, 0b00111111
    192,195,204,207, // 0b11000000, 0b11000011, 0b11001100, 0b11001111
    240,243,252,255  // 0b11110000, 0b11110011, 0b11111100, 0b11111111
};

// Frame-stepped sound tables (derived directly from upstream's own
// generating loops, not hand-transcribed - see this file's own header
// comment for each one's real source).
int[4] tmnPetitDotNotes = { 40,4,200,4 };
int[8] tmnBigDotNotes = { 10,12,250,12,100,12,10,12 };
// Downsampled from upstream's own full 62-note/31-iteration sweep
// (`for(Sn1=1;Sn1<125;Sn1+=4){Sound(Sn1,3);Sound(Sn1+125,2);}`) - kept
// every 8th iteration (1,33,65,97) rather than all 31. Needed because
// upstream's own real total duration for this sweep is only ~76ms (each
// individual `Sound()` call is well under 1ms - this project's own
// frame-stepped sequencer can't represent a note shorter than one real
// 60fps tick, ~16.67ms), so porting all 62 notes 1:1 would have stretched
// a ~76ms sound effect out to over a full second (62 notes x >=1 frame
// each) - confirmed via a direct user report that the ghost-eaten pause
// felt far longer than the same moment in real footage of the original
// game. Downsampling to 16 notes (~267ms at 1 frame/note) keeps the
// audible "descending sweep" character recognizable while landing much
// closer to the real duration than a full 1:1 port could, matching this
// project's own established fix for this exact bug shape (Tiny Missile/
// Pipe/Bert's own oversized real sweeps).
int[16] tmnGubFreq_Dur =
{
    1,3,126,2,33,3,158,2,65,3,190,2,97,3,222,2,
};
int[116] tmnFanfareNotes =
{
    20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,
    20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,
    20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,
    20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,20,35,200,45,
    20,35,200,45,
};

// -----------------------------------------------------------------------------
//   Constants (matches upstream's own #defines)
// -----------------------------------------------------------------------------

#define TMN_NBLVL 7

#define TMN_LEVELW 15
#define TMN_LEVELH 21
#define TMN_LEVELW_1 14
#define TMN_LEVELH_1 20

#define TMN_BNSPOSX 7
#define TMN_BNSPOSY 9

#define TMN_RESPAWNX 7
#define TMN_RESPAWNY 6

#define TMN_PACPOSX 7
#define TMN_PACPOSY 13

#define TMN_TOTAL_DOTS 112

#define TMN_NUM_GHOST 7
#define TMN_JUMPINGGHOST 4

#define TMN_DRIFTX 7
#define TMN_DRIFTY 4

#define TMN_UP    0
#define TMN_RIGHT 1
#define TMN_DOWN  2
#define TMN_LEFT  3

int tmnGetLevelCell( int x_, int y_ ) { return tmnLevel[ y_ ][ x_ ]; }

int tmnCheckGrid( int X_, int Y_ )
{
    if( X_ > TMN_LEVELW_1 ) return 0;
    if( Y_ > TMN_LEVELH_1 ) return 0;
    return tmnGetLevelCell( X_, Y_ );
}

int* tmnResolveBlocks( int mode )
{
    if( mode == 0 ) return tmnBlock1;
    return tmnBlock2;
}

int* tmnResolveGhostPic( int mode )
{
    if( mode == 0 ) return tmnGhost;
    if( mode == 1 ) return tmnGhostsGobTime;
    return tmnGhostsGobbed;
}

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

struct TmnSprite
{
    int Type;
    int Speed;
    int SkipMode;
    int active;
    int health;
    int DecalageX, DecalageY;
    int GridX, GridY;
    int Xdir;
    int Ydir;
};

TmnSprite tmnMainSprite;
TmnSprite[7] tmnGhosts;

int tmnCtrlLeft, tmnCtrlRight, tmnCtrlUp, tmnCtrlDown;

int tmnBonusDelivery;
int tmnBonusTimer;
int tmnBonusAnim;
int tmnBonusOrder;
int tmnBonusFruit;

int tmnTotalDotsCollected;
int tmnPacLives;
int tmnNbGhosts;
int tmnGobDotTimer;
int tmnLvl;
int tmnBLKs;
int tmnGhostSpeed;
int tmnPacSpeedTimer;

int tmnFadeActive;   // 0=off, 1=fading out, 2=fading in, 3=holding fully black
int tmnFadeFrame;    // 0-8
int tmnFadeTrigger;  // 0=intro, 1=new game, 2=restart level, 3=next level
int tmnPostBlackWaitFrames;

bool tmnIngame;
int[5] tmnDigits;

bool tmnBlink;
int tmnGobModeTimer;
int tmnJmpSeq;
int tmnJmpTrig;
int tmnJmpPos;

int[4][64] tmnVBuffer;
int[128] tmnRow7Buffer;

// Row-7 HUD (score digits/lives icons) dirty-flag cache - see
// tmnRebuildHudCache()/tmnComposeRow7() below. Only the digit/lives
// *portion* of row 7 is cacheable this way; the level-background portion
// sharing that same row genuinely changes every frame (camera scroll),
// so it's always recomputed fresh regardless.
bool tmnHudDirty;
int[128] tmnHudCache;
bool[128] tmnHudIsCol;

int tmnCamX, tmnCamY;
int tmnFRM;
int tmnDIR;

int tmnRDC;

bool[15][21] tmnDotGrid;

bool tmnAttractFireHeld;

// Play sub-states, only meaningful while tmnIngame && tmnFadeActive==0 -
// see this file's own header comment for the full state-by-state mapping
// back to upstream's own blocking delays.
#define TMN_PLAY_NORMAL             0
#define TMN_PLAY_EAT_GUB            1
#define TMN_PLAY_EAT_WAIT           2
#define TMN_PLAY_DEATH_GUB          3
#define TMN_PLAY_DEATH_WAIT1        4
#define TMN_PLAY_DEATH_SONG         5
#define TMN_PLAY_DEATH_WAIT2        6
#define TMN_PLAY_LEVELCLEAR_WAIT1   7
#define TMN_PLAY_LEVELCLEAR_FANFARE 8
#define TMN_PLAY_LEVELCLEAR_WAIT2   9

int tmnPlayState;
int tmnPlayWaitFrames;

// 30fps whole-tick throttle (see gameTinyMania_update()'s own comment) -
// every existing frame-counted wait constant above (15/30/48/60/etc) is
// deliberately left unrescaled, matching this project's own standing
// "one divisor, no dual bookkeeping" practice for gameplay pacing - they
// simply now take twice as long in real time. Sound is the one deliberate
// exception (see tmnAdvanceSeq()/tmnAdvanceBgSeq() below), rescaled so it
// keeps its own original real-time pace despite the halved tick rate.
#define TMN_TICK_DIVISOR 2
int tmnTickSkip;

// -----------------------------------------------------------------------------
//   Small helpers
// -----------------------------------------------------------------------------

int tmnMymap( int x, int in_min, int in_max, int out_min, int out_max )
{
    return ( x - in_min ) * ( out_max - out_min ) / ( in_max - in_min ) + out_min;
}

int tmnRD4()
{
    if( tmnRDC < 15 ) tmnRDC++;
    else tmnRDC = 0;
    return tmnRndArray[ tmnRDC ];
}

bool tmnReadDot( int x, int y )
{
    if( x < 0 || x >= TMN_LEVELW || y < 0 || y >= TMN_LEVELH ) return false;
    return tmnDotGrid[ x ][ y ];
}

void tmnWriteDot( int x, int y, bool value )
{
    if( x < 0 || x >= TMN_LEVELW || y < 0 || y >= TMN_LEVELH ) return;
    tmnDotGrid[ x ][ y ] = value;
}

void tmnCopyDotInTab()
{
    int y_, x_;
    for( y_ = 0; y_ < TMN_LEVELH; y_++ )
      for( x_ = 0; x_ < TMN_LEVELW; x_++ )
      {
          int cell = tmnGetLevelCell( x_, y_ );
          if( cell != 0 && cell != 3 && cell != 4 )
            tmnWriteDot( x_, y_, true );
          else
            tmnWriteDot( x_, y_, false );
      }
}

// -----------------------------------------------------------------------------
//   Sprite / movement logic
// -----------------------------------------------------------------------------

void tmnInitSpk( int Type_, TmnSprite* SPK_ )
{
    SPK_->Type = Type_;
    SPK_->active = 0;
    SPK_->SkipMode = 0;
    SPK_->health = 0;
    SPK_->DecalageX = 0;
    SPK_->DecalageY = 0;
    SPK_->GridX = TMN_PACPOSX;
    SPK_->GridY = TMN_PACPOSY;
    SPK_->Xdir = 0;
    SPK_->Ydir = 0;
}

void tmnTrimXpos( TmnSprite* SPK_ )
{
    SPK_->DecalageX = -SPK_->DecalageY / 2;
}

void tmnGo2Left( TmnSprite* SPK_ )
{
    if( SPK_->DecalageX > -( TMN_DRIFTX - 1 ) )
    {
        if( tmnGetLevelCell( SPK_->GridX - 1, SPK_->GridY ) ) SPK_->DecalageX--;
    }
    else
    {
        if( tmnGetLevelCell( SPK_->GridX - 1, SPK_->GridY ) )
        {
            SPK_->DecalageX = 0;
            SPK_->GridX--;
        }
    }
}

void tmnGo2Right( TmnSprite* SPK_ )
{
    if( SPK_->DecalageX < 0 )
    {
        SPK_->DecalageX++;
    }
    else
    {
        if( tmnGetLevelCell( SPK_->GridX + 1, SPK_->GridY ) )
        {
            SPK_->DecalageX = -( TMN_DRIFTX - 1 );
            SPK_->GridX++;
        }
    }
}

void tmnGo2Up( TmnSprite* SPK_ )
{
    if( SPK_->DecalageY > -( TMN_DRIFTY - 1 ) )
    {
        if( tmnGetLevelCell( SPK_->GridX, SPK_->GridY - 1 ) ) SPK_->DecalageY--;
    }
    else
    {
        if( tmnGetLevelCell( SPK_->GridX, SPK_->GridY - 1 ) )
        {
            SPK_->DecalageY = 0;
            SPK_->GridY--;
        }
    }
    tmnTrimXpos( SPK_ );
}

void tmnGo2Down( TmnSprite* SPK_ )
{
    if( SPK_->DecalageY < 0 )
    {
        SPK_->DecalageY++;
    }
    else
    {
        if( tmnGetLevelCell( SPK_->GridX, SPK_->GridY + 1 ) )
        {
            bool doorBlocked = ( tmnGetLevelCell( SPK_->GridX, SPK_->GridY + 1 ) == 4 && SPK_->Type == 1 );
            if( !doorBlocked )
            {
                SPK_->DecalageY = -( TMN_DRIFTY - 1 );
                SPK_->GridY++;
            }
        }
    }
    tmnTrimXpos( SPK_ );
}

int tmnMainAnim( TmnSprite* SPK_ )
{
    if( SPK_->Xdir == 1 ) return tmnFRM;
    if( SPK_->Xdir == -1 ) return 6 + tmnFRM;
    if( SPK_->Ydir == 1 ) return 3 + tmnFRM;
    if( SPK_->Ydir == -1 ) return 9 + tmnFRM;
    return 0;
}

void tmnASA()
{
    tmnCamX = -5 + tmnMainSprite.GridX;
    tmnCamY = -4 + tmnMainSprite.GridY;
}

int tmnGobAnim( TmnSprite* SPK_ )
{
    if( SPK_->health == 2 || SPK_->health == 0 ) return SPK_->health;
    if( tmnGobModeTimer > 0 && tmnGobModeTimer < 80 )
    {
        if( tmnBlink ) return SPK_->health;
        return 0;
    }
    return SPK_->health;
}

int tmnNeutralPosition( TmnSprite* SPK_ )
{
    if( SPK_->DecalageX == 0 && SPK_->DecalageY == 0 ) return 1;
    return 0;
}

int tmnMoveOk( int Dir_, TmnSprite* SPK_ )
{
    int x = SPK_->GridX;
    int y = SPK_->GridY;

    if( Dir_ == TMN_UP )
      return tmnGetLevelCell( x, y - 1 ) != 0;

    if( Dir_ == TMN_RIGHT )
      return tmnGetLevelCell( x + 1, y ) != 0;

    if( Dir_ == TMN_DOWN )
    {
        int cell = tmnGetLevelCell( x, y + 1 );
        return ( cell != 0 ) && !( ( cell == 4 ) && ( SPK_->Type == 1 ) );
    }

    if( Dir_ == TMN_LEFT )
      return tmnGetLevelCell( x - 1, y ) != 0;

    return 0;
}

void tmnReverseGhosts( TmnSprite* SPK_ )
{
    SPK_->Xdir = -SPK_->Xdir;
    SPK_->Ydir = -SPK_->Ydir;
}

int tmnCheckDirection( TmnSprite* SPK_ )
{
    if( SPK_->Xdir == 1 ) return TMN_LEFT;
    if( SPK_->Xdir == -1 ) return TMN_RIGHT;
    if( SPK_->Ydir == 1 ) return TMN_UP;
    if( SPK_->Ydir == -1 ) return TMN_DOWN;
    return TMN_LEFT;
}

int tmnTrackPointX( TmnSprite* SPK_ )
{
    if( SPK_->health != 0 ) return 7;
    return tmnMainSprite.GridX;
}

int tmnTrackPointY( TmnSprite* SPK_ )
{
    if( SPK_->health != 0 ) return 5;
    return tmnMainSprite.GridY;
}

void tmnTrackDirection( TmnSprite* SPK_ )
{
    if( tmnMoveOk( TMN_LEFT, SPK_ ) )
    {
        if( tmnCtrlLeft != 3 )
        {
            if( tmnTrackPointX( SPK_ ) < SPK_->GridX ) tmnCtrlLeft = 1;
            else tmnCtrlLeft = 2;
        }
    }
    else tmnCtrlLeft = 0;

    if( tmnMoveOk( TMN_RIGHT, SPK_ ) )
    {
        if( tmnCtrlRight != 3 )
        {
            if( tmnTrackPointX( SPK_ ) > SPK_->GridX ) tmnCtrlRight = 1;
            else tmnCtrlRight = 2;
        }
    }
    else tmnCtrlRight = 0;

    if( tmnMoveOk( TMN_UP, SPK_ ) )
    {
        if( tmnCtrlUp != 3 )
        {
            if( tmnTrackPointY( SPK_ ) < SPK_->GridY ) tmnCtrlUp = 1;
            else tmnCtrlUp = 2;
        }
    }
    else tmnCtrlUp = 0;

    if( tmnMoveOk( TMN_DOWN, SPK_ ) )
    {
        if( tmnCtrlDown != 3 )
        {
            if( tmnTrackPointY( SPK_ ) > SPK_->GridY ) tmnCtrlDown = 1;
            else tmnCtrlDown = 2;
        }
    }
    else tmnCtrlDown = 0;
}

void tmnSetCtrl( int Pos_, int Val_ )
{
    if( Pos_ == TMN_UP ) tmnCtrlUp = Val_;
    else if( Pos_ == TMN_RIGHT ) tmnCtrlRight = Val_;
    else if( Pos_ == TMN_DOWN ) tmnCtrlDown = Val_;
    else if( Pos_ == TMN_LEFT ) tmnCtrlLeft = Val_;
}

void tmnFixPriority( int Val_ )
{
    if( tmnCtrlLeft == Val_ ) tmnCtrlLeft = 1; else tmnCtrlLeft = 0;
    if( tmnCtrlRight == Val_ ) tmnCtrlRight = 1; else tmnCtrlRight = 0;
    if( tmnCtrlUp == Val_ ) tmnCtrlUp = 1; else tmnCtrlUp = 0;
    if( tmnCtrlDown == Val_ ) tmnCtrlDown = 1; else tmnCtrlDown = 0;
}

void tmnFixCtrl()
{
    if( tmnCtrlLeft == 1 || tmnCtrlRight == 1 || tmnCtrlUp == 1 || tmnCtrlDown == 1 )
    {
        tmnFixPriority( 1 );
        return;
    }
    if( tmnCtrlLeft == 2 || tmnCtrlRight == 2 || tmnCtrlUp == 2 || tmnCtrlDown == 2 )
    {
        tmnFixPriority( 2 );
        return;
    }
    if( tmnCtrlLeft == 3 || tmnCtrlRight == 3 || tmnCtrlUp == 3 || tmnCtrlDown == 3 )
    {
        tmnFixPriority( 3 );
        return;
    }
}

void tmnResetCtrl()
{
    tmnCtrlDown = 0;
    tmnCtrlUp = 0;
    tmnCtrlLeft = 0;
    tmnCtrlRight = 0;
}

void tmnSetMove( int Move_ )
{
    tmnResetCtrl();
    if( Move_ == TMN_LEFT ) tmnCtrlLeft = 1;
    else if( Move_ == TMN_RIGHT ) tmnCtrlRight = 1;
    else if( Move_ == TMN_UP ) tmnCtrlUp = 1;
    else if( Move_ == TMN_DOWN ) tmnCtrlDown = 1;
}

void tmnRandomDirection()
{
    int t;
    for( t = 0; t < 4; t++ )
    {
        int pick = tmnRD4();
        if( pick == TMN_LEFT )
        {
            if( tmnCtrlLeft == 1 || tmnCtrlLeft == 2 ) { tmnSetMove( TMN_LEFT ); return; }
        }
        else if( pick == TMN_RIGHT )
        {
            if( tmnCtrlRight == 1 || tmnCtrlRight == 2 ) { tmnSetMove( TMN_RIGHT ); return; }
        }
        else if( pick == TMN_UP )
        {
            if( tmnCtrlUp == 1 || tmnCtrlUp == 2 ) { tmnSetMove( TMN_UP ); return; }
        }
        else if( pick == TMN_DOWN )
        {
            if( tmnCtrlDown == 1 || tmnCtrlDown == 2 ) { tmnSetMove( TMN_DOWN ); return; }
        }
    }

    if( tmnCtrlLeft == 3 ) { tmnSetMove( TMN_LEFT ); return; }
    if( tmnCtrlRight == 3 ) { tmnSetMove( TMN_RIGHT ); return; }
    if( tmnCtrlUp == 3 ) { tmnSetMove( TMN_UP ); return; }
    if( tmnCtrlDown == 3 ) { tmnSetMove( TMN_DOWN ); return; }
}

void tmnGhostsDirectionProcess( TmnSprite* SPK_ )
{
    tmnCtrlLeft = 1; tmnCtrlRight = 1; tmnCtrlUp = 1; tmnCtrlDown = 1;
    tmnSetCtrl( tmnCheckDirection( SPK_ ), 3 );
    tmnTrackDirection( SPK_ );

    if( tmnRD4() > 2 ) tmnRandomDirection();
    else tmnFixCtrl();
}

void tmnControlUpdate( TmnSprite* SPK_ )
{
    if( SPK_->Type == 1 )
    {
        tmnResetCtrl();
        tmnCtrlLeft = isLeftPressed();
        tmnCtrlRight = isRightPressed();
        tmnCtrlUp = isUpPressed();
        tmnCtrlDown = isDownPressed();
    }
    else
    {
        if( SPK_->GridX == TMN_RESPAWNX && SPK_->GridY == TMN_RESPAWNY )
        {
            if( SPK_->health == 2 ) SPK_->health = 0;
        }
        tmnGhostsDirectionProcess( SPK_ );
    }
}

void tmnSelectDirection( TmnSprite* SPK_ )
{
    int N_ = tmnNeutralPosition( SPK_ );
    if( N_ == 0 ) return;

    tmnControlUpdate( SPK_ );
    if( tmnCtrlLeft && tmnMoveOk( TMN_LEFT, SPK_ ) ) { SPK_->Xdir = -1; SPK_->Ydir = 0; }
    if( tmnCtrlRight && tmnMoveOk( TMN_RIGHT, SPK_ ) ) { SPK_->Xdir = 1; SPK_->Ydir = 0; }
    if( tmnCtrlUp && tmnMoveOk( TMN_UP, SPK_ ) ) { SPK_->Ydir = -1; SPK_->Xdir = 0; }
    if( tmnCtrlDown && tmnMoveOk( TMN_DOWN, SPK_ ) ) { SPK_->Ydir = 1; SPK_->Xdir = 0; }
}

// -----------------------------------------------------------------------------
//   Sound sequencers - two independent instances (see this file's own
//   header comment for why): `tmnSeq*` drives the three genuinely-paced,
//   gameplay-freezing sequences (ghost-eaten/death/level-clear), while
//   `tmnBgSeq*` plays every small dot/fruit pickup cue in the background,
//   alongside ongoing gameplay, never freezing anything - matching Tiny
//   Pacman's own established two-cue-player precedent. Both can run
//   concurrently without cutting each other off, since `md_playTone()`
//   is genuinely multi-voice project-wide.
// -----------------------------------------------------------------------------

int* tmnSeqNotes;
int tmnSeqCount;
int tmnSeqIndex;
int tmnSeqWaitFrames;
bool tmnSeqActive;

void tmnStartSeq( int* notes, int count )
{
    tmnSeqNotes = notes;
    tmnSeqCount = count;
    tmnSeqIndex = 0;
    tmnSeqWaitFrames = 0;
    tmnSeqActive = true;
}

// Returns true once the sequence has finished.
bool tmnAdvanceSeq()
{
    if( !tmnSeqActive ) return true;

    if( tmnSeqWaitFrames > 0 )
    {
        tmnSeqWaitFrames--;
        return false;
    }

    if( tmnSeqIndex >= tmnSeqCount )
    {
        tmnSeqActive = false;
        return true;
    }

    int freq = tmnSeqNotes[ tmnSeqIndex ];
    int dur = tmnSeqNotes[ tmnSeqIndex + 1 ];
    Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    // Rescaled against the *logic* tick rate (30fps under
    // TMN_TICK_DIVISOR), not the engine's native 60fps - both sequencers
    // are only ever advanced from inside gameTinyMania_update(), which
    // now only runs every other real frame, so counting against 60.0
    // here would silently halve every sound's own real playback speed.
    int waitFrames = (int)( durationSeconds * ( 60.0 / TMN_TICK_DIVISOR ) );
    if( waitFrames < 1 ) waitFrames = 1;
    tmnSeqWaitFrames = waitFrames;
    tmnSeqIndex = tmnSeqIndex + 2;

    return false;
}

int* tmnBgNotes;
int tmnBgCount;
int tmnBgIndex;
int tmnBgWaitFrames;
bool tmnBgActive;

void tmnStartBgSeq( int* notes, int count )
{
    tmnBgNotes = notes;
    tmnBgCount = count;
    tmnBgIndex = 0;
    tmnBgWaitFrames = 0;
    tmnBgActive = true;
}

void tmnAdvanceBgSeq()
{
    if( !tmnBgActive ) return;

    if( tmnBgWaitFrames > 0 )
    {
        tmnBgWaitFrames--;
        return;
    }

    if( tmnBgIndex >= tmnBgCount )
    {
        tmnBgActive = false;
        return;
    }

    int freq = tmnBgNotes[ tmnBgIndex ];
    int dur = tmnBgNotes[ tmnBgIndex + 1 ];
    Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    // Rescaled against the *logic* tick rate (30fps under
    // TMN_TICK_DIVISOR), not the engine's native 60fps - both sequencers
    // are only ever advanced from inside gameTinyMania_update(), which
    // now only runs every other real frame, so counting against 60.0
    // here would silently halve every sound's own real playback speed.
    int waitFrames = (int)( durationSeconds * ( 60.0 / TMN_TICK_DIVISOR ) );
    if( waitFrames < 1 ) waitFrames = 1;
    tmnBgWaitFrames = waitFrames;
    tmnBgIndex = tmnBgIndex + 2;
}

// -----------------------------------------------------------------------------
//   Scoring / bonus
// -----------------------------------------------------------------------------

void tmnBonusCollected()
{
    tmnBonusDelivery = 0;
    tmnBonusTimer = 0;
    tmnBonusAnim = 0;
}

// Matches upstream's own `BonusDelivery()` - renamed to avoid colliding
// with the `tmnBonusDelivery` state variable itself (upstream's C++-
// flavored naming has a function and a struct field share the same bare
// name, `Bonus.Delivery` vs `BonusDelivery()`, disambiguated there only
// by the struct-member dot - this dialect has no such struct-vs-function
// namespacing, so the function needed its own distinct name).
void tmnRollBonusDelivery()
{
    if( tmnBonusOrder )
    {
        if( tmnRD4() > 1 ) tmnBonusDelivery = 5;
        else tmnBonusDelivery = 6;
    }
    else
      tmnBonusDelivery = tmnBonusFruit + 1;

    tmnBonusTimer = 255;
    tmnBonusOrder = !tmnBonusOrder;
}

void tmnScores( int Add_ )
{
    if( Add_ > 0 ) tmnHudDirty = true;
    int i;
    for( i = 0; i < Add_; i++ )
    {
        tmnDigits[4]++;
        if( tmnDigits[4] > 9 )
        {
            tmnDigits[4] = 0;
            tmnDigits[3]++;
            if( tmnDigits[3] > 9 )
            {
                tmnDigits[3] = 0;
                tmnDigits[2]++;
                if( tmnDigits[2] > 9 )
                {
                    tmnDigits[2] = 0;
                    tmnDigits[1]++;
                    tmnRollBonusDelivery();
                    if( tmnDigits[1] > 9 )
                    {
                        tmnDigits[1] = 0;
                        tmnDigits[0]++;
                        if( tmnDigits[0] > 9 ) tmnDigits[0] = 0;
                    }
                }
            }
        }
    }
}

void tmnResetScores()
{
    tmnDigits[0] = 0; tmnDigits[1] = 0; tmnDigits[2] = 0; tmnDigits[3] = 0; tmnDigits[4] = 0;
    tmnBonusCollected();
    tmnHudDirty = true;
}

void tmnActivateGobTimer()
{
    tmnGobModeTimer = tmnGobDotTimer;
    int t;
    for( t = 0; t < tmnNbGhosts; t++ )
    {
        if( tmnGhosts[ t ].health == 0 )
        {
            tmnGhosts[ t ].health = 1;
            tmnReverseGhosts( &tmnGhosts[ t ] );
        }
    }
}

void tmnBonusPickup()
{
    if( tmnBonusDelivery >= 1 && tmnBonusDelivery <= 4 )
    {
        tmnScores( 255 );
        tmnBonusCollected();
        tmnStartBgSeq( tmnGubFreq_Dur, 16 );
    }
    else if( tmnBonusDelivery == 5 )
    {
        tmnScores( 255 );
        tmnActivateGobTimer();
        tmnBonusCollected();
        tmnStartBgSeq( tmnBigDotNotes, 8 );
    }
    else if( tmnBonusDelivery == 6 )
    {
        tmnScores( 255 );
        tmnPacSpeedTimer = 255;
        tmnBonusCollected();
        tmnStartBgSeq( tmnBigDotNotes, 8 );
    }
}

void tmnCheckCollectible()
{
    if( tmnJmpPos > 2 ) return;

    if( tmnReadDot( tmnMainSprite.GridX, tmnMainSprite.GridY ) )
    {
        if( tmnGetLevelCell( tmnMainSprite.GridX, tmnMainSprite.GridY ) == 2 )
        {
            tmnActivateGobTimer();
            tmnStartBgSeq( tmnBigDotNotes, 8 );
            tmnScores( 30 );
        }
        else
        {
            tmnStartBgSeq( tmnPetitDotNotes, 4 );
            tmnScores( 15 );
        }
        tmnTotalDotsCollected++;
        if( tmnTotalDotsCollected == TMN_TOTAL_DOTS )
        {
            tmnPlayState = TMN_PLAY_LEVELCLEAR_WAIT1;
            tmnPlayWaitFrames = 30; // ~500ms at 60fps
        }
        tmnWriteDot( tmnMainSprite.GridX, tmnMainSprite.GridY, false );
    }

    if( tmnMainSprite.GridX == TMN_BNSPOSX && tmnMainSprite.GridY == TMN_BNSPOSY && tmnBonusDelivery )
      tmnBonusPickup();
}

void tmnUpdateMove( TmnSprite* SPK_ )
{
    if( SPK_->Xdir == -1 ) tmnGo2Left( SPK_ );
    if( SPK_->Xdir == 1 ) tmnGo2Right( SPK_ );
    if( SPK_->Ydir == -1 ) tmnGo2Up( SPK_ );
    if( SPK_->Ydir == 1 ) tmnGo2Down( SPK_ );
    // Always checks the *player's* own neutral position regardless of
    // which sprite is being updated - a faithfully-preserved upstream
    // quirk, see this file's own header comment.
    if( tmnNeutralPosition( &tmnMainSprite ) ) tmnCheckCollectible();
}

void tmnMainPlayerUpdate()
{
    if( isFirePressed() && tmnJmpTrig == 2 ) tmnJmpTrig = 1;

    tmnSelectDirection( &tmnMainSprite );
    tmnUpdateMove( &tmnMainSprite );

    if( tmnPacSpeedTimer )
    {
        tmnSelectDirection( &tmnMainSprite );
        tmnUpdateMove( &tmnMainSprite );
        if( tmnPacSpeedTimer ) tmnPacSpeedTimer--;
    }
}

void tmnGhostsUpdate()
{
    int t;
    for( t = 0; t < tmnNbGhosts; t++ )
    {
        if( tmnGhosts[ t ].SkipMode == 0 || tmnGhosts[ t ].health == 2 )
        {
            tmnSelectDirection( &tmnGhosts[ t ] );
            tmnUpdateMove( &tmnGhosts[ t ] );
            tmnGhosts[ t ].SkipMode = tmnGhosts[ t ].Speed;
        }
        else
        {
            if( tmnGhosts[ t ].SkipMode != 0 ) tmnGhosts[ t ].SkipMode--;
        }
    }
}

// Returns 0 = no collision, 1 = a lethal (normal) ghost was touched,
// 2 = a frightened ghost was eaten (its own health is already flipped to
// 2/eaten here - the caller drives the sound/pause/score sequence).
int tmnPacCollision()
{
    int sprite1x = tmnMainSprite.GridX * ( TMN_DRIFTX - 1 ) + tmnMainSprite.DecalageX;
    int sprite1y = tmnMainSprite.GridY * ( TMN_DRIFTY - 1 ) + tmnMainSprite.DecalageY;

    int t;
    for( t = 0; t < tmnNbGhosts; t++ )
    {
        int sprite2x = tmnGhosts[ t ].GridX * ( TMN_DRIFTX - 1 ) + tmnGhosts[ t ].DecalageX;
        int sprite2y = tmnGhosts[ t ].GridY * ( TMN_DRIFTY - 1 ) + tmnGhosts[ t ].DecalageY;

        int xDist = sprite1x - sprite2x;
        int yDist = sprite1y - sprite2y;

        if( abs( xDist ) <= 2 && abs( yDist ) <= 2 )
        {
            if( abs( tmnJmpPos ) <= 2 || t == TMN_JUMPINGGHOST )
            {
                if( tmnGhosts[ t ].health == 0 ) return 1;
                if( tmnGhosts[ t ].health == 1 )
                {
                    tmnGhosts[ t ].health = 2;
                    return 2;
                }
            }
        }
    }
    return 0;
}

void tmnRefreshJump()
{
    if( tmnJmpTrig == 1 )
    {
        tmnJmpPos = tmnJump[ tmnJmpSeq ];
        if( tmnJmpSeq < 23 ) tmnJmpSeq++;
        else { tmnJmpSeq = 0; tmnJmpTrig = 2; }
    }
}

// -----------------------------------------------------------------------------
//   Level / game lifecycle
// -----------------------------------------------------------------------------

void tmnAdjGamePlay()
{
    tmnBLKs = ( ( tmnLvl % 2 ) != 0 );
    tmnGhostSpeed = tmnMymap( tmnLvl, 0, TMN_NBLVL, 2, 0 );
    tmnNbGhosts = tmnMymap( tmnLvl, 0, TMN_NBLVL, 4, TMN_NUM_GHOST );
    tmnGobDotTimer = tmnMymap( tmnLvl, 0, TMN_NBLVL, 255, 100 );
}

void tmnAlwaysInit()
{
    tmnGobDotTimer = 0;
    tmnPacSpeedTimer = 0;
    tmnBonusTimer = 0;
    tmnBonusDelivery = 0;
    tmnJmpSeq = 0;
    tmnJmpTrig = 2;
    tmnJmpPos = 0;
    tmnGobModeTimer = 0;
    tmnFRM = 0;
    tmnResetCtrl();
}

void tmnFade2Black( int val )
{
    tmnFadeActive = 1;
    tmnFadeTrigger = val;
}

void tmnRestartLevel()
{
    tmnAlwaysInit();
    tmnAdjGamePlay();
    tmnInitSpk( 1, &tmnMainSprite );
    int t;
    for( t = 0; t < tmnNbGhosts; t++ )
    {
        tmnInitSpk( 0, &tmnGhosts[ t ] );
        tmnGhosts[ t ].GridY = tmnGhosts[ t ].GridY - 6;
        tmnGhosts[ t ].Speed = tmnGhostSpeed;
    }
    if( tmnPacLives > 0 ) tmnPacLives--;
    else tmnFade2Black( 0 );
    tmnHudDirty = true;
}

void tmnInitNewLevel()
{
    tmnAlwaysInit();
    tmnTotalDotsCollected = 0;
    tmnBonusOrder = 0;
    tmnAdjGamePlay();
    tmnInitSpk( 1, &tmnMainSprite );
    tmnCopyDotInTab();
    int t;
    for( t = 0; t < tmnNbGhosts; t++ )
    {
        tmnInitSpk( 0, &tmnGhosts[ t ] );
        tmnGhosts[ t ].GridY = tmnGhosts[ t ].GridY - 6;
        tmnGhosts[ t ].Speed = tmnGhostSpeed;
    }
}

void tmnNextLevel()
{
    if( tmnLvl < 7 ) tmnLvl++;
    else tmnLvl = 7;

    tmnInitNewLevel();

    if( tmnLvl < 3 ) tmnBonusFruit = tmnBonusFruit + 1;
    else tmnBonusFruit = tmnLvl - 4;
}

void tmnInitNewGame()
{
    tmnPacLives = 3;
    tmnLvl = 0;
    tmnBonusFruit = 0;
    tmnInitNewLevel();
    tmnResetScores();
}

void tmnGobEnding()
{
    int t;
    for( t = 0; t < tmnNbGhosts; t++ )
      if( tmnGhosts[ t ].health == 1 ) tmnGhosts[ t ].health = 0;
}

void tmnGobTimer()
{
    if( tmnGobModeTimer != 0 )
    {
        tmnGobModeTimer--;
        if( tmnGobModeTimer == 0 ) tmnGobEnding();
    }
    tmnBlink = !tmnBlink;
}

void tmnAnimTick()
{
    if( tmnDIR != 0 )
    {
        tmnDIR--;
    }
    else
    {
        tmnDIR = 1;
        if( tmnFRM != 2 ) tmnFRM++;
        else tmnFRM = 0;

        if( tmnFadeActive == 0 )
        {
            if( tmnBonusAnim < 7 ) tmnBonusAnim++;
            else tmnBonusAnim = 0;
        }
        if( tmnBonusTimer > 0 )
        {
            tmnBonusTimer--;
            if( tmnBonusTimer == 0 ) tmnBonusDelivery = 0;
        }
    }
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

void tmnDrawSprite2Bit( int x0, int y0, int* sprite, int frame, bool mono )
{
    int w = sprite[0];
    int h = sprite[1];
    int pages = ( h + 7 ) >> 3;
    int planeSize = w * pages;
    int frameSize = planeSize * 2;

    int base = 2 + frame * frameSize;
    int whiteBase = base;
    int blackBase = base + planeSize;

    if( x0 >= 64 || y0 >= 32 || x0 + w <= 0 || y0 + h <= 0 ) return;

    int x;
    for( x = 0; x < w; x++ )
    {
        int sx = x0 + x;
        if( sx < 0 || sx >= 64 ) continue;

        int p;
        for( p = 0; p < pages; p++ )
        {
            int offset = x + p * w;
            int wb = sprite[ whiteBase + offset ];
            // Some of this game's own decorative attract-screen sprites
            // (Start2, TinyMania2) are declared upstream with only a
            // single data plane, not the two (white+black) this formula
            // generally expects - harmless on real AVR PROGMEM (an
            // adjacent-flash-byte read that's simply never acted on), but
            // a genuine out-of-bounds global read on Vircon32. Since
            // every one of those single-plane tables is only ever called
            // with mono==false, and the black plane is only ever
            // meaningful when mono==true, skip reading it entirely
            // otherwise - a real correctness fix, not just a micro-
            // optimization.
            int bb = 0;
            if( mono ) bb = sprite[ blackBase + offset ];
            if( !wb && !bb ) continue;

            int baseY = p << 3;
            int maxBit = h - baseY;
            if( maxBit > 8 ) maxBit = 8;

            int bit;
            for( bit = 0; bit < maxBit; bit++ )
            {
                int mask = 1 << bit;
                int sy = y0 + baseY + bit;
                if( sy < 0 || sy >= 32 ) continue;

                if( ( bb & mask ) && mono )
                  tmnVBuffer[ sy >> 3 ][ sx ] = tmnVBuffer[ sy >> 3 ][ sx ] & ~( 1 << ( sy & 7 ) );
                else if( wb & mask )
                  tmnVBuffer[ sy >> 3 ][ sx ] = tmnVBuffer[ sy >> 3 ][ sx ] | ( 1 << ( sy & 7 ) );
            }
        }
    }
}

int tmnSliceByte( int page, int data )
{
    // Central defensive mask, matching this project's own established
    // "fix it once, centrally" precedent (md_drawColumn()/Tiny Arena's
    // own arSliceByte()) - Vircon32 ints don't truncate the way AVR's
    // uint8_t gave upstream, so a stray high bit from any arithmetic
    // elsewhere would otherwise reach the expand[] lookup out of range.
    data = data & 0xFF;
    int nibble;
    if( page & 1 ) nibble = data >> 4;
    else nibble = data & 0x0F;
    return tmnExpand[ nibble ];
}

// Per-cell ghost index (a small bucket linked list, since level start
// genuinely can place *several* ghosts on the exact same grid cell
// simultaneously - see this function's own header comment) - built once
// per frame, O(ghosts), so the main per-cell scan below can look up
// "which ghost(s), if any, are here" in O(1) instead of re-scanning all
// `tmnNbGhosts` ghosts for every one of the ~168 cells it visits (the
// same "self-gated check still costs a full iteration every time it
// runs" shape already found and fixed in several other ports here -
// Bomber/Pacman/Missile's own O(pixels x sprites) render loops).
int[15][21] tmnGhostCellHead;
int[7] tmnGhostCellNext;

// Iterating ghosts high-to-low while always prepending to the bucket's
// own head preserves the exact same draw order upstream's own ascending
// `for(t=0;t<NbGhosts;t++)` scan would produce for any ghosts genuinely
// sharing one cell (a real, common case right after a level/life starts,
// since every ghost's own `GridX` is left at the same spawn column until
// they've each had a chance to move apart) - not just an approximation.
void tmnBuildGhostCellIndex()
{
    int x, y;
    for( x = 0; x < TMN_LEVELW; x++ )
      for( y = 0; y < TMN_LEVELH; y++ )
        tmnGhostCellHead[ x ][ y ] = -1;

    int t;
    for( t = tmnNbGhosts - 1; t >= 0; t-- )
    {
        int gx = tmnGhosts[ t ].GridX;
        int gy = tmnGhosts[ t ].GridY;
        if( gx >= 0 && gx < TMN_LEVELW && gy >= 0 && gy < TMN_LEVELH )
        {
            tmnGhostCellNext[ t ] = tmnGhostCellHead[ gx ][ gy ];
            tmnGhostCellHead[ gx ][ gy ] = t;
        }
    }
}

// Specialized for the w=9,h=7,1-frame,2-plane shape shared by Block1/
// Block2/Dot/BigDot - the four sprites `tmnDrawLevel()`'s own per-cell
// scan calls most often (up to ~150+ times/frame combined). Skips the
// generic `tmnDrawSprite2Bit()`'s own frame/plane-size arithmetic
// (`pages`/`planeSize`/`frameSize`/`base` - all compile-time-constant
// for this one specific shape, but recomputed fresh on every one of
// those calls) - measured via the perf overlay to meaningfully help
// (see this function's own call sites below and `tmnDrawLevel()`'s own
// header note).
void tmnBlit9x7( int x0, int y0, int* sprite, bool mono )
{
    if( x0 >= 64 || y0 >= 32 || x0 + 9 <= 0 || y0 + 7 <= 0 ) return;

    int x;
    for( x = 0; x < 9; x++ )
    {
        int sx = x0 + x;
        if( sx < 0 || sx >= 64 ) continue;

        int wb = sprite[ 2 + x ];
        int bb = 0;
        if( mono ) bb = sprite[ 11 + x ];
        if( !wb && !bb ) continue;

        int bit;
        for( bit = 0; bit < 7; bit++ )
        {
            int mask = 1 << bit;
            int sy = y0 + bit;
            if( sy < 0 || sy >= 32 ) continue;

            if( ( bb & mask ) && mono )
              tmnVBuffer[ sy >> 3 ][ sx ] = tmnVBuffer[ sy >> 3 ][ sx ] & ~( 1 << ( sy & 7 ) );
            else if( wb & mask )
              tmnVBuffer[ sy >> 3 ][ sx ] = tmnVBuffer[ sy >> 3 ][ sx ] | ( 1 << ( sy & 7 ) );
        }
    }
}

// A batched, whole-column version of this (writing all 7 bits into
// tmnVBuffer per column in at most 4 masked read-modify-writes instead
// of a 7-iteration bit loop) - worth doing specifically for walls, the
// single largest remaining per-cell rendering cost, since unlike
// Dot/BigDot's own mostly-transparent art, a wall tile's dense checkered
// pattern almost never benefits from the early "empty column" exit
// above. A first attempt at this shipped a real, user-reported bug
// ("this introduced bugs in wall drawing and scrolling of them") -
// **not** the shift-on-a-negative-value hazard the design deliberately
// avoided (that reasoning was sound and is still why this never shifts
// `sy0` directly), but a plain arithmetic mistake in the very next step:
// the shift *direction* was backwards in both branches below (a page
// "at or after" the source's own start row needs the source shifted
// *right* to align, not left, and vice versa), plus both boundary
// guards were off by one. Re-derived from scratch algebraically (`row =
// y0+i = page*8+b` => `b = i - shift` where `shift = page*8-y0`) and
// confirmed via a 20,000-case randomized brute-force comparison against
// the plain per-bit loop above before shipping this corrected version -
// the same "don't trust reasoning alone, verify" lesson this project has
// needed repeatedly elsewhere, just applied here via an offline script
// instead of an in-engine screenshot, since this is pure bit arithmetic
// with no rendering dependency to screenshot in the first place.
void tmnBlitWallCol( int sx, int sy0, int wb7, int bb7 )
{
    if( sx < 0 || sx >= 64 ) return;

    int clearBits = bb7;            // walls are always drawn mono=true
    int setBits = wb7 & ~clearBits; // matches the original's "clear wins over set" per-bit priority
    if( !clearBits && !setBits ) return;

    int page;
    for( page = 0; page < 4; page++ )
    {
        int shift = page * 8 - sy0;

        int pageClear, pageSet;
        if( shift >= 0 )
        {
            if( shift >= 7 ) continue; // this page starts at/after row y0+6 - no overlap
            pageClear = ( clearBits >> shift ) & 0xFF;
            pageSet = ( setBits >> shift ) & 0xFF;
        }
        else
        {
            int drop = -shift;
            if( drop >= 8 ) continue; // this page ends before row y0 - no overlap
            pageClear = ( clearBits << drop ) & 0xFF;
            pageSet = ( setBits << drop ) & 0xFF;
        }
        if( !pageClear && !pageSet ) continue;

        tmnVBuffer[ page ][ sx ] = ( tmnVBuffer[ page ][ sx ] & ~pageClear ) | pageSet;
    }
}

void tmnBlitWall9x7( int x0, int y0, int* sprite )
{
    if( x0 >= 64 || y0 >= 32 || x0 + 9 <= 0 || y0 + 7 <= 0 ) return;

    int x;
    for( x = 0; x < 9; x++ )
    {
        int sx = x0 + x;
        int wb = sprite[ 2 + x ];
        int bb = sprite[ 11 + x ];
        if( !wb && !bb ) continue;
        tmnBlitWallCol( sx, y0, wb, bb );
    }
}

void tmnDrawLevel()
{
    tmnBuildGhostCellIndex();
    tmnASA();
    int ScrX = -tmnMainSprite.DecalageX;
    int ScrY = -tmnMainSprite.DecalageY;
    int offsetX = -4;
    int offsetY = 3;
    int* wallSprite = tmnResolveBlocks( tmnBLKs ); // hoisted - constant for the whole frame, was re-resolved on every one of up to ~90 wall draws

    int y;
    for( y = -2 + tmnCamY; y < 10 + tmnCamY; y++ )
    {
        int x;
        for( x = -2 + tmnCamX; x < 12 + tmnCamX; x++ )
        {
            if( x <= TMN_LEVELW_1 && x >= 0 && y <= TMN_LEVELH - 1 && y >= 0 )
            {
                if( tmnCheckGrid( x, y ) == 0 )
                  tmnBlitWall9x7( x * TMN_DRIFTX - offsetX - ( tmnCamX * TMN_DRIFTX ) + ScrX, y * TMN_DRIFTY - ( tmnCamY * TMN_DRIFTY ) + ScrY - offsetY, wallSprite );

                if( tmnReadDot( x, y ) )
                {
                    int cell = tmnGetLevelCell( x, y );
                    if( cell == 1 )
                      tmnBlit9x7( x * TMN_DRIFTX - offsetX - ( tmnCamX * TMN_DRIFTX ) + ScrX, y * TMN_DRIFTY - ( tmnCamY * TMN_DRIFTY ) + ScrY - offsetY, tmnDot, true );
                    else if( cell == 2 )
                      tmnBlit9x7( x * TMN_DRIFTX - offsetX - ( tmnCamX * TMN_DRIFTX ) + ScrX, y * TMN_DRIFTY - ( tmnCamY * TMN_DRIFTY ) + ScrY - offsetY, tmnBigDot, true );
                }

                if( TMN_BNSPOSX == x && TMN_BNSPOSY == y && tmnBonusDelivery )
                  tmnDrawSprite2Bit( x * TMN_DRIFTX - offsetX - ( tmnCamX * TMN_DRIFTX ) + ScrX, y * TMN_DRIFTY - ( tmnCamY * TMN_DRIFTY ) + ScrY - tmnAnimFruits[ tmnBonusAnim ] - offsetY, tmnFruits, tmnBonusDelivery - 1, true );

                if( tmnMainSprite.GridX == x && tmnMainSprite.GridY == y )
                  tmnDrawSprite2Bit( ( x * TMN_DRIFTX ) - ( tmnCamX * TMN_DRIFTX ) + 1 - offsetX, ( y * TMN_DRIFTY ) - ( tmnCamY * TMN_DRIFTY ) - tmnJmpPos - offsetY, tmnPac, tmnMainAnim( &tmnMainSprite ), true );

                int GC = tmnGhostCellHead[ x ][ y ];
                while( GC != -1 )
                {
                    int jumpOffset = 0;
                    if( GC == TMN_JUMPINGGHOST ) jumpOffset = tmnJmpPos;
                    tmnDrawSprite2Bit( ( x * TMN_DRIFTX ) - ( tmnCamX * TMN_DRIFTX ) + 1 - offsetX + ScrX + tmnGhosts[ GC ].DecalageX, ( y * TMN_DRIFTY ) - ( tmnCamY * TMN_DRIFTY ) + ScrY + tmnGhosts[ GC ].DecalageY - offsetY - jumpOffset, tmnResolveGhostPic( tmnGobAnim( &tmnGhosts[ GC ] ) ), tmnMainAnim( &tmnGhosts[ GC ] ), true );
                    GC = tmnGhostCellNext[ GC ];
                }
            }
        }
        offsetX = offsetX + 2;
    }

    if( tmnBonusDelivery )
      tmnDrawSprite2Bit( 57, -1, tmnFruits, tmnBonusDelivery - 1, true );
}

int tmnRecupDigital( int* pos, int* dig )
{
    int index = *pos + 4 * tmnDigits[ *dig ];
    int value = tmnPolice[ index ];
    if( *pos < 3 ) (*pos)++;
    else { (*dig)++; *pos = 0; }
    return value;
}

int tmnRecupLives( int* pos )
{
    int value = tmnMiniPac[ *pos ];
    if( *pos < 5 ) (*pos)++;
    else *pos = 0;
    return value;
}

// Rebuilds the score-digit/lives-icon portion of row 7 into tmnHudCache
// (and records, per output column, whether it's HUD at all via
// tmnHudIsCol) - only actually called when tmnHudDirty is set, i.e. when
// tmnScores()/tmnResetScores()/tmnRestartLevel() last changed tmnDigits[]
// or tmnPacLives, not on every frame. Structurally identical to the
// original per-frame loop (same pos_/dig_/pos2_ cursor-threading through
// tmnRecupDigital()/tmnRecupLives()), just run once and cached instead
// of unconditionally every tick.
void tmnRebuildHudCache()
{
    int pos_ = 0, pos2_ = 0, dig_ = 0;
    int halfX;
    int outIdx = 0;
    for( halfX = 0; halfX < 64; halfX++ )
    {
        int sub;
        for( sub = 0; sub < 2; sub++ )
        {
            if( halfX > 53 )
            {
                tmnHudCache[ outIdx ] = tmnRecupDigital( &pos_, &dig_ );
                tmnHudIsCol[ outIdx ] = true;
            }
            else if( halfX + 1 < 3 * tmnPacLives )
            {
                tmnHudCache[ outIdx ] = tmnRecupLives( &pos2_ );
                tmnHudIsCol[ outIdx ] = true;
            }
            else
            {
                tmnHudIsCol[ outIdx ] = false;
            }
            outIdx++;
        }
    }
    tmnHudDirty = false;
}

void tmnComposeRow7()
{
    if( tmnHudDirty ) tmnRebuildHudCache();

    int halfX;
    int outIdx = 0;
    for( halfX = 0; halfX < 64; halfX++ )
    {
        // The level-background portion genuinely changes every frame
        // (camera scroll) - always recomputed fresh, never cached.
        bool needLevel = ( halfX <= 53 ) && ( halfX + 1 >= 3 * tmnPacLives );
        int out = 0;
        if( needLevel ) out = tmnSliceByte( 7, tmnVBuffer[ 3 ][ halfX ] );

        int sub;
        for( sub = 0; sub < 2; sub++ )
        {
            if( tmnHudIsCol[ outIdx ] ) tmnRow7Buffer[ outIdx ] = tmnHudCache[ outIdx ];
            else tmnRow7Buffer[ outIdx ] = out;
            outIdx++;
        }
    }
}

void tmnTinyFlip()
{
    md_beginFrame();
    int fadeMask = tmnFD[ tmnFadeFrame ];

    int p;
    for( p = 0; p < 8; p++ )
    {
        int bufPage = p >> 1;
        if( p == 7 && tmnIngame )
        {
            tmnComposeRow7();
            int x;
            for( x = 0; x < 128; x++ )
              md_drawColumn( x, p, tmnRow7Buffer[ x ] & fadeMask );
        }
        else
        {
            int x;
            for( x = 0; x < 64; x++ )
            {
                int out = tmnSliceByte( p, tmnVBuffer[ bufPage ][ x ] ) & fadeMask;
                md_drawColumn( x * 2, p, out );
                md_drawColumn( x * 2 + 1, p, out );
            }
        }
    }
}

void tmnClearVBuffer()
{
    int p, x;
    for( p = 0; p < 4; p++ )
      for( x = 0; x < 64; x++ )
        tmnVBuffer[ p ][ x ] = 0;
}

void tmnSetPixel( int x, int y )
{
    if( x < 0 || x >= 64 || y < 0 || y >= 32 ) return;
    tmnVBuffer[ y >> 3 ][ x ] = tmnVBuffer[ y >> 3 ][ x ] | ( 1 << ( y & 7 ) );
}

void tmnIntro()
{
    // Both border loops below are a real, measured CPU hotspot (confirmed
    // via the perf overlay: attract-screen CPU pegged at a saturated
    // 100%) - upstream draws each solid border line one pixel-column at a
    // time via a full `drawSprite2Bit()` call (55 real-time-tolerant
    // bit-bang calls per horizontal line on real AVR hardware, 220 calls
    // total for all 4 lines; 66 more for the 6 vertical lines) - the same
    // "a naive per-column call loop is fine on real hardware but not
    // under this project's own per-call-overhead-dominated cost model"
    // shape already found and fixed in several other ports here (Tiny
    // Trick's background lookup, Tiny Bike's bomb rendering, etc).
    // Reproduced as direct `tmnVBuffer` writes instead - the exact same
    // pixels, just without 286 wasted function calls/frame. The 22
    // animated "marching dashes" `Pix` calls (which do need their own
    // per-position logic, since they animate via `tmnFRM`) are similarly
    // inlined via `tmnSetPixel()` rather than a full sprite-blit call for
    // a single bit.
    int y;
    for( y = 0; y < 32; y = y + 3 )
    {
        tmnSetPixel( 1, y + tmnFRM );
        tmnSetPixel( 62, y + tmnFRM );
    }

    // Vertical2 is a fully-solid 8-tall strip (data 0xFF) drawn at 11
    // overlapping y-offsets (0,3,...,30) - net effect is simply "every
    // row, 0-31, is set" at each of these 6 fixed columns.
    tmnVBuffer[0][0] = 255; tmnVBuffer[1][0] = 255; tmnVBuffer[2][0] = 255; tmnVBuffer[3][0] = 255;
    tmnVBuffer[0][2] = 255; tmnVBuffer[1][2] = 255; tmnVBuffer[2][2] = 255; tmnVBuffer[3][2] = 255;
    tmnVBuffer[0][63] = 255; tmnVBuffer[1][63] = 255; tmnVBuffer[2][63] = 255; tmnVBuffer[3][63] = 255;
    tmnVBuffer[0][61] = 255; tmnVBuffer[1][61] = 255; tmnVBuffer[2][61] = 255; tmnVBuffer[3][61] = 255;
    tmnVBuffer[0][4] = 255; tmnVBuffer[1][4] = 255; tmnVBuffer[2][4] = 255; tmnVBuffer[3][4] = 255;
    tmnVBuffer[0][59] = 255; tmnVBuffer[1][59] = 255; tmnVBuffer[2][59] = 255; tmnVBuffer[3][59] = 255;

    // Horizon2 sets a single bit (row 0 of its own 8-tall span) - the 4
    // horizontal lines land at page/bit (0,0), (1,7), (2,1), (3,7)
    // respectively (from y0=0/15/17/31 via >>3 and &7).
    int yy;
    for( yy = 4; yy < 59; yy++ )
    {
        tmnVBuffer[0][yy] = tmnVBuffer[0][yy] | 1;
        tmnVBuffer[1][yy] = tmnVBuffer[1][yy] | 128;
        tmnVBuffer[2][yy] = tmnVBuffer[2][yy] | 2;
        tmnVBuffer[3][yy] = tmnVBuffer[3][yy] | 128;
    }

    tmnDrawSprite2Bit( 9 + tmnAnimFruits[ tmnBonusAnim ], 4, tmnTinyMania2, 0, false );
    if( tmnBonusAnim > 3 ) tmnDrawSprite2Bit( 23, 22, tmnStart2, 0, false );
    tmnDrawSprite2Bit( 13, 23, tmnFruits, 3, false );
    tmnDrawSprite2Bit( 52, 18, tmnGhost, 3 + tmnFRM, false );
    tmnDrawSprite2Bit( 5, 18, tmnPac, tmnFRM, false );
    tmnDrawSprite2Bit( 43, 23, tmnFruits, 0, false );
}

// -----------------------------------------------------------------------------
//   Fade / state dispatch
// -----------------------------------------------------------------------------

void tmnMenuFadeSelect()
{
    if( tmnFadeTrigger == 0 )
    {
        tmnPostBlackWaitFrames = 0;
        tmnFadeActive = 3;
        tmnAttractFireHeld = isFirePressed();
    }
    else if( tmnFadeTrigger == 1 )
    {
        tmnInitNewGame();
        tmnPostBlackWaitFrames = 30; // ~500ms
        tmnFadeActive = 3;
    }
    else if( tmnFadeTrigger == 2 )
    {
        // tmnRestartLevel() may call tmnFade2Black(0) itself (last life
        // lost) - if so, don't clobber that re-trigger's own setup.
        // Checking tmnFadeActive here would NOT work to detect this -
        // both tmnFade2Black(0) and the state this branch was entered
        // with already leave tmnFadeActive at 1 either way, so that
        // comparison can never actually distinguish the two cases (a
        // real bug this port shipped with: since the "no nested
        // re-trigger" guard below always evaluated false, this branch's
        // own postBlackWait/fadeActive=3 assignment never ran, so
        // tmnUpdateFadeSequence() immediately re-entered tmnFadeActive==1
        // on the very next tick and called this function again - and
        // again - decrementing tmnPacLives once per real tick until it
        // hit 0, all within a handful of frames, which looked exactly
        // like "hit one ghost, instantly lose all 3 lives" to a player
        // even though the death/respawn sequence itself was otherwise
        // working correctly). tmnFadeTrigger is the one that actually
        // differs between the two cases (stays 2 normally, becomes 0 the
        // moment a nested Fade2Black(0) runs), so check that instead.
        tmnRestartLevel();
        if( tmnFadeTrigger == 2 )
        {
            tmnPostBlackWaitFrames = 48; // ~800ms
            tmnFadeActive = 3;
        }
    }
    else if( tmnFadeTrigger == 3 )
    {
        tmnNextLevel();
        tmnPostBlackWaitFrames = 60; // ~1000ms
        tmnFadeActive = 3;
    }
    tmnPlayState = TMN_PLAY_NORMAL;
}

void tmnUpdateFadeSequence()
{
    if( tmnFadeActive == 1 )
    {
        if( tmnFadeFrame != 0 ) tmnFadeFrame--;
        else
        {
            tmnIngame = tmnFadeTrigger;
            tmnMenuFadeSelect();
        }
    }
    else if( tmnFadeActive == 3 )
    {
        if( tmnPostBlackWaitFrames > 0 ) tmnPostBlackWaitFrames--;
        else tmnFadeActive = 2;
    }
    else if( tmnFadeActive == 2 )
    {
        if( tmnFadeFrame != 8 ) tmnFadeFrame++;
        else
        {
            tmnIngame = tmnFadeTrigger;
            tmnFadeActive = 0;
        }
    }
}

void tmnUpdatePlaying()
{
    if( tmnPlayState == TMN_PLAY_NORMAL )
    {
        int collision = tmnPacCollision();
        if( collision == 1 )
        {
            tmnPlayState = TMN_PLAY_DEATH_GUB;
            tmnStartSeq( tmnGubFreq_Dur, 16 );
        }
        else if( collision == 2 )
        {
            tmnPlayState = TMN_PLAY_EAT_GUB;
            tmnStartSeq( tmnGubFreq_Dur, 16 );
        }
        else
        {
            tmnMainPlayerUpdate();
            tmnGhostsUpdate();
            tmnRefreshJump();
        }
    }
    else if( tmnPlayState == TMN_PLAY_EAT_GUB )
    {
        if( tmnAdvanceSeq() )
        {
            tmnPlayState = TMN_PLAY_EAT_WAIT;
            tmnPlayWaitFrames = 15; // ~250ms
        }
    }
    else if( tmnPlayState == TMN_PLAY_EAT_WAIT )
    {
        tmnPlayWaitFrames--;
        if( tmnPlayWaitFrames <= 0 )
        {
            tmnScores( 250 );
            tmnPlayState = TMN_PLAY_NORMAL;
        }
    }
    else if( tmnPlayState == TMN_PLAY_DEATH_GUB )
    {
        if( tmnAdvanceSeq() )
        {
            tmnPlayState = TMN_PLAY_DEATH_WAIT1;
            tmnPlayWaitFrames = 60; // ~1000ms
        }
    }
    else if( tmnPlayState == TMN_PLAY_DEATH_WAIT1 )
    {
        tmnPlayWaitFrames--;
        if( tmnPlayWaitFrames <= 0 )
        {
            tmnPlayState = TMN_PLAY_DEATH_SONG;
            tmnStartSeq( tmnDeadSongPairs, 54 );
        }
    }
    else if( tmnPlayState == TMN_PLAY_DEATH_SONG )
    {
        if( tmnAdvanceSeq() )
        {
            tmnPlayState = TMN_PLAY_DEATH_WAIT2;
            tmnPlayWaitFrames = 60; // ~1000ms
        }
    }
    else if( tmnPlayState == TMN_PLAY_DEATH_WAIT2 )
    {
        tmnPlayWaitFrames--;
        if( tmnPlayWaitFrames <= 0 )
          tmnFade2Black( 2 );
    }
    else if( tmnPlayState == TMN_PLAY_LEVELCLEAR_WAIT1 )
    {
        tmnPlayWaitFrames--;
        if( tmnPlayWaitFrames <= 0 )
        {
            tmnPlayState = TMN_PLAY_LEVELCLEAR_FANFARE;
            tmnStartSeq( tmnFanfareNotes, 116 );
        }
    }
    else if( tmnPlayState == TMN_PLAY_LEVELCLEAR_FANFARE )
    {
        if( tmnAdvanceSeq() )
        {
            tmnPlayState = TMN_PLAY_LEVELCLEAR_WAIT2;
            tmnPlayWaitFrames = 30; // ~500ms
        }
    }
    else if( tmnPlayState == TMN_PLAY_LEVELCLEAR_WAIT2 )
    {
        tmnPlayWaitFrames--;
        if( tmnPlayWaitFrames <= 0 )
          tmnFade2Black( 3 );
    }
}

// -----------------------------------------------------------------------------
//   Entry points
// -----------------------------------------------------------------------------

void gameTinyMania_init()
{
    InitTinyJoypad();

    tmnRDC = 0;
    tmnSeqActive = false;
    tmnBgActive = false;
    tmnPlayState = TMN_PLAY_NORMAL;
    tmnHudDirty = true; // belt-and-braces - tmnResetScores() (via tmnInitNewGame() below) already sets this too

    // Matches upstream's own setup() exactly - InitNewGame() runs once
    // here, then is immediately superseded by the fade-in/attract screen
    // below (its result sits unused in memory until the player actually
    // starts a game later, harmless but faithfully reproduced).
    tmnInitNewGame();

    tmnFadeActive = 2;
    tmnFadeFrame = 0;
    tmnFadeTrigger = 0;
    tmnIngame = false;
    tmnAttractFireHeld = isFirePressed();
}

void gameTinyMania_update()
{
    // Whole-tick throttle (30fps instead of the engine's native 60) -
    // requested directly after this project's own CPU-load investigation
    // found `tmnDrawLevel()`/`tmnTinyFlip()` (only reachable from inside
    // this function) to be the dominant remaining cost, with no further
    // safe reduction available without a riskier rewrite (see this
    // file's own header comment). Matches the same whole-function tick-
    // skip shape already established for NumberPlace/HollowSeeker/t2048/
    // Doc/Pacman/Pipe (not the movement-only/render-stays-60fps shape
    // used for Trick/Invaders/Pinball/Bert) - correct here specifically
    // *because* rendering, not movement logic, is the expensive part, so
    // a movement-only throttle would have left the real cost untouched.
    // Skipped ticks simply return without calling `md_beginFrame()` at
    // all - the previous frame's own pixels just persist (Vircon32's
    // screen isn't cleared unless something explicitly clears it), the
    // same "just don't redraw" trick this project's own dirty-flag
    // caching elsewhere already relies on.
    tmnTickSkip++;
    if( tmnTickSkip < TMN_TICK_DIVISOR ) return;
    tmnTickSkip = 0;

    tmnClearVBuffer();
    tmnAdvanceBgSeq();

    if( tmnIngame )
    {
        if( tmnFadeActive == 0 ) tmnUpdatePlaying();
        tmnDrawLevel();
    }
    else
    {
        bool fireDown = isFirePressed();
        if( tmnFadeActive == 0 )
        {
            if( fireDown && !tmnAttractFireHeld )
            {
                tmnAttractFireHeld = true;
                Sound( 100, 255 );
            }
            else if( !fireDown && tmnAttractFireHeld )
            {
                tmnAttractFireHeld = false;
                Sound( 20, 255 );
                tmnFade2Black( 1 );
            }
        }
        tmnIntro();
    }

    tmnGobTimer();
    tmnUpdateFadeSequence();
    tmnTinyFlip();
    tmnAnimTick();
}

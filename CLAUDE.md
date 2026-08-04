# TinyJoypad → Vircon32 Port

## Goal

Port a selection of TinyJoypad games (originally written for the ATtiny85 +
SSD1306 128x64 monochrome OLED) into a single Vircon32 cartridge, presented
behind one shared game-select menu — the same overall shape as
crisp-game-lib-portable's "one binary, many games, addGame() menu" pattern,
and reusing lessons learned from the playdate-arduboy (srcstub) compatibility
layer.

This file is project context for Claude Code. Read it before making changes,
and keep it updated as decisions get made — it should stay a reliable summary
of the architecture and open questions, not just a one-time brief.

## `more games/` — staged source for future ports

`more games/` (repo root) holds unmodified upstream sources for every
TinyJoypad game found on tinyjoypad.com/tinyjoypad_attiny85, fetched so a
future porting pass doesn't need to re-download anything:
- GitHub-hosted (git clones, full history not kept - `--depth 1`):
  `Tiny-invaders-v4.2`, `TinyDungeon`, `TinyMinez` (all hosted under the
  `Lorandil` GitHub account - see the Licensing section for what each
  game's own header actually credits as its programmer, which isn't
  always "Lorandil" itself),
  `TinyLanderV1.0` (tscha70), `TinyJoypadWorks` (Obono - a monorepo: its own
  `hollowseeker/`, `numberplace/`, and `t2048/` subfolders are 3 games in
  one clone).
- Official "Tiny X" games (Google Drive-only, no public GitHub source) -
  downloaded and unzipped directly: `Tiny Arena`, `Tiny Doc`,
  `Tiny SQuest`, `Tiny Pipe`, `Tiny Morpion`, `Tiny Missile`, `Tiny DDug`,
  `Tiny Plaque`, `Tiny Tris`, `Tiny Trick`, `Tiny Bike`, `Tiny Bert`,
  `Tiny Bomber`, `Tiny Arkanoid`, `Tiny Pacman`, `Tiny Pinball`,
  `Tiny Gilbert`, `TESTMODE`.
- Not fetched: the official "Tiny Invaders" Drive download (redundant -
  `Tiny-invaders-v4.2`'s GitHub clone already has full source, the Drive
  link is just a compiled `.hex`) and the 3 "non-joypad-compatible"
  projects the site lists (Simon, VU Meter, Tinyscope - not TinyJoypad
  games, wrong input/display shape to port here).
- `gametiny` (cheungbx, GitHub) - a *different* author's ATtiny85+SSD1306
  console project ("inspired by" TinyJoypad, not affiliated with
  tinyjoypad.com), cloned whole for its **unique** games not found
  anywhere on tinyjoypad.com itself: `BatBonanzaAttinyArcade`,
  `Frogger_Attiny_Arcade`, `SpaceAttackAttiny` (+ a `SpaceAttackAttiny2but`
  variant), `Tetris_Multi_Button`, `UFO_Breakout_Arduino`,
  `UFO_Stacker_Attiny`, `WrenRollercoasterAttinyArcade`. Its other
  subfolders (`tinyarkanoid`, `tinybomber`, `tinygilbert`, `tinypacman`,
  `tinyPinball`, `Tiny_space_invaders`, `MorseAttinyArcade`,
  `Test_ATtiny`) are either duplicates of games already collected above or
  not games (a Morse practice tool, a hardware test sketch) - skip those,
  they add nothing new.
- Deliberately **not** fetched: tinyjoypad.com's separate Arduboy and
  ESP8285 platform pages, which mostly re-list the same games above but
  also include 3 Arduboy-exclusive titles (`Ardumania`, `Nohzdyve`,
  `Gilbert in the Downland`) not ported here - same 128x64 OLED, but
  originally written for Arduboy's beefier hardware (real buttons, more
  RAM/flash) rather than ATtiny85's analog-ladder input and 512B RAM, so
  out of scope for this pass. Revisit only if asked.

Most of the Daniel-C `tinyJoypadShim`-lineage titles and all three Obono
`TinyJoypadWorks` games in this folder are shipped now (22 games total -
see Status below for the full list and per-game writeups); `TinyDungeon`
remains scoped but not started (see its own note in Status - a full C++
raycaster class, deliberately deferred). Before porting anything new from
this folder, triage it the same way CLAUDE.md's own porting plan
describes (discrete-object vs procedural-pixel vs irrelevant-bit-banged-
timing) and check which driver lineage it's built on (tinyJoypadShim vs
obonoCoreShim, or a third one if it doesn't match either).

## Target platform facts (Vircon32) — verified against vircon32.com

- 15 MHz CPU, 32-bit, floating point support, 16 MB RAM
- Screen: 640x360, true color
- **GPU is a texture-region blitter, not a mutable CPU-writable framebuffer.**
  The standard C API (`video.h`) only exposes: `select_texture` /
  `select_region` / `define_region*` to pick a rectangle out of a preloaded
  texture, plus a `draw_region*` family (`draw_region`, `draw_region_at`,
  `draw_region_zoomed[_at]`, `draw_region_rotated[_at]`,
  `draw_region_rotozoomed[_at]`) to blit it to screen with optional
  scale/rotation. There is no `set_pixel()` or documented "upload a raw
  buffer to VRAM every frame" call. Textures are cartridge resources.
- Hardware blending modes: alpha (default), add, subtract
- Audio: 16-channel CD-quality stereo (`audio.h`)
- Input: up to 4 gamepads, 6 buttons + Start (`input.h`)
- ROM is a single file; has a C compiler (asm available for advanced/optimized
  work — see the [[v32opt]] project for prior Vircon32-assembly experience)
- 1 MB memory cards for save data

**Implication:** games that draw discrete objects (sprites, digits, shapes,
tile-based playfields) map cleanly onto region blits and should run
comfortably within the CPU/GPU budget. Games that rely on per-pixel
procedural rendering (plasma effects, simple raycasters, per-pixel noise)
do NOT have a direct home in this model — one `draw_region()` call per pixel
(128x64 = 8192 calls/frame) is not viable. Before ruling those out entirely,
check whether a lower-level runtime texture-write mechanism exists outside
the documented `video.h` (e.g. in the assembly-level GPU commands, Part 4 of
the official spec at vircon32.com/specs.html) — otherwise treat those games
as out of scope or requiring a full rewrite rather than a port.

## Source platform facts (TinyJoypad)

- Games target ATtiny85: 8 KB flash, 512 B RAM, single analog pin decoded as
  a ~5-button voltage-divider ladder, piezo buzzer for sound
- Display: SSD1306 128x64 monochrome OLED over I2C
- Most games are built on the `ssd1306xled` display driver plus a small
  shared utility header (commonly `TinyDriver.h` or `tinyJoypadUtils.h`) that
  handles init and button reads — but this isn't perfectly uniform; several
  community forks patch the driver per author/device, so expect some
  per-game cleanup rather than one drop-in shim covering every game verbatim
- Each game is a single `.ino`, normally owning `setup()`/`loop()` and
  assuming it's the only program on the chip — no namespacing, no menu
  concept, globals and asset arrays given generic names

## Porting plan

1. **Triage the game list first**, before porting anything. Sort into:
   - draws discrete objects per frame → portable, likely straightforward
   - draws pixels procedurally / full-buffer effects → likely out of scope
     (see GPU note above)
   - relies on raw bit-banged I2C or cycle-exact AVR asm for display timing
     → irrelevant on Vircon32 (the GPU handles display output, no bit-banging
     needed), treat as "discard or rewrite from scratch," not "port"
2. **Build one shared compatibility shim first**, proven against 2–3 simple
   games before investing further. It should map the common
   `ssd1306xled`/`TinyDriver` calls (draw pixel/line/rect/bitmap/text, read
   button) onto Vircon32 equivalents:
   - drawing primitives → preloaded texture regions blitted via
     `draw_region_at()` at the right position
   - button ladder reads → real Vircon32 gamepad button reads
   - buzzer tones → `audio.h` channel playback
3. **Per game, once the shim is validated:**
   - rename `setup()`/`loop()` to unique entry points
     (e.g. `game_invaders_init()`, `game_invaders_update()`)
   - prefix/namespace all globals and asset tables to avoid symbol collisions
     when multiple games are linked into one cartridge
   - make sure the game resets cleanly when selected from the menu (Vircon32
     doesn't reboot between menu choices the way separate AVR firmware
     flashes effectively did — GPU/audio state left over from the previous
     game needs to be cleared)
4. **Build the menu/game-select screen last**, after the shim and a couple of
   games are proven — it's the least risky part and validating the core shim
   first avoids rework.

## Open questions — resolved

- **No runtime texture-write path exists beyond `video.h`** (confirmed by
  reading the real header) - but it turned out not to matter. Every
  TinyJoypad driver lineage found (Lorandil/phoenixbozo's
  `tinyJoypadUtils`/`FastTinyDriver`, Obono's `TinyJoypadWorks` core)
  streams the display **one SSD1306 hardware "page" byte at a time**, never
  keeps a full framebuffer (ATtiny85 only has 512B RAM), and never reads
  pixels back. A byte is 8 vertical pixels of one column, so there are only
  256 possible byte values - `tools/gen_column_atlas.py` pre-bakes a
  256-tile texture atlas at build time (one tile per byte value, already
  scaled to final on-screen size), and each `SendPixels(byte)`-equivalent
  call becomes one `select_region(byte); draw_region_at(x, y)` GPU blit
  (skipped entirely when `byte == 0`, since the frame's already
  `clear_screen()`-ed black). Even TinyDungeon's raycaster funnels through
  this same per-byte page stream internally - it's column-buffered, not
  truly per-pixel-procedural. See `src/portVircon32.c`'s `md_drawColumn()`.
- **Presentation**: 128x64 -> 5x scale -> exactly 640x320 (128*5 = 640, no
  remainder), centered in the 640x360 screen with a 20px black bar top and
  bottom.
- **Asset pipeline**: the column atlas above is the *only* image asset the
  whole cartridge needs - `tools/gen_column_atlas.py` generates it
  deterministically (no PROGMEM-bitmap-to-PNG conversion required at all,
  since sprite/font/level data ported from each game stays as plain
  numeric arrays in the compiled C, not texture assets).

## A real bug worth knowing about: word vs. byte truncation

Vircon32 ints are full 32-bit words with **no implicit truncation** the way
AVR's `uint8_t` gave the original code. Upstream draw code that relies on a
shift-then-OR "overflowing" out the top of a byte (common in sprite/font
compositing and in NumberPlace's sub-page digit rendering, since its 7px-tall
board rows don't align to 8px hardware pages) leaves stray high bits set
once every `uint8_t` becomes a plain `int` via `avrCompat.h`'s aliasing.
Symptom: corrupted texture-region lookups (garbled grid lines/digits) since
the stray bits push the "byte" value past 255. **Fixed once, centrally**, in
`md_drawColumn()` (`src/portVircon32.c`) by masking `value &= 0xFF` right
before it's used to select a texture region - every column value from every
game/shim funnels through that one function, so this single fix covers the
whole cartridge rather than needing per-call-site masking.

## Two more real bugs worth knowing about: `rand()` range and shift wraparound

**`rand()` range mismatch.** AVR/Arduino's `rand()` returns a non-negative
~15-bit value (0..32767). Vircon32's `rand()` (`misc.h`) instead returns the
raw 32-bit RNG register directly - any sign, any magnitude. Code ported
verbatim from AVR that does `rand() % n` (or worse, arithmetic assuming a
~16-bit range, like HollowSeeker's original hollow-distance formula
`((rand() + 32768) * score >> 22) + 2`) silently breaks: a negative raw value
modulo n can itself be negative in C, and combined with Vircon32's *logical*
`>>` (no sign extension), shifting a negative value right produces a huge
positive result instead of a small one. Symptom (HollowSeeker): the cave's
"make a gap" branch almost never fired, so the tunnel filled in solid over
time. **Fixed once, centrally**, with a shared `arand(n)` helper in
`avrCompat.h` (clamps to non-negative, then mods) - used by every ported
game's own random helper (NumberPlace's `npRandom`, t2048's `t2048Random`,
Tiny Invaders' monster-shoot RNG, HollowSeeker's cave generation) instead of
raw `rand() % n`.

**Shift-count wraparound.** Confirmed empirically: `(0xFF << 32) & 0xFF`
evaluates to `255` on Vircon32, not `0` - the shift instruction wraps the
shift amount modulo 32 (like x86 hardware), whereas AVR-GCC's codegen for a
*variable*-count shift (a bit-by-bit loop, since AVR has no barrel shifter)
correctly saturates to 0 once the count exceeds the value's width, no
wraparound possible. HollowSeeker's cave-wall edge-highlight patterns
(`edgeTopPtn`/`edgeBottomPtn` in `hsDrawGame()`, `src/games/gameHollowSeeker.c`)
compute a shift amount from wall thickness, which routinely exceeds 8 (walls
are commonly 30-40+ px thick) - upstream this always correctly saturated to
0 ("far from the tunnel edge, no highlight needed"), but on Vircon32 any
shift amount landing in the `[32,39]` window (mod 32 = 0..7) instead produced
a solid or near-solid byte, overpowering the intended sparse crosshatch
texture - the reported "white garbage blocks", worst when both cave walls
were thick/close together since both computations were likely to hit the
wraparound simultaneously. Fixed by explicitly clamping both shift amounts
to the true 8-bit-meaningful range (0-7) rather than trusting the shift
instruction outside it - this is the same class of bug as the byte-
truncation one above (an AVR-implicit-behavior the port can't assume holds),
just via a different mechanism (shift-count wraparound vs. assignment
truncation), so any *other* upstream code relying on "shift past the
register width gives zero" would need the same explicit-clamp treatment -
none currently does (checked every other `<<`/`>>` site in the codebase;
all of them shift by a value already bounded well under 8 by construction).

## A third real bug, same family: signed-sentinel comparison

Tiny Invaders' `MonsterGrid` is `int8_t` upstream, and its "clear the
battlefield" step does `memset(space->MonsterGrid, 0xff, sizeof(...))` -
since raw memset fills bytes, and `int8_t` reads a `0xFF` byte back as
**-1**, this matches the sentinel every monster-grid read in the file
checks for (`< 0`, `!= -1`) to mean "no monster here". The ported
`tinvClearBattleground()` (`src/games/gameTinyInvaders.c`) translated this
as a per-element loop assigning the literal `tinvSpace->MonsterGrid[y][x]
= 0xFF` - but `MonsterGrid` is a plain 32-bit `int` via `avrCompat.h`, so
that literal stores `255`, not `-1`. Every sentinel check in the file
(`spriteType < 0`, `!= -1`) then silently failed to match, so
`tinvMurgeSplitUpDown()` fell through and indexed `tinvMonsters[]` with
`spriteType=255` - wildly out of bounds - reading garbage sprite data.
Symptom: the "LEVEL N" flash screen (`tinvBeginLevelStart()` renders one
frame with a freshly-cleared battleground, *before*
`tinvVarResetNewLevel()` populates real monster data a few frames later)
showed a corrupted, static-like repeating pattern across the monster-grid
rows instead of a blank background. **Fixed** by using the literal `-1`
instead of `0xFF`, matching every other sentinel assignment/check already
in the file. Same root cause as the two bugs above (an AVR narrow-signed-
type behavior the port's `int`-widening can't preserve implicitly) - a
third distinct mechanism (signed-sentinel comparison, vs. truncation or
shift wraparound), so any future port should treat *every* `0xFF`/`0x80`-
style "magic byte" constant as suspect and check whether the original type
was signed before assuming the literal ports over unchanged.

## A fourth bug, same family, found much later via live user play: logical vs. arithmetic right-shift

Found in HollowSeeker, long after it shipped, from a user report: "when
the little guy moves right he is sometimes very briefly drawn on the
bottom of the screen 'over' the cave." `hsUpdatePlayer()`'s player-Y
calculation includes `hsDivByColumnW(hsPlayerJump * hsPlayerMove)`, where
`hsDivByColumnW(val)` was simply `(val) >> 3` (a divide-by-8 shortcut) and
`hsPlayerJump = pPlayerColumn->bottom - pNextColumn->bottom` is genuinely
**negative** whenever the player moves into a column where the cave floor
drops lower on screen. Upstream (AVR-GCC) sign-extends a negative `int`'s
right shift, so `-5 >> 3` correctly gives `-1` there - but
**`VIRCON32_C_DIALECT.md` explicitly documents `>>` as a *logical* (zero-
fill) shift, not arithmetic**, so the same operation on Vircon32 turned a
small negative number into a huge *positive* one instead. That garbage
value fed straight into `playerY`, and the very next line's own clamp
(`if (playerY > HEIGHT-HS_PLAYER_H) playerY = HEIGHT-HS_PLAYER_H;`) -
there to keep the player from ever being drawn below the visible cave -
instead caught the garbage and pinned the sprite to the absolute bottom
of the screen for that one frame, exactly matching the reported symptom.
Same root cause as the shift-wraparound bug above (an AVR-implicit shift
behavior the port can't assume holds) via yet another distinct mechanism
(sign handling on negative operands, vs. shift-*count* wraparound).
**Fixed** by turning `hsDivByColumnW` from a macro into a real function
that branches on `val >= 0` and, for negatives, computes
`-((-val + 7) >> 3)` - mathematically equal to floor-division-by-8 (what
AVR's arithmetic shift produced) while only ever shifting a non-negative
operand, where logical and arithmetic shifts agree. Needed moving the
function above every one of its call sites (`hsGetColumnOfPos()`'s own
macro included) per Vircon32's no-forward-declarations rule. Verified
with an extended Puppeteer soak test - held the right d-pad continuously
across 40 sampled frames (a fresh screenshot every 150ms) through several
floor-height transitions - confirming the player sprite stayed correctly
seated on the cave floor throughout, never flashing to the bottom edge.
**Generalizable takeaway**: any upstream `>>` on a value that can be
negative (not just ones already found via a crash or a visibly garbled
static pattern, per the earlier shift-wraparound bug) is suspect on
Vircon32 - a subtly wrong single-frame visual glitch like this one is
much easier to miss in testing than a hard crash or a persistent
corruption, and can hide in already-shipped code for an entire session
before a live player happens to trigger and report it.

A related, smaller bug found alongside it: the very first playthrough of a
session showed "LEVEL 0" instead of "LEVEL 1". Upstream's `NEWGAME:` label
(reached via `goto` at cold boot *and* on every post-game-over restart)
unconditionally sets `currentLevel = 1` before the intro cycle begins; the
port split that label's reset logic between `gameTinyInvaders_init()`
(cartridge launch) and `tinvStartNewGame()` (restart-after-game-over only),
and only the latter got the correct `= 1` - `gameTinyInvaders_init()` was
left at `= 0`. Fixed by matching upstream's value in both places.

## A fourth bug: a shared "wait complete" dispatcher clobbering its own callee's new state

Not an AVR-type-width issue this time - a genuine control-flow mistake from
restructuring upstream's `goto`-chained state transitions into the port's
explicit `tinvWaitFrames`/`tinvWaitAction` frame-counted wait
(`tinvOnWaitComplete()`, `src/games/gameTinyInvaders.c`). Its
`TINV_WAIT_AFTER_LEVEL_CLEARED` branch calls `tinvBeginLevelStart()`, which
sets a *brand new* `tinvWaitAction = TINV_WAIT_AFTER_LEVEL_START` (plus a
fresh 60-frame countdown) so gameplay can resume once the "LEVEL N" flash's
own wait finishes - but `tinvOnWaitComplete()` had an unconditional
`tinvWaitAction = TINV_WAIT_NONE;` *after* its if/else-if chain, which ran
regardless of which branch fired, clobbering that fresh value back to NONE
immediately. So every time the level-start countdown completed,
`tinvOnWaitComplete()` ran again with `tinvWaitAction` already NONE -
matching no branch - and the game never reached `tinvBeginPlaying()`.
Symptom (reported by the user): clear a level, see the correct "LEVEL N"
flash, then the game freezes permanently - no monsters, no ship response to
input - because `TINV_STATE_LEVEL_START` has no per-frame dispatch branch
of its own in `gameTinyInvaders_update()` (by design, since it's normally
just a fixed wait), so once stuck there with `tinvWaitFrames` also stuck at
0, nothing ever runs again. **Fixed** by moving the reset from the end of
the function to the start (into a local `completedAction` snapshot used for
the branching), so a branch's own callee can set a new wait action that
actually persists. Diagnosed by reproducing the freeze with a temporarily-
edited level layout (2 monsters instead of 24, reverted after) plus a
temporary on-screen readout of `tinvWaitFrames`/`tinvWaitAction`/`tinvState`
forced to redraw every frame during the stuck wait (normally nothing
redraws during a plain wait, so the freeze itself made the bug invisible
without forcing a redraw) - both removed again after the fix was confirmed.

## A fifth bug: a highscore poll that can be permanently starved of its last update

Also not an AVR-type-width issue - a genuine pre-existing quirk confirmed
present in *upstream too* (checked `Tiny_Flip()`'s own `newHighScore |=
updateHighScorePoints();` call), just one this port chose to fix rather
than preserve. `tinvUpdateHighScore()` (checks `tinvScore > tinvHighScore`,
updates if so) is called once per frame, early inside `tinvTinyFlip()` -
*before* that same frame's own scoring happens, since scoring
(`tinvAddScore()`, via `tinvMonsterAttackCheck()`/`tinvUFOAttackCheck()`)
is triggered from `tinvMyShoot()` deep in the per-pixel render loop that
runs later in the same call. This gives every score change a full extra
frame to be reflected in `tinvHighScore` - invisible during normal play,
since there's always another `tinvTinyFlip()` call moments later - *unless*
the very last scoring kill of a life lands on the exact frame the ship's
death is finalized: the frame after that stops calling `tinvTinyFlip()` at
all for ~36 frames (see `tinvUpdatePlaying()`'s `tinvShipDead` handling), so
the lagging poll never gets another chance to catch up before
`tinvBeginGameOverDisplay()` reads `tinvHighScore` for the "NEW HISCORE!"
screen. Symptom (reported by the user): a clearly-visible live score (e.g.
60) but the "NEW HISCORE!" screen showing a stale, lower value (e.g. 30 -
exactly the score from before that last kill). **Fixed** by syncing
`tinvHighScore`/`tinvNewHighScore` immediately inside `tinvAddScore()`
itself (the point score actually changes), rather than relying solely on
the once-per-frame poll elsewhere - `tinvUpdateHighScore()` is left in
place as a harmless no-op fallback. Confirmed both the bug and the fix with
a temporary side-by-side on-screen readout of `tinvScore` next to
`tinvHighScore` on the game-over screen itself (removed after).

## A sixth bug, a different family entirely: the PlayNote wavetable's own hardware speed clamp flattening every note in Tiny Arkanoid's intro tune to one pitch

Found via a direct user report comparing this build against its own
sibling ports: Tiny Arkanoid's own well-known intro jingle (a real,
recognizable classic-Arkanoid melody, see `arkStartNoteSeq`/
`arkAdvanceNoteSeq` in `src/games/gameTinyArkanoid.c`) was completely
unrecognizable here, while playing correctly on this project's own SDL3
and Playdate sibling ports. Since `gameTinyArkanoid.c` itself is
byte-identical across all three ports (confirmed directly - only the
mechanical dialect differences, e.g. `int[N] name` vs `int name[N]`,
differ at all), the bug had to be in this build's own platform-specific
audio backend, not the shared game logic - not the same family as the
five bugs above (all of which stem from an AVR-implicit-narrow-type
behavior the Vircon32 port's own `int`-widening can't preserve
automatically). This one is a genuine Vircon32-*hardware* constraint the
port's own `libs/PlayNote` wavetable-playback library didn't account for.

**Root cause**: `set_channel_speed()`'s real hardware register is clamped
to `[0, 128]` (confirmed directly in the emulator's own source,
`V32SPUWriters.cpp`'s `WriteSPUChannelSpeed`: `Clamp(Value.AsFloat, 0,
128)`) - a request for a higher effective playback speed than that is
silently clamped to exactly 128, not rejected or reported. PlayNote's own
`playnote_start()` computes that speed as `freq_hz * wave_period_samples
/ 44100.0`, so with `WAVETABLE_PERIOD_SAMPLES` originally `2048` (a
wavetable with a ~21.5Hz natural pitch at speed 1.0), the *maximum
representable frequency before hitting that clamp* was only `128 * 44100
/ 2048 = 2756.25Hz`. Every one of Tiny Arkanoid's own intro-tune notes
(5 distinct pitches, freq bytes 105/125/135/140/145, which `Sound()`'s
own `500000/(255-freq)` formula turns into 3333-4545Hz) sat comfortably
*above* that ceiling - so every single one got clamped to the exact same
2756.25Hz, regardless of its own intended pitch. The tune's rhythm/timing
survived completely intact (that's driven by the shared, platform-
agnostic sequencer code, `arkNoteWaitFrames`) - only the pitch variation
that makes it recognizable as a *melody* was gone, flattened to one
monotone note repeated in rhythm. This is also why a single hit/blip
sound effect in some other game sounding "a bit off" was never reported
or noticed the same way a famous melody losing 100% of its pitch
variation immediately was - a lone SFX's exact pitch isn't memorable the
way a well-known tune's relative note-to-note shape is.

Ruled out before landing on this explanation: whether this build's own
`md_playTone()` needed the same "+1 frame" minimum-tone-duration fix its
SDL3 sibling has (that fix exists there because `gFrameCounter` is a
plain software int incremented as the first line of that build's own
`md_updateAudio()`, so a 1-frame note can get started and expired within
the same real frame before ever really sounding) - traced through this
build's own real hardware timer instead (`get_frame_counter()` reads
`TIM_FrameCounter`, incremented once per real frame by
`V32Console::RunNextFrame()`'s own `Timer.ChangeFrame()` call, which runs
*before* that frame's CPU cycle budget executes) and confirmed a note
here already gets a full, correctly-timed real frame of playback with no
equivalent race - so blindly porting that same "+1" over would have
been an unverified, likely-wrong change (an extra frame of ring-on,
overlapping the next note) chasing a bug that doesn't actually exist on
this platform. Also checked and ruled out: the sibling `tinyjoypad_SDL3`
repo's own copy of `gameTinyArkanoid.c` (confirmed byte-identical, no
data/logic drift between the two projects' independently-maintained
copies) and this build's own documented 250,000-cycle/frame budget
(directly confirmed via live measurement that CPU usage during the intro
scene never approaches 100%, ruling out frame truncation as a cause).

**Fixed** by regenerating `libs/PlayNote/sounds/wt_saw.wav` at a shorter
256-sample period (down from 2048) - preserving the exact same linear-
ramp sawtooth shape and amplitude range (-9000..8991, matching the
original file byte-for-byte in every way except the shorter cycle) and
the same `period+1`-samples-with-a-repeated-first-sample loop-closing
convention the original already used - and updating
`WAVETABLE_PERIOD_SAMPLES` (`portVircon32.c`) to match. This raises the
wavetable's natural pitch to `44100/256 ≈ 172.3Hz` and, with it, the
speed-clamp ceiling to `128 * 44100 / 256 = 22050Hz` - comfortably above
not just Tiny Arkanoid's own tune (now computing to speeds of 19.35-26.39,
nowhere near the 128 ceiling) but also the most common frequency actually
used elsewhere in this project's own 33 games (a direct grep across every
literal `Sound()` call in `src/games/*.c` found freq byte 200 - resolving
to ~9.1kHz - used 27 times, the single most common non-trivial value in
the whole codebase). Verified: recomputed every one of Tiny Arkanoid's
own 5 note frequencies against the new period and confirmed none any
longer exceed the clamp; rebuilt the full ROM via `Make.sh` (including
the `wav2vircon` asset-conversion step picking up the regenerated `.wav`)
and confirmed a clean `BUILD SUCCESSFUL`; user-confirmed by ear afterward
- the intro tune now plays correctly. Also directly confirmed this fix
carries no CPU cost: `playnote_start()`'s only wavetable-period-dependent
work is a single float multiply (`freq_hz * wave_period_samples /
44100.0`), the same cost for any constant value, and the actual sample
playback/mixing (`V32SPU::UpdateOutputBuffer()`'s own `Position +=
Speed`) runs in the emulator's own audio-hardware-emulation pass,
entirely outside `Constants::CyclesPerFrame` - the same way a real SPU
chip generates audio without consuming the CPU's own instruction budget.
If anything this is a small net positive: the wavetable asset itself
shrank to about 1/8th its previous size (`obj/wt_saw.vsnd`: 1040 bytes).

**Generalizable takeaway**: this is a *sixth*, structurally distinct
mechanism from the five AVR-narrow-type bugs above, worth remembering
alongside them - a real hardware/API constraint (a documented register
clamp) interacting with a *deliberately chosen asset parameter*
(`WAVETABLE_PERIOD_SAMPLES`) rather than with anything in the ported game
code itself. Any future game whose own `Sound()`/`md_playTone()` calls
lean on frequencies noticeably above the current ~22kHz ceiling (unlikely
in practice - see the freq=200/220/240 survey above, all comfortably
covered or, for the rarest/most extreme values, already above normal
human hearing anyway) would be worth rechecking with the same "compute
every distinct note's own `speed` value, check it against the clamp"
method used here, rather than assuming a melody sounding "off" is a
group-of-five-bugs-style narrow-type issue by default.

## A pixel-grid presentation overlay, and a genuine "toggle doesn't visually take effect" bug found while verifying it

Ported (not a straight port, see below) from the sibling `tinyjoypad_SDL3`
project's own trio of presentation effects (glow/CRT-scanlines/pixel-grid)
- deliberately **only** the pixel-grid one: the other two need real per-
frame recomputation (glow's own downscale-then-blur, CRT's own scrolling
scanline position) to stay correct without accumulating onto a persistent
buffer, which has no equivalent design here, whereas the pixel grid is a
plain, unchanging pattern - a thin 1px black outline around every one of
the original 128x64 OLED pixels, so each reads as its own distinct cell
once scaled up 5x instead of blending into a smooth block.

**Implementation, chosen specifically because Vircon32's own screen size
is fixed** (unlike a resizable SDL window, which is why the SDL3 sibling
draws its own version as 194 individual filled rects, one per grid line):
one pre-baked 640x320 texture (`assets/pixelgrid.png`, generated by tiling
a single 5x5 corner-line tile across the whole game-area size - a
transparent background with opaque black pixels only at each cell's own
top/left edge) blitted with a single `draw_region_at()` call, relying on
`blending_alpha` (confirmed the emulator's own default mode, and confirmed
directly in `png2vircon.cpp`'s own source that a source PNG's real alpha
channel survives conversion rather than being silently flattened to
opaque) so only the opaque grid-line pixels actually affect the screen -
the transparent majority of the texture is a no-op over whatever the game
already drew. A new `PIXELGRID_TEXTURE_ID` (3rd texture, alongside the
column atlas and the two thumbnail atlases) plus a matching `rom.xml`/
`Make.sh`/`Make.bat` entry were needed for the new asset.

Toggled by Button X (previously unused by this project - only A/Fire, B/
Fire2, and Start were read anywhere), edge-detected the same way every
other button-press-driven state change in this file already is. Off by
default, and only ever processed/rendered while a game is actually
running (`currentGameIndex != -1 && !confirmingQuit`) - never on the menu,
matching the SDL sibling's own `!isInMenu`-equivalent gating, and not
drawn on top of the quit-confirmation dialog either (a deliberate design
choice: the dialog is meant to read as a clean modal, not have grid lines
cut across it).

**A real bug found immediately during verification, not assumed away**:
toggling the grid ON worked first try (visually confirmed via a Puppeteer
screenshot of 2048's own title screen), but toggling it back OFF appeared
to silently do nothing - the grid just stayed on screen indefinitely
afterward, confirmed reproducible across several different timing/hold-
duration attempts before concluding it wasn't a test-timing artifact.
Root cause, once actually reasoned through rather than kept blaming the
test harness: the grid's own black lines are drawn directly into
Vircon32's **persistent** GPU display buffer, permanently overwriting
whatever game pixels used to be there - flipping `pixelGridEnabled` back
to false only stops the *next* frame's own `drawPixelGridOverlay()` call
from running, it does nothing to actually erase the lines already baked
into the picture. The only thing that ever repaints those exact pixels
is the game's own next genuine full redraw - but 2048 (obonoCoreShim
lineage) skips its own redraw entirely on frames where nothing changed
internally (the same `isInvalid`-gated skip this project's own quit-
confirmation dialog already had to work around for an identical reason),
so on its static title/attract screen, nothing was ever naturally going
to trigger that redraw again. **Fixed** the exact same way the quit-
dialog's own resume path already does: call
`menu_getGame(currentGameIndex)->onResume()` (if non-NULL) the instant
the toggle actually changes state, in *either* direction - forcing one
fresh full redraw that frame regardless of whether the game itself
thought anything needed repainting. Confirmed via Puppeteer, using the
WebGL build: toggling ON then OFF now cleanly restores the exact original
title-screen picture, no grid lines left behind.

## A seventh bug, same family as the sixth: Tiny Pacman's own sound effects all collapsing to their very last tone

Found from a direct user report ("on a video on youtube I noticed there's
a little pacman tune playing on game start... it seems to be missing in
our port"), then, once that first fix surfaced the pattern, found again
independently in three more places in the same file via a follow-up
question ("what about the sound when pacman takes a pill to catch
ghosts"). All five instances share one root cause: `gameTinyPacman.c`
fired every one of upstream's `Sound(freq,dur)` calls for a given effect
back-to-back, in a single synchronous burst, with no real time elapsing
between them. Upstream's own `Sound()` genuinely blocks (a real bit-bang,
so N calls take N times as long in real wall-clock time and are each
audible in turn); this engine's `md_playTone()` has no such queue - each
call immediately replaces whatever tone is currently sounding, so a burst
of N calls issued with zero elapsed real time between them is only ever
*audible* as the very last one. Same root cause and same fix shape as the
sixth bug above and every other oversized/multi-call `Sound()` burst
already found and fixed elsewhere in this project (Tiny Arkanoid, Tiny
Missile, Tiny Arena, Tiny Gilbert, Tiny SQuest, Tiny Plaque, Tiny Pipe) -
just the first time this exact bug turned out to affect *five separate
cues in one file* rather than a single jingle.

**1) Start-of-game jingle** (`pacMusic[141]`, a real 70-note table,
triggered once when a fresh game begins). Two independent bugs stacked
here: the sequencing bug above (70 calls in one burst, collapsing to the
last note), *and* the pitch/duration conversion formula already baked
into the port before this session (`pacMusic[t]-8` for frequency,
`(pacMusic[t+1]-100)/1000.0` for duration) was itself wrong, unrelated to
the sequencing issue - it computed pitches in the 100-170Hz range (a low
bass rumble) and a flat ~155ms per note regardless of the table's real
values, nothing like upstream's actual melody. Found only because the
first fix attempt (preserving that formula, just fixing the sequencing)
produced an implausible ~10.85s total duration - checking upstream's real
`Sound(uint8_t freq,uint8_t dur)` bit-bang implementation directly
(`tinypacman.ino:401-408`) gave the correct formula instead: each `Sound`
call is `dur` full HIGH/LOW cycles, each half-cycle `(255-freq)`
microseconds, i.e. `freqHz = 500000/(255-freqByte)` and
`durationSeconds = durByte*2*(255-freqByte)/1e6` - the same formula
`tinyJoypadShim.c`'s own shared `Sound()` already uses elsewhere in this
project. Recomputed with the correct formula: ~4.4 real seconds, pitches
2778-5882Hz, a plausible chiptune melody range. **Fixed** with a new
`PAC_STATE_MUSIC_WAIT` state (`pacMusicIndex`/`pacMusicNoteWaitFrames`) -
one note per real logic tick, freezing the just-initialized Pac-Man/ghost
scene on screen for the tune's own real duration, the same frame-stepped-
sequencer pattern Tiny Arkanoid's own `arkStartNoteSeq`/
`arkAdvanceNoteSeq` already established. Deployed and user-confirmed
correct by ear afterward (a stale browser-cached ROM briefly caused a
false "plays way too fast" report mid-session - not a real bug, resolved
by re-testing against the actual current build).

**2) Ghost-eaten cue** (`Sound(20,100);Sound(2,100);`, fired when
Pac-Man touches a vulnerable ghost during power-pellet mode) and **3) dot-
eaten "waka" cue** (`Sound(10,10);Sound(50,10);`, fired on every normal
dot) - both just two-call bursts, same collapse. Both call sites were
also using raw byte values directly as literal Hz/seconds (e.g.
`md_playTone(20.0, 0.1)` for a freq *byte* of 20) rather than the real
formula - fixed with correctly-derived real values (ghost: 2127.7Hz/
0.047s then 1976.3Hz/0.051s; dot: 2040.8Hz/0.005s then 2439.0Hz/0.004s).
Unlike the three jingles below, these don't need to freeze gameplay -
added a small standalone, reusable two-note player (`pacStartSfx2()`/
`pacAdvanceSfx()`, `pacSfxFreq`/`pacSfxDur`/`pacSfxLen`/`pacSfxPos`/
`pacSfxWaitFrames`) advanced once per logic tick alongside normal play,
declared ahead of `pacCollisionPac2Caracter()`/`pacDotsWrite()` (its two
call sites) since this dialect requires definition before use - both are
earlier in the file than every other `PAC_STATE_*` machinery, which lives
down in `gameTinyPacman_update()` itself.

**4) Death jingle** (`Sound(100,200);Sound(75,200);Sound(50,200);
Sound(25,200);Sound(12,200);` then a real `delay(400)`, fired on losing a
life or ending the game) - a 5-call burst, same collapse, plus the same
raw-byte-as-literal-Hz mistake as the two cues above. **Fixed** with a new
`PAC_STATE_DEATH_SWEEP` state (`pacDeathNoteIndex`/`pacDeathWaitFrames`),
transitioning into the pre-existing `PAC_STATE_DEATH_WAIT` once all 5
notes finish - and `PAC_STATE_DEATH_WAIT`'s own existing
`pacWaitFrames = PAC_FPS*2/5` (12 ticks = exactly 0.4s at this file's
30-tick/sec rate) turned out to already be a correct port of upstream's
own trailing `delay(400)`, not (as it looked in isolation before this
fix) an undersized stand-in for the whole jingle - restructuring the
trigger to play the jingle *then* fall into that existing wait reproduces
upstream's real ~0.805s total pause (jingle + delay) exactly, using code
that was already there.

**5) Level-clear sweep** (`for(r=0;r<60;r++){Sound(2+r,10);
Sound(255-r,20);}`, fired once every dot in a level is gone, then a real
`delay(1000)`) - the largest burst of the five, 120 individual calls.
Reproducing all 120 one-per-logic-tick (this file's 30 ticks/sec) would
take a full 4 real seconds, far longer than upstream's own genuinely fast
bit-banged sweep (~0.3s) - downsampled the loop's own step size instead
(stride 4, 15 steps instead of 60, `PAC_LEVELCLEAR_SWEEP_STRIDE`/
`PAC_LEVELCLEAR_SWEEP_STEPS`), matching the established fix for every
other oversized computed sweep in this project (Tiny Missile/Arena/
Gilbert/Pipe) rather than trying to play all 120 real notes. **Fixed**
with a new `PAC_STATE_LEVELCLEAR_SWEEP` state
(`pacSweepStepIndex`/`pacSweepSubNote`/`pacSweepWaitFrames`), transitioning
into the pre-existing `PAC_STATE_LEVELCLEAR_WAIT` once done (matching
upstream's own trailing `delay(1000)` the same way the death jingle's fix
does above).

Verified via Puppeteer on the rebuilt WebGL ROM: menu thumbnail/selection
still correct, launching into Tiny Pacman still shows the frozen maze
scene during the (now audibly longer, correctly-paced) start jingle, and
active movement/dot-eating afterward renders correctly with ghosts
leaving the box and dots disappearing along Pac-Man's path - confirming
the restructured control flow (2 new SFX-player globals moved earlier in
the file, ahead of their first call sites, plus 2 new `PAC_STATE_*`
states) didn't break or freeze normal gameplay. The death jingle and
level-clear sweep states themselves were not independently triggered in
this session's own testing (would need reaching an actual death or
clearing a full board) - both are structurally identical to the already-
verified `PAC_STATE_MUSIC_WAIT` pattern, just with different trigger data,
so risk is low, but worth a direct check if anything sounds off.

## An eighth bug, same family as the sixth/seventh: the "burst collapses to one tone" bug found in ten more games via a project-wide audit

After fixing Tiny Pacman's five sound bugs above, a direct user question
("what about the sound when pacman takes a pill to catch ghosts") led to
finding one more instance in that same file, which prompted a project-
wide audit rather than continuing to fix these one report at a time.
Grepped every game file for `md_playTone(...)` called directly (bypassing
`tinyJoypadShim.c`'s `Sound()` wrapper) and separately for `Sound(...)`
calls appearing more than once on the same source line or inside a `for`
loop - both strong signals of the same bug shape, since a burst of N
calls fired with zero real time between them is only ever audible as the
very last one (`md_playTone()`, and therefore `Sound()` which calls into
it, has no queue - confirmed directly by reading `portVircon32.c`'s own
implementation: it unconditionally calls `playnote_stop_all()` before
starting any new tone, discarding whatever was previously playing,
regardless of the underlying PlayNote library's own real 16-channel
capability, which this wrapper deliberately doesn't use).

**Ten more games had this bug, all fixed the same way** (a small non-
blocking multi-note sequencer, one note per real/logic tick, matching the
`PAC_STATE_MUSIC_WAIT` shape above - a few used a fixed jingle needing its
own freezing state, most used a lightweight "advance once per tick
regardless of state" player since they don't need to freeze gameplay):

- **Tiny Bomber** - three real bugs, plus two bonus finds from reading
  upstream directly rather than trusting the port's own prior "simplified"
  comment: (1) the death sound (two call sites - enemy collision and
  bomb self-damage - both firing the same `md_playTone(200,0.3);
  md_playTone(100,0.3);` pair synchronously) fixed with a small
  `bomStartSfx2()`/`bomAdvanceSfx()` non-blocking player; (2) the 22-note
  start-game jingle (`bomMusic[]`, a direct byte-for-byte match to
  upstream's own `Music[]` table) fixed with a new `BOM_STATE_MUSIC_WAIT`
  state, freezing gameplay the same way Pacman's own start jingle does;
  (3) while deriving this state's own note-index bound, found that
  upstream's own loop (`for(t=0;t<=42;t=t+2)`) reads one pair *past* the
  end of its 42-element `Music[]` table - harmless on real AVR/PROGMEM,
  a genuine out-of-bounds global read here - capped at the real 21 valid
  pairs instead of reproducing the stray read; (4) upstream's own
  game-over buzzer (`for(t=0;t<5;t++){Sound(100,100);Sound(1,100);}`,
  fired from *both* death paths once lives reach 0) had no port
  equivalent at all - not even a collapsed one, just silence - restored
  via the same small SFX player, extended to a 10-note buzzer.
- **Tiny Invaders** - four bugs: the level-cleared fanfare (5 tones, the
  trailing duplicate `Sound(60,255)` deduped, matching upstream's own
  effective 5 distinct pitches), a "crawling"/ship-dying tick sound that
  fires every real frame while active (`Sound(80,1);Sound(100,1);` each
  time) - fixed by alternating a single tone by parity instead of firing
  both every frame, the same technique this file's own monster-march step
  sound already uses for an identical shape, rather than a multi-tick
  sequence that would never finish before being retriggered - the
  UFO-destroyed sweep (`for(x=1;x<100;x++){Sound(x,1);}`, 99 real notes,
  downsampled to 15 via stride 7), and `tinvBebeep()` (a 2-tone confirm
  cue, fired once when Fire is pressed on the attract/intro screen).
- **Tiny Pinball** - five bugs: `tpFalseBall()`'s 50-note descending sweep
  (downsampled to 13, stride 4), the bonus-ball-slot's 9-note ascending
  sweep (kept in full, already modest), the bumper "bounce push" 3-tone
  cue, the title screen's own already-"approximated" 5-tone sweep
  (upstream's real double 255-step sweep, 510 calls total, already
  reduced to 5 representative tones by an earlier session - that
  reduction was sound, but the 5 tones still fired as one synchronous
  burst, so only the last was ever audible even with the "approximation"
  already applied), and the game-over buzzer (matching Bomber's own
  `for(t=0;t<5;t++){Sound(100,100);Sound(1,100);}` shape exactly, same
  fix).
- **Tiny Doc, Tiny Trick, Tiny Tris** - each has a central `snd`-number
  sound dispatch function (`tdSndTdoc()`/`trkSndTrk()`/`trisSndTtris()`)
  whose own header comment already said "simplified - see header comment"
  or "simplified long tone-loops" - a deliberate design choice made
  *before* this project had established the frame-stepped-sequencer
  pattern (first used for Tiny Arkanoid), reducing upstream's own long
  computed sweeps to a "handful of representative tones." That reasoning
  assumed a short burst would still sound like several distinct tones -
  wrong on this engine, since the call *count* doesn't matter, only
  whether there's more than one with no gap between them. 4 of Doc's 7
  branches, 7 of Trick's 8, and 5 of Tris's 6 were multi-tone bursts,
  fixed with a small per-file byte-pair sequencer (`Sound(freqByte,
  durByte)` called directly, no formula re-derivation needed since
  `Sound()` itself already converts). Doc/Trick's fixes needed to match
  each file's own real tick rate for wait-frame math (Doc ticks at 30/sec
  via `TD_TICK_DIVISOR`; Trick and Tris have no whole-function divisor,
  ticking at the real 60fps).
- **Tiny Morpion** - two bugs, both fixed by adding a 4th mode (a fixed
  2-note cue) to the file's own pre-existing generic note-runner
  (`tmorpionAdvanceNote()`, modes 0-2 already existed for other cues):
  the blink-winner cue (`Sound(140,10);Sound(220,4);`, fired repeatedly
  during the win-blink animation) and `tmorpionSoundStart()`'s own
  session-start cue (`Sound(100,250);Sound(20,250);`, on two separate
  lines - missed by the initial same-line grep pass, found only once
  every `Sound(` call in the file was read individually). The
  blink-winner site needed its own explicit `tmorpionAdvanceNote()` call
  added too - that branch `return`s before ever reaching the shared call
  used by the normal-play/endgame-sweep states, so a queued note there
  would never have actually advanced without it.
- **Tiny Pipe** - three bugs (SND_TPIPE(0)'s 5-note confirm chime, a
  kill-sprite 2-tone cue, and a 6-note bonus-life jingle), fixed with a
  new small byte-pair sequencer separate from the file's own existing
  500-call-sweep-specific one. The confirm chime's own code comment
  claimed it was "harmless as a synchronous burst (only 5 calls)" -
  itself a real, now-corrected misunderstanding: call *count* was never
  the determining factor, only whether there's more than one call with no
  gap between them, so even 2 calls collapse exactly the same way 500
  would. The bonus-life jingle is the most audible case in this whole
  audit: its last call is literally `Sound(0,255)` (upstream's own
  deliberate silent "rest," alternating with `Sound(200,255)` for a
  beep-beep-beep pattern) - meaning the *entire* jingle was previously
  playing as complete silence, not just a wrong tone.
- **Tiny Bert** - one bug, `bertDeadSound()`'s own
  `for(s=200;s>100;s--) Sound(s,10);` (100 real notes), downsampled to 13
  via stride 8, same shape as Pinball's `tpFalseBall()` fix.
- **Tiny Bike** - two bugs (`bikAddLive()`'s 3-tone bonus-life burst, and
  a 2-tone race-start cue), both routed through this file's own existing
  `bikStartNoteSeq()`/`bikAdvanceNoteSeq()` sequencer (already built for
  the intro/win jingles) rather than a new one. Doing this exposed a real
  gap in that existing mechanism: `bikAdvanceNoteSeq()` was previously
  only ever called from two specific states (`BIK_STATE_START_LINE`/
  `BIK_STATE_LEVEL_WIN_WAIT`), so a sequence triggered from `bikAddLive()`
  (mid-gameplay) or the race-start cue (triggered from `BIK_STATE_ATTRACT`,
  which passes through two *other* states before ever reaching
  `START_LINE`) would never actually have advanced. Fixed by moving the
  advance call to run unconditionally once per real frame at the top of
  `gameTinyBike_update()`, with the two original call sites changed to
  check `!bikNoteSeqActive` (the same information their old return value
  gave them) instead of calling it a second time - avoids double-advancing
  the sequencer during the one state that used to call it directly.

**A related, deliberate revert, done on direct user request rather than
found as a new bug**: Tiny Missile's dome-explosion sound
(`gameTinyMissile.c`, inside its explosion-animation loop) previously
fired `SNDBOX`-equivalent sound only on the tick an explosion begins
(`tmisDome[t].frame==1`), rather than upstream's own literal "every one
of the 6 explosion-animation ticks" - a deliberate fix from earlier this
session for a real reported "stuck buzz" (retriggering a multi-tick
sequence every animation tick, rather than upstream's own near-
instantaneous blocking beep, produced an audible stutter). Reverted back
to upstream's literal per-tick call at the user's explicit request,
accepting the risk that the original "stuck buzz" symptom may return -
flagged clearly before making the change, user confirmed "yes fix it"
anyway.

Verified via Puppeteer: all ten fixed files compile clean individually
(rebuilt and confirmed `BUILD SUCCESSFUL` after every single game's
fix, not just once at the end) and Tiny Bomber - the game with the most
invasive changes (a new frozen jingle state plus the two-site death-sound
fix) - was screenshot-tested through menu selection, launch, the frozen
start-jingle scene, and post-jingle gameplay (movement + bomb placement),
confirming no crash/freeze/corruption from the restructuring. The other
nine games were verified via a clean individual rebuild only, not
independently screenshot-tested this session - each fix is mechanically
consistent with the same pattern proven working in Bomber and in Pacman's
own earlier, separately-verified fix, but worth a direct play-test if
anything sounds off.

## A project-wide audit for entirely missing (not just collapsed) sound cues

A direct follow-up question ("also check for missing musics") after the
burst-collapse audit above prompted a *second*, separate kind of check -
not "does this event's sound play correctly," but "does this event have
*any* sound call in the port at all." Dispatched 4 parallel background
agents, each covering ~8 games, comparing every upstream `Sound()`/
`TinySound()`/`beep()` call site (including companion C++ class files,
not just the main `.ino`) against the port's own `.c` file for a matching
event. 29 of 33 games came back completely clean (either full coverage or
upstream genuinely has no sound to begin with - confirmed for Tiny
Dungeon via its own `NO_SOUND` build guard, and Four in a Row/Dino Game
via a direct grep finding no functional sound code anywhere in their
upstream source, just a vestigial unused pin definition in Dino Game's
case).

**Four genuine omissions found and fixed**, all via the same session's
own established small-sequencer patterns:

- **Tiny Bomber** - the level-start jingle (upstream's `NEWLEVEL:` label:
  `for(t=0;t<=4;t++){Sound(80,100);delay(300);}`, 5 identical beeps with a
  real 300ms gap between each) had no port equivalent at all - not the
  start-of-*game* jingle already fixed above (a different event, the
  22-note `bomMusic[]` table played once when `bomInGame` first goes
  active), but a *separate* cue firing on every level transition.
  Reproduced with a new `bomStartLevelJingle()` (extending the file's
  existing `bomSfxFreq`/`bomSfxDur` player with a new parallel
  `bomSfxExtraFrames[]` array - the real `delay(300)` gap after each
  beep's own tone finishes, 0 for this file's other cues which have no
  such gap upstream) called from `bomBeginNewLevel()`, matching upstream's
  own `if(Level>-1)` guard exactly (skips the very first NEWGAME->level-0
  transition, plays on every one after).
- **Tiny Tris** - `trisSndTtris(4)` (the new-game confirm chime,
  `Sound(20,150);Sound(100,150);`) was already correctly implemented as a
  dispatch case (from this session's own earlier burst-collapse fix pass)
  but was never actually *called* anywhere - upstream fires it right when
  the player presses Fire on the attract screen
  (`INTRO_MANIFEST_TTRIS()`), a call site the port's own
  `TRIS_STATE_ATTRACT` handler never had. Fixed by adding the one missing
  call.
- **Tiny Missile** - `Destroy_TMISSILE()`'s own descending 4-note tone
  (`TinySound(Sn_=Sn_-45,4)`, fired once per missile slot scanned -
  `TMIS_NUM_MISSILE`(4) times per call, *outside* the active/hit check, so
  it always plays all 4 notes regardless of which missiles were actually
  live) had no port equivalent in `tmisDestroy()` at all. This is also a
  genuine multi-call burst in its own right (4 synchronous calls, same
  family as every bug in the section above) - fixed with a new
  `tmisDestroyNotes[8]` table (210/165/120/75, each dur 4, matching
  `Sn_`'s own arithmetic) routed through this file's own existing
  `tmisStartNoteSeq()`/`tmisAdvanceNoteSeq()` sequencer.
- **UFO** - the thruster hum while climbing (`beep(1,random(0,i*2))`,
  called up to 6 times per real tick inside a 3x2 nested loop while
  actively flying up with room left to climb) had no port equivalent.
  Approximated as a single representative `ufoBeepOnce(1, arand(4))` per
  tick instead of the full nested loop - both to avoid reintroducing the
  same burst-collapse issue this session already fixed elsewhere (6 near-
  identical short blips colliding into one), and because the original's
  own randomized sub-frame durations have no meaningfully different
  audible result at 6x redundancy.

Verified via Puppeteer after rebuilding: the ROM boots cleanly, the full
33-game alphabetized menu renders correctly across all 4 pages, and both
Tiny Bomber (already screenshot-verified above through the frozen
start-jingle scene and post-jingle gameplay) and UFO (launched cleanly
into its own attract screen, held Up to exercise the new thruster-hum
code path) show no crash, freeze, or visual corruption from any of the
four fixes. The other two games (Tris/Missile) were verified via a clean
individual rebuild only, not independently screenshot-tested this
session - both changes are small, mechanically consistent additions
(one new function call; one new note table wired into an already-proven
sequencer) rather than structural changes, so risk is low, but worth a
direct check if anything sounds off.

## `md_playTone()` became genuinely multi-voice, replacing the single shared channel this whole project was built on

A direct follow-up request ("make pacman eating pills audible while the
alarm sounds goes off... really don't gate it to single channel calls")
asked for something the entire audio model above couldn't do: Tiny
Pacman's power-pellet siren (retriggered every real tick while active,
`md_playTone((float)(255-pacTimerGobeActive), 0.01)`) and its dot-eaten/
ghost-eaten SFX shared the same single tracked "audioVoice" `md_playTone()`
has used since the very first Arkanoid audio investigation this session -
so whichever fired more recently always cut the other one off, and this
was true for every game in the cartridge, not just Pacman.

First attempt was a targeted fix - a second, independent channel
(`md_playTone2()`/`md_stopTone2()`) specifically for Pacman's siren,
leaving every other game's own single-voice `md_playTone()` untouched -
reasoned as lower-risk than a global change, since some other games'
own retriggered-tone cues (monster-march steps, footstep ticks) are
*meant* to replace themselves each call, not stack.

**The user pushed back twice, correctly, before this shipped**: first
"can't we make playtone pick the next channel" (questioning why a
hand-built second slot was needed at all, rather than just letting
every call use a fresh channel), then, once a hand-wavy "the SDL3
backend can't easily do this" justification came up, "playtone lib
should already do this" - pointing directly at `playnote_start()`
itself. Reading it confirmed the user was right: `playnote_start()`
already calls Vircon32's own `play_sound()`, which picks the first free
SPU channel *internally* - `md_playTone()` never needed to manually
track a single voice at all, it was just fighting PlayNote's own
already-correct channel management by forcing `playnote_stop_all()`
before every call.

**Fixed properly**: replaced the single `audioVoice`/`audioStopAtFrame`
pair with a 16-element `audioStopAtFrame[]` array indexed by actual
channel number. `md_playTone()` now just calls `playnote_start()` and
records whichever channel it returns; `md_updateAudio()` loops over all
16 channels checking each one's own expiry independently. No per-game
opt-in, no second API - every existing `md_playTone()` call site across
every game gets this for free. The earlier two-channel version
(`md_playTone2()`/`md_stopTone2()`, and Pacman's own call routed through
it) was fully reverted before this shipped - grepped for both names
project-wide to confirm zero references remained.

**Why this doesn't break the "replace, don't stack" cues it was
initially worried about**: every frame-stepped sequencer already built
this session (and every one already in the project before it -
`arkAdvanceNoteSeq`, `tmisAdvanceNoteSeq`, etc) gates its own next note
to start only once the previous note's real duration has elapsed, via
its own `waitFrames` bookkeeping - so by the time any such sequence's
next call happens, the previous note has normally already auto-expired
and freed its channel back up via `md_updateAudio()`, same as before.
The only thing that actually changed is what happens when two
*genuinely concurrent* cues overlap in time (Pacman's per-tick siren vs.
a dot-eaten blip landing in the same window) - exactly the case that was
broken, and exactly what was asked for.

Verified via Puppeteer after rebuilding: both Tiny Pacman (through the
start jingle, into active gameplay, dots visibly eaten and ghosts
moving out of the box after extended movement) and Tiny Bomber (start
jingle, movement, bomb placement) render and play correctly with no
crash or corruption - a meaningful check here since this change touches
the one shared audio primitive every game in the cartridge calls into,
not just Pacman's own file.

## A global sound on/off toggle, added on direct request after comparing against the sibling SDL ports

A follow-up question ("can sound be turned off/on by button press +
debounce in this port?") revealed a real asymmetry: the sibling SDL
ports (`tinyjoypad_SDL3`/`Tinyjoypad_SDL`) have a dedicated, always-
available mute button (`BUTTON_SOUNDSWITCH`) working identically across
all 33 games, but this Vircon32 build had no equivalent - only 5 specific
AttinyArcade-lineage games (Stacker, Wren Rollercoaster, Bat Bonanza/Pong,
Frogger, UFO) have their own *local*, upstream-inherited "hold Fire ~2s"
secret mute gesture, faithfully ported from those specific games' own
original behavior - not a project-wide feature, and a different debounce
shape entirely (a hold-duration threshold + an action-done flag, not a
single-press edge check).

Added a genuine global toggle on request, modeled on the SDL ports' own
`gMuted` design: **Button Y** (the last of the 4 face buttons still
unused by this project - A=Fire, B=Fire2, X=pixel-grid toggle, Start=quit
dialog), a plain single-press edge check
(`muteButton && !prevMuteButton`), toggling a new `audioMuted` bool.
Deliberately **not** gated to "a game is running" the way the pixel-grid
toggle is (see that feature's own write-up above) - works identically on
the menu, mid-game, and during the quit-confirmation dialog, matching the
SDL ports' own always-available behavior rather than the pixel-grid's
own gameplay-only scope.

**Implementation is simpler than the SDL ports' own fix required**:
Vircon32 has a real hardware master-volume register exposed as
`set_global_volume(float)`/`get_global_volume()` (`audio.h`, range 0-2) -
muting is just `set_global_volume(0.0)`, unmuting `set_global_volume(1.0)`.
No need to gate every individual `md_playTone()` call or touch the
multi-voice mixing added earlier this session at all - the SPU's own
volume register applies to every channel uniformly, so this composes
cleanly with the just-added multi-voice fix without any interaction
between the two.

One dialect gotcha hit immediately: `audioMuted ? 0.0 : 1.0` (the natural
first attempt) doesn't compile on this platform (`character '?' is not a
valid identifier start` - this compiler has no ternary operator, a
long-standing documented restriction throughout this project) - rewritten
as a plain `if`/`else` instead.

Verified via Puppeteer: toggling the button on the menu screen, then
launching a game while muted, then toggling again mid-game to unmute -
all three transitions rendered correctly with no crash, freeze, or visual
corruption. Audio correctness itself (does it actually silence/restore
sound) wasn't verifiable via screenshot the same way the multi-voice fix
above couldn't be either - worth a real play-test to confirm.

## Tiny Missile's `ATTACK_WEAPON()` bug generalized into a project-wide audit for "loop's own re-checked condition flattened away" bugs

A direct follow-up request ("can verify if there are any other such
upstream vs downstream porting bugs... in all games i mean") turned the
Tiny Missile fix above into a targeted, project-wide sweep. The bug
class: an upstream `while`/`goto`-shaped loop whose own condition is
re-checked *fresh on every iteration* (not just once, at the top) gates
a call into a function with its own internal state-dependent branching -
porting that loop into a frame-stepped state can silently drop the
per-iteration recheck, either doing more than upstream (calling the
inner function when it shouldn't) or less (stopping too early).

Dispatched 4 parallel background agents, each covering ~8 games,
specifically hunting for this shape (not general code review, not the
sound-effect bugs already fixed earlier this session, not the byte-
truncation/shift-wraparound/signed-sentinel bug family already
extensively documented elsewhere in this file) - reading every upstream
`while`/`goto` construct (including companion C++ class files, not just
the main `.ino`) and tracing whether the port's own conversion preserves
the exact re-check cadence.

**31 of 32 remaining games came back clean** (Tiny Missile itself was
already fixed and excluded from the sweep). Tiny Plaque's own
`ADD_TEETH_TPLAQUE()`/`PUT_TEETH_TPLAQUE()` teeth-pool scan is
structurally the closest analog to Missile's own bug (a genuine resource-
pool-drain loop with per-candidate re-checking) and got the deepest
individual scrutiny of the whole audit - confirmed already correctly
ported, re-checking `extraTeeth > 0` fresh at each candidate and short-
circuiting the remaining scan the instant the pool empties, matching
upstream's own edge case (a call with an already-empty pool still
consumes its "turn" without activating anything) exactly.

**One real bug found: Tiny Bike.** Upstream's own per-tick movement loop:
```c
for (t=0; t<CHECK_SPEED_ADJ(ACCEL); t++){
  INCREMENTE_SCROLL(); if (DIV1==3) {...TRACK_RUN_ADJ();...} else{DIV1++;}
}
```
A plain C `for` loop re-evaluates its bound *every iteration* - so
`CHECK_SPEED_ADJ(ACCEL)` (which also has its own side effect,
`Higher_adj(ret)`, updating the jump-arc physics constant) is called
fresh each pass against whatever `ACCEL` currently is. `ACCEL` is not
read-only inside the loop body: `INCREMENTE_SCROLL()` ->
`RefreshPosSprite()` -> `CheckCollision()` -> `analise_minutieuse()` can
reduce it mid-loop via an oil-slick hit (`ACCEL-=0.20`), and a hard ramp
landing via `Break_Gravity()` (`ACCEL-=2`) can too - so a same-tick
collision correctly shortens the *remaining* iterations and re-derives
the jump-height constant against the new, slower speed.

The port (`gameTinyBike.c`) had instead hoisted this to
`int speedTicks = bikCheckSpeedAdj( bikAccel ); for(t=0;t<speedTicks;t++)`
- computed once, before the loop, with the SAME reachable mid-loop
`bikAccel` reduction paths (`bikAnaliseMinutieuse()`'s oil-slick case,
`bikBreakGravity()`) present in the port too (confirmed directly, not
just inferred from the agent's report). Effect: after a same-tick
oil-slick hit or hard landing, the port kept running the loop for the
*original* (higher, pre-collision) iteration count instead of correctly
shortening it, over-advancing scroll/track state that tick, and left
`bikHigherJump` computed from the stale pre-collision speed instead of
being refreshed - a different (less-gentle) jump arc than upstream
immediately after a mid-tick deceleration event. **Fixed** by moving the
`bikCheckSpeedAdj( bikAccel )` call directly into the loop condition,
matching upstream's genuine per-iteration re-evaluation instead of a
single hoisted value.

Verified via Puppeteer after rebuilding: Tiny Bike launches, plays
(acceleration, wheelie tilt, track scrolling, HUD) with no crash or
corruption - didn't specifically force an oil-slick-hit-mid-loop frame to
visually confirm the corrected shortened-iteration behavior itself
(audio/physics-timing side effects aren't screenshot-verifiable the same
way a crash would be), so worth a direct play-test focused on that exact
scenario if time allows. The other 31 games' own audits were report-only
(no files touched) and are not independently re-verified beyond the
agents' own traced reasoning - each report is detailed enough (exact
upstream line numbers, exact port line numbers, the precise re-check
condition compared) to re-check by hand if anything seems off later.

## Status (as of this session)

Shipped and visually verified (WebGL emulator + a Puppeteer screenshot
harness - see below): the shim architecture, the menu, and 33 full games
(NumberPlace, Tiny Invaders, 2048, HollowSeeker, Tiny Pinball, Tiny
Pacman, Tiny Bomber, Tiny Doc, Tiny Bert, Tiny Tris, Tiny Arkanoid, Tiny
Trick, Tiny Minez, Tiny Missile, Tiny Bike, Tiny Arena, Tiny Gilbert,
Tiny Pipe, Tiny Morpion, Tiny Plaque, Tiny SQuest, Tiny DDug, Tiny
Lander, Wren Rollercoaster, Frogger, Bat Bonanza, Stacker, UFO, Tiny
Dungeon, Oroboros, Run Dude Run, Four in a Row, Dino Game). Every game
from the project's original scope shipped with Tiny Dungeon (see its own
writeup below for what's still not independently re-verified about it
specifically) - Oroboros, Run Dude Run, Four in a Row, and Dino Game are
this project's first four additions *beyond* that original scope, found
via a follow-up GitHub/web search for TinyJoypad-compatible games not
already catalogued (see "Beyond the original scope" below for the full
survey and what else it turned up) - **Dino Game was the last known
candidate from that survey**, so the beyond-scope backlog is now empty
unless a future search turns up something new (see the Status update at
the very end of this section for the full picture of what's left).

- `src/machineDependent.h` + `src/portVircon32.c` - the `md_*` primitives
  (video/input/audio) plus the atlas texture setup and the top-level
  menu<->game dispatch loop.
- `src/tinyJoypadShim.h/.c` - reproduces the Lorandil/phoenixbozo
  `tinyJoypadUtils.h` API (`InitTinyJoypad`, `isXPressed`,
  `PrepareDisplayRow`/`SendPixels`/etc, `Sound`) on top of `md_*`.
- `src/obonoCoreShim.h/.c` - reproduces Obono's `TinyJoypadWorks` core API
  (sprite/string compositing engine, button state, `playTone`/`playScore`)
  on top of `md_*`.
- `src/avrCompat.h` - the trick that made porting upstream `.ino`/`.cpp`
  files tractable without line-by-line rewrites: `uint8_t`/`int8_t`/etc
  alias to plain `int`, `PROGMEM`/`pgm_read_*`/`memcpy_P` become ordinary
  flat-memory access. Combined with the fact that `sizeof`/`memcpy` both
  count in words on Vircon32, most upstream array-sizing arithmetic (written
  in bytes, for byte arrays) keeps working unmodified once the array becomes
  word-sized elements - the units change together.
- `src/games/gameNumberPlace.c` - NumberPlace (Obono, MIT). Low structural
  risk - upstream was already mode/state-based, no blocking loops. Memory-
  card persistence (high scores) deferred - `obonoCoreShim.h`'s
  `loadRecord`/`storeRecord` are stubs for now.
- `src/games/gameT2048.c` - t2048 (Obono, MIT). Easiest port yet - `core.h`
  is nearly byte-identical to NumberPlace's own (see `#define SPECIAL
  DIRECT` alias at the top of the file), already mode/state-based
  upstream, no blocking loops. Surfaced one real, generalizable bug in the
  shared shim: `obonoCoreShim.c`'s `SPRITES` capacity was hardcoded to 8
  (NumberPlace only ever needed 5), but t2048's 4x4 board calls
  `setSprite()` with indices up to 15 - `setSprite`/`moveSprite`/
  `clearSprite` do no bounds checking, so indices 8+ silently overran into
  the adjacent `string[]` array, corrupting UI label positions and
  glitching whichever board tiles were far enough out-of-bounds (the
  bottom rows). Fixed by raising `SPRITES` to 20 - a shim-level fix, so it
  protects any future game with a bigger sprite count too, the same way
  the earlier byte-masking fix did for `md_drawColumn()`.
- `src/games/gameTinyInvaders.c` - Tiny Invaders v4.2 (credited in the menu
  as "DANIEL C / SVEN B" - the `.ino`'s own header says "Programmer: Daniel
  C 2018-2020, Enhancements: Sven B 2021" for this specific v4.2 release;
  earlier notes in this file said "Lorandil" for this game, which was
  imprecise - `Lorandil@gmx.de` is Sven B's own listed contact email
  (confirmed against TinyMinez's and TinyDungeon's headers, both of which
  say "\[Programmer/Developer\]: Sven B" with that same contact address),
  so "Lorandil" appears to be Sven B's GitHub/online handle rather than a
  separate third person - **GPLv3**, see Licensing below. The bigger lift:
  upstream's `loop()` is one
  never-returning function built from nested `while(1)` + `goto`
  (`NEWGAME`/`NEWLEVEL`/`BYPASS2`/`Bypass`/`RestartLevel` labels) plus
  several `_delay_ms()` calls, since it assumed it owned the whole CPU for
  the entire play session. Rewritten as an explicit frame-stepped state
  machine (`enum` + `tinvWaitFrames` countdown replacing every
  `_delay_ms()`) - see the file's own header comment for the full mapping.
  High-score EEPROM persistence and the 3-letter name-entry screen are
  dropped for now (score is tracked in-memory for the cartridge session);
  "NEW HIGH SCORE" just shows a banner instead. Several more bugs found
  later the same session: the `MonsterGrid` 0xFF-vs-`-1` signed-sentinel
  mismatch (corrupted static on the "LEVEL N" flash screen), the first
  playthrough showing "LEVEL 0" instead of "LEVEL 1", and `tinvLive`
  likewise starting at 0 instead of 3 on a true first playthrough (see "A
  third real bug, same family" above - same root cause as the level number,
  `gameTinyInvaders_init()` not fully replicating upstream's `NEWGAME:`
  reset block); a control-flow bug where clearing a level froze the game
  permanently right after the correct "LEVEL N" flash (see "A fourth bug"
  above) - `tinvOnWaitComplete()` was clobbering its own callee's freshly-
  set wait action back to NONE; and a stale "NEW HISCORE!" value that could
  permanently lag behind the true score after certain deaths (see "A fifth
  bug" above) - `tinvHighScore` now syncs immediately in `tinvAddScore()`
  instead of relying solely on a once-per-frame poll that could be starved.
- `src/games/gameHollowSeeker.c` - HollowSeeker (Obono, MIT). Already mode/
  state-based upstream (LOGO/TITLE/GAME with STATE_START/PLAYING/OVER inside
  GAME), no blocking loops to convert. Its `core.cpp` needed a WIDER font
  range ('!' through '_') than NumberPlace/t2048's ('-' through 'Z') -
  `obonoCoreShim.c`'s shared `imgFont[]` table was widened to the superset
  (byte-identical in the overlap) rather than duplicating a second table,
  same "fix it once in the shim" approach as the SPRITES capacity bump.
  Surfaced two real, generalizable bugs (see the section above): the
  `rand()`-range mismatch (fixed via the new shared `arand()` helper, also
  applied retroactively to the other three games' own random helpers) and
  the shift-count-wraparound bug in its cave-wall rendering (fixed with
  explicit clamping in `hsDrawGame()`). Also needed `hsCavePhase` (a lap
  counter) to explicitly wrap at 256 with `& 0xFF` - upstream relied on this
  being a `uint8_t`'s implicit overflow to run its once-per-lap death check
  and its "final 12 frames of the lap" warning tone; without the explicit
  wrap the counter just grew forever, so the death check only ever fired on
  the very first frame (the player became unkillable) and the warning tone
  condition went permanently true forever the first time it crossed 244 (an
  endless hum) - both symptoms the user found through actual play.
- `src/games/gameTinyPinball.c` - Tiny Pinball (Daniel C, GPLv3). First
  Tier-2 (tinyJoypadShim-lineage, per the porting-priority memory) game
  ported. Straightforward structurally (upstream's `loop()` is just a
  `NEWGAME:`/`start:` goto-chain around two `while(1)`s, the same shape
  Tiny Invaders already established a conversion pattern for) - the real
  work was figuring out upstream's button reads, which bypass
  `isXPressed()`-style helpers entirely and poll `analogRead(A3)`/
  `digitalRead(1)` directly; cross-referencing the exact thresholds against
  Tiny Invaders' own driver source (and this game's own `ELECTROLIB.h`,
  which confirms A3 is the up/down axis) resolved the mapping without
  needing any new shim primitives. Remapped controls to independent
  left/right/down d-pad inputs (left flipper/right flipper/launch spring)
  instead of upstream's up-only-left-flipper and one shared down-or-fire
  input serving double duty for both the right flipper and charging the
  launch spring - reads far more naturally on a real gamepad. Also dropped
  Tiny_Flip2's partial-screen-band redraw optimization (upstream skips
  `i2c_write` calls outside the ball's immediate row-band and relies on the
  physical SSD1306's VRAM *persisting* whatever was drawn there last frame -
  a real hardware behavior this project's always-clear-then-redraw model via
  `md_beginFrame()` can't replicate; skipping those columns here would make
  the playfield outside the ball flash black every frame instead of staying
  visible) - always redraws the full 8 pages instead, trivial against the
  budget. One real bug found via actual play (reported by the user: "start
  with 0 balls... then 4 balls of a sudden"): `tpBeginPlaying()` (reached
  once, from the READY screen, before the very first ball) was missing the
  `totalBall--` decrement upstream's `start:` label does unconditionally
  before every ball including the first - left `tpTotalBall` at its initial
  5, one past the 0-4 range the ball-count sprite arrays index into, so the
  first ball rendered a garbage out-of-bounds frame instead of "4 balls
  left" (only self-corrected once losing that first ball hit the *other*,
  correct decrement already present in the "ball lost, balls remain" path).
- `src/games/gameTinyPacman.c` - Tiny Pacman (Daniel C, GPLv3). Second
  Tier-2 game. Button mapping was ordinary this time (no remap needed) -
  cross-referencing upstream's raw `analogRead(A0)`/`analogRead(A3)`/
  `digitalRead(1)` reads against Tiny Invaders' driver constants confirmed
  they land exactly on `isLeftPressed()`/`isRightPressed()` (A0) and
  `isUpPressed()`/`isDownPressed()` (A3) - upstream's own `DirectionV`/
  `DirectionH` field names are swapped from what they actually control
  (`DirectionV` steps x, `DirectionH` steps y), just an upstream naming
  quirk, not a porting wrinkle. Preserved upstream's "sticky" direction
  input exactly (once set, `DirectionV`/`DirectionH` are never reset to
  their "no input" sentinel of 2 by releasing a button, only by pressing
  the opposite direction) - that's what makes Pac-Man/ghosts keep gliding
  in their last direction the way the original arcade game does. Dropped
  upstream's own 44ms-per-tick `FPS_Control` real-time frame limiter and
  its "only redraw every *other* logic tick" split (`Frame%2==0` gating
  `Tiny_Flip()` itself) - both were AVR performance compromises (bit-
  banging a redraw over i2c is slow; capping the loop rate kept gameplay
  sane despite that) this project's fast, always-clear-then-redraw model
  doesn't need - runs the full tick (movement + redraw) once every real
  60fps frame instead, so movement is somewhat brisker than upstream's
  blended ~45fps logic-tick rate. `random()%2` (a ghost's post-wall-bump
  direction pick) switched to the shared `arand(2)` helper, same reasoning
  as HollowSeeker's fix. No bugs found this time - played correctly
  (Pac-Man, ghosts, dots, power pellets, lives, attract-mode Pac-Man
  suppression) on the first build.
- `src/games/gameTinyBomber.c` - Tiny Bomber (Daniel C, GPLv3). Third
  Tier-2 game (a Bomberman clone) - skipped **Tiny DDug** for now despite
  its lower goto count, since it's built around a C++ class (`ClassTDDUG.h`)
  rather than a plain `.ino`, the same complexity that made TinyDungeon a
  deferred Tier-3 game (goto/while(1) counts alone don't capture that kind
  of cost - worth re-triaging Tiny DDug properly before assuming it's
  next). Bomber's own button mapping and structure closely mirror
  gameTinyPacman.c (same author, same analog-axis-plus-discrete-pin
  pattern, same `DirectionV`/`DirectionH` naming swap) - but unlike Pacman,
  direction fields *do* reset to their "stopped" sentinel here, gated on
  reaching the next grid cell (`x%8==0`/`Decalagey==0`) rather than on
  button release, preserved exactly since that's what gives movement its
  clean grid-snapping feel. Upstream's `StartGame()` busy-waits for the
  button to be *released* before returning (so the same press that
  confirms "start" can't also instantly place a bomb) - exactly what
  `md_armInputFireGate()` (built for gameTinyInvaders.c) already does, used
  here instead of a real blocking wait. Same AVR-performance-compromise
  drops as Pacman (44ms frame limiter, alternate-tick redraw split) and the
  same `random()%2` to `arand(2)` fix. Verified the full gameplay loop by
  actual play: movement, destructible checkerboard blocks vs. solid walls,
  bomb placement/fuse/explosion, and - confirming self-damage works
  correctly, a core Bomberman mechanic - the player's own bomb blast
  correctly killed them and decremented lives (3 to 2) with a clean
  respawn at the level start position.
  **One real bug found via later user playtesting**: reproducible with
  "move right, then left, then hold up" - the player could move up
  about one grid row then get permanently stuck, unable to go further,
  even though nothing was visually blocking the path. Root cause: a
  single transcribed byte was missing from the 1024-entry `bomBack[]`
  wall-collision bitmap (`spritebank.h`'s `back[]`) - a plain manual-
  transcription slip, not a Vircon32-dialect issue like the earlier
  byte-truncation/shift-count bugs. One dropped `0x00` at array index
  655 (row 5, column 15) shifted every following byte in the array by
  one position, so from that row down the collision data the game
  checked was actually the *next* column's data - producing false
  "wall here" collisions at effectively-random-looking spots. Found by
  writing a Node script to re-extract both the ported `int[1024]
  bomBack` array and upstream `spritebank.h`'s `back[] PROGMEM` array
  and diff them value-by-value (rather than trusting eyeballed hex, or
  the initial debug-marker/HUD-overlay approach that only proved the
  bug was real but not *why*) - the same technique is worth reaching
  for immediately on any future "collision looks wrong in one specific
  area but the logic reads correctly" report, since manual transcription
  of large data tables is exactly the kind of mistake static code review
  won't catch. All of Bomber's other data tables (`bomBlocDetect`,
  `bomLevelData`, `bomBackBlitz`, `bomCaracters`, `bomMusic`, `bomFire`,
  `bomBomb`) were re-diffed the same way after this find and confirmed
  byte-for-byte correct - this was an isolated transcription slip, not a
  systemic issue. Fixed by inserting the missing byte; rebuilt and
  re-verified the exact repro sequence in the WebGL emulator, confirming
  the player can now traverse the full column to the top row.
  **A second, more serious bug reported later**: a hard crash ("ERROR:
  INVALID MEMORY READ") reported on level 3, near a bomb exploding close
  to the upper-right corner. Vircon32's compiler accepts a `-g` flag that
  emits a debug-info file mapping each instruction address back to a
  source line; feeding the crash screen's reported Instruction Pointer
  into that mapping pointed directly at `bomRecupeBackToCompV()`'s
  `bomBack[ sprite.y * 128 + maxV ]` / `bomBack[ (sprite.y + 1) * 128 +
  maxV ]` reads - the vertical-movement (`DirectionV`, x-axis) sibling of
  `bomRecupeBackToCompH()`, which already guarded every `bomBack` index
  with `idx > 1023 || idx < 0` before reading. `bomRecupeBackToCompV()`
  never got the equivalent guard (neither did upstream's own
  `RecupeBacktoCompV` - the same unguarded read exists in the original
  `.ino`, it just never crashes there since AVR has no memory protection
  and silently returns garbage flash data instead). A sprite sitting at
  the bottom row (`y == 7`, a completely normal, reachable position -
  the player *starts* every level there) makes the `y + 1` lookup index
  `1024 + maxV` - one full row past the 1024-entry array. In most cases
  this silently reads into the next declared global (`bomBackBlitz`,
  same size, right after it) and just returns wrong-but-valid data - which
  is presumably why this had survived multiple earlier full playthroughs
  unnoticed. Because `gameTinyBomber.c` is the *last* `#include` in
  `main.c`, its own globals sit at the tail end of the whole cartridge's
  13,445-word global segment - on level 3, apparently, the exact
  combination of `sprite.y`/`maxV` finally pushed the read far enough
  past `bomBackBlitz` too to hit genuinely unmapped memory and hard-crash
  instead of just returning bad data. Fixed by adding the same bounds
  guard `bomRecupeBackToCompH()` already had (routed through a new
  `bomRecupeBackToCompVIdx()` helper). While in there, also fixed a
  sibling latent bug in `bomBoolWrite0()` (the destructible-block bitmap
  writer): unlike `bomBoolRead()`, which already rejected `numero > 105`,
  `bomBoolWrite0()` had no upper bound at all, so a bomb exploding at the
  bottom row (`bomDestroyBloc()`'s `y+1` case, reachable since `BOMBXY[1]`
  can hit 8 when a bomb is placed at max `Decalagey`) could write a
  couple words past the 14-entry `bomBlocBombMem` array into the next
  global (`bomMusic`) - same "the sibling function has a guard, this one
  doesn't" pattern, fixed the same way. Verified the fix with a 30+ second
  soak test letting all 4 enemies bounce autonomously on both level 1 and
  a temporarily-forced level 3 (enemies visibly reached the bottom-right
  corner in both) with no crash, then reverted the temporary level-force
  test hook. The `-g`/debug-info-address-to-source-line technique used
  here is worth reusing directly for any future "ERROR: INVALID MEMORY
  READ/WRITE" crash report, rather than guessing from a code read alone -
  see `obj/main.vbin.debug` (generated via `assemble -g program`) for the
  address-to-line mapping format.
- `src/games/gameTinyDoc.c` - Tiny Doc (Daniel C, GPLv3). Fourth Tier-2 game,
  picked next per the priority triage (lowest `goto` count remaining in the
  Daniel-C family). A Dr. Mario-style falling-pill puzzle: two-cell "pills"
  drop into an 8x10 grid, runs of 4+ same-colored cells (pill halves or
  viruses) clear, and clearing every virus in the grid advances the level.
  Same `FastTinyDriver.h`/ELECTROLIB.h button-read pattern as Pinball/Bomber
  (no UP needed here - only left/right/soft-drop/rotate). Data tables
  (`SpriteBank.h`) were extracted from upstream with a small Node script
  rather than transcribed by hand, specifically to avoid a repeat of
  Bomber's dropped-byte bug - all 11 arrays confirmed byte-for-byte via
  the script's own count vs. a manual re-read of the source. The riskiest
  structural piece: upstream's line-clear/gravity resolution
  (`do { CheckCompletedLine_TD(); } while (DropPills_TD());`, plus
  `ClearLine_TD()`'s own 6-frame clear-flash loop) is a genuinely
  *blocking, multi-frame* cascade upstream (each iteration calls
  `TinyFlip_TD()`+`FPS_Count_TD()` itself, looping until nothing more
  matches or drops) - rewritten as an explicit `tdResolveState` sub-state
  machine (SCAN -> CLEAR_ANIM -> DROP -> back to SCAN if anything moved,
  else resume normal play) that advances exactly one step per real engine
  frame instead of looping to completion inside a single frame, the same
  "blocking loop -> explicit resumable state" treatment every other
  tinyJoypadShim port here has needed, just applied to a genuine
  match-cascade instead of a simple timer. Also had to catch, before ever
  compiling: `DropPills_TD()` counts `for (uint8_t y = 8; y < 255; y--)`,
  relying on `y` underflowing 0 -> 255 as a loop-termination trick - since
  `uint8_t` aliases to a real (non-wrapping) `int` here, that condition
  would be true forever, so rewritten as a plain signed
  `for (int y = 8; y >= 0; y--)` - the same AVR-implicit-behavior class of
  bug as the byte-truncation/shift-wraparound/signed-sentinel bugs found
  earlier this project, just caught by inspection before it ever shipped
  rather than by a bug report. Also rewrote GCC's `case N ... M:` case-
  range extension (used twice upstream) as plain `if`/`else if` chains,
  and avoided `switch` entirely, since neither is exercised anywhere else
  in this project's ports and there was no reason to be the first to find
  out whether the dialect supports them. Wrote the render loop
  (`tdTinyFlip`) with the row/x-range gating lesson from this same
  session's Tiny Invaders/Bomber CPU-load pass baked in from the start
  (each of its 7 draw layers only ever applies to an already-known narrow
  x/y sub-range - gated in the outer loop instead of calling every layer
  unconditionally on all 1024 pixels/frame). Verified via actual play:
  attract screen, pill movement/rotation/soft-drop, connected multi-cell
  pieces dropping together, locking, game over, and the return-to-attract
  flow, across two extended sessions with no crashes - did not
  specifically force-verify the clear-*animation* frames firing (color
  matches are random per-playthrough and none happened to line up during
  testing), so that specific path is unconfirmed by direct observation,
  though the logic was ported unchanged from upstream's own (line-by-line
  equivalent) scan/clear code.
  **Two bugs found shortly after shipping, both from not fully applying
  this project's own established lessons to a new port:**
  1. The user reported the right portion of the screen (background art,
     the mascot, the level/virus-count panel) staying black except while
     a pill was being "thrown" (`PILLMODE_TD==3`). Root cause: I'd ported
     upstream's `TinyFlip_TD(PartialX_85_128, PartialY_4_8)` parameters
     verbatim - upstream only redraws the left 85-of-128 columns (or the
     top 4-of-8 rows) most of the time, and relies on the real SSD1306's
     VRAM *persisting* whatever was drawn in the skipped columns/rows last
     time - a real hardware behavior this engine's always-`clear_screen()`
     -then-redraw model via `md_beginFrame()` can't replicate. This is the
     **exact same class of bug already documented and fixed** in
     `gameTinyPinball.c`'s own header comment (its `Tiny_Flip2` partial-
     redraw optimization) - I simply didn't cross-check Tiny Doc's
     `TinyFlip_TD` against that already-written lesson before porting it.
     Fixed the same way Pinball was: always redraw the full 128x8 every
     frame (`tdTinyFlip()` no longer takes width/height parameters at
     all). A good example of why "grep this project's own CLAUDE.md for
     the same shape of upstream optimization" is worth doing *before*
     porting a new game's flip function, not after a user reports it.
  2. Separately, the user asked whether Tiny Doc got the same CPU-load
     optimization pass as Invaders/Bomber/Pacman, since it was also
     reported hitting the ~100% ceiling. It had only gotten the *first*
     technique (row/x-range gating, above) - not the deeper one:
     `DrawTAB_TP`/`DrawNewPill_TD` were still scanning every relevant
     *pixel* and re-deriving which grid cell/pill-half could overlap it
     before calling `blitzSprite()`, the same O(pixels x objects) shape
     Bomber/Pacman's sprite compositing had. Fixed with the same
     restructuring: `tdCompositeTabIntoBuffer()`/
     `tdCompositeNewPillIntoBuffer()` now walk the actual grid
     cells/pill-halves once per page row and write only their up-to-4
     occupied columns into a shared `tdSpritePageBuffer[128]`, instead of
     scanning all ~273 (tab) / ~504 (pill) pixels and calling
     `blitzSprite()` for each regardless of whether anything was there.
     Re-verified rendering afterward (stacked columns of locked pills,
     mixed pill/virus icons, rotation) with no regressions. No numeric
     CPU% measurement exists for this build either (same caveat as the
     Invaders/Bomber pass) - worth checking in real play.
  **Still reported heavy, specifically "with many pills on screen"**: that
  phrasing pointed straight at `tdCompositeTabIntoBuffer()`, since it's the
  one layer whose cost scales with how many grid cells are filled (the
  currently-falling pill and every other layer are cost-independent of
  board fill). Found a real, if subtle, waste once looked at again: for
  every locked cell, the loop called `tdBlitzSprite()` **twice** - once
  for the pill-half sprite, once for the virus overlay via
  `tdSwitchRecupVirus()` - but that function's own "not a virus" sentinel
  (frame `1`) points at `tdVirus`'s frame 1, which is literally
  `{0x00,0x00,0x00,0x00}` (checked directly in the data table) - meaning
  the virus-layer blit was *guaranteed* to compute to 0 for every locked
  pill-half cell, every time, and got called anyway. Since pill halves
  only ever accumulate over a level while viruses only ever decrease,
  this cost grows in exactly the direction the user described. Fixed by
  checking the cell's type once (already extracted for the pill-frame
  lookup) and skipping the virus-layer `tdBlitzSprite()` call entirely
  unless the cell is actually a virus (type 1-3) - `tdSwitchRecupVirus()`
  itself became dead code and was removed. Re-verified: locked-pill
  rendering identical, virus cells still show their distinct overlay
  texture, no regressions. Same class of finding as the last two rounds -
  a render-cost lever that isn't about *whether* a layer is gated by
  position, but whether it's doing work whose result is already knowable
  as a constant in the common case.
  **Still reported slow specifically "when the grid is almost full"**:
  a different lever again - not a per-pixel gating gap, and not a
  provably-constant branch, but recomputing an *entire* layer from
  scratch every single frame even on frames where nothing about it had
  actually changed. `tdCompositeTabIntoBuffer()` (the locked-grid
  composite) was still being fully rebuilt - every filled cell, every
  frame - regardless of whether the grid had been touched since the
  previous frame. In practice the grid only actually changes when a pill
  locks, a clear/drop cascade is running, or a level resets; on every
  *other* frame (a pill just falling/moving under player control, which
  is most frames) the entire expensive recompute was wasted work
  reproducing an identical result. Added a `tdTabDirty` flag (default
  `true`) plus a persistent `tdTabCache[1024]`: every function that
  actually writes to `tdTab` (`tdFixPill`, `tdSetSinglePill`, `tdInitRnd`,
  `tdDropPills`, `tdInitPublicVarForNewLevel`, and the inline clear-
  animation step in `gameTinyDoc_update()` - found by grepping every
  `tdTab[x][y] = ...` assignment site in the file, not just the obvious
  ones) sets the flag; `tdTinyFlip()` only calls the real composite (and
  refreshes the cache) when the flag is set, otherwise just copies the
  cached bytes for that row - a plain array read, zero `blitzSprite()`
  calls - and clears the flag once a full dirty pass finishes. This is a
  different *class* of lever than the previous two rounds for this game
  (which were both about reducing per-call cost); this one is about
  avoiding the call entirely across consecutive unchanged frames.
  Verified across several drop-then-idle cycles: locked-grid rendering
  stayed pixel-identical whether freshly recomputed or served from cache,
  newly-added pills still appeared correctly on the next dirty frame, and
  game-over/attract transitions were unaffected (they use a separate
  render path entirely). This kind of dirty-flag caching is worth
  reaching for on any future port whose per-frame cost scales with a
  mostly-static board/grid state rather than with something that
  genuinely changes every frame (a falling piece, animation timers, etc).
  **A real regression from that same caching, reported much later in the
  session**: the user noticed the viruses sitting in the bottle from the
  start of a level barely seemed to animate anymore, making them harder
  to visually distinguish at a glance - exactly the "animation timers"
  case the caching writeup above already called out as the wrong thing
  to cache, but the actual virus-wiggle animation wasn't caught at the
  time. Root cause: `tdFrmVirus` (a 3-frame idle-wiggle counter,
  `tdAnimSpeedVirus`-throttled to advance roughly every 3 real frames) is
  baked into the locked-grid composite's virus-layer blit
  (`tdCompositeTabIntoBuffer`), but that composite only recomputes when
  `tdTabDirty` is set by an actual grid *mutation* - advancing a purely
  cosmetic animation counter doesn't touch `tdTab` at all, so the cached
  buffer kept showing whichever virus frame was active during the *last*
  real grid mutation, only ever jumping to a new one whenever some
  unrelated pill-lock/clear happened to also refresh the cache (rare
  compared to how often the animation itself ticks). Fixed with one line
  - `tdTabDirty = true;` added right where `tdFrmVirus` actually changes -
  so the animation keeps its own 3-frame cadence exactly as before the
  caching was introduced, while the cache still holds for every other
  frame where nothing (grid or animation) actually changed. Verified by
  cropping the virus region from 8 consecutive real-time screenshots and
  diffing them pairwise (hundreds of differing pixels between neighboring
  frames, versus what would be near-zero if still stuck) rather than
  eyeballing a static comparison - the same "instrument and prove
  empirically" approach as every other reported-but-hard-to-eyeball bug
  this session.
- `src/games/gameTinyBert.c` - Tiny Bert (Daniel C, GPLv3). Fifth Tier-2
  game, picked next for its lowest remaining goto count among the
  non-C++-class Daniel-C titles (Tiny DDug was checked again and is still
  a C++-class file, deferred same as before). A Q*bert clone: an
  isometric 4-row pyramid, jump diagonally between plates to flip every
  one to the target color while dodging a bouncing ball and a snake
  enemy. Applied every lesson from this session's optimization/bug-fixing
  work **from the start** instead of needing a later pass: composited the
  3 moving sprites (plus their black cutout masks, used for monochrome
  silhouette punch-through) once per page row into shared buffers rather
  than recomputing all 3 at every pixel; gated the narrow-range UI layers
  (lives, score) by row; always redraws the full 128x8 screen (upstream's
  `Scan` parameter skipped columns 109-127 most frames, the exact same
  VRAM-persistence assumption already found and fixed in Pinball/Doc).
  One real structural bug caught *while writing the port*, before ever
  compiling: upstream's `Sprite` struct used C++ in-class default member
  initializers (`uint8_t sw=1; Timer_new_Live=MAX_RENEW;`) that only ever
  run once, since the `Sprite sprite[3];` declaring them sits inside
  `loop()`, which here never actually returns/restarts from scratch (it's
  one big internal goto-loop, so that local declaration executes exactly
  once, at power-on) - ported as an explicit one-time init in
  `gameTinyBert_init()` rather than relying on struct-literal defaults
  (not supported for plain C structs here regardless). Also caught, only
  after drafting a first version and re-reading upstream more carefully:
  upstream's `MENU_LOOP:`/`NEW_GAME:`/`NEW_LEVEL:` fall-through means a
  fresh game's *first* level setup (score/lives/dificulty/plate-grid
  reset) only ever happens once - at power-on, or again after a true
  game-over - never on every "press fire to start" at the title screen
  (that press only flips a session flag and resets the score digits). My
  first draft called the full reset from the fire-press handler itself,
  which would have silently re-randomized/reset state on every attract-
  to-playing transition instead of matching upstream's actual one-shot
  reset semantics; split into `bertBeginAttract()` (full reset, matching
  `NEW_GAME:`) vs. `bertSetUpLevel()` (level-only reset, matching
  `NEW_LEVEL:`, reused on both true game start and level-clear) before
  ever building, once the distinction was noticed by re-tracing the
  control flow rather than assuming the first draft's shape was right.
  Verified via extended play: pyramid/lives/score/lift-platform rendering
  all correct on the first build, jump movement in all 4 directions,
  enemy AI wandering and flipping plates, and - unprompted, during a
  routine soak test - a real player-enemy collision that correctly
  decremented lives (3->2) and respawned Bert at the pyramid's apex, then
  30+ seconds of continued autonomous play with no crashes.
  **Still reported heavy after shipping**: the user asked directly whether
  there was more room to optimize, since Bert was still noticeably heavy
  despite the sprite-compositing/row-gating done at ship time. There was:
  the pyramid tiles (`bertGridPlate()`) and the two lift platforms were
  *not* composited or gated the same way the 3 moving sprites were - both
  still called `bertBlitzSprite()` for all 1024 pixels/frame regardless of
  whether anything was actually there, the exact O(pixels x objects)
  pattern already fixed for the sprites (and for Bomber/Pacman/Doc
  earlier). Fixed the same way: `bertCompositePlatesIntoBuffer()` walks
  only the (up to 4) pyramid cells whose row overlaps the current page and
  are actually flipped, using upstream's own hardcoded cell positions
  (matching its `PlatePos[]` table, which `GridPlate()` had hand-unrolled
  into per-row literal calls rather than looping - likely an AVR
  performance choice) instead of scanning all 128 columns; the lift
  platforms got a straightforward row+column range gate (`LIFT_PLATE` is
  1 page tall at a fixed y, only 13 columns wide at each of 2 fixed x
  positions) in place of calling `bertBlitzSprite()` unconditionally for
  every column and trusting its own bounds check to no-op most of them.
  Re-verified afterward: identical rendering, plus visible confirmation
  the plate rewrite is correct (score changed to a real nonzero value and
  3 tiles visibly flipped to solid white, matching the grid state exactly)
  across a jump sequence and a further soak test with no regressions. This
  is the second time in this session a game shipped with only *some* of
  its per-pixel draw layers optimized (Doc had the same gap first) - worth
  checking *every* draw-layer function in a new port's flip routine against
  this pattern before shipping, not just the obviously-largest one.
  **Requested audit, much later, using the new perf overlay** ("check Tiny
  Bert for optimizations"): found and fixed two more real gaps, plus a
  useful negative result.
  1. The attract/title screen branch of `bertTinyFlip()` called
     `bertPolicePrint(x,y)` (the score-digit font blit, self-gated to
     `y==0 && x>=94`) unconditionally for all 1024 pixels/frame - the
     in-game branch already call-site-gated the same function, but the
     attract branch (which also genuinely needs it, to show the live
     score on the title screen) never got the equivalent gate. Fixed by
     matching the same `scoreRow && x>=94` gate the in-game branch uses.
  2. `bertCompositePlatesIntoBuffer()` (the pyramid-tile composite) was
     still being fully recomputed every single render frame, even though
     `bertPlateGrid` only actually changes once every ~15+ real frames (on
     a jump landing) or at level start/flash - the same "recompute an
     unchanging structure every frame" waste Tiny Doc's own locked-grid
     composite had, just not yet applied here. Fixed with a
     `bertPlateDirty` flag plus a persistent `bertPlateCache[1024]`:
     `bertRefreshPlateCacheIfDirty()` recomputes all 8 rows only when the
     grid actually changed (marked at all 3 real mutation sites -
     `bertResetPlateGrid()`, `bertFlipPlate()`, and the jump-landing tile
     flip), otherwise `bertTinyFlip()` just reads the cached array.
  3. **Measured, not just applied on theory** (per this session's own
     established practice): gameplay CPU stayed at a steady ~70-76% both
     before and after the plate-cache fix, with no meaningful change even
     during entirely passive play (no input, plate grid genuinely static).
     This means the plate composite was *not* actually the dominant cost
     here - Bert's real bottleneck appears to be the sheer number of real
     `md_drawColumn()` GPU-blit calls the checkered-dither background art
     demands (nearly all 1024 columns/frame are non-zero, so almost none
     of the "skip if value==0" short-circuit fires) - an inherent cost of
     this game's dense visual style under the shared column-atlas
     rendering model, not a fixable inefficiency in Bert's own compositing
     logic. Both fixes above are still real, safe, correct improvements
     (verified via screenshot: tiles flip correctly, score renders
     correctly on both the attract and gameplay screens, no regressions)
     - they just weren't the source of the ~70% baseline. Consistent with
     this project's own standing lesson from the column-run-coalescing
     regression: measure before concluding an optimization helped, even
     when the reasoning behind it is sound.
- `src/games/gameTinyTris.c` - Tiny Tris (Daniel C, GPLv3). Fourth Tier-2
  game (a Tetris clone) - button mapping and structure again closely mirror
  the other Daniel-C games (analog-axis-plus-discrete-pin, same author's
  generic `blitzSprite`/`RecupeLineY`/`RecupeDecalageY` primitives, no
  `arand()` fix needed since `PSEUDO_RND_TTRIS` is upstream's own rotating
  0-6 counter, not a real `rand()` call). Its 12x19 playfield is stored
  bit-packed (`Grid_TTRIS[12][3]`, 8 rows per byte, 3 bytes per column,
  matching how upstream's own `GRID_STAT_TTRIS`/`CHANGE_GRID_STAT_TTRIS`
  address it) - kept faithful to that representation rather than
  simplified to a plain per-cell array, same as this project's other
  bit-packed data structures. `Tiny_Flip_TTRIS(uint8_t HR_TTRIS)`'s
  partial-redraw width parameter (82-of-128 columns most frames) was
  dropped from the start (always redraws full width), avoiding the
  Pinball/Doc VRAM-persistence bug proactively rather than rediscovering
  it. `DELETE_LINE_TTRIS()`'s blocking 5-iteration flash-and-delay
  animation became an explicit `TRIS_STATE_LINE_FLASH` sub-state advancing
  one half-step per real frame, same treatment as every other blocking
  upstream loop this session. Locked-grid/falling-piece/preview-piece
  rendering was built as per-cell-per-page compositing *from the start*
  (not retrofitted after a "still heavy" report, unlike Bert/Doc) -
  `trisCompositeGridIntoBuffer()`/`trisCompositeDropPieceIntoBuffer()`/
  `trisCompositeNextBlockIntoBuffer()` only visit cells that actually
  overlap the current page row and are actually filled/present, rather
  than scanning every candidate cell against all 1024 pixels the way
  upstream's own `Recupe_TTRIS()`/`DropPiece_TTRIS()`/`NEXT_BLOCK_TTRIS()`
  did. EEPROM high-score persistence (a 4-slot checksummed backup scheme)
  is stubbed to in-memory-only, matching the NumberPlace/Tiny Invaders
  precedent.
  **Two real bugs found via actual play, both in the newly-added menu
  pagination + this game's own attract-screen rendering, neither present
  in upstream since AVR has no per-frame instruction budget to blow**:
  (1) the title screen flickered heavily - proven by screenshotting
  consecutive frames, one of which showed only the top ~1.5 of 8 page-rows
  drawn before cutting to black, i.e. exactly the CPU-load model's own
  documented risk ("if a frame's work exceeds budget, the CPU literally
  stops executing mid-instruction-stream"). Root cause: upstream's own
  `recupe_SCORES_TTRIS`/`recupe_Nb_of_line_TTRIS`/`recupe_LEVEL_TTRIS`
  each self-gate with a position bounds-check *before* doing any of the
  digit-splitting arithmetic or blit calls - but the ported versions had
  that gating moved to the gameplay flip's call site instead (redundant
  with, not replacing, self-gating) and the *attract-screen* flip
  (`trisFlipIntro()`, which has no such call-site gating at all) called
  all three completely unconditionally across all 1024 pixels/frame,
  reintroducing the exact O(pixels x objects) cost this session's other
  ports had already learned to avoid - except this time in a *newly*
  written function, not a retrofit gap. Fixed by moving the bounds checks
  back inside the three functions themselves (matching upstream exactly),
  so both flip functions get the cheap short-circuit regardless of
  call-site gating. (2) The user separately reported the flicker
  persisting specifically while the blinking on-screen START button was
  in its visible half of the cycle - `Recupe_Start_TTRIS` upstream has
  *no* position gate at all (only the blink-timer check), so ported
  as-is it called `blitzSprite` twice per pixel across all 1024 pixels
  whenever the button was visible, with nothing but the timer condition
  bounding the cost; added a position bounds-check (`x` in [49,78], `y`
  page in [3,5], the button's own footprint) not present upstream, since
  upstream never needed one to stay inside a hard instruction budget.
  Also applied this session's dirty-flag-caching lesson *proactively*:
  `trisCompositeGridIntoBuffer()` now only recomputes when
  `trisGridDirty` is set (done at the single choke point every grid
  mutation already passes through, `trisChangeGridStat()`, plus
  `trisInitAllVar()`'s direct full-grid reset) and otherwise reuses a
  persistent `trisGridCache[1024]`, addressing the user's report that CPU
  usage climbed further once the board had many locked blocks (the
  locked-grid composite is the one render cost that scales with how full
  the board is, and was being redone every frame even while a piece was
  merely falling and nothing about the grid had changed). Verified: dense
  consecutive-frame screenshots across a full blink cycle now show every
  frame fully drawn (both with and without the START box visible), and an
  extended play session (movement, rotation against the left wall,
  soft-drop, piece lock, score update, preview-piece advance) rendered
  correctly with no artifacts.
  **A further report, after the two fixes above**: the user still
  considered the attract screen's blinking START button to be "constantly
  redrawing" while it's visible, and pointed out this wasn't something a
  screenshot could prove or disprove either way (a still image can't show
  whether a *sequence* of identical frames was recomputed from scratch
  each time or not) - the actual fix needed was architectural, not a
  render-cost optimization within an already-redrawing frame. The whole
  attract screen (`trisFlipIntro()`) was still being called
  unconditionally every engine frame regardless of whether anything on
  screen had actually changed - correct in output, but wasteful the same
  way recomputing an unchanged locked-grid composite every frame would be
  (see the dirty-flag-caching lesson elsewhere in this file), just
  applied at the whole-screen level instead of one render layer. Fixed
  with a `trisAttractDirty` flag plus a tracked `trisAttractBoxVisible`
  boolean: the frame-counter logic that steps `trisIntroTimer1` now only
  sets the dirty flag when the derived visible/hidden boolean actually
  flips, and `trisFlipIntro()` is only called (and the flag cleared) when
  dirty - so the attract screen draws once on the exact frame the button
  needs to appear, holds that identical frame while nothing changes, then
  draws once more on the exact frame it needs to disappear, instead of
  recomputing an identical frame 60 times a second. Verified the state
  machine logic by inspection (exactly one call site for `trisFlipIntro()`,
  gated correctly, `trisAttractDirty`/`trisAttractBoxVisible` reset
  correctly in `trisBeginAttract()` so returning to the attract screen
  after a game over still redraws immediately) and confirmed the blink
  still renders correctly and fire-press still transitions into gameplay
  cleanly - did not attempt to re-prove the original "redrawing" report
  via screenshots, since the user had already correctly pointed out a
  static capture can't distinguish "redrawn every frame" from "held
  frame" when the two look pixel-identical.
  Tiny Tris is also the 10th game added to
  the menu - the trigger for finally needing `src/menu.c`'s pagination
  (added proactively ahead of this port, anticipating the menu was "right
  at the limit" of one screen's worth of entries): LEFT/RIGHT now jump a
  whole page at a time (9 games/page, wrapping past the first/last page),
  with a "PAGE n/total" indicator shown only when there's more than one
  page - verified via screenshot that page 1 still shows the original 9
  games unchanged and page 2 correctly shows just Tiny Tris.

**Menu game-select thumbnails, added after all 10 games shipped**: the
menu now shows a real gameplay screenshot of the currently-selected game,
switching immediately as the selection moves, in space freed up by moving
the game list itself close to the left edge (small margin only, was
previously centered further right). Asset pipeline, built from scratch for
this feature:
- Captured one real *gameplay* screenshot per game (not a title/attract
  screen) via the existing Puppeteer/WebGL test harness, at a 640x360
  viewport (native resolution, so no scale-factor math is needed when
  cropping) - each game needed its own tailored button sequence to reach
  an actual playing state (some go straight to gameplay off one fire
  press, several needed 2-3 presses through a logo/title/ready screen
  first, matched by trial and error per game rather than assumed).
- Cropped each to the actual 640x320 game area (stripping the 20px
  top/bottom letterbox bars every game already renders inside) and
  downscaled to 256x128 (40% of 640x320, per the requested size) with
  ImageMagick.
- **First attempt used a 256x1280 vertical strip (all 10 stacked
  straight down) - this would have exceeded Vircon32's hard 1024x1024
  texture dimension cap** (confirmed directly in the emulator's own
  source, `V32Console.cpp`: "Cartridge texture does not have correct
  dimensions (1x1 up to 1024x1024 pixels)") - caught before ever trying
  to load it, not discovered via a crash. Rebuilt as a 4-column x 3-row
  grid instead (1024x384 - comfortably inside the cap), with the 10 real
  thumbnails filling reading order left-to-right/top-to-bottom and the 2
  trailing unused cells left as plain black filler.
- New asset `assets/thumbnails.png` -> `obj/thumbnails.vtex` (via
  `png2vircon`, wired into `Make.sh`/`Make.bat`/`rom.xml` alongside the
  existing `columns.png`/`columns.vtex` atlas) - loaded as a *second*
  cartridge texture (id 1, since `rom.xml`'s `<textures>` list order
  assigns ids) - `md_initVideo()` (`src/portVircon32.c`) defines 12
  regions across it via `define_region_matrix()` (ids 0-9 usable, matching
  `addGames()`'s own registration order in `menuGameList.c` exactly, so
  "region id == game index" needs no separate lookup table).
- New `md_getThumbnailCount()`/`md_drawGameThumbnail(gameIndex, x, y)`
  primitives (`machineDependent.h`/`portVircon32.c`) - `menu.c` calls the
  latter once per frame with the current `selection` at a fixed position
  in the freed right-side margin, gated on the former so a future 11th+
  game without a baked thumbnail yet just draws nothing there instead of
  reading out of range. Confirmed no texture-selection leakage between
  the menu's own draw (which explicitly selects texture 1 for this call)
  and a subsequently-launched game's own rendering (`md_beginFrame()`
  already unconditionally re-selects texture 0 every frame regardless of
  whatever the menu left selected) - verified via screenshot: launching
  Tiny Pinball from the menu still renders its own title screen
  correctly, and returning to the menu afterward still shows the correct
  thumbnail for whichever entry is selected, with no corruption either
  direction.
- **Reference/precedent**: the user's sibling project `retrotime_vircon32`
  already has its own, more elaborate version of this same idea (a
  `CImage`/`Texture` abstraction loading pre-made "gamepreview" screenshot
  atlases for a scrolling title-screen background effect) - confirmed
  pre-baked screenshot assets is the established pattern for this kind of
  feature across the user's own Vircon32 projects, but that project's
  heavier abstraction layer (depends on its own `texture.h`/
  `SDL_HelperTypes.h`) wasn't pulled in here - this project kept using its
  own existing simpler direct `video.h` pattern (the same one
  `columns.png`/`COLUMNS_TEXTURE_ID` already established) rather than
  importing a second asset framework for one feature.

**Thumbnail grid grew from 4x3 to 4x4 when Tiny Minez shipped as the 13th
game**: the original 12-cell grid (1024x384) was exactly full, so a 13th
region needed a new row rather than fitting in existing slack. Extended
`assets/thumbnails.png`'s canvas to 1024x512 (still comfortably inside the
1024x1024 cap - room for several more games before another row is needed),
composited the new screenshot into cell 12 (region ids stay row-major, so
this is still just "next reading-order slot" with no renumbering of the
existing 12), and bumped `portVircon32.c`'s `THUMBNAIL_GRID_ROWS` (3->4)
and `THUMBNAIL_COUNT` (12->13) to match - `THUMBNAIL_GRID_COLS` (4) was
untouched, so `define_region_matrix()`'s region-id assignment for the
existing 12 thumbnails is unaffected by the new row. Verified via
screenshot that all 12 existing thumbnails still render correctly at their
original positions and the new Tiny Minez thumbnail appears correctly
when it's the selected entry (9th alphabetically, last on menu page 1).

**Tiny Missile's own thumbnail was initially forgotten entirely** - shipped
without one for a while until the user directly pointed out the menu
still showed no screenshot for it, exactly the standing per-port step this
project's own memory already calls out. Fixed the same way as every prior
addition: captured a real gameplay screenshot (crosshair, a missile trail,
and an interceptor detonation flash all visible in one frame), cropped/
downscaled to 256x128, and composited into cell 13 - still inside the
existing 4x4 grid (14 of 16 cells now used, 2 free), no grid growth
needed this time. `THUMBNAIL_COUNT` bumped 13->14. Verified via
screenshot that Tiny Minez's own thumbnail (cell 12) was untouched and
Tiny Missile's new one displays correctly when selected.

**Two follow-up layout tweaks to the same menu, requested right after**:
(1) the 3 header lines (title + the 2 control-hint lines) were each at a
hand-picked fixed x that only happened to look centered for their
original text - once the page-hint line's own text grew a variable-length
"N/total" suffix, that stopped holding up in general. Replaced with a
small `menuCenteredX(text)` helper (`strlen(text) * bios_character_width`
against `video.h`'s own `screen_width`) so all 3 lines - including the
page-hint line, whose length actually varies with the page/total digit
count - stay genuinely centered regardless of content length. (2) the
thumbnail was top-aligned with the list (`y = LIST_AREA_TOP`) - moved to
be vertically centered within the *whole* list/selection area instead
(`LIST_AREA_TOP` down to `screen_height`), so it sits at the same on-screen
position on every page regardless of how many entries that page actually
has (a 1-entry page like Tiny Tris's own page 2 doesn't pull the
thumbnail up to hug just that one entry). `MD_THUMBNAIL_WIDTH`/
`MD_THUMBNAIL_HEIGHT` were promoted from `portVircon32.c`-local defines to
`machineDependent.h` so `menu.c` could reference the real thumbnail size
for this centering math without duplicating the constant.

**"BY <author>" credit line added under the thumbnail, requested much
later in the same session**: `struct Game` (`menu.h`) gained an `author`
field, `addGame()` a new parameter for it - each game in `menuGameList.c`
passes its actual credited author ("OBONO" for NumberPlace/2048/
HollowSeeker, "DANIEL C / SVEN B" for Tiny Invaders, "DANIEL C" for most
of the rest, "SVEN B / LORANDIL" for Tiny Minez - see this file's own
Licensing section and the per-game Status entries above for how each
credit was determined, since more than one turned out to need both names
rather than a single guessed one).
Rather than just append the text below the thumbnail at a fixed offset
(which would un-center the thumbnail-plus-text group as a whole,
re-introducing the exact asymmetry the layout tweap above fixed), the
vertical-centering math was extended to treat the thumbnail and the
author line as one combined block: `blockHeight = MD_THUMBNAIL_HEIGHT +
authorGapY + bios_character_height`, centered within the same
`LIST_AREA_TOP`-to-`screen_height` span the thumbnail alone used before -
the thumbnail draws at the block's top, the author text (`strcpy`+
`strcat`'d as `"BY " + author` each frame, matching the existing page-
hint text-building pattern) at its bottom, horizontally centered within
the thumbnail's own width (not the full screen, since the thumbnail sits
in the right-side margin). Verified via screenshot across four games
(2048/HollowSeeker/NumberPlace/Pinball, spanning both `OBONO`- and
`DANIEL C`-credited titles and both menu pages) that the credit line
renders correctly centered under each thumbnail and the whole block stays
vertically centered regardless of which game is selected.

**Menu list sorted alphabetically by title, requested right after** -
with an explicit heads-up from the user to watch out for the thumbnail
mapping specifically, which turned out to be exactly the right thing to
watch for. `games[]` itself is *not* reordered - it stays in
`addGame()`'s own registration order, since that order is also what the
thumbnail atlas's region ids were baked against (region id ==
registration index) and what a launched game's `init`/`update` function
pointers are looked up by (`menu_getGame()`). Reordering `games[]`
directly would have silently scrambled both. Instead, added a separate
`int[MAX_GAMES] displayOrder` indirection array (`menu_buildDisplayOrder()`,
a small selection sort by `strcmp`-ing `games[].title` - `gameCount` is
always a handful of entries, so O(n^2) is irrelevant here), built once on
the first `menu_init()` call (guarded by a `displayOrderBuilt` flag, since
`menu_init()` also runs every time the player returns from a game and
`games[]`/`gameCount` never change after boot). `selection` continues to
mean "display position" exactly as before (paging/up-down math is
unchanged) - every place that used to read `games[selection]` or draw
`selection`'s own thumbnail now goes through `displayOrder[selection]`
first to get back the real registration index, and the value
`menu_update()` returns to `main()`'s dispatch loop (which launches
`menu_getGame(chosen)`) was switched from `selection` to that same
resolved index - otherwise the menu would still *display and launch*
correctly-corresponding text, but show a mismatched thumbnail, or worse,
launch a different game than the one whose title was highlighted, the
exact class of bug the user's warning was pointing at. Verified via
screenshot across the reordered list (now 2048 -> HOLLOWSEEKER ->
NUMBERPLACE -> TINY BERT -> TINY BOMBER -> TINY DOC -> TINY INVADERS ->
TINY PACMAN -> TINY PINBALL -> TINY TRIS, digits sorting before letters):
each selected entry's thumbnail matched its own title (spot-checked 2048,
Tiny Bert, Tiny Tris), and firing on the reordered first entry ("2048",
registration index 2, no longer index 0) actually launched 2048 itself
rather than whatever used to sit at position 0.
- `src/games/gameTinyArkanoid.c` - Tiny Arkanoid (Daniel C, GPLv3). Fifth
  Tier-2 game (a Breakout/Arkanoid clone - a paddle on the left edge moves
  up/down, launching a ball that bounces around a 6x5 block grid on the
  right). Picked as one of the two lowest-goto (goto=8) untried titles
  (Tiny Trick, the other, is meaningfully bigger - 566 vs 366 lines - so
  Arkanoid went first). Data tables extracted programmatically as usual.
  Two things ported directly rather than needing the usual retrofit:
  built per-object footprint gating into the render loop from the start
  (every layer here already self-gates internally, matching upstream's
  own shape, unlike Bomber/Pacman's re-scanned sprite lists), and
  replaced a stateful per-column texture-tiling counter (`SWIFT_TEXTURE`,
  reset at x==0 and incremented per call) with a direct `(x+1) % 15`
  computation after tracing through its exact reset/increment order and
  confirming the two are byte-identical - avoids a stateful global with a
  call-order dependency this port's own compositing might not preserve.
  The intro tune (`PLAYMUSIC()`, 46 notes) and the shorter life-lost/
  level-clear cues are genuinely sequential blocking `Sound()` calls
  upstream - built a small shared frame-stepped note sequencer
  (`arkStartNoteSeq`/`arkAdvanceNoteSeq`) that fires one note at a time,
  waiting each note's own real duration (computed with the exact same
  freq/dur formula `Sound()` already uses) before advancing - the first
  time this project has had to actually sequence real multi-note music
  rather than substitute a handful of simultaneous representative tones.
  **Two real bugs found via actual play, both from this game's own
  structural shape rather than a retrofit gap**:
  (1) the ball and paddle moved *extremely* slowly - traced to upstream's
  own `Frame` counter incrementing every single uncapped loop iteration
  (with only the *redraw* throttled to `Frame%32==0`), so paddle-read/
  ball-update modulo checks fire far more often, in real time, than the
  display refreshes - porting this the same way every *other* game's own
  tick counter was ported here (one increment per real 60fps frame,
  since those games run their whole logic+redraw together each tick with
  no internal throttle of their own) silently made the whole game ~8x
  slower than intended, since this is the first port where the
  logic-tick rate and the redraw rate were two genuinely different things
  upstream. Fixed by decoupling them: `gameTinyArkanoid_update()`'s
  playing-state branch now runs the paddle/ball logic body in a small
  inner loop (`ARK_TICKS_PER_FRAME` times per real frame) while still
  calling the render function only once at the end - restores a normal
  arcade pace without changing the redraw rate. Worth checking for on any
  future port whose upstream loop has its *own* internal tick counter
  separate from its own redraw gate, rather than assuming every game's
  timing model matches the ones already ported. (2) still reported
  hitting 100% CPU after the speed fix - the render loop calls 6 layer
  functions unconditionally for all 1024 pixels/frame (6144 calls total),
  and even though every layer already self-gates internally (bounds-check
  before real work, matching upstream), that self-gating only cuts the
  *work per call*, not the *call count* - without `v32opt`'s inlining
  (this project's default dev build uses `SKIP_V32OPT=1`), raw per-call
  overhead multiplied by 6144 calls/frame was apparently the dominant
  cost. Fixed by gating the call *site* by row/x-range instead (same
  technique Tris/Bert/Doc already needed for their own layers) - since
  every layer here has an easily-computable narrow footprint, this cuts
  the call count to roughly the number of pixels each layer actually
  occupies (~1245 calls/frame total) instead of 1024 regardless of
  footprint. A generalizable lesson distinct from the earlier "self-
  gating must be checked per caller" one: *even a correctly self-gated
  function* still costs a full call every time it's invoked, so a layer
  with a small footprint should still be gated at the call site when it's
  invoked at every one of 1024 pixels - self-gating avoids wasted *work*,
  call-site gating avoids wasted *calls*, and a naive port can need both.
  A later request also swapped which d-pad direction moves the paddle
  which way - upstream's own `TrackBary`/`TrackBaryDecal` wiring has
  increasing values move the paddle to a larger page index (further down
  the screen), so a first, faithful port had UP move the paddle down and
  vice versa; this reads as inverted on a real gamepad even though it
  matched upstream exactly, so the mapping was swapped (and LEFT/RIGHT
  added as aliases for UP/DOWN, since the game's own art - title screen,
  side panels - is drawn sideways as if meant to be played with the
  display physically rotated, which Vircon32's screen can't do).
- `src/games/gameTinyTrick.c` - Tiny Trick (Daniel C, GPLv3). Twelfth game,
  ported after the quit-confirmation dialog and further Tris/Doc bug fixes
  (see below) - an air-hockey game: player (white) and computer (black)
  paddles knock a puck around a rink, scoring past a goalie into the left/
  right goal, first to 10 wins. Same `FastTinyDriver.h` button-read pattern
  as every other Daniel-C game (free 2D paddle movement via
  isUp/Down/Left/RightPressed(), Fire hits the puck while "dragged").
  Structural decisions: upstream's `Recupe()` composite is *subtractive*
  (`background & ~sprite1 & ... & ~sprite5`, forcing every sprite
  silhouette to solid black over the busy rink texture) rather than the
  OR-based composite every other game here uses - by De Morgan's law
  (`~s1 & ~s2 & ... = ~(s1|s2|...)`), ORed all 5 sprites into one shared
  per-page buffer exactly like every other game's own compositing
  (`trkCompositeSpritesIntoBuffer()`), then inverted and ANDed the
  *combined* mask against the background once at the end - same simpler
  shared-buffer shape, just applied to a subtractive composite. Built
  per-object-per-page footprint gating into the 5-sprite composite from
  the start (not a later retrofit), applying Arkanoid's freshly-learned
  lesson that even self-gated functions need call-site gating. Converted
  upstream's blocking `SCREEN_GOAL()` slide-in/hold/slide-out animation
  (13 chained `intro()` calls plus two `_delay_ms()`s) into an explicit
  `TRK_STATE_GOAL_SLIDE_IN/HOLD/SLIDE_OUT/HOLD2` sub-state sequence, one
  step per real frame - the usual "blocking loop -> resumable state"
  treatment. The title screen only redraws on two exact blink-timer values
  (matching upstream's `switch(TIMER){case 0:...;case 128:...}` with no
  `default`) - same shape as Tris's attract screen and the obono games'
  idle screens (see the dialog write-up below), so this port also exposes
  an `onResume` hook (`gameTinyTrick_forceRedraw()`) from the start rather
  than waiting for a bug report.
  **Two-round CPU fix for the goal-scored scoreboard, both from real user
  reports after shipping**: (1) `trkIntro()`'s goalScreen 1/2/3 branches
  called `trkBlitzSprite` 2-4x per pixel unconditionally across all 1024
  pixels/frame with zero position gating - the same shape as Arkanoid's
  call-count bug. Fixed by adding row+x-range gating (`panelRows`/
  `numRows`/`winLoseRows`, computed once per row) around each call. (2)
  reported still at 100% CPU after that fix - even gated, the composite
  still recomputed `trkCompositeSpritesIntoBuffer(y)` fresh every one of
  the ~90 frames of the goal sequence, despite all 5 sprites being
  completely frozen (no physics runs) for that whole sequence - pure
  wasted recomputation of an unchanging result. Fixed with a freeze-cache:
  `trkFreezeSpriteMask()` computes the composite once, on the exact frame
  the goal sequence begins (called right before entering
  `TRK_STATE_GOAL_SLIDE_IN`, both from the normal in-play goal-detection
  branch and matching the same call the win/lose screen path already
  needed), storing the result in a persistent `trkFrozenMaskCache[1024]`;
  `trkIntro()`'s background-composite branch then just reads the cache
  instead of recomputing, for every frame of the sequence.
  `trkTinyFlip()` (real gameplay) resets `trkMaskFrozen = false` at its own
  top so the cache is only trusted while genuinely frozen. Also inlined
  `trkPatinoire1_2(x,y)`'s background-mirroring lookup (`x>63` folds to
  `127-x`) directly into both `trkTinyFlip()`'s and `trkIntro()`'s pixel
  loops, removing 1024 extra function calls/frame each - a background
  lookup this simple doesn't need its own call, unlike every other game's
  background reads here which are already a plain array index with no
  wrapper. This is the first port in this session where a *fully frozen,
  multi-frame sequence* (not just a single static screen) got its own
  dedicated cache, distinct from the dirty-flag caching Doc/Tris use for
  a mostly-static-but-occasionally-changing grid - worth reaching for
  specifically when a whole run of frames is provably identical, not just
  "usually unchanged". Verified via a temporary debug hook that jumped
  `gameTinyTrick_init()` straight into `TRK_STATE_GOAL_SLIDE_IN` with
  preset scores (bypassing the normal goal-scoring trigger, which didn't
  reliably reproduce within automated screenshot timing) - screenshots
  across the full slide-in/hold/slide-out sequence showed a clean
  bordered scoreboard panel with correct digits and no artifacts, and the
  rink/background rendered correctly both during and after the sequence;
  removed before shipping, then re-verified the normal title -> pre-play
  -> playing flow still worked correctly on the debug-free build.

**Quit-confirmation dialog, added after all 11 games shipped**: pressing
Start while a game is running no longer instantly quits to the menu - it
now opens a black-and-white "CONFIRM / QUIT TO MENU? / YES / NO" dialog
box first (`drawConfirmQuitDialog()` in `src/portVircon32.c`), and the
current game's own `update()` is not called at all while it's open (so
gameplay is genuinely frozen, not just visually paused) - Left/Right
toggles the selection (defaults to NO, the safer choice), Fire confirms,
and Start again also cancels.
- **Rectangle drawing**: `video.h` has no fill-rectangle primitive of its
  own (only region blits) - added `md_drawSolidRect()`, reusing the
  columns atlas's own region 255 (byte value 0xFF, an already-loaded
  solid `TILE_W x TILE_H` white tile) tinted via `set_multiply_color()`
  and stretched to any size via `set_drawing_scale()` +
  `draw_region_zoomed_at()` - the same technique the sibling
  `crisp-game-lib-portable_vircon32` project's own `md_drawRect()`
  already uses (confirmed by reading its `portVircon32.c`), just built on
  this project's existing atlas instead of a dedicated 1x1 texture asset.
  White outline + a smaller inset black rectangle gives the bordered box.
- **The same physical Fire-press that confirms the dialog must not bleed
  into whatever comes next** - `md_armInputFireGate()` (already built for
  the menu->game transition) is called at the moment the dialog resolves,
  since `md_inputFire()` is a single shared gate every `isFirePressed()`
  call and the menu's own fire-read both go through. Verified empirically
  (not just by reading the code) with a Puppeteer test that held Fire
  continuously across the entire confirm+destination-arrival window: with
  Tiny Arkanoid, confirming NO with Fire held did not auto-launch the
  ball on resume (it stayed sitting at the paddle exactly as before,
  launching only on a genuinely later fresh press); confirming YES with
  Fire held landed cleanly on the menu without instantly relaunching
  whatever was highlighted.
- **A user question about screen capture led to finding a real, more
  serious bug than the one being asked about.** The user asked whether the
  game's own screen could be captured right before the dialog and
  redisplayed after it closes. Vircon32 has no readback API at all (only
  draw/blit calls), so a literal capture isn't possible - but the
  practical effect doesn't need one: freezing the game's own `update()`
  while the dialog is open already leaves its last real frame sitting
  underneath the dialog untouched, and resuming just calls that same
  `update()` again against unchanged state, which reproduces the
  identical frame. *This reasoning quietly assumed every game
  unconditionally redraws its whole screen on every `update()` call* - the
  user immediately pushed back ("certain games don't redraw every frame")
  and asked whether other games had this same issue, specifically
  mentioning obono's logo screens. Checking confirmed both things: (1)
  Tiny Tris's attract screen has exactly this gap (`trisAttractDirty`
  skips the whole draw call when nothing changed - already known from
  this session's own dirty-flag-caching work, but not cross-checked
  against the new dialog feature); (2) worse, `obonoCoreShim.c`'s own
  shared `refreshScreen()` - used by NumberPlace, HollowSeeker, *and*
  t2048 - has the identical shape (`if (!isInvalid) return;`), and
  `isInvalid` is only set `true` at specific state-changing moments across
  all three games, never unconditionally every frame. On an idle static
  screen (a difficulty-select prompt, a "PRESS BUTTON" title) nothing ever
  re-invalidates it, so this is actually *worse* than Tris's version -
  Tris self-corrects within a few frames since its own frame counter keeps
  ticking, but an obono game's logo/prompt screen could stay stuck
  showing the dialog's leftover pixels indefinitely, until the player
  happens to do something that changes state. Reproduced directly:
  opening the dialog over NumberPlace's difficulty-select screen and
  cancelling left the dialog box fully visible with nothing redrawn
  underneath, confirmed via screenshot before any fix.
- **Fix**: added an optional `onResume` hook to `Game` (`menu.h`'s
  `struct Game`, `addGame()`'s new 4th parameter - `NULL`/0 for the 8
  games that don't need it) called once when a game resumes from the
  dialog (`portVircon32.c`'s dispatch loop, both the NO-via-Fire and
  Start-cancels-again paths). `obonoCoreShimForceRedraw()` (new,
  `obonoCoreShim.c`/`.h` - just sets `isInvalid = true`) is wired as the
  shared hook for NumberPlace/HollowSeeker/t2048 since the flag they all
  need reset lives in the shim, not per-game; `gameTinyTris_forceRedraw()`
  (new, `gameTinyTris.c` - sets both `trisAttractDirty` and
  `trisGridDirty` true, harmless regardless of which state Tris was
  actually paused in) covers Tris the same way. Verified via screenshot
  that both NumberPlace (difficulty-select screen) and t2048 (title
  screen) now redraw cleanly with no dialog remnants after cancelling.
  **Generalizable lesson**: a "does this game skip its own redraw when
  idle" audit needs to cover shared shim code, not just each game's own
  file - the obono bug lived in `obonoCoreShim.c`, affecting 3 games
  identically, and would have been easy to miss checking games one at a
  time.

**Same onResume audit, extended to every game shipped since, much later
in the session** - the user directly pointed out that games ported after
this dialog existed (Tiny Minez, Tiny Missile, Tiny Bike, Tiny Arena,
Tiny Gilbert - all still registering `NULL` as their 4th `addGame()`
argument) had never actually been checked for this bug class, only
assumed fine by omission. Audited each game's own `update()` state
machine directly rather than guessing from the symptom:
- **Tiny Minez**: every state branch calls its own render function
  unconditionally - confirmed `NULL` is correct, not an oversight.
- **Tiny Missile**: the only redraw skip is its global
  `TMIS_TICK_DIVISOR=3` tick throttle, self-correcting within 3 real
  frames regardless of dialog interference - left as `NULL`, a hook would
  fix an effectively imperceptible case.
- **Tiny Bike**: `BIK_STATE_WAIT_RELEASE` has no timer of its own (waits
  for Fire to release) - real, indefinite-persistence risk.
  `BIK_STATE_LEVEL_WIN_WAIT`/`GAME_OVER_WAIT` both self-correct, but only
  after a couple real seconds - still visibly wrong for that whole
  window. Fixed with `gameTinyBike_forceRedraw()` (a `bikForceRedraw`
  flag, checked at the top of all three branches).
  **A follow-up bug in this exact fix, found via direct user report**
  ("press enter to show confirm dialog and choose no it will show 'next
  race' text" after reaching the finish and sitting idle): the initial
  fix redrew with `bikTinyFlip(2)` - the same generic mode ATTRACT/
  LEVEL_INTRO_WAIT use, which draws `bikIntroPic` full-screen - but
  `bikIntroPic` is a shared global reassigned *ahead* of when it's
  actually shown: `bikNextLevel()` sets it to `bikNEXTRACE` right as the
  level-intro countdown finishes (transitioning into `WAIT_RELEASE`,
  before the race even starts), preparing that picture for whichever
  `LEVEL_INTRO_WAIT` screen comes *next* - not the state the player is
  actually sitting in. Since none of these three states normally redraw
  at all (the frozen real gameplay frame - bike at the finish line, or
  at the crash point - just persists, which is correct), the stale
  `bikIntroPic` value was invisible until this fix started actively
  redrawing with it, surfacing "NEXT RACE" prematurely over what should
  have stayed the frozen finish-line frame. **Fixed** by redrawing with
  mode 0 (the real gameplay composite, reproduced from still-current
  sprite/track state) instead of mode 2 at all three call sites -
  confirmed by reading `bikAdjustVarScroll()`/`bikCompositeSpriteMapRow()`
  that neither has any per-call side effect, so an extra redraw of
  already-current state reproduces the exact same frozen frame safely.
  Diagnosed and fixed by code inspection only, per the user's own
  request, without testing in the emulator.
- **Tiny Arena**: `AR_STATE_GAMEOVER_WAIT_RELEASE` is a direct port of
  upstream's own busy-wait with **no redraw at all** - and it's the
  state reached right at boot (doubling as the attract screen), so this
  was the highest-risk finding of the whole audit. Fixed with
  `gameTinyArena_forceRedraw()` (an `arForceRedraw` flag, calling
  `arTinyFlip()` once when set).
- **Tiny Gilbert**: `GILB_STATE_TITLE_WAIT` (the actual title/attract
  screen, no timer, indefinite risk) and the intro-jingle/wait states
  (bounded to a couple seconds, fixed for consistency) all share the same
  static logo picture - fixed with `gameTinyGilbert_forceRedraw()`
  (a `gilbForceRedraw` flag, redrawing via `gilbIntro()`).
  Verified via screenshot: cancelling the dialog from Gilbert's title
  screen redraws it cleanly with no dialog remnants (byte-for-byte the
  same frame as before the dialog opened); cancelling from Arena's boot
  screen also redraws cleanly - a follow-up check confirmed the "START"
  text's normal blink cycle (visible/blank/visible) continues correctly
  afterward, so an initial blank-looking frame right after cancelling
  was just ordinary blink timing landing on its "off" phase, not a
  regression, rather than assuming the fix had failed.
- `src/games/gameTinyMinez.c` - Tiny Minez (credited in the menu as "SVEN B
  / LORANDIL" - the `.ino`'s own header says "Programmer: Sven B" but
  lists contact email Lorandil@gmx.de, the same author already credited
  for Tiny Invaders; unclear whether that's a pen name or a separate
  collaborator, so both names are credited rather than guessing. GPLv3,
  same `tinyJoypadShim`/`FastTinyDriver.h` lineage as
  Tiny Invaders/Pinball/Pacman/Bomber/Doc/Bert/Tris/Arkanoid/Trick). A
  Minesweeper clone on a fixed 12x8 grid, 4 difficulty levels (5/10/15/20
  mines). The most structurally novel port so far: upstream's `Game`/
  `Selection` are real C++ classes (`TinyMinezGame.h`/`.cpp`,
  `Selection.h`/`.cpp`) - the first port in this project to actually do
  the class-to-plain-function conversion that had kept TinyDungeon/Tiny
  DDug/Tiny SQuest deferred as "too complex" until now, converting to
  flat `tmz*` functions operating on global state instead of a
  `this`-bound object (the same treatment those three still-deferred
  games will eventually need). Several of upstream's states were
  themselves blocking multi-frame loops despite the outer structure
  already being switch-based - the difficulty-select screen's own
  `do {...} while(!isFirePressed())` plus an internal
  `waitUntilButtonsReleased()` busy-wait, and the "hold Fire to flag, tap
  to uncover" gesture's `do {...} while(isFirePressed())` hold-duration
  timer - both converted to non-blocking per-frame edge-detection/tick-
  counters, the same pattern established by NumberPlace's own
  `NP_SHORT_PRESS`/`NP_LONG_PRESS`. Added one new shared shim primitive,
  `isFire2Pressed()` (`tinyJoypadShim.h/.c`, backed by a new
  `md_inputFire2()`/`bool gamepad_button_b()` in `machineDependent.h`/
  `portVircon32.c`) - the first game needing Vircon32's B button, for an
  instant flag-toggle alongside the long-press alternative (matching
  upstream's own "count = isFire2Pressed() ? 255 : 0" trick, which forced
  its long-press branch unconditionally rather than needing a second
  gesture path). Upstream's 6 full-screen splash bitmaps (title, rules,
  difficulty-select, boom, game-won, awesome) are RLE-compressed in
  PROGMEM, a real AVR-flash-size concern this project doesn't share -
  rather than port `uCompression.cpp`'s `pgm_RLEdecompress256()` codec at
  runtime, all 6 were decompressed *once*, offline, via a small Node
  script into flat 1024-byte arrays, verified byte-for-byte against the
  decompression algorithm's own documented control-byte format (top 2
  bits select literal-run/generic-repeat/repeat-0x00/repeat-0xFF, bottom
  6 bits are the run length) rather than trusting a first attempt - two
  real bugs were caught this way before the port ever compiled: a CRLF
  line-ending bug in the extraction script's own comment-stripping regex
  (JavaScript's end-of-line anchor not matching before a trailing `\r`
  under Windows line endings - fixed by dropping the anchor), and an
  incorrect assumed size for the dashboard
  bitmap (512 vs the actual/correct 256 = 32px wide x 8 page-rows).
  Also confirmed (by directly reading it) that upstream's hand-written
  AVR-assembly decompression fast-path (`uDecompression.S`) is a 1:1
  translation of the plain-C branch also present in the same file and
  used only on non-AVR targets during upstream's own development/testing
  - so no assembly semantics needed understanding or porting at all.
  RNG: upstream's `seed`/`incrementSeed()` fields look like a bespoke
  PRNG but are actually just an entropy source (incremented every idle
  frame while polling for input, so `seed` captures "how long the player
  took to press Fire"), fed once into stdlib `randomSeed()`/`random()`
  right before mine placement - ported as a direct call to the shared
  `arand(n)` helper instead, needing no such manual seeding ritual (same
  reasoning as every other port here). The flood-fill `uncoverCells(x,y)`
  is kept as upstream's own iterative "repeatedly rescan the whole board
  until a pass finds nothing new" algorithm (its own comment explains
  this was chosen specifically over a simpler recursive version to avoid
  overflowing the ATtiny85's tiny stack - confirmed with a real
  screenshotted stack-overflow bug in upstream's own README) rather than
  rewriting it recursively, since Vircon32's ample stack removes the
  original constraint but the iterative version is already correct.
  **A real fire-input bug found via live Puppeteer testing**: confirming
  a difficulty selection immediately, incorrectly uncovered the center
  board cell too (`Clicks: 01` visible with zero actual player input in
  the new PLAYING state) - the same physical button press that confirmed
  DIFFICULTY was being read a second time as PLAYING's own edge-triggered
  "tap to uncover" release gesture, since upstream explicitly busy-waits
  for that confirming press to be *released* before ever entering the
  play loop (a blocking mechanic this port's non-blocking edge-detection
  didn't originally replicate). Fixed with a local fire-gate
  (`tmzFireGateActive`, mirroring `md_armInputFireGate()`'s existing
  menu-to-game handoff pattern but applied *within* a single game's own
  state machine) - forcing `fire = false` both for every later tick while
  the gate is armed *and*, critically, on the very same tick the gate is
  armed (missed on the first attempt: forcing it only on later ticks left
  `tmzPrevFire` still `true` from the arming tick, so the next gated
  `fire=false` read was itself misread as a fresh release edge one tick
  later, reproducing the identical bug). Verified fixed via screenshot
  showing a fully-hidden fresh board (`Tiles: 96, Clicks: 00`) immediately
  after confirming difficulty. Full gameplay verified live afterward
  (Puppeteer + the WebGL build): cursor movement clamped correctly at all
  4 board edges, quick-tap uncover with correct flood-fill, both flag
  methods (`isFire2Pressed()` instant toggle and the long fire-hold,
  each independently confirmed to add/remove a flag and update the
  dashboard's flagged-count "Mines" field - which, per upstream's own
  identical `getFlaggedTilesCount()`-driven dashboard call, tracks flags
  *placed*, not mines remaining, so reading "00" on a fresh board is
  correct/expected, not a bug), and the full BOOM_FLASH -> GAME_OVER
  sequence (bomb reveal via `tmzUncoverCellsMask(TMZ_BOMB)`, inverted-
  color board, held steady indefinitely without a confirming Fire press -
  exactly matching the code's own no-timeout `TMZ_STATE_GAME_OVER`
  branch). GAME_WON was not directly triggered during this session's
  testing (an automated full-board clear got most of the way there on
  Easy difficulty - down to a small, disconnected pocket the initial
  flood-fill never reached - before time/effort budget on that specific
  brute-force verification ran out) but was confirmed correct by direct
  code inspection: `tmzIsWon()`'s check and the `TMZ_STATE_GAME_WON`
  branch share the exact same render/sound/return-to-intro structure as
  the already-empirically-verified `TMZ_STATE_GAME_OVER` branch, just
  with different bitmap/sound assets.
- `src/games/gameTinyMissile.c` - Tiny Missile (Daniel C, GPLv3, same
  `tinyJoypadShim`/`FastTinyDriver.h` lineage). A Missile-Command-style
  game: a crosshair fires interceptor rockets to destroy incoming
  missiles before they hit one of 6 domes/cities; clearing every missile
  advances the level, losing every dome ends the game. Picked as the next
  lowest-effort Tier-2 candidate (goto=10) - but its own `CLASS_TMISSILE.h`
  turned out to use *real* C++ classes with single inheritance
  (`STATIC_SPRITE_ANIM_TMISSILE`/`CROSS`/`DEFENCE` all `: public
  STATIC_SPRITE_TMISSILE`), a complexity the goto-count triage doesn't
  capture (same blind spot that under-costed Tiny DDug earlier) - though
  a much smaller, flatter hierarchy than TinyMinez's own classes, so still
  tractable: flattened into plain structs with the base class's X/Y/ACTIVE
  fields inlined directly into each derived type, methods becoming
  `tmis*` functions taking an explicit pointer. `ARMY_TMISSILE::
  ATTACK_WEAPON()`'s blocking rapid-fire burst (a missile got through to
  the crosshair, auto-firing every remaining rocket) became a
  `tmisAttackBurstActive` flag ticking one rocket per real frame, main
  engine update skipped entirely while active (matching upstream's own
  complete freeze during the burst). Genuine upstream whole-loop
  `CONTROL_FRAMERATE(46)` throttle (~21.7fps) ported as a real
  `TMIS_TICK_DIVISOR=3` (~20fps) tick-skip from day one, per this
  project's own standing rule - the first port to actually follow that
  rule at write-time instead of retrofitting it after a report.
  **A cluster of real bugs found via live user play, in the order
  reported**:
  1. *"Explosions don't draw correctly"* - confirmed real: the interceptor
     detonation-flash sprite was rendered with `tmisSpeedBlitz` (upstream's
     own page-aligned blit, used correctly for the page-locked DOME
     sprite) instead of `tmisBlitzSprite` (the pixel-space, sub-page-split
     variant upstream *actually* uses for INTERCEPT, confirmed by re-
     reading the `.ino` directly) - since an intercept's Y position is
     inherited from the defence rocket's animated float pixel coordinate,
     not a page index, the page-aligned bounds check was comparing
     incompatible coordinate spaces and silently returning nothing for
     most of the sprite's real vertical range. Fixed by switching the
     call.
  2. *"Sounds play too long"* - two compounding causes. First, the shared
     frame-stepped note sequencer (same `tmisStartNoteSeq`/
     `tmisAdvanceNoteSeq` shape as Arkanoid's/Minez's own) can only ever
     advance one note per real 60fps frame - fine for a genuine melody,
     but the win/game-over cues are *computed sweeps*
     (`for(t=1;t<255;t++){Sound(50,2);Sound(t,2);}` and its mirror) with
     508/380 notes each, meaning a literal port would take a *minimum* of
     8.5/6.3 real seconds regardless of each note's own true duration -
     upstream's version only finishes in well under a second because each
     of its own Sound() calls is a genuinely *blocking* AVR bit-bang,
     which Vircon32's fire-and-forget async audio channel has no
     equivalent of. Fixed by downsampling the sweep's step size
     (`t+=8`/`t-=8` instead of `t+=1`/`t-=1`) rather than reproducing every
     literal step - same audible ascending/descending sweep effect,
     landing in a duration comparable to Arkanoid's own ~46-note intro
     jingle instead of a step count that was only ever fast because of
     AVR's blocking audio model. Second, a `waitFrames<1 -> 1` minimum-
     wait floor (copied defensively from Arkanoid's own sequencer) was
     stretching this game's own very short UI blips (~1-2ms true duration)
     out to at least a full 16.67ms frame each time - removed, since nothing
     in the sequencer's own logic actually needed a nonzero floor to stay
     safe.
  3. *Dome-explosion sound retriggering every animation tick* - upstream's
     `UPDATE_DOME_TMISSILE()` calls `SNDBOX_TMISSILE(5)` on all 6 ticks of
     a dome's explosion animation, harmless there since each of its own
     calls is a ~2ms blocking beep; ported verbatim, Vircon32's async
     channel instead retriggered audibly on every tick, sounding stuck.
     Fixed by sounding only on the tick the explosion actually starts
     (`frame==1`), matching what a player perceives as one "boom" rather
     than upstream's own AVR-inaudible repetition.
  4. *"When our base gets hit, missiles/lives (ammo) suddenly all go to
     0"* - a real burst-logic bug, not just the dramatic-but-correct
     arsenal drain: upstream's `ATTACK_WEAPON()` only enters its
     ammo-draining `while` loop if `ROCKET>0` *at the moment the burst
     starts*, and `USE_WEAPON()`'s own auto-refill-from-`SPARE` keeps that
     loop going until the *entire* arsenal (current clip + every spare
     clip) is exhausted; if `ROCKET` was already 0 when hit, upstream
     instead takes a single defensive shot straight from `SPARE`, no
     refill. An early version of this port checked `rocket>0` fresh on
     every tick instead of capturing "did we have rockets at burst-start"
     once, so the barrage incorrectly stopped (and misreported the
     single-shot sound) the instant the current clip ran dry mid-burst,
     instead of continuing into the spare clips. Fixed with a
     `tmisAttackBurstHadRocket` flag captured once in
     `tmisAttackWeaponStart()`.
  5. *"Seems to very easily hit 100% CPU constantly"* - this game's render
     pipeline never got the per-row call-site gating/per-page-buffer
     compositing pass every other tinyJoypadShim game here needed
     (Bert/Doc/Tris/Arkanoid/Bomber/Pacman) - 7 composited layers called
     unconditionally for all 1024 pixels/frame, several looping over
     multiple objects internally (4 missiles, 6 domes, 3 defence, 3
     intercepts), which had already caused a lower-severity symptom
     (the crosshair/domes occasionally, transiently missing from an
     otherwise-normal frame - game *state* untouched, just that one
     frame's drawing cut short by the 250,000-cycle/frame budget) before
     the user confirmed the CPU cost itself was the bigger problem. Fixed
     in two rounds: first, row-gating each narrow-footprint layer's call
     site (skip Dome/Cross/Shield/Intercept/Panel entirely on rows they
     can't touch - Dome only row 7, Panel only row 0, the rest computed
     from each object's own current position); second, converting each of
     those into a `tmisComposite*Row()` function writing directly into a
     shared `tmisPageBuffer[128]` for *just its own narrow column range*
     (dome width 15, cross width 3, defence width 2, intercept width 10)
     instead of scanning all 128 columns per object, plus the same
     treatment for missiles (bounded to each one's own `[min(x1,x2),
     max(x1,x2)]` span, usually far narrower than the full width since
     it's set by the RDLP oscillator's own ~22-60px cycle) with a
     guaranteed static skip of rows 0 and 7 (a missile's trail only ever
     spans rows 1-6, `pixel-y=11` to `pixel-y=55`). No in-browser CPU%
     measurement exists to confirm the final number - would need the
     native desktop emulator's `performance-display` overlay - ask the
     user to confirm in real play.
  6. *(Later session, using the new WebGL perf overlay - see its own
     section below) "the game seems to slow down when enemy missiles
     reach near our bases"* - confirmed and measured directly: CPU load
     climbed steadily over consecutive real seconds (roughly 5% -> 100%)
     specifically as active missiles approached the bottom of their fall,
     matching the report precisely. Root cause: `tmisCompositeLineRow(y)`
     (round 5's own fix, above) still scanned a missile's *entire* trail
     width (up to ~60 columns, its full `[min(x1,x2),max(x1,x2)]` span) on
     every one of its active page rows, relying on `tmisTraceLine()`'s
     own internal check to reject the columns that don't belong to that
     row - and a missile's trail only reaches its full length (all 6 rows
     simultaneously active) once it's nearly at the bottom, so the
     wasted-call count peaks exactly when the missile is near a dome. Per
     this project's own tenth lesson (a correctly self-gated function
     still costs a full call every time it's invoked), rejecting ~85% of
     those calls internally doesn't avoid their cost. Fixed by computing
     each row's real matching column sub-range *directly*, analytically,
     instead of discovering it by scanning: since the trail's geometry is
     fixed (`y1=11`, `y2=55` always) and strictly linear, mapping the two
     pixel-Y extremes of a given page row back to X via the same
     `tmisMymap()` formula already used forward (just with X/Y swapped)
     brackets the true matching range with a small margin for rounding -
     typically only a handful of columns per row instead of the full
     22-60px span, since the trail's dx/dy ratio (~0.5-1.4) means one
     8px-tall row only ever covers a few columns' worth of horizontal
     travel. New helper `tmisMissileRowXRange()`, used by
     `tmisCompositeLineRow()` in place of its old full-span loop -
     verified via the perf overlay that sustained load in the same "let
     missiles fall to the bases unintercepted" scenario dropped from a
     climb to 100% down to a 53-65% plateau, with trail rendering
     confirmed pixel-correct (same diagonal lines, no gaps) and normal
     gameplay (crosshair movement, firing, interception, score) unaffected.
- `src/games/gameTinyBike.c` - Tiny Bike (Daniel C, GPLv3, same
  `tinyJoypadShim`/`FastTinyDriver.h` lineage). A BMX/motocross side-
  scroller: hold Fire to accelerate, tilt the wheelie angle (LEFT/RIGHT,
  upstream `analogRead(A0)`) and change track lane/height (UP/DOWN,
  upstream `analogRead(A3)`) to jump ramps and dodge oil slicks/holes
  before the time bar runs out. Picked as the next lowest-effort untried
  Tier-2 candidate (goto=11) after re-confirming via a header-grep across
  every included file (not just the main `.ino`) that it has no hidden
  C++-class complexity, the same triage gap that had under-costed Tiny
  DDug/Missile earlier. Structurally straightforward once ported (no
  blocking loops beyond the usual goto-chain and two real multi-tone
  sound sequences) but surfaced three genuine **dialect-support
  discoveries**, each a first for this project rather than a bug in the
  upstream game itself: binary literals (`0b00000011`) aren't accepted by
  this compiler ("bad floating point literal") - rewritten as decimal with
  a `// 0bNNNNNNNN` comment preserving the original intent; the ternary
  operator (`a?b:c`) isn't accepted either ("character '?' is not a valid
  identifier start") - rewritten as plain `if`/`else`; and `switch`/`case`
  was avoided proactively rather than being the first port to test
  whether the dialect supports it at all (matching Tiny Doc's own already-
  documented caution) - upstream's own six `switch` blocks (background
  tile dispatch, jump-height lookup, collision-type dispatch, map-byte
  splitting, sprite-type dispatch, `Tiny_Flip`'s own mode dispatch) all
  became `if`/`else if` chains instead. Data tables extracted from
  `spritebank.h` with a small Python script (parsing the real `PROGMEM`
  array literals directly, not hand-retyped) - every table's element
  count verified against the script's own parse, the same anti-Bomber-
  bug technique used for Doc/Bert/Minez/Bike's own dead-data audit (see
  below). Two blocking multi-tone sound sequences (a 3-beep countdown
  with real `_delay_ms(400)` gaps between beeps, and a 5-pair win
  fanfare) converted to a small frame-stepped note sequencer matching
  Arkanoid/Missile/Minez's own shape, extended with a per-note `extraMs`
  field specifically to reproduce the real delay gaps between the first
  three countdown beeps (every prior sequencer in this project only
  needed each note's own natural duration, no separate silence gap after
  it). Applied the VRAM-persistence lesson **from the start**: upstream's
  `Tiny_Flip(MODE)` has its own `PRINT`/`PRINT2` partial-row table (mode 0
  skips row 7, mode 1 skips rows 0-1, alternated every real gameplay frame
  via `Tiny_Flip(FOUL_BLITZ)`) - the exact same "assumes real SSD1306 VRAM
  persists the skipped rows" pattern already found and fixed in Pinball/
  Doc/Bert - `bikTinyFlip()` always redraws all 8 rows regardless of mode,
  avoiding the bug proactively instead of needing a later fix. Also
  dropped several genuinely-dead upstream elements found while porting
  rather than porting them verbatim: `BigStepB`/`MinijumpB` (declared in
  `spritebank.h`, never referenced anywhere in the `.ino`), `DScroll0`/
  `BScroll0` (declared, never used), `RECUPE_Y_SPRITE()` (defined, never
  called), and `Sprite2PAINTinBLACK` (reset then only ever read inside the
  exact same statement that would set it, so it can never actually
  influence control flow beyond the sprite-hit check it's OR'd with).
  Verified via extended Puppeteer play: attract screen, level-intro
  picture display, the start-line countdown sequence, active gameplay
  (acceleration, wheelie tilt, lane changes, scrolling parallax
  background/HUD bars, obstacle sprites, ramp jumps), and a deliberate
  crash scenario (holding the wheelie-up input continuously until
  `Wheel_up` maxed out) correctly decrementing lives (helmet icons
  visibly dropping from 3 to 1) with continued playable recovery
  afterward and no crashes/hangs. Win-state (reaching the finish line) and
  the exact game-over-to-attract transition were not specifically forced
  in this session's testing - lower risk than the already-verified crash
  path since both reuse the same wait-state machinery already exercised,
  but worth a specific check in a future session if time allows.
  **CPU-load pass, requested right after shipping**: measured first with
  the perf overlay rather than guessing - gameplay CPU was pegged at a
  steady 100% throughout. Root cause was the exact same O(pixels x
  objects) shape already fixed in Bomber/Pacman/Doc/Bert, just never
  applied here since Bike only ever has at most 2 obstacle sprites (looked
  low-risk enough to skip at ship time, incorrectly - 2 sprites x calling
  `bikBlitzSprite()` for all 1024 pixels/frame each is still 2048+ wasted
  calls, before even counting the bike's own sprite). Fixed both call
  sites: `bikBikeSprite()` (the player's own sprite, fixed x in [24,36]
  since its xPos is hardcoded to 24 in upstream's own call site - gated
  there, cutting its call count by ~90%) and `bikBlitzSpriteMap()`
  (rewritten as `bikCompositeSpriteMapRow(y)`, composited once per page
  row into a shared `bikSpritePageBuffer[128]` by walking only the up-to-2
  active obstacle sprites and writing just their own narrow column range,
  preserving the original per-pixel "first active sprite with a nonzero
  pixel wins" priority order by only writing a column that isn't already
  set). Verified two ways: (1) an extended play session including a full
  crash-and-recovery *and*, incidentally, a complete game-over-to-attract-
  to-restart cycle (not specifically forced in the initial port's own
  testing) rendered correctly throughout, with no regressions in obstacle
  sprites, the bike's own animation, or draw priority between overlapping
  sprites; (2) measured before/after with the perf overlay on the same
  input sequence - CPU dropped from a steady 100% to a 79-96% range.
- `src/games/gameTinyArena.c` - Tiny Arena (Daniel C, GPLv3, same
  `tinyJoypadShim`/`FastTinyDriver.h` lineage). A DOOM-style raycaster
  arena shooter - rotate/move with the d-pad, shoot tentacle enemies with
  Fire before they reach you, survive as long as possible. Picked
  deliberately, when asked to bring TinyDungeon back into scope, as a
  stepping stone instead: this project's **first raycaster port**, proving
  the technique out on a plain (non-C++-class) game before attempting the
  much bigger TinyDungeon (a C++-class raycaster with combat/dice/
  inventory on top of it) in a future session - my own judgment call,
  not something explicitly requested, made because de-risking the
  raycasting technique first seemed clearly lower-risk than tackling both
  the C++-conversion and raycasting problems at once.
  **Key architectural finding**: despite being a raycaster, this game
  needed no new machineDependent primitives at all. Upstream never writes
  real pixels - it renders into a *half-resolution* 64x32 monochrome
  buffer (`VBuffer[4][64]`) and its own `Tiny_Flip()` doubles that buffer
  2x both ways to fill the real 128x64 display (horizontally by writing
  each output byte twice, vertically via a nibble-expansion lookup table).
  The exact same "compute a byte value per real (column,page), call
  `md_drawColumn()`" model already used by every other port here covers
  this without changes - `arTinyFlip()` just derives that byte differently
  (via the half-res buffer + nibble expansion) instead of reading a
  pre-baked column-atlas byte. This meaningfully de-risks TinyDungeon for
  a dedicated future session instead of leaving it permanently shelved.
  Dialect issues hit (all already-known findings from earlier ports, none
  new): no `static` locals (two upstream statics hoisted to file-scope
  globals - one relying on `uint8_t` wraparound, fixed with an explicit
  `& 0xFF` mask, the same lesson as HollowSeeker's cave-phase counter);
  no ternary operator (rewritten as `if`/`else`); no `fabsf()` (only a
  float-only `fabs()`, used instead - the same operation here since this
  game's usage is already all-float). A genuine upstream off-by-one was
  also found and fixed by inspection (not by a crash): `isWall()`, the DDA
  loop's out-of-map check, and the enemy-respawn validity check all
  bounds-checked against `>= 10`, but `Lvl1` is declared `[9][9]` (valid
  indices 0-8) - harmless on real AVR (silent PROGMEM overrun), but risky
  on Vircon32 (a real out-of-bounds array read) - fixed by using the
  correct bound (9) at all three sites.
  **Three bugs found via direct user play-testing, each fixed in turn**:
  (1) up/down movement was inverted - a straightforward transcription
  error (upstream's `isDownPressed()`/`isUpPressed()` swapped from the
  established A0/A3 mapping), fixed by swapping the two conditions and
  verified via screenshot showing the perspective correctly closing on a
  wall after pressing UP. (2) A user question ("does this game only
  display the text 'START' on its titlescreen?") led to noticing an
  unrelated real bug while investigating: the game-over screen rendered
  with a BLACK background instead of upstream's intended WHITE one, since
  Vircon32 zero-initializes globals (not `0xff`) and the port's state
  machine never replicated upstream's "clear VBuffer to `0xff` once,
  exactly when entering the game-over sequence" behavior - fixed with a
  new `arClearBufferWhite()` helper called at the
  `WAIT_RELEASE`->`BLINK` transition, verified via screenshot. (3) A
  runtime "ERROR: INVALID MEMORY READ" crash, root-caused via the
  `assemble -g program` debug-info technique (see Tiny Bomber's own
  writeup for the technique's first use) directly to the game-over blink
  state's `arVBuffer[2][19+i] = ~bit;` line - `~bit` on a small value sets
  all 32 bits (Vircon32 has no implicit byte truncation the way AVR's
  `uint8_t` gave upstream), later feeding a huge value into
  `arSliceByte()`'s unmasked `data >> 4` nibble extraction and reading
  `arExpand[16]` far out of bounds. Fixed the same way this project's
  very first byte-truncation bug fixed `md_drawColumn()`: an explicit
  `& 0xFF` mask at the specific site, plus a second defensive mask added
  centrally inside `arSliceByte()` itself (every `arVBuffer` read funnels
  through there, so this is the one place that needs to guard against a
  stray out-of-range value regardless of which call site produced it).
  **Performance investigation, requested directly** ("can this game be
  optimized when enemy is very near the player performance tanks"):
  `arDrawWorldSprites()`'s per-stripe, per-row inner loop was recomputing
  `texY` (and everything derived from it - the texture row-byte offset,
  page, bit mask) from scratch, including a real division, for every one
  of up to 64x32=2048 pixels when a close enemy fills most of the screen -
  precomputed once per row instead (at most 32 rows), cutting the division
  count from up to 2048 to at most 32 per sprite per frame. A second pass
  found that when the sprite is scaled up past 1x (exactly the "enemy very
  near" case), many consecutive destination columns map to the *same*
  source texture column - cached each run's per-row white/black
  classification once (`colDraw[32]`) instead of re-reading the sprite
  data for every stripe in the run. Measuring this properly required a
  more rigorous debug-hook technique than earlier sessions' one-shot spawn
  overrides: continuously re-freezing the enemy's position to a fixed
  offset from the player *every tick* (undoing the chase AI's own movement
  each frame), eliminating the "enemy keeps closing distance during the
  test" noise that made earlier one-shot-override comparisons
  inconclusive. At a stable frozen distance of 0.5 (close, sprite filling
  much of the screen, safely outside the contact-damage radius): baseline
  was a rock-steady, saturated 100% CPU; the optimized version was a
  rock-steady 97% - a real, clean, unambiguous, repeatable improvement,
  moving this scenario from over-budget (risking frame truncation) to
  under-budget. (At a frozen distance of 2.5, both versions read an
  identical, stable 65% - correctly showing no benefit when the sprite is
  small enough that the redundancy doesn't matter, a useful negative
  result confirming the fix's benefit is real and distance-dependent, not
  a measurement artifact.)
  **A further audit, requested directly** ("any more 'clear'
  optimizations you can see for this game"), found two more real issues:
  (1) `arVBuf(x,y)` - the single-bit wall-dither/border helper in
  `arRenderRaycast()` - was called via a real function call up to ~50
  times per column when a wall fills the whole 32-row screen height
  (standing close to a wall, the direct analogue of the already-fixed
  "enemy very near" case, up to ~3200 calls/frame worst case across all 64
  columns). Inlined directly into its 3 call sites, the same technique
  already used for Tiny Trick's own background-lookup inlining - measured
  with the perf overlay at a fixed close-wall distance: 69% -> 64% CPU, a
  real, repeatable drop, with rendering confirmed pixel-identical. (2) The
  death sequence's own sound effect - upstream's `for(t=220;t>3;t--)
  Sound(t,2);` - fires ~217 `Sound()` calls synchronously within a single
  frame. Harmless on real AVR hardware (each call blocks for a genuine but
  tiny slice of real time, so the whole sweep audibly finishes in well
  under a real frame), but Vircon32's async audio channel has no queue -
  `md_playTone()` unconditionally stops whatever the previous call started
  before beginning the new tone - so 217 calls issued with no real time
  between them can only ever be *heard* as the very last call's tone (an
  inaudible click), while still costing 217 real
  `playnote_start()`/`stop_all()` invocations in a single frame for an
  effect nobody perceives. Same root cause as Tiny Missile's own computed-
  sweep bug. Fixed with a new frame-stepped `arAdvanceDeathSweep()`
  (called unconditionally once per real frame, independent of `arState`,
  since the state machine moves on to the game-over wait state the very
  next frame), with a downsampled step (30 instead of 1) so the sweep
  still finishes in a short handful of frames rather than stretching
  upstream's near-instant zap out to a full ~3.6 real seconds. `min()`/
  `max()` were also checked as candidates and ruled out - both already
  compile to a single hardware instruction each (`imin`/`imax` via the
  Vircon32 math library's own inline asm), so there was no further call-
  overhead win available there.
  Verified via Puppeteer throughout: normal gameplay (movement, shooting,
  enemy AI, kills/respawns), a forced-death sequence (health temporarily
  set to 1 with the enemy forced adjacent, to reliably reach the game-over
  transition, then reverted), and a soak test spanning a full play session
  - all render correctly with no regressions from any of the fixes above.
  Also added the standing per-port menu thumbnail: a real gameplay
  screenshot (the raycast corridor with dithered walls and the gun
  sprite), composited into the 4x4 thumbnail grid's cell 15 - the last
  previously-free cell (`THUMBNAIL_COUNT` bumped 15->16) - verified via
  screenshot that it displays correctly and neighboring thumbnails are
  untouched.
- `src/games/gameTinyGilbert.c` - Tiny Gilbert (Daniel C, GPLv3, same
  `tinyJoypadShim`/`FastTinyDriver.h` lineage). A side-scrolling
  platformer - run left/right, jump gaps and hazards, collect all of a
  level's keys, then reach the door to advance across 10 levels with 7
  lives. Picked as the next port: the lowest `goto` count (7) of any
  untried title, on the already-proven shim lineage, confirmed via a
  fresh per-header `class ` grep to have no hidden C++ complexity.
  Structurally straightforward (a single `RESTARTGAME:`/`RESTARTLEVEL:`/
  `NEXTLEVEL:` goto-chain, the same shape already established) but with
  one genuinely new wrinkle: upstream's own `FPS_Control` is a real
  ~40fps whole-loop throttle (matching NumberPlace/HollowSeeker/t2048/
  Doc's "genuine fixed real-rate" category) - but unlike those games, 40
  does not evenly divide Vircon32's 60fps engine rate. Solved with **a
  new technique for this project**: a Bresenham-style accumulator
  (`gilbTickAccum += 40; if >= 60 then -= 60 and run one tick`) producing
  exactly 40 ticks per 60 real frames long-term, instead of the plain
  integer-divisor counter every earlier "genuine rate" game could use.
  The intro jingle's `sound(2)` (`for(t=255;t>2;t--) Sound(t,1);`, ~253
  notes) is the same class of bug as Tiny Arena's death sweep and Tiny
  Missile's computed sweeps - converted to a downsampled (~18-note)
  frame-stepped sweep proactively, from the start. Two genuine out-of-
  bounds risks were found and fixed by inspection before ever compiling:
  `delKey()`'s search loop scanned 3 slots past its own 20-slot array
  (harmless on AVR's flat memory, fixed to the correct bound), and
  `CollisionCheck()`'s 4x4 neighborhood scan could index 1-2 rows outside
  the 8-row grid (`gridV` is transiently -1 inside `JumpProcedure()` and
  7 inside `GravityUpdate()`, before each function's own later checks
  correct it) - fixed with an explicit bounds guard. Data tables
  extracted from `spritebank.h` via a Python script; the first extraction
  pass didn't strip `/* */` block comments (only `//` line comments), so
  the `/*0*/`.../*12*/` index markers embedded inside `map1coucheN[]`'s
  array literals were parsed as extra data values (65 read instead of the
  real 52) - caught by checking the extracted count against a manual
  source read before the data was ever used, fixed by also stripping
  block comments.
  **Two real bugs found via direct, live user play, right after
  shipping** (the user was actively testing while this port was still
  being finished, catching both within minutes of each report):
  1. "starting a game i don't see a player nor can i seem to be able to
     do anything" - the player sprite was permanently invisible.
     Root cause: upstream's `uint8_t visible=1;` is a non-zero global
     initializer, but Vircon32 zero-initializes globals - the ported
     `gilbVisible` defaulted to 0, and since it only ever toggles once
     the player takes damage (via the injury-blink counter), the
     sprite's own driftBL/BR/TL/TR values (computed each tick from
     `gilbVisible`) stayed in the "invisible" branch indefinitely from a
     fresh boot. Fixed with an explicit `gilbVisible = 1;` in
     `gameTinyGilbert_init()`. A more thorough per-global initializer
     audit (checking every upstream global with an explicit non-zero
     value against its ported counterpart) confirmed this was the only
     one missed - every other non-zero upstream initializer (`LorR=1`)
     was already correctly re-applied via `ResetVarNextLevel()`.
  2. "when i finished level 1, level 2 started but the player spawned
     inside a block and kept dying" (later: "i also spawned in a spike
     already on level 3") - a real, more serious bug, and initially a
     red herring: my first hypothesis (checked directly against the
     actual level 2 tile data via a small Python simulation of the exact
     lookup math) was that the fixed spawn coordinates might literally
     overlap a solid tile in some levels' layouts - disproven, the
     tile at the spawn cell itself was empty. The real cause needed
     re-reading upstream's *goto-chain structure*, not just the
     `NextLevel()` function it calls: `NextLevel()` itself doesn't reset
     the sprite's position (matching what got ported), but every path
     that reaches it is immediately followed by `goto NEXTLEVEL;`, and
     the shared `NEXTLEVEL:` label *unconditionally* calls
     `SpriteShiftInitialise()` right after - regardless of whether that
     label was reached from a fresh game, a retried level after death, or
     completing a level via the door. The port had correctly wired the
     first two paths (both already went through a shared
     `gilbBeginLevel()` that resets position) but the door-completion
     path called `gilbNextLevel()` directly, skipping the reset entirely
     - so the sprite's grid position simply carried over unchanged from
     wherever it happened to touch the previous level's door, which the
     next level's own layout has no reason to keep safe. Fixed by folding
     `gilbSpriteShiftInitialise()` into `gilbNextLevel()` itself (its only
     two call sites). Verified two ways: (1) a temporary debug hook that
     bypassed the key-collection requirement (`hitDoor && 1 /* ... */`)
     to quickly reach real doors via actual play, confirming the fix
     compiles and the game keeps running; (2) a more targeted test -
     simulating three consecutive door completions from a fresh boot to
     land directly on level 3 (matching the user's own report) - showed
     the player spawning cleanly on a safe platform at the fixed (11,3)
     position rather than embedded in level 3's own terrain. Both
     temporary debug hooks were fully removed afterward and the fix
     re-verified on the clean build.
  Also added the standing per-port menu thumbnail - since the existing
  4x4 grid (16 cells) was already exactly full going into this port (the
  16th game, Tiny Gilbert, is registration index 16, one past the last
  filled cell), the atlas canvas grew a 5th row (1024x512 -> 1024x640,
  still comfortably inside Vircon32's 1024x1024 texture cap) rather than
  needing a full grid reshuffle - `THUMBNAIL_GRID_ROWS` bumped 4->5 and
  `THUMBNAIL_COUNT` bumped 16->17, with the new thumbnail composited into
  the new row's first cell. Verified via screenshot that it displays
  correctly and every existing thumbnail (spot-checked Tiny Doc) is
  untouched.
- `src/games/gameTinyPipe.c` - Tiny Pipe (Daniel C, GPLv3, same
  `tinyJoypadShim`/`FastTinyDriver.h` lineage). A Mappy/Pengo-style
  single-screen platformer - bounce on the pipework to knock turtles
  over from below (via a bump indicator or an "earthquake" stomp), then
  walk into a stunned turtle to kick it off for points; touching an
  un-stunned turtle costs a life. `CLASS_TPIPE.h` declares two real C++
  classes, but a small, flat inheritance step (`PASIVE_SPRITE_TPIPE` base
  + `SPRITE_TPIPE : public PASIVE_SPRITE_TPIPE`) - the same tractable
  shape already solved for Tiny Missile's own class header, confirmed via
  a fresh per-header `class ` grep before committing, rather than the
  harder full-hierarchy complexity that keeps TinyDungeon/SQuest/DDug
  deferred. Flattened into one `TpipeSprite` struct with plain `tpipe*`
  functions taking an explicit pointer, matching Missile's own precedent.
  Every intra-function `goto` used as a structured-control-flow shortcut
  (a "continue" via `goto SKIPP_`, an early-exit-past-cleanup via
  `goto EnD_`, etc.) was rewritten with plain `if`/`else`/`continue`
  rather than tested verbatim - lower-risk than being the first port here
  to exercise intra-function goto/label control flow, as opposed to the
  already-proven outer-loop state-dispatch `goto` shape every other port
  uses. `Trace_LINE`/`DIRECTION_LINE`/`Return_Full_Byte`/
  `RECONSTRUCT_BYTE` (a line-drawing primitive) and the `DEBOUNCE` macro
  are declared in `ELECTROLIB.h` but never actually called anywhere in
  the game logic - confirmed by grep before dropping them as dead code.
  **The largest computed sound sweep found in this project yet**:
  `SND_TPIPE(1)` (played on player death) is a *nested* loop
  (`for(e=0;e<100;e+=20){for(r=e;r<e+100;r++){Sound(255-r,2);}}`) firing
  **~500** `Sound()` calls synchronously - bigger than Tiny Missile's own
  508-note sweeps in the same ballpark. Same root cause/fix as every
  other one of these: converted proactively, before ever compiling, to a
  downsampled (~15-note) frame-stepped descending sweep. `switch`/`case`
  avoided proactively throughout (matching Tiny Doc/Bike's established
  caution) - including a genuine multi-case fall-through (bonus-life
  levels 2/5/8/11/14/17 all sharing one action), rewritten as one
  `||`-chained `if`. `SND_TPIPE`'s own case 4 is never called from
  anywhere - confirmed dead and dropped.
  **A genuine logical-vs-arithmetic-shift bug, the same class already
  found in HollowSeeker**, caught by inspection before compiling:
  `ELECTROLIB.h`'s `RecupeLineY(int8_t Valeur){ return (Valeur>>3); }`
  is fed genuinely negative `yPos` values (turtles spawn at y=-3) -
  AVR-GCC's `int8_t >>` sign-extends (arithmetic shift, giving the
  correct floor-division result), but Vircon32's `>>` is a documented
  *logical* (zero-fill) shift. Fixed the same way as HollowSeeker's own
  `hsDivByColumnW`: branch on sign, only ever shift a non-negative
  operand (`-((-val+7)>>3)` for negatives). `RecupeDecalageY()` (also fed
  negative values) was derived directly from the now-safe line-Y helper
  instead of its own separate shift trick, avoiding a second site with
  the same risk.
  **The upstream `Tiny_Flip_TPIPE()`'s own `FLIP_MODE_` parameter maps to
  a column width of 128 or 110** - but the *only* real call site in the
  whole game (`Tiny_Flip_TPIPE(0)`, every real gameplay frame) always
  resolves to **110**, meaning columns 110-127 (18 of 128) were never
  redrawn during actual play - the same "real SSD1306 VRAM persistence"
  assumption already found and fixed in Pinball/Doc/Bert/Trick, newly
  discovered here and fixed proactively rather than retrofitted after a
  report. This also exposed a genuine latent out-of-bounds risk in the
  earthquake screen-shake effect (a shifted column could reach 128,
  past the background array's real 127 bound for a given row) - fixed
  with an explicit clamp. Also caught by inspection: `FADE_TPIPE()`'s own
  9-step fade mask (`0xff << (8-l)` / `0xff << l`) relies on AVR's
  implicit `uint8_t` narrowing to stay within a real byte - on Vircon32's
  full-width `int` shift, `0xff << 8` is `0xFF00`, not 0, which would
  silently zero out every masked pixel; fixed with an explicit `& 0xFF`
  on the computed mask, the same byte-truncation-reliant-trick class of
  bug as Tiny Arena's own VSlide fix.
  `FADE_TPIPE()`'s real 9-step, `_delay_ms(20)`-per-step blocking loop was
  converted to a shared `tpipeAdvanceFade()` helper (one step per real
  engine frame - the whole transition was already only ~180ms real time
  upstream, so this is a close match) reused via its own dedicated state
  at every fade-in/fade-out call site, and `NEXT_LEVEL_TPIPE()`'s two
  `_delay_ms(250)` calls became plain frame countdowns - the same
  "blocking loop -> explicit resumable state" treatment every port here
  needs, just with more states than usual (this game has more distinct
  blocking pieces than most) to cover each piece individually.
  **Checked proactively against the quit-dialog onResume audit before
  shipping** (see that section's own writeup below) rather than waiting
  for another report: `TPIPE_STATE_INTRO_WAIT_RELEASE` has no timer of
  its own (real, indefinite risk) and the level-load states can last a
  couple of seconds (bounded, fixed for consistency) - both wired to
  `gameTinyPipe_forceRedraw()`. Splitting the level-number digit draw
  into a draw-only half (`tpipeDrawLevelDisplay()`) separate from the
  digit-counter advance (`tpipeUpdateDigital()`) was necessary here
  specifically so the forced redraw could safely repeat the draw without
  also advancing the level-number digits a second time.
  **A real bug in that exact split, found via direct user report**
  ("when i press enter on the 'levels: 01' screen... choose no it
  displays level: 02") - the same root-cause *shape* as Tiny Bike's own
  "NEXT RACE" bug from earlier this session, just via a different
  mechanism. Upstream's own `DRAW_LEVEL_TPIPE()` draws the *current*
  digits, then immediately advances them (a genuine, correct "prepare the
  next level's display" pattern) - all in one call, at the
  `WAIT1`->`MUSIC` transition. The split avoided a *second* advance on a
  forced redraw, but didn't account for the *first* advance already
  having happened by the time that forced redraw could fire in the
  `MUSIC`/`WAIT2` states - `tpipeDrawLevelDisplay()` was still reading
  the *live* `tpipeGP.digit1/digit2` fields directly, which are already
  the *next* level's values one call after the real draw, not what's
  still genuinely on screen. Diagnosed by code inspection alone (no
  testing, per the user's own request) by tracing exactly when
  `tpipeUpdateDigital()` runs relative to every place that could redraw
  afterward. **Fixed** by freezing what was actually just drawn into a
  dedicated `tpipeShownDigit1`/`tpipeShownDigit2` pair at the one real
  draw call site, with `tpipeDrawLevelDisplay()` reading those instead of
  the live, already-advanced GP fields - any later forced redraw (real or
  not) now always reproduces the exact digits genuinely on screen,
  regardless of how far the live counters have since advanced.
  **Generalizable lesson, same family as the Tiny Bike case**: splitting
  a draw-plus-mutate function to make a repeated redraw safe only removes
  the *second* mutation - it doesn't restore the *original* pre-mutation
  values if the single legitimate mutation already ran before the redraw
  fires. Any "redraw the same screen again" hook needs to redraw from a
  frozen snapshot of what was actually shown, not from whatever mutable
  state the draw function *used to* read, if that state can advance
  again before the hook fires.
  **Data tables extracted via Python script - two real transcription
  errors found and fixed before ever measuring performance**: the first
  hand-copy of the `MAIN_TPIPE` player-sprite table had one extra trailing
  value (45 instead of the correct 44), and separately the two large
  1024-byte raw-framebuffer tables (`BACKGROUND_TPIPE`/`TITLE_TPIPE`) each
  had at least one wrong byte from manual transcription - caught by
  writing a small script that re-parsed *every* array in the finished
  game file and diffed it value-by-value against the original extraction
  output (not just trusting a first eyeballed copy), the same "byte-diff
  transcribed tables" technique this project established after Tiny
  Bomber's own dropped-byte bug. Both large tables were then replaced
  programmatically (regenerated from the verified extraction, not
  hand-fixed byte-by-byte) rather than trying to spot individual errors
  in 1024-value arrays by eye.
  **CPU-load pass, requested directly after a soak test caught a
  genuinely truncated frame** (a screenshot showing only the top page
  rows drawn before cutting to black - this project's own documented
  over-budget failure signature) - measured with the perf overlay rather
  than assuming: gameplay CPU was pegged at a steady 100%. Root cause was
  the by-now-familiar O(pixels x objects) shape: the player and all 4
  turtles were each composited via a full per-pixel call (`tpipeMainBlitz`/
  `tpipeSpritesTurtle`, the latter also looping over all 4 sprites
  internally on every one of 1024 pixels/frame). Fixed the same way as
  Bomber/Pacman/Bert/Doc: composite each sprite once per page row (8x/
  frame) into a shared buffer, over only its own real column footprint.
  The row-membership gate used (`recupeLineY <= y <= recupeLineY+1`) is
  not an approximation - it's the exact range `tpipeBlitzSprite()` already
  checks internally (both sprites' own HSPRITE field is 1), so this cuts
  wasted calls without changing output, the same "self-gating still costs
  a full call every time it's invoked" lesson applied elsewhere. Verified
  two ways: (1) the perf overlay showed CPU drop from a steady 100% to a
  stable 67% on the same input sequence; (2) a full soak-test re-run
  showed every previously-truncated frame now rendering completely (the
  player, all visible turtles, and the power indicator all present),
  confirming the fix addressed the actual truncation, not just the
  measured percentage.
  **Frame-pacing, added later by direct request** ("limit tinypipe to
  30fps including its logic"): upstream itself has no genuine real-time
  throttle at all (the "no timing model" category, same as Trick/
  Invaders/Pinball/Bert/Tris/Arkanoid in this project's own frame-pacing
  survey), so this was a deliberate slowdown rather than restoring an
  original rate. Added a whole-function `TPIPE_TICK_DIVISOR=2` tick-skip
  gating the entire top of `gameTinyPipe_update()` - the same shape as
  NumberPlace/HollowSeeker/t2048/Doc/Pacman's own throttle (not the
  movement-only/redraw-stays-60fps shape used for Trick/Invaders/
  Pinball/Bert), since "including its logic" specifically calls for the
  whole tick to slow down, not just the redraw. Every existing tick-
  counted wait constant in the file (`tpipeWaitFrames`, `tpipeIntroBlinkT`,
  the fade-step counters) was left unrescaled, matching this project's
  own standing "one divisor, no dual bookkeeping" practice - they simply
  now take twice as long in real time.
- `src/games/gameTinyMorpion.c` - Tiny Morpion (Daniel C, GPLv3, same
  `tinyJoypadShim`/`FastTinyDriver.h` lineage - this game's own
  button-threshold `#define`s live in its own `spritebank_TMORPION.h`
  rather than `ELECTROLIB.h`, unusual for this project but confirmed to
  match every other Daniel-C game's real A0/A3 thresholds regardless). A
  tic-tac-toe game against a CPU opponent with 3 difficulty levels (EASY/
  HARD/BRAVE), best-of-9-rounds match. Confirmed via a fresh per-header
  `class ` grep (no C++ complexity hiding anywhere, unlike Tiny Pipe/
  Missile/Minez) before committing to the port. `BOARD[3][3]` (accessed
  both 2D and via a flat `uint8_t*` alias upstream) flattened to a single
  `int[9] tmorpionBoard`. The difficulty-select menu and gameplay
  `goto`-chains became an explicit state machine, same approach as every
  other port here; `BLINK_WINNER_TMORPION()`'s real 10-iteration blocking
  blink (every mark belonging to the winning player, not just the 3 in
  the line) and `NULL_GAME_TMORPION()`'s 30-iteration draw "buzz" each
  became their own frame-stepped sub-state. **The largest dual-tone
  computed sound sweep found in this project**: `SND_BOX_TMORPION(5)`
  (CPU wins the whole match) fires 380 synchronous `Sound()` calls
  (`for(t=200;t>10;t--){Sound(200-t,3);Sound(t,12);}`) - downsampled to a
  step of -15 (~13 dual-tone steps) rather than reproduced verbatim, same
  fix shape as every other computed sweep found here. `switch`, ternary,
  binary literals, and intra-function `goto`-as-control-flow were all
  avoided proactively throughout (several genuine GCC case-range
  extensions rewritten as `if`/`else if` chains) - `CPU_DOUBLE_TMORPION()`
  and `ELECTROLIB.h`'s own dead line-drawing primitives
  (`Trace_LINE`/`Mymap`/etc, confirmed never called, same as Tiny Pipe's
  identical header) were dropped rather than ported. `rand()%3`/`rand()%4`
  in the CPU's random-move fallback and its symmetric-position-replicate
  heuristic switched to the shared `arand(n)` helper.
  **A subtle cursor-rendering trick worth remembering for any future
  board-game port**: the in-game cursor has no sprite of its own - frame
  index 2 of the board sprite tables (the same index used for "genuinely
  empty, no mark" everywhere else) is reused as the cursor-position
  graphic. A non-cursor cell skips drawing entirely when empty (matching
  every other game's "return 0 means background shows through"
  convention), but the cursor's *own* cell never takes that shortcut - the
  sprite lookup always runs, using either the real cell content (blink-off
  phase) or a forced value of 2 (blink-on phase). The practical effect:
  hovering an empty cell shows the cursor steadily (both phases resolve to
  frame 2), while hovering an already-marked cell visibly blinks between
  the real mark and the cursor graphic. Ported as the original two-branch
  structure rather than simplified, since the branches are not equivalent.
  A minor, deliberate, documented simplification: upstream's own win-check
  loop doesn't `break` after a non-terminal win, so one move completing
  two lines at once could double-increment the win counter upstream (an
  instant re-entrant function call there) - since this port's win
  resolution is now a genuine multi-frame animated state, not re-enterable
  the same way, it resolves at most one winning line per move instead.
  **Two live bugs found and fixed via direct code inspection, per the
  user's explicit "check code, don't test" instruction, using the WebGL
  perf overlay only after the fact to confirm rather than to diagnose**:
  the user reported switching difficulty on the title screen spiking CPU
  to 100%. Reading `tmorpionRecupeBack()`/`tmorpionDisplay()`/
  `tmorpionMenuFlip()` directly (not by testing first) found two
  instances of this project's own established "a self-gated function
  still costs a full call every time it's invoked" pattern (see
  Arkanoid/Bert/Tris/Trick's own history above), plus one call that was
  provably wasted on *every* frame with no dependency on game state at
  all: (1) `tmorpionPolice`'s own sprite height is 1 page, so both
  score-digit blits inside `tmorpionDisplay()` can only ever match
  `yPass==0` - yet `tmorpionRecupeBack()`'s `yPass==0||yPass==1` branch
  called `tmorpionDisplay()` (2 blits x 128 columns = 256 calls/frame) on
  row 1 too, a result that's mathematically always zero; fixed by
  splitting the two rows apart so row 1 never calls it at all. (2)
  `tmorpionDisplay()` itself called both score blits for all 128 columns
  on row 0, though only 8 total columns (x in [25,28] and [90,93]) can
  ever be nonzero - fixed by gating each call to its own exact column
  range at the call site. (3) `tmorpionMenuFlip()` (the exact function the
  difficulty-toggle key press calls) ran 3 `tmorpionBlitzSprite()` calls
  (the cursor plus both intro-picture illustrations) unconditionally
  across all 1024 pixels/frame - fixed by precomputing each sprite's own
  page-row and column range once per row/call (derived directly from each
  sprite's own `xPos`/`wPos`/`yPos`/`hSprite`, so the gate is an exact
  reproduction of what `tmorpionBlitzSprite()`'s own internal bounds check
  already computes, not an approximation - zero behavior change,
  confirmed via screenshot showing identical menu rendering before and
  after). Verified via the perf overlay: instantaneous CPU at rest dropped
  to a steady 4-5%, with only a brief single-frame tick at the exact
  moment of a difficulty toggle (one real full-render call plus a state
  transition) rather than a sustained spike - confirmed correct by the
  user directly ("the fix helps") after re-testing.
- `src/games/gameTinyPlaque.c` - Tiny Plaque (Daniel C, GPLv3, same
  `tinyJoypadShim`/`FastTinyDriver.h` lineage). A dental-hygiene shooter -
  a toothpaste-tube submarine drifts between two rows of teeth (top/
  bottom, 8 each), shooting loose "food"/plaque particles before they
  stick to a healthy tooth and start attacking it; clearing every
  particle in a level banks leftover tube fuel as bonus score/teeth, then
  the level advances. The last untried title in the already-proven
  `tinyJoypadShim` family (confirmed via `FastTinyDriver.h`) - picked
  specifically because it has the **deepest C++ class hierarchy found in
  this project** (`Sprite_TPLAQUE` -> `Moving_Sprite_TPLAQUE` ->
  `Food_Sprite_TPLAQUE`/`Main_Sprite_TPLAQUE`, plus a separate
  `Weapon_Sprite_TPLAQUE`, all `:public`-deriving from the base) and the
  highest goto count (20) of any remaining Tiny X title, confirmed via a
  fresh grep before committing. Flattened into one combined `TplaqSprite`
  struct holding every field any of the 4 flavors ever uses, same
  "flatten to one struct + explicit-pointer functions" treatment as Tiny
  Missile/Tiny Pipe's own smaller class headers, just wider here since
  more flavors share one struct. `switch`/ternary/intra-function-`goto`
  all avoided proactively (15 switch statements, 18 ternary uses, and
  every `goto SUITE`/`goto ENDING` early-exit rewritten as `continue`/
  `break`/early `return`); binary literals (the checkerboard-dither
  masks, the weapon's own single-byte bitmap) rewritten as decimal with a
  `// 0bNNNNNNNN` comment, matching Tiny Bike's own established finding.
  `TSIA_TPLAQUE` (declared upstream, never referenced anywhere else)
  confirmed dead by grep and dropped. All 11 data tables extracted via a
  Python script and byte-diff-verified against the finished port before
  ever building - all matched on the first attempt.
  **A genuine VRAM-persistence partial-redraw bug, proactively caught
  before shipping rather than retrofitted after a report**: upstream's
  own `Tiny_Flip_TPLAQUE(0)` (normal gameplay) only ever draws pages 1-7,
  and `Tiny_Flip_TPLAQUE(2)` (the score-panel refresh) only ever draws
  page 0 - each relies on the real SSD1306's own hardware VRAM to still
  hold the *other* mode's last write for the page it itself skips, the
  same real-hardware assumption already found and fixed in Pinball/Doc/
  Bert/Trick/Pipe. Fixed by folding the score/extra-teeth overlay
  directly into the same per-pixel function mode 0 already uses and
  having every mode always redraw all 8 pages - modes 0 and 2 became
  pixel-identical once unified, so they now share one dispatch branch.
  **The level-completion sequence needed the most states of any port so
  far** (14 total) - upstream's `END_OF_LEVEL_TPLAQUE()` synchronously
  calls a genuinely multi-second blocking cascade (`DECOUNT_TPLAQUE()`'s
  own fuel-to-score countdown loop, then a per-tooth restore/dispense
  sequence with real `_delay_ms()` waits throughout) entirely from inside
  a single function called once per tick - rewritten as explicit frame-
  stepped states (`TPLAQ_STATE_DECOUNT_FUEL`, `_DECOUNT_TEETH_UP/DOWN`,
  `_NEXTLEVEL_WAIT1/2`, `_ADD_TEETH[_FINAL_WAIT]`) covering each real
  animated piece individually, the same "blocking loop -> explicit
  resumable state" treatment every port here needs, just with more
  distinct pieces than usual to track (`RESTORE_TEETH_TPLAQUE()`'s own
  per-tooth 1ms delays were the one exception - imperceptible even on
  real hardware, so collapsed into one atomic pass instead of a dedicated
  state, since a real redraw always follows immediately anyway).
  **A real bug found immediately after the first build, before any user
  report** - a genuine data/function naming collision: the tube sprite's
  own PROGMEM data table and its render function were both ported as
  `tplaqTube`, silently shadowing each other. Vircon32's single-
  translation-unit, no-forward-declaration compile model caught this
  immediately as a redefinition error rather than letting it slip through
  - fixed by renaming the data table to `tplaqTubeSprite`, matching every
  other sprite table's own `*Sprite` naming convention in this file.
  **A serious self-introduced CPU regression, found immediately via the
  perf overlay before ever reporting the port "done"**: an early draft
  added an *unconditional* `tplaqTinyFlip(0)` call at the very end of the
  per-tick playing-state function - but re-reading the actual upstream
  `.ino` confirmed the real main loop only ever redraws from *inside* its
  own `Skip_Frame==0` branch (1 of every 6 ticks); there is no trailing
  redraw call after that switch at all. This wasn't a porting simplification
  choice, it was a straightforward transcription mistake - and since
  Vircon32's screen persists between frames exactly like real SSD1306
  VRAM does when nothing redraws it, simply deleting the stray call was
  the complete, correct fix (not a new caching scheme) - game logic
  (movement/food/collision) still runs every tick, matching upstream,
  only the *visual* refresh is 1-in-6. This alone dropped a sustained,
  pegged 100% CPU reading down to ~1% at rest. Two smaller, genuine
  per-pixel-render fixes were also applied proactively once caught
  looking at this: `Food_Recupe_TPLAQUE`'s own per-pixel 8-sprite re-scan
  (the same O(pixels x objects) shape already fixed in Bomber/Pacman/Doc/
  Bert/Pipe) was replaced with once-per-page-row compositing into a
  shared `tplaqFoodPageBuffer[128]`; and `tplaqTube()`/the score/extra-
  teeth overlay (each already self-gated internally) were also gated at
  the call site to their own precomputed per-row footprint, the same
  "self-gated call still costs a full call" lesson as Arkanoid/Bert/Tris/
  Trick/Morpion. Measured via the perf overlay during continuous fire+
  movement (the specific scenario a direct user report called out as
  still feeling slow after the redraw-throttle fix alone): dropped from a
  sustained pegged 100% down to a 1-8% range. **Open question, raised
  directly by the user and not yet resolved**: upstream's main loop has
  no `_delay_ms()`/`FPS_Control` of its own at all (the "no genuine rate
  to match" category, same as Trick/Invaders/Pinball/Bert/Tris in this
  project's own frame-pacing survey) - so whether this port's uncapped-
  60fps food-spawn timing (`tplaqGD.renew`/`renewFood`, ticked once per
  real engine frame, never throttled) matches real ATtiny85 hardware's
  own bare-loop speed is unverifiable without real hardware or reference
  footage, the same open-ended category Arkanoid's own genuine 8x-too-
  slow bug once hid in before someone happened to notice by feel.
- `src/games/gameTinySQuest.c` - Tiny SQuest (Daniel C, GPLv3, same
  `tinyJoypadShim`/`FastTinyDriver.h` lineage). A Seaquest-style
  underwater rescue-and-shoot game - a sub drifts left/right shooting
  fish while surfacing periodically to refill oxygen and drop off
  rescued divers for points. `PASIVE_SPRITE_TSQUEST`/
  `ACTIVE_SPRITE_TSQUEST` flattened into one `TsqSprite` struct, matching
  every other class-hierarchy port here. All 20 data tables extracted
  and byte-diff verified before ever building - caught one real
  transcription error (`tsqBackgroundData` had 1044 values instead of
  the correct 1024, fixed by regenerating the block programmatically) -
  and one naming collision (`tsqBackground` used as both a table and a
  function name, same mistake as Tiny Plaque's own `tplaqTube` bug,
  fixed by renaming the table to `tsqBackgroundData`). Needed four
  separate sound sequencers (a generic small-list player reused for two
  different short cues, a dedicated reader for the real 27-note
  `Music[]` table, and a downsampled ~380-call refill sweep) - the first
  port needing more than one sequencer shape at once.
  **CPU-load pass**: `tsqRecupeOther()`/`tsqRecupeBallisticOther()` had
  the familiar O(pixels x objects) shape (up to 9 enemy sprites scanned
  across all 1024 pixels/frame) - fixed via
  `tsqCompositeOtherRow()`, composited once per page row. A user report
  tying a further spike specifically to "shooting" pointed straight at
  `tsqRecupeMain()`/`tsqRecupeSubsolo()`/`tsqRecupeBallisticMain()`,
  which were self-gated internally but still called for all 1024
  pixels/frame - the by-now-familiar "self-gated call still costs a
  full call" lesson, fixed with row/x-range call-site gating.
  **A new, fifth mechanism of the established "AVR-implicit-behavior"
  bug family**, found from a user report ("sometimes no enemies or
  'swimmers' appear at all") plus their own hint ("it may be related to
  8bit vs 32bit somewhere"): `int8_t` signed-overflow/wraparound
  *reliance* (joining byte-truncation, rand()-range-mismatch, shift-
  count-wraparound, signed-sentinel-comparison, and logical-vs-
  arithmetic-shift as a sixth-mechanism sibling, all stemming from the
  same root cause - an AVR-implicit narrow-type behavior the plain-`int`
  port can't assume holds). Three sites relied on a genuine
  `int8_t` wraparound (-128 -> +127) as a deliberate upstream trick -
  `SPEEDCALC_NEG`'s off-screen enemy-spawn decrement, `SUBSOLO_X`'s
  background-scroll decrement (both "spawn far off-screen, wrap to
  reappear on the opposite edge"), and `BallisticUpdate()`'s
  `BallisticPositionX` (whose only clear condition, `<-6` with no upper
  bound, meant a *rightward* shot - the sub's default facing - never
  cleared without wrapping, permanently jamming the weapon - this was
  the exact cause of a separate "sometimes i don't seem able to shoot
  bullets" report). Fixed uniformly with a shared `tsqWrapInt8()`
  helper (`val&0xFF`, then subtract 256 if >127) applied at each
  load-bearing site. A background research agent subsequently audited
  all ~20 other already-shipped games for the same pattern and found
  **zero new genuine instances** (several close look-alikes were traced
  and confirmed already safe via explicit bounds checks). Also fixed:
  an off-by-one in the enemy sprite composite (`ox+6` should have been
  `ox+7`, clipping the rightmost sprite column) and a VRAM-persistence
  partial-redraw assumption (upstream's separate row-1-6/row-0+7 flip
  calls unified into one `tsqRenderFrame()` that always draws all 8
  rows, avoiding the Pinball/Doc/Bert-class bug proactively). Shipped
  with a genuine 30fps whole-tick throttle (`TSQ_TICK_DIVISOR=2`) per
  direct user request, with an explicit correction from the user mid-
  session ("make sure logic runs at half speed also... so you did not
  have to change wait constants") that settled this project's now-
  standing approach: gate the *whole* tick body identically to the
  render call and never rescale tick-counted wait constants
  independently - the divisor alone is the single source of truth for
  real-world timing. **A standing behavioral lesson reinforced hard
  this session**: mid-investigation of the "can't shoot bullets" report,
  testing was run to "verify" a fix already found via code reading alone
  - the user corrected this immediately and sharply ("what did i tell
  you about not testing yourself") - for any bug-report investigation,
  diagnose and fix via code inspection only, never run the emulator to
  test or verify, even after already finding and applying a fix.
- `src/games/gameTinyDDug.c` - Tiny DDug (Daniel C, GPLv3, same
  `tinyJoypadShim`/`FastTinyDriver.h` lineage). A Dig-Dug-style game -
  dig tunnels through a bit-packed rock grid, fight up to 4 tracking
  enemies with a 2-segment extending laser, or crush them by tunneling
  out their support; the last enemy standing flees to a fixed exit
  point once it's the only one left. `Sprite_TDDUG` ->
  `Moving_Sprite_TDDUG` -> both `Enemy_Sprite_TDDUG`/`Main_Sprite_TDDUG`,
  plus a separate sibling `WEAPON_TDDUG`, all flattened into one
  `TddugSprite` struct. `Moving_Sprite_TDDUG::Ou_suis_je()` and
  `WEAPON_TDDUG::Ou_suis_je()` are byte-for-byte duplicates upstream -
  ported as one shared `tddugOuSuisJe()`. Found and dropped one
  genuinely dead upstream parameter before ever compiling:
  `WEAPON_COLISION_TDDUG(WEAPON_TDDUG W_, uint8_t Nu_)` takes its
  sibling weapon segment *by value* and only ever mutates that local
  copy in its `Nu_==0` branch - a guaranteed no-op with zero observable
  effect on the real caller, confirmed by tracing the C++ value
  semantics rather than assumed.
  **A genuine real-hardware timing discovery**: upstream's main loop
  alternates `Skip_Frame` between two halves every iteration - one with
  a real `millis()`-based ~66ms busy-wait (render + first-time/death
  checks), one without (`Trigger_adj`+`Check_Collision`, which runs "for
  free" immediately after). Since the wait dominates both halves' real
  time, the true tick period is a genuine ~66ms/~15.15Hz hardware rate,
  not an AVR performance compromise - unlike most other Daniel-C games
  in this project's own frame-pacing survey. Ported as one merged
  per-tick body (dropping the Skip_Frame split itself as a redundant
  AVR-era loop shape, same category of drop as Pacman/Bomber's own
  `FPS_Control` split) gated by a single `TDDUG_TICK_DIVISOR` - a real,
  minor, documented simplification results (the death animation's own
  `DEAD` 1->6 counter now climbs at the tick rate directly instead of
  once per Skip_Frame pair, so it plays back roughly 2x faster than
  upstream - a ~6-tick animation, not worth a second nested throttle).
  Two more logical-vs-arithmetic-shift-on-negative-operand bugs (the
  fourth bug's own mechanism, already documented above) found and fixed
  proactively before ever compiling: `RecupeLineY_TDDUG`'s own
  `Valeur>>3` (fed a weapon Y that can go negative) and
  `Moving_Sprite_TDDUG::Ou_suis_je()`'s own `y_=PY>>2` - both fixed with
  shared `tddugSafeShiftDiv4()`/`tddugSafeShiftDiv8()` helpers (branch
  on sign, `-((-val+N-1)>>k)` for negatives), plus the established byte-
  truncation fix (`RecupeDecalageY_TDDUG` takes a `uint8_t` parameter by
  value upstream - reproduced with an explicit `&0xFF` mask before the
  now-safe modulo-8 math, since Vircon32 has no implicit narrowing).
  `LEVEL_TDDUG[]`'s 252 values (binary literals throughout) converted to
  decimal; all 12 data tables and all 5 generated sound-sweep note
  tables byte-diff verified against a Python extraction before ever
  building. Three real dialect issues surfaced only at **compile time**
  despite a careful proactive read of the dialect rules beforehand:
  ternary operators used pervasively (rewritten as `if`/`else`), a
  `struct TypeName[N] varName;` array declaration (the dialect accepts
  `struct TypeName{...}` only at the actual definition site - every
  other reference, including array declarations, needs the bare type
  name with no `struct` keyword at all, confirmed against Tiny SQuest's
  own `TsqSprite[9] tsqOther` precedent), and a sound-sequencer helper
  originally declared to take an `int[150] notes` parameter (arrays
  can't be passed by value - "functions cannot pass arguments of size >
  1" - switched to a plain `int* notes` pointer, matching every other
  sprite-table parameter in this project).
  **A real bug found via live user testing, right after shipping**: the
  user reported that trying to launch the game from the menu instead
  showed a corrupted, "double-exposure" mess of *other* games' leftover
  art (Tiny Bomber's HUD/checkered grid, Invaders-style monster rows,
  Pac-Man dots) stacked on top of the menu, then nothing - a screenshot
  made the cause obvious in hindsight. Root cause: `tddugRenderFrame()`/
  `tddugRenderAttract()` never called `md_beginFrame()` - every other
  game's own render/flip function does, at its very top. `md_drawColumn()`
  deliberately skips its draw call whenever a column's composited byte
  is `0`, relying on `md_beginFrame()`'s own `clear_screen()` having
  already blanked the frame - without it, every pixel DDug never
  explicitly draws just kept showing whatever was on screen from
  earlier games/the menu, accumulating across unrelated screens instead
  of a clean frame each tick. Fixed by adding `md_beginFrame()` to the
  top of both render entry points - a plain oversight, not an AVR-
  dialect issue, but the single highest-impact bug of this port by a
  wide margin. Separately, a genuine (if lower-severity) logic bug was
  found via a *requested* code-review pass (not a user report): the
  weapon's second segment can legitimately reach an X position slightly
  left of the tunnel's own coordinate origin when fired left near the
  left wall, and `tddugOuSuisJe()`'s X half used a raw, unsafe `>>2`
  shift assuming X is always non-negative (true for the player/enemies,
  both hard-clamped >=20, but not for this one weapon case) - the
  resulting huge-positive logical-shift garbage value happened to be
  safely caught by `tddugReadGrid()`'s own `x>21` bounds check (not a
  crash), but still produced a wrong "hit a wall" result for that edge
  case. Fixed by routing X through the same safe shift helper already
  used for Y.
  **A requested CPU-load audit** (not a user-observed report this time)
  found the same "self-gated call still costs a full call" gap already
  fixed elsewhere: `tddugMainSprite()` (the player) and
  `tddugRecupeScores()` were each self-gated internally but still
  called across all 1024 pixels/tick - fixed with the same row/x-range
  call-site gating used throughout this project. A second lever found
  in the same pass: the tunnel wall-mask was being recomputed via 2
  `tddugReadGrid()` calls *per pixel* for the whole ~88x6 tunnel region
  (up to ~1056 grid reads/tick), even though every 4 consecutive
  columns share the same underlying grid cell - cached once per row
  instead (`tddugBackWallCache`, 44 reads/row) via the same "cache what
  doesn't actually change every pixel" lesson as Tiny Doc's own
  row-scoped dirty tracking.
  **A design question surfaced to the user rather than assumed**: a
  report that "certain enemies seem to be able to pass through walls"
  right after the player cleared a wall elsewhere turned out, on
  inspection, to be a faithful port of a deliberate upstream mechanic -
  `E_GRID_UPDATE_UP/DOWN/LEFT/RIGHT` return the enemy's own `Tracking`
  flag (not a fixed "1") when a wall is hit, so an enemy that has lost
  tracking phases straight through walls - the classic Dig Dug "ghost"
  behavior, used both by a periodic real-time counter
  (`Trigger_adj_TDDUG`, which permanently drops one enemy's tracking
  once `Counter` exceeds `Trigger_Counter`, itself decreasing each
  level) and by the final-surviving-enemy escape mechanic (so it isn't
  stuck behind un-dug tunnel walls reaching the exit). Confirmed no
  code path ties this to digging itself - `Trigger_Counter` is purely
  time-based (~13+ real seconds on level 1), so the apparent correlation
  with "just cleared a wall elsewhere" was very likely coincidental
  timing rather than causal. Presented to the user as a keep-faithful-
  vs-change decision rather than unilaterally "fixed" - left faithful
  (no change requested).
  **Frame-pacing, revisited twice more by direct request**: shipped at
  `TDDUG_TICK_DIVISOR=4` (matching the genuine ~15fps hardware rate
  above), then changed to `1` (native 60fps, "so the game runs twice as
  fast" - the same "faster feels nicer than historically-accurate" call
  already made for t2048), then settled at `2` (30fps) - all three
  changes touched only the one `#define`, with every tick-counted
  constant (`tddugWaitFrames`, `triggerCounter`, etc) deliberately left
  unrescaled throughout, matching the standing "one divisor, no dual
  bookkeeping" philosophy from Tiny SQuest's own throttle above.
  **A project-wide warning cleanup, requested directly, that surfaced
  once DDug's own warnings were fixed first**: this compiler caps how
  many warnings it reports per run, so `obonoCoreShim.c`'s own 9
  pre-existing warnings (unused parameters in its `loadRecord`/
  `storeRecord` stubs, an unused `counter` global) were silently eating
  the entire budget and hiding every later file's own warnings,
  including DDug's own single "unused parameter" warning
  (`tddugAdjustWeapon2`'s `other`, matching upstream's own confirmed-
  dead reference parameter). Confirmed via an isolated single-file
  compile harness (bypassing the cap) that fixing obonoCoreShim.c's own
  warnings unmasked four more, pre-existing, unrelated warnings in
  already-shipped games (`gameTinyInvaders.c`'s `tinvBackground`/
  `tinvUFOAttackCheck`, each with one genuinely-unused parameter;
  `gameTinyTris.c`'s `trisHGrid`, `gameTinyBike.c`'s `bikTOP_BACK`, and
  `gameTinyPipe.c`'s `tpipeBurstStep`, three data tables/a variable
  confirmed dead by grep before removal). Fixed unused-*parameter*
  warnings by self-assignment (`param = param;`) at the top of the
  function - this dialect has no `(void)param;` cast-to-void idiom to
  suppress them the conventional way - and fixed unused-*variable*/
  *table* warnings by outright removal instead (self-assignment doesn't
  apply the same way to a table with no natural call site), matching
  this project's own established practice for confirmed-dead code.
  Verified project-wide with `compile -Wall`: zero warnings remain.
  Menu thumbnail added the same way as every other port - a real
  gameplay screenshot (dug tunnels, two enemies, HUD) landed directly in
  the existing 4x6 grid's cell 21 (row 5, col 1) without needing to grow
  the canvas.
- `src/games/gameTinyLander.c` - Tiny Lander v1.0 (Roger Buehler/tscha70,
  2020, GPLv3). A Lunar-Lander-style game - thrust left/right/up to guide
  a ship down onto a landing pad carved into a scrolling terrain
  silhouette, across 10 hand-authored levels. Picked via a quick token-
  cost survey across every remaining untried candidate (TinyDungeon,
  TinyLanderV1.0, and gametiny's 5 non-overlapping "unique" concepts) once
  every already-catalogued `tinyJoypadShim`/`obonoCoreShim` game was
  shipped - TinyLander's own `.ino` is 454 lines (goto/while1=6, no C++
  classes, no `switch`/ternary at all), dramatically smaller than any
  gametiny candidate (805-1081 lines, each also needing a genuinely new
  driver adaptation) or TinyDungeon (a full raycaster class, ~1400 lines).
  Not actually `tinyJoypadShim`/`obonoCoreShim` lineage by name (its own
  `gameinterface.h/.cpp`), but investigation before committing showed this
  doesn't need a new shim at all: its `JOYPAD_LEFT/RIGHT/UP/DOWN/FIRE`
  macros use the exact same `analogRead(A0)`/`analogRead(A3)`/
  `digitalRead(1)` thresholds as every Daniel-C game already ported here,
  and its own `SOUND(freq,dur)` is byte-for-byte identical to
  ELECTROLIB.h's shared bit-bang formula - both ported straight onto the
  existing `isLeftPressed()`/etc and `Sound()` from `tinyJoypadShim`.
  The `DIGITAL` struct's `uint8_t D[5]` array member was ported as 5
  separate named fields (`d0`..`d4`, read via a `tlandDigitAt()` index
  helper) rather than an array-typed struct member, since no existing
  struct in this project had tried one and there was no reason to be the
  first. `GameDisplay()`'s own per-pixel collision detection is genuinely
  embedded in what's otherwise a "compute this column's byte" render
  function (a classic overlap-via-OR-vs-ADD bit trick) - ported exactly as
  structured, including a colliding pixel re-invoking the lander-sprite
  function a *second* time within the same call (which, since
  `shipExplode` was just set to 3 by that same collision check,
  immediately renders that pixel via the explosion-sprite branch instead
  of the normal one - the actual mechanism upstream uses to make the
  explosion visibly begin on the very frame a crash is detected, not
  redundant work to simplify away). The level-clear bonus sequence's two
  blocking loops (a per-star flash-and-chime, and an uncapped "count the
  score up one point at a time with a chime each point" tally - up to 960
  individual +1 steps on the last level) were converted to frame-stepped
  sub-states, with the score tally specifically downsampled to finish in
  about half a second regardless of magnitude (a computed step size) -
  the same "downsample a computed sweep rather than reproducing every
  literal step" treatment already used for oversized *sound* sweeps
  elsewhere, just applied to a *visual* count-up loop here instead.
  One genuine upstream quirk fixed rather than replicated: `moveShip()`
  clamps `ShipPosY` against an upper bound but has no corresponding lower
  bound at all - upstream's own `ShipPosY` is `uint8_t`, so flying up
  aggressively enough to push it negative would AVR-wrap to a large
  positive value instead of going negative, and nothing else in the game
  treats a wrapped Y as a deliberate mechanic (unlike Tiny SQuest's or
  Tiny DDug's own genuine wraparound-reliant tricks) - read as a plain
  missing bounds check rather than a designed behavior, so fixed with an
  explicit lower clamp instead of relying on Vircon32's own unspecified
  negative-value handling in the downstream page arithmetic. Also
  initialized `SetLandingMap()`'s local `prev` variable to 0 explicitly,
  where upstream reads it before ever assigning it on the loop's first
  iteration (relying on whatever garbage happened to be on the AVR stack,
  formally undefined behavior rather than a documented AVR-vs-Vircon32
  semantic difference like this project's other found bugs).
  Data extraction/verification followed the established script-based
  byte-diff workflow - caught one real transcription error (`GAMEMAP`,
  a 540-value terrain table, had 2 values dropped partway through a
  hand-copy, shifting everything after; fixed by regenerating the whole
  block programmatically from the verified extraction rather than
  hand-patching, the same "don't try to eyeball-fix a large data table"
  lesson from Tiny Bomber's own dropped-byte bug).
  **Two real issues found via direct user report, right after shipping**:
  (1) "left and right are swapped" - traced to upstream's own control
  naming: `ThrustLEFT` increases `velocityX` (moving the ship *right* on
  screen) and `ThrustRIGHT` decreases it (moving *left*) - upstream's
  naming reflects which thruster physically fires (a left-mounted
  thruster pushes the ship right, like a real rocket), not the resulting
  screen direction, so a faithful `isLeftPressed()`-to-`thrustLeft`
  mapping reads backwards on a real gamepad - the same class of
  deliberate remap already done for Tiny Arkanoid's own faithfully-
  inverted-but-confusing upstream control scheme. Fixed by swapping
  which physical button sets which flag (not the flags' own meaning
  elsewhere in the physics/sprite code), diagnosed and fixed via code
  reading alone. (2) a requested CPU-load check found the same "self-
  gated call still costs a full call" gap this project keeps finding -
  the ship-sprite display function was called for all ~840 game-area
  pixels/frame despite its own real footprint being only ~14 pixels
  (a 7x2 box) - fixed by precomputing the ship's row/column bounds once
  per frame and gating the call site to that exact range (a literal
  duplicate of the callee's own existing bounds check, not an
  approximation). A separate optimization applied from the start rather
  than retrofitted: every dashboard/UI layer (score/velocity/fuel/lives)
  has its own real footprint entirely within the left instrument panel
  (x<=22), and the game-area/collision layer's footprint is entirely
  within x>=23 - these never overlap, so the render loop is split on
  that fixed boundary instead of calling every self-gated layer across
  all 128 columns. **A follow-up optimization request found one more
  layer of the same gap, one level narrower**: within that x<=22 panel
  branch, Score/Velocity(x2)/Fuel/Lives were each still being called for
  all 8 rows despite each one's own internal check only ever matching a
  single specific row (`y==1`/`4`/`5`/`6`/`7` respectively) - only
  Dashboard genuinely needs every row (it's the panel's own background
  image). Fixed by gating each of the other 5 calls to its own exact row
  at the call site too, cutting roughly 700 wasted calls/frame from the
  panel side on top of the earlier x-boundary split.
- `src/games/gameWrenRollercoaster.c` - Wren Rollercoaster (Andy Jackson,
  2015-2017, non-commercial-with-attribution; ATtiny-Joypad port by Billy
  Cheung, 2018). A Tiny-Wings-style endless flyer - a bird glides over a
  scrolling sine-wave landscape, gaining speed/height by sliding through
  valleys and gently flapping over hills, until a fixed "distance" budget
  runs out (no crash/death mechanic - purely a scoring endurance game).
  From `more games/gametiny/`, the folder of ATtiny-Arcade-lineage games
  this project's own earlier triage confirmed use a genuinely different,
  hand-rolled `ssd1306_send_byte()` driver rather than `tinyJoypadShim`/
  `obonoCoreShim` - picked as the next port via the same "least tokens"
  survey used for Tiny Lander, once every already-catalogued Tiny-X/obono
  game shipped: smallest remaining file (805 lines) among every untried
  candidate (TinyDungeon, and gametiny's other 4 non-overlapping "unique"
  concepts, 833-1002 lines each), goto/while1=0, no C++ classes. Investigation
  before committing found the same thing Tiny Lander's own port already
  proved once: despite the different-named driver, Billy Cheung's own
  Tiny-Joypad button-remap comment documents the exact same A0/A3
  500-750/750-950 analog thresholds and digital-pin-1 fire button every
  other game here already uses, and `ssd1306_send_byte()` is called
  exactly once per column per page - the same "one byte per (column,
  page)" model this whole project's `md_drawColumn()` already handles -
  so no new shim was needed here either, just straight onto
  `isLeftPressed()`/`isRightPressed()`/`isFirePressed()`/`Sound()` from
  the existing `tinyJoypadShim`.
  A real VRAM-persistence partial-redraw assumption was found and fixed
  proactively before ever compiling (matching the same bug class already
  found in Pinball/Doc/Bert/Tris/Pipe/Plaque): upstream's own gameplay
  tick calls `drawBird(0,2)` (bird sprite only, columns 8-15, pages 0-1)
  and `drawLandscape(2,8)` (full composite, pages 2-7) - pages 0-1's own
  columns 0-7 and 16-127 are never drawn during normal play at all,
  relying on the real SSD1306's VRAM still holding whatever was last
  written there (always black in practice, since nothing else ever draws
  there after the initial screen clear) - reproduced this exact visual
  result directly rather than needing an actual persistence trick, since
  the intended appearance (blank sky) is already known and constant.
  `doDrawRS`/`doDrawLS`/`doDrawRSP`/`doDrawLSP`'s own small per-column
  bird-sprite lookup (a switch upstream, cases 0-6 plus a default) was
  ported as a flat 8-entry array instead, avoiding this dialect's
  unverified switch support the same way Doc/Bike's own established
  caution already does. A genuinely subtle catch found by careful
  reading rather than a bug report: `floor(boost/40)` and
  `floor(2+speedBoost/110)` upstream both operate on values that are
  already plain `int`, so the inner `/` is *already* a truncating integer
  division before `floor()` ever sees it - floor of an already-integer
  value is a no-op, unlike the landscape-height computation's own
  `floor(62-height+height*sinfactor)` (a genuine float floor, since
  `height`/`sinfactor` are real floats there) - ported the first two as
  plain integer division instead of literally calling `floor()` on a
  float cast of an int quotient, which would have been a needless,
  easy-to-get-subtly-wrong indirection for no behavioral difference
  (C's own truncate-toward-zero `/` is identical on both AVR and
  Vircon32 regardless of operand sign, so no cross-platform discrepancy
  needed guarding against here either, unlike the shift-based bugs found
  in other games). `random(min,max)` calls ported onto the shared
  `arand()` helper; `randomSeed(0)` itself (a fixed, deterministic seed
  upstream, meaning the terrain sequence is bit-identical between
  playthroughs on real hardware) has no equivalent here since Vircon32's
  `rand()` isn't seedable the same way - the terrain will differ between
  playthroughs on this port, a minor accepted deviation. `beep()`'s own
  NOP-loop-based tone generation (not ELECTROLIB.h's calibrated
  `_delay_us()` formula) has no exact real-Hz equivalent to reproduce
  faithfully - ported as a heuristic mapping onto the shared
  `Sound(freq,dur)`; the intro bounce animation's own ~270-step tight
  loop (each step both redraws and beeps at a continuously-varying
  pitch) was simplified to one representative beep per bounce cycle (3
  total) rather than one per step, since Vircon32's queueless audio
  channel would only ever make the *last* of ~90 rapid calls per cycle
  audible anyway - the same "collapses to the last tone" finding already
  documented for every other oversized upstream sound loop in this
  project. EEPROM high-score persistence dropped per every other port's
  own precedent (session-local only) - the "hold fire 2s to reset high
  score or toggle mute" secret menu action still works, just against the
  session-local value.
  **CPU-load pass, applied proactively from the start rather than
  retrofitted**: every text/number rendering layer (title, game-over,
  new-high-score screens, and the in-game score/time-bar) is self-gated
  to one exact row internally, but - the by-now-well-established "self-
  gated call still costs a full call" lesson - each was also gated by
  row at the render loop's own call site before ever shipping, rather
  than waiting for a CPU report the way several earlier games in this
  project needed.
  Font data (`font6x8AJ.h`, a hand-truncated subset of the standard
  `ssd1306xled_font6x8` table missing z/h and most symbols to save flash
  space) extracted and byte-diff verified - the game's own credit text
  ("andh jackson") deliberately exploits the header's own documented
  character remap (lower-case h prints as y, w prints as /) to spell
  "andy jackson" using only the truncated character set available;
  reproduced the exact same remap formula (`c=ch-32; if(c>0)c-=12;
  if(c>15)c-=6; if(c>40)c-=6;`) and left the string literal unchanged
  rather than "fixing" the spelling, since together they reproduce the
  correct on-screen text.
  **A real bug found via direct user report right after shipping**: "the
  new highscore texts... seem to wrap around the screen." Root cause: the
  shared `wrenTextByte()` text-rendering helper only correctly stops
  *exactly at* a string's own null terminator (`str[charIdx]==0`) - but
  the render loop calls it for every one of 128 columns on the matching
  row, not just the columns the string itself occupies, so for `x` values
  further right than the string's real end, `charIdx` keeps advancing
  *past* the terminator into whatever undefined memory happens to follow
  the string literal - not guaranteed to be zero, so it could read back
  as garbage "characters" bleeding across the rest of the row (matching
  the reported "wrap around" symptom exactly). The " NEW HIGH SCORE "
  screen has the *shortest* string relative to its allotted row width of
  any text on this game's screens (96 of 128 columns actually occupied,
  leaving the largest unprotected tail of any call site), which is likely
  why this specific screen was the one where it became visible enough to
  notice and report. Fixed by bounding `charIdx` against the string's own
  real length via `strlen()` (already available project-wide via
  `string.h`, pulled in earlier by `menu.c`'s own single-translation-unit
  include) before ever indexing into it, rather than relying solely on
  eventually encountering a genuine null byte. `wrenNumberByte()` was
  independently confirmed *not* to share this gap - it already computes
  and bounds against `wrenCountDigits()`'s own correct digit count before
  ever indexing, unlike `wrenTextByte()`'s bare terminator check. Diagnosed
  and fixed via code inspection only, per this project's own standing
  practice for bug-report investigations.
- `src/games/gamePong.c` - Bat Bonanza / Pong (Andy Jackson, 2015-2017,
  non-commercial-with-attribution; ATtiny-Joypad port by Billy Cheung,
  2018). A classic Pong clone - a 1-pixel-wide bat on each screen edge,
  single-button/dual-button/2-player control modes, 4 difficulty levels,
  first to 7 points wins. From `more games/gametiny/
  BatBonanzaAttinyArcade/` - a badly-named folder (its own header credits
  "Pong game by Andy Jackson", nothing to do with bats or bonanzas) -
  picked next via the same "least tokens" survey as every prior gametiny
  pick, among the remaining candidates (this folder, the two "UFO"-
  combined files, TinyDungeon): smallest file (910 lines) and lowest goto
  count (1), confirmed via a fresh per-header `class ` grep to have no
  hidden C++ complexity. **Menu title corrected after the initial port
  shipped as "PONG"**: the user asked to name it "Bat Bonanza" instead,
  after checking - the game's own title screen genuinely spells out
  "BAT" / "BONANZA" on screen (confirmed directly in the `.ino`'s own
  `ssd1306_char_f6x8` calls), so what a player actually sees takes
  priority over the header comment's own attribution ("Pong game by...",
  which stays as the credited author, "ANDY JACKSON", rather than the
  display title). Same no-new-shim-needed pattern as every other gametiny
  port (Lander/Wren/Frogger) - Billy Cheung's own A0/A3/fire-pin
  thresholds matched exactly.
  Not `tinyJoypadShim`/`obonoCoreShim` lineage by name. This game's own
  `font6x8AJ.h` (re-extracted and byte-diff verified rather than assumed
  identical to Wren's same-named file, which turned out to have a
  *different* character set - full A-Z here, vs. Wren's own truncated
  set) remaps lowercase 'h' to a 'y'-shaped glyph, and the credit string
  `"bh andh jackson"` deliberately exploits this (renders as "by andy
  jackson") - ported verbatim rather than "corrected", the same z->@/h->y
  substitution lesson from Frogger's own font bug.
  **The one genuinely novel structural piece**: upstream's main loop
  delay is a real, **variable** millisecond delay (`factor`, 2-30ms,
  computed from difficulty and the live score gap) rather than a fixed
  FPS - faster when a side is behind, to help it catch up or make it
  harder depending on difficulty. Ported with a small accumulator
  (`pongAccumMs`, added every real frame, ticking the game forward once
  it reaches the current `pongFactor`) capped to at most one logic tick
  per real 60fps frame - a **deliberate, documented simplification**: at
  the most extreme setting (expert difficulty, big lead) `factor` can
  drop below 16.67ms, meaning upstream's intended rate there slightly
  exceeds Vircon32's native 60fps; capping at 60fps instead of adding a
  multi-tick-per-frame catch-up loop accepts a minor speed cap only in
  that one extreme corner in exchange for much simpler, lower-risk
  accumulator logic (every other difficulty/score-gap combination lands
  comfortably under 60fps, where the accumulator behaves exactly as
  intended).
  Round-scoring win-check moved earlier than upstream's own control flow:
  upstream only skips the round-flash-and-restart when the just-updated
  score has already reached WINSCORE, but doesn't actually end the
  *match* until a separate, later check at the bottom of the main loop -
  reached only on a subsequent non-scoring tick, since the scoring branch
  itself unconditionally breaks out first. Ported as a single immediate
  win-check right at the scoring point instead, since the *outcome*
  upstream clearly intends (skip the round-flash on the match-winning
  point, go straight to the win screen) is identical either way, and this
  port's explicit state machine has no equivalent "keep falling through
  to a later check" shape to replicate faithfully anyway.
  **Caught one real bug before ever compiling**: upstream's `int
  platformWidth = 16;` (a non-zero global initializer) was initially
  declared in the port with no explicit value, defaulting to Vircon32's
  usual zero-init - the exact "audit every non-zero upstream global
  initializer" bug class already found in Tiny Gilbert's own
  `visible=1`. Caught and fixed by re-checking upstream's own declaration
  line before ever building, not from a report.
  **8-bit-vs-32-bit audit came back clean**: every shift relying on AVR's
  implicit `uint8_t` narrowing (the paddle-drawing byte math,
  `0xFF<<player%8` / `0x7E>>(8-player%8)`) got an explicit `&0xFF` mask
  from the start, all shift amounts are provably bounded 0-7 (`player%8`
  is never negative), no signed-sentinel tricks, no narrow int8_t/uint8_t
  types anywhere (matching upstream's own all-plain-int globals).
  **One real per-pixel optimization found via a requested pass**: the
  render loop's PLAYING branch divided `pongBallX` by 8 on all 1024
  pixels/frame to find the ball's column, even though the ball's position
  is constant for the whole render pass - fixed by hoisting the division
  (and the equivalent one for the ball's row) to run once per frame
  instead. Score-digit rendering wasn't given the same caching treatment
  - `WINSCORE=7` means scores here are always single-digit, so the
  digit-count helper is already O(1) by construction, not worth a cache.
  Verified via Puppeteer: attract screen (title art, mode indicator, and
  the h->y credit substitution all correct), the 3-2-1 countdown, active
  gameplay (both paddles and the ball rendering and moving correctly),
  and an incidental live scoring event (the round-flash screen correctly
  appearing with the AI's score at 1) confirming collision detection and
  the scoring/round-transition state machine all work end-to-end.
  **Menu title corrected from "PONG" to "BAT BONANZA"**, requested
  directly ("name the game batbonanza in the menu (check up the name
  first)") - checked before renaming rather than assuming: the game's own
  title screen genuinely spells out "BAT" / "BONANZA" on screen (confirmed
  in the `.ino`'s own `ssd1306_char_f6x8` calls), even though the header
  comment credits it as "Pong game by Andy Jackson" - what a player
  actually sees on screen took priority over the source comment's own
  attribution, which stays as the credited author rather than the
  display title. This also moved the game's alphabetized menu position
  (from 5th, under "P", to 2nd, under "B") - no thumbnail impact, since
  thumbnail region id is keyed to registration index, not display title.
  **Control scheme revised twice more by direct request, right after**:
  (1) "make the player bat move up with left or up and down with right or
  down as well" - LEFT/RIGHT added as UP/DOWN aliases for the player's own
  bat. This directly conflicts with LEFT/RIGHT's own existing difficulty-
  cycle/mute-toggle gesture (holding left/right to move the bat would also
  accumulate toward - and fire - that gesture on release) - flagged to the
  user as a real design question rather than silently shipping the
  conflict; the user chose to keep both behaviors as-is rather than remove
  or rebind either one. (2) Immediately after, a direct bug report ("the
  player BAT needs to move up when i press up and down when i press down
  this does not currently happen") revealed the deeper issue: upstream's
  own `gameMode` concept meant UP/DOWN only ever worked for player 1 in
  "DUAL BUTTON" mode - the *default* mode ("ONE BUTTON") used fire-only
  control (up=fire fast, released=down slow), matching upstream faithfully
  but not the simple, expected behavior the user wanted out of the box.
  Fixed by making player 1's own bat always respond directly to up/down
  (and their left/right aliases) regardless of `pongGameMode`, collapsing
  what upstream treated as two distinct control schemes into one - the
  `gameMode` setting is now consulted only for player 2's own behavior (AI
  vs. a second human on fire in "2 PLAYERS" mode).
  **A 100% CPU report on the attract screen specifically, diagnosed and
  fixed via code inspection only per direct user instruction** ("in bat
  bonanaza / pong the title / attract screen reaches 100 cpu (don't test
  yourself)"): `pongTextByte()` called `strlen(str)` on every one of the
  ~768 pixel calls spanning the attract screen's 6 simultaneous text rows
  - a real, avoidable "redundant per-pixel recompute of a frame-constant
  value" (the same class already fixed for `pongBallX`'s own division,
  and for Frogger's/Stacker's own score-digit counts) - proportionally
  worse here than on any other game's attract screen, since Bat Bonanza's
  own strings are both longer (~20 chars, heavily space-padded for
  centering) and span more rows (6) simultaneously than Frogger's (4) or
  Wren's (~5). Fixed by hoisting each row's string selection (and its
  `strlen()`) out of the 128-column inner loop into a once-per-row setup
  step, with a new `pongTextByteLen()` variant taking the pre-computed
  length instead of recomputing it - `pongTextByte()` itself is
  unchanged, still used by every other mode's own (lower-row-count,
  shorter-string) text calls. Rebuilt and confirmed compiling clean; not
  verified in the emulator, per the user's explicit instruction.
- `src/games/gameStacker.c` - Stacker (Andy Jackson, 2015-2017, non-
  commercial-with-attribution; ATtiny-Joypad port by Billy Cheung, 2018).
  A classic Stacker tower-building clone - a row of blocks sweeps left/
  right; pressing fire (or up/down) locks it against the row below,
  trimming away any unaligned columns; running out of columns ends the
  game, while reaching the top levels up (narrower start, faster sweep).
  From `more games/gametiny/UFO_Stacker_Attiny/` - a genuinely **combined
  cartridge** upstream (its own boot-time prompt lets the player pick UFO
  or Stacker, sharing font/sound/EEPROM-highscore/game-over code between
  both) - split into its own standalone menu entry rather than
  replicating that in-cartridge sub-menu, matching this project's own
  precedent for combined-file sources (Obono's `TinyJoypadWorks` monorepo
  became 3 separate entries). UFO (the other half of this same file) is
  intentionally left as a separate future port, not attempted here.
  Picked via the same "least tokens" survey as every other gametiny pick,
  among the remaining candidates (this file, the two "UFO"-combined
  files, TinyDungeon) - chosen specifically because it offers *two*
  genuinely novel concepts (UFO and Stacker) in one file, versus
  `UFO_Breakout_Arduino`'s own UFO+Breakout pairing (Breakout duplicates
  Tiny Arkanoid's already-shipped genre). Confirmed via a fresh per-header
  `class ` grep to have no hidden C++ complexity.
  This game's own `font6x8AJ.h` (re-extracted and byte-diff verified, not
  assumed identical to any other game's same-named file) turned out to
  remap 'f' to an 'h'-shaped glyph *and* 'h' to a 'y'-shaped glyph - a
  different substitution pair than either Wren's or Bat Bonanza's own
  copy of the "same" file - credit strings (`"andh jackson"`,
  `"inspired bh"`, `"/ebboggles.com"`) ported verbatim rather than
  "corrected", the same h->y/w->[slash] lesson already found in Frogger's
  z->@ bug and Bat Bonanza's own font.
  **A genuine VRAM-persistence gap, found by reasoning about the render
  model rather than counting bytes this time**: upstream only ever
  redraws the *current* moving row and the row that was *just* locked -
  every earlier locked row (including the bottom "foundation" row) is
  drawn exactly once and never touched again, relying on real SSD1306
  VRAM to keep showing it. Fixed with a persistent `stkLockedRows[8][16]`
  array tracking every screen row's own locked-cell pattern (not just
  upstream's transient `row[1]`), refreshed only at the two real mutation
  points (a successful lock, and a fresh level's reset) and redrawn in
  full every frame alongside the currently-moving row - without this, the
  tower would only ever show its topmost 1-2 rows instead of the whole
  built structure. The live score digits and the row-7 tower foundation
  share the same screen row upstream, by simple draw-order coincidence
  (the digits are redrawn over the foundation every tick) - ported with
  the same priority (score digits win over the locked-row pattern at row
  7, not OR-combined, since OR-ing bit patterns would corrupt both images
  into a hybrid rather than show either genuine one).
  Upstream's own fire/up/down handling is a genuine input-redundancy
  quirk worth preserving: fire, up, *and* down are all accepted as the
  "lock the row" trigger, each via its own real busy-wait-for-release -
  ported as a single edge-detected "any of the three, just pressed" check
  instead of three separate blocking waits, since a frame-stepped engine
  has no equivalent to "block until released" and a plain level-check
  would let a held button re-trigger every tick instead of once per press.
  **8-bit-vs-32-bit audit came back completely clean** - no shift
  operators anywhere in the file at all (unlike Bat Bonanza/Frogger, this
  game's box-tile rendering needed no byte-composition math), no signed-
  sentinel tricks, no narrow int8_t/uint8_t types (matching upstream's own
  all-plain-int/bool globals).
  **One real optimization found via a requested pass**: unlike Bat
  Bonanza's `WINSCORE`-capped single-digit score, Stacker's score grows
  unbounded, so its own row-7 HUD digit recomputed its digit count via
  `stkCountDigits()` on every one of 128 columns/frame for an unchanging
  value - fixed with a dedicated `stkScoreByte()` variant taking the
  digit count pre-computed once per frame, mirroring Frogger's own
  `frgScoreDigits` fix (every other `stkNumberByte()` call site is a
  low-frequency static screen - LEVELUP/GAMEOVER/NEWHIGH - not worth the
  same treatment). EEPROM high-score persistence dropped (session-only),
  matching every other port's precedent - the "hold fire ~2s to mute"
  gesture is kept, the combined-cartridge-specific "reset both games'
  high scores" gesture doesn't apply to a standalone entry and was
  dropped outright. The `runCounter` idle-kill (ends the game after 20
  full sweep cycles with the tower never locked, upstream's own anti-
  battery-drain safeguard) was kept faithfully - a harmless, still-
  reasonable anti-AFK safety net even without a battery to protect.
  Verified via Puppeteer: attract screen (title, credits with both font
  substitutions, and the decorative ascending-staircase graphic all
  correct), active gameplay (the sweeping row, a real lock event
  producing a partial/imperfect match and a visibly narrower next row,
  score updating), and an incidental full game-over-to-new-high-score
  transition (triggered naturally once the tower's width was trimmed to
  nothing), confirming the persistent multi-row tower rendering, the
  lock/comparison logic, and the end-game state machine all work
  correctly end-to-end.
- `src/games/gameUFO.c` - UFO (Ilya Titov, non-commercial-with-
  attribution; ATtiny-Joypad port by Billy Cheung, 2018; combined into
  one cartridge with Stacker by Andy Jackson). A Flappy-Bird-style
  flyer - hold up/down to fly, release to fall, weaving through gaps in
  oncoming obstacle walls; some gaps have a destructible dithered barrier
  that only clears if aligned with it while firing an active shot as it
  passes. The other half of `UFO_Stacker_Attiny`'s own combined cartridge
  (Stacker, above, already shipped as the first half) - completing this
  file means every genuinely new concept from `more games/gametiny/` is
  now ported; only TinyDungeon remains from the whole project's original
  scope. This game's own font, credit-string substitutions (`"mods bh
  andh jackson"`, `"original game bh"`, `"/ebboggles.com"` - same h->y/
  w->[slash] pattern as Stacker's own font, ported verbatim), and shared
  game-over/new-high screens are the exact same upstream code Stacker's
  own port already had to replicate - each split-out game keeps its own
  self-contained copy, no cross-game-file sharing mechanism exists here.
  **A genuine shift-safety rewrite, not a literal port, for the obstacle
  wall/gap byte math** - the riskiest single decision in this port:
  upstream's own `B11111111>>((row+1)*8-gapOffset[i])` (and its `<<`
  sibling) can receive a *negative* shift amount whenever a gap's offset
  falls outside the specific page being drawn - a real, reachable case
  (`gapOffset` can exceed 8 while `row==0`, giving a shift amount well
  below zero, yet still satisfying upstream's own `<=8` guard). This is a
  *different* mechanism from the already-documented logical-vs-
  arithmetic-shift and shift-count-wraparound bug classes - here the
  shift *amount itself* can be negative, not just large - and AVR's own
  behavior for a variable negative shift count isn't something this
  project can safely assume or replicate. Rather than trying to preserve
  a formula whose correctness depends on an unverified AVR quirk, the
  wall/gap byte for a given page is computed directly with a small
  fixed-range (0-7) per-pixel loop instead - independently correct by
  construction, with no shift-amount risk at all. The dithered "gap is
  blocked" barrier overlay is layered on top with a plain OR, same as
  upstream's own approach (invisible over solid wall, visible only over
  the otherwise-clear gap - exactly the intended look).
  The player ship + its fire-trail are one continuous 35-byte sequence
  upstream (8 ship bytes, masked by a 2-state `flameMask` for the thrust-
  flame effect, followed by up to 27 trail bytes while a shot is active) -
  ported as one `ufoShipTrailByte(idx)` lookup instead of duplicating the
  ship-vs-trail distinction at every call site, preserving upstream's own
  single continuous column layout (x 8-42).
  **Two defensive clamps added beyond a faithful translation**: unlike
  every other port's own score, UFO's can genuinely go negative in real
  play (firing costs a point, with no floor) - `ufoBlockChance` and
  `ufoMaxGap` are both later fed into `arand()` as a range, and upstream's
  own formulas for both can drive them to zero or negative once score is
  deeply negative, which no other port here needed to guard against.
  Clamped to a safe positive floor instead of risking `arand()` receiving
  a degenerate range. Given this, `ufoNumberByte`/`ufoScoreByte` were also
  built with genuine signed-number rendering (a leading `-` glyph) from
  the start, unlike every other port's own non-negative-only score
  display.
  The attract screen's 30 "stars" are generated *once* upstream (part of
  the boot splash, never regenerated, relying on real SSD1306 VRAM to
  keep showing them indefinitely) - generated once via `arand()` when
  entering the attract state instead, cached, and redrawn from that cache
  every frame (regenerating fresh random positions every frame would make
  them flicker/jump instead of sitting still).
  **8-bit-vs-32-bit audit came back clean**: every shift is either a
  small fixed constant or bounded to 0-7 by construction, all explicitly
  `&0xFF`-masked; the one genuinely risky shift-based formula upstream
  had (the obstacle wall/gap byte) was avoided entirely via the per-pixel
  reimplementation above rather than patched with a mask, since the risk
  there was the shift *amount* going negative, not the shifted value
  overflowing a byte.
  **A real O(pixels x objects) optimization applied proactively during
  this same session's audit pass, not retrofitted after a report**: the
  obstacle list (up to 5) and the star field (30) were each initially
  checked with their own per-object loop *inside* the 1024-pixel/frame
  render loop (5120 and 30720 iterations/frame respectively) - the same
  shape this project has repeatedly found and fixed in other multi-
  object games (Bomber/Pacman/Missile/Frogger). Fixed by compositing each
  into a shared page buffer once per page/row instead (40 and 240
  iterations/frame respectively). The score's own digit count is also
  cached once per frame (`ufoScoreByte`, mirroring Stacker's own
  `stkScoreByte`), since UFO's score is unbounded like Stacker's, not
  single-digit-capped like Bat Bonanza's.
  Verified via Puppeteer: attract screen (title, both credit-line font
  substitutions, and the star field all correct), active gameplay (the
  ship rendering and responding to flight input, an obstacle wall with
  its gap rendering correctly), a real collision-triggered game-over
  (reached naturally in an early test run - a legitimate outcome, not a
  bug, confirming collision detection and the state machine work), the
  game-over-to-new-high-score transition, and an extended bobbing-flight
  survival session confirming continued stable play (score tracking
  correctly net of both scoring and firing-cost ticks) without crashes.
  **A real bug found via direct user report right after shipping**
  ("pressing A... killing a laser the score keeps going negative and no
  new obstacles appear"), diagnosed and fixed via code inspection only,
  per the user's own explicit "don't test yourself" instruction. Root
  cause: upstream's `doDrawLS()`/`doDrawRS()` (the ship-render functions)
  have a real side effect embedded in them - once `fireCount` is active,
  they play the fire sound *and* reset `fire` back to 0 - the exact same
  "render function has embedded logic" shape already found once before
  in Tiny Invaders' own `tinvMyShoot()`. Converting those two functions
  into pure byte-lookup helpers for this port's rendering model
  (`ufoDoDrawLS`/`ufoDoDrawRS`) dropped that reset entirely, so
  `ufoFire` never cleared after a shot - `if(ufoFire==1)ufoScore--;`
  then fired every subsequent tick forever (not just once per shot),
  driving score deeply negative, which in turn drove `ufoMaxObstacles`
  down to 0 or negative (`(ufoScore+40)/70+1` with a very negative
  score), silently stopping the obstacle-spawn/movement loop entirely
  (`for(i=0;i<ufoMaxObstacles;i++)` never running) - matching both
  reported symptoms from one single root cause. **Fixed** by restoring
  the reset (and the fire-sound trigger) as an explicit step in
  `ufoPlayingTick()` itself, at the same point in the tick upstream's own
  render call would have reached it. Not verified in the emulator, per
  the user's explicit instruction - fixed, recompiled clean, and
  rebuilt only.
- `src/games/gameTinyDungeon.c` - Tiny Dungeon v2.0.1 (Sven B, contact
  Lorandil@gmx.de - same author/contact as Tiny Minez, credited "SVEN B /
  LORANDIL"; MIT License). The hardest, most-deferred port in this whole
  project - a full C++ *class* combining a first-person raycaster-like
  renderer with combat/dice/inventory/switches/teleporters, ~1400 lines
  across `dungeon.cpp`+`bitmapDrawing.cpp`. Tiny Arena's own raycaster port
  was picked earlier specifically to de-risk this one before attempting
  it, and that de-risking held up: the "active" renderer (`bitmapDrawing.cpp`'s
  `#else` branch - a disabled `#if 0` branch supports a fuller 0-7 view-
  distance model but was never even compiled upstream) turned out to be a
  small, fixed 24-entry lookup table of pre-drawn wall-segment placements
  (view distances 0-3 only), not a runtime DDA raycast - maps directly onto
  the same "compute one byte per (column,page), call md_drawColumn()" model
  every other port here uses, no new machineDependent primitives needed.
  `Dungeon`/`DUNGEON` converted to flat global state + `tdng*`-prefixed
  functions (the same treatment already proven for Tiny Minez/Missile/Pipe/
  Plaque/SQuest/DDug's own class hierarchies) - "tdng" instead of the
  expected "td" prefix specifically to avoid colliding with Tiny Doc's own
  pre-existing "td" prefix, caught immediately by a real redefinition
  compile error, not a report. `getCellRaw()`'s raw `uint8_t*` pointer
  arithmetic (`cell - _dungeon.currentLevel`) became a plain integer index
  throughout instead. `NON_WALL_OBJECT`/`SIMPLE_WALL_INFO`'s own
  `bitmapData`/`wallBitmap` fields are C pointers to other global bitmap
  arrays baked into a PROGMEM static initializer - never attempted
  anywhere else in this project and not proven safe under this dialect -
  replaced with a small integer ID per table plus a resolver function
  returning the *runtime* pointer (`int* tdngResolveBitmapArray(id)`),
  confirmed safe by precedent (`gameTinyMinez.c`'s own `int* bitmap = ...`
  dispatch) rather than guessed; only a pointer baked into a *static
  initializer* was ever the actual untested risk. `gameLoop()`'s outer
  `while(isPlayerAlive())` and `checkPlayerMovement()`'s inner
  `while(!playerAction && !disableFlashEffect)` busy-wait both became an
  explicit per-frame state machine - upstream's various "flash the screen/
  monster/status bar" effects (teleporter/spinner XOR flash, hit-monster
  inversion held through a busy-wait for Fire to release, retaliation-hit
  inversion) all relied on `renderImage()` being called sparingly by hand;
  since this engine redraws every real frame unconditionally, each flash
  became its own explicit fixed-duration state (~200-250ms) instead of
  relying on `renderImage()`'s own self-clearing side effect, which would
  otherwise clear the very next engine frame (1/60s) instead of staying
  visible. `getDice()`'s AVR-timer-based entropy source replaced with the
  shared `arand()` helper, same as every other port's own RNG replacement.
  Sound: real ATtiny85 hardware trades this game's own sound effects away
  entirely for the dungeon-floor rendering feature (`settings.h` defines
  `_ENABLE_DUNGEON_FLOOR_` and `NO_SOUND` together for that target - every
  upstream sound function is a real no-op on real hardware) - this port
  stays faithful to that shipped, silent behavior (floor rendering *is*
  ported) rather than adding new sound effects Vircon32 could easily
  afford; a real, cheap enhancement opportunity if ever requested.

  **Data extraction was unusually failure-prone and needed two separate
  fixes even after this project's own established byte-diff-everything
  discipline.** All data (level grid, interaction/special-cell/monster
  tables) uses enum-combining symbolic expressions (`SWITCH_L | N_S`,
  `4 + 4 * LEVEL_WIDTH`) rather than plain literals - a Python evaluator
  with the real enum constants defined was used to resolve these, not
  hand-computed. The level grid's *first* extraction attempt (during an
  earlier part of this same session, before a context-window compaction)
  produced a badly corrupted result - not a subtle off-by-one but a
  structurally wrong grid (most cells silently zeroed, switches/chests
  simply absent from the cells the interaction/special-cell tables
  expected them at) - caught only later, after the game was already
  built and playable, via a *cross-validation* pass: computing
  `cellValue & OBJECT_MASK` for every `currentPos` in the interaction
  table against the actual level array and confirming it equals that
  entry's own `currentStatus` (all 21 entries checked - all passed
  cleanly except two toggle-pair "off" halves, which are supposed to
  mismatch, and one entry at position 7 which references a cell the real
  level data leaves empty even in a from-scratch re-verification against
  the live source file - most likely a harmless orphaned/unreachable
  puzzle-design leftover in the *original* game, not a porting error).
  Re-extracted from a fresh, careful re-read of the live source file
  (not the earlier scratch copy) and re-verified clean. **Generalizable
  lesson**: for a level/world layout specifically (as opposed to a sprite/
  sound table), byte-diffing the array's own element count isn't
  sufficient proof of correctness the way it's been for every other
  port's data tables - the real cross-check is whether *other* tables
  that reference specific positions in it (interaction/special-effect/
  monster-placement data) actually agree with what's really there,
  since a corrupted-but-still-256-elements grid can pass a naive count
  check while still being completely wrong.

  **A second, genuine logic bug found via live user play** (reported as
  "moves too fast in certain direction"): turning (Left/Right alone, no
  movement/Fire) never transitioned into the post-action cooldown state -
  upstream's own `playerAction=true` on a turn exits `checkPlayerMovement()`
  the same as a move or interaction would, which is followed by the outer
  loop's own `_delay_ms(200)` - but the ported state machine's turn-only
  path fell through both the "reached new cell" and "fire pressed"
  branches with no `else` case, so a held turn button re-processed every
  real 60fps frame instead of pacing at ~5 turns/sec like every other
  action here. Fixed by adding the missing `else if (playerAction)` branch
  routing pure turns through the same `tdngBeginActionWait()` every other
  action already uses.

  **A genuinely new dialect-adjacent bug class for this project**: caught
  by inspection before ever compiling, not by a report -
  `getDownScaledBitmapData()`'s inner bit-scan loop is
  `for (uint8_t bitValue=1; bitValue!=0; bitValue<<=1)`, relying on
  `uint8_t` wraparound (`128<<1==0` on a real byte) to terminate after
  exactly 8 iterations - Vircon32 ints don't wrap at 8 bits, so this would
  have been a genuine infinite loop, not just a subtly-wrong result the
  way every previous instance of this bug family (byte-truncation/shift-
  wraparound/signed-sentinel/int8-overflow-reliance/logical-vs-arithmetic-
  shift) had been - fixed with an explicit `bitValue<=128` bound from the
  start. Two more latent-but-harmless-on-real-AVR-flash out-of-bounds
  reads (same class as Tiny Arena's own `Lvl1` off-by-one) found and
  clamped centrally by inspection: `maxObjectDistance` can default to
  `MAX_VIEW_DISTANCE` (7) when no wall matches a column's line of sight,
  overrunning the 4-entry scaling tables (clamped to 3); and
  `dungeonFloor`'s own lookup can index one column past its 96-column
  table when `mirror` is active and `x==0` (`floorX==96`, clamped to 95).

  **CPU-load, found and fixed proactively across five real rounds**
  (unlike most other ports here, which shipped once and got optimized
  later if reported - this one needed the full pass *before* the port
  could be called done, since the first playable build was reported
  hitting a saturated, truncating-frames 100% even at rest): (1) hoisted
  the wall/object "is there really a matching cell here" search - which
  only depends on player position/direction, not on which of the 96
  columns is currently rendering - out of the per-column loop into a
  once-per-frame precompute pass (`tdngPrepareVisibleWalls()`/
  `tdngPrepareVisibleObjects()`), the same "self-gated call still costs a
  full call every time it's invoked" lesson this project has hit
  repeatedly, just at a larger scale (up to 24 wall entries x 96 columns,
  or 3 distances x 11 objects x 3 offsets x 96 columns); (2) converted
  those lookups into short compact lists (typically a small handful of
  real matches, not the full 24/99) instead of a same-size boolean table
  still scanned in full every column; (3) hoisted the several object/
  distance-constant fields `getDownScaledBitmapData()` recomputed on
  every one of its 16 calls per column (8 rows x mask+bitmap) into a
  `tdngPrepareScaledBitmap()` step called once per drawn object per
  column instead; (4) row-range-gated the same function's own call sites
  to the object's real vertical range (as narrow as 2 of 8 rows for a
  distant object) instead of always calling for all 8 and letting the
  callee's own internal check absorb the waste; (5) merged the separate
  mask-scan and bitmap-scan calls (which walk the identical bit range,
  differing only in which half of the source array they read) into one
  combined pass, and resolved each object's bitmap array via a *runtime*
  pointer once per column instead of re-running a 10-way if/else dispatch
  on every single byte read inside the scan's own inner loop - confirmed
  by the user's own live testing that visible sprites (doors/monsters/
  chests), not bare wall-only views, were specifically the dominant
  remaining cost, which is exactly what round 5 targeted. Measured (not
  assumed) via the WebGL perf overlay throughout: a previously-truncated
  frame (missing the far wall and the entire dashboard, direct visible
  evidence of exceeding the 250,000-cycle/frame budget) rendered
  completely after these fixes, and an idle/no-visible-object scene
  dropped from a saturated 100% to 85%; a close-up monster/sprite view can
  still spike to 100% in this build. Further reduction from here would
  need a materially bigger restructuring (e.g. pre-rendering each visible
  sprite's whole scaled bitmap once per frame into a buffer instead of
  per-column bit-scanning) rather than another incremental pass - flagged
  as a known open item rather than silently left unmentioned, consistent
  with this project's own standing practice (e.g. Tiny Bert's accepted
  ~70-76% baseline) of being honest about a measured ceiling instead of
  overclaiming a fix.

  Menu thumbnail: the existing 4x7 grid (28 cells, exactly full) grew an
  8th row (1024x896 -> 1024x1024, landing exactly at Vircon32's hard
  texture-dimension cap) rather than needing a full reshuffle, matching
  every earlier grid-growth precedent in this file - verified via
  screenshot that the new thumbnail (the corridor+bars-gate view, credited
  "BY SVEN B / LORANDIL") displays correctly when selected and a spot-
  checked neighboring thumbnail (HollowSeeker) is untouched.

  **Not yet independently re-verified after the fixes above** (budget/
  time ran out this session, flagged honestly rather than silently
  assumed working): the exact browser-automation test sequence used to
  confirm the bars-gate-removal switch's *visible* effect kept landing the
  player one cell short of the intended target despite the underlying
  interaction logic being independently proven correct via an offline
  Python re-simulation of the identical algorithm against the verified
  level data (all 21 interaction entries' toggle pairs check out) - most
  likely a Puppeteer key-repeat/timing artifact in the *test script*
  itself rather than a genuine game bug, but this was not conclusively
  re-confirmed in-engine before time ran out. Chest-opening, teleporters/
  spinners, monster combat (attacks-first vs. player-attacks-first,
  death/treasure), the fade-in/fade-out death sequence, and the victory
  fountain were ported following the exact same faithful logic pattern as
  every already-cross-validated switch/chest interaction, but were not
  each individually exercised live this session either - worth a direct
  playthrough check in a future session before considering this port
  fully proven, the same way most other games here only reached that bar
  after actual extended play uncovered their own remaining bugs.
- `src/games/gameFrogger.c` - Frogger (Andy Jackson, 2015-2017, non-
  commercial-with-attribution; ATtiny-Joypad port by Billy Cheung, 2018;
  artwork by @senkunmusashi). A classic Frogger clone (3 scrolling river
  rows, 3 scrolling road rows, 5 docks) - the second game from `more
  games/gametiny/` (after Wren Rollercoaster), picked via the same "least
  tokens to port" survey now that every tinyjoypad.com-proper title is
  shipped. Not `tinyJoypadShim`/`obonoCoreShim` lineage by name, but -
  matching Tiny Lander's/Wren's own precedent - needed no new shim: Billy
  Cheung's own header comment documents the identical A0/A3/fire-pin
  thresholds every other game here uses. The `ISR(PCINT0_vect)` fire-
  button interrupt was converted to a plain polled `isFirePressed() &&
  !frgClickLock` check, the same "no real async interrupts on Vircon32"
  treatment every other ISR-based upstream driver here has needed.
  **The riskiest structural piece**: `drawGameScreen()`'s own render logic
  is a genuinely *stateful, sequential byte-stream cursor* (a "wraparound
  tail / 15 grid columns with an embedded frog overlay / wraparound head"
  structure per scrolling row, alternating direction row-by-row) rather
  than a closed-form per-column query function the way most other games'
  simpler object composites allow - ported as a **direct structural
  mirror** (`frgComputeGameRowLeft`/`frgComputeGameRowRight`, walking the
  exact same loop shape into a `frgGameRowBuf[128]` cursor buffer) instead
  of trying to invert the logic into a stateless formula, since faithfully
  copying an intricate stateful algorithm's own shape carries far less
  risk than re-deriving an equivalent closed form from scratch.
  **A genuine VRAM-persistence undershoot, found by literally counting
  bytes upstream's own loop emits** (not from a report): each row-composite
  sends exactly `(7-shift) + 15*8 + shift = 127` bytes to a 128-column
  page - one byte short, by design it looks like (trading one column of
  scrolling precision for a simpler wraparound split) - meaning column 127
  is never written by this function on real hardware (and, when the frog
  is actively hopping across a traffic row, the padding+frog+padding
  branch undershoots by a further byte for that one frame). Fixed the same
  way as every prior game with this bug class (Pinball/Doc/Bert/Tris/Pipe/
  Plaque): `frgGameRowBuf` is always padded out to the full 128 columns
  with a background byte after the real cursor-driven writes finish.
  Font remap formula re-derived directly from this game's own
  `font6x8AJ2.h` rather than assumed from Wren's last one - looks similar
  but the final bracket differs (`c-=9` here vs Wren's `c-=6`). Data
  tables extracted via the established Python script; the extraction
  script's first pass didn't understand the `#ifdef SMALLLOGS ... #else
  ... #endif` wrapped around the log sprites (SMALLLOGS is commented out
  upstream, so only the `#else` "bigger logs" branch actually compiles),
  grabbing both branches and yielding 144 values instead of the correct
  120 - caught by the count not matching `bitmaps[15][8]`'s own expected
  size, fixed with a second pass that explicitly strips the inactive
  branch before re-extracting - a new pitfall for this project's own
  "byte-diff everything" discipline (naive comment-stripping doesn't
  understand preprocessor conditionals). `beep(bCount,bDelay)` ported via
  a heuristic mapping like Wren's own, but rescaled differently - this
  game's own bDelay values range much wider (0-1000 vs Wren's ~0-700,
  already mostly clamped), so reusing Wren's exact `255-bDelay` formula
  verbatim would have clamped almost every one of this game's 8 real sound
  sequences to the same floor pitch, losing all tonal variation; rescaled
  bDelay into a 0-250 band first instead. 8-bit-vs-32-bit audit came back
  clean - no shift operators anywhere in the file and no `0xFF`-as-signed-
  sentinel tricks, the only genuine byte-truncation-reliant site
  (`frgByteVal`'s `~fill`) already needed and got the standard `&0xFF` fix
  from the start.
  **Three real bugs found via live testing/direct user report, all fixed
  via code inspection only**:
  1. *"the cars and logs move very slowly... does not seem to be a 100%
     CPU thing"* - the exact same bug class as Tiny Arkanoid's own
     ~8x-too-slow ball/paddle bug: upstream's `interimStep` counts raw
     bare-AVR-loop iterations (thousands/sec on real hardware, gated only
     by two `analogRead()` calls' own real time cost) - far faster than
     this port's 60Hz `update()`. Unlike Arkanoid, only `interimStep`
     itself needed decoupling here, not the whole tick body - the click
     debounce (`frgClickBase`/`frgTickCounter`) already mirrors upstream's
     own genuine `millis()`-based wall-clock timer, which advances at real
     time regardless of loop speed, so it was left alone. Fixed with a
     `FRG_INTERIM_STEP_RATE` multiplier (a best-effort estimate, not a
     precisely measured value - documented as such in the code, same as
     Arkanoid's own approximation) added to `interimStep` once per real
     frame instead of a bare `++`.
  2. A font-substitution bug caught by screenshot review (not a user
     report): the attract screen's credit line showed
     "**:**senkunmusashi" instead of "**@**senkunmusashi". Root cause:
     `font6x8AJ2.h`'s own comment says "@ in place of z" - the special
     "@"-shaped glyph lives at the **'z'** character's slot in the
     truncated font table, and upstream's own source string is literally
     `"zsenkunmusashi"` (relying on that substitution), not
     `"@senkunmusashi"`. Using the literal `@` character instead ran
     through `frgCharIndex`'s own remap formula and landed on font index
     14 (`:`) rather than index 63 (the real `z`/`@` slot) - confirmed by
     tracing the formula by hand for both characters before fixing.
     Fixed by using the string `"zsenkunmusashi"`, matching upstream
     exactly rather than "helpfully" substituting the visually-intended
     character.
  3. A requested optimization pass (not a correctness bug) found two
     minor, low-risk wastes: `frgComputeDockByte(x)` ran its full 5-dock
     scan for every one of the ~58 columns that can never match any dock
     (fixed with a single outer bound `x<3||x>=113`), and
     `frgComputeBottomByte`/`frgNumberByte` both independently recomputed
     `frgScore`'s digit count on every one of the 128 columns/frame for an
     unchanging value - fixed by caching it once per frame
     (`frgScoreDigits`, refreshed in `frgRenderPlaying()`). Both are minor
     relative to the game's already-efficient row-buffer composite (each
     row computed once per frame, not per pixel, from the start - unlike
     several earlier ports, this one didn't need an O(pixels x objects)
     retrofit at all).
  4. **A fourth bug, found via direct user report right after the above
     round shipped**: "when a player falls in the water/dies there is a
     frog display all the way on the left as if the blinking happens on
     the wrong X coordinate." Diagnosed via code inspection only, per the
     user's own explicit instruction not to test this one in the emulator
     - `frgFrogXStart(screenRow)` (the shared helper mirroring upstream's
     `drawFrog()` position formula, reused by the `frogColumn==0` overlay,
     the start-row frog, and the death-animation blink) had a plain
     transcription slip: upstream's real formula is `frogColumn*8 + 7 -
     blockShiftL` (river rows 1/3) or `frogColumn*8 + blockShiftR` (row
     2) - multiplying the frog's actual column by 8 and adding the shift
     offset - but the ported version had `0*8 + 7 - frgBlockShiftL` and
     `0*8 + frgBlockShiftR`, a literal `0` where `frgFrogColumn` belonged.
     This coincidentally produced the right answer for the one call site
     it was originally reasoned through (the `frogColumn==0` special-case
     overlay, where the real column genuinely is 0) but was silently wrong
     for every other caller - most visibly the death-animation blink,
     which force-draws at the frog's actual death position regardless of
     column, so a frog dying anywhere but column 0 blinked at x=0 (the far
     left edge) instead of its real position. Fixed by using
     `frgFrogColumn*8` in both branches instead of `0*8`. Confirmed by
     re-tracing the formula against the .ino source only, then rebuilding
     (compile only, not run) to confirm it still compiles clean - no
     emulator verification performed for this fix, per the user's explicit
     instruction.
  Verified via Puppeteer (before the fourth bug's own report, which was
  diagnosed and fixed via code reading only per direct user instruction):
  attract screen (title art + credits, both correct after the font-string
  fix), a full playing session (docks, river/road scrolling at the
  corrected pace, HUD score/lives), and an incidental full collision/
  death/life-loss/respawn cycle (triggered naturally by hopping into the
  river without lining up on a log) - that same session's own screenshots
  weren't scrutinized closely enough at the time to catch bug 4 above by
  eye, only caught once the user reported it directly. Level-up/dock-fill
  and the full game-over/new-high sequence were not separately forced-
  tested - ported directly from upstream's own logic with the same state-
  machine treatment already proven correct elsewhere, but worth a specific
  check if reported as off.

## CPU-load investigation: Tiny Invaders/Bomber/Pacman hitting ~100%

The user reported these three (all `tinyJoypadShim`-lineage, Daniel-C-style
ports) visibly maxing out CPU load in the emulator, while the
`obonoCoreShim`-lineage games (NumberPlace/HollowSeeker/t2048) and Tiny
Pinball didn't. Investigated by reading the actual emulator engine source
(`C:\github\WebEmulator\DesktopEmulator\ConsoleLogic\V32Console.cpp` /
`V32CPU.cpp`, not just this project's own code) rather than guessing:

- **What "100% CPU" precisely means**: Vircon32 runs at a hard 15 MHz /
  60 fps budget = exactly `Constants::CyclesPerFrame` = 250,000 CPU cycles
  per frame. `V32CPU::RunNextCycle()` shows each cycle is **exactly one
  instruction** - no variable per-opcode cost, so cycles and instructions
  are the same number. `V32Console::RunNextFrame()` runs a loop capped at
  that count and reports `CPULoad = cycles_used / 250000` - and critically,
  if a frame's work doesn't finish inside that budget, the loop **just
  stops mid-instruction-stream**; the rest of that frame's logic silently
  never runs. So "100%" isn't only a slowdown risk, it's a real risk of
  truncated per-frame game logic. Any C statement/function call directly
  costs real instructions - there is no free-tier work here.
- **Root cause**: `bomTinyFlip()`/`pacTinyFlip()`/`tinvTinyFlip()` all loop
  over every one of the 1024 pixels (128x8) and, per pixel, call several
  compositing functions. For Bomber/Pacman specifically, the sprite-
  compositing function (`bomSpriteWrite`/`pacSpriteWrite`) re-looped over
  all 5 sprites *at every single pixel* - O(1024 x 5) sprite-lookups a
  frame, each with real per-call overhead (params, stack frame). Compare
  `obonoCoreShim.c`'s `drawSprites(y)` (used by the games that *don't* hit
  the ceiling): it composites once **per page** (8x/frame), with an early
  `continue` per sprite that doesn't overlap that page, and writes whole
  pixel runs directly - roughly 150x fewer sprite-lookup operations a
  frame. This per-pixel shape was inherited straight from upstream's AVR
  code, which had to stream one SSD1306 byte at a time (its *only* way to
  talk to that display) - a constraint that doesn't exist on Vircon32, so
  porting the loop verbatim carried over cost the original platform forced
  but this one doesn't need.
- **Fix, `gameTinyBomber.c`/`gameTinyPacman.c`**: replaced
  `bomSpriteWrite(x,y)`/`pacSpriteWrite(x,y)` (called once per pixel) with
  `bomCompositeSprites(y)`/`pacCompositeSprites(y)` (called once per page),
  which iterate each of the 5 sprites *once* and, only if that sprite
  overlaps the current page row, write its up-to-8 occupied columns
  directly into a new `bomPageBuffer[128]`/`pacPageBuffer[128]` global via
  the same Decalagey shift-split math the old per-pixel version used - same
  exact output, O(5 x 8) per page instead of O(128 x 5). The main pixel
  loop now just reads that pre-filled buffer instead of calling the sprite
  function again per column. (Pacman's version also had to preserve the
  `pacInGame == 0 && spriteNumber == 0` player-suppression check and the
  ghost "gobbled" frame-offset math exactly, both moved as-is into the new
  per-sprite block.)
- **Fix, `gameTinyInvaders.c`**: different shape of problem here - its
  monster grid is already addressed via direct coordinate division
  (`tinvOuDansLaGrilleMonster`), not a re-scanned list, so there was no
  O(pixels x objects) blowup to fix. Its cost instead comes from 8 layer
  functions called unconditionally on *every* row, when 5 of them
  (`tinvLivePrint`/`tinvVessoFn` only draw on `y==7`, `tinvUFOWrite`/
  `tinvDisplayText` only on `y==0`, `tinvMyShield` only on `y==6`) already
  internally no-op on every other row. Gated those 5 calls behind the
  matching `y ==` check directly in `tinvTinyFlip()`'s pixel loop instead
  of always calling in and immediately returning 0 - cuts those 5 layers'
  call count by 7/8 (5120 calls/frame down to 640) for zero behavior
  change (verified byte-for-byte identical rendering against the
  unmodified version - see below). Also converted `tinvWriteMonster14()`'s
  `while(x>=14) x-=14;` loop to a plain `x % 14` (safe since `x` is only
  ever called non-negative here).
- **Fix, shared across all three**: `bomBoolRead`/`bomBoolWrite0`
  (Bomber's destructible-block bitmap) and `checkDotPresent`/
  `pacDotsDestroy` (Pacman's dot bitmap) all used a repeated-subtraction
  `while` loop to compute `numero/8` and `numero%8` - a real division
  avoided only because AVR-GCC had no cheap division instruction. Vircon32
  has real integer division, so replaced each with plain `/`/`%` - O(1)
  instead of up to ~13 loop iterations, with an explicit `numero < 0`
  guard added first (matching what the loop already did for negative
  input by accident) so the division can't produce an out-of-bounds
  array index.
- **Verification**: rebuilt after each change and screenshot-tested actual
  gameplay (not just title screens) for all three games. One scare during
  this: Tiny Invaders' background art includes a dithered, radially-
  patterned decorative graphic near the bottom-left of the play field that
  looks exactly like the kind of static/garbage corruption seen in this
  project's earlier real bugs - reverted the `tinvTinyFlip()` change
  temporarily and reproduced the *identical* pattern from the *unmodified*
  code with the same input sequence, proving it's pre-existing background
  art rather than a regression, before restoring the optimization. Worth
  remembering: this pattern is legitimate and expected, not a bug, if seen
  again during future Tiny Invaders work.
- **v32opt (separate, orthogonal lever - not yet shipped)**: this
  project's own `Make.sh`/`Make.bat` already had a hook for
  `v32opt` (see Prior Art below), a real post-compile assembly optimizer
  (dead-code elimination, function inlining, register promotion) that was
  simply never on `PATH`. Since cycles==instructions here, any inlining/
  DCE it does directly lowers measured CPU% with zero source changes.
  Copied a pre-built `v32opt.exe` (from the sibling
  `crisp-game-lib-portable_vircon32` project) onto `PATH` at
  `C:\utils\Vircon32\DevTools\v32opt.exe` and confirmed
  `v32opt obj/main.asm obj/main_opt.asm -v -O3` runs cleanly against this
  project's full `main.asm` (268 optimizations applied: 77 inlined
  functions, 46 dead functions removed, plus algebraic/forwarding/strength-
  reduction passes) - but the current shipped build was **not** built with
  it (`SKIP_V32OPT=1` was used for every build in this session after this
  point) - the visual scare above was investigated with v32opt in the
  build initially, which understandably raised suspicion of it, but since
  the same pattern was later proven to be pre-existing/unrelated, v32opt
  itself was never actually confirmed guilty *or* innocent - it just never
  got a clean re-test after that finding. Re-verify it independently
  (screenshot-test all 7 games with a `-O3` build) before shipping it.

### Second pass: Pacman fixed, Invaders/Bomber still over budget

The per-page sprite-compositing rewrite above resolved Pacman entirely
(confirmed by the user - also incidentally explained a "movement feels
faster now" observation: `V32Console::RunNextFrame()`'s cycle-budget loop
doesn't reset the CPU's instruction pointer when it runs out of budget -
it just resumes next real 60Hz tick from wherever it stopped - so an
over-budget frame doesn't corrupt anything, it just silently stretches
one logical game-frame across 2+ real frames' worth of wall-clock time.
Pacman wasn't running *fast* after the fix, it was running *finally at
its intended speed* - it had been in AVR-independent, engine-level slow
motion before). Invaders and Bomber were still reported over budget, so
went another round on each - same read-the-actual-emulator-source-first
approach, but this time looking for redundant *per-pixel* recomputation
of values that don't actually change per-pixel, rather than the
O(pixels x sprites) shape the first pass fixed:

- **`gameTinyBomber.c`, `bomBlockBomb()`**: `bomBlocDetect[x]` groups many
  consecutive x columns under the same value (an x-group of the
  destructible-block grid), so `bomBoolRead(blocVal + (y-1)*15)` returns
  the *same* answer for every x in that group at a given y - it was being
  recomputed from scratch for all ~896 relevant pixels/frame regardless.
  Added a tiny single-entry cache (`bomBlockCacheY`/`bomBlockCacheVal`/
  `bomBlockCachePresent`) that only re-runs `bomBoolRead()` when either
  changes from the last pixel checked - safe because
  `bomBlocBombMem` (the underlying destructible-block bitmap) can only
  change via `bomDestroyBloc()`, which runs during the update-logic half
  of the frame, strictly before `bomTinyFlip()` renders - never mid-render.
- **`gameTinyBomber.c`, `bomTinyFlip()`**: `bomPrintLive()` (only ever
  non-trivial for `x` in 1-7), `bomBombBlitz()` (only ever draws on the
  bomb's own row) and `bomExplose()` (only ever draws within 1 row above/
  below an *active* explosion) were all called unconditionally for every
  one of the 1024 pixels/frame, each just to immediately return a no-op
  for the vast majority of them. Precomputed per-*row* (not per-pixel)
  whether `bomBombBlitz`/`bomExplose` can possibly apply this row at all
  (`bombBlitzThisRow`/`explodeThisRow`), and gated `bomPrintLive`'s call
  behind its own already-cheap `x` bounds check done in the caller instead
  of inside the callee - same "don't pay a full function call for a
  guaranteed no-op" reasoning as Invaders' row-gating in the first pass.
- **`gameTinyInvaders.c`, `tinvMurgeSplitUpDown()`**: unlike Bomber/
  Pacman's flat sprite list, Invaders' monster grid is already addressed
  by direct division (`tinvOuDansLaGrilleMonster`), so there was no
  O(pixels x objects) scan to fix here - but the grid-cell lookup
  (`MonsterGrid[gridY][gridX]`) and its derived "anims" value *were* being
  recomputed on every single pixel, even though `PositionDansGrilleMonsterX`
  only changes once every ~14 columns (a monster sprite's width) and
  `PositionDansGrilleMonsterY` is constant for the whole row. Added a
  small cache keyed on `(gridX, gridY)` (`tinvMonsterCacheX/Y` +
  the cached spriteType/anims for both the current row and the row above,
  since the "split sprite across two page rows" branch needs both) -
  the actual `tinvMonsters[]` bitmap byte read still happens per-pixel
  unchanged (that part *is* genuinely different every column), only the
  grid-membership lookup feeding it is now shared across the ~14-pixel
  cell instead of repeated for each column in it.
- **Verification**: same approach as the first pass - rebuilt, screenshot-
  tested actual gameplay (monsters descending/splitting across page-row
  boundaries for Invaders, since that exercises every branch of the new
  cache; player/enemies/blocks/explosion for Bomber) rather than trusting
  the diff alone, since both changes touch per-pixel rendering math where
  an off-by-one is easy to introduce and easy to miss by inspection.
- **Not yet done**: no fresh CPU% measurement exists yet to confirm these
  two now stay under budget (same caveat as the first pass - no in-browser
  CPU meter is exposed; would need the native desktop emulator's
  `performance-display` overlay, already enabled in
  `C:\utils\Vircon32\Emulator\Config-Settings.xml`, to check numerically).
  Ask the user to confirm in real play before assuming these are fully
  resolved, the way Pacman's fix already was.

### Third pass (much later session): call-site gating for tinvMonster/tinvMyShoot/tinvMonsterShoot, using the new perf overlay

Requested audit ("verify tiny invaders for optimizations, only during
actual gameplay") using the WebGL perf overlay this project built for the
Tiny Doc/Missile investigations. `tinvTinyFlip()`'s per-pixel loop still
called `tinvMonster(x,y)`, `tinvMyShoot(x,y)`, and `tinvMonsterShoot(x,y)`
unconditionally for all 1024 pixels/frame - each of the three already
self-gates internally to a narrow match (the monster grid's own bounding
box for `tinvMonster`; a single exact `(x,y)` pixel each for the two shot
functions, matching their own equality checks against the shot's current
position) - the same "self-gated function still costs a full call every
time it's invoked" cost this project has found repeatedly in other games,
just never applied here since Invaders' render loop predates that lesson.

Fixed by precomputing, once per row (not per pixel): the monster grid's
valid Y range and X range (`monsterRowValid`/`monsterXMin`/`monsterXMax`,
from `MonsterGroupeXpos`/`MonsterGroupeYpos` - the same bounding box
`tinvOuDansLaGrilleMonster()` already checks internally), and the exact
row each shot currently occupies (`myShootRowMatch`/`monsterShootRowMatch`,
from `MyShootBall`/`MonsterShoot[1]/2`) - then gating each call to exactly
that already-existing condition, narrowed further to the exact matching
column for the two shot functions. This is a literal duplicate of each
function's own internal check, not an approximation, so it cannot change
*which* pixel ends up drawn or *when* a collision side effect fires -
`tinvMyShoot()`'s embedded `tinvMonsterAttackCheck()`/`tinvUFOAttackCheck()`
calls (see the fifteenth lesson on embedded render-logic) still happen at
the exact same single pixel as before, just without scanning the other
~1023 pixels/frame that were guaranteed to return 0x00 with no side
effect. Confirmed safe by tracing every place `MonsterGroupeXpos/Ypos`,
`MyShootBall(xpos)`, and `MonsterShoot[]` are mutated - none of it happens
anywhere else during this same render pass, so the once-per-row
precomputation can't go stale mid-frame the way a naive cache might.

**Verified two ways**: (1) gameplay screenshots across an active
move+shoot sequence showed correct rendering throughout (monster
formation, shields, background dithering, ship) and correct game-state
effects (score climbed from 0 to 60, monster formation visibly thinned
from 6x4 to 4 remaining, matching real kills registering); (2) measured,
not just applied on theory - a temporary side-by-side comparison (same
input sequence, gated vs. a temporarily-reverted "call everything
unconditionally" baseline) showed the baseline pegged at 100% CPU for 7
of 8 samples (one dip to 24%), while the gated version dropped below
100% more often (three distinct low readings out of 8 samples) - a real,
if imprecisely-quantifiable-by-the-0-100-clamped-meter, improvement,
consistent with the halved rough call count (background+drawColumn calls
are unavoidable; monster calls dropped from 1024 to ~420/frame, shot
calls from 1024 each to ~1 each/frame).

## Frame-pacing pass: matching each game's original logic-tick rate

The user asked whether the original TinyJoypad games ran at 30fps and, if
so, whether the ports should skip every other real frame to match -
raising a real concern from the Vircon32 author's own guidance (paraphrased
from a Discord exchange the user relayed): a game whose *logic* only ticks
once every few real frames must not use a plain single-frame edge check
(`button == 1`) for "just pressed," since a press landing on a skipped
frame would already read as a higher value by the time the next tick
samples it - the fix is checking the button's raw held-frames counter is
in `[1, window]` instead of `== 1`.

**Investigation first, before touching any code**: read every shipped
game's actual upstream `.ino` for its real frame-pacing mechanism (see
`more games/`), rather than assuming a uniform "they were all 30fps."
Found three genuinely different categories:
- **Genuine fixed-rate throttle** (logic+redraw together, one real
  `millis()`-based delay per loop): NumberPlace (`FPS=20`), HollowSeeker/
  t2048 (`FPS=30`), Tiny Doc (`FPS_Count_TD(33)`, ~30fps). These four
  really did run at a fixed rate on real hardware.
- **Redraw throttled, logic decoupled and uncapped**: Pacman/Bomber (44ms
  redraw gate, logic ran every loop iteration - already fixed earlier this
  project by decoupling render from tick), Tiny Arkanoid (no real-time
  delay at all, paced by a free-running `Frame` byte - already fixed by
  *speeding up* logic 8x/frame to match real hardware's actual bare-loop
  speed, the opposite direction from a slowdown).
- **No timing model whatsoever upstream**: Tiny Trick, Tiny Invaders, Tiny
  Pinball, Tiny Bert, Tiny Tris ran at whatever raw, undocumented speed the
  bare AVR loop achieved.

Given this split, blanket "skip every other frame" only makes sense for
the first group - the other two already have deliberate, previously-
verified pacing decisions (Arkanoid's speed-up in particular would be
directly undone by adding a slowdown on top).

**Shared mechanism added** (`machineDependent.h`/`portVircon32.c`):
- `md_input{Left,Right,Up,Down,Fire}Frames()` - raw signed held-frame
  counters straight from Vircon32's own `gamepad_*()` registers (positive
  N = held for N real frames, negative N = released N real frames ago;
  confirmed by reading the emulator's own `V32GamepadController.cpp`, not
  guessed). `md_inputFireFrames()` also owns the existing input-fire-gate
  transition (`md_inputFire()` is now defined in terms of it) so there's
  one source of truth for what "gated" looks like.
- `md_recentlyPressed(framesValue, window)` (a macro in machineDependent.h,
  matching the existing `circulate()` macro convention in
  obonoCoreShim.h rather than introducing a header-defined function) -
  `framesValue >= 1 && framesValue <= window`, the safe replacement for a
  plain `== 1` edge check in any game whose logic ticks once every
  `window` real frames.
- `obonoCoreShim.c`'s `updateButtonState()` gained a `tickWindow` parameter
  and a new `justPressedState` (built from the windowed check per
  direction/fire) - `isButtonDown()` now reads `justPressedState` instead
  of a plain `buttonState`/`lastButtonState` XOR, fixing exactly the
  "tap landed on a skipped frame" risk for NumberPlace/HollowSeeker/t2048.

**Shipped - whole-function tick-skip** (matches each game's genuine
original rate exactly, since every existing internal timer in these games
was already tuned in "ticks" against that same real rate upstream, so
throttling to match needs zero rescaling): NumberPlace (`NP_TICK_DIVISOR`
= 3, `NP_FPS` changed 60->20), HollowSeeker (`HS_TICK_DIVISOR` = 2,
`HS_FPS` 60->30), Tiny Doc (`TD_TICK_DIVISOR` = 2, no existing `_FPS`
constant to rescale - its own tick-counted timers were already tuned
against upstream's real 30fps tick verbatim). Each game's `_update()`
increments a counter and returns immediately (no redraw call at all)
until it reaches the divisor, then resets and runs its full body - the
same "just skip the whole draw call, previous frame persists" trick this
project's dirty-flag caching already relies on elsewhere. Verified via
Puppeteer, including a rapid 3-tap sequence on the highest-risk case
(edge-triggered tile moves via `isButtonDown()`) registering as 3 distinct
moves with no lost or duplicated input.

**t2048 - throttled, then reverted per direct user request.** Initially
given the same whole-function `T2048_TICK_DIVISOR`=2/`T2048_FPS`=30
treatment as the other three genuine-rate games (verified working
correctly, including the same rapid-tap edge-trigger safety check).
Later reverted after the user reported it "plays faster/nicer" without
the throttle - `T2048_FPS` restored to 60, `T2048_TICK_DIVISOR`/
`t2048TickSkipCounter` removed entirely, `updateButtonState()` called
with a plain `1` (an unthrottled tick window, matching every other
non-throttled `obonoCoreShim` call site) instead of the divisor. Verified
via Puppeteer that tile sliding/merging/scoring all still work correctly
post-revert. Unlike Tris's revert (which uncovered and left behind a
real pre-existing bug), this one had no known defect - it was a pure
preference call, the throttled version played "correctly" by every
measure applied, just less enjoyably.

**Shipped - movement-only throttle** (Trick/Invaders/Pinball/Bert): these
four have no genuine original rate, but *do* have existing wait/blink/
animation timers this port itself invented assuming a tick happens every
real 60fps frame (e.g. Tiny Trick's own `trkWaitFrames = 36; // ~600ms at
60fps`) - a whole-function throttle would have silently doubled every one
of those real-time durations as a side effect. Instead, each game's own
movement/physics/AI/collision step is gated to run every 2nd real frame
(the same decouple-logic-from-redraw approach already used for Arkanoid),
while the render call stays outside the gate, called every real frame for
smooth visuals:
- **Tiny Trick** (`TRK_MOVE_DIVISOR`): paddle/puck physics, AI direction,
  collision, and the fire-kick check gated together; `trkTinyFlip()`
  unconditional.
- **Tiny Invaders** (`TINV_MOVE_DIVISOR`): `tinvUpdatePlaying()` (monster/
  UFO/shot movement, collision, scoring) gated; the `else` branch calls
  `tinvTinyFlip()` alone on skipped frames (confirmed side-effect-free to
  call redundantly). The `tinvWaitFrames`-based wait/flash states are
  untouched - real-time timers, not movement.
- **Tiny Pinball** (`TP_MOVE_DIVISOR`): `tpBallUpdate()` plus the spring-
  charge and flipper-trigger logic gated together (kept in the same tick
  as the ball physics they interact with); `tpTinyFlip2()` and the ball-
  falls-off-bottom check stay outside the gate.
- **Tiny Bert** (`BERT_MOVE_DIVISOR`): jump input, AI movement/death,
  collision gated; `bertAnimLift`/`bertInterlace` (cosmetic-only animation
  counters, not movement) deliberately left ungated to keep their
  originally-tuned blink speed; `bertTinyFlip()` unconditional.

Verified all four via direct Puppeteer play tests (single-tap jump/shot/
flipper/launch-charge sequences) - correct single-response behavior, no
lost input, no double-triggers.

**Tiny Tris - attempted, found a real bug, reverted per explicit user
request; left completely untouched.** The movement-only throttle was
applied here too at first, but the user reported the falling piece "drops
a few frames then pauses... not a linear drop." Investigated with a
temporary on-screen instrument tracking the real-frame gap between
consecutive `trisYy` changes (not sampled screenshots, which alias against
the actual period) - confirmed a genuine, reproducible bug: drops are
normally evenly spaced, but periodically stall for ~11x longer than
normal. Root-caused to a beat-frequency interaction between two
independent counters in `trisControle()`: `trisDropTrig` (the "want to
fall" trigger) only decrements while the piece sits at a grid-aligned
position (`trisOuSuisJeYEngaged==0`, itself just `trisYy % 3`), while
`trisDropSpeed` (the "may I move now" gate) decrements unconditionally
every tick on its own independent cycle. When `trisDropTrig` fires while
aligned but `trisDropSpeed` isn't *also* 0 that same tick, the fall
intent is silently discarded - `trisMovePiece()` unconditionally clears
`trisDeplacementYy` once realigned, regardless of whether the move
actually applied - forcing `trisDropTrig` to retry an entire fresh
`trisLevelSpeedAdj`-tick cycle, sometimes several times before the two
phases happen to coincide again. **Confirmed via the upstream `.ino`
(`tiny-tris_v3.ino`'s `CONTROLE_TTRIS`/`Move_Piece_TTRIS`) that this exact
dual-counter shape is original, byte-for-byte identical upstream code, not
a porting mistranslation** - the stall exists on real hardware too, just
proportionally shorter (and easy to miss) at whatever raw, uncapped speed
the bare AVR loop actually ran. A one-line fix (force `trisDropSpeed = 0`
whenever `trisDropTrig` fires, guaranteeing the same-tick apply) was
tried and, after a debugging detour (the fix appeared to freeze the piece
entirely - actually the *very next* line in the same function
unconditionally resets `trisDropSpeed` back to `trisLevelSpeedAdj`
whenever it reads 0, silently clobbering the forced value before
`trisMovePiece()` ever saw it - a reminder to trace a one-line fix through
every subsequent statement in the same function, not just the block it's
in) - re-traced by hand and confirmed correct, but the user asked to
revert *all* of it rather than ship a fix mid-session, including the
30fps whole-function cap that had been substituted in as a simpler
fallback. **Current state: Tris ships completely unmodified from before
this investigation** - no throttle, no fix, running at full real rate,
with the pre-existing stall bug (present upstream too) still there,
undocumented as a task for a future session if it's worth revisiting.

**Tiny Pacman - added afterward, per direct user follow-up** ("pacman it
plays too fast also"). Pacman wasn't in the original four-game "genuine
rate" group because its own upstream mechanism is the redraw-throttled/
logic-decoupled shape (44ms `FPS_Control`, gating only the redraw every
other loop iteration - see the frame-pacing survey above) rather than a
single combined-rate timer - this port's existing CPU-load fix already
decoupled its sprite compositing from the render loop but still ran the
whole tick once per real 60fps frame, faster than upstream's own blended
~45fps logic rate. Rather than reproduce that exact non-uniform "2 ticks
bunched together every 44ms" shape, the user asked for the simple fix
used elsewhere: `#define PAC_FPS 60` changed to `30`
(`PAC_TICK_DIVISOR = 60/PAC_FPS = 2`), plus a `pacTickSkipCounter` gate at
the very top of `gameTinyPacman_update()` covering the *whole* function
(including its own wait-states and `pacTinyFlip()` call) - the same
whole-function tick-skip shape as NumberPlace/HollowSeeker/t2048/Doc, not
the movement-only/redraw-decoupled shape used for Trick/Invaders/Pinball/
Bert. This also preserves upstream's own "no redraw on the death-
transition frame" quirk exactly, since the skipped-tick path and the
death-early-return path both just leave the previous frame on screen.
Verified via Puppeteer: Pac-Man's own single-tap movement registered
correctly, and all three ghosts visibly dispersed to different maze
positions after a real-time wait, confirming AI/movement keeps running
(not frozen) at the new half rate. Tiny Bomber shares the *identical*
upstream `FPS_Control`/`Frame%2==0` mechanism (same author, same pattern)
and almost certainly has the same "plays too fast" issue, but was not
raised by the user and not touched this round - worth applying the same
fix proactively if it comes up.

## Tiny Invaders: two collision bugs found via direct user play-testing

After the Pacman/Tris throttle work, the user reported Invaders' own
collision detection "seems off sometimes - I seem to shoot right through
an invader without it dying, sometimes I shoot visually nothing and an
invader seems to die on the right of my bullet." Two distinct bugs,
found by tracing the actual collision code rather than guessing:

**Bug 1 (this session's own regression, from the Invaders movement-only
throttle above)**: `tinvTinyFlip()` isn't a pure render function -
`tinvMyShoot()` (the shot's own position advancement *and* its collision
check, via `tinvMonsterAttackCheck()`/`tinvUFOAttackCheck()`) is embedded
directly in its per-pixel draw loop, a deliberate space-saving reuse of
the same per-column pass upstream already did. The throttle's "call
`tinvTinyFlip()` alone on skipped real frames to keep redraw at 60fps"
pattern - correct and safe for Trick/Pinball/Bert, whose render functions
have no embedded logic - meant the shot kept advancing/colliding at full
rate here while monster movement (inside `tinvUpdatePlaying()`, gated by
the throttle) ran at half rate, desyncing the two. **Fixed** by removing
the skip-frame render call entirely for Invaders specifically - skipped
frames now do nothing (previous frame persists, the same trick the whole-
tick-throttled games already use), so the shot+monsters+render only ever
advance as one atomic unit, never independently.

**Bug 2 (genuine, longstanding, confirmed present in the actual upstream
`Tiny-invaders.ino` - not a porting mistake, not caused by this session's
throttle work at all)**: `tinvMonsterAttackCheck()`'s own grid-cell math
(`round((myShootX - xMouin)/14.0)`, upstream lines 631-632) is genuinely
inconsistent with `tinvOuDansLaGrilleMonster()`'s render-side grid lookup
(plain integer division `(x - MonsterGroupeXpos)/14`, upstream line 658)
- the function that decides what to *draw* at each pixel. Near a cell
boundary these disagree: a shot in the right half of a monster's visually
rendered 14px-wide sprite gets attributed to the *next* monster over by
the collision check, matching both reported symptoms exactly (a "miss"
on the real target, or a kill registering on the neighbor). Confirmed via
the actual `.ino` source, not inferred - both formulas are there,
genuinely different, in the original 2019 game itself. Asked the user
whether to preserve this faithfully or fix it (a real design flaw, not a
deliberate original choice, but still a deviation from strict upstream
fidelity) - chose to fix it. **Fixed** by deleting
`tinvMonsterAttackCheck()`'s own hand-rolled `xMouin`/`yMouin`/`round()`
math entirely and reusing `tinvOuDansLaGrilleMonster()` directly (the
exact same function already used for rendering) to get the grid cell -
collision can now never disagree with what's drawn, by construction
rather than by keeping two formulas in sync by hand. Required moving
`tinvOuDansLaGrilleMonster()`'s own definition earlier in the file (single
translation unit, no forward declarations - it needed to be defined
before `tinvMonsterAttackCheck()`, which didn't previously call it).
Verified via an extended soak test (repeated move+shoot over 15 cycles):
score climbed from 0 to 170 and the 24-monster formation visibly dropped
to ~7 remaining, confirming kills register reliably post-fix.

## Beyond the original scope: a follow-up search for uncatalogued games

Once every game from the project's original scope (tinyjoypad.com +
gametiny) had shipped, a direct request to look for more TinyJoypad-
compatible games turned up a genuinely new source: GitHub/GitLab/
Codeberg/web searches found nothing on GitLab or Codeberg at all (this
whole ecosystem lives on GitHub), but surfaced
`Yevgeniy-Olexandrenko/tiny-handheld` (a hardware clone repo bundling a
large pre-built game library) and, tracing its own bundled games back to
their real authors, the actual canonical repos `webboggles/AttinyArcade`
(Ilya Titov's own) and `andyhighnumber/Attiny-Arduino-Games` (Andy
Jackson's own) - both cloned into `more games/` (`AttinyArcade/`,
`tiny-handheld/`) the same `--depth 1` way as every other GitHub source
here. These are the TRUE original sources for this project's existing
Wren/Frogger/Bat Bonanza/Stacker/UFO ports, which had only ever been
sourced via `cheungbx/gametiny`'s own mirror of them.

Found genuinely new, not-yet-catalogued games: **Oroboros** and **Run
Dude Run** (both Ilya Titov, same author as the already-shipped UFO, but
different games), **Four in a Row** (unattributed in its own source), and
**Dino Game** (an original creation of the tiny-handheld repo itself, not
a port - uses a different SSD1306 library than every other game here,
so would need its own driver work rather than being a drop-in). Also
found several genre-duplicates of already-shipped games (ATtiny Tetris
Gold, an AttinyArcade Pacman, HIDIOT 2048, Breakout) - left unported,
matching this project's own established "skip duplicate genres, not
just duplicate files" precedent (e.g. SpaceAttackAttiny vs. Tiny
Invaders) - and two MAKERbuino-platform ports (different hardware, out
of scope).

- `src/games/gameOroboros.c` - Oroboros ("UFO Escape", Ilya Titov,
  webboggles.com/AttinyArcade, 2015; non-commercial with attribution -
  same license family and author as this project's own UFO). A classic
  Snake clone on a 32x16 logical grid, wrapping at the playfield edges,
  growing on bait, dying on self-collision. Not tinyJoypadShim/
  obonoCoreShim lineage by name, and - unlike this project's other
  AttinyArcade-lineage ports (Wren/Frogger/Bat Bonanza/Stacker/UFO, all
  already remapped to TinyJoypad's control scheme by Billy Cheung before
  this project ever saw them) - this is the genuinely untouched original,
  reading two discrete hardware-interrupt buttons rather than TinyJoypad's
  analog ladder. Needed no new shim regardless: the game only ever reads
  two logical inputs (turn left / turn right, relative to current
  heading), so `isLeftPressed()`/`isRightPressed()` cover its entire
  input surface. Upstream's `screenBuffer[16]` (a 32-bit-per-row occupancy
  bitmask, read back via `>> (31-col) & 1` bit tests) was ported as a
  plain flat occupancy array instead (`orbGrid[16*32]`, one int per cell)
  - avoids ever needing a shift up to the sign bit (bit 31) on this
  dialect's signed ints, never tested anywhere else in this project and
  easily sidestepped with a plain array. Upstream's own two buttons set a
  "pending turn" flag via real hardware interrupts, consumed once per
  tick - a naive `isLeftPressed()` level read *at tick time* would behave
  differently (spinning in circles if held across several ticks, since a
  snake tick is much faster than a human can release a button) - fixed by
  edge-detecting both buttons every real engine frame (independent of the
  game's own slower tick rate) into a pending-turn flag consumed by the
  next tick, reproducing upstream's real "one press, one turn" behavior
  far more faithfully than a per-tick level read would. `nextDir` and
  `selfCollision` are set upstream but never actually read anywhere
  afterward - confirmed dead by inspection, dropped rather than ported.
  The render loop upstream only ever streams 124 of 128 real columns,
  relying on real SSD1306 VRAM to keep showing the always-black last 4
  columns - the same VRAM-persistence assumption already found and fixed
  in several other ports here, applied proactively from the start this
  time. `system_sleep()`'s real AVR power-down has no Vircon32 equivalent
  (no battery to conserve) - ported as its *observable* gameplay effects
  instead: the 40-second idle-turn timeout still auto-advances the
  heading once, and the post-game-over sleep becomes an explicit
  wait-for-Fire gate, matching this project's own standing convention.
  Upstream's game-over sound (a 1000-call synchronous `beep()` sweep)
  was downsampled to a short frame-stepped descending sweep, matching the
  established fix for this exact bug shape (Vircon32's audio channel has
  no queue, so 1000 near-instant calls would only ever be audible as the
  last one).
  **A real bug found via direct, immediate user report right after
  shipping** ("there does not seem to be food spawned for the snake"):
  the ported tick function rebuilt the occupancy grid from the snake's
  own segments only, missing upstream's own separate
  `screenBuffer[baitY] |= (bit at baitX)` step that marks the bait's cell
  in the SAME occupancy grid the snake segments use - since the bait's
  own render condition is itself gated behind that grid bit already being
  set (matching upstream's identical structure, where a cell draws
  *nothing at all* unless its occupancy bit is set, checking the special
  bait pattern only as a sub-case of an already-occupied cell), the bait
  was computing valid X/Y coordinates every tick but never actually
  becoming visible, since only snake segments ever set the grid. Fixed by
  adding the missing grid-marking step for the bait's own cell,
  positioned to match upstream's exact ordering (after the eat-check,
  which can reset `baitDropped` back to 0 on the same tick the bait is
  eaten - marking it unconditionally would have shown a phantom bait for
  one frame immediately after eating). Caught and fixed within minutes of
  the user's report by re-reading the render/tick functions side by side
  against upstream, rather than needing further back-and-forth. 8-bit/
  32-bit audit came back clean (no shift operators used anywhere in the
  final port at all, no truncation-reliant sentinels, no negative-operand
  modulo risk) and CPU stayed at a healthy ~50-56% throughout testing -
  no optimization pass needed, the simplest render model of any port in
  this project (plain grid lookups, no font-dispatch chains or bit-scan
  loops).
- `src/games/gameRunDudeRun.c` - Run Dude Run (Ilya Titov,
  webboggles.com/AttinyArcade, 2017; non-commercial with attribution -
  same license family and author as this project's own UFO/Oroboros).
  Dodge falling bombs by moving left/right along the bottom row; bomb
  spawn rate and live bomb count both scale up with score. Not
  tinyJoypadShim/obonoCoreShim lineage by name (genuine #AttinyArcade
  hardware, two discrete interrupt-pin buttons), needing no new shim -
  `isLeftPressed()`/`isRightPressed()` cover the whole input surface.
  Unlike Oroboros (this game's own sibling port, which needed edge-
  detection to replicate a "one press, one turn" gesture), upstream's
  own button handling here is a genuine level read
  (`if (digitalRead(0)==1||btn1==1) ...`), matching a "run" game's real
  intent - held directly at tick time with no edge-detection needed.
  Upstream's bomb rendering is the most intricate part: bombs fall at an
  arbitrary (non-page-aligned) pixel Y, so upstream slices its 7x16
  sprite across up to 3 real hardware pages using `~byte >> offset ^
  0xFF << offset2`-style expressions relying on AVR's implicit `uint8_t`
  narrowing - the exact bug class this project has hit and fixed
  repeatedly elsewhere. Re-derived instead with `rddBombColByte()` - a
  single sign-branched shift of the sprite's 16-bit column value (top+
  bottom source byte combined), independently correct by construction.
  Upstream's ~30-second all-input idle timeout (real AVR sleep) was
  dropped, matching this project's "only replicate a sleep call's
  observable gameplay effect" convention - unlike Oroboros's own idle-
  turn timeout, it has none. Upstream's game-over sound (a 1000-call
  synchronous `beep()` sweep) was downsampled to a short frame-stepped
  descending sweep, same established fix as every other oversized
  upstream sound sweep in this project. `bottle1` is declared upstream
  but only ever referenced from a commented-out call site - confirmed
  dead by inspection, dropped.
  **A self-found bug via this project's own now-standing proactive 8-
  bit/32-bit audit, caught before ever shown to the user**: the INTRO
  splash screen's byte read used a *logical* NOT (`!rddSplash[...]`)
  instead of a *bitwise* one - would have collapsed any nonzero byte to
  0 and any zero byte to 1 instead of inverting each bit, rendering the
  splash essentially wrong. Fixed to `(~rddSplash[...]) & 0xFF`, the
  same explicit-mask fix shape as this project's very first documented
  bug (byte truncation/an unmasked bitwise NOT sets all 32 bits, not
  just 8, on this dialect's full-width ints).
  **A real, user-reported CPU spike ("once score hits 1000 CPU usage is
  100%")**, needing two rounds to actually fix, both measured with the
  perf overlay rather than assumed - see OPTIMIZATIONS.md's own entry
  for the short version; the root cause was `rddUpdateBombs()`'s own
  `else if (rddScore > 1000) rddTotalBombs++` branch having no upper
  bound beyond the array capacity, so live bomb count keeps climbing
  toward `RDD_MAX_BOMBS` (16) the longer a run survives past that
  score. Round 1 (composite bombs once per page-row into a shared
  buffer instead of rescanning all 16 at every pixel) measurably existed
  as a real fix in isolation, but wasn't sufficient alone - verified via
  a temporary debug hook (force `rddScore=1500`/`rddTotalBombs=16` at
  init, and, to get a readable multi-second observation window before
  an inevitable collision, also temporarily disabled the collision check
  with `if (0 && ...)`) that CPU was *still* pegged near 100% with 16
  bombs live even after round 1. Investigating further with the same
  harness found round 2's real cause: `rddBombColByte()`'s original
  form (`rddBombByteAt()`) resolved each output byte with an 8-iteration
  bit-scan loop, each iteration calling a second function
  (`rddBombPixelSet()`) - at 16 bombs x up to 7 columns x ~2 matching
  pages x 8 bits, up to ~1,800+ real nested function calls/frame just to
  resolve bomb pixels, consistent with this project's own repeated
  finding that per-call overhead, not raw instruction count, dominates
  cost on this platform. Rewritten as the single-shift `rddBombColByte()`
  described above - zero inner loop, zero nested call. Confirmed via the
  perf overlay: CPU with all 16 bombs live dropped from pegged/near-100%
  to a steady ~52-63%, with a single-bomb baseline (47%) confirming bomb
  count really was the scaling driver throughout. Rendering verified
  pixel-correct via screenshot at every step (overlapping-bomb
  compositing, player movement, score digits), and both temporary debug
  hooks (forced score/bomb-count, disabled collision) were fully removed
  and the real build re-verified clean afterward. Menu thumbnail added
  the same way as every other port - the existing 4x8 grid's cell 30
  (row 7, col 2) was still free (Oroboros had used cell 29 without
  needing to grow the canvas), so no grid growth was needed here either;
  `THUMBNAIL_COUNT` bumped 30->31, verified via screenshot that both the
  new thumbnail and its Oroboros neighbor (cell 29) render correctly.
- `src/games/gameFourInRow.c` - Four in a Row (Connect 4). From
  `more games/tiny-handheld/software/games/attiny-arcade/four-in-row/` -
  the third beyond-scope addition, and the first one with **no author
  name anywhere in its own source** ("connect 4 engine with custom
  functions / ported to ATTiny85 hardware....see pockeTETRIS" is the
  entire header comment) - not present in `webboggles/AttinyArcade`
  either, so unlike Oroboros/Run Dude Run there's no Ilya Titov credit to
  give here; menu credits it "UNKNOWN" and the README lists its license
  as "None specified" rather than guessing. Genuine #AttinyArcade
  hardware (A3/A0 analog axis + 2 discrete digital buttons), needing no
  new shim - though only `IsLeft()`/`IsRight()` (A3-based) and
  `IsAction()`/`IsCenter()` are ever actually called; `IsUp()`/`IsDown()`
  (A0-based) are declared but dead, confirmed by grep before dropping
  them. A connect-4-vs-simple-AI game: move a row selector up/down along
  the 7 drop-columns (mapped to screen pages, matching this project's own
  established A3-is-the-up/down-axis convention), drop with fire, first
  to connect 4 wins. `boardId`-selector pattern (0=real board, 1=AI
  scratch board) used instead of passing a 2D array as a function
  parameter, matching Tiny Dungeon's own resolve-by-id precedent - no
  existing port here passes a 2D array by pointer/value at all. One real
  out-of-bounds read caught by inspection before ever compiling: the
  anti-diagonal win check's own index formula uses `FIRO_BOARD_W-1-y-c`
  (7, the board's *width*) instead of the board's *height* (6) - upstream's
  own formula, preserved faithfully rather than "corrected" (tracing it by
  hand confirmed it still only ever matches a genuine connected diagonal
  whenever in-bounds, just anchored differently than a naive reader might
  expect) - but harmless-on-real-AVR out-of-range indices are a real
  out-of-bounds read here, so a bounds guard was added around just that
  one check, the same "preserve behavior, guard the crash" treatment as
  Tiny Arena's own `Lvl1` fix.
  **The AI's own real minimax-ish lookahead** (`AISCANS=20` random
  rollouts x `AIDEPTH=5` plies, per candidate column) runs *synchronously*
  upstream, a genuinely perceptible "thinking" pause on real 8MHz AVR
  hardware - spread across real engine frames from the start per this
  project's own "always check for optimizations, unprompted" standing
  instruction, needing two rounds before it was actually safe: round 1
  (one column - all 20 scans - evaluated per frame) still measured
  pegged 100% CPU for ~2 real frames per column even after eliminating
  the `boardId`-dispatch function-call overhead for the hot path
  (`firoScratchPlay()`/`firoScratchWin()`, operating directly on
  `firoAiCboard` with plain array reads instead of `firoCellGet()`'s
  dispatch - the same nested-function-call-overhead lesson just found in
  Run Dude Run's own bomb rendering); round 2 narrowed the granularity
  further to a single rollout *scan* per real frame (worst case 7
  columns x 20 scans = 140 real frames, ~2.3s at 60fps - still a
  perfectly reasonable board-game "thinking" pause) landing at a steady
  ~53% CPU with no risk of frame truncation.
  **The menu-text font needed four rounds of back-and-forth with the user
  to get right, since this project's usual "keep the pixels faithful,
  just fix the input mapping" precedent (established by Tiny Arkanoid)
  turned out not to be enough here** - this game's own side-status text
  ("PLAY"/"WINS"/"TIE", each pair of letters packed into one compressed
  8x8 glyph, upstream's own `font[][8]` table) is genuinely sideways by
  design (upstream's own comment: "Input for vertical screen
  orientation"), and getting a ROTATED word to actually read correctly
  needed the *exact* rotation transform, not just "any" rotation. Wrong
  guesses along the way: a column-only mirror (fixed letter order within
  each glyph, "YALP"->"PLAY", but left it upside-down); a naive full
  180-degree rotation of the ALREADY-mirrored data (overcorrected,
  un-fixing the order); an independently-derived "rotate the monitor 90
  clockwise" transform reasoned from the user's own precise description
  ("the bottom is actually on the right") - closer, but still not right,
  since a single 90-degree rotation doesn't account for BOTH axes needing
  correction simultaneously. The fix that actually worked came from a
  direct, explicit user instruction - "starting from originals rotate
  180 degrees and flip" - implemented as two literal sequential steps
  (which algebraically collapse to a plain per-byte bit-reversal, since
  the two column-order reversals from "rotate 180" and "flip" cancel out)
  applied to the ORIGINAL upstream byte values, not to any of the
  already-modified intermediate attempts. A final, separate fix swapped
  which page (3 vs 4) renders which glyph-pair, since the corrected
  glyph shapes still needed PL to visually read *before* AY. X/O/blank/
  empty glyphs are symmetric under every one of these transforms
  (verified directly against their own byte values each time), so only
  the 6 letter-pair glyphs (PL/AY/WI/NS/TI/E) ever needed new data.
  **Generalizable lesson**: for text/UI elements in a genuinely rotated
  presentation (not just "sideways art" like a game board), don't assume
  a single mirror or a generic 180-degree rotation is correct by
  reasoning alone - the exact transform needs either rigorous, verified
  derivation (a marker-pixel test through the actual tool being used to
  preview it, as eventually done here to nail down ImageMagick's own
  rotation convention with certainty) or a precise, literal instruction
  from the person who can actually see the result, and even a
  carefully-reasoned intermediate guess (the "rotate the monitor 90
  clockwise" one) can still be wrong if it only accounts for one of the
  axes that need correcting.
  **A real, reachable false-win bug found via a user-provided exact board
  state** (not a screenshot transcription - asked the user directly for
  the board as plain text once repeated screenshot-reading attempts on
  both sides kept landing on "so close, one cell short of 4" patterns,
  removing all pixel-transcription ambiguity): `firoComputerThinkStep()`'s
  AI evaluation compared each candidate column's accumulated score against
  the running best (`firoAiSmax`, initialized to -99999999) with **no
  check that the column had actually been playable** - an unplayable
  (already-full) column's score simply stays at its untouched initial 0,
  which still beats -99999999, so the AI could "choose" a full column.
  Committing that choice via `firoBoardPlay()` then correctly fails (the
  column really is full), falling into a defensive branch originally
  written under the assumption it was "practically unreachable" - which
  unconditionally declares a player win with no real board check at all.
  **This exact flaw exists in upstream's own original code too**
  (`if(mscore[m]>smax)`, same missing guard) - not something the port's
  own per-frame AI restructuring introduced (confirmed by checking: the
  very first synchronous, one-shot version of this project's own AI port
  had the identical bug, inherited straight from upstream). Fixed by
  adding a `firoAiColumnPlayable` flag and requiring it before a column's
  score is allowed to win the comparison. Verified entirely through code-
  level simulation per the user's explicit "no play testing" instruction
  mid-investigation: a Python port of the exact scoring/comparison logic,
  run against the user's own literal board text, reproduced the precise
  failure (buggy logic "chooses" the reported-full column, which then
  fails to play) and confirmed the fix picks a genuinely playable column
  instead. A temporary on-screen debug readout (three small marker rows
  encoding the winning check's type/x/y as raw glyph counts, added to
  help pin down *which* code path was firing if the bug recurred) was
  built, deployed for the user's own testing, and fully removed again
  (confirmed via grep) once no further recurrence was reported. Menu
  thumbnail added the same way as every other port - this filled the
  4x8 grid's last remaining cell (31), so `THUMBNAIL_COUNT` bumped 31->32
  and the grid is now **completely full** - and, as it turned out,
  already sitting right at Vircon32's own 1024x1024 texture-dimension
  cap (4 cols x 8 rows x 256x128 = exactly 1024x1024), so unlike every
  earlier grid-growth in this project's history, there is no further "add
  a row" option left at all - the next game needs a genuinely separate
  second texture (see Dino Game's own writeup immediately below for how
  that was actually built).
- `src/games/gameDinoGame.c` - Dino Game. From
  `more games/tiny-handheld/software/games/tiny-handheld/dino-game/` -
  the fourth beyond-scope addition, and the **last known candidate**
  from the original follow-up search (see this section's own intro) -
  unlike every other beyond-scope game, this one is an ORIGINAL creation
  of the `tiny-handheld` repo itself, not a port of an existing
  TinyJoypad/AttinyArcade game (a Chrome-offline-dino-style endless
  runner: jump cacti, dodge ground rocks, score climbs over time).
  Credited in the menu as "TINY HANDHELD" (the originating project, not
  an individual person - no author name is stated anywhere in this
  game's own source either, but unlike Four in a Row's genuinely
  anonymous origin, this one *is* explicitly documented elsewhere in the
  same repo as that project's own original work, so crediting the
  project itself is meaningfully informative rather than a guess).
  **The first game in this whole project built on the real
  `lexus2k/ssd1306` Arduino library** (a sprite + text API) instead of
  either tinyJoypadShim/obonoCoreShim or a hand-rolled raw-byte driver -
  the library itself isn't bundled in this repo (an external Arduino
  library dependency), so its own internal implementation was never
  read at all; instead this port inferred the exact intended pixel
  output directly from the game's own draw calls (sprite positions/
  widths, per-column byte data, text pixel coordinates), which turned
  out to be entirely sufficient - a real, generalizable finding: you
  don't need a library's own source if the *calling code* fully
  determines what ends up on screen. Concretely: `drawGround()` already
  streams one raw byte per column via `ssd1306_lcd.send_pixels1()` - the
  exact model this whole project's `md_drawColumn()` already handles, no
  reinterpretation needed at all. The sprite API's own `eraseTrace()`
  (a real-hardware optimization - track a sprite's last position, erase
  just that rectangle instead of clearing the whole screen) has no
  equivalent needed here at all, matching this project's own standing
  treatment of any upstream "only redraw what changed" trick - every
  sprite is just composited fresh from its current position every frame.
  Both sprites are one page (8px) tall but the dino's own Y is a genuine
  arbitrary pixel value (jump physics), not page-aligned like the fixed-
  height cactus - composited with a single sign-branched shift of the
  sprite's own column byte, the same safe sub-page-slicing technique
  already proven in Run Dude Run's own `rddBombColByte()`, just simpler
  (one 8-bit source byte instead of two combined into 16 bits, since
  these sprites are only 8px tall not 16). Text
  (`ssd1306_printFixed`/`ssd1306xled_font5x7`) reuses this project's own
  already-extracted standard 95-char ssd1306xled font (confirmed the
  identical `font6x8.h` file is bundled in this same repo, already
  ported once for both Oroboros's and Run Dude Run's own text) rather
  than needing a fresh extraction - each game keeps its own self-
  contained copy per this project's standing precedent. EEPROM high-
  score persistence dropped (session-in-memory only), matching every
  other port. Upstream's own real-time tick throttle (`millis()`-based,
  ~71fps - genuinely *faster* than this engine's native 60fps, unlike
  every other port's own throttle which has always asked for something
  *slower*) was dropped entirely rather than built into an accumulator,
  since there was nothing to gain from replicating a throttle asking for
  more ticks than the engine can natively provide anyway - a deliberate,
  documented ~15% pace simplification. One latent uninitialized-read
  risk caught by inspection before ever compiling: a bucket-scan loop
  that could complete without ever assigning its own output variable on
  a specific edge-case input - fixed with an explicit default.
  **A real, proactive CPU-load fix applied before ever shipping, per
  standing instruction and verified via code inspection only (not
  tested in the emulator)**: the cactus layer had the classic O(pixels x
  objects) shape (128 columns x 6 cactuses = up to 768 checks/frame) -
  composited into a shared per-frame page buffer instead (cutting it to
  ~48 iterations), the same lesson applied throughout this project. The
  dino sprite and ground/rock layers were already call-site-gated/cheap
  from the start and needed no further change.
  **A real bug found via direct user report right after shipping - not
  a rendering bug at all, but the game silently never appearing in the
  menu in the first place**: `menu.c`'s own `MAX_GAMES` cap was
  hardcoded to 32, and Dino Game was the 33rd game registered (via
  `addGame()`, called from `menuGameList.c`) - `addGame()`'s own
  capacity guard (`if(gameCount>=MAX_GAMES) return;`) silently no-ops
  once the cap is hit, with no warning or error of any kind, so the
  game was simply never added to `games[]` at all. Fixed by bumping
  `MAX_GAMES` to 48, giving headroom for several more future ports
  without needing another bump each time - confirmed via screenshot
  that "DINO GAME" now appears correctly in the alphabetized menu list.
  **Menu thumbnail needed a genuinely new mechanism, not just "the next
  free cell"**: the existing 4x8 grid (`assets/thumbnails.png`) was
  already completely full AND already at Vircon32's hard 1024x1024
  texture-dimension cap (see Four in a Row's own writeup above) - no
  further growth of that single texture was possible at all. Solved
  with a genuine second cartridge texture (`assets/thumbnails2.png`,
  `obj/thumbnails2.vtex`, registered as texture id 2 in `rom.xml` and
  both `Make.sh`/`Make.bat`) - a small 4x2 grid (1024x256, 8 cells of
  headroom for several more games before a *third* texture is ever
  needed) - `md_drawGameThumbnail()`/`md_getThumbnailCount()` in
  `portVircon32.c` now dispatch between the two textures by gameIndex
  (0..31 in the original atlas, 32+ in the new one), treating them as
  one contiguous logical thumbnail space to every caller in `menu.c` -
  no changes needed in `menu.c` itself at all. Verified via screenshot
  that Dino Game's own thumbnail (texture 2, cell 0) and Four in a Row's
  neighboring thumbnail (texture 1, cell 31, the original atlas's own
  last cell) both render correctly with no cross-contamination between
  the two textures.

## Licensing

Tiny Invaders v4.2 is GPLv3 (its `tinyJoypadUtils`/driver lineage). Since a
GPLv3 game is compiled into the cartridge, the cartridge as a whole is a
GPLv3 combined work - `LICENSE.txt` (repo root) is GPLv3 for this port's own
new code (shim, menu, `portVircon32.c`), unlike the sibling
`crisp-game-lib-portable_vircon32` project (MIT throughout, since every
upstream source there was MIT). Each game's own original
license/attribution is preserved in a header comment in its ported `.c`
file: NumberPlace/t2048/HollowSeeker stay MIT/OBONO-attributed; Tiny
Dungeon also stays MIT (its own header: "MIT License", "Developer: Sven
B", contact Lorandil@gmx.de - the same contact as Tiny Minez, credited
the same way, "SVEN B / LORANDIL"); Tiny
Invaders, Tiny Pinball, Tiny Pacman, Tiny Bomber, Tiny Doc, Tiny Bert,
Tiny Tris, Tiny Arkanoid, Tiny Trick, Tiny Minez, Tiny Missile, Tiny Bike,
Tiny Arena, Tiny Gilbert, Tiny Pipe, Tiny Morpion, Tiny Plaque, Tiny
SQuest, Tiny DDug, and Tiny Lander all stay GPLv3 (Tiny Lander's own
header credits "Roger Buehler" / GitHub handle "tscha70" - a different
author from every Daniel-C/Sven-B title above, credited separately in
the menu as "ROGER BUEHLER"). Wren Rollercoaster, Frogger, Bat Bonanza,
Stacker, and UFO are the exceptions - all five share the same looser
"non commercial [use] with attribution" license (crediting Billy
Cheung's own ATtiny-Joypad port adaptation and the separate
`ssd1306xled`/Neven-Boyanov-authored font each builds on), though not
all under the same original author: Wren/Frogger/Bat Bonanza/Stacker are
all Andy Jackson's own games (Frogger's own credit also names
@senkunmusashi for its artwork bitmaps), while UFO is credited to a
different original author, Ilya Titov - Jackson himself only combined
UFO alongside his own Stacker into one shared cartridge, not authored it -
credited in the menu as "ILYA TITOV" accordingly, not Jackson. Oroboros
and Run Dude Run (see "Beyond the original scope" above) share this same
license family and are also Ilya Titov's own, sourced directly from his
own `webboggles/AttinyArcade` repo rather than a combined-cartridge
mirror. Four in a Row and Dino Game are the two exceptions to every
license-family grouping above - neither's own source carries an author
name or a license statement at all (neither is present in
`webboggles/AttinyArcade` either, only in
`Yevgeniy-Olexandrenko/tiny-handheld`'s own bundle, which itself has no
repo-wide LICENSE file covering either). Four in a Row is credited in
the menu as "UNKNOWN" (genuinely anonymous, no context at all as to its
origin); Dino Game is credited as "TINY HANDHELD" instead of "UNKNOWN"
since it's specifically documented as an original creation of that
project itself (not sourced from anywhere else, unlike every other game
in this whole cartridge) - crediting the originating project is
meaningfully informative here, not a guess, even though no individual
person is named. Both are listed in the README as "None specified"
rather than guessing a license that isn't actually stated anywhere.
Preserved as their own distinct license text in each file's own header comment
rather than relabeled GPLv3, though the cartridge as a combined work
remains GPLv3 overall regardless (a project-level determination already
settled by Tiny Invaders' own GPLv3
lineage, not something that changes per individual game file).
Attribution (corrected mid-session after checking each `.ino`'s own
header comment directly, rather than trusting this file's earlier,
imprecise "Lorandil" shorthand): Tiny Pinball/Pacman/Bomber/Doc/Bert/
Tris/Trick are "Daniel C"; Tiny Arkanoid's own header spells the same
name out in full, "Daniel Champagne" (contact `phoenixbozo@gmail.com` -
the same "phoenixbozo" already credited above for the shared
`tinyJoypadUtils`/`FastTinyDriver` display driver, so likely the same
person as "Daniel C" using a fuller name); Tiny Invaders v4.2 credits
"Daniel C" as original programmer plus "Sven B" for this specific v4.2
release's enhancements; Tiny Minez credits "Sven B" as programmer, with
contact email `Lorandil@gmx.de` - the same contact address TinyDungeon's
own header also uses for its "Sven B"-credited author, strongly
suggesting "Lorandil" (the GitHub account these repos are hosted under)
and "Sven B" are the same individual rather than two different people.
Games are credited in the menu using whichever name(s) each upstream
header actually states, rather than the single guessed name used
earlier this session.

## Project-wide render optimization attempt: column-run coalescing (tried, measured a regression, fully reverted)

Prompted by a direct question ("would RLE-encoded images / drawing one
rect for a run of same-color pixels be faster for all games?") after Tiny
Missile's own CPU-load fix - the underlying reasoning looked sound and
matched this project's own established cost model (a GPU blit's *call*
overhead dominates, not the pixel area it covers), and `md_drawSolidRect()`
already proved stretching one atlas tile via `set_drawing_scale()`+
`draw_region_zoomed_at()` works mechanically. Rolled out project-wide (all
14 games/shims, not piloted first, per explicit request) as two new
primitives, `md_drawColumnRun(col,page,value,count)` and
`md_drawColumnRow(page,values)` (coalesces consecutive equal-value runs in
a row into single stretched blits instead of one `md_drawColumn()` call
per column).

**The user then measured it in the real native emulator (not a synthetic
benchmark) and reported a genuine regression** - a screenshot showing
"On-time 50/60 (83%)" and a dense/spiky CPU graph, worse than before the
change, specifically calling out that even Tiny Doc with many pills felt
slower, not faster. Investigated by reading the actual emulator engine
source (`V32Console.cpp`) rather than re-reasoning from the documented
cost model alone: **GPU load is pixel-count-based** (`GPULoad =
GPUUsedPixels / GPUPixelCapacityPerFrame`), not call-count-based - so
stretching a run wider doesn't reduce GPU cost the way reducing draw
*calls* reduces CPU cost. Worse, `set_drawing_scale()`+
`draw_region_zoomed_at()` (used for any run >=2 columns) evidently costs
*more* CPU cycles per call than plain `draw_region_at()`, and most real
game content produces short 2-3-column runs, not long ones - so the
"fewer calls" win from coalescing was smaller than the "each surviving
call costs more" penalty for the overwhelming majority of actual content,
a net loss rather than a win.

**Fully reverted** across all 14 game/shim files back to plain
per-pixel `md_drawColumn()` calls, plus removed the two now-unused shared
primitives from `machineDependent.h`/`portVircon32.c` - confirmed via a
final `grep -rn "md_drawColumnRun\|md_drawColumnRow"` sweep (zero matches)
and a grep for every leftover per-game row-buffer variable name added for
this rollout (also zero matches), then rebuilt and Puppeteer-screenshot-
verified all 14 games render correctly on the reverted build. This was a
judgment call, not an explicit user instruction to revert - made directly
once the measured evidence was conclusive, consistent with this project's
own standing practice of trusting real measurement over a theory-only
cost model, even a previously-reliable one.

**Generalizable lesson**: "fewer draw calls is always better" is only
true for the CPU-cycle side of Vircon32's cost model; the GPU-pixel side
doesn't care about call count at all, and a "smarter" replacement
primitive can itself cost more per call than the naive one it replaces.
Any future call-reduction optimization on this platform needs to be
verified with a real before/after measurement (see the performance
overlay below), not shipped on cost-model reasoning alone, however solid
that reasoning seemed the last several times it held up.

## In-browser performance overlay (WebGL build)

The native desktop emulator (`C:\utils\Vircon32\Emulator\Vircon32.exe`)
has its own built-in ImGui CPU/GPU load graph overlay, but automating
input into that separate native GUI process from this environment proved
unreliable (SendKeys/SendInput/SetForegroundWindow attempts all failed to
reliably land - see git/session history) - so a matching overlay was
built directly into this project's WebGL/Puppeteer test build instead
(`C:\github\WebEmulator\DesktopEmulator\Emulator\MainWeb.cpp`/
`shell.html`), modeled on the native overlay's own underlying mechanism
(`V32Console::GetCPULoad()`/`GetGPULoad()`, each a 0-100% share of that
frame's cycle/pixel budget) after reading `ComputerSoftware_X`'s source as
reference.
- **Hidden by default, toggled with F6** (`#perf-overlay` uses the same
  `.hidden`-class convention as this file's other overlays - a first
  attempt used a bare `display:flex` CSS rule that overrode the `hidden`
  attribute's own default, a real bug, fixed by switching to `.hidden`).
- `MainWeb.cpp`'s per-frame loop pushes `CpuLoad`/`GpuLoad` out via
  `EM_ASM(Module.onPerfUpdate($0,$1))`; `shell.html`'s JS side keeps an
  80-sample scrolling history per meter and draws a bar graph (green/
  yellow/red by threshold), reusable via `require('puppeteer')` screenshot
  captures the same way every other test in this project works.
- **A real bug found while building this**: `Module.onPerfUpdate = ...`
  was originally assigned at top-level script scope *before* `var
  Module = {...}` was actually assigned later in the same script block
  (JS only hoists the `var` declaration, not the assignment) - threw an
  uncaught exception that aborted the rest of the script, which looked
  exactly like "the ROM doesn't load" from the outside. Fixed by moving
  the assignment to right after `var Module = {...};`.
- **Always use this overlay (not a synthetic estimate) for any future
  performance work in the WebGL build** - screenshot it via Puppeteer,
  crop/zoom the top-left corner with ImageMagick if the digits are too
  small to read directly, and read the actual number rather than
  estimating from visual busyness.

## Tiny Doc: row-scoped dirty tracking (found via the new perf overlay)

Requested stress test: prefill Tiny Doc's board to near-full and check
whether `tdCompositeTabIntoBuffer()`'s existing dirty-flag cache (see its
own history in the Status section above) holds up under that load, using
the new perf overlay above rather than guessing.

**Two bugs found in the debug stress-test hook itself, not in the real
game**, worth remembering for any future synthetic stress-test scaffold:
1. The debug fill (`tdDebugFillBoard()`, called once at init) was wiped
   the instant real gameplay started, because `tdBeginLevel()` (fired by
   the ATTRACT screen's own Fire-press handler) calls
   `tdInitPublicVarForNewLevel()`, which unconditionally zeroes the whole
   `tdTab` grid - fixed by calling the debug fill *after* `tdBeginLevel()`
   returns, not before it's ever reached.
2. The debug fill's cell-value generator used `tdOrderSelect()` - which
   is specifically the *virus* value generator (values 1-3) - instead of
   a genuine pill-half value. Since `tdCountVirusTypes()` treats any cell
   value 1-3 as a virus regardless of intent, this miscounted almost the
   entire filled board as viruses (visible as an absurd "V:56"/"V:64"
   reading), which then corrupted the on-screen score via
   `tdFirstCalculeDisplay()`'s virus-count-delta formula - garbled digits
   in the SCORE readout that looked exactly like a real rendering bug
   until traced back to the fill data. Fixed by switching to a plain
   `arand(3)+1+10` pill-half color (matching `tdGenerateSidePill()`'s own
   value range, no direction bits needed since the debug fill doesn't
   need genuine connected pairs).

**The real, generalizable finding**: with the board genuinely packed
(all columns except the pill-spawn lane, to still allow a real fall), a
single deliberate pill lock previously forced `tdCompositeTabIntoBuffer()`
to recompute *all 7* tab-bearing page rows (`tdTabDirty` was one global
flag covering the whole grid), even though a single lock only ever writes
1-2 known grid cells. Replaced the single `bool tdTabDirty` with a per-
page-row `bool[8] tdPageRowDirty` plus two helpers: `tdMarkAllRowsDirty()`
(used by every mutation site whose touched range is unpredictable or
genuinely whole-grid - level init, the clear/drop resolve cascade, the
virus idle-animation tick) and `tdMarkGridRowDirty(gy)` (marks only the
page row(s) whose grid-row range, per the existing `tdReturnScanLineY()`
table, actually covers grid row `gy`). `tdFixPill()` - by far the single
most frequent grid mutation during real play, one call per pill lock -
was switched to the precise per-row marking, cutting its recompute scope
from all 7 page rows to the 1-2 that actually changed. Every other
mutation site kept the safe `tdMarkAllRowsDirty()` fallback, zero
behavior change there. Verified: normal (non-debug-filled) gameplay
renders identically after the change (score/virus-count/board all
correct), and the change is a pure narrowing of an already-correct
invalidation scope with no new risk.

**Caveat on quantifying the improvement**: the perf overlay's CPU% meter
clamps to 100, so a saturated reading can't show *how far* over budget a
frame is or by how much a fix reduced that - an aggressive synthetic
"spam locks every 120ms" test read ~100% both before and after this fix,
which is inconclusive by itself (could mean "still over budget but less
so" or "no change" - the meter can't distinguish). A more realistic test
(board packed, piece merely falling under normal gravity with no manual
spamming - the scenario the user actually asked about) showed CPU
oscillating between ~6-8% (idle, cache hit) and 100% (the frame a lock's
recompute lands on), which is the expected sawtooth shape for a dirty-
cache design - narrower recompute scope should shrink the *size* of each
spike even where the crude 0-100 meter can't prove it numerically. This
project's testing throughout has used `SKIP_V32OPT=1` (unoptimized)
builds, consistent with prior sessions - `v32opt`'s real inlining/DCE
(268 optimizations last time it was tried, never independently
re-verified since - see the CPU-load investigation section above) remains
a separate, larger, not-yet-confirmed lever for lowering these numbers
further if they still prove to be a problem in practice.

## Visual testing setup (useful for future sessions)

No screenshot/browser-automation tool is available directly in this
environment, but a working headless-browser test loop was set up using
tools already present on this machine:
- `C:\github\WebEmulator\DesktopEmulator\WebBuild` - a WebGL/Emscripten
  build of the Vircon32 emulator. It loads `game.v32` from its own
  directory (relative fetch), so testing a build means copying
  `bin/tinyjoypad.v32` over that file (the original demo ROM there was
  backed up to `game.v32.orig` first).
- Served locally via `python -m http.server` from that directory.
- `C:\github\emsdk\node\<version>\bin` has a bundled Node/npm (this machine
  has no system-wide Node install) - used to `npm install puppeteer` and
  drive a headless Chromium (needs `--use-gl=angle --use-angle=swiftshader
  --enable-unsafe-swiftshader` launch flags for software WebGL to actually
  work headless) to load the page, send keydown/keyup events, and
  screenshot. The BIOS boot splash takes a few seconds to clear before the
  cartridge's own menu appears - wait for it before interacting.

## Prior art in this codebase / author's other projects

- **crisp-game-lib-portable (SDL port)** — precedent for the "one binary,
  many games, shared menu" structure this project follows.
- **playdate-arduboy / srcstub** — closest existing precedent for writing a
  compatibility shim that reimplements a small monochrome-OLED game API on
  top of a different target's real hardware.
- **v32opt** — Vircon32 assembly-level optimizer project; useful reference
  for how the Vircon32 compiler's generated code behaves, in case
  performance tuning is needed for any of the ported games.

## References

- https://www.tinyjoypad.com/tinyjoypad_attiny85
- https://www.vircon32.com/ — home, specs, C API reference, dev tools
- https://www.vircon32.com/api/video.html — GPU/drawing API reference

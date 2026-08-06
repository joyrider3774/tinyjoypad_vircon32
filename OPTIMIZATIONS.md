# Per-game optimizations

Vircon32's budget is a hard 250,000 CPU cycles/frame - if a frame's work
doesn't finish inside that, the engine just stops mid-instruction-stream
until the next frame. Most fixes below share one of two shapes:
**per-pixel work that didn't need to run every pixel** (composite sprites
once per row/object into a buffer instead of re-scanning per pixel), or
**a self-gated function that still costs a full call every time it's
invoked** (gate the *call site* too, not just the function body). Full
details/measurements are in `CLAUDE.md`; this is just the summary.

- **NumberPlace / 2048 / HollowSeeker** - obonoCoreShim's own shared
  `drawSprites()` already composites per page row, not per pixel - no
  extra work needed.
- **Tiny Invaders** - row-gated 5 UI layers to their real footprint;
  cached the monster-grid cell lookup per ~14px cell instead of
  recomputing every pixel; call-site-gated monster/shot draws to their
  own known row/column range.
- **Tiny Pacman / Tiny Bomber** - replaced per-pixel sprite re-scan with
  per-page-row compositing into a shared buffer; O(1) bit-index math
  instead of a repeated-subtraction loop for block/dot bitmaps.
- **Tiny Doc** - row/x-range gating; per-row sprite compositing; skip a
  virus-overlay blit that was provably always zero; row-scoped dirty-flag
  cache for the locked-grid layer (only recompute rows that changed).
- **Tiny Bert** - per-object-per-page sprite compositing from the start;
  later added plate-grid dirty-flag caching + attract-screen gating.
  Baseline stayed ~70-76% either way - dominant cost is sheer draw-call
  volume from dense background art, not compositing logic.
- **Tiny Tris** - grid/piece/preview composited per row from the start;
  attract screen only redraws on the exact frame its content changes
  (whole-screen dirty flag) instead of every frame.
- **Tiny Arkanoid** - decoupled logic-tick rate from redraw rate (fixed
  an ~8x speed bug); call-site row/x-gated all 6 render layers.
- **Tiny Trick** - sprite compositing from the start; frozen-frame cache
  during the multi-second goal-scored sequence (recompute once, reuse for
  every held frame); inlined a 1024x/frame background-mirror lookup.
- **Tiny Missile** - two-round fix: per-page-row compositing for all
  sprite layers, then analytic (not scanned) per-row trail-column range
  for missile trails.
- **Tiny Bike** - per-object footprint gating for the player sprite and
  the obstacle map compositor.
- **Tiny Arena** (raycaster) - precomputed per-row texture lookups and
  cached per-run column classification for sprite scaling; inlined a
  tiny but extremely hot (~3200 calls/frame) helper; downsampled a
  ~217-call synchronous death-sound sweep to a frame-stepped one.
- **Tiny Pipe** - per-row sprite compositing (player + turtles).
- **Tiny Morpion** - call-site-gated 3 menu sprites to their exact known
  footprint; removed a score-digit call that was provably always zero.
- **Tiny Plaque** - removed a stray unconditional full-screen redraw call
  left over from porting (upstream only redraws 1-in-6 ticks); per-row
  food-sprite compositing; call-site gating for score/tube overlays.
- **Tiny SQuest** - per-row compositing for enemies and main/sub/
  ballistic sprites.
- **Tiny DDug** - call-site gating for the player sprite and score;
  per-row cache for tunnel-wall reads instead of two lookups per pixel.
- **Tiny Lander** - split the render loop on the fixed instrument-panel
  vs. game-area boundary (these never overlap), then row-gated each
  individual dashboard layer to its own single matching row.
- **Wren Rollercoaster** - every text/number layer row-gated to its real
  footprint from the start (no retrofit needed).
- **Frogger** - bounded an unbounded dock-scan loop; cached a per-column
  digit-count recompute.
- **Bat Bonanza (Pong)** - hoisted a per-pixel division out of the render
  loop; cached `strlen()` per row instead of per pixel on the attract
  screen.
- **Stacker** - cached the score's digit count once per frame instead of
  recomputing it on every HUD pixel.
- **UFO** - per-page compositing for the obstacle list and star field;
  cached score digit count.
- **Tiny Dungeon** - the deepest optimization pass in the project, five
  rounds: (1) hoisted wall/object visibility checks out of the per-column
  loop into a once-per-frame precompute pass; (2) replaced full 24/99-
  entry scans with compact lists of only the actually-visible entries;
  (3) computed per-object scan constants once per column instead of once
  per row/mask-vs-bitmap call; (4) row-range-gated the sprite scaler to
  each object's real vertical extent (as narrow as 2 of 8 rows); (5)
  merged the separate mask/bitmap scans into one pass and resolved each
  sprite's bitmap array via a runtime pointer instead of a per-byte
  dispatch chain. Fixed real frame truncation and dropped an idle scene
  from a saturated 100% to 85%; a close-up sprite view can still spike to
  100% - the remaining cost would need a bigger rendering-model change
  (pre-render each sprite once per frame instead of per-column scanning).
- **Oroboros** - none needed. Plain grid-cell lookups only (no sprite
  bitmaps, no font-dispatch chains) - the simplest render model of any
  port in this project, measured at a steady ~50-56% CPU with no
  optimization pass required.
- **Run Dude Run** - two rounds, both found via the user reporting CPU hit
  100% once score passed 1000 (the point `rddTotalBombs` starts climbing
  toward its 16-bomb cap with no ceiling check). Round 1: composited all
  live bombs into a shared per-page buffer instead of rescanning all 16
  at every one of 1024 pixels/frame (up to 16,384 checks/frame down to a
  few hundred). Measured with a temporary debug hook forcing 16 bombs:
  CPU was *still* pegged near 100% after round 1 - round 2 found why:
  each bomb byte was resolved via an 8-bit-scan loop calling a second
  function per bit (up to ~2,000 nested function calls/frame at 16
  bombs) - replaced with a single sign-branched shift of the sprite's
  16-bit column value (top+bottom byte combined), no inner loop or
  nested call at all. Confirmed via the perf overlay: 16 bombs live
  dropped from pegged/near-100% to a steady ~52-63%, correct rendering
  verified via screenshot throughout both rounds.
- **Four in a Row** - none needed for rendering (a simple board of static
  glyph lookups, no sprite/object scanning). The AI's own minimax-ish
  lookahead was spread across real frames *proactively*, before ever
  shipping, once a back-of-envelope estimate suggested a single-frame
  synchronous evaluation risked exceeding the cycle budget - one column
  (20 rollout scans) per frame measured pegged CPU at 100% for ~2 frames
  per column, so narrowed further to one rollout scan per frame (worst
  case 140 real frames total, ~2.3s - still a reasonable "thinking"
  pause), landing at a steady ~53% with zero risk of frame truncation.
- **Dino Game** - one proactive fix, applied before ever shipping and
  verified via code inspection only (per direct instruction not to test
  it): the cactus layer had the classic O(pixels x objects) shape (128
  columns x 6 cactuses = up to 768 checks/frame). Composited into a
  shared per-frame page buffer instead (cuts it to ~48 iterations), the
  same lesson applied throughout this project. The dino sprite itself and
  the ground/rock layer were already call-site-gated/cheap from the
  start, needing no further change.
- **SnakeGame85** - one fix, a direct consequence of a display-
  orientation bug fix (real hardware runs this game column/row-reversed
  and bit-reversed - see CLAUDE.md): the bit-reversal was first done as
  an 8-iteration shift/or loop for every one of 1024 pixels/frame (8192
  total), pushing a real frame over budget (visible as partial-frame
  truncation). Replaced with a precomputed 256-entry lookup table (every
  byte's own reversal is a pure function of its 8-bit value, the same
  "bake all 256 byte values into a table" approach the column atlas
  itself already uses) - CPU dropped to a steady ~5-6%.
- **Jump Slime** - the current stage's 12-13 blocks were rescanned from
  inside the per-pixel render call, and player/coin/enemy sprites were
  handed to the sprite blitter across all 128 columns regardless of
  their real ~8px footprint. Fixed with two per-page composite buffers
  (blocks read directly since they're always page-aligned; sprites still
  call the blitter but only across their own real column span) - dropped
  from a pegged, frame-truncating 100% to a steady 60%.
- **TinyRoG** - the map-tile renderer was called once per pixel even
  though 8 consecutive columns always belong to the same tile (a
  redundant-lookup waste, not the O(pixels x objects) shape). A first
  attempt added a per-tile cache but measured *no* improvement, since the
  call itself (not the lookup) was the cost - fixed properly by
  compositing each page row by walking tiles (at most 17/row) instead of
  pixels (128/row) into a shared buffer. Dropped from a pegged 100% to a
  steady 60%.
- **TinY Fi** - already composited per-character into a per-page buffer
  from the start, but that composite still called the generic per-column
  sprite blitter once per column per layer per character (up to ~20 x 5
  x 4 = 400 calls/page), each redundantly recomputing the same per-row
  constants. Fixed by hoisting that computation to run once per
  (character, layer, page) instead of once per column; the same lesson
  was then applied to the HUD row too. Dropped from a saturated,
  frame-truncating 100% to a steady 50-54%.
- **Breakout** - none needed. Measured 39% CPU during gameplay, 66% on
  the attract screen - both comfortably under budget as shipped.
- **Space Attack** - the alien-fire render check queried every one of
  1024 pixels/frame individually, including a 5-slot fire-position scan
  per pixel. Composited into a shared per-page row buffer instead,
  touching only each feature's own real column range - dropped from
  93-98% to a steady 44%.
- **Falling Blocks** - two layers needed fixing: the per-cell game-grid
  renderer recomputed the same block/ghost-cell lookup up to 6 times in
  a row (one board row spans 6 physical columns), and the attract
  screen's fixed title text/credits were called unconditionally across
  all 1024 pixels/frame despite each only ever being nonzero within a
  narrow known footprint. Fixed with a per-row composite buffer for the
  game grid and call-site gating (a literal duplicate of each callee's
  own bounds check) for the attract-screen text. Attract screen dropped
  from a pegged 100% to 5-6%, gameplay from a pegged 100% (with a
  visibly truncated frame) to a steady 5-9%.
- **Tiny Mania** - the largest optimization pass in the project after
  Tiny Dungeon's, several rounds: (1) replaced 286 individual
  `drawSprite2Bit()` calls drawing the attract screen's own solid border
  lines one pixel-column at a time with direct buffer writes (100% ->
  46%); (2) replaced an O(cells x ghosts) per-cell ghost scan (up to
  1176 redundant comparisons/frame) with a per-frame O(ghosts) bucket-
  linked-list index; (3) a specialized blitter for the shared w=9,h=7
  sprite shape (walls/dots), skipping the generic blitter's own frame/
  plane-size arithmetic; (4) a 30fps whole-tick throttle (halves average
  load, though not the peak cost of frames that still run full logic -
  see CLAUDE.md for why that distinction matters), with sound
  sequencing specifically rescaled to keep its own real-time pace
  despite the halved tick rate; (5) a wall-column batched writer (7
  bit-by-bit writes collapsed to <=4 masked read-modify-writes) - a
  first attempt shipped a real bug (reversed shift direction, found via
  direct user report), fixed by re-deriving the bit math from scratch
  and validating it against 20,000 randomized test cases before
  re-shipping; (6) a dirty-flag cache for the score/lives HUD row,
  only recomputed when the underlying values actually change. Full
  story, including two further correctness bugs found via user reports
  during this same pass (an instant-game-over state-machine bug, and a
  62-note sound sequence stretched to 13.6x its real duration), in
  CLAUDE.md.
- **Blocks Gold** - none needed. Built on the same per-page composite
  rendering technique already proven in the sibling Falling Blocks (both
  share the same underlying engine), so it was efficient from the start:
  measured 64% CPU on the attract screen, 55% during dense gameplay -
  both comfortably under budget. The one real fix in this port wasn't a
  CPU issue at all: the title-screen melody genuinely blocked gameplay
  start for ~12.9-25.8 real seconds (traced directly to upstream's own
  per-note duration values, not a porting artifact) - scaled down 0.3x
  (pitch unchanged) rather than downsampled by skipping notes, since it's
  a real composed tune worth keeping intact rather than a computed sweep.
  See CLAUDE.md for the full story.
- **Astro Barrier** - the render loop called `barrBulletByte()`/
  `barrTargetByte()` (the latter 3x, once per target) unconditionally for
  every one of 1024 pixels/frame, each a full function call with its own
  struct-pointer dereference - levels with a 32x32 large target or
  multiple simultaneous targets paid this worst, matching a direct user
  report of certain levels hitting 100%. Fixed with a per-page composite
  buffer (the same technique already proven in Falling Blocks/Blocks
  Gold) writing each object - player, bullet, all 3 targets, the bullet-
  count text - directly into a shared row buffer, gated to its own real
  bounding box. Diagnosed and fixed via code inspection only, per direct
  user instruction not to test it in the emulator; the user tested it
  independently afterward and confirmed it was fine. See CLAUDE.md for
  the full story.
- **ATtiny Snake** - the PLAYING-state grid rendering was built with an
  occupancy grid (O(1) per-cell lookup) from the start, so it never had
  the O(pixels x objects) issue - but a direct user question right after
  shipping ("did you optimize it?") prompted a proactive re-check, which
  found the ATTRACT screen's own "S" logo reveal animation *did* have
  it: checking up to 19 revealed trail cells against every one of 1024
  pixels/frame. Fixed the same way, before any user report of it being
  slow: a small occupancy grid mirroring the PLAYING state's own,
  updated once per ~100ms reveal-step instead of rescanned per pixel.
  Measured 20% CPU at rest during play, 28-36% during the attract
  screen's own animation - comfortably under budget both before and
  after the fix.
- **Meteor Storm** - a small, targeted per-digit hoist (compute each
  score digit's decimal value once instead of re-dividing it on every one
  of its own 6 columns) - never measured as a real problem, applied
  proactively on request. Comfortably under budget throughout (19-38%
  CPU). The one real fix in this port wasn't a CPU issue at all: a
  rendering-correctness bug where the border/player/obstacle layers'
  plain-assignment compositing let one layer's own byte write fully
  clobber a different layer's bits sharing the same (column, page)
  address whenever they occupied different real pixel rows within that
  one byte - switched every layer to OR-based compositing instead. See
  CLAUDE.md for the full story, including how it was diagnosed after an
  initial "maybe it's just obstacles" misdiagnosis was directly corrected
  by the user.

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

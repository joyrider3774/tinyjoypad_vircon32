// =============================================================================
// Tiny Morpion - ported from Daniel C's Tiny-Morpion.ino (tinyjoypad.com,
// GPLv3). Same tinyJoypadShim lineage as every other Daniel-C game here
// (FastTinyDriver.h) - Sound()/isXPressed() reuse the existing shim as-is.
//
// A tic-tac-toe (French: "morpion") game against a CPU opponent with 3
// difficulty levels. Move the cursor with the d-pad, place a mark with
// Fire; first to a line wins the round (best of up to 9 rounds - reaching
// 9 wins ends the whole match and returns to the difficulty menu).
//
// Button mapping (matches every other Daniel-C game's own established A0/
// A3 thresholds, confirmed directly against this game's own
// `spritebank_TMORPION.h`, which - unusually for this project - is where
// this particular game's own author put the button-threshold #defines,
// not `ELECTROLIB.h`):
//   analogRead(A0) in (500,750) = isRightPressed()
//   analogRead(A0) in [750,950) = isLeftPressed()
//   analogRead(A3) in (500,750) = isUpPressed()
//   analogRead(A3) in [750,950) = isDownPressed()
//   digitalRead(1) (active low) = isFirePressed() - place a mark, confirm
//     the difficulty menu.
//
// Structural changes from upstream:
//  - `BOARD[3][3]` (accessed both 2D, `BOARD[y][x]`, and flat via
//    `uint8_t *p = &BOARD[0][0]; p[0..8]`) flattened to a single
//    `int[9] tmorpionBoard`, with every `BOARD[y][x]` site rewritten as
//    `tmorpionBoard[y*3+x]` - avoids needing to test whether this dialect
//    supports taking the address of a 2D array element and using it as a
//    flat pointer, and the code already treated it as flat 90% of the
//    time anyway.
//  - upstream's `loop()` is a `NEW_GAME:` goto-chain around a difficulty-
//    select menu (with its own nested busy-wait-for-release loops) and a
//    gameplay `while(1)` - rewritten as an explicit frame-stepped state
//    machine (`tmorpionState`), same approach as every other
//    tinyJoypadShim port here. The genuinely blocking pieces (see below)
//    each got their own dedicated state.
//  - `BLINK_WINNER_TMORPION()` (a real 10-iteration, two-redraw-per-
//    iteration blocking blink of every mark belonging to the winning
//    player - not just the 3 cells in the winning line, confirmed by
//    reading the function directly) became a `TMORPION_STATE_BLINK_WINNER`
//    sub-state advancing one half-iteration per real engine frame (20
//    half-steps total). `NULL_GAME_TMORPION()`'s own 30-iteration draw
//    "buzz" (real `_delay_ms(4)` between tones) became
//    `TMORPION_STATE_NULL_GAME_BUZZ`, one step per frame.
//  - **The largest computed sound sweep in this project's history**:
//    `SND_BOX_TMORPION(5)` (played when the CPU wins the whole match) is
//    `for(t=200;t>10;t--){Sound(200-t,3);Sound(t,12);}` - **190
//    iterations x 2 calls = 380 synchronous `Sound()` calls**, edging out
//    Tiny Pipe's own ~500-call death sweep only because that one still
//    holds the literal top spot, but in the same extreme ballpark and
//    the largest *dual-tone* sweep found - same root cause/fix as every
//    other one of these: converted to a downsampled (step -15 instead of
//    -1, ~13 dual-tone steps) frame-stepped sweep. `SND_BOX_TMORPION(4)`
//    (player wins the whole match) is a smaller but still real 38-call
//    alternating two-tone alarm (`for(t=1;t<20;t++){Sound(4,80);
//    Sound(100,80);}`) - each note has a genuine ~25-40ms felt duration,
//    so this one didn't need downsampling, just the standard frame-
//    stepped treatment. `SND_BOX_TMORPION`'s own cases 2/3 (which called
//    `COMPLETED_PROCEDURE_TMORPION()`/`NULL_GAME_TMORPION()` from *inside*
//    a sound-dispatch switch) are never actually invoked anywhere -
//    confirmed dead by grep before dropping.
//  - `switch`/`case` avoided proactively throughout (matching Tiny Doc/
//    Bike/Pipe's established caution) - including several genuine GCC
//    case-range extensions (`case 0 ... 3:`) - all rewritten as `if`/
//    `else if` chains. Every intra-function `goto` used as a structured-
//    control-flow shortcut (`goto GoOut`/`GoNext`/`PUTCORNER`/etc. in the
//    CPU AI helpers) was rewritten with plain `if`/`else`/early `return`
//    instead of tested verbatim, same reasoning as Tiny Pipe's own port.
//  - `rand()%3`/`rand()%4` (in the CPU's random-move fallback and its
//    "replicate a symmetric position" heuristic) switched to the shared
//    `arand(n)` helper - the same rand()-range-mismatch fix already
//    applied to every other game's own random helper in this project.
//  - `CPU_DOUBLE_TMORPION()` is declared but never called anywhere in the
//    file - confirmed dead by grep, dropped rather than ported verbatim.
//    `Trace_LINE`/`DIRECTION_LINE`/`Return_Full_Byte`/`RECONSTRUCT_BYTE`/
//    `Universal_Swap` (`ELECTROLIB.h`'s line-drawing primitive) and
//    `Mymap` (only ever used *by* that same dead primitive) are likewise
//    never called from the game logic - the same "provably dead, don't
//    port verbatim" finding as Tiny Pipe's identical `ELECTROLIB.h`.
//  - Every render function's partial page-row range (`Tiny_Flip_TMORPION`
//    took an explicit `START_`/`END_` row range, with the per-tick idle
//    redraw only covering rows 2-7 versus a full 0-7 redraw right after
//    a move is placed) was simplified to always redraw the full 8 pages,
//    matching this project's own established "don't rely on real SSD1306
//    VRAM persisting a skipped row" precedent - proactively here rather
//    than needing to first prove it was actually safe upstream (it likely
//    was, since the skipped rows only ever change right after a full
//    redraw already ran, but there was no reason to be the one port that
//    re-litigates that question instead of just always drawing everything).
//  - A subtle **cursor-rendering trick** worth documenting since it's easy
//    to misread: the in-game cursor has no dedicated sprite of its own -
//    frame index 2 of `UP_TMORPION`/`MIDDLE_TMORPION`/`DOWN_TMORPION`
//    (the same index used for "genuinely empty, no mark" elsewhere) is
//    reused as the cursor-position indicator graphic. For any *non*-
//    cursor cell, an empty cell (`value==2`) is skipped entirely (no
//    sprite drawn, matching every other game's own "return 0 means
//    background shows through" convention) - but for the cursor's own
//    cell specifically, that early skip is *not* applied: the sprite
//    lookup always runs, using either the real cell content (blink-off
//    phase) or a forced value of 2 (blink-on phase) - so hovering an
//    empty cell shows the cursor graphic steadily (both phases resolve
//    to frame 2), while hovering an already-marked cell makes it visibly
//    blink between the real mark and the cursor graphic. Ported exactly
//    as this two-branch structure (cursor cell: always draw; non-cursor
//    cell: skip only if genuinely empty), not simplified, since the two
//    branches are not equivalent.
//  - A minor, deliberate simplification, noted rather than silently
//    dropped: upstream's own `Check_WIN_TMORPION` loop does not `break`
//    after a non-terminal win (`COMPLETED_PROCEDURE_TMORPION()` returning
//    0, meaning the win counter incremented but the match continues) -
//    it keeps scanning the remaining winning lines, and a single move
//    that completes *two* lines at once (rare, but possible in real tic-
//    tac-toe) would upstream call the whole resolve procedure a second
//    time, double-incrementing the win counter for one move. Since this
//    port's resolve procedure is now a genuine multi-frame animated
//    state (not an instant function call upstream could re-enter same-
//    frame), replicating that exact double-trigger isn't practical -
//    this port resolves at most one winning line per move instead.
// =============================================================================

int[16] tmorpionCpuRndAlt =
{
2,6,8,0,
0,8,6,2,
6,2,0,8,
8,0,2,6,
};

int[8] tmorpionLineCheck =
{
24,66,
129,36,
224,7,
41,148,
};

int[36] tmorpionCpuCheckMiddle =
{
129,90,
36,90,
80,128,
72,32,
10,1,
18,4,
65,32,
68,128,
136,32,
12,1,
34,1,
130,4,
48,128,
17,4,
128,1,
32,4,
4,32,
1,128,
};

int[42] tmorpionPolice =
{
4,1,248,136,248,0,0,248,0,0,232,168,184,0,136,168,248,0,56,32,
248,0,184,168,232,0,248,168,232,0,8,232,24,0,248,168,248,0,184,168,
248,0,
};

int[256] tmorpionPlateauUp =
{
254,1,252,254,254,254,6,214,198,254,6,126,126,246,238,30,230,254,254,254,
118,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,
254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,
252,1,254,0,0,254,1,252,254,254,254,6,118,118,254,6,214,198,254,6,
126,6,254,254,254,118,254,254,254,254,254,254,254,254,254,254,254,254,254,254,
254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,254,
254,254,254,254,254,252,1,254,3,4,9,27,43,27,43,27,43,27,43,27,
43,27,43,27,43,27,43,27,43,27,43,27,43,27,43,27,43,27,43,27,
43,27,43,59,43,59,43,59,43,59,43,59,43,59,43,59,43,59,43,59,
43,59,43,59,43,59,43,59,25,12,7,0,0,3,4,9,27,43,27,43,
27,43,27,43,27,43,27,43,27,43,27,43,27,43,27,43,27,43,27,43,
27,43,27,43,27,43,27,43,27,43,59,43,59,43,59,43,59,43,59,43,
59,43,59,43,59,43,59,43,59,43,59,43,59,25,12,7,
};

int[768] tmorpionPlateau =
{
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,192,192,224,96,
48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,176,112,48,48,48,
48,48,48,48,48,48,48,48,48,48,48,48,112,176,48,48,48,48,48,48,
48,48,48,48,48,48,48,48,48,96,224,192,192,128,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,192,224,240,120,
124,94,79,71,67,65,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
64,224,92,67,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
64,67,92,224,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,65,
67,71,79,94,124,120,240,224,192,128,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,192,224,240,120,
60,30,15,7,3,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,192,56,7,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,7,56,192,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,3,7,15,30,
60,120,240,224,192,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,192,224,240,120,
60,30,15,7,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
2,2,2,2,2,2,2,2,2,2,2,130,114,14,3,2,2,2,2,2,
2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
2,2,3,14,114,130,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
2,2,2,2,2,2,2,2,2,2,2,3,3,7,15,30,60,120,240,224,
192,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,64,224,240,248,
252,158,15,7,3,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,224,28,3,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,3,28,224,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,1,3,7,15,158,252,248,240,224,64,0,0,0,
0,0,0,63,66,252,133,249,235,171,235,235,235,235,235,235,235,235,235,235,
235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,
235,235,235,235,227,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,
235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,
235,235,235,235,227,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,
235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,235,171,
235,249,133,252,66,63,0,0,
};

int[3] tmorpionUpPos = {36,56,75};
int[3] tmorpionMiddlePos = {25,52,78};
int[3] tmorpionDownPos = {11,47,82};

int[324] tmorpionUp =
{
0,192,224,96,48,176,176,176,176,176,176,176,176,176,176,48,48,0,0,69,
78,79,95,95,93,89,88,88,88,88,93,79,79,71,71,0,0,48,48,48,
48,176,176,176,176,176,176,176,48,48,48,48,0,0,0,70,79,79,79,93,
89,88,88,88,89,93,79,79,79,70,0,0,0,48,48,176,176,176,176,176,
176,176,176,176,176,48,96,224,192,0,0,71,71,79,79,93,88,88,88,88,
89,93,95,95,79,78,69,0,192,192,224,96,48,176,176,176,48,48,48,48,
48,176,176,176,176,176,83,89,88,72,76,76,69,71,71,71,71,79,95,93,
89,89,64,96,112,48,176,176,176,48,48,48,48,48,48,48,176,176,176,48,
112,0,80,88,88,93,77,79,71,71,71,71,71,79,77,93,88,88,80,0,
176,176,176,176,176,48,48,48,48,48,176,176,176,48,96,224,192,192,96,64,
89,89,93,95,79,71,71,71,71,69,76,76,72,88,89,83,192,192,224,96,
48,176,48,176,48,176,48,176,48,176,48,176,48,176,83,73,84,74,85,74,
85,74,85,74,85,74,85,74,85,74,69,96,112,48,176,48,176,48,176,48,
176,48,176,48,176,48,176,48,112,0,72,85,74,85,74,85,74,85,74,85,
74,85,74,85,74,85,72,0,176,48,176,48,176,48,176,48,176,48,176,48,
176,48,96,224,192,192,96,69,74,85,74,85,74,85,74,85,74,85,74,85,
74,84,73,83,
};

int[234] tmorpionMiddle =
{
0,0,0,19,61,124,124,126,254,230,231,227,227,227,227,227,227,99,119,127,
126,62,62,28,8,0,0,56,60,124,126,126,254,230,231,227,195,195,195,195,
195,227,231,230,254,126,126,124,60,56,0,0,0,8,28,62,62,126,127,119,
99,227,227,227,227,227,227,231,230,254,126,124,124,61,19,0,0,0,158,207,
199,227,97,96,113,51,51,55,63,30,30,30,60,60,126,126,246,246,227,227,
227,195,1,193,135,192,192,225,227,227,115,119,54,62,62,28,28,28,62,62,
54,119,115,227,227,225,192,192,135,0,193,1,195,227,227,227,246,246,126,126,
60,60,30,30,30,63,55,51,51,113,96,97,227,199,207,158,158,79,167,83,
169,84,170,85,170,85,170,85,170,85,170,85,170,85,170,85,170,85,170,85,
10,193,135,80,170,85,170,85,170,85,170,85,170,85,170,85,170,85,170,85,
170,85,170,85,170,80,135,0,193,10,85,170,85,170,85,170,85,170,85,170,
85,170,85,170,85,170,85,170,84,169,83,167,79,158,
};

int[648] tmorpionDown =
{
0,0,0,0,120,60,158,206,198,226,226,226,242,114,114,114,58,58,58,58,
58,58,58,58,58,58,122,250,242,242,242,226,226,194,130,0,0,0,0,0,
6,15,31,31,31,63,63,63,127,124,120,120,120,120,120,120,120,56,56,60,
60,60,62,31,31,15,15,7,7,3,97,0,0,0,142,194,226,226,242,242,
242,250,122,122,58,58,58,58,58,58,58,58,58,58,58,122,122,250,242,242,
242,226,226,194,142,0,0,0,0,0,7,7,15,31,31,63,63,63,126,124,
120,120,120,120,120,120,120,120,120,120,120,124,126,63,63,63,31,31,15,7,
7,0,0,0,0,130,194,226,226,242,242,242,250,122,58,58,58,58,58,58,
58,58,58,58,114,114,114,242,226,226,226,198,206,158,60,120,0,0,0,0,
0,97,3,7,7,15,15,31,31,62,60,60,60,56,56,120,120,120,120,120,
120,120,124,127,63,63,63,31,31,31,15,6,0,0,0,0,128,192,224,240,
120,60,30,14,6,26,58,58,122,122,250,242,242,226,226,226,194,194,194,226,
226,226,226,114,114,114,58,58,58,26,26,26,71,99,113,112,120,120,56,60,
60,28,28,30,14,14,15,7,7,7,3,7,7,15,15,31,31,62,62,126,
124,124,120,120,112,0,224,28,0,0,14,2,2,26,26,58,58,58,122,114,
242,242,226,226,226,194,226,226,226,242,242,114,122,58,58,58,26,26,2,2,
14,0,0,0,0,0,112,112,120,120,124,60,60,62,30,30,15,15,7,7,
7,3,7,7,7,15,15,30,30,62,60,60,124,120,120,112,112,0,0,0,
26,26,26,58,58,58,114,114,114,226,226,226,226,194,194,194,226,226,226,242,
242,250,122,122,58,58,26,6,14,30,60,120,240,224,192,128,28,224,0,112,
120,120,124,124,126,62,62,31,31,15,15,7,7,3,7,7,7,15,14,14,
30,28,28,60,60,56,120,120,112,113,99,71,128,192,224,240,120,60,158,78,
166,82,170,82,170,82,170,82,170,82,170,82,170,82,170,82,170,82,170,82,
170,82,170,82,170,82,170,18,39,83,41,84,42,85,42,85,42,85,42,85,
42,85,42,85,42,85,42,85,42,85,42,85,42,85,42,85,42,85,42,85,
42,5,96,28,0,114,14,66,170,82,170,82,170,82,170,82,170,82,170,82,
170,82,170,82,170,82,170,82,170,82,170,82,170,82,170,66,14,114,0,0,
0,80,42,85,42,85,42,85,42,85,42,85,42,85,42,85,42,85,42,85,
42,85,42,85,42,85,42,85,42,85,42,85,42,80,0,0,18,170,82,170,
82,170,82,170,82,170,82,170,82,170,82,170,82,170,82,170,82,170,82,170,
82,170,82,166,78,158,60,120,240,224,192,128,28,96,5,42,85,42,85,42,
85,42,85,42,85,42,85,42,85,42,85,42,85,42,85,42,85,42,85,42,
85,42,85,42,84,41,83,39,
};

int[62] tmorpionMenuPic =
{
20,3,
0,159,21,145,0,159,133,159,0,151,149,157,0,147,148,15,0,0,0,0,
64,207,66,207,0,207,66,207,0,207,70,203,0,207,8,199,0,192,64,64,
4,7,5,7,0,7,3,5,0,7,1,7,0,3,4,3,0,7,5,4,
};

int[52] tmorpionCurseur =
{
25,2,
255,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,255,254,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
3,3,3,3,3,3,3,3,3,3,
};

int[207] tmorpionIntroPic =
{
41,5,254,3,1,1,1,1,17,33,65,129,65,33,17,1,241,1,1,1,
1,1,1,1,1,1,1,1,241,1,17,33,65,129,65,33,17,1,1,1,
3,254,252,255,0,0,0,0,0,20,18,17,16,17,18,20,16,255,16,16,
144,16,16,16,16,16,144,16,16,255,16,20,18,17,16,17,18,20,0,0,
0,0,255,255,255,0,0,0,0,0,0,0,0,0,0,0,0,0,255,0,
0,32,17,10,4,10,17,32,0,0,255,0,0,0,0,0,0,0,0,0,
0,0,0,255,255,255,0,0,0,64,64,225,81,73,73,73,81,225,65,255,
65,65,225,81,73,73,73,81,225,65,65,255,65,225,81,73,73,73,81,225,
64,64,0,0,255,255,15,24,48,48,48,48,48,49,50,50,50,49,48,48,
49,48,48,48,49,50,50,50,49,48,48,48,49,48,48,49,50,50,50,49,
48,48,48,48,56,47,31,
};

// -----------------------------------------------------------------------------
//   Sprite blitters (ELECTROLIB.h's own blitzSprite/SPEED_BLITZ - not part
//   of the shared shim, ported here as game-local functions, same
//   treatment as Tiny Pipe's identical ELECTROLIB.h).
// -----------------------------------------------------------------------------

// Same negative-yPos-safe fix as Tiny Pipe's own tpipeRecupeLineY, even
// though this game's own blitzSprite call sites never actually pass a
// negative yPos - kept for the same defensive reason (a shared helper
// that could be reused, and it costs nothing extra).
int tmorpionRecupeLineY( int val )
{
    if( val >= 0 ) return val >> 3;
    return -( ( -val + 7 ) >> 3 );
}

int tmorpionRecupeDecalageY( int val )
{
    return val - ( tmorpionRecupeLineY( val ) * 8 );
}

int tmorpionSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown ) return input << decalage;
    return input >> ( 8 - decalage );
}

int tmorpionBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = tmorpionRecupeLineY( yPos );

    if( xPass > ( xPos + wSprite - 1 ) || xPass < xPos ||
        recupeLineY > yPass || ( recupeLineY + hSprite ) < yPass )
      return 0x00;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = tmorpionRecupeDecalageY( yPos );
    int scanA = ( xPass - xPos ) + ( spriteYLine * wSprite ) + 2;
    int scanB = ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;

    int outByte;
    if( scanA > wMax ) outByte = 0x00;
    else outByte = tmorpionSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = tmorpionSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

int tmorpionSpeedBlitz( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    if( xPass > ( xPos + wSprite - 1 ) || xPass < xPos ||
        yPass < yPos || yPass > ( yPos + hSprite - 1 ) )
      return 0x00;
    return sprites[ 2 + ( xPass - xPos ) + ( yPass - yPos ) * wSprite + frame * ( hSprite * wSprite ) ];
}

// -----------------------------------------------------------------------------
//   State
// -----------------------------------------------------------------------------

int[9] tmorpionBoard;
int[2] tmorpionPlayers;
int tmorpionMyTurn; // 0 = CPU's turn, 1 = player's turn
int[2] tmorpionPosXY;
int tmorpionWinPly;
int tmorpionWinCpu;
int tmorpionEndgame;
int tmorpionSelect;
int tmorpionDivideBy2;
int tmorpionNoRipple;

void tmorpionInitMData()
{
    tmorpionMyTurn = 1;
    tmorpionEndgame = 0;
    tmorpionWinPly = 0;
    tmorpionWinCpu = 0;
    tmorpionPosXY[0] = 1;
    tmorpionPosXY[1] = 1;
    int i;
    for( i = 0; i < 9; i++ ) tmorpionBoard[i] = 2;
}

// Always called with cpu0Ply1==1 (player goes first) at this game's one
// real call site - the cpu0Ply1==0 branch is unreachable in practice, but
// kept (not simplified away) to match upstream's own general-purpose
// function shape.
void tmorpionAssignerXMain( int cpu0Ply1 )
{
    if( cpu0Ply1 == 0 ) { tmorpionPlayers[0] = 1; tmorpionPlayers[1] = 0; tmorpionMyTurn = 0; }
    else { tmorpionPlayers[0] = 0; tmorpionPlayers[1] = 1; tmorpionMyTurn = 1; }
}

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

int tmorpionNoteActive;
int tmorpionNoteMode; // 0 = simple ascending burst, 1 = 38-call alarm, 2 = downsampled big sweep
int tmorpionNoteT;
int tmorpionNoteEnd;
int tmorpionNoteStep;
int tmorpionNoteDur;
int tmorpionNoteSub;

// SND_BOX_TMORPION cases 0/1 - a short 10-note ascending burst (each note
// only a few ms) - small enough that a straightforward frame-stepped
// runner (below) covers it without needing any downsampling.
void tmorpionStartMoveSound( int isPlayer )
{
    tmorpionNoteActive = 1;
    tmorpionNoteMode = 0;
    if( isPlayer ) { tmorpionNoteT = 0; tmorpionNoteEnd = 100; tmorpionNoteDur = 4; }
    else { tmorpionNoteT = 100; tmorpionNoteEnd = 200; tmorpionNoteDur = 3; }
    tmorpionNoteStep = 10;
}

// SND_BOX_TMORPION(6) - two fixed notes at the very start of a session.
void tmorpionSoundStart()
{
    Sound( 100, 250 );
    Sound( 20, 250 );
}

// SND_BOX_TMORPION(4) - player wins the whole match: a genuine ~38-call
// alternating two-tone alarm, each note with a real ~25-40ms felt
// duration - no downsampling needed, just the standard frame-stepped
// treatment.
void tmorpionStartAlarm()
{
    tmorpionNoteActive = 1;
    tmorpionNoteMode = 1;
    tmorpionNoteT = 1;
    tmorpionNoteEnd = 20;
    tmorpionNoteSub = 0;
}

// SND_BOX_TMORPION(5) - CPU wins the whole match. See this file's own
// header comment: upstream fires 380 synchronous Sound() calls here
// (`for(t=200;t>10;t--){Sound(200-t,3);Sound(t,12);}`) - the largest
// dual-tone sweep found in this project. Downsampled to step -15.
void tmorpionStartBigSweep()
{
    tmorpionNoteActive = 1;
    tmorpionNoteMode = 2;
    tmorpionNoteT = 200;
    tmorpionNoteSub = 0;
}

// Returns true once the active sequence has finished.
int tmorpionAdvanceNote()
{
    if( !tmorpionNoteActive ) return 1;

    if( tmorpionNoteMode == 0 )
    {
        if( tmorpionNoteT >= tmorpionNoteEnd ) { tmorpionNoteActive = 0; return 1; }
        Sound( tmorpionNoteT, tmorpionNoteDur );
        tmorpionNoteT += tmorpionNoteStep;
        return 0;
    }

    if( tmorpionNoteMode == 1 )
    {
        if( tmorpionNoteT >= tmorpionNoteEnd ) { tmorpionNoteActive = 0; return 1; }
        if( tmorpionNoteSub == 0 ) { Sound( 4, 80 ); tmorpionNoteSub = 1; }
        else { Sound( 100, 80 ); tmorpionNoteSub = 0; tmorpionNoteT++; }
        return 0;
    }

    // mode 2 - downsampled big sweep
    if( tmorpionNoteT <= 10 ) { tmorpionNoteActive = 0; return 1; }
    if( tmorpionNoteSub == 0 ) { Sound( 200 - tmorpionNoteT, 3 ); tmorpionNoteSub = 1; }
    else { Sound( tmorpionNoteT, 12 ); tmorpionNoteSub = 0; tmorpionNoteT -= 15; }
    return 0;
}

// -----------------------------------------------------------------------------
//   Board helpers
// -----------------------------------------------------------------------------

int tmorpionCompactMap( int player )
{
    int t, byteComp = 0;
    for( t = 0; t < 4; t++ ) if( tmorpionBoard[t] == player ) byteComp = byteComp | ( 1 << t );
    for( t = 5; t < 9; t++ ) if( tmorpionBoard[t] == player ) byteComp = byteComp | ( 1 << ( t - 1 ) );
    return byteComp;
}

int tmorpionRecupePosGrid( int pos )
{
    if( pos >= 0 && pos <= 3 ) return pos;
    if( pos >= 4 && pos <= 7 ) return pos + 1;
    return 0;
}

// -----------------------------------------------------------------------------
//   CPU AI
// -----------------------------------------------------------------------------

int tmorpionCpuTermination( int plys )
{
    int byteComp = tmorpionCompactMap( plys );
    int x, t2, counter1, byteTmp, byteTmp2;

    for( x = 4; x < 8; x++ )
    {
        byteTmp2 = tmorpionLineCheck[x];
        byteTmp = byteComp & byteTmp2;
        counter1 = 0;
        for( t2 = 0; t2 < 8; t2++ ) if( byteTmp & ( 1 << t2 ) ) counter1++;
        if( counter1 == 2 )
        {
            for( t2 = 0; t2 < 8; t2++ )
            {
                if( ( byteTmp2 & ( 1 << t2 ) ) != ( byteTmp & ( 1 << t2 ) ) )
                {
                    int pos = tmorpionRecupePosGrid( t2 );
                    if( tmorpionBoard[ pos ] == 2 ) { tmorpionBoard[ pos ] = tmorpionPlayers[0]; return 1; }
                }
            }
        }
    }

    for( x = 0; x < 4; x++ )
    {
        byteTmp2 = tmorpionLineCheck[x];
        byteTmp = byteComp & byteTmp2;
        counter1 = 0;
        for( t2 = 0; t2 < 8; t2++ ) if( byteTmp & ( 1 << t2 ) ) counter1++;

        int doCorner = 0;
        if( counter1 == 1 )
        {
            if( tmorpionBoard[4] == plys ) doCorner = 1;
        }
        else if( counter1 == 2 )
        {
            if( tmorpionBoard[4] == 2 ) { tmorpionBoard[4] = tmorpionPlayers[0]; return 1; }
        }

        if( doCorner )
        {
            for( t2 = 0; t2 < 8; t2++ )
            {
                if( ( byteTmp2 & ( 1 << t2 ) ) != ( byteTmp & ( 1 << t2 ) ) )
                {
                    int pos = tmorpionRecupePosGrid( t2 );
                    if( tmorpionBoard[ pos ] == 2 ) { tmorpionBoard[ pos ] = tmorpionPlayers[0]; return 1; }
                }
            }
        }
    }
    return 0;
}

int tmorpionCpuMiddle()
{
    if( tmorpionBoard[4] != tmorpionPlayers[0] ) return 0;
    int byteComp = tmorpionCompactMap( tmorpionPlayers[1] );
    int t, t2, byteTmp, byteTmp2;
    for( t = 0; t < 2; t++ )
    {
        byteTmp = tmorpionCpuCheckMiddle[ t * 2 ];
        if( ( byteTmp & byteComp ) == byteTmp )
        {
            byteTmp2 = tmorpionCpuCheckMiddle[ ( t * 2 ) + 1 ];
            for( t2 = 0; t2 < 8; t2++ )
            {
                if( byteTmp2 & ( 1 << t2 ) )
                {
                    int pos = tmorpionRecupePosGrid( t2 );
                    if( tmorpionBoard[ pos ] == 2 ) { tmorpionBoard[ pos ] = tmorpionPlayers[0]; return 1; }
                }
            }
        }
    }
    return 0;
}

int tmorpionCpuCorner()
{
    int byteComp = tmorpionCompactMap( tmorpionPlayers[1] );
    int t, t2, byteTmp, byteTmp2;
    for( t = 2; t < 18; t++ )
    {
        byteTmp = tmorpionCpuCheckMiddle[ t * 2 ];
        if( ( byteTmp & byteComp ) == byteTmp )
        {
            byteTmp2 = tmorpionCpuCheckMiddle[ ( t * 2 ) + 1 ];
            for( t2 = 0; t2 < 8; t2++ )
            {
                if( byteTmp2 & ( 1 << t2 ) )
                {
                    int pos = tmorpionRecupePosGrid( t2 );
                    if( tmorpionBoard[ pos ] == 2 ) { tmorpionBoard[ pos ] = tmorpionPlayers[0]; return 1; }
                }
            }
        }
    }
    return 0;
}

int tmorpionCpuReplicate()
{
    int t = arand( 4 );
    int i0 = tmorpionCpuRndAlt[ ( t * 4 ) + 0 ];
    int i1 = tmorpionCpuRndAlt[ ( t * 4 ) + 1 ];
    int i2 = tmorpionCpuRndAlt[ ( t * 4 ) + 2 ];
    int i3 = tmorpionCpuRndAlt[ ( t * 4 ) + 3 ];
    if( tmorpionBoard[ i0 ] == 2 ) { tmorpionBoard[ i0 ] = tmorpionPlayers[0]; return 1; }
    if( tmorpionBoard[ i1 ] == 2 ) { tmorpionBoard[ i1 ] = tmorpionPlayers[0]; return 1; }
    if( tmorpionBoard[ i2 ] == 2 ) { tmorpionBoard[ i2 ] = tmorpionPlayers[0]; return 1; }
    if( tmorpionBoard[ i3 ] == 2 ) { tmorpionBoard[ i3 ] = tmorpionPlayers[0]; return 1; }
    return 0;
}

void tmorpionCpuRnd()
{
    int x, y;
    while( 1 )
    {
        x = arand( 3 );
        y = arand( 3 );
        if( tmorpionBoard[ y * 3 + x ] == 2 ) { tmorpionBoard[ y * 3 + x ] = tmorpionPlayers[0]; break; }
    }
}

void tmorpionCpuPlay()
{
    if( tmorpionCpuTermination( tmorpionPlayers[0] ) ) return;
    if( tmorpionSelect > 0 && tmorpionCpuTermination( tmorpionPlayers[1] ) ) return;
    if( tmorpionSelect > 0 && tmorpionBoard[4] == 2 ) { tmorpionBoard[4] = tmorpionPlayers[0]; return; }
    if( tmorpionSelect > 0 && tmorpionCpuMiddle() ) return;
    if( tmorpionSelect > 1 && tmorpionCpuCorner() ) return;
    if( tmorpionSelect > 1 && tmorpionCpuCorner() ) return;
    if( tmorpionSelect > 1 && tmorpionCpuReplicate() ) return;
    tmorpionCpuRnd();
}

// -----------------------------------------------------------------------------
//   Display
// -----------------------------------------------------------------------------

int tmorpionRecupeUpXBoard( int xPass )
{
    if( xPass >= 36 && xPass <= 53 ) return 0;
    if( xPass >= 56 && xPass <= 73 ) return 1;
    if( xPass >= 75 && xPass <= 92 ) return 2;
    return 4;
}

int tmorpionRecupeMiddleXBoard( int xPass )
{
    if( xPass >= 25 && xPass <= 50 ) return 0;
    if( xPass >= 52 && xPass <= 77 ) return 1;
    if( xPass >= 78 && xPass <= 103 ) return 2;
    return 4;
}

int tmorpionRecupeDownXBoard( int xPass )
{
    if( xPass >= 11 && xPass <= 46 ) return 0;
    if( xPass >= 47 && xPass <= 81 ) return 1;
    if( xPass >= 82 && xPass <= 117 ) return 2;
    return 4;
}

// See this file's own header comment on the cursor-rendering trick -
// frame 2 doubles as the cursor-position graphic, and the cursor's own
// cell skips the "genuinely empty -> draw nothing" shortcut that every
// other cell uses.
int tmorpionRecupeUp( int xPass, int yPass )
{
    int caseX = tmorpionRecupeUpXBoard( xPass );
    if( caseX == 4 ) return 0;
    int sprite = tmorpionBoard[ caseX ];
    if( caseX == tmorpionPosXY[0] && tmorpionPosXY[1] == 0 )
    {
        if( tmorpionDivideBy2 == 1 ) sprite = 2;
    }
    else
    {
        if( sprite == 2 ) return 0;
    }
    int tmpX = tmorpionUpPos[ caseX ];
    return tmorpionUp[ ( sprite * 108 ) + ( caseX * 36 ) + ( xPass - tmpX ) + ( ( yPass - 2 ) * 18 ) ];
}

int tmorpionRecupeMiddle( int xPass, int yPass )
{
    int caseX = tmorpionRecupeMiddleXBoard( xPass );
    if( caseX == 4 ) return 0;
    int sprite = tmorpionBoard[ 3 + caseX ];
    if( caseX == tmorpionPosXY[0] && tmorpionPosXY[1] == 1 )
    {
        if( tmorpionDivideBy2 == 1 ) sprite = 2;
    }
    else
    {
        if( sprite == 2 ) return 0;
    }
    int tmpX = tmorpionMiddlePos[ caseX ];
    return tmorpionMiddle[ ( sprite * 78 ) + ( caseX * 26 ) + ( xPass - tmpX ) + ( ( yPass - 4 ) * 26 ) ];
}

int tmorpionRecupeDown( int xPass, int yPass )
{
    int caseX = tmorpionRecupeDownXBoard( xPass );
    if( caseX == 4 ) return 0;
    int sprite = tmorpionBoard[ 6 + caseX ];
    if( caseX == tmorpionPosXY[0] && tmorpionPosXY[1] == 2 )
    {
        if( tmorpionDivideBy2 == 1 ) sprite = 2;
    }
    else
    {
        if( sprite == 2 ) return 0;
    }
    int tmpX = tmorpionDownPos[ caseX ];
    return tmorpionDown[ ( sprite * 216 ) + ( caseX * 72 ) + ( xPass - tmpX ) + ( ( yPass - 5 ) * 36 ) ];
}

int tmorpionRecupePointer( int xPass, int yPass )
{
    if( yPass == 2 || yPass == 3 ) return tmorpionRecupeUp( xPass, yPass );
    if( yPass == 4 ) return tmorpionRecupeMiddle( xPass, yPass );
    if( yPass == 5 || yPass == 6 ) return tmorpionRecupeDown( xPass, yPass );
    return 0;
}

// Both score digits use tmorpionPolice, whose own height (tmorpionPolice[1])
// is 1 page - so neither digit can ever appear outside page 0 or outside
// its own 4-column-wide footprint (x 25-28 for the player score, x 90-93
// for the CPU score). Gating both checks at the call site (rather than
// relying solely on tmorpionSpeedBlitz's own internal bounds check) avoids
// paying for a call that's provably going to return 0.
int tmorpionDisplay( int xPass, int yPass )
{
    int result = 0;
    if( xPass >= 25 && xPass <= 28 )
      result = result | tmorpionSpeedBlitz( 25, 0, xPass, yPass, tmorpionWinPly, tmorpionPolice );
    if( xPass >= 90 && xPass <= 93 )
      result = result | tmorpionSpeedBlitz( 90, 0, xPass, yPass, tmorpionWinCpu, tmorpionPolice );
    return result;
}

int tmorpionRecupeBack( int xPass, int yPass )
{
    // Page 1 can never show a score digit (see tmorpionDisplay's own
    // comment above) - the subtraction there is always a no-op, so skip
    // calling it entirely instead of computing a guaranteed-zero result
    // for all 128 columns of this row, every single frame.
    if( yPass == 0 )
      return tmorpionPlateauUp[ xPass ] & ( 0xff - tmorpionDisplay( xPass, yPass ) );
    if( yPass == 1 )
      return tmorpionPlateauUp[ xPass + 128 ];
    return tmorpionPlateau[ xPass + ( ( yPass - 2 ) * 128 ) ];
}

int tmorpionRecupe( int xPass, int yPass )
{
    return tmorpionRecupeBack( xPass, yPass ) | tmorpionRecupePointer( xPass, yPass );
}

// pointerMode 1 matches upstream's REFRESH_SCREEN_TMORPION (steady cursor,
// no blink toggle this call) - anything else toggles the cursor blink,
// matching the per-tick idle redraw. Always redraws the full 8 pages -
// see this file's own header comment on why the original partial-row
// range was simplified away rather than ported verbatim.
void tmorpionTinyFlip( int pointerMode )
{
    if( pointerMode == 1 ) tmorpionDivideBy2 = 0;
    else { if( tmorpionDivideBy2 == 0 ) tmorpionDivideBy2 = 1; else tmorpionDivideBy2 = 0; }
    if( tmorpionMyTurn == 0 ) tmorpionDivideBy2 = 0;

    md_beginFrame();
    int y, x;
    for( y = 0; y < 8; y++ )
      for( x = 0; x < 128; x++ )
        md_drawColumn( x, y, tmorpionRecupe( x, y ) );
}

int tmorpionRecupeMenu( int xPass, int yPass )
{
    // Same page-1-can-never-show-a-score-digit reasoning as
    // tmorpionRecupeBack() above.
    if( yPass == 0 )
      return tmorpionPlateauUp[ xPass ] & ( 0xff - tmorpionDisplay( xPass, yPass ) );
    if( yPass == 1 )
      return tmorpionPlateauUp[ xPass + 128 ];
    if( yPass >= 3 && yPass <= 5 && xPass >= 54 && xPass <= 73 )
      return tmorpionSpeedBlitz( 54, 3, xPass, yPass, 0, tmorpionMenuPic );
    return 0;
}

// The cursor and both intro-picture blits each have an easily-precomputed
// narrow footprint (a handful of pages x a fixed column range) - gating
// them at the call site instead of calling all 3 unconditionally at every
// one of 1024 pixels/frame avoids paying full call overhead for a result
// that's overwhelmingly guaranteed to be 0. This is the same "a self-gated
// function still costs a full call every time it's invoked" lesson this
// project's other ports already learned (see Arkanoid/Bert/Tris/Trick) -
// missed here since the menu screen predates re-checking against it. This
// is also exactly the path the user reported spiking CPU on: switching
// difficulty on the title screen calls tmorpionMenuFlip() directly.
void tmorpionMenuFlip( int pointer )
{
    md_beginFrame();
    int curPageMin = tmorpionRecupeLineY( pointer );
    int curPageMax = curPageMin + 2; // tmorpionCurseur[1] (height) == 2
    int y, x;
    for( y = 0; y < 8; y++ )
    {
        int curRowOk = ( y >= curPageMin && y <= curPageMax );
        int picRowOk = ( y >= 2 && y <= 7 ); // tmorpionIntroPic yPos=19 -> page 2, height 5 -> up to page 7
        for( x = 0; x < 128; x++ )
        {
            int val = tmorpionRecupeMenu( x, y );
            if( curRowOk && x >= 52 && x <= 76 )
              val = val | tmorpionBlitzSprite( 52, pointer, x, y, 0, tmorpionCurseur );
            if( picRowOk && x >= 8 && x <= 48 )
              val = val | tmorpionBlitzSprite( 8, 19, x, y, 0, tmorpionIntroPic );
            if( picRowOk && x >= 80 && x <= 120 )
              val = val | tmorpionBlitzSprite( 80, 19, x, y, 0, tmorpionIntroPic );
            md_drawColumn( x, y, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define TMORPION_STATE_MENU              0
#define TMORPION_STATE_MENU_WAIT_RELEASE 1
#define TMORPION_STATE_STARTING_WAIT     2
#define TMORPION_STATE_PLAYING           3
#define TMORPION_STATE_BLINK_WINNER      4
#define TMORPION_STATE_ENDGAME_SWEEP     5
#define TMORPION_STATE_NULL_GAME_BUZZ    6

int tmorpionState;
int tmorpionWaitFrames;
int tmorpionMenuMoved;
int[9] tmorpionBlinkBackup;
int tmorpionBlinkStep;
int tmorpionNullStep;
int tmorpionForceRedraw;

void tmorpionBeginMenu()
{
    tmorpionMenuMoved = 0;
    tmorpionMenuFlip( 22 + ( tmorpionSelect * 7 ) );
    tmorpionState = TMORPION_STATE_MENU;
}

void tmorpionBeginPlaying()
{
    tmorpionNoRipple = 0;
    tmorpionInitMData();
    tmorpionAssignerXMain( 1 );
    tmorpionTinyFlip( 0 );
    tmorpionSoundStart();
    tmorpionWaitFrames = 30;
    tmorpionState = TMORPION_STATE_STARTING_WAIT;
}

void tmorpionBeginBlinkWinner()
{
    int t;
    for( t = 0; t < 9; t++ ) tmorpionBlinkBackup[t] = tmorpionBoard[t];
    tmorpionBlinkStep = 0;
    tmorpionState = TMORPION_STATE_BLINK_WINNER;
}

void tmorpionBeginNullGame()
{
    tmorpionNullStep = 0;
    tmorpionState = TMORPION_STATE_NULL_GAME_BUZZ;
}

void gameTinyMorpion_init()
{
    InitTinyJoypad();
    tmorpionSelect = 1;
    tmorpionBeginMenu();
}

// Quit-confirmation-dialog resume hook (see menuGameList.c's own comment
// on this pattern) - checked proactively against the onResume audit
// before shipping, matching this project's own established practice
// (Tiny Bike/Arena/Gilbert/Pipe all needed this). TMORPION_STATE_MENU_
// WAIT_RELEASE has no timer of its own.
void gameTinyMorpion_forceRedraw()
{
    tmorpionForceRedraw = 1;
}

// Detects a win or a draw after a move; if either is found, begins the
// matching sub-state and returns 1 (caller should stop this tick here -
// matches upstream's own `goto GO_OUT`). Returns 0 if play continues.
// See this file's own header comment on the deliberate "at most one
// winning line per move" simplification.
int tmorpionCheckWin( int oX )
{
    int byteComp = tmorpionCompactMap( oX );
    int x;
    for( x = 0; x < 8; x++ )
    {
        int byteTmp = byteComp & tmorpionLineCheck[x];
        if( byteTmp == tmorpionLineCheck[x] )
        {
            if( x >= 0 && x <= 3 )
            {
                if( tmorpionBoard[4] == oX ) { tmorpionBeginBlinkWinner(); return 1; }
            }
            else
            {
                tmorpionBeginBlinkWinner(); return 1;
            }
        }
    }
    int anyEmpty = 0;
    int i;
    for( i = 0; i < 9; i++ ) if( tmorpionBoard[i] == 2 ) anyEmpty = 1;
    if( !anyEmpty ) { tmorpionBeginNullGame(); return 1; }
    return 0;
}

void gameTinyMorpion_update()
{
    if( tmorpionState == TMORPION_STATE_MENU )
    {
        if( tmorpionForceRedraw )
        {
            tmorpionMenuFlip( 22 + ( tmorpionSelect * 7 ) );
            tmorpionForceRedraw = 0;
        }
        if( isDownPressed() || isUpPressed() )
        {
            if( !tmorpionMenuMoved )
            {
                tmorpionMenuMoved = 1;
                if( isDownPressed() ) { if( tmorpionSelect < 2 ) tmorpionSelect++; }
                else { if( tmorpionSelect > 0 ) tmorpionSelect--; }
                tmorpionMenuFlip( 22 + ( tmorpionSelect * 7 ) );
                Sound( 200, 3 );
            }
        }
        else tmorpionMenuMoved = 0;

        if( isFirePressed() ) tmorpionState = TMORPION_STATE_MENU_WAIT_RELEASE;
        return;
    }

    if( tmorpionState == TMORPION_STATE_MENU_WAIT_RELEASE )
    {
        if( tmorpionForceRedraw )
        {
            tmorpionMenuFlip( 22 + ( tmorpionSelect * 7 ) );
            tmorpionForceRedraw = 0;
        }
        if( !isFirePressed() ) tmorpionBeginPlaying();
        return;
    }

    if( tmorpionState == TMORPION_STATE_STARTING_WAIT )
    {
        if( tmorpionWaitFrames > 0 ) { tmorpionWaitFrames--; return; }
        tmorpionState = TMORPION_STATE_PLAYING;
        return;
    }

    if( tmorpionState == TMORPION_STATE_BLINK_WINNER )
    {
        int half = tmorpionBlinkStep % 2;
        if( half == 0 )
        {
            Sound( 140, 10 ); Sound( 220, 4 );
            int t;
            for( t = 0; t < 9; t++ ) if( tmorpionBoard[t] == tmorpionPlayers[ tmorpionMyTurn ] ) tmorpionBoard[t] = 2;
            tmorpionTinyFlip( 1 );
        }
        else
        {
            int t;
            for( t = 0; t < 9; t++ ) tmorpionBoard[t] = tmorpionBlinkBackup[t];
            Sound( 10, 4 );
            tmorpionTinyFlip( 1 );
        }
        tmorpionBlinkStep++;
        if( tmorpionBlinkStep >= 20 )
        {
            int i;
            for( i = 0; i < 9; i++ ) tmorpionBoard[i] = 2;
            if( tmorpionMyTurn == 0 )
            {
                if( tmorpionWinCpu < 9 ) { tmorpionWinCpu++; tmorpionMyTurn = 0; tmorpionTinyFlip( 1 ); tmorpionState = TMORPION_STATE_PLAYING; }
                else { tmorpionStartBigSweep(); tmorpionEndgame = 1; tmorpionMyTurn = 0; tmorpionTinyFlip( 1 ); tmorpionState = TMORPION_STATE_ENDGAME_SWEEP; }
            }
            else
            {
                if( tmorpionWinPly < 9 ) { tmorpionWinPly++; tmorpionMyTurn = 0; tmorpionTinyFlip( 1 ); tmorpionState = TMORPION_STATE_PLAYING; }
                else { tmorpionStartAlarm(); tmorpionEndgame = 2; tmorpionMyTurn = 0; tmorpionTinyFlip( 1 ); tmorpionState = TMORPION_STATE_ENDGAME_SWEEP; }
            }
        }
        return;
    }

    if( tmorpionState == TMORPION_STATE_ENDGAME_SWEEP )
    {
        if( tmorpionAdvanceNote() ) tmorpionBeginMenu();
        return;
    }

    if( tmorpionState == TMORPION_STATE_NULL_GAME_BUZZ )
    {
        Sound( 10, 4 );
        Sound( 100, 4 );
        tmorpionNullStep++;
        if( tmorpionNullStep >= 30 )
        {
            int t;
            for( t = 0; t < 9; t++ ) tmorpionBoard[t] = 2;
            tmorpionMyTurn = 0;
            tmorpionTinyFlip( 1 );
            tmorpionState = TMORPION_STATE_PLAYING;
        }
        return;
    }

    // TMORPION_STATE_PLAYING
    tmorpionAdvanceNote();

    if( tmorpionMyTurn )
    {
        int moved = 0;
        if( isRightPressed() ) { if( !tmorpionNoRipple ) { moved = 1; if( tmorpionPosXY[0] < 2 ) tmorpionPosXY[0]++; } }
        else if( isLeftPressed() ) { if( !tmorpionNoRipple ) { moved = 1; if( tmorpionPosXY[0] > 0 ) tmorpionPosXY[0]--; } }
        else if( isDownPressed() ) { if( !tmorpionNoRipple ) { moved = 1; if( tmorpionPosXY[1] < 2 ) tmorpionPosXY[1]++; } }
        else if( isUpPressed() ) { if( !tmorpionNoRipple ) { moved = 1; if( tmorpionPosXY[1] > 0 ) tmorpionPosXY[1]--; } }
        else tmorpionNoRipple = 0;
        if( moved ) tmorpionNoRipple = 1;

        if( !tmorpionNoRipple && isFirePressed() )
        {
            int idx = tmorpionPosXY[1] * 3 + tmorpionPosXY[0];
            if( tmorpionBoard[ idx ] == 2 )
            {
                tmorpionBoard[ idx ] = tmorpionPlayers[1];
                tmorpionTinyFlip( 1 );
                tmorpionStartMoveSound( 1 );
                if( tmorpionCheckWin( tmorpionPlayers[1] ) ) return;
                tmorpionMyTurn = 0;
                tmorpionNoRipple = 1;
            }
        }
    }
    else
    {
        tmorpionCpuPlay();
        tmorpionTinyFlip( 1 );
        tmorpionStartMoveSound( 0 );
        if( tmorpionCheckWin( tmorpionPlayers[0] ) ) return;
        tmorpionMyTurn = 1;
    }

    tmorpionTinyFlip( 0 );
}

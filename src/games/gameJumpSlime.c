// =============================================================================
// JUMP SLIME - ported from attiny85AIjump6.ino ("more games/sample/
// attiny85AIjump6/", the more complete of two local revisions - see below).
// Author: 近藤さんちの研究室 ("Kondo-san's Laboratory"), note.com handle
// "kondolab" - credited in the menu as "KONDOLAB" (romanized handle, same
// treatment as SnakeGame85's own "TEREZAZA" credit, since no individual
// real name is given anywhere in English). Article:
// https://note.com/kondolab/n/ndc93ac31e555 (2025-07-06). No license is
// stated anywhere on the article page for this game's own code (only the
// separately-referenced gametiny/ssd1306xled libraries are confirmed
// GPLv3) - listed as "None specified" for licensing purposes, matching
// this project's own precedent for Four in a Row/Dino Game/SnakeGame85,
// but that's a statement about the license, not the author - the author
// is known and credited above.
//
// A simple jump-action platformer starring a slime: run/jump across 5
// stages of floating blocks, dodge back-and-forth patrolling enemies, and
// reach the coin on each stage's goal block. Built with heavy Google
// Gemini AI assistance (the article documents this explicitly, including
// the author's own "rules for creating games with AI").
//
// Two local folders exist for this game (`attiny85AIjump5`/
// `attiny85AIjump6`) - NOT two different games. `jump6`'s own file is a
// strict superset of `jump5`'s (confirmed via a line-ending-normalized
// diff - `jump6` is saved CRLF, `jump5` LF, which made a naive diff
// initially look like every single line had changed): `jump6` just adds a
// 5th stage on top of otherwise-identical code, and its own file
// timestamp is 11 days later - a later revision, ported here instead of
// the earlier one.
//
// Not tinyJoypadShim/obonoCoreShim lineage by name - this game's own
// `util.h` is a bespoke-looking driver, but its actual button thresholds
// (`TINYJOYPAD_LEFT`/`RIGHT`/`UP`/`DOWN`, A0/A3 analog reads) and its
// sprite/line-drawing helpers (`blitzSprite`/`SPEED_BLITZ`/`RecupeLineY`/
// `RecupeDecalageY`/`SplitSpriteDecalageY`) are functionally identical to
// Daniel C's own `ELECTROLIB.h` already used throughout this project
// (confirmed by direct diff against this project's own already-staged
// copy) - almost certainly the AI was fed that driver as a reference and
// reproduced it with added Japanese comments, rather than inventing a
// genuinely new one. No new shim needed: `isLeftPressed()`/`isRightPressed()`/
// `isFirePressed()`/`Sound()` from the existing `tinyJoypadShim` cover the
// whole input/audio surface, and `jslmBlitzSprite()`/`jslmSpeedBlitzNum()`
// below are a direct, already-proven translation of the same algorithm
// this project's own `gameTinyBert.c`/`gameTinyMissile.c` already carry
// under their own prefixes.
//
// Upstream's own debounce mechanism (`lastBtnAState`/`DEBOUNCE`, a real
// blocking `_delay_ms(30)` on every button check) is dropped entirely in
// favor of a single shared `jslmPrevFire`/edge-detect flag, matching the
// standard "convert real hardware debounce into a plain 'just pressed'
// check" treatment already used throughout this project - a single global
// flag updated once per real frame (not reset per state) is enough to
// keep a state-transition's own confirming press from bleeding into the
// very next state's own edge check (e.g. title -> gameplay not
// instantly triggering a jump on the same physical press), with no need
// for a dedicated per-transition gate.
//
// Upstream has no explicit frame-rate throttle anywhere (no FPS_Control,
// no CONTROL_FRAMERATE call) - the "no timing model whatsoever upstream"
// category from this project's own frame-pacing survey (same category as
// Tiny Trick/Invaders/Pinball/Bert/Tris), so this port runs at the
// engine's native 60fps with no throttle, matching the established
// default treatment for that category rather than guessing at a
// slowdown with no evidence to support one.
//
// Two genuine 3-note Sound() bursts (game-over, triggered from either the
// fall-off-bottom or enemy-collision path; stage-clear) were found via a
// direct check of every Sound() call site before ever compiling, matching
// this project's own established audit for this exact bug class (Vircon32's
// audio channel has no queue - a synchronous burst of Sound() calls is
// only ever audible as the very last note) - both converted to small
// frame-stepped sequencers up front rather than shipping the collapsed
// version and needing a later fix. The single jump-sound Sound(150,50)
// needed no such treatment (only ever one call). The other two sample
// games from the same batch (TinY Fi, TinyRoG) were checked too, at the
// user's request - both call Sound() only once per event and never invoke
// their own ELECTROLIB.h's shared (and, in both games, entirely unused)
// PLAY_MUSIC() helper, so neither has this bug waiting when it's ported.
// =============================================================================

#define JSLM_INTERVAL 100
#define JSLM_MOVE_DISTANCE 1
#define JSLM_CH_SIZE 8
#define JSLM_SCREEN_HEIGHT 64
#define JSLM_SCREEN_WIDTH 128
#define JSLM_JUMP_STRENGTH -6

#define JSLM_TEXT_CHAR_WIDTH 6

#define JSLM_SCORE_X_START 105
#define JSLM_SCORE_X_END 125
#define JSLM_STAGE_X_START 17
#define JSLM_STAGE_X_END 26
#define JSLM_TITLE_TEXT_X ( ( JSLM_SCREEN_WIDTH - ( 10 * JSLM_TEXT_CHAR_WIDTH ) ) / 2 )
#define JSLM_GAMEOVER_TEXT_X 40
#define JSLM_ALL_CLEAR_TEXT_X ( ( JSLM_SCREEN_WIDTH - ( 10 * JSLM_TEXT_CHAR_WIDTH ) ) / 2 )
#define JSLM_STAGE_CLEAR_TEXT_X ( ( JSLM_SCREEN_WIDTH - ( 12 * JSLM_TEXT_CHAR_WIDTH ) ) / 2 )

#define JSLM_TOTAL_STAGES 5

// Upstream itself has no genuine real-time throttle at all (see this
// file's own header comment on the frame-pacing survey), so this is a
// deliberate slowdown rather than restoring an original rate - added on
// direct request. Whole-function tick-skip (gates logic AND redraw
// together, not just the redraw), matching the majority precedent in this
// project (NumberPlace/HollowSeeker/t2048/Doc/Pacman/Pipe) rather than the
// movement-only/redraw-stays-60fps split used for games that had an
// already-established 60fps-tuned wait timer needing to stay real-time
// accurate. Every existing frame-counted constant in this file
// (jslmBlinkTimer's own 36-frame cycle, the sound sequencers' own
// jslmGameOverWaitFrames/jslmClearWaitFrames) is deliberately left
// unrescaled - they simply now take twice as long in real time, matching
// this project's own standing "one divisor, no dual bookkeeping" practice.
#define JSLM_FPS 30
#define JSLM_TICK_DIVISOR ( 60 / JSLM_FPS )
int jslmTickSkipCounter;

#define JSLM_STATE_TITLE 0
#define JSLM_STATE_GAMEPLAY 1
#define JSLM_STATE_STAGE_CLEAR 2
#define JSLM_STATE_GAMEOVER 3

#define JSLM_TYPE_SCORE 0
#define JSLM_TYPE_STAGE 1

// -----------------------------------------------------------------------------
// Sprite data - every sprite in this game is a uniform 8px wide, 1 page
// (8px) tall, matching upstream's own {W,H,...data} blitzSprite format
// exactly (kept in full, header included, rather than simplified away -
// see this file's own header comment on reusing gameTinyBert.c's already-
// proven general-form translation instead of a game-specific shortcut).
// -----------------------------------------------------------------------------

int[10] jslmCoin0    = { 8, 1, 0x00, 0x3e, 0x63, 0x5d, 0x5d, 0x7f, 0x3e, 0x00 };
int[10] jslmCoin1    = { 8, 1, 0x00, 0x3e, 0x7f, 0x5d, 0x5d, 0x63, 0x3e, 0x00 };
int[10] jslmBlock0   = { 8, 1, 0x7e, 0x81, 0xbd, 0x85, 0x85, 0x85, 0xc1, 0x7e };
int[10] jslmBlock1   = { 8, 1, 0x7e, 0x81, 0xa5, 0x81, 0x81, 0xa5, 0x81, 0x7e };
int[10] jslmChLeft0  = { 8, 1, 0x70, 0xd8, 0xfc, 0xfc, 0xdc, 0xfc, 0xf8, 0x70 };
int[10] jslmChLeft1  = { 8, 1, 0x38, 0x6c, 0xfe, 0xfe, 0xee, 0xfe, 0x7c, 0x38 };
int[10] jslmChRight0 = { 8, 1, 0x70, 0xf8, 0xfc, 0xdc, 0xfc, 0xfc, 0xd8, 0x70 };
int[10] jslmChRight1 = { 8, 1, 0x38, 0x7c, 0xfe, 0xee, 0xfe, 0xfe, 0x6c, 0x38 };
int[10] jslmEnemySyuriken0 = { 8, 1, 0x40, 0x19, 0x34, 0x46, 0x62, 0x2c, 0x98, 0x02 };
int[10] jslmEnemySyuriken1 = { 8, 1, 0x06, 0x98, 0xac, 0x62, 0x46, 0x35, 0x19, 0x60 };
// upstream's own "mountain" enemy has two identical frames (byte-for-byte
// the same data twice in enemy[][2][10]) - stored once here, referenced
// for both frame lookups, rather than duplicated.
int[10] jslmEnemyMountain = { 8, 1, 0xf0, 0xdc, 0xe3, 0x8e, 0xc4, 0x98, 0xe0, 0x00 };

int* jslmPlayerSprite( int cmp, int frame )
{
    if( cmp == 0 )
    {
        if( frame == 0 ) return jslmChLeft0;
        return jslmChLeft1;
    }
    if( frame == 0 ) return jslmChRight0;
    return jslmChRight1;
}

int* jslmEnemySprite( int type, int frame )
{
    if( type == 0 )
    {
        if( frame == 0 ) return jslmEnemySyuriken0;
        return jslmEnemySyuriken1;
    }
    return jslmEnemyMountain;
}

int* jslmCoinSprite( int frame )
{
    if( frame == 0 ) return jslmCoin0;
    return jslmCoin1;
}

// blockType 1 (jslmBlock1) is defined upstream but never actually used by
// any of the 5 stages' own data (every stage entry's blockType is 0) -
// confirmed by inspection of every stageN_blocks table below - kept anyway
// since it's part of a genuine lookup mechanism, not dead code in the
// usual sense, matching this project's own standing practice of not
// stripping data a real dispatch function still indexes into.
int* jslmBlockSprite( int type )
{
    if( type == 0 ) return jslmBlock0;
    return jslmBlock1;
}

// -----------------------------------------------------------------------------
// Stage data - {xFactor, yPage, blockType} triples per block, {x,y} pixel
// pairs for goal positions, {initXFactor, initYPage, type, tgtX1, tgtY1,
// tgtX2, tgtY2} septuples per enemy. Byte-diff-verified against the
// upstream source by direct re-read, not hand-copied blind.
// -----------------------------------------------------------------------------

int[36] jslmStage1Blocks = { 8,4,0, 13,4,0, 0,5,0, 1,5,0, 2,5,0, 3,5,0, 5,5,0, 6,5,0, 10,5,0, 11,5,0, 12,5,0, 15,5,0, };
int[39] jslmStage2Blocks = { 6,2,0, 8,3,0, 9,3,0, 5,4,0, 11,4,0, 0,5,0, 1,5,0, 3,5,0, 10,5,0, 8,6,0, 6,7,0, 15,2,0, 13,3,0, };
int[36] jslmStage3Blocks = { 8,4,0, 13,4,0, 0,5,0, 1,5,0, 2,5,0, 3,5,0, 5,5,0, 6,5,0, 10,5,0, 11,5,0, 12,5,0, 15,5,0, };
int[39] jslmStage4Blocks = { 6,2,0, 8,3,0, 9,3,0, 5,4,0, 11,4,0, 0,5,0, 1,5,0, 3,5,0, 10,5,0, 8,6,0, 6,7,0, 15,2,0, 13,3,0, };
int[27] jslmStage5Blocks = { 7,3,0, 5,4,0, 0,5,0, 1,5,0, 3,5,0, 13,6,0, 15,5,0, 10,7,0, 11,7,0, };

int[5] jslmNumBlocks = { 12, 13, 12, 13, 9, };

int[14] jslmStage3Enemies = { 10,4,0,80,32,20,32,  4,6,0,30,48,90,48, };
int[7]  jslmStage4Enemies = { 7,5,0,50,40,100,40, };
int[14] jslmStage5Enemies = { 6,3,0,16,24,48,24,  11,6,0,48,48,88,48, };

int[5] jslmNumEnemies = { 0, 0, 2, 1, 2, };

int[10] jslmGoalPosi = { 120,40, 48,16, 120,40, 48,16, 120,40, };

int* jslmBlocksForStage( int stage )
{
    if( stage == 0 ) return jslmStage1Blocks;
    if( stage == 1 ) return jslmStage2Blocks;
    if( stage == 2 ) return jslmStage3Blocks;
    if( stage == 3 ) return jslmStage4Blocks;
    return jslmStage5Blocks;
}

// stages 0/1 have 0 enemies (jslmNumEnemies gates the loop to never read
// this), so the fallback return value here is never actually dereferenced
int* jslmEnemiesForStage( int stage )
{
    if( stage == 2 ) return jslmStage3Enemies;
    if( stage == 3 ) return jslmStage4Enemies;
    if( stage == 4 ) return jslmStage5Enemies;
    return jslmStage3Enemies;
}

// -----------------------------------------------------------------------------
// Font data - miniAscii (' ' through 'Z', 59 chars x 6 columns) and
// miniNum (10 digits x 4 columns), both with their constant {W,H} header
// stripped (every char/digit is uniformly 1 page tall), matching this
// project's own established font-table simplification (e.g. gameOroboros.c's
// own orbFont). miniStg (a fixed pre-rendered "STG" label, 16 columns x 1
// page) kept as one flat bitmap, matching upstream's own choice to bake it
// as a dedicated image rather than spell it through the generic font.
// -----------------------------------------------------------------------------

int[354] jslmFont =
{
0,0,0,0,0,0, 0,0,0,0x2f,0,0, 0,0,7,0,7,0, 0,0x14,0x7f,0x14,0x7f,0x14,
0,0x24,0x2a,0x7f,0x2a,0x12, 0,0x62,0x64,8,0x13,0x23, 0,0x36,0x49,0x55,0x22,0x50,
0,0,5,3,0,0, 0,0,0x1c,0x22,0x41,0, 0,0,0x41,0x22,0x1c,0,
0,0x14,8,0x3e,8,0x14, 0,8,8,0x3e,8,8, 0,0,0,0xa0,0x60,0,
0,8,8,8,8,8, 0,0,0x60,0x60,0,0, 0,0x20,0x10,8,4,2,
0,0x3e,0x51,0x49,0x45,0x3e, 0,0,0x42,0x7f,0x40,0, 0,0x42,0x61,0x51,0x49,0x46,
0,0x21,0x41,0x45,0x4b,0x31, 0,0x18,0x14,0x12,0x7f,0x10, 0,0x27,0x45,0x45,0x45,0x39,
0,0x3c,0x4a,0x49,0x49,0x30, 0,1,0x71,9,5,3, 0,0x36,0x49,0x49,0x49,0x36,
0,6,0x49,0x49,0x29,0x1e, 0,0,0x36,0x36,0,0, 0,0,0x56,0x36,0,0,
0,8,0x14,0x22,0x41,0, 0,0x14,0x14,0x14,0x14,0x14, 0,0,0x41,0x22,0x14,8,
0,2,1,0x51,9,6, 0,0x32,0x49,0x59,0x51,0x3e, 0,0x7c,0x12,0x11,0x12,0x7c,
0,0x7f,0x49,0x49,0x49,0x36, 0,0x3e,0x41,0x41,0x41,0x22, 0,0x7f,0x41,0x41,0x22,0x1c,
0,0x7f,0x49,0x49,0x49,0x41, 0,0x7f,9,9,9,1, 0,0x3e,0x41,0x49,0x49,0x7a,
0,0x7f,8,8,8,0x7f, 0,0,0x41,0x7f,0x41,0, 0,0x20,0x40,0x41,0x3f,1,
0,0x7f,8,0x14,0x22,0x41, 0,0x7f,0x40,0x40,0x40,0x40, 0,0x7f,2,0xc,2,0x7f,
0,0x7f,4,8,0x10,0x7f, 0,0x3e,0x41,0x41,0x41,0x3e, 0,0x7f,9,9,9,6,
0,0x3e,0x41,0x51,0x21,0x5e, 0,0x7f,9,0x19,0x29,0x46, 0,0x46,0x49,0x49,0x49,0x31,
0,1,1,0x7f,1,1, 0,0x3f,0x40,0x40,0x40,0x3f, 0,0x1f,0x20,0x40,0x20,0x1f,
0,0x3f,0x40,0x38,0x40,0x3f, 0,0x63,0x14,8,0x14,0x63, 0,7,8,0x70,8,7,
0,0x61,0x51,0x49,0x45,0x43,
};

int[40] jslmNumFont =
{
0xf8,0x88,0xf8,0, 0,0xf8,0,0, 0xe8,0xa8,0xb8,0, 0x88,0xa8,0xf8,0, 0x38,0x20,0xf8,0,
0xb8,0xa8,0xe8,0, 0xf8,0xa8,0xe8,0, 8,0xe8,0x18,0, 0xf8,0xa8,0xf8,0, 0xb8,0xa8,0xf8,0,
};

int[16] jslmStgLabel = { 0xb8, 0xe8, 0, 8, 0xf8, 8, 0xf0, 0x28, 0xf0, 0, 0x70, 0x88, 0xc8, 0, 0xf8, 0xa8, };

int jslmFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 90 ) ) return 0;
    return jslmFont[ ( ch - 32 ) * JSLM_TEXT_CHAR_WIDTH + col ];
}

int jslmTextByte( int* text, int textLen, int startX, int x )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / JSLM_TEXT_CHAR_WIDTH;
    if( charIdx >= textLen ) return 0;
    return jslmFontByte( text[ charIdx ], rel % JSLM_TEXT_CHAR_WIDTH );
}

int jslmNumByte( int startX, int x, int digit )
{
    if( ( x < startX ) || ( x > startX + 3 ) ) return 0;
    return jslmNumFont[ digit * 4 + ( x - startX ) ];
}

int jslmStgByte( int x )
{
    if( ( x < 0 ) || ( x > 15 ) ) return 0;
    return jslmStgLabel[ x ];
}

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

struct JslmNumDisp
{
    int hundreds, tens, units, xStart, xEnd;
};

struct JslmEnemyState
{
    int currentX, currentY, targetIdx;
};

int jslmState;
int jslmScore;
int jslmCmp; // player facing: 0 left, 1 right
int jslmGravity;
int jslmCurrentStage; // 0-based
int jslmIsOnGround;
int jslmMyX, jslmMyY;

int jslmPrevFire;
int jslmBlinkTimer;
int jslmBlinkCh;

JslmNumDisp jslmScoreDisp;
JslmNumDisp jslmStageDisp;

JslmEnemyState[2] jslmEnemyStates;

// game-over cue (3 notes - matches upstream's own back-to-back
// Sound(30,400);Sound(80,200);Sound(1,500); triplet, fired identically
// from both the fall-off-bottom and enemy-collision paths)
int[3] jslmGameOverFreq = { 30, 80, 1, };
int[3] jslmGameOverDur  = { 400, 200, 500, };
int[3] jslmGameOverWaitFrames = { 11, 5, 16, }; // ceil(dur/freqHz * 60), freqHz = 500000/(255-freq)
int jslmGameOverIdx;
int jslmGameOverTimer;

// stage-clear cue (3 notes - matches Sound(50,100);Sound(100,100);Sound(150,300);)
int[3] jslmClearFreq = { 50, 100, 150, };
int[3] jslmClearDur  = { 100, 100, 300, };
int[3] jslmClearWaitFrames = { 3, 2, 4, };
int jslmClearIdx;
int jslmClearTimer;

// -----------------------------------------------------------------------------
// Sound sequencers
// -----------------------------------------------------------------------------

void jslmStartGameOverSfx()
{
    jslmGameOverIdx = 0;
    jslmGameOverTimer = 0;
}

int jslmAdvanceGameOverSfx()
{
    if( jslmGameOverIdx >= 3 ) return 0;
    jslmGameOverTimer = jslmGameOverTimer - 1;
    if( jslmGameOverTimer <= 0 )
    {
        Sound( jslmGameOverFreq[ jslmGameOverIdx ], jslmGameOverDur[ jslmGameOverIdx ] );
        jslmGameOverTimer = jslmGameOverWaitFrames[ jslmGameOverIdx ];
        jslmGameOverIdx = jslmGameOverIdx + 1;
    }
    return 1;
}

void jslmStartClearSfx()
{
    jslmClearIdx = 0;
    jslmClearTimer = 0;
}

int jslmAdvanceClearSfx()
{
    if( jslmClearIdx >= 3 ) return 0;
    jslmClearTimer = jslmClearTimer - 1;
    if( jslmClearTimer <= 0 )
    {
        Sound( jslmClearFreq[ jslmClearIdx ], jslmClearDur[ jslmClearIdx ] );
        jslmClearTimer = jslmClearWaitFrames[ jslmClearIdx ];
        jslmClearIdx = jslmClearIdx + 1;
    }
    return 1;
}

// -----------------------------------------------------------------------------
// Sprite blitting - a direct, already-proven translation of upstream's own
// blitzSprite()/SplitSpriteDecalageY()/RecupeLineY()/RecupeDecalageY(),
// reusing the exact same algorithm this project's gameTinyBert.c already
// carries under its own "bert" prefix (that game's own header/body split
// is byte-for-byte the same shape as this game's util.h version).
// -----------------------------------------------------------------------------

int jslmRecupeLineY( int valeur )
{
    return valeur >> 3;
}

int jslmRecupeDecalageY( int valeur )
{
    return valeur - ( ( valeur >> 3 ) << 3 );
}

int jslmSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown )
      return ( input << decalage ) & 0xFF;
    return input >> ( 8 - decalage );
}

int jslmBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[ 0 ];
    int hSprite = sprites[ 1 ];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = jslmRecupeLineY( yPos );

    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos ||
        recupeLineY > yPass || ( recupeLineY + hSprite ) < yPass )
      return 0x00;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = jslmRecupeDecalageY( yPos );
    int scanA = ( xPass - xPos ) + ( spriteYLine * wSprite ) + 2;
    int scanB = ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;

    int outByte;
    if( scanA > wMax )
      outByte = 0x00;
    else
      outByte = jslmSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = jslmSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

// -----------------------------------------------------------------------------
// Game logic
// -----------------------------------------------------------------------------

int jslmHitTest( int x1, int y1, int lenX1, int lenY1, int x2, int y2, int len2 )
{
    return ( x1 < x2 + len2 ) && ( x1 + lenX1 > x2 ) && ( y1 < y2 + len2 ) && ( y1 + lenY1 > y2 );
}

void jslmMakeNumData( int value, int type, JslmNumDisp* data )
{
    if( type == JSLM_TYPE_SCORE )
    {
        data->hundreds = value / 100;
        data->tens = ( value - ( data->hundreds * 100 ) ) / 10;
        data->units = value - ( data->tens * 10 ) - ( data->hundreds * 100 );
    }
    else
    {
        data->tens = value / 10;
        data->units = value % 10;
        data->hundreds = 0;
    }
}

// matches resetPlayerForStage(): resets the player to the fixed spawn
// point and re-seeds every enemy for the current stage from its own
// PROGMEM-equivalent table - called on a fresh game and on every stage
// transition (but deliberately NOT after the final stage - see the goal-
// check below, matching upstream's own explicit "don't blank the screen
// after the last stage" comment).
void jslmResetPlayerForStage()
{
    jslmMyX = 8;
    jslmMyY = 16;
    jslmCmp = 1;
    jslmGravity = 0;
    jslmIsOnGround = 1;

    jslmMakeNumData( jslmCurrentStage + 1, JSLM_TYPE_STAGE, &jslmStageDisp );

    int* enemies = jslmEnemiesForStage( jslmCurrentStage );
    int numEnemies = jslmNumEnemies[ jslmCurrentStage ];

    int i;
    for( i = 0; i < numEnemies; i++ )
    {
        jslmEnemyStates[ i ].currentX = enemies[ i * 7 + 0 ] * JSLM_CH_SIZE;
        jslmEnemyStates[ i ].currentY = enemies[ i * 7 + 1 ] * JSLM_CH_SIZE;
        jslmEnemyStates[ i ].targetIdx = 0;
    }
}

// matches CollisionXY(): steps the player 1px at a time along X then Y
// (gravity), checking block/screen-edge collision at each step - a direct
// port, no blocking behavior to convert (this is already a plain bounded
// loop, not a real-time wait).
void jslmCollisionXY( int dx )
{
    int* currentBlocks = jslmBlocksForStage( jslmCurrentStage );
    int currentNumBlocks = jslmNumBlocks[ jslmCurrentStage ];

    int stepX;
    if( dx > 0 ) stepX = 1; else stepX = -1;
    int absDx;
    if( dx > 0 ) absDx = dx; else absDx = -dx;

    int i;
    for( i = 0; i < absDx; i++ )
    {
        int testNextX = jslmMyX + stepX;

        if( testNextX < 0 )
        {
            jslmMyX = 0;
            break;
        }
        if( testNextX + JSLM_CH_SIZE > JSLM_SCREEN_WIDTH )
        {
            jslmMyX = JSLM_SCREEN_WIDTH - JSLM_CH_SIZE;
            break;
        }

        int collidedWithBlockX = 0;
        int b;
        for( b = 0; b < currentNumBlocks; b++ )
        {
            int blockXFactor = currentBlocks[ b * 3 + 0 ];
            int blockYPage = currentBlocks[ b * 3 + 1 ];
            int blockPixelYTop = blockYPage * JSLM_CH_SIZE;
            int blockPixelXLeft = blockXFactor * JSLM_CH_SIZE;

            if( jslmHitTest( testNextX, jslmMyY + 2, JSLM_CH_SIZE, JSLM_CH_SIZE - 2, blockPixelXLeft, blockPixelYTop, JSLM_CH_SIZE ) )
            {
                if( stepX > 0 ) jslmMyX = blockPixelXLeft - JSLM_CH_SIZE;
                else jslmMyX = blockPixelXLeft + JSLM_CH_SIZE;
                collidedWithBlockX = 1;
                break;
            }
        }
        if( collidedWithBlockX ) break;

        jslmMyX = testNextX;
    }

    int dy = jslmGravity;
    int stepY;
    if( dy > 0 ) stepY = 1; else stepY = -1;
    int absDy;
    if( dy > 0 ) absDy = dy; else absDy = -dy;

    int hitSomethingVertically = 0;

    for( i = 0; i < absDy; i++ )
    {
        int testNextY = jslmMyY + stepY;

        if( testNextY < 0 )
        {
            jslmMyY = 0;
            jslmGravity = 0;
            hitSomethingVertically = 1;
            break;
        }

        if( testNextY + JSLM_CH_SIZE > JSLM_SCREEN_HEIGHT )
        {
            jslmState = JSLM_STATE_GAMEOVER;
            jslmStartGameOverSfx();
            jslmMyY = JSLM_SCREEN_HEIGHT;
            jslmGravity = 0;
            jslmIsOnGround = 0;
            hitSomethingVertically = 1;
            break;
        }

        int collidedWithBlockY = 0;
        if( jslmState != JSLM_STATE_GAMEOVER )
        {
            int b;
            for( b = 0; b < currentNumBlocks; b++ )
            {
                int blockXFactor = currentBlocks[ b * 3 + 0 ];
                int blockYPage = currentBlocks[ b * 3 + 1 ];
                int blockPixelYTop = blockYPage * JSLM_CH_SIZE;
                int blockPixelXLeft = blockXFactor * JSLM_CH_SIZE;

                if( jslmHitTest( jslmMyX, testNextY + 2, JSLM_CH_SIZE, JSLM_CH_SIZE - 2, blockPixelXLeft, blockPixelYTop, JSLM_CH_SIZE ) )
                {
                    if( stepY < 0 )
                    {
                        jslmMyY = blockPixelYTop + JSLM_CH_SIZE;
                        jslmGravity = 0;
                        collidedWithBlockY = 1;
                        hitSomethingVertically = 1;
                    }
                    else
                    {
                        jslmMyY = blockPixelYTop - JSLM_CH_SIZE;
                        jslmGravity = 0;
                        jslmIsOnGround = 1;
                        collidedWithBlockY = 1;
                        hitSomethingVertically = 1;
                    }
                    break;
                }
            }
        }
        if( collidedWithBlockY ) break;
        jslmMyY = testNextY;
    }

    if( !hitSomethingVertically && jslmGravity != 0 && jslmState != JSLM_STATE_GAMEOVER )
      jslmIsOnGround = 0;
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

// jslmComputeByte() runs once per pixel (1024 times/frame) - scanning all
// 12-13 of a stage's own blocks from inside that per-pixel call (checking
// each one's own page against the current row) cost up to 13*1024 wasted
// loop iterations/frame just to find the 1-2 blocks that actually matched,
// the same O(pixels x objects) shape already found and fixed in nearly
// every other Daniel-C-lineage port in this project. Confirmed via the
// WebGL perf overlay: CPU pegged at a saturated 100% during real gameplay,
// with a visibly truncated frame (only the top couple of rows drawn before
// cutting to black) - this project's own well-documented "frame truncated
// mid-instruction-stream, real CPU-budget exceeded" failure signature, not
// just a slow-but-safe frame. Fixed by compositing each stage's own blocks
// into a shared per-page buffer once per page (8x/frame) instead of once
// per pixel (1024x/frame) - blocks are always page-aligned in every one of
// this game's 5 stages (confirmed by inspection: every stageN_blocks
// entry's own Y factor lines up exactly with a whole page), so this reads
// each block sprite's own raw column bytes directly rather than going
// through jslmBlitzSprite()'s more general sub-page-split logic.
int[128] jslmBlockRowBuffer;

void jslmCompositeBlocksRow( int page )
{
    int i;
    for( i = 0; i < JSLM_SCREEN_WIDTH; i++ ) jslmBlockRowBuffer[ i ] = 0;

    int* currentBlocks = jslmBlocksForStage( jslmCurrentStage );
    int currentNumBlocks = jslmNumBlocks[ jslmCurrentStage ];
    int b;
    for( b = 0; b < currentNumBlocks; b++ )
    {
        int blockYPage = currentBlocks[ b * 3 + 1 ];
        if( blockYPage != page ) continue;

        int blockXFactor = currentBlocks[ b * 3 + 0 ];
        int blockType = currentBlocks[ b * 3 + 2 ];
        int blockPixelX = blockXFactor * JSLM_CH_SIZE;
        int* sprite = jslmBlockSprite( blockType );

        int col;
        for( col = 0; col < JSLM_CH_SIZE; col++ )
        {
            int screenX = blockPixelX + col;
            if( screenX >= 0 && screenX < JSLM_SCREEN_WIDTH )
              jslmBlockRowBuffer[ screenX ] = jslmBlockRowBuffer[ screenX ] | sprite[ 2 + col ];
        }
    }
}

// Same "self-gated call still costs a full call every time it's invoked"
// lesson as the blocks fix above, applied to the player/coin/enemy
// sprites: each is only ever an 8px-wide footprint, but was being handed
// to jslmBlitzSprite() for all 128 columns of every page regardless.
// Composited into a second per-page buffer, only ever calling
// jslmBlitzSprite() across each sprite's own real column range (8 columns)
// on the (up to 2) pages its own Y position could actually touch, instead
// of scanning all 128 columns x 8 pages unconditionally.
int[128] jslmSpriteRowBuffer;

void jslmCompositeSpritesRow( int page )
{
    int i;
    for( i = 0; i < JSLM_SCREEN_WIDTH; i++ ) jslmSpriteRowBuffer[ i ] = 0;

    int playerLine = jslmRecupeLineY( jslmMyY );
    if( page == playerLine || page == playerLine + 1 )
    {
        int* sprite = jslmPlayerSprite( jslmCmp, jslmBlinkCh );
        int col;
        for( col = jslmMyX; col <= jslmMyX + 7; col++ )
        {
            if( col >= 0 && col < JSLM_SCREEN_WIDTH )
              jslmSpriteRowBuffer[ col ] = jslmSpriteRowBuffer[ col ] | jslmBlitzSprite( jslmMyX, jslmMyY, col, page, 0, sprite );
        }
    }

    int goalBlockX = jslmGoalPosi[ jslmCurrentStage * 2 + 0 ];
    int goalBlockY = jslmGoalPosi[ jslmCurrentStage * 2 + 1 ] - JSLM_CH_SIZE;
    int coinLine = jslmRecupeLineY( goalBlockY );
    if( page == coinLine || page == coinLine + 1 )
    {
        int* sprite = jslmCoinSprite( jslmBlinkCh );
        int col;
        for( col = goalBlockX; col <= goalBlockX + 7; col++ )
        {
            if( col >= 0 && col < JSLM_SCREEN_WIDTH )
              jslmSpriteRowBuffer[ col ] = jslmSpriteRowBuffer[ col ] | jslmBlitzSprite( goalBlockX, goalBlockY, col, page, 0, sprite );
        }
    }

    int* enemies = jslmEnemiesForStage( jslmCurrentStage );
    int numEnemies = jslmNumEnemies[ jslmCurrentStage ];
    int e;
    for( e = 0; e < numEnemies; e++ )
    {
        int enemyLine = jslmRecupeLineY( jslmEnemyStates[ e ].currentY );
        if( page == enemyLine || page == enemyLine + 1 )
        {
            int enemyType = enemies[ e * 7 + 2 ];
            int* sprite = jslmEnemySprite( enemyType, jslmBlinkCh );
            int col;
            for( col = jslmEnemyStates[ e ].currentX; col <= jslmEnemyStates[ e ].currentX + 7; col++ )
            {
                if( col >= 0 && col < JSLM_SCREEN_WIDTH )
                  jslmSpriteRowBuffer[ col ] = jslmSpriteRowBuffer[ col ] | jslmBlitzSprite( jslmEnemyStates[ e ].currentX, jslmEnemyStates[ e ].currentY, col, page, 0, sprite );
            }
        }
    }
}

int jslmComputeByte( int x, int page )
{
    int tmpBmp = 0;

    if( jslmState == JSLM_STATE_GAMEPLAY )
    {
        tmpBmp = tmpBmp | jslmBlockRowBuffer[ x ];
        tmpBmp = tmpBmp | jslmSpriteRowBuffer[ x ];

        if( page == 0 )
        {
            tmpBmp = tmpBmp | jslmStgByte( x );
            tmpBmp = tmpBmp | jslmNumByte( JSLM_STAGE_X_START + 0, x, jslmStageDisp.tens );
            tmpBmp = tmpBmp | jslmNumByte( JSLM_STAGE_X_START + 4, x, jslmStageDisp.units );
        }
    }

    if( page == 3 )
    {
        if( jslmState == JSLM_STATE_TITLE )
          tmpBmp = tmpBmp | jslmTextByte( "JUMP SLIME", 10, JSLM_TITLE_TEXT_X, x );
        else if( jslmState == JSLM_STATE_GAMEOVER )
          tmpBmp = tmpBmp | jslmTextByte( "GAME OVER", 9, JSLM_GAMEOVER_TEXT_X, x );
        else if( jslmState == JSLM_STATE_STAGE_CLEAR )
        {
            if( jslmCurrentStage >= JSLM_TOTAL_STAGES )
              tmpBmp = tmpBmp | jslmTextByte( "ALL CLEAR!", 10, JSLM_ALL_CLEAR_TEXT_X, x );
            else
              tmpBmp = tmpBmp | jslmTextByte( "STAGE CLEAR!", 12, JSLM_STAGE_CLEAR_TEXT_X, x );
        }
    }

    if( page == 7 )
    {
        tmpBmp = tmpBmp | jslmNumByte( JSLM_SCORE_X_START + 0, x, jslmScoreDisp.hundreds );
        tmpBmp = tmpBmp | jslmNumByte( JSLM_SCORE_X_START + 4, x, jslmScoreDisp.tens );
        tmpBmp = tmpBmp | jslmNumByte( JSLM_SCORE_X_START + 8, x, jslmScoreDisp.units );
    }

    return tmpBmp;
}

void jslmRender()
{
    md_beginFrame();
    jslmMakeNumData( jslmScore, JSLM_TYPE_SCORE, &jslmScoreDisp );

    int x, page;
    for( page = 0; page < 8; page++ )
    {
        if( jslmState == JSLM_STATE_GAMEPLAY )
        {
            jslmCompositeBlocksRow( page );
            jslmCompositeSpritesRow( page );
        }

        for( x = 0; x < JSLM_SCREEN_WIDTH; x++ )
        {
            md_drawColumn( x, page, jslmComputeByte( x, page ) );
        }
    }
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

// Every state falls through to jslmRender() unconditionally at the end of
// gameJumpSlime_update() (no dirty-flag skip anywhere in this port), so
// NULL is the correct addGame() hook, matching Four in a Row/Dino Game/
// SnakeGame85's own confirmed-correct precedent.

void gameJumpSlime_init()
{
    InitTinyJoypad();

    jslmScoreDisp.xStart = JSLM_SCORE_X_START;
    jslmScoreDisp.xEnd = JSLM_SCORE_X_END;
    jslmStageDisp.xStart = JSLM_STAGE_X_START;
    jslmStageDisp.xEnd = JSLM_STAGE_X_END;

    jslmCurrentStage = 0;
    jslmResetPlayerForStage();
    jslmScore = 0;

    jslmState = JSLM_STATE_TITLE;
    jslmPrevFire = 0;
    jslmBlinkTimer = 0;
}

void gameJumpSlime_update()
{
    jslmTickSkipCounter = jslmTickSkipCounter + 1;
    if( jslmTickSkipCounter < JSLM_TICK_DIVISOR )
      return;
    jslmTickSkipCounter = 0;

    int fireNow = isFirePressed();
    int fireJustPressed = fireNow && !jslmPrevFire;

    if( jslmState == JSLM_STATE_TITLE )
    {
        if( fireJustPressed )
        {
            jslmState = JSLM_STATE_GAMEPLAY;
            jslmCurrentStage = 0;
            jslmResetPlayerForStage();
            jslmScore = 0;
        }
    }
    else if( jslmState == JSLM_STATE_GAMEPLAY )
    {
        int dx = 0;
        if( isRightPressed() ) { dx = JSLM_MOVE_DISTANCE; jslmCmp = 1; }
        else if( isLeftPressed() ) { dx = -JSLM_MOVE_DISTANCE; jslmCmp = 0; }

        if( fireJustPressed && jslmIsOnGround )
        {
            jslmGravity = JSLM_JUMP_STRENGTH;
            jslmIsOnGround = 0;
            Sound( 150, 50 );
        }

        jslmGravity = jslmGravity + 1;
        jslmCollisionXY( dx );

        // enemy movement + collision (matches upstream: not gated on
        // jslmState, so a same-tick fall-off-bottom game-over can in
        // theory still run this - harmless, see this file's own note on
        // why that's left unguarded to match upstream exactly)
        int* enemies = jslmEnemiesForStage( jslmCurrentStage );
        int numEnemies = jslmNumEnemies[ jslmCurrentStage ];

        int e;
        for( e = 0; e < numEnemies; e++ )
        {
            int tgtX1 = enemies[ e * 7 + 3 ];
            int tgtY1 = enemies[ e * 7 + 4 ];
            int tgtX2 = enemies[ e * 7 + 5 ];
            int tgtY2 = enemies[ e * 7 + 6 ];

            int targetX, targetY;
            if( jslmEnemyStates[ e ].targetIdx == 0 )
            {
                targetX = tgtX1;
                targetY = tgtY1;
            }
            else
            {
                targetX = tgtX2;
                targetY = tgtY2;
            }

            if( jslmEnemyStates[ e ].currentX < targetX ) jslmEnemyStates[ e ].currentX = jslmEnemyStates[ e ].currentX + 1;
            else if( jslmEnemyStates[ e ].currentX > targetX ) jslmEnemyStates[ e ].currentX = jslmEnemyStates[ e ].currentX - 1;
            if( jslmEnemyStates[ e ].currentY < targetY ) jslmEnemyStates[ e ].currentY = jslmEnemyStates[ e ].currentY + 1;
            else if( jslmEnemyStates[ e ].currentY > targetY ) jslmEnemyStates[ e ].currentY = jslmEnemyStates[ e ].currentY - 1;

            if( jslmEnemyStates[ e ].currentX == targetX && jslmEnemyStates[ e ].currentY == targetY )
              jslmEnemyStates[ e ].targetIdx = 1 - jslmEnemyStates[ e ].targetIdx;

            if( jslmHitTest( jslmMyX, jslmMyY, JSLM_CH_SIZE, JSLM_CH_SIZE, jslmEnemyStates[ e ].currentX, jslmEnemyStates[ e ].currentY, JSLM_CH_SIZE ) )
            {
                jslmState = JSLM_STATE_GAMEOVER;
                jslmStartGameOverSfx();
                break;
            }
        }

        // goal / stage-clear check - isOnGround is already false on the
        // same tick a fall-off-bottom game-over just happened (set inside
        // jslmCollisionXY's own game-over branch), so this naturally can't
        // also fire that same tick without an extra explicit state guard
        int goalBlockX = jslmGoalPosi[ jslmCurrentStage * 2 + 0 ];
        int goalBlockY = jslmGoalPosi[ jslmCurrentStage * 2 + 1 ];

        if( jslmIsOnGround &&
            jslmMyX < goalBlockX + JSLM_CH_SIZE && jslmMyX + JSLM_CH_SIZE > goalBlockX &&
            jslmMyY + JSLM_CH_SIZE == goalBlockY )
        {
            jslmCurrentStage = jslmCurrentStage + 1;
            jslmScore = jslmScore + 10;
            jslmStartClearSfx();
            jslmState = JSLM_STATE_STAGE_CLEAR;
            // matches upstream's own explicit comment: don't reset the
            // player/stage display after the final stage, or the screen
            // would go blank instead of showing "ALL CLEAR!"
            if( jslmCurrentStage < JSLM_TOTAL_STAGES )
              jslmResetPlayerForStage();
        }
    }
    else if( jslmState == JSLM_STATE_STAGE_CLEAR )
    {
        if( fireJustPressed )
        {
            if( jslmCurrentStage >= JSLM_TOTAL_STAGES )
              jslmState = JSLM_STATE_TITLE;
            else
              jslmState = JSLM_STATE_GAMEPLAY;
        }
    }
    else if( jslmState == JSLM_STATE_GAMEOVER )
    {
        if( fireJustPressed )
          jslmState = JSLM_STATE_TITLE;
    }

    jslmPrevFire = fireNow;

    jslmAdvanceGameOverSfx();
    jslmAdvanceClearSfx();

    jslmBlinkTimer = jslmBlinkTimer + 1;
    if( jslmBlinkTimer >= 36 ) jslmBlinkTimer = 0;
    if( jslmBlinkTimer < 18 ) jslmBlinkCh = 1; else jslmBlinkCh = 0;

    jslmRender();
}

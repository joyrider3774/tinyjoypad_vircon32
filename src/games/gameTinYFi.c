// =============================================================================
// TinY Fi - ported from attiny85Bttle13Boss.ino ("more games/sample/
// attiny85Bttle13Boss/" - the folder name references the local user's own
// "battle" + "13" + "boss" naming, not the author's real title). Author:
// 近藤さんちの研究室 ("Kondo-san's Laboratory"), note.com handle "kondolab" -
// credited in the menu as "KONDOLAB", same treatment as this project's own
// Jump Slime/TinyRoG credits (no individual real name given anywhere in
// English). Article: https://note.com/kondolab/n/n2c96413eaa23 (published
// 2025-11-03), source download `sample4.zip` linked from that article, also
// playable via an embedded Wokwi simulator. No license is stated anywhere on
// the article page for this game's own code - listed as "None specified" for
// licensing purposes only, matching Jump Slime/TinyRoG's own precedent (a
// known author, an unstated license, not the same thing as an unknown
// author).
//
// A belt-scroll-style fighting game - move/jump with the d-pad, punch/
// uppercut/jump-kick with Fire, across 6 stages of increasingly numerous
// enemies (the last enemy of stages 3 and 6 is a tougher "boss" with double
// HP and its own head sprite), described by the author as "a simple
// fighting game mixed with belt-scroll action" kept small to fit the
// ATtiny85's 8KB flash. Last of the 3 `more games/sample/` games to be
// ported (after Jump Slime and TinyRoG) - picked last specifically because
// its render dispatch is a dense, per-animation-state tangle of several
// stacked sprite layers per character (body/head/color-tint), the exact
// shape this project's own CLAUDE.md flagged as the reason it was
// deprioritized versus TinyRoG's more self-contained algorithmic
// complexity (maze generation, turn-based combat) at the time.
//
// `#include "ELECTROLIB.h"` directly (same header, confirmed byte-identical
// to this project's own already-staged copy) - the same driver lineage as
// every other Daniel-C-family game already in this project, and the same
// one Jump Slime/TinyRoG's own `util.h`-lookalikes turned out to reproduce
// under a different filename. No new shim needed:
// `isLeftPressed()`/`isRightPressed()`/`isUpPressed()`/`isFirePressed()`/
// `Sound()` from the existing `tinyJoypadShim` cover the whole input/audio
// surface, and `tfiBlitzSprite()`/`tfiSpeedBlitz()`/`tfiBlitzSpriteDir()`
// below are the same already-proven translation of
// `blitzSprite`/`SPEED_BLITZ`/the left-right-mirroring wrapper this
// project's `gameTinyBert.c`/`gameTinyMissile.c`/`gameJumpSlime.c` already
// carry under their own prefixes.
//
// Unlike Jump Slime/TinyRoG, this game's own upstream `loop()` has no
// blocking waits or FPS-limiting mechanism at all to convert - every state
// transition is already a plain per-tick edge-detected button check
// (`BUTTON_DOWN&&lastBtnAState==HIGH`, `lastBtnAState` updated once per
// real loop iteration), so `tfiPrevFire` (checked against a fresh
// `isFirePressed()` read every real engine frame) reproduces this exactly
// with no restructuring needed. Runs at the engine's native 60fps, matching
// the "no timing model whatsoever upstream" default treatment this project
// already uses for Jump Slime/Tiny Trick/Invaders/Pinball/Bert/Tris, rather
// than guessing at a slowdown with no evidence for one.
//
// Sound: every one of this game's 3 `Sound()` calls (punch/kick-or-upper
// hit, player-damaged) fires exactly once per event - no synchronous
// multi-call bursts anywhere in the file (confirmed directly, and already
// flagged clean in Jump Slime's own header comment from the project-wide
// sound-burst check done at that game's own porting time) - no sequencer
// needed here.
//
// A proactive fix applied from the start rather than found via a bug
// report: `RecupeLineY(Valeur)`/`RecupeDecalageY(Valeur)` are fed genuinely
// negative Y positions here (a jump's initial `gravity=JUMP_STRENGTH=-8`
// can carry a character's Y above 0 before gravity brings it back down,
// unlike Jump Slime's own block/enemy Y values, which never go negative) -
// upstream's raw `Valeur>>3` relies on AVR-GCC's arithmetic (sign-
// extending) right shift on `int8_t`, but Vircon32's `>>` is a documented
// *logical* (zero-fill) shift - the exact same bug class already found and
// fixed in HollowSeeker's `hsDivByColumnW` and Tiny Pipe's own
// `RecupeLineY`. Fixed the same way as Tiny Pipe: `tfiRecupeLineY()`
// branches on sign (`-((-val+7)>>3)` for negatives, only ever shifting a
// non-negative operand), and `tfiRecupeDecalageY()` is derived directly
// from that now-safe helper instead of its own separate raw shift.
//
// Several genuine upstream quirks were traced and preserved faithfully
// rather than "corrected" (matching this project's own standing practice
// for confirmed-harmless pre-existing behavior, e.g. Tiny Morpion's own
// win-check, Tiny DDug's ghost-enemy tracking):
// - `initGame()` computes a random enemy spawn X (`16*(rand()%5+1)`) that
//   is unconditionally overwritten by 3 fixed positions (8/80/96) right
//   below it, before ever being read - a genuine dead computation, traced
//   by checking every read of these fields between the two - skipped
//   entirely here rather than calling `arand()` for no observable effect.
// - the punch-cooldown-times-4 uppercut-cooldown bonus
//   (`pncCd=14; if(pncCd%4==0) pncCd+=14;`) can never actually fire, since
//   `pncCd` is set to the literal `14` on the line immediately above the
//   check and `14%4==2`, never `0` - ported literally (the always-false
//   condition is still computed) rather than silently dropped, in case a
//   future upstream revision changes the literal.
// - the boss-designation check on enemy respawn reads `acter[i].x!=200`
//   (`i`, the 0..2 loop index into the *whole* `acter[]` array - i.e.
//   `ME`'s own X for `i==0`) rather than the enemy actually being
//   respawned (`acter[ENM+i].x`) - almost certainly an off-by-one typo in
//   the original, but since the player's own X is clamped well inside the
//   screen and never reaches 200, this condition is always true in
//   practice - a harmless no-op, ported literally.
// - `acter[ME].cdw` is decremented twice in a single tick (once at the very
//   top of `GAME_STAGE`, once again near the bottom) - a genuine upstream
//   double-decrement, not a transcription slip on this port's part (traced
//   directly against both source line ranges) - reproduced exactly at both
//   points, since it's already-tuned, shipped cooldown-timing behavior,
//   not a bug to fix.
// - `setup()`'s own `enmCntNow=enmCnt[nowStageNo]` runs with
//   `nowStageNo=200` (the Title screen's own sentinel value) against a
//   6-entry array - an out-of-bounds read that's harmless-but-undefined on
//   real AVR flash (PROGMEM has no memory protection) but a genuine
//   memory-safety risk here. Traced every later read of this field and
//   confirmed it's always overwritten by `tfiInitGame()` before a real
//   floor ever starts (the Title/floor-intro screens drawn while
//   `nowGame==GAME_TEXT` never read it) - skipped rather than guarded,
//   since guarding a value that's never actually read would be pure
//   ceremony.
//
// Rendering: applied this project's own "self-gated call still costs a
// full call every time it's invoked" lesson from the start (matching Jump
// Slime's own from-day-1 treatment, not a later retrofit) rather than
// porting upstream's per-pixel per-character dispatch verbatim - with up
// to 4 characters and as many as 3 stacked sprite layers (body/head/color
// tint) each, an unguarded per-pixel port would cost up to
// 4 characters * ~3 layers * 128 columns * 8 pages calls/frame, the same
// O(pixels x objects) shape already found and fixed repeatedly elsewhere
// in this project. `tfiComposeActerRow()` composites all 4 characters into
// a shared `tfiSpriteRowBuffer[128]` once per page, only ever calling the
// real per-column sprite-blit logic (`tfiComposeActerColumn()`) across
// each character's own real ~16-20-column footprint (after a cheap Y-page-
// range reject) rather than scanning all 128 columns per character.
// Dropped upstream's own `startPage`/`endPage` min/max-Y range-narrowing
// entirely (an AVR I2C-bandwidth optimization, not a correctness
// requirement) in favor of always compositing/redrawing all 8 pages every
// frame, matching this project's own established treatment of every other
// upstream partial-redraw trick (Pinball/Doc/Bert/Tris/Pipe/Plaque) -
// characters are never drawn on page 0 or page 7 either way (upstream's
// own render loop unconditionally skips both, reserving them for the
// HP-bar/label HUD), so this is a pure simplification with no visual
// difference.
// =============================================================================

#define TFI_SCREEN_WIDTH 128
#define TFI_SCREEN_HEIGHT 64

#define TFI_STAND 0
#define TFI_MOVE 1
#define TFI_DAMAGE 2
#define TFI_DOWN 3
#define TFI_JUMP 4
#define TFI_PUNCH 5
#define TFI_UPPER 6
#define TFI_KICK 7

#define TFI_ATK_SIZE 9
#define TFI_HIT_SIZE 10
#define TFI_ACT_JUMPKICK 110
#define TFI_JUMP_COOLDOWN 8
#define TFI_JUMP_STRENGTH -8
#define TFI_GROUND 32

#define TFI_CNTCYCLE 8

#define TFI_ME 0
#define TFI_ENM 1
#define TFI_ENM_LEN 3
#define TFI_NUM_ACTER 4

#define TFI_STAGE_CNT 6

#define TFI_GAME_STAGE 0
#define TFI_GAME_TEXT 1

#define TFI_SCORE_X_START ( TFI_SCREEN_WIDTH - 8 )
#define TFI_SCORE_X_END ( TFI_SCORE_X_START + 8 )
#define TFI_STAGE_X_START ( TFI_SCREEN_WIDTH / 2 )
#define TFI_STAGE_X_END ( TFI_STAGE_X_START + 8 )

// Added at direct user request. Upstream itself has no genuine real-time
// throttle at all (no FPS_Control, no CONTROL_FRAMERATE call - the "no
// timing model whatsoever upstream" category from this project's own
// frame-pacing survey, same category as Jump Slime/TinyRoG/Tiny Trick/
// Invaders/Pinball/Bert/Tris), so this is a deliberate slowdown rather than
// restoring an original rate. Whole-function tick-skip (gates logic AND
// redraw together), matching Jump Slime/TinyRoG/NumberPlace/HollowSeeker/
// t2048/Doc/Pacman/Pipe's own precedent rather than the movement-only/
// redraw-stays-60fps split used for games that had an already-established
// 60fps-tuned wait timer needing to stay real-time accurate - this file has
// no such existing timer to preserve (every cdw/pncCd/tfiCnt counter
// simply now advances at half the real rate, matching this project's own
// standing "one divisor, no dual bookkeeping" practice - none of them are
// rescaled here).
#define TFI_FPS 30
#define TFI_TICK_DIVISOR ( 60 / TFI_FPS )
int tfiTickSkipCounter;

// -----------------------------------------------------------------------------
// Sprite data - byte-diff-verified against the upstream source via a small
// Python script before ever being pasted in, not hand-copied blind (matching
// this project's own standing "byte-diff transcribed tables" discipline,
// reinforced hard by earlier sessions' own transcription bugs).
// -----------------------------------------------------------------------------

int[42] tfiMiniNum =
{
    4, 1, 248, 136, 248, 0, 0, 248, 0, 0, 232, 168, 184, 0, 136, 168,
    248, 0, 56, 32, 248, 0, 184, 168, 232, 0, 248, 168, 232, 0, 8, 232,
    24, 0, 248, 168, 248, 0, 184, 168, 248, 0,
};

// 7 frames: 0=HP, 1=EN, 2=Fl., 3=DIE, 4=Fin, 5=Tin, 6=yFi (frames 5+6
// together spell "TinyFi" on the title screen)
int[58] tfiMiniStg =
{
    8, 1, 0, 62, 8, 62, 0, 62, 10, 14, 62, 42, 42, 0, 62, 4,
    8, 62, 248, 40, 0, 248, 128, 0, 128, 0, 248, 136, 112, 0, 248, 0,
    248, 168, 248, 40, 0, 232, 0, 224, 32, 192, 8, 248, 8, 208, 0, 224,
    32, 192, 16, 224, 16, 0, 248, 40, 0, 208,
};

int[18] tfiFighterColor =
{
    4, 1, 49, 62, 63, 1, 24, 31, 19, 0, 32, 126, 31, 6, 7, 7,
    0, 0,
};

// 7 frames: STAND, MOVE, DAMAGE, DOWN, JUMP/KICK, PUNCH, UPPER
int[114] tfiMyHead =
{
    16, 1, 0, 0, 0, 0, 64, 128, 224, 192, 224, 64, 128, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 128, 160, 192, 240, 96, 240, 32, 64, 0,
    0, 0, 0, 128, 64, 224, 192, 224, 128, 64, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 128, 0, 192, 128, 192, 128, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 64, 128, 224, 192, 224, 64, 128, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 32, 192, 240, 224, 112, 128, 216, 40,
    248, 0,
};

int[114] tfiBossHead =
{
    16, 1, 0, 0, 64, 32, 48, 176, 224, 192, 128, 64, 128, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 32, 144, 152, 216, 240, 96, 192, 32, 64, 0,
    0, 0, 0, 128, 64, 128, 192, 224, 176, 48, 32, 64, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 128, 64, 96, 96, 192, 128, 0, 128, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 64, 32, 48, 176, 224, 192, 128, 64, 128, 0, 0, 0,
    0, 0, 0, 0, 0, 64, 32, 48, 48, 96, 224, 224, 96, 128, 216, 40,
    248, 0,
};

// 3 frames, 1 page tall each: [0]=neck, [1]=upper body, [2]=lower body -
// stacked at y/y+8/y+16 by the STAND/MOVE draw code to build one 3-page
// tall character out of independently-animatable pieces.
int[50] tfiFighterStd =
{
    16, 1, 0, 0, 0, 0, 0, 128, 192, 192, 128, 64, 128, 0, 0, 0,
    0, 0, 0, 28, 34, 185, 119, 59, 5, 100, 223, 122, 84, 56, 0, 0,
    0, 0, 0, 192, 190, 177, 248, 6, 2, 12, 241, 186, 188, 192, 0, 0,
    0, 0,
};

// 4 frames, 3 pages tall each (a single blitzSprite call spans all 3
// pages): 0=Move, 1=Damage, 2=Down, 3=Jump
int[194] tfiFighter =
{
    16, 3, 0, 0, 0, 0, 0, 128, 128, 192, 224, 96, 192, 32, 64, 0,
    0, 0, 0, 0, 0, 14, 17, 220, 59, 29, 2, 50, 239, 61, 42, 28,
    0, 0, 0, 0, 0, 192, 191, 184, 252, 3, 223, 188, 176, 239, 0, 0,
    0, 0, 0, 128, 64, 128, 192, 192, 128, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 56, 84, 250, 87, 100, 5, 51, 206, 10, 20, 56, 80, 48, 0,
    0, 0, 0, 192, 191, 188, 248, 6, 12, 24, 251, 188, 176, 192, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 192, 192, 64, 160, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 124, 171, 189, 98, 194, 223, 205, 106, 46, 88, 208, 160, 160, 192,
    0, 0, 0, 0, 0, 0, 0, 128, 128, 0, 128, 0, 0, 0, 0, 0,
    0, 0, 56, 68, 114, 238, 119, 139, 137, 255, 36, 89, 104, 56, 0, 0,
    0, 0, 0, 0, 99, 182, 254, 52, 25, 31, 15, 14, 0, 0, 0, 0,
    0, 0,
};

// 3 frames, 3 pages tall each: 0=Punch, 1=Upper, 2=JumpKick
int[146] tfiFighterAct =
{
    16, 3, 0, 0, 0, 0, 0, 128, 192, 192, 128, 64, 128, 0, 0, 0,
    0, 0, 0, 28, 34, 185, 119, 59, 5, 4, 251, 14, 10, 10, 6, 6,
    10, 14, 0, 192, 190, 177, 248, 6, 2, 12, 240, 187, 188, 192, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 224, 224, 96, 128, 216, 40,
    248, 0, 0, 0, 0, 0, 12, 22, 218, 61, 3, 193, 243, 61, 6, 1,
    0, 0, 0, 0, 224, 184, 254, 61, 24, 12, 3, 119, 191, 191, 192, 0,
    0, 0, 0, 0, 0, 0, 0, 128, 128, 0, 128, 0, 0, 0, 0, 0,
    0, 0, 56, 68, 242, 110, 119, 11, 73, 255, 212, 149, 40, 56, 80, 48,
    0, 0, 6, 13, 8, 10, 31, 23, 30, 6, 4, 12, 13, 15, 14, 14,
    20, 28,
};

int[6] tfiEnmCnt = { 1, 1, 2, 2, 3, 3, };

// -----------------------------------------------------------------------------
// Sprite blitting primitives - a direct, already-proven translation of
// upstream's own blitzSprite()/SPEED_BLITZ()/SplitSpriteDecalageY(), the
// same algorithm gameTinyBert.c/gameTinyMissile.c/gameJumpSlime.c already
// carry under their own prefixes - except RecupeLineY/RecupeDecalageY,
// which need the sign-safe rewrite described in this file's own header
// comment (Y positions here, unlike those other games, can go negative).
// -----------------------------------------------------------------------------

int tfiRecupeLineY( int valeur )
{
    if( valeur >= 0 ) return valeur >> 3;
    return -( ( -valeur + 7 ) >> 3 );
}

int tfiRecupeDecalageY( int valeur )
{
    return valeur - ( tfiRecupeLineY( valeur ) << 3 );
}

int tfiSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown )
      return ( input << decalage ) & 0xFF;
    return input >> ( 8 - decalage );
}

int tfiBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[ 0 ];
    int hSprite = sprites[ 1 ];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = tfiRecupeLineY( yPos );

    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos ||
        recupeLineY > yPass || ( recupeLineY + hSprite ) < yPass )
      return 0;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = tfiRecupeDecalageY( yPos );
    int scanA = ( xPass - xPos ) + ( spriteYLine * wSprite ) + 2;
    int scanB = ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;

    int outByte;
    if( scanA > wMax )
      outByte = 0;
    else
      outByte = tfiSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = tfiSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

int tfiSpeedBlitz( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[ 0 ];
    int hSprite = sprites[ 1 ];
    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos || yPass < yPos || yPass > ( yPos + ( hSprite - 1 ) ) )
      return 0;
    return sprites[ 2 + ( ( xPass - xPos ) + ( ( yPass - yPos ) * wSprite ) + ( frame * ( hSprite * wSprite ) ) ) ];
}

// left-right mirroring wrapper - matches upstream's blitzSpriteDir()
int tfiBlitzSpriteDir( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites, int dir )
{
    if( dir == 0 )
      return tfiBlitzSprite( xPos, yPos, xPass, yPass, frame, sprites );
    int newXPos = xPos - 4;
    int wSprite = sprites[ 0 ];
    int flippedXPass = newXPos + ( wSprite - 1 ) - ( xPass - newXPos );
    return tfiBlitzSprite( newXPos, yPos, flippedXPass, yPass, frame, sprites );
}

// -----------------------------------------------------------------------------
// Number / HUD display helpers
// -----------------------------------------------------------------------------

struct TfiNumDisp
{
    int tens, units, xStart, xEnd;
};

TfiNumDisp tfiScoreDisp;
TfiNumDisp tfiStageDisp;

void tfiMakeNumData( int value, TfiNumDisp* data )
{
    data->tens = value / 10;
    data->units = value % 10;
}

int tfiGetByteNum( int xPix, TfiNumDisp* data )
{
    if( xPix < data->xStart || xPix > data->xEnd ) return 0;
    int tmpBmp = 0;
    tmpBmp = tmpBmp | tfiSpeedBlitz( data->xStart + 0, 0, xPix, 0, data->tens, tfiMiniNum );
    tmpBmp = tmpBmp | tfiSpeedBlitz( data->xStart + 4, 0, xPix, 0, data->units, tfiMiniNum );
    return tmpBmp;
}

// Reproduces Trace_LINE(1,...)'s output for this file's own one specific
// use (a horizontal HP-bar line, both endpoints at the same Y) - since
// DESACTIVE_=1 always forces Trace_LINE's own point-based path, and a
// horizontal line's Mymap() always resolves to the same Y regardless of X,
// its real output collapses to "a single set bit at row hpY, for every
// column from hpX to hpX+hpLen inclusive". Inlined directly into
// tfiComposeHudRow()'s own once-per-page bar-writing loop rather than kept
// as a separate per-column function - see that function's own comment.
//
// -----------------------------------------------------------------------------
// Character state
// -----------------------------------------------------------------------------

struct TfiActer
{
    int x, y, hp, dir, act, cdw;
};

TfiActer[4] tfiActer;

int tfiCnt;
int tfiNowGame;
int tfiNowStageNo;
int tfiEnmCntNow;
int tfiEnmBossNo;

int tfiMyAtkHit;
int tfiJmpState;
int tfiPncCd;
int tfiPncCnt;
int tfiGravity;

int tfiPrevFire;

// -----------------------------------------------------------------------------
// Collision helpers
// -----------------------------------------------------------------------------

bool tfiHitTest( int x1, int y1, int len1, int x2, int y2, int len2 )
{
    return ( x1 < x2 + len2 ) && ( x1 + len1 > x2 ) && ( y1 < y2 + len2 ) && ( y1 + len1 > y2 );
}

void tfiGetHitPosi( int charX, int charY, int charDir, int charAct, int* outX, int* outY )
{
    int offsetX = 0;
    int offsetY = 0;
    if( charAct == TFI_PUNCH || charAct == TFI_UPPER || charAct == TFI_MOVE || charAct == TFI_STAND )
    {
        offsetY = 5;
        if( charDir == 0 ) offsetX = 1; else offsetX = 0;
    }
    else if( charAct == TFI_KICK || charAct == TFI_JUMP )
    {
        offsetY = 11;
        if( charDir == 0 ) offsetX = 1; else offsetX = 0;
    }
    *outX = charX + offsetX;
    *outY = charY + offsetY;
}

void tfiGetAtkPosi( int charX, int charY, int charDir, int charAct, int* outX, int* outY )
{
    int offsetX = 0;
    int offsetY = 0;
    if( charAct == TFI_PUNCH )
    {
        offsetY = 6;
        if( charDir == 0 ) offsetX = 6; else offsetX = -4;
    }
    else if( charAct == TFI_UPPER )
    {
        if( charDir == 0 ) offsetX = 6; else offsetX = -4;
    }
    else if( charAct == TFI_KICK )
    {
        offsetY = 14;
        if( charDir == 0 ) offsetX = 6; else offsetX = -4;
    }
    *outX = charX + offsetX;
    *outY = charY + offsetY;
}

bool tfiChkCollision( int* nextX, int* nextY, int nextDir, int nextAct, bool isPushBack )
{
    int myHitX, myHitY;
    tfiGetHitPosi( *nextX, *nextY, nextDir, nextAct, &myHitX, &myHitY );

    int ci;
    for( ci = 0; ci < TFI_ENM_LEN; ci = ci + 1 )
    {
        if( tfiActer[ TFI_ENM + ci ].act == TFI_DAMAGE || tfiActer[ TFI_ENM + ci ].act == TFI_DOWN )
          continue;

        int enmHitX, enmHitY;
        tfiGetHitPosi( tfiActer[ TFI_ENM + ci ].x, tfiActer[ TFI_ENM + ci ].y, tfiActer[ TFI_ENM + ci ].dir, tfiActer[ TFI_ENM + ci ].act, &enmHitX, &enmHitY );
        if( tfiHitTest( myHitX, myHitY, TFI_HIT_SIZE, enmHitX, enmHitY, TFI_HIT_SIZE ) )
        {
            if( isPushBack )
            {
                int myCenter = *nextX + ( TFI_HIT_SIZE / 2 );
                int enmCenter = tfiActer[ TFI_ENM + ci ].x + ( TFI_HIT_SIZE / 2 );
                if( myCenter < enmCenter && *nextX > 4 )
                  *nextX = *nextX - 1;
                else if( myCenter >= enmCenter && *nextX < TFI_SCREEN_WIDTH - 16 )
                  *nextX = *nextX + 1;
            }
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// initGame() - resets player/enemy state for a fresh stage (called both on
// a brand new game and on every subsequent floor transition, matching
// upstream's own single entry point for both cases).
// -----------------------------------------------------------------------------

void tfiInitGame()
{
    tfiMyAtkHit = 0;
    tfiJmpState = 0;
    tfiPncCd = 0;
    tfiPncCnt = 0;
    tfiEnmBossNo = 99;
    tfiEnmCntNow = tfiEnmCnt[ tfiNowStageNo ];

    tfiActer[ TFI_ME ].x = 32;
    tfiActer[ TFI_ME ].y = TFI_GROUND;
    tfiActer[ TFI_ME ].hp = 8;
    tfiActer[ TFI_ME ].dir = 0;
    tfiActer[ TFI_ME ].act = TFI_STAND;
    tfiActer[ TFI_ME ].cdw = 0;

    int ii;
    for( ii = 0; ii < TFI_ENM_LEN; ii = ii + 1 )
    {
        // upstream computes a random spawn X here (16*(rand()%5+1)) that is
        // unconditionally overwritten by the 3 fixed assignments right
        // below - see this file's own header comment. Skipped entirely.
        tfiActer[ TFI_ENM + ii ].y = TFI_GROUND;
        tfiActer[ TFI_ENM + ii ].hp = 8;
        tfiActer[ TFI_ENM + ii ].dir = 0;
        tfiActer[ TFI_ENM + ii ].act = TFI_STAND;
        tfiActer[ TFI_ENM + ii ].cdw = 0;
    }
    tfiActer[ 1 ].x = 8;
    tfiActer[ 2 ].x = 80;
    tfiActer[ 3 ].x = 96;
}

// -----------------------------------------------------------------------------
// Rendering - see this file's own header comment on why compositing happens
// once per page instead of once per pixel per character.
//
// A second, deeper round of the same lesson, found via a direct user CPU
// report during this port's own first playtest (visible as characters
// rendering as small, incomplete blobs mid-fight - this project's own
// documented "frame truncated mid-instruction-stream, real CPU-budget
// exceeded" failure signature, not a logic bug): the first version of this
// compositing still called tfiBlitzSpriteDir() once per *column* per layer
// (up to ~20 columns x up to 5 layers x up to 4 characters x up to 6 pages),
// and each of those calls independently recomputed the same per-row-constant
// values (wMax/picByte/recupeLineY/spriteYDecalage) that don't actually
// change across a single sprite layer's own row. `tfiBlitzSpriteRow()`
// hoists that computation out to run once per (character, layer, page)
// instead of once per column - the same "cache/hoist what doesn't change
// across an inner loop" lesson as TinyRoG's own tile-row compositing and
// Tiny DDug's wall-mask cache, just applied one level deeper than the
// per-character row-buffer compositing already in place.
// -----------------------------------------------------------------------------

// Composites one sprite layer's own real column range directly into
// outBuffer for the given page - the row-constant parts of
// tfiBlitzSprite()'s own algorithm (wMax/picByte/recupeLineY/
// spriteYDecalage) are computed once here rather than once per column, and
// this replaces tfiBlitzSpriteDir()'s own xPos-shift-plus-mirrored-xPass
// trick with an equivalent direct source-column reversal, since there's no
// longer a single xPass being fed through a generic entry point.
void tfiBlitzSpriteRow( int page, int xPos, int yPos, int frame, int* sprites, int dir, int* outBuffer )
{
    int wSprite = sprites[ 0 ];
    int hSprite = sprites[ 1 ];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = tfiRecupeLineY( yPos );

    if( recupeLineY > page || ( recupeLineY + hSprite ) < page )
      return;

    int spriteYLine = page - recupeLineY;
    int spriteYDecalage = tfiRecupeDecalageY( yPos );

    int actualXPos = xPos;
    if( dir != 0 ) actualXPos = xPos - 4;

    int colIdx;
    for( colIdx = 0; colIdx < wSprite; colIdx = colIdx + 1 )
    {
        int screenX = actualXPos + colIdx;
        if( screenX < 0 || screenX >= TFI_SCREEN_WIDTH ) continue;

        int sourceCol;
        if( dir == 0 ) sourceCol = colIdx; else sourceCol = ( wSprite - 1 ) - colIdx;

        int scanA = sourceCol + ( spriteYLine * wSprite ) + 2;
        int outByte;
        if( scanA > wMax )
          outByte = 0;
        else
          outByte = tfiSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

        if( spriteYLine > 0 )
        {
            int scanB = sourceCol + ( ( spriteYLine - 1 ) * wSprite ) + 2;
            if( scanB <= wMax )
            {
                int outByte2 = tfiSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
                outByte = outByte | outByte2;
            }
        }

        outBuffer[ screenX ] = outBuffer[ screenX ] | outByte;
    }
}

int[128] tfiSpriteRowBuffer;

// Same layer logic as before, restructured to call tfiBlitzSpriteRow() once
// per layer instead of tfiBlitzSpriteDir() once per column per layer.
void tfiComposeActerLayers( int i, int page, int ani )
{
    int act = tfiActer[ i ].act;
    int x = tfiActer[ i ].x;
    int y = tfiActer[ i ].y;
    int dir = tfiActer[ i ].dir;

    // body layer (non-STAND/MOVE)
    if( act == TFI_DAMAGE || act == TFI_DOWN || act == TFI_JUMP )
      tfiBlitzSpriteRow( page, x, y, act - 1, tfiFighter, dir, tfiSpriteRowBuffer );
    else if( act == TFI_PUNCH || act == TFI_KICK || act == TFI_UPPER )
      tfiBlitzSpriteRow( page, x, y, act - 5, tfiFighterAct, dir, tfiSpriteRowBuffer );

    // head layer (same act set as the body layer above; ME/boss only)
    if( act == TFI_DAMAGE || act == TFI_DOWN || act == TFI_JUMP || act == TFI_PUNCH || act == TFI_UPPER || act == TFI_KICK )
    {
        if( i == TFI_ME )
        {
            int tmpAct = act;
            if( act == TFI_KICK ) tmpAct = TFI_JUMP;
            tfiBlitzSpriteRow( page, x, y, tmpAct, tfiMyHead, dir, tfiSpriteRowBuffer );
        }
        else if( i == tfiEnmBossNo )
        {
            int tmpEAc = act;
            if( act == TFI_KICK ) tmpEAc = TFI_JUMP;
            tfiBlitzSpriteRow( page, x, y, tmpEAc, tfiBossHead, dir, tfiSpriteRowBuffer );
        }
    }

    // per-state extra layer
    if( act == TFI_STAND )
    {
        tfiBlitzSpriteRow( page, x, y + ani, 0, tfiFighterStd, dir, tfiSpriteRowBuffer );
        tfiBlitzSpriteRow( page, x, y + 8 + ani, 1, tfiFighterStd, dir, tfiSpriteRowBuffer );
        tfiBlitzSpriteRow( page, x, y + 16, 2, tfiFighterStd, dir, tfiSpriteRowBuffer );
        if( i == TFI_ME )
          tfiBlitzSpriteRow( page, x, y + ani, act, tfiMyHead, dir, tfiSpriteRowBuffer );
        else if( i == tfiEnmBossNo )
          tfiBlitzSpriteRow( page, x, y + ani, act, tfiBossHead, dir, tfiSpriteRowBuffer );
        else
        {
            int plsX = 5;
            if( dir != 0 ) plsX = 7;
            tfiBlitzSpriteRow( page, x + plsX, y + 10, 0, tfiFighterColor, dir, tfiSpriteRowBuffer );
        }
    }
    else if( act == TFI_MOVE )
    {
        if( ani == 0 )
        {
            tfiBlitzSpriteRow( page, x, y, 0, tfiFighterStd, dir, tfiSpriteRowBuffer );
            tfiBlitzSpriteRow( page, x, y + 8, 1, tfiFighterStd, dir, tfiSpriteRowBuffer );
            tfiBlitzSpriteRow( page, x, y + 16, 2, tfiFighterStd, dir, tfiSpriteRowBuffer );
            if( i == TFI_ME )
              tfiBlitzSpriteRow( page, x, y, TFI_STAND, tfiMyHead, dir, tfiSpriteRowBuffer );
            else if( i == tfiEnmBossNo )
              tfiBlitzSpriteRow( page, x, y, TFI_STAND, tfiBossHead, dir, tfiSpriteRowBuffer );
            else
            {
                int plsX = 5;
                if( dir != 0 ) plsX = 7;
                tfiBlitzSpriteRow( page, x + plsX, y + 10, 0, tfiFighterColor, dir, tfiSpriteRowBuffer );
            }
        }
        else
        {
            tfiBlitzSpriteRow( page, x, y, act - 1, tfiFighter, dir, tfiSpriteRowBuffer );
            if( i == TFI_ME )
              tfiBlitzSpriteRow( page, x, y, act, tfiMyHead, dir, tfiSpriteRowBuffer );
            else if( i == tfiEnmBossNo )
              tfiBlitzSpriteRow( page, x, y, act, tfiBossHead, dir, tfiSpriteRowBuffer );
            else
            {
                int plsX = 7;
                if( dir != 0 ) plsX = 5;
                tfiBlitzSpriteRow( page, x + plsX, y + 10, 1, tfiFighterColor, dir, tfiSpriteRowBuffer );
            }
        }
    }
    else if( act == TFI_DAMAGE )
    {
        if( i != TFI_ME )
        {
            int plsX = 4;
            if( dir != 0 ) plsX = 8;
            tfiBlitzSpriteRow( page, x + plsX, y + 10, 0, tfiFighterColor, dir, tfiSpriteRowBuffer );
        }
    }
    else if( act == TFI_DOWN )
    {
        if( i != TFI_ME )
        {
            int plsX = 3;
            if( dir != 0 ) plsX = 9;
            tfiBlitzSpriteRow( page, x + plsX, y + 18, 3, tfiFighterColor, dir, tfiSpriteRowBuffer );
        }
    }
    else if( act == TFI_JUMP )
    {
        if( i != TFI_ME )
        {
            int plsX = 5;
            if( dir != 0 ) plsX = 7;
            tfiBlitzSpriteRow( page, x + plsX, y + 12, 3, tfiFighterColor, dir, tfiSpriteRowBuffer );
        }
    }
    else if( act == TFI_PUNCH )
    {
        if( i != TFI_ME )
        {
            int plsX = 5;
            if( dir != 0 ) plsX = 7;
            tfiBlitzSpriteRow( page, x + plsX, y + 10, 0, tfiFighterColor, dir, tfiSpriteRowBuffer );
        }
    }
    else if( act == TFI_UPPER )
    {
        if( i != TFI_ME )
        {
            int plsX = 7;
            if( dir != 0 ) plsX = 5;
            tfiBlitzSpriteRow( page, x + plsX, y + 9, 0, tfiFighterColor, dir, tfiSpriteRowBuffer );
        }
    }
    else if( act == TFI_KICK )
    {
        if( i != TFI_ME )
        {
            int plsX = 5;
            if( dir != 0 ) plsX = 7;
            tfiBlitzSpriteRow( page, x + plsX, y + 12, 3, tfiFighterColor, dir, tfiSpriteRowBuffer );
        }
    }
}

void tfiComposeActerRow( int page )
{
    int x;
    for( x = 0; x < TFI_SCREEN_WIDTH; x = x + 1 ) tfiSpriteRowBuffer[ x ] = 0;

    int ani = ( tfiCnt / TFI_CNTCYCLE ) % 2;

    int i;
    for( i = 0; i < TFI_NUM_ACTER; i = i + 1 )
    {
        // cheap per-character page reject - a character's own sprite stack
        // spans at most y..y+24 (3 pages); a small margin either side keeps
        // this a safe superset of every real blit's own internal bound
        // check, not an exact one.
        int topLine = tfiRecupeLineY( tfiActer[ i ].y ) - 1;
        int botLine = tfiRecupeLineY( tfiActer[ i ].y + 24 ) + 1;
        if( page < topLine || page > botLine ) continue;

        tfiComposeActerLayers( i, page, ani );
    }
}

int tfiComputeByte( int x, int page )
{
    int displayByte = 0;

    if( tfiNowGame == TFI_GAME_TEXT )
    {
        if( page == 3 )
        {
            if( tfiNowStageNo == 100 )
              displayByte = displayByte | tfiSpeedBlitz( TFI_SCREEN_WIDTH / 2 - 4, 0, x, 0, 3, tfiMiniStg );
            else if( tfiNowStageNo == 101 )
              displayByte = displayByte | tfiSpeedBlitz( TFI_SCREEN_WIDTH / 2 - 4, 0, x, 0, 4, tfiMiniStg );
            else if( tfiNowStageNo == 200 )
            {
                displayByte = displayByte | tfiSpeedBlitz( TFI_SCREEN_WIDTH / 2 - 8, 0, x, 0, 5, tfiMiniStg );
                displayByte = displayByte | tfiSpeedBlitz( TFI_SCREEN_WIDTH / 2, 0, x, 0, 6, tfiMiniStg );
            }
            else
            {
                tfiMakeNumData( tfiNowStageNo + 1, &tfiStageDisp );
                displayByte = displayByte | tfiGetByteNum( x, &tfiStageDisp );
                displayByte = displayByte | tfiSpeedBlitz( TFI_SCREEN_WIDTH / 2 - 8, 0, x, 0, 2, tfiMiniStg );
            }
        }
        return displayByte;
    }

    // TFI_GAME_STAGE
    if( page == 7 )
    {
        if( x >= tfiScoreDisp.xStart - 9 && x <= tfiScoreDisp.xEnd )
        {
            displayByte = displayByte | tfiBlitzSprite( TFI_SCREEN_WIDTH - 17, TFI_SCREEN_HEIGHT - 6, x, 7, 1, tfiMiniStg );
            displayByte = displayByte | tfiGetByteNum( x, &tfiScoreDisp );
        }
        return displayByte;
    }

    // page 0 and pages 1-6 both read from the same shared row buffer -
    // tfiRender() composes it once per page (tfiComposeHudRow() for page 0,
    // tfiComposeActerRow() for 1-6) instead of recomputing per column here
    // (upstream never draws characters on page 0/7 either way).
    displayByte = displayByte | tfiSpriteRowBuffer[ x ];
    return displayByte;
}

// Composites page 0's own HUD (the "HP"/"EN" labels plus every acter's own
// HP-bar segment) once into the shared row buffer instead of recomputing it
// for all 128 columns - the same call-site-gating lesson as
// tfiComposeActerRow()/tfiBlitzSpriteRow(), applied to the HUD layer found
// via a direct user CPU report during this port's own first playtest.
void tfiComposeHudRow()
{
    int x;
    for( x = 0; x < TFI_SCREEN_WIDTH; x = x + 1 ) tfiSpriteRowBuffer[ x ] = 0;

    for( x = 0; x < 8; x = x + 1 )
      tfiSpriteRowBuffer[ x ] = tfiSpriteRowBuffer[ x ] | tfiSpeedBlitz( 0, 0, x, 0, 0, tfiMiniStg );

    int enLabelStart = 64 - 8 - 1;
    for( x = enLabelStart; x < enLabelStart + 8; x = x + 1 )
    {
        if( x < 0 || x >= TFI_SCREEN_WIDTH ) continue;
        tfiSpriteRowBuffer[ x ] = tfiSpriteRowBuffer[ x ] | tfiSpeedBlitz( enLabelStart, 0, x, 0, 1, tfiMiniStg );
    }

    int i;
    for( i = 0; i < TFI_NUM_ACTER; i = i + 1 )
    {
        if( tfiActer[ i ].hp <= 0 ) continue;
        int hpX, hpY;
        if( i == TFI_ME ) { hpX = 10; hpY = 3; }
        else { hpX = 64; hpY = 1 + i * 2; }
        int hpLen = tfiActer[ i ].hp * 3;
        int bit = 1 << hpY;

        int bx;
        for( bx = hpX; bx <= hpX + hpLen; bx = bx + 1 )
        {
            if( bx < 0 || bx >= TFI_SCREEN_WIDTH ) continue;
            tfiSpriteRowBuffer[ bx ] = tfiSpriteRowBuffer[ bx ] | bit;
        }
    }
}

void tfiRender()
{
    md_beginFrame();

    if( tfiNowGame == TFI_GAME_STAGE )
      tfiMakeNumData( tfiEnmCntNow, &tfiScoreDisp );

    int page, x;
    for( page = 0; page < 8; page = page + 1 )
    {
        if( tfiNowGame == TFI_GAME_STAGE )
        {
            if( page == 0 ) tfiComposeHudRow();
            else if( page != 7 ) tfiComposeActerRow( page );
        }

        for( x = 0; x < TFI_SCREEN_WIDTH; x = x + 1 )
          md_drawColumn( x, page, tfiComputeByte( x, page ) );
    }
}

// -----------------------------------------------------------------------------
// State machine - a direct, per-tick translation of upstream's own
// GAME_TEXT/GAME_STAGE switch (already frame-based upstream, with no
// blocking waits anywhere in the game logic itself to convert).
// -----------------------------------------------------------------------------

void gameTinYFi_init()
{
    InitTinyJoypad();

    tfiScoreDisp.xStart = TFI_SCORE_X_START;
    tfiScoreDisp.xEnd = TFI_SCORE_X_END;
    tfiStageDisp.xStart = TFI_STAGE_X_START;
    tfiStageDisp.xEnd = TFI_STAGE_X_END;

    tfiCnt = 0;
    tfiNowGame = TFI_GAME_TEXT;
    tfiNowStageNo = 200; // Title screen
    // upstream also reads enmCnt[nowStageNo] here (nowStageNo==200, a
    // 6-entry array) - see this file's own header comment on why that's
    // skipped rather than guarded.
    tfiEnmCntNow = 0;
    tfiEnmBossNo = 99;

    tfiPrevFire = 0;
}

void gameTinYFi_update()
{
    tfiTickSkipCounter = tfiTickSkipCounter + 1;
    if( tfiTickSkipCounter < TFI_TICK_DIVISOR )
      return;
    tfiTickSkipCounter = 0;

    if( tfiCnt >= 64 ) tfiCnt = 0; else tfiCnt = tfiCnt + 1;

    int fireNow = isFirePressed();
    int fireJustPressed = fireNow && !tfiPrevFire;

    if( tfiNowGame == TFI_GAME_TEXT )
    {
        if( fireJustPressed )
        {
            if( tfiNowStageNo == 100 || tfiNowStageNo == 101 )
              tfiNowStageNo = 200;
            else if( tfiNowStageNo == 200 )
              tfiNowStageNo = 0;
            else
            {
                tfiInitGame();
                tfiNowGame = TFI_GAME_STAGE;
            }
        }
    }
    else if( tfiNowGame == TFI_GAME_STAGE )
    {
        if( tfiActer[ TFI_ME ].cdw > 0 ) tfiActer[ TFI_ME ].cdw = tfiActer[ TFI_ME ].cdw - 1;
        if( ( tfiActer[ TFI_ME ].act != TFI_DAMAGE && tfiActer[ TFI_ME ].act != TFI_DOWN ) ||
            ( tfiActer[ TFI_ME ].act == TFI_DAMAGE && tfiActer[ TFI_ME ].cdw == 0 ) )
          tfiActer[ TFI_ME ].act = TFI_STAND;

        if( tfiActer[ TFI_ME ].act == TFI_DOWN && tfiActer[ TFI_ME ].cdw == 0 )
        {
            tfiNowStageNo = 100;
            tfiNowGame = TFI_GAME_TEXT;
        }

        tfiGravity = tfiGravity + 1;
        tfiActer[ TFI_ME ].y = tfiActer[ TFI_ME ].y + tfiGravity;

        if( tfiGravity > 0 )
        {
            int nextX = tfiActer[ TFI_ME ].x;
            tfiChkCollision( &nextX, &( tfiActer[ TFI_ME ].y ), tfiActer[ TFI_ME ].dir, TFI_JUMP, true );
            tfiActer[ TFI_ME ].x = nextX;
        }

        if( tfiActer[ TFI_ME ].y >= TFI_GROUND )
        {
            tfiGravity = 0;
            tfiActer[ TFI_ME ].y = TFI_GROUND;
            if( tfiJmpState == TFI_ACT_JUMPKICK ) tfiJmpState = 0;

            int myHitX, myHitY;
            tfiGetHitPosi( tfiActer[ TFI_ME ].x, tfiActer[ TFI_ME ].y, tfiActer[ TFI_ME ].dir, tfiActer[ TFI_ME ].act, &myHitX, &myHitY );
            int pj;
            for( pj = 0; pj < TFI_ENM_LEN; pj = pj + 1 )
            {
                if( tfiActer[ TFI_ENM + pj ].act == TFI_DAMAGE || tfiActer[ TFI_ENM + pj ].act == TFI_DOWN )
                  continue;

                int enmHitX, enmHitY;
                tfiGetHitPosi( tfiActer[ TFI_ENM + pj ].x, tfiActer[ TFI_ENM + pj ].y, tfiActer[ TFI_ENM + pj ].dir, tfiActer[ TFI_ENM + pj ].act, &enmHitX, &enmHitY );
                if( tfiHitTest( myHitX, myHitY, TFI_HIT_SIZE, enmHitX, enmHitY, TFI_HIT_SIZE ) )
                {
                    if( tfiActer[ TFI_ME ].x < tfiActer[ TFI_ENM + pj ].x )
                    {
                        if( tfiActer[ TFI_ENM + pj ].x + 1 < TFI_SCREEN_WIDTH - 16 ) tfiActer[ TFI_ENM + pj ].x = tfiActer[ TFI_ENM + pj ].x + 1;
                    }
                    else
                    {
                        if( tfiActer[ TFI_ENM + pj ].x - 1 > 4 ) tfiActer[ TFI_ENM + pj ].x = tfiActer[ TFI_ENM + pj ].x - 1;
                    }
                }
            }
        }

        if( tfiActer[ TFI_ME ].cdw == 0 && tfiActer[ TFI_ME ].act != TFI_DOWN )
        {
            if( isUpPressed() && tfiJmpState >= TFI_JUMP_COOLDOWN && tfiJmpState != TFI_ACT_JUMPKICK && tfiPncCd == 0 )
            {
                tfiGravity = TFI_JUMP_STRENGTH;
                tfiJmpState = 0;
            }
            if( ( isRightPressed() || isLeftPressed() ) && tfiPncCd == 0 )
            {
                int nextX = tfiActer[ TFI_ME ].x;
                int nextDir = tfiActer[ TFI_ME ].dir;
                int moveLen;
                if( tfiActer[ TFI_ME ].y == TFI_GROUND ) moveLen = 1; else moveLen = 2;

                if( isRightPressed() && tfiPncCd == 0 )
                {
                    if( tfiActer[ TFI_ME ].x < TFI_SCREEN_WIDTH - 16 ) nextX = tfiActer[ TFI_ME ].x + moveLen; else nextX = tfiActer[ TFI_ME ].x;
                    nextDir = 0;
                }
                else if( isLeftPressed() && tfiPncCd == 0 )
                {
                    if( tfiActer[ TFI_ME ].x > 4 ) nextX = tfiActer[ TFI_ME ].x - moveLen; else nextX = tfiActer[ TFI_ME ].x;
                    nextDir = 1;
                }

                if( tfiChkCollision( &nextX, &( tfiActer[ TFI_ME ].y ), nextDir, TFI_MOVE, false ) )
                {
                    tfiActer[ TFI_ME ].x = nextX;
                    tfiActer[ TFI_ME ].act = TFI_MOVE;
                }
                tfiActer[ TFI_ME ].dir = nextDir;
            }

            if( tfiActer[ TFI_ME ].y == TFI_GROUND )
            {
                if( tfiActer[ TFI_ME ].act != TFI_DAMAGE && tfiActer[ TFI_ME ].act != TFI_DOWN && !isRightPressed() && !isLeftPressed() )
                  tfiActer[ TFI_ME ].act = TFI_STAND;
                if( tfiJmpState < 100 ) tfiJmpState = tfiJmpState + 1; else tfiJmpState = 100;
                if( isFirePressed() && tfiPncCd == 0 )
                {
                    tfiPncCnt = tfiPncCnt + 1;
                    tfiPncCd = 14;
                    // upstream: if(pncCd%4==0) pncCd+=14; - always false in
                    // upstream too (14%4==2), see this file's own header
                    // comment. Ported literally for fidelity.
                    if( ( tfiPncCd % 4 ) == 0 ) tfiPncCd = tfiPncCd + 14;
                }
                if( tfiPncCd > 6 )
                {
                    if( ( tfiPncCnt % 4 ) == 0 ) { tfiActer[ TFI_ME ].act = TFI_UPPER; tfiPncCnt = 0; }
                    else tfiActer[ TFI_ME ].act = TFI_PUNCH;
                }
            }
            else
            {
                tfiActer[ TFI_ME ].act = TFI_JUMP;
                tfiPncCd = 0;
                tfiPncCnt = 0;
                if( tfiJmpState == TFI_ACT_JUMPKICK ) tfiActer[ TFI_ME ].act = TFI_KICK;
                if( isFirePressed() && tfiActer[ TFI_ME ].cdw == 0 )
                {
                    tfiActer[ TFI_ME ].act = TFI_KICK;
                    tfiJmpState = TFI_ACT_JUMPKICK;
                }
            }
        }

        if( tfiPncCd > 0 ) tfiPncCd = tfiPncCd - 1;

        if( tfiActer[ TFI_ME ].act == TFI_PUNCH || tfiActer[ TFI_ME ].act == TFI_UPPER || tfiActer[ TFI_ME ].act == TFI_KICK )
        {
            if( tfiMyAtkHit == 0 )
            {
                int atkX, atkY;
                tfiGetAtkPosi( tfiActer[ TFI_ME ].x, tfiActer[ TFI_ME ].y, tfiActer[ TFI_ME ].dir, tfiActer[ TFI_ME ].act, &atkX, &atkY );
                int aj;
                for( aj = 0; aj < TFI_ENM_LEN; aj = aj + 1 )
                {
                    if( tfiActer[ TFI_ENM + aj ].act == TFI_DAMAGE || tfiActer[ TFI_ENM + aj ].act == TFI_DOWN )
                      continue;

                    int enmHitX, enmHitY;
                    tfiGetHitPosi( tfiActer[ TFI_ENM + aj ].x, tfiActer[ TFI_ENM + aj ].y, tfiActer[ TFI_ENM + aj ].dir, tfiActer[ TFI_ENM + aj ].act, &enmHitX, &enmHitY );
                    if( tfiHitTest( atkX, atkY, TFI_ATK_SIZE, enmHitX, enmHitY, TFI_HIT_SIZE ) && tfiActer[ TFI_ENM + aj ].act != TFI_DAMAGE && tfiActer[ TFI_ENM + aj ].cdw == 0 )
                    {
                        tfiActer[ TFI_ENM + aj ].act = TFI_DAMAGE;
                        tfiActer[ TFI_ENM + aj ].cdw = 15;
                        tfiMyAtkHit = 1;
                        if( tfiActer[ TFI_ENM + aj ].hp - 1 <= 0 )
                        {
                            tfiActer[ TFI_ENM + aj ].hp = 0;
                            tfiActer[ TFI_ENM + aj ].act = TFI_DOWN;
                            tfiActer[ TFI_ENM + aj ].cdw = 130;
                        }
                        else
                          tfiActer[ TFI_ENM + aj ].hp = tfiActer[ TFI_ENM + aj ].hp - 1;

                        if( tfiActer[ TFI_ME ].act == TFI_UPPER )
                        {
                            if( tfiActer[ TFI_ME ].dir == 0 )
                            {
                                if( tfiActer[ TFI_ENM + aj ].x < TFI_SCREEN_WIDTH - 16 ) tfiActer[ TFI_ENM + aj ].x = tfiActer[ TFI_ENM + aj ].x + 5;
                            }
                            else
                            {
                                if( tfiActer[ TFI_ENM + aj ].x > 4 ) tfiActer[ TFI_ENM + aj ].x = tfiActer[ TFI_ENM + aj ].x - 5;
                            }
                        }
                        if( tfiActer[ TFI_ME ].act == TFI_PUNCH ) Sound( 190, 50 );
                        else Sound( 200, 70 );
                    }
                }
            }
        }
        else
          tfiMyAtkHit = 0;

        if( tfiActer[ TFI_ME ].act != TFI_DAMAGE && tfiActer[ TFI_ME ].act != TFI_DOWN && tfiActer[ TFI_ME ].cdw == 0 )
        {
            int myHitX2, myHitY2;
            tfiGetHitPosi( tfiActer[ TFI_ME ].x, tfiActer[ TFI_ME ].y, tfiActer[ TFI_ME ].dir, tfiActer[ TFI_ME ].act, &myHitX2, &myHitY2 );
            int dj;
            for( dj = 0; dj < TFI_ENM_LEN; dj = dj + 1 )
            {
                if( tfiActer[ TFI_ENM + dj ].act == TFI_PUNCH || tfiActer[ TFI_ENM + dj ].act == TFI_UPPER )
                {
                    int enmAtkX, enmAtkY;
                    tfiGetAtkPosi( tfiActer[ TFI_ENM + dj ].x, tfiActer[ TFI_ENM + dj ].y, tfiActer[ TFI_ENM + dj ].dir, tfiActer[ TFI_ENM + dj ].act, &enmAtkX, &enmAtkY );
                    if( tfiHitTest( enmAtkX, enmAtkY, TFI_ATK_SIZE, myHitX2, myHitY2, TFI_HIT_SIZE ) )
                    {
                        tfiActer[ TFI_ME ].act = TFI_DAMAGE;
                        if( tfiActer[ TFI_ME ].hp - 1 <= 0 )
                        {
                            tfiActer[ TFI_ME ].hp = 0;
                            tfiActer[ TFI_ME ].cdw = 100;
                            tfiActer[ TFI_ME ].act = TFI_DOWN;
                        }
                        else
                        {
                            tfiActer[ TFI_ME ].hp = tfiActer[ TFI_ME ].hp - 1;
                            tfiActer[ TFI_ME ].cdw = 10;
                            if( tfiActer[ TFI_ENM + dj ].dir == 0 )
                            {
                                if( tfiActer[ TFI_ME ].x < TFI_SCREEN_WIDTH - 16 ) tfiActer[ TFI_ME ].x = tfiActer[ TFI_ME ].x + 3;
                            }
                            else
                            {
                                if( tfiActer[ TFI_ME ].x > 4 ) tfiActer[ TFI_ME ].x = tfiActer[ TFI_ME ].x - 3;
                            }
                        }
                        Sound( 180, 50 );
                        if( tfiActer[ TFI_ENM + dj ].act != TFI_UPPER ) tfiActer[ TFI_ENM + dj ].act = TFI_STAND;
                        tfiActer[ TFI_ENM + dj ].cdw = 10;
                        break;
                    }
                }
            }
        }

        if( tfiActer[ TFI_ME ].act == TFI_DAMAGE && tfiActer[ TFI_ME ].cdw == 0 ) tfiActer[ TFI_ME ].act = TFI_STAND;
        if( tfiActer[ TFI_ME ].cdw > 0 ) tfiActer[ TFI_ME ].cdw = tfiActer[ TFI_ME ].cdw - 1;

        int i;
        for( i = 0; i < TFI_ENM_LEN; i = i + 1 )
        {
            if( tfiActer[ TFI_ENM + i ].cdw > 0 ) tfiActer[ TFI_ENM + i ].cdw = tfiActer[ TFI_ENM + i ].cdw - 1;
            if( tfiActer[ TFI_ENM + i ].y < TFI_GROUND )
            {
                tfiActer[ TFI_ENM + i ].y = tfiActer[ TFI_ENM + i ].y + 4;
                if( tfiActer[ TFI_ENM + i ].y >= TFI_GROUND ) { tfiActer[ TFI_ENM + i ].y = TFI_GROUND; tfiActer[ TFI_ENM + i ].act = TFI_STAND; }
            }

            if( tfiActer[ TFI_ENM + i ].act == TFI_DOWN && tfiActer[ TFI_ENM + i ].cdw < 100 )
            {
                tfiActer[ TFI_ENM + i ].x = 200;
            }
            else if( tfiActer[ TFI_ENM + i ].act == TFI_DAMAGE && tfiActer[ TFI_ENM + i ].cdw == 0 )
            {
                tfiActer[ TFI_ENM + i ].act = TFI_STAND;
            }
            else if( tfiActer[ TFI_ENM + i ].act == TFI_PUNCH && tfiActer[ TFI_ENM + i ].cdw == 0 )
            {
                tfiActer[ TFI_ENM + i ].act = TFI_STAND;
            }
            else if( tfiActer[ TFI_ENM + i ].act == TFI_UPPER && tfiActer[ TFI_ENM + i ].cdw == 0 )
            {
                tfiActer[ TFI_ENM + i ].act = TFI_STAND;
                tfiActer[ TFI_ENM + i ].cdw = 20;
            }
            else
            {
                if( tfiActer[ TFI_ENM + i ].x < tfiActer[ TFI_ME ].x ) tfiActer[ TFI_ENM + i ].dir = 0;
                else if( tfiActer[ TFI_ENM + i ].x > tfiActer[ TFI_ME ].x ) tfiActer[ TFI_ENM + i ].dir = 1;

                if( tfiActer[ TFI_ENM + i ].act == TFI_STAND && tfiActer[ TFI_ENM + i ].cdw == 0 )
                {
                    int dx = tfiActer[ TFI_ME ].x - tfiActer[ TFI_ENM + i ].x;
                    int dist;
                    if( dx > 0 ) dist = dx; else dist = -dx;

                    if( dist < 15 )
                    {
                        if( arand( 100 ) < 3 )
                        {
                            tfiActer[ TFI_ENM + i ].act = TFI_PUNCH;
                            tfiActer[ TFI_ENM + i ].cdw = 10;
                        }
                    }
                    else
                    {
                        if( arand( 100 ) < 2 )
                        {
                            if( dx > 0 ) { tfiActer[ TFI_ENM + i ].dir = 0; if( tfiActer[ TFI_ENM + i ].x < TFI_SCREEN_WIDTH - 16 ) tfiActer[ TFI_ENM + i ].x = tfiActer[ TFI_ENM + i ].x + 2; }
                            if( dx < 0 ) { tfiActer[ TFI_ENM + i ].dir = 1; if( tfiActer[ TFI_ENM + i ].x > 4 ) tfiActer[ TFI_ENM + i ].x = tfiActer[ TFI_ENM + i ].x - 2; }

                            if( ( TFI_ENM + i ) == tfiEnmBossNo && dist < 17 )
                            {
                                if( dx > 0 ) { tfiActer[ TFI_ENM + i ].dir = 0; if( tfiActer[ TFI_ENM + i ].x < TFI_SCREEN_WIDTH - 16 ) tfiActer[ TFI_ENM + i ].x = tfiActer[ TFI_ENM + i ].x + 2; }
                                if( dx < 0 ) { tfiActer[ TFI_ENM + i ].dir = 1; if( tfiActer[ TFI_ENM + i ].x > 4 ) tfiActer[ TFI_ENM + i ].x = tfiActer[ TFI_ENM + i ].x - 2; }
                                tfiActer[ TFI_ENM + i ].act = TFI_UPPER;
                            }
                            tfiActer[ TFI_ENM + i ].cdw = 10;
                        }
                    }
                }
            }
        }

        if( tfiEnmCntNow == 0 )
        {
            bool isNextStageGo = true;
            int sj;
            for( sj = 0; sj < TFI_ENM_LEN; sj = sj + 1 )
            {
                if( tfiActer[ TFI_ENM + sj ].x >= 0 && tfiActer[ TFI_ENM + sj ].x < TFI_SCREEN_WIDTH && tfiActer[ TFI_ENM + sj ].y >= 0 && tfiActer[ TFI_ENM + sj ].y < TFI_SCREEN_HEIGHT )
                  isNextStageGo = false;
            }
            if( isNextStageGo )
            {
                if( tfiNowStageNo == TFI_STAGE_CNT - 1 )
                  tfiNowStageNo = 101;
                else
                {
                    tfiNowStageNo = tfiNowStageNo + 1;
                    if( tfiNowStageNo >= TFI_STAGE_CNT ) tfiNowStageNo = 0;
                }
                tfiNowGame = TFI_GAME_TEXT;
            }
        }
        else
        {
            int actyEnm = 0;
            int rj;
            for( rj = 0; rj < TFI_ENM_LEN; rj = rj + 1 ) { if( tfiActer[ TFI_ENM + rj ].act != TFI_DOWN ) actyEnm = actyEnm + 1; }
            if( actyEnm < TFI_ENM_LEN )
            {
                for( rj = 0; rj < TFI_ENM_LEN; rj = rj + 1 )
                {
                    if( tfiActer[ TFI_ENM + rj ].act == TFI_DOWN && tfiActer[ TFI_ENM + rj ].cdw == 0 )
                    {
                        tfiActer[ TFI_ENM + rj ].act = TFI_JUMP;
                        tfiActer[ TFI_ENM + rj ].x = 16 * ( arand( 5 ) + 1 );
                        tfiActer[ TFI_ENM + rj ].hp = 8;
                        tfiActer[ TFI_ENM + rj ].y = -16;
                        // upstream checks acter[i].x!=200 here (i, not
                        // ENM+i) - see this file's own header comment on
                        // why this is an always-true no-op, ported
                        // literally rather than "corrected".
                        if( tfiEnmCntNow == 1 && tfiActer[ rj ].x != 200 && ( tfiNowStageNo == 2 || tfiNowStageNo == 5 ) )
                        {
                            tfiEnmBossNo = TFI_ENM + rj;
                            tfiActer[ TFI_ENM + rj ].hp = 16;
                        }
                        if( tfiEnmCntNow > 0 ) tfiEnmCntNow = tfiEnmCntNow - 1;
                        break;
                    }
                }
            }
        }
    }

    tfiPrevFire = fireNow;

    tfiRender();
}

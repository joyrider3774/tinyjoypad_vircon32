// =============================================================================
// Tiny Invaders v4.2 - ported from Lorandil's Tiny-invaders-v4.2
// (https://github.com/Lorandil/Tiny-invaders-v4.2), GPLv3, original by
// Daniel C (Electro L.I.B) 2018-2020, enhancements by Sven B ("sbr") 2021.
//
// Ported onto tinyJoypadShim.h (which reproduces the tinyJoypadUtils.h/
// FastTinyDriver display/input/sound API on top of machineDependent.h's
// md_* primitives). Every file-scope name is prefixed tinv*/TINV_* to avoid
// collisions with other games sharing this cartridge's single translation
// unit (VIRCON32_C_DIALECT.md section 17.1).
//
// Structural changes from upstream, beyond dialect fixes (array-declaration
// syntax, no bitfields/scoped enums, no ternary, no comma operator, no
// goto - see below):
//  - The original loop() is a single, never-returning function built from
//    nested `while(1)` loops linked by `goto` (NEWGAME/NEWLEVEL/BYPASS2/
//    Bypass/RestartLevel labels) plus several _delay_ms() calls - it owns
//    the whole ATtiny85 CPU for the entire play session. That whole shape
//    is rewritten here as an explicit state machine (enum TinvState) -
//    each original `goto` becomes a state transition, and each
//    `_delay_ms(n)` becomes a frame-counted wait (tinvWaitFrames) that
//    lets the rest of the cartridge (menu, audio) keep running instead of
//    freezing. `gameTinyInvaders_update()` itself (and every wait-state's
//    own countdown) is still called once per real Vircon32 frame; only
//    the PLAYING state's own movement/collision step
//    (`tinvUpdatePlaying()`) is throttled to every other real frame by a
//    later frame-pacing session's `TINV_MOVE_DIVISOR` (see CLAUDE.md and
//    that constant's own comment) - upstream had no real timing model of
//    its own to match, so this was this port's own choice, not a fidelity
//    fix.
//  - Direct AVR analogRead(LEFT_RIGHT_BUTTON)/digitalRead(FIRE_BUTTON)
//    calls (bypassing even the original's own tinyJoypadUtils
//    abstraction, presumably for a few bytes of flash) are replaced with
//    the shim's isLeftPressed()/isRightPressed()/isFirePressed() etc,
//    since Vircon32 has real digital gamepad buttons, not an analog
//    voltage-ladder joystick.
//  - High-score persistence is restored (see the project-wide "Real
//    persistent high-score saving" section in CLAUDE.md), but as a plain
//    2-byte score at upstream's own addr 128, not upstream's full 6-byte
//    CRC-checked HISCORE struct - the 3-letter name-entry screen that
//    struct's other fields existed for stays dropped ("NEW HIGH SCORE"
//    still just shows a brief banner rather than letting the player enter
//    initials), so there's nothing left for those fields to hold.
// =============================================================================

// -----------------------------------------------------------------------------
//   SPACE (per-game mutable playfield state)
// -----------------------------------------------------------------------------

struct TinvSpace
{
    int UFOxPos;
    int oneFrame;
    int[2] MonsterShoot;
    int[5][6] MonsterGrid;
    int[6] Shield;
    int ScrBackV;
    int MyShootBall;
    int MyShootBallxpos;
    int MyShootBallFrame;
    bool anim;
    int frame;
    int PositionDansGrilleMonsterX;
    int PositionDansGrilleMonsterY;
    int MonsterFloorMax;
    int MonsterOffsetGauche;
    int MonsterOffsetDroite;
    int MonsterGroupeXpos;
    int MonsterGroupeYpos;
    int DecalageY8;
    int frameMax;
    int Direction;
};

TinvSpace tinvSpaceInstance;
TinvSpace* tinvSpace = &tinvSpaceInstance;

#define TINV_GAME_SCREEN  0
#define TINV_INTRO_SCREEN 1
#define TINV_BLANK_SCREEN 2

#define TINV_MAXLEVELSHIELDED 3
#define TINV_SHOOTS 2

// -----------------------------------------------------------------------------
//   Sprite / level / font data (see spritebank.h / smallFont.h upstream)
// -----------------------------------------------------------------------------

int[240] tinvMonstersLevels =
{
0,0,0,0,0,0,2,2,2,2,2,2,4,4,4,4,4,4,4,4,4,4,4,4,
4,4,4,4,4,4,4,2,0,0,2,4,4,2,0,0,2,4,4,4,4,4,4,4,
-1,0,0,0,0,-1,2,2,2,2,2,2,4,4,4,4,4,4,-1,4,4,4,4,-1,
0,-1,0,0,-1,0,2,-1,2,2,-1,2,4,-1,4,4,-1,4,4,-1,4,4,-1,4,
-1,-1,2,2,-1,-1, 0,2,2,2,2,0, 2,4,2,2,4,2, 2,-1,-1,-1,-1,2,
4,4,4,4,4,4, 2,2,2,2,2,2, 0,0,0,0,0,0, 0,0,0,0,0,0,
-1,0,0,0,0,-1,2,-1,-1,-1,-1,2,4,-1,-1,-1,-1,4,-1,4,4,4,4,-1,
4,-1,4,-1,4,-1,-1,4,-1,4,-1,4,4,-1,4,-1,4,-1,-1,4,-1,4,-1,4,
-1,-1,0,0,-1,-1,2,2,4,4,2,2,2,2,4,4,2,2,-1,-1,0,0,-1,-1,
0,0,4,4,2,2,0,0,4,4,2,2,0,0,4,4,2,2,0,0,4,4,2,2
};

int[15] tinvLIVE = { 0x80, 0xC0, 0x80, 0x00, 0x00, 0x80, 0xC0, 0x80, 0x00, 0x00, 0x80, 0xC0, 0x80, 0x00, 0x00 };
int[2] tinvSHOOT = { 0xF0, 0x0F };

int[168] tinvMonsters =
{
0x00, 0x00, 0x00, 0x58, 0xBC, 0x16, 0x3F, 0x3F, 0x16, 0xBC, 0x58, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x98, 0x5C, 0xB6, 0x5F, 0x5F, 0xB6, 0x5C, 0x98, 0x00, 0x00, 0x00,
0x00, 0x70, 0x18, 0x7D, 0xB6, 0xBC, 0x3C, 0x3C, 0xBC, 0xB6, 0x7D, 0x18, 0x70, 0x00,
0x00, 0x1E, 0xB8, 0x7D, 0x36, 0x3C, 0x3C, 0x3C, 0x3C, 0x36, 0x7D, 0xB8, 0x1E, 0x00,
0x00, 0x9C, 0x9E, 0x5E, 0x76, 0x37, 0x5F, 0x5F, 0x37, 0x76, 0x5E, 0x9E, 0x9C, 0x00,
0x00, 0x1C, 0x5E, 0xFE, 0xB6, 0x37, 0x5F, 0x5F, 0x37, 0xB6, 0xFE, 0x5E, 0x1C, 0x00,
0x00, 0x40, 0x60, 0xF0, 0x50, 0x78, 0x58, 0x58, 0x78, 0x50, 0xF0, 0x60, 0x40, 0x00,
0x00, 0x40, 0x60, 0xD0, 0x70, 0x58, 0x78, 0x78, 0x58, 0x70, 0xD0, 0x60, 0x40, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x18, 0x18, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x81, 0x00, 0x24, 0x18, 0x18, 0x24, 0x00, 0x81, 0x00, 0x00, 0x00,
0x00, 0x00, 0x24, 0x81, 0x18, 0x24, 0x5A, 0x5A, 0x24, 0x18, 0x81, 0x24, 0x00, 0x00,
0x42, 0x00, 0x24, 0x81, 0x4A, 0x3C, 0xA4, 0x25, 0x3C, 0x4A, 0x81, 0x24, 0x00, 0x42
};

int[26] tinvVesso =
{
    0x70, 0x78, 0x78, 0x78, 0x78, 0x7E, 0x7F, 0x7E, 0x78, 0x78, 0x78, 0x78, 0x70, 0x54, 0xD1, 0xB4,
    0x78, 0x3C, 0xF0, 0x34, 0xF8, 0x80, 0x78, 0xEA, 0xE0, 0x74
};

// RLE-compressed background/intro bitmaps (see spritebank.h - "extended"
// RLE encoding, RLEcompressionDefs.h's RLE2_* masks) - 8 chunks of 128
// uncompressed bytes each, decompressed a chunk (one OLED "page") at a
// time by tinvRLEdecompressExtended() during Tiny_Flip().
int[427] tinvBackCompressed =
{
0xC2, 0x01, 0xFD, 0xD1, 0x01, 0xFD, 0xCC, 0x0C, 0x3F, 0x5F, 0x1F, 0xCF, 0x2F, 0xC7, 0xBF, 0xE7,
0x7F, 0xEF, 0xFF, 0xBF, 0xCF, 0x01, 0xFB, 0xCE, 0x01, 0xF7, 0xCF, 0x01, 0x7F, 0xCD, 0x01, 0x7F,
0xD0, 0x01, 0xF7, 0xC5, 0xCE, 0x01, 0xFE, 0xCA, 0x01, 0xFD, 0xC6, 0x11, 0xC0, 0x14, 0xE9, 0xF6,
0xBD, 0xEF, 0xFD, 0xFF, 0xFD, 0xFF, 0xBF, 0xFF, 0xD7, 0x7F, 0xDF, 0xBF, 0xF6, 0xD0, 0x01, 0x7F,
0xE2, 0x01, 0xBF, 0xCC, 0x01, 0xDF, 0xC8, 0x01, 0xBF, 0xC5, 0xCC, 0x01, 0xEF, 0xD5, 0x0A, 0xFE,
0xFF, 0xFD, 0xF7, 0xFF, 0xF7, 0xFF, 0xF7, 0xFF, 0xFD, 0xC2, 0x01, 0xFD, 0xDB, 0x1C, 0xBF, 0x7F,
0x1F, 0xBF, 0x0F, 0x47, 0x8F, 0x23, 0x47, 0x93, 0x43, 0xB5, 0x4B, 0xA3, 0xDB, 0xA5, 0xDB, 0xB3,
0xE7, 0x5B, 0xF7, 0xAF, 0xF7, 0x6F, 0xFF, 0xDF, 0xFF, 0x5F, 0xCA, 0x01, 0xBF, 0xCF, 0xD5, 0x01,
0xDF, 0xCC, 0x01, 0xEF, 0xD5, 0x01, 0xBF, 0xCB, 0x2A, 0x9F, 0x0F, 0x43, 0x15, 0x02, 0x50, 0x04,
0xA2, 0x18, 0xE2, 0x0C, 0xF2, 0x0C, 0xF3, 0xAC, 0xDA, 0xF5, 0xBA, 0xED, 0x7A, 0xEF, 0xDA, 0xBF,
0xF6, 0xFF, 0xEF, 0xFD, 0xF7, 0x7F, 0xFF, 0xED, 0xFF, 0x7F, 0xFB, 0xBF, 0xFE, 0xDF, 0x7D, 0xF7,
0xDF, 0xFF, 0xBF, 0xC3, 0x01, 0xBF, 0xCE, 0x02, 0xFF, 0xFE, 0xD1, 0x01, 0xBF, 0xC4, 0x01, 0xAF,
0xC4, 0x01, 0xBF, 0xE4, 0x24, 0x11, 0x24, 0x08, 0x52, 0x00, 0x55, 0x28, 0xC3, 0x1C, 0xD1, 0xB6,
0x4D, 0xFA, 0x57, 0xFD, 0xD7, 0x6E, 0xFB, 0xEE, 0x77, 0xDF, 0xF5, 0xDF, 0xFB, 0xFD, 0xBF, 0xFA,
0xFF, 0xFD, 0xFF, 0xFD, 0x7F, 0xFE, 0xBF, 0xFF, 0xDF, 0xC3, 0x01, 0xBF, 0xC3, 0x02, 0xFE, 0xAB,
0xCA, 0x01, 0xFD, 0xC6, 0xCE, 0x01, 0xFE, 0xC2, 0x0F, 0xF7, 0xFF, 0xF7, 0xFF, 0xB6, 0xD5, 0xF7,
0x80, 0xF7, 0xD5, 0xB6, 0xFF, 0xF7, 0xFF, 0xF7, 0xE1, 0x19, 0xF6, 0x48, 0xA1, 0x14, 0x49, 0xA6,
0x59, 0xA5, 0xBA, 0x6B, 0xDE, 0x75, 0xFF, 0xDB, 0x7F, 0xED, 0xFF, 0xF7, 0xFF, 0xFD, 0x7F, 0xFF,
0xFD, 0xFF, 0xFE, 0xC4, 0x11, 0xDF, 0xFB, 0xFF, 0xBF, 0xF7, 0xDF, 0xFB, 0xBF, 0x6D, 0xDF, 0xFF,
0xB7, 0x7F, 0xFF, 0x57, 0xBE, 0xD5, 0xC6, 0x01, 0x7F, 0xC8, 0x01, 0xBF, 0xC1, 0xC7, 0x01, 0xFD,
0xCB, 0x01, 0xFE, 0xC4, 0x02, 0xFA, 0xEF, 0xC3, 0x01, 0xFE, 0xD3, 0x01, 0xFD, 0xD1, 0x10, 0xFE,
0xE9, 0xD4, 0x6B, 0x96, 0x79, 0xD7, 0xBD, 0xD7, 0xFD, 0xEF, 0xBD, 0xF7, 0xDF, 0xFF, 0xEE, 0xC4,
0x01, 0xEF, 0xC3, 0x12, 0xFB, 0xEE, 0xFF, 0x77, 0xFD, 0xFF, 0xDF, 0xF6, 0xEF, 0x7D, 0xEB, 0xFF,
0x56, 0xBD, 0xCB, 0xF5, 0xF6, 0xFD, 0xD3, 0xC6, 0x01, 0xEF, 0xCF, 0x01, 0xFB, 0xDA, 0x01, 0x7F,
0xCA, 0x01, 0xEF, 0xCB, 0x1F, 0xFD, 0xFE, 0xFF, 0xFA, 0xF7, 0xFD, 0xEF, 0xF7, 0xDE, 0xEF, 0xFF,
0xBF, 0xEE, 0xFF, 0xDF, 0xBF, 0xFF, 0xDF, 0xFE, 0xBF, 0xFF, 0xAF, 0xFF, 0xDF, 0xFB, 0xFF, 0xF6,
0xFF, 0xFB, 0xFF, 0xFC, 0xC7, 0x01, 0xFE, 0xC6, 0x01, 0xFB, 0xCA
};

int[578] tinvIntroCompressed =
{
  0xA0, 0x02, 0x80, 0xC0, 0x43, 0xE0, 0x4A, 0xF0, 0x01, 0xE0, 0x42, 0xC0, 0x01, 0xE0, 0x49, 0xF0,
  0x42, 0xE0, 0x4C, 0xF0, 0x42, 0xE0, 0x49, 0xF0, 0x01, 0xE0, 0x42, 0xC0, 0x01, 0xE0, 0x45, 0xF0,
  0x42, 0xE0, 0x02, 0xC0, 0x80, 0x9E, 0x9C, 0x03, 0x78, 0x9C, 0x1E, 0x4C, 0x1F, 0x01, 0x3F, 0xC7,
  0x45, 0x1F, 0xDC, 0x46, 0x3F, 0xC7, 0x42, 0x3F, 0x05, 0x3E, 0x3C, 0x38, 0xB0, 0x60, 0x98, 0x9D,
  0x03, 0x01, 0x02, 0x06, 0x43, 0x04, 0x01, 0xFC, 0x85, 0x44, 0xFC, 0xC3, 0x03, 0x7F, 0x1F, 0xFF,
  0x45, 0x04, 0xC7, 0x45, 0x07, 0x01, 0x0F, 0x46, 0x07, 0x42, 0x0F, 0x02, 0x1F, 0x7F, 0xC6, 0x01,
  0xFC, 0x84, 0x0D, 0x01, 0xFF, 0x7F, 0x3F, 0x1F, 0x07, 0x80, 0x40, 0x20, 0x18, 0x0E, 0x03, 0x01,
  0x99, 0xA3, 0x03, 0x03, 0x7C, 0x80, 0x83, 0x02, 0x03, 0x7F, 0xC3, 0x02, 0x1F, 0x03, 0x82, 0x02,
  0x07, 0xF8, 0x84, 0x01, 0x07, 0xC2, 0x01, 0x0F, 0xC2, 0x01, 0x7F, 0x84, 0x04, 0x80, 0x7E, 0x07,
  0x7F, 0xC3, 0x01, 0x7E, 0x83, 0x01, 0x80, 0xC7, 0x01, 0x7E, 0x84, 0x05, 0xC0, 0x30, 0x18, 0x06,
  0x03, 0xA0, 0x06, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0x47, 0xFE, 0x01, 0xFC, 0x46, 0xFE, 0x03,
  0xFC, 0xF0, 0xEE, 0x4A, 0xFE, 0x01, 0xFC, 0x42, 0xF8, 0x02, 0xFE, 0xFF, 0x45, 0xC0, 0xC2, 0x01,
  0xE1, 0x42, 0xC0, 0x43, 0xF8, 0x01, 0xFF, 0x45, 0xC0, 0xC2, 0x01, 0xF0, 0xC2, 0x45, 0xC0, 0x01,
  0xFF, 0x43, 0xF8, 0xC3, 0x44, 0xC0, 0x02, 0xFF, 0xF8, 0xC2, 0x03, 0xDF, 0xC7, 0xC1, 0x43, 0xC0,
  0x03, 0xF8, 0xF6, 0xE1, 0x48, 0xF8, 0x46, 0xFE, 0x03, 0xFC, 0xF0, 0xE0, 0x42, 0xF0, 0x01, 0xF8,
  0x42, 0xFC, 0x48, 0xFE, 0x01, 0xF8, 0x42, 0xE0, 0x01, 0xC0, 0x83, 0x09, 0x07, 0x0F, 0x79, 0xE1,
  0xC1, 0x01, 0x03, 0x0F, 0x3F, 0xC2, 0x07, 0xF9, 0xC3, 0x83, 0x07, 0x0F, 0x1F, 0x7F, 0xC2, 0x02,
  0xF3, 0x83, 0x43, 0x03, 0x01, 0x3F, 0xC2, 0x02, 0xFB, 0xC3, 0x42, 0x03, 0x03, 0x07, 0x0F, 0x7F,
  0xC3, 0x01, 0xE1, 0x44, 0x01, 0x01, 0x03, 0xC5, 0x44, 0x03, 0x44, 0x01, 0x01, 0x0F, 0xC5, 0x45,
  0x03, 0x44, 0xF3, 0x42, 0x03, 0x01, 0x07, 0x42, 0x0F, 0xC4, 0x44, 0x03, 0x01, 0x83, 0x47, 0xF3,
  0xC3, 0x02, 0x1F, 0x03, 0x42, 0x01, 0x01, 0x81, 0x42, 0xF1, 0x42, 0xF3, 0x44, 0x03, 0x01, 0x8F,
  0xC3, 0x02, 0x7F, 0x1F, 0x42, 0x0F, 0x05, 0x87, 0xE3, 0xF3, 0xF1, 0x31, 0x43, 0x01, 0x03, 0x81,
  0xE7, 0x7E, 0x84, 0x10, 0x03, 0x07, 0x1C, 0x38, 0xE0, 0x80, 0x03, 0x07, 0x1F, 0x7F, 0xFE, 0xF0,
  0xE0, 0x80, 0x00, 0x01, 0x42, 0x07, 0x02, 0xCE, 0x80, 0x83, 0x02, 0x07, 0x3F, 0xC2, 0x0A, 0xFC,
  0xF0, 0xC0, 0x00, 0x03, 0x0F, 0x3F, 0xFF, 0xF8, 0x80, 0x83, 0x01, 0x3F, 0xC4, 0x84, 0x03, 0xFE,
  0xFC, 0xE0, 0x82, 0x01, 0x03, 0xC5, 0x84, 0xC3, 0x01, 0x0F, 0x84, 0x01, 0xF8, 0xC2, 0x01, 0x0F,
  0x84, 0x01, 0xC0, 0x45, 0xCF, 0xC3, 0x03, 0x3F, 0x07, 0x01, 0x82, 0x07, 0x80, 0x9E, 0x9F, 0x1F,
  0x0F, 0x61, 0xE0, 0x42, 0xF0, 0x01, 0xFC, 0xC2, 0x02, 0xEF, 0x83, 0x43, 0x80, 0x07, 0x98, 0x1E,
  0x1F, 0x1E, 0x1F, 0xF6, 0xE6, 0x43, 0x06, 0x03, 0x07, 0x01, 0x00, 0x06, 0x70, 0x80, 0x70, 0x00,
  0x80, 0x00, 0x42, 0xA8, 0x1F, 0xF9, 0x03, 0x0F, 0x1C, 0x70, 0xE0, 0x81, 0x83, 0x8F, 0xBF, 0xFE,
  0xF8, 0xE0, 0x80, 0x81, 0x8F, 0x9F, 0xFE, 0xDC, 0x30, 0xE0, 0xC0, 0x83, 0x9F, 0xFF, 0xCF, 0x1F,
  0x7C, 0xE0, 0xC0, 0x81, 0x42, 0x83, 0x44, 0x80, 0xC2, 0x02, 0x0F, 0xFF, 0x44, 0x80, 0x01, 0xF3,
  0x42, 0x13, 0x01, 0xF0, 0x43, 0x80, 0x01, 0x9F, 0xC3, 0x43, 0x80, 0x01, 0xBC, 0x43, 0xBF, 0x42,
  0x80, 0x03, 0xC0, 0xE0, 0x7F, 0xC2, 0x01, 0x87, 0x44, 0x80, 0x01, 0xBE, 0x44, 0xBF, 0xC3, 0x02,
  0x9F, 0x83, 0x42, 0x80, 0x02, 0xF0, 0xFE, 0xC2, 0x08, 0x8F, 0x81, 0x80, 0xE0, 0xF0, 0x3E, 0xFF,
  0xDF, 0x43, 0x87, 0x01, 0x97, 0x42, 0x9F, 0x09, 0xDF, 0xC7, 0xE1, 0x60, 0x38, 0x1C, 0x0E, 0x03,
  0x01, 0x86
};

// 3x5 font (padded to a byte per column, 4 columns/char), '0'-'9', '!?'
// then 'A'-'Z' with some alien/UFO glyphs interleaved in the unused
// punctuation slots between '9' and 'A' - identical layout to upstream's
// characterFont3x5[], a functional glyph table.
int[200] tinvCharacterFont3x5 =
{
  0x3C,0x22,0x1E,0x00,  0x00,0x3E,0x00,0x00,  0x3A,0x2A,0x2E,0x00,  0x22,0x2A,0x3E,0x00,
  0x0E,0x08,0x3C,0x00,  0x2E,0x2A,0x3A,0x00,  0x3E,0x28,0x38,0x00,  0x02,0x32,0x0E,0x00,
  0x3E,0x2A,0x3E,0x00,  0x0E,0x0A,0x3E,0x00,
  0x1E,0x16,0x16,0x1E,  0x14,0x3C,0x18,0x10,
  0x70,0x18,0x7D,0xB6,  0xBC,0x3C,0x3C,0xBC,  0xB6,0x7D,0x18,0x70,
  0x02,0x2A,0x0E,0x00,  0x00,0x2E,0x00,0x00,
  0x3C,0x0A,0x3C,0x00,  0x3E,0x2A,0x14,0x00,  0x1C,0x22,0x22,0x00,  0x3E,0x22,0x1C,0x00,
  0x3E,0x2A,0x22,0x00,  0x3E,0x0A,0x02,0x00,  0x1C,0x22,0x32,0x00,  0x3E,0x08,0x3E,0x00,
  0x00,0x3E,0x00,0x00,  0x30,0x20,0x1E,0x00,  0x3E,0x08,0x36,0x00,  0x3E,0x20,0x20,0x00,
  0x3E,0x0C,0x3E,0x00,  0x3E,0x1C,0x3E,0x00,  0x1C,0x22,0x1C,0x00,  0x3E,0x0A,0x04,0x00,
  0x1C,0x22,0x3C,0x00,  0x3E,0x0A,0x34,0x00,  0x24,0x2A,0x12,0x00,  0x02,0x3E,0x02,0x00,
  0x3E,0x20,0x3E,0x00,  0x1E,0x30,0x1E,0x00,  0x3E,0x18,0x3E,0x00,  0x36,0x08,0x36,0x00,
  0x0E,0x38,0x0E,0x00,  0x32,0x2A,0x26,0x00,
  0x00,0x00,0x58,0xBC,  0x16,0x3F,0x3F,0x16,  0xBC,0x58,0x00,0x00,
  0x9C,0x9E,0x5E,0x76,  0x37,0x5F,0x5F,0x37,  0x76,0x5E,0x9E,0x9C,
  0x10,0x18,0x3C,0x14
};

int[16] tinvNibbleZoom =
{
  0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F,
  0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
};

// -----------------------------------------------------------------------------
//   RLE (extended) decompression - see RLEdecompression.cpp/RLEcompressionDefs.h.
//   Flat memory means there's no PROGMEM-vs-RAM distinction to keep two
//   variants for, so this one function covers both of upstream's
//   pgm_RLEdecompressExtended()/pgm_RLEdecompressExt8().
// -----------------------------------------------------------------------------

#define TINV_RLE2_UNCOMPRESSED_DATA    0x00
#define TINV_RLE2_COMPRESSED_0xFF_DATA 0xC0
#define TINV_RLE2_COMPRESSED_0x00_DATA 0x80
#define TINV_RLE2_COMPRESSION_MASK     0xC0

int tinvRLEdecompressExtended( int* compressedData, int compressedPos, int* uncompressedData, int uncompressedByteCount )
{
    int outPos = 0;

    while( uncompressedByteCount != 0 )
    {
        int count = compressedData[ compressedPos ];
        compressedPos++;

        int encoding = count & TINV_RLE2_COMPRESSION_MASK;

        if( encoding == TINV_RLE2_UNCOMPRESSED_DATA )
        {
            for( int n = 0; n < count; n++ )
            {
                uncompressedData[ outPos ] = compressedData[ compressedPos ];
                outPos++;
                compressedPos++;
            }
        }
        else
        {
            int value = 0x00;
            if( encoding == TINV_RLE2_COMPRESSED_0xFF_DATA )
            {
                value = 0xFF;
            }
            else if( encoding != TINV_RLE2_COMPRESSED_0x00_DATA )
            {
                value = compressedData[ compressedPos ];
                compressedPos++;
            }

            count -= encoding;

            for( int n = 0; n < count; n++ )
            {
                uncompressedData[ outPos ] = value;
                outPos++;
            }
        }

        uncompressedByteCount -= count;
    }

    return compressedPos;
}

// -----------------------------------------------------------------------------
//   Score / on-screen text (see displayscore.h/.cpp) - simplified: EEPROM
//   persistence is restored (see the file header comment) but as a plain
//   2-byte value, not upstream's own CRC/struct machinery - see
//   gameTinyInvaders_init()/tinvBeginGameOverDisplay() for the load/save.
// -----------------------------------------------------------------------------

int tinvScore = 0;
int tinvHighScore = 0;
bool tinvNewHighScore = false;
int[64] tinvTextBuffer;

void tinvResetScore() { tinvScore = 0; }
void tinvAddScore( int points )
{
    tinvScore += points;
    // Sync immediately at the point score changes, rather than relying
    // solely on tinvUpdateHighScore()'s once-per-frame poll (called early
    // in tinvTinyFlip(), before this function's caller - tinvMyShoot() via
    // the render loop - ever runs each frame). That poll gives every score
    // change a full frame to be reflected, which is invisible during
    // normal play, but it means tinvHighScore can permanently lag behind
    // tinvScore by exactly one kill's worth of points if the very last
    // scoring kill of a life happens on the same frame the ship's death
    // is finalized - the frame after that stops calling tinvTinyFlip() at
    // all for ~36 frames (see tinvUpdatePlaying()'s tinvShipDead handling),
    // so the lagging poll never gets another chance to catch up before
    // tinvBeginGameOverDisplay() reads tinvHighScore for the "NEW HISCORE!"
    // screen - which is exactly the reported symptom (a stale, too-low
    // value shown there that doesn't match the visibly higher live score).
    if( tinvScore > tinvHighScore )
    {
        tinvHighScore = tinvScore;
        tinvNewHighScore = true;
    }
}
int tinvGetScore() { return tinvScore; }
bool tinvUpdateHighScore()
{
    if( tinvScore > tinvHighScore )
    {
        tinvHighScore = tinvScore;
        return true;
    }
    return false;
}

void tinvConvertValueToDigits( int value, int* digits, int digitsOffset )
{
    int[6] dividerList = { 10000, 1000, 100, 10, 1, 0 };
    int dividerIndex = 0;

    while( dividerList[ dividerIndex ] > value )
      dividerIndex++;

    if( value == 0 )
      dividerIndex = 4;

    while( true )
    {
        int digit = '0';
        while( value >= dividerList[ dividerIndex ] )
        {
            digit++;
            value -= dividerList[ dividerIndex ];
        }
        digits[ digitsOffset ] = digit;
        digitsOffset++;
        dividerIndex++;
        if( dividerList[ dividerIndex ] == 0 )
          break;
    }
}

int tinvDisplayText( int x, int y )
{
    if( y == 0 )
    {
        int value = tinvTextBuffer[ x >> 2 ];
        if( value != 0 )
          return tinvCharacterFont3x5[ ( ( value - '0' ) << 2 ) + ( x & 0x03 ) ];
    }
    return 0x00;
}

int tinvDisplayZoomedText( int x, int y )
{
    int value = tinvTextBuffer[ ( ( y >> 1 ) << 4 ) + ( x >> 3 ) ];
    if( value == 0 )
      return value;

    int reverse = value & 0x80;
    value -= reverse;
    value = tinvCharacterFont3x5[ ( ( value - '0' ) << 2 ) + ( ( x >> 1 ) & 0x03 ) ];

    if( ( y & 0x01 ) == 0 )
      value = tinvNibbleZoom[ value & 0x0F ];
    else
      value = tinvNibbleZoom[ value >> 4 ];

    if( reverse )
      value = value ^ 0xFF;

    return value;
}

void tinvClearTextBuffer()
{
    for( int i = 0; i < 64; i++ )
      tinvTextBuffer[ i ] = 0;
}

void tinvPrintText( int x, int* text, int textLength )
{
    for( int i = 0; i < textLength; i++ )
      tinvTextBuffer[ x + i ] = text[ i ];
}

// -----------------------------------------------------------------------------
//   Game text strings (functional UI labels, not narrative text - see
//   upstream's own comment: encoded this way just to skip a terminating
//   zero and remap space to 0)
// -----------------------------------------------------------------------------

int[3] tinvTxtOneUp = { '1', 'U', 'P' };
int[12] tinvTxtNewHiScore = { 'N', 'E', 'W', 0, 'H', 'I', 'S', 'C', 'O', 'R', 'E', '@' };
int[5] tinvTxtLevel = { 'L', 'E', 'V', 'E', 'L' };
int[55] tinvTxtPointValues =
{
    '^','_' ,'`', 0 , 0 ,'1','0', 0 , 0 , 0 , 0 ,
    0 , 0 , 0 , 0 , 0 ,'<','=' ,'>', 0 , 0 ,'2','0', 0 , 0 , 0 , 0 ,
    0 , 0 , 0 , 0 , 0 ,'[','\\',']', 0 , 0 ,'4','0', 0 , 0 , 0 , 0 ,
    0 , 0 , 0 , 0 , 0 ,'a',':' ,';', 0 , 0 ,'?','?'
};
int[64] tinvTxtGameOver =
{
    '[','\\',']','<','=','>','[','\\',']','^','_','`','a',':',';' , 0 ,
    0 , 0  , 0 ,'G','A','M','E', 0  ,'O','V','E','R', 0 ,'<','=' ,'>',
   '^','_' ,'`', 0 ,'1','U','P', 0  , 0 , 0 , 0 , 0 , 0 , 0 , 0  , 0 ,
    0 ,'a',':',';','[','\\',']','^','_' ,'`','<','=','>' ,'[','\\',']'
};

// -----------------------------------------------------------------------------
//   Persistent game state (globals - these were loop()-locals upstream,
//   but upstream's loop() ran forever in one call, so they persisted for
//   the whole session anyway; ours must be explicit globals since
//   gameTinyInvaders_update() returns every frame)
// -----------------------------------------------------------------------------

int tinvLive = 0;
int tinvShieldRemoved = 0;
int tinvMonsterRest = 0;
int tinvLevels = 0;
int tinvCurrentLevel = 0;
int tinvSpeedShootMonster = 0;
int tinvShipDead = 0;
int tinvShipPos = 56;
int[128] tinvChunkBuffer;
bool tinvFirstRun = true;
bool tinvNewLevelAnimation = false;
int tinvLevelShiftOffsetX = 0;
bool tinvDisplayLevelNumber = false;

int tinvDecompte = 0;
int tinvVarPot = 54;
int tinvMyShootReady = TINV_SHOOTS;
int tinvIntroN = 0;
int tinvIntroSubFrame = 0;

// -----------------------------------------------------------------------------
//   State machine (replaces the goto/while(1) main loop - see file header)
// -----------------------------------------------------------------------------

#define TINV_STATE_GAME_OVER_DISPLAY 0
#define TINV_STATE_INTRO             1
#define TINV_STATE_LEVEL_START       2
#define TINV_STATE_PLAYING           3
#define TINV_STATE_LEVEL_CLEARED     4

int tinvState = TINV_STATE_INTRO;
int tinvWaitFrames = 0;
int tinvWaitAction = 0;

// Upstream never had a real timing model (uncapped loop, see CLAUDE.md's
// frame-pacing survey) - the wait-frame counts above (tinvWaitFrames's own
// "~600ms"/"~500ms" comments elsewhere in this file) are this port's own
// invented real-60fps-tick pacing, so they stay untouched. Only the
// gameplay movement/shooting/monster-AI/collision step
// (tinvUpdatePlaying()) is gated to run every TINV_MOVE_DIVISOR real
// frames instead - the same decouple-logic-from-redraw approach already
// used for Tiny Arkanoid's paddle/ball updates - so redraw
// (tinvTinyFlip()) still happens every real frame for smooth visuals.
#define TINV_MOVE_DIVISOR 2
int tinvMoveTickCounter = 0;

#define TINV_WAIT_NONE                 0
#define TINV_WAIT_AFTER_LEVEL_START     1
#define TINV_WAIT_AFTER_SHIP_DESTROYED  2
#define TINV_WAIT_AFTER_LEVEL_CLEARED   3

void tinvBeginLevelStart();
void tinvBeginPlaying();
void tinvBeginLevelCleared();
void tinvBeginGameOverDisplay();

// -----------------------------------------------------------------------------

void tinvLoadMonstersLevels( int levels )
{
    for( int i = 0; i < 24; i++ )
      tinvSpace->MonsterGrid[ i / 6 ][ i % 6 ] = tinvMonstersLevels[ levels * 24 + i ];

    tinvSpace->MonsterGrid[ 4 ][ 0 ] = -1;
    tinvSpace->MonsterGrid[ 4 ][ 1 ] = -1;
    tinvSpace->MonsterGrid[ 4 ][ 2 ] = -1;
    tinvSpace->MonsterGrid[ 4 ][ 3 ] = -1;
    tinvSpace->MonsterGrid[ 4 ][ 4 ] = -1;
    tinvSpace->MonsterGrid[ 4 ][ 5 ] = -1;
}

void tinvSpeedControle()
{
    tinvMonsterRest = 0;
    for( int y = 0; y < 4; y++ )
      for( int x = 0; x < 6; x++ )
      {
          int v = tinvSpace->MonsterGrid[ y ][ x ];
          if( v != -1 && v <= 5 )
            tinvMonsterRest++;
      }

    tinvSpace->frameMax = tinvMonsterRest / 8;
}

void tinvGRIDMonsterFloorY()
{
    tinvSpace->MonsterFloorMax = 3;

    for( int y = 3; y >= 0; y-- )
    {
        bool rowHasMonster = false;
        for( int x = 0; x <= 5; x++ )
          if( tinvSpace->MonsterGrid[ y ][ x ] != -1 )
            rowHasMonster = true;

        if( rowHasMonster )
          return;

        tinvSpace->MonsterFloorMax--;
    }
}

int tinvLivePrint( int x, int y )
{
    int liveWide = ( 5 * tinvLive ) - 1;
    if( ( 0 >= ( x - liveWide ) ) && ( y == 7 ) )
      return tinvLIVE[ x ];
    return 0x00;
}

int tinvUFOWrite( int x, int y )
{
    if( ( tinvSpace->UFOxPos != -120 ) && ( y == 0 ) &&
        ( tinvSpace->UFOxPos <= x ) && ( tinvSpace->UFOxPos >= x - 14 ) )
      return tinvMonsters[ ( x - tinvSpace->UFOxPos ) + ( 6 * 14 ) + ( tinvSpace->oneFrame * 14 ) ];
    return 0x00;
}

void tinvUFOUpdate()
{
    if( tinvSpace->UFOxPos != -120 )
    {
        tinvSpace->UFOxPos -= 2;
        if( tinvSpace->UFOxPos <= -20 )
          tinvSpace->UFOxPos = -120;
    }
}

void tinvShipDestroyByMonster()
{
    if( tinvSpace->MonsterShoot[ 1 ] >= 14 && tinvSpace->MonsterShoot[ 1 ] <= 15 &&
        tinvSpace->MonsterShoot[ 0 ] >= tinvShipPos && tinvSpace->MonsterShoot[ 0 ] <= tinvShipPos + 14 )
      tinvShipDead = 1;
}

int tinvBoolRead( int shNum, int lineSH )
{
    int mask = 0x80 >> lineSH;
    if( ( tinvSpace->Shield[ shNum ] & mask ) != 0 )
      return 1;
    return 0;
}

void tinvShieldDestroyWrite( int boolWrite, int line, int origine )
{
    tinvSpace->Shield[ boolWrite ] &= ~( 0x80 >> line );
    if( origine == 0 )
      tinvSpace->MyShootBall = -1;
}

int tinvShieldDestroy( int origine, int varX, int varY )
{
    if( varY == 6 )
    {
        varX -= 20;

        for( int n = 0; n < 6; n += 2 )
        {
            if( varX <= 7 )
            {
                if( tinvBoolRead( n, varX ) ) { tinvShieldDestroyWrite( n, varX, origine ); return 1; }
            }

            varX -= 8;
            if( varX <= 7 )
            {
                if( tinvBoolRead( n + 1, varX ) ) { tinvShieldDestroyWrite( n + 1, varX, origine ); return 1; }
            }

            varX -= 27;
        }
    }
    return 0;
}

int tinvShieldBlitz( int part, int lineSH )
{
    if( lineSH == 0 )
    {
        if( part == 0 ) return 0xF0;
        return 0x0F;
    }
    if( lineSH == 1 )
    {
        if( part == 0 ) return 0xFC;
        return 0x0F;
    }
    if( lineSH >= 2 && lineSH <= 5 )
      return 0x0F;
    if( lineSH == 6 )
    {
        if( part == 1 ) return 0xFC;
        return 0x0F;
    }
    if( lineSH == 7 )
    {
        if( part == 1 ) return 0xF0;
        return 0x0F;
    }
    return 0x00;
}

int tinvMyShield( int x, int y )
{
    if( y == 6 )
    {
        x -= 20;

        for( int n = 0; n < 6; n += 2 )
        {
            if( x <= 7 )
            {
                if( tinvBoolRead( n, x ) ) return tinvShieldBlitz( 0, x );
                return 0x00;
            }

            x -= 8;
            if( x <= 7 )
            {
                if( tinvBoolRead( n + 1, x ) ) return tinvShieldBlitz( 1, x );
                return 0x00;
            }

            x -= 27;
        }
    }
    return 0x00;
}

void tinvRemoveExplodOnMonsterGrid()
{
    for( int y = 0; y <= 3; y++ )
      for( int x = 0; x <= 5; x++ )
      {
          if( tinvSpace->MonsterGrid[ y ][ x ] >= 11 )
            tinvSpace->MonsterGrid[ y ][ x ] = -1;
          if( tinvSpace->MonsterGrid[ y ][ x ] >= 8 )
            tinvSpace->MonsterGrid[ y ][ x ] = tinvSpace->MonsterGrid[ y ][ x ] + 1;
      }
}

int tinvBackground( int x, int y )
{
    // y unused (background scroll is horizontal-only) - self-assigned to
    // silence the unused-parameter warning, since this dialect has no
    // (void)param; idiom.
    y = y;
    int scr = ( tinvSpace->ScrBackV + x ) & 0x7F;
    return 0xFF - tinvChunkBuffer[ scr ];
}

int tinvVessoFn( int x, int y )
{
    if( ( x - tinvShipPos ) >= 0 && ( x - tinvShipPos ) < 13 && y == 7 )
    {
        if( tinvShipDead == 0 )
          return tinvVesso[ x - tinvShipPos ];
        return tinvVesso[ ( x - tinvShipPos ) + ( 12 * tinvSpace->oneFrame ) ];
    }
    return 0;
}

void tinvMonsterShootupdate()
{
    if( tinvSpace->MonsterShoot[ 1 ] != 16 )
    {
        tinvShipDestroyByMonster();
        if( tinvShieldDestroy( 1, tinvSpace->MonsterShoot[ 0 ], tinvSpace->MonsterShoot[ 1 ] / 2 ) )
          tinvSpace->MonsterShoot[ 1 ] = 16;
        else
          tinvSpace->MonsterShoot[ 1 ] = tinvSpace->MonsterShoot[ 1 ] + 1;
    }
}

void tinvMonsterShootGenerate()
{
    int a = arand( 3 );
    int b = arand( 6 );

    if( tinvSpace->MonsterShoot[ 1 ] == 16 )
    {
        if( tinvSpace->MonsterGrid[ a ][ b ] != -1 )
        {
            tinvSpace->MonsterShoot[ 0 ] = ( tinvSpace->MonsterGroupeXpos + 7 ) + ( b * 14 );
            tinvSpace->MonsterShoot[ 1 ] = ( ( tinvSpace->MonsterGroupeYpos + a ) * 2 ) + 1;
        }
    }
}

int tinvMonsterShoot( int x, int y )
{
    if( ( tinvSpace->MonsterShoot[ 1 ] / 2 ) == y && tinvSpace->MonsterShoot[ 0 ] == x )
    {
        if( ( tinvSpace->MonsterShoot[ 1 ] % 2 ) == 0 )
          return 0x0F;
        return 0xF0;
    }
    return 0x00;
}

int tinvOuDansLaGrilleMonster( int x, int y )
{
    if( x < tinvSpace->MonsterGroupeXpos ) return -1;
    if( y < tinvSpace->MonsterGroupeYpos ) return -1;

    tinvSpace->PositionDansGrilleMonsterX = ( x - tinvSpace->MonsterGroupeXpos ) / 14;
    tinvSpace->PositionDansGrilleMonsterY = ( y - tinvSpace->MonsterGroupeYpos );

    if( tinvSpace->PositionDansGrilleMonsterX > 5 ) return -1;
    if( tinvSpace->PositionDansGrilleMonsterY > 4 ) return -1;
    return 0;
}

// Small non-blocking multi-note SFX player, same shape as
// gameTinyPacman.c's/gameTinyBomber.c's own - upstream's real Sound(freq,
// dur) calls genuinely block on real hardware, but md_playTone() has no
// queue: a burst of N calls with no real time between them is only ever
// audible as the very last one. Declared here, ahead of its first call
// site (this function, the UFO-destroyed sweep below), since this
// dialect requires definition before use.
#define TINV_SFX_MAX_NOTES 15
float[TINV_SFX_MAX_NOTES] tinvSfxFreq;
float[TINV_SFX_MAX_NOTES] tinvSfxDur;
int tinvSfxLen;
int tinvSfxPos;
int tinvSfxWaitFrames;

void tinvStartSfx2( float freq0, float dur0, float freq1, float dur1 )
{
    tinvSfxFreq[ 0 ] = freq0; tinvSfxDur[ 0 ] = dur0;
    tinvSfxFreq[ 1 ] = freq1; tinvSfxDur[ 1 ] = dur1;
    tinvSfxLen = 2;
    tinvSfxPos = 0;
    tinvSfxWaitFrames = 0;
}

// Upstream's own level-cleared fanfare: Sound(110,255);_delay_ms(40);
// Sound(130,255);_delay_ms(40);Sound(100,255);_delay_ms(40);Sound(1,155);
// _delay_ms(20);Sound(60,255);Sound(60,255); - 5 distinct tones (the
// trailing Sound(60,255) is a duplicate, deduped here), each with a real
// _delay_ms() gap upstream that a synchronous burst here can't reproduce.
void tinvStartFanfare()
{
    int[5] freqBytes;
    freqBytes[ 0 ] = 110; freqBytes[ 1 ] = 130; freqBytes[ 2 ] = 100;
    freqBytes[ 3 ] = 1; freqBytes[ 4 ] = 60;
    int[5] durBytes;
    durBytes[ 0 ] = 255; durBytes[ 1 ] = 255; durBytes[ 2 ] = 255;
    durBytes[ 3 ] = 155; durBytes[ 4 ] = 255;

    int i;
    for( i = 0; i < 5; i++ )
    {
        int periodUs = 255 - freqBytes[ i ];
        if( periodUs < 1 )
          periodUs = 1;
        tinvSfxFreq[ i ] = 500000.0 / (float)periodUs;
        tinvSfxDur[ i ] = (float)( durBytes[ i ] * 2 * periodUs ) / 1000000.0;
    }
    tinvSfxLen = 5;
    tinvSfxPos = 0;
    tinvSfxWaitFrames = 0;
}

// Upstream's own UFO-destroyed sweep: for(x=1;x<100;x++){Sound(x,1);} - 99
// real notes, each only a couple microseconds long on real hardware
// (bit-banged, genuinely fast - the whole sweep is a near-instant "zap").
// Reproducing all 99 one-per-real-frame would stretch it to over a real
// second, far longer than intended - downsampled the loop's own step size
// instead (stride 7, matching the established fix for every other
// oversized computed sweep in this project), capped to this file's shared
// TINV_SFX_MAX_NOTES(15)-note buffer.
void tinvStartUfoSweep()
{
    int i;
    int x = 1;
    for( i = 0; i < TINV_SFX_MAX_NOTES; i++ )
    {
        int periodUs = 255 - x;
        if( periodUs < 1 )
          periodUs = 1;
        tinvSfxFreq[ i ] = 500000.0 / (float)periodUs;
        tinvSfxDur[ i ] = (float)( 1 * 2 * periodUs ) / 1000000.0;
        x = x + 7;
        if( x >= 100 ) x = 99;
    }
    tinvSfxLen = TINV_SFX_MAX_NOTES;
    tinvSfxPos = 0;
    tinvSfxWaitFrames = 0;
}

void tinvAdvanceSfx()
{
    if( tinvSfxPos >= tinvSfxLen )
      return;

    if( tinvSfxWaitFrames > 0 )
    {
        tinvSfxWaitFrames--;
        return;
    }

    md_playTone( tinvSfxFreq[ tinvSfxPos ], tinvSfxDur[ tinvSfxPos ] );

    int waitFrames = (int)( tinvSfxDur[ tinvSfxPos ] * 60.0 );
    if( waitFrames < 1 )
      waitFrames = 1;
    tinvSfxWaitFrames = waitFrames;

    tinvSfxPos++;
}

void tinvUFOAttackCheck( int x )
{
    // x unused (the UFO's own X range is read directly off tinvSpace) -
    // self-assigned to silence the unused-parameter warning.
    x = x;
    if( tinvSpace->MyShootBall == 0 )
    {
        if( tinvSpace->MyShootBallxpos >= tinvSpace->UFOxPos && tinvSpace->MyShootBallxpos <= tinvSpace->UFOxPos + 14 )
        {
            tinvStartUfoSweep();

            if( tinvLive < 3 ) { tinvLive++; tinvAddScore( 50 ); }
            else { tinvAddScore( 150 ); }

            tinvSpace->UFOxPos = -120;
        }
    }
}

void tinvMonsterAttackCheck()
{
    // Upstream (Tiny-invaders.ino lines 631-632) computed this grid cell
    // with its own round()-based formula, genuinely inconsistent with the
    // plain-integer-division formula OuDansLaGrilleMonster() uses to
    // decide what to *render* at each pixel (line 658) - a real bug
    // confirmed present in the original game itself, not a porting
    // mistake: near a cell boundary, a shot in the right half of a
    // monster's visually-drawn sprite gets attributed to the *next*
    // monster over instead, matching user reports of "shoot through
    // without a kill" / "nothing visible, but the monster to the right
    // dies". Fixed here (a deliberate, requested deviation from faithful-
    // upstream behavior) by reusing tinvOuDansLaGrilleMonster() directly
    // - the exact same grid-cell lookup already used for rendering - so
    // collision can never disagree with what's drawn.
    int myShootX = tinvSpace->MyShootBallxpos;
    int myShootY = tinvSpace->MyShootBall;

    if( tinvOuDansLaGrilleMonster( myShootX, myShootY ) == 0 )
    {
        int varX = tinvSpace->PositionDansGrilleMonsterX;
        int varY = tinvSpace->PositionDansGrilleMonsterY;

        int monster = tinvSpace->MonsterGrid[ varY ][ varX ];
        if( monster >= 0 && monster < 6 )
        {
            md_playTone( 50.0, 0.1 );
            if( monster < 2 ) tinvAddScore( 10 );
            if( monster < 4 ) tinvAddScore( 10 );
            tinvAddScore( 10 );
            tinvSpace->MonsterGrid[ varY ][ varX ] = 8;
            tinvSpace->MyShootBall = -1;
            tinvSpeedControle();
        }
    }
}

int tinvMyShoot( int x, int y )
{
    if( tinvSpace->MyShootBallxpos == x && y == tinvSpace->MyShootBall )
    {
        if( tinvSpace->MyShootBall > -1 )
          tinvSpace->MyShootBallFrame = 1 - tinvSpace->MyShootBallFrame;
        else
          return 0x00;

        if( tinvSpace->MyShootBallFrame == 1 )
          tinvSpace->MyShootBall--;

        tinvMonsterAttackCheck();
        tinvUFOAttackCheck( x );

        return tinvSHOOT[ tinvSpace->MyShootBallFrame ];
    }
    return 0x00;
}

int tinvSplitSpriteDecalageY( int input, int upOrDown )
{
    if( upOrDown )
      return input << tinvSpace->DecalageY8;
    return input >> ( 8 - tinvSpace->DecalageY8 );
}

int tinvWriteMonster14( int x )
{
    // x is always >= 0 here (called only with x - MonsterGroupeXpos, after
    // tinvOuDansLaGrilleMonster() already confirmed x >= MonsterGroupeXpos).
    return x % 14;
}

// tinvMurgeSplitUpDown() is called for every pixel column inside the
// monster grid's bounding box (up to ~336 of the 1024 pixels/frame), but
// PositionDansGrilleMonsterX only actually changes once every ~14 columns
// (it's (x - MonsterGroupeXpos) / 14) and PositionDansGrilleMonsterY is
// constant for the whole row - so the MonsterGrid[][] lookup and its
// derived "anims" value are identical for ~14 consecutive calls. Caching
// them here (keyed on the grid cell, recomputed only when it changes)
// avoids repeating that lookup+branch for every pixel in the cell; the
// actual tinvMonsters[] bitmap read still happens per-pixel unchanged,
// since that part *is* genuinely different for every column.
int tinvMonsterCacheX = -999;
int tinvMonsterCacheY = -999;
int tinvMonsterCacheSpriteType0;
int tinvMonsterCacheAnims0;
int tinvMonsterCacheSpriteTypeM1;
int tinvMonsterCacheAnimsM1;

int tinvMurgeSplitUpDown( int x )
{
    int gridX = tinvSpace->PositionDansGrilleMonsterX;
    int gridY = tinvSpace->PositionDansGrilleMonsterY;

    if( gridX != tinvMonsterCacheX || gridY != tinvMonsterCacheY )
    {
        tinvMonsterCacheX = gridX;
        tinvMonsterCacheY = gridY;

        int spriteType0 = tinvSpace->MonsterGrid[ gridY ][ gridX ];
        tinvMonsterCacheSpriteType0 = spriteType0;
        if( spriteType0 >= 0 && spriteType0 < 8 ) tinvMonsterCacheAnims0 = tinvSpace->anim * 14;
        else tinvMonsterCacheAnims0 = 0;

        if( gridY > 0 )
        {
            int spriteTypeM1 = tinvSpace->MonsterGrid[ gridY - 1 ][ gridX ];
            tinvMonsterCacheSpriteTypeM1 = spriteTypeM1;
            if( spriteTypeM1 >= 0 && spriteTypeM1 < 8 ) tinvMonsterCacheAnimsM1 = tinvSpace->anim * 14;
            else tinvMonsterCacheAnimsM1 = 0;
        }
    }

    int spriteType, anims;

    if( tinvSpace->DecalageY8 == 0 )
    {
        spriteType = tinvMonsterCacheSpriteType0;
        if( spriteType < 0 ) return 0x00;
        anims = tinvMonsterCacheAnims0;
        return tinvMonsters[ tinvWriteMonster14( x - tinvSpace->MonsterGroupeXpos ) + spriteType * 14 + anims ];
    }

    if( gridY == 0 )
    {
        spriteType = tinvMonsterCacheSpriteType0;
        if( spriteType < 0 ) return 0;
        anims = tinvMonsterCacheAnims0;
        return tinvSplitSpriteDecalageY( tinvMonsters[ tinvWriteMonster14( x - tinvSpace->MonsterGroupeXpos ) + spriteType * 14 + anims ], 1 );
    }

    int murge1 = 0;
    int murge2 = 0;

    spriteType = tinvMonsterCacheSpriteTypeM1;
    if( spriteType >= 0 )
    {
        anims = tinvMonsterCacheAnimsM1;
        murge1 = tinvSplitSpriteDecalageY( tinvMonsters[ tinvWriteMonster14( x - tinvSpace->MonsterGroupeXpos ) + spriteType * 14 + anims ], 0 );
    }

    spriteType = tinvMonsterCacheSpriteType0;
    if( spriteType >= 0 )
    {
        anims = tinvMonsterCacheAnims0;
        murge2 = tinvSplitSpriteDecalageY( tinvMonsters[ tinvWriteMonster14( x - tinvSpace->MonsterGroupeXpos ) + spriteType * 14 + anims ], 1 );
    }

    return murge1 | murge2;
}

int tinvMonster( int x, int y )
{
    if( tinvOuDansLaGrilleMonster( x, y ) == -1 )
      return 0x00;
    return tinvMurgeSplitUpDown( x );
}

void tinvMonsterRefreshMove()
{
    if( tinvSpace->Direction == 1 )
    {
        if( tinvSpace->MonsterGroupeXpos < tinvSpace->MonsterOffsetDroite )
        {
            tinvSpace->MonsterGroupeXpos += 2;
            return;
        }
        if( tinvSpace->DecalageY8 < 7 )
        {
            tinvSpace->DecalageY8 += 4;
            if( tinvSpace->DecalageY8 > 7 ) tinvSpace->DecalageY8 = 7;
        }
        else
        {
            tinvSpace->MonsterGroupeYpos++;
            tinvSpace->DecalageY8 = 0;
        }
        tinvSpace->Direction = 0;
        return;
    }

    if( tinvSpace->MonsterGroupeXpos > tinvSpace->MonsterOffsetGauche )
    {
        tinvSpace->MonsterGroupeXpos -= 2;
        return;
    }
    if( tinvSpace->DecalageY8 < 7 )
    {
        tinvSpace->DecalageY8 += 4;
        if( tinvSpace->DecalageY8 > 7 ) tinvSpace->DecalageY8 = 7;
    }
    else
    {
        tinvSpace->MonsterGroupeYpos++;
        tinvSpace->DecalageY8 = 0;
    }
    tinvSpace->Direction = 1;
}

void tinvVarResetNewLevel()
{
    tinvShieldRemoved = 0;
    tinvSpeedShootMonster = 0;
    tinvMonsterRest = 24;

    tinvSpace->UFOxPos = 0;
    tinvSpace->oneFrame = 0;
    for( int i = 0; i < 2; i++ ) tinvSpace->MonsterShoot[ i ] = 0;
    for( int y = 0; y < 5; y++ ) for( int x = 0; x < 6; x++ ) tinvSpace->MonsterGrid[ y ][ x ] = 0;
    for( int i = 0; i < 6; i++ ) tinvSpace->Shield[ i ] = 0;
    tinvSpace->ScrBackV = 0;
    tinvSpace->MyShootBall = 0;
    tinvSpace->MyShootBallxpos = 0;
    tinvSpace->MyShootBallFrame = 0;
    tinvSpace->anim = false;
    tinvSpace->frame = 0;
    tinvSpace->PositionDansGrilleMonsterX = 0;
    tinvSpace->PositionDansGrilleMonsterY = 0;
    tinvSpace->MonsterFloorMax = 0;
    tinvSpace->MonsterOffsetGauche = 0;
    tinvSpace->MonsterOffsetDroite = 0;
    tinvSpace->MonsterGroupeXpos = 0;
    tinvSpace->MonsterGroupeYpos = 0;
    tinvSpace->DecalageY8 = 0;
    tinvSpace->frameMax = 0;
    tinvSpace->Direction = 0;

    tinvLoadMonstersLevels( tinvLevels );

    for( int i = 0; i < 6; i++ ) tinvSpace->Shield[ i ] = 255;
    tinvSpace->MonsterShoot[ 0 ] = 16;
    tinvSpace->MonsterShoot[ 1 ] = 16;
    tinvSpace->UFOxPos = -120;
    tinvSpace->MyShootBall = -1;
    tinvSpace->MonsterFloorMax = 3;
    tinvSpace->MonsterGroupeXpos = 20;
    if( tinvLevels > 3 ) tinvSpace->MonsterGroupeYpos = 1;
    else tinvSpace->MonsterGroupeYpos = 0;
    tinvSpace->frameMax = 8;
    tinvSpace->Direction = 1;

    tinvNewLevelAnimation = false;
}

void tinvBebeep()
{
    // Upstream: Sound(100,125); Sound(50,125); - real freq/dur derived via
    // the shared 500000/(255-freq) formula, sequenced non-blocking instead
    // of a synchronous burst (md_playTone() has no queue).
    tinvStartSfx2( 3225.8, 0.039, 2439.0, 0.051 );
}

void tinvCalcNewBackgroundOffset()
{
    int scrBackV = ( tinvShipPos / 14 ) + 52;

    if( tinvNewLevelAnimation )
    {
        tinvLevelShiftOffsetX += 4;
        tinvLevelShiftOffsetX &= 0x7F;
        if( tinvLevelShiftOffsetX == 0 || tinvLevelShiftOffsetX == 68 )
          tinvNewLevelAnimation = false;
    }

    tinvSpace->ScrBackV = ( scrBackV + tinvLevelShiftOffsetX ) & 0x7F;
}

void tinvClearBattleground()
{
    // Upstream: memset(space->MonsterGrid, 0xff, sizeof(...)) - MonsterGrid
    // is int8_t there, so a raw 0xFF byte reads back as -1, matching the
    // "no monster here" sentinel every other MonsterGrid check in this file
    // uses (tinvOuDansLaGrilleMonster/tinvMurgeSplitUpDown's `< 0` checks,
    // the `!= -1` checks elsewhere). MonsterGrid is a plain (32-bit) `int`
    // here via avrCompat.h, so assigning the literal 0xFF stored 255, not
    // -1 - the sentinel checks never matched, so spriteType=255 fell
    // through into tinvMonsters[] far out of bounds, reading garbage sprite
    // data - the corrupted static seen during the level-start flash (before
    // tinvVarResetNewLevel() has a chance to populate real monster data a
    // few frames later).
    for( int y = 0; y < 5; y++ ) for( int x = 0; x < 6; x++ ) tinvSpace->MonsterGrid[ y ][ x ] = -1;
    for( int i = 0; i < 6; i++ ) tinvSpace->Shield[ i ] = 0;
    tinvSpace->UFOxPos = -120;
    tinvSpace->MyShootBall = -1;
    tinvSpace->MonsterShoot[ 0 ] = 16;
    tinvSpace->MonsterShoot[ 1 ] = 16;
    tinvShipPos = 56;
}

void tinvCalcMonsterPositionLimits()
{
    tinvSpace->MonsterOffsetGauche = 127;
    tinvSpace->MonsterOffsetDroite = -128;

    for( int x = 0; x < 6; x++ )
    {
        int monsterPresent = 0xFF;
        for( int y = 0; y < 4; y++ )
          monsterPresent &= tinvSpace->MonsterGrid[ y ][ x ];

        if( monsterPresent != 0xFF )
          if( tinvSpace->MonsterOffsetGauche == 127 )
            tinvSpace->MonsterOffsetGauche = -( x * 14 );
    }

    for( int x = 5; x >= 0; x-- )
    {
        int monsterPresent = 0xFF;
        for( int y = 0; y < 4; y++ )
          monsterPresent &= tinvSpace->MonsterGrid[ y ][ x ];

        if( monsterPresent != 0xFF )
          if( tinvSpace->MonsterOffsetDroite == -128 )
            tinvSpace->MonsterOffsetDroite = 44 + ( 5 - x ) * 14;
    }
}

// -----------------------------------------------------------------------------
//   Tiny_Flip - draws one full frame, one OLED "page" (8 vertical pixels x
//   128 columns) at a time, exactly mirroring the original's byte-stream
//   shape (see machineDependent.h / tinyJoypadShim.h for how that maps
//   onto Vircon32's texture-region blitter instead of a real SSD1306).
// -----------------------------------------------------------------------------

void tinvTinyFlip( int render0Picture1 )
{
    int* render;
    if( render0Picture1 == TINV_GAME_SCREEN )
      render = tinvBackCompressed;
    else
      render = tinvIntroCompressed;

    int renderPos = 0;

    // Tiny_Flip calls md_drawColumn() directly rather than going through
    // tinyJoypadShim's PrepareDisplayRow()/SendPixels() wrappers (which
    // normally clear the screen on row 0), so it has to clear here itself -
    // otherwise a pixel lit in a previous frame but not re-drawn this frame
    // (SendPixels(0) is always skipped, by design - see md_drawColumn's own
    // comment) would never be cleared and would ghost indefinitely.
    md_beginFrame();

    tinvNewHighScore = tinvNewHighScore || tinvUpdateHighScore();

    if( render0Picture1 == TINV_GAME_SCREEN )
    {
        tinvClearTextBuffer();
        tinvPrintText( 0, tinvTxtOneUp, 3 );
        tinvConvertValueToDigits( tinvGetScore(), tinvTextBuffer, 4 );
        tinvConvertValueToDigits( tinvHighScore, tinvTextBuffer, 26 );

        if( tinvNewHighScore )
          tinvPrintText( 22, tinvTxtOneUp, 3 );

        tinvPrintText( 36, tinvTxtLevel, 5 );
        tinvConvertValueToDigits( tinvCurrentLevel, tinvTextBuffer, 42 );
    }

    tinvCalcNewBackgroundOffset();

    for( int y = 0; y < 8; y++ )
    {
        renderPos = tinvRLEdecompressExtended( render, renderPos, tinvChunkBuffer, 128 );

        // Gates for tinvMonster()/tinvMyShoot()/tinvMonsterShoot() below,
        // computed once per row rather than re-derived on every pixel.
        // Each duplicates that function's own internal match condition
        // *exactly* (not an approximation of it) - tinvMonster() only ever
        // draws within the monster grid's own bounding box
        // (tinvOuDansLaGrilleMonster()'s own range check), and
        // tinvMyShoot()/tinvMonsterShoot() only ever match one single
        // (x,y) pixel each (their own equality check against the shot's
        // current position). None of MonsterGroupeXpos/Ypos, MyShootBall,
        // MyShootBallxpos, or MonsterShoot[] are mutated anywhere else in
        // this same render pass, so precomputing these once per row is
        // exactly equivalent to what each function already checks inside
        // itself on every call - this can never change *which* pixel ends
        // up drawn or *when* a collision side effect fires (tinvMyShoot's
        // embedded tinvMonsterAttackCheck()/tinvUFOAttackCheck() calls
        // still happen at the exact same single pixel as before), only
        // skips calls that were guaranteed to return 0x00 with no side
        // effect anyway - see this project's own established "a correctly
        // self-gated function still costs a full call every time it's
        // invoked" lesson.
        bool monsterRowValid = false;
        int monsterXMin = 0, monsterXMax = -1;
        bool myShootRowMatch = false;
        bool monsterShootRowMatch = false;

        if( render0Picture1 == TINV_GAME_SCREEN )
        {
            monsterRowValid = ( y >= tinvSpace->MonsterGroupeYpos && y <= tinvSpace->MonsterGroupeYpos + 4 );
            monsterXMin = tinvSpace->MonsterGroupeXpos;
            monsterXMax = monsterXMin + 83; // 6 columns * 14px - 1

            myShootRowMatch = ( y == tinvSpace->MyShootBall );
            monsterShootRowMatch = ( y == ( tinvSpace->MonsterShoot[ 1 ] / 2 ) );
        }

        for( int x = 0; x < 128; x++ )
        {
            int pixels;

            if( render0Picture1 == TINV_GAME_SCREEN )
            {
                // tinvLivePrint/tinvVessoFn (y==7 only), tinvUFOWrite/
                // tinvDisplayText (y==0 only) and tinvMyShield (y==6 only)
                // already internally no-op on every other row - skipping
                // the call entirely on those other 7/8 rows avoids paying
                // call overhead 1024 times/frame for something that can
                // only ever draw on 128 of those pixels.
                int myShield = 0;
                if( y == 6 && tinvShieldRemoved == 0 )
                  myShield = tinvMyShield( x, y );

                int liveAndVesso = 0;
                if( y == 7 )
                  liveAndVesso = tinvLivePrint( x, y ) | tinvVessoFn( x, y );

                int ufoAndText = 0;
                if( y == 0 )
                  ufoAndText = tinvUFOWrite( x, y ) | tinvDisplayText( x, y );

                int monster = 0;
                if( monsterRowValid && x >= monsterXMin && x <= monsterXMax )
                  monster = tinvMonster( x, y );

                int myShoot = 0;
                if( myShootRowMatch && x == tinvSpace->MyShootBallxpos )
                  myShoot = tinvMyShoot( x, y );

                int monsterShoot = 0;
                if( monsterShootRowMatch && x == tinvSpace->MonsterShoot[ 0 ] )
                  monsterShoot = tinvMonsterShoot( x, y );

                pixels = tinvBackground( x, y ) | liveAndVesso | ufoAndText |
                         monster | myShoot |
                         monsterShoot | myShield;

                if( tinvDisplayLevelNumber )
                  if( y >= 4 && y <= 5 )
                    pixels |= tinvDisplayZoomedText( x, y );
            }
            else if( render0Picture1 == TINV_INTRO_SCREEN )
            {
                pixels = tinvChunkBuffer[ x ];
            }
            else
            {
                pixels = tinvDisplayZoomedText( x, y );
            }

            md_drawColumn( x, y, pixels );
        }

        if( render0Picture1 == TINV_GAME_SCREEN )
          if( tinvShieldRemoved == 0 )
            tinvShieldDestroy( 0, tinvSpace->MyShootBallxpos, tinvSpace->MyShootBall );
    }

    if( render0Picture1 == TINV_GAME_SCREEN )
    {
        bool monstersStillHigh = tinvSpace->MonsterGroupeYpos < ( 2 + ( 4 - ( tinvSpace->MonsterFloorMax + 1 ) ) );
        if( !monstersStillHigh )
          if( tinvShieldRemoved != 1 )
          {
              for( int i = 0; i < 6; i++ ) tinvSpace->Shield[ i ] = 0;
              tinvShieldRemoved = 1;
          }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

void tinvBeginGameOverDisplay()
{
    tinvState = TINV_STATE_GAME_OVER_DISPLAY;
    tinvClearTextBuffer();

    if( tinvNewHighScore )
    {
        tinvPrintText( 0 * 16 + 2, tinvTxtNewHiScore, 12 );
        tinvConvertValueToDigits( tinvHighScore, tinvTextBuffer, 2 * 16 + 8 );

        // Upstream's own storeHighScoreToEEPROM() only actually runs once
        // the player finishes entering their 3-letter initials - that
        // whole name-entry screen is dropped in this port (see this
        // file's own header comment), so the save happens here instead,
        // at the same "a new high score was just reached" point this
        // screen itself already gates on. Stored as a plain 2-byte score
        // at addr 128 (upstream's own TINY_INVADERS_EEPROM_ADDR) - not
        // upstream's own 6-byte {score,name[3],crcFix} struct, since the
        // name/crcFix fields have nothing to persist once the initials
        // entry UI itself is gone.
        eeprom_write_word( 128, tinvHighScore );
    }
    else
    {
        tinvPrintText( 0, tinvTxtGameOver, 16 );
        tinvConvertValueToDigits( tinvGetScore(), tinvTextBuffer, 2 * 16 + 8 );
    }

    tinvTinyFlip( TINV_BLANK_SCREEN );
    tinvWaitFrames = 180; // ~3000ms at 60fps
    tinvWaitAction = TINV_WAIT_NONE;
}

void tinvBeginIntro()
{
    tinvState = TINV_STATE_INTRO;
    tinvIntroN = 0;
    tinvIntroSubFrame = 0;
}

void tinvBeginLevelStart()
{
    tinvState = TINV_STATE_LEVEL_START;
    tinvClearBattleground();
    tinvDisplayLevelNumber = true;
    tinvTinyFlip( TINV_GAME_SCREEN );
    tinvDisplayLevelNumber = false;
    tinvWaitFrames = 60; // ~1000ms
    tinvWaitAction = TINV_WAIT_AFTER_LEVEL_START;
}

void tinvBeginPlaying()
{
    tinvState = TINV_STATE_PLAYING;
    tinvShipDead = 0;
    tinvDecompte = 0;
}

void tinvBeginLevelCleared()
{
    tinvState = TINV_STATE_LEVEL_CLEARED;

    tinvStartFanfare();

    tinvClearBattleground();
    tinvNewLevelAnimation = true;
    tinvCurrentLevel++;
    if( tinvLevels < 9 ) tinvLevels++;
}

void gameTinyInvaders_init()
{
    // Upstream's NEWGAME: label sets Live=3 here too (same reset-block-
    // split issue as tinvCurrentLevel above) - gameTinyInvaders_init() left
    // this at 0, so a true first playthrough (never restarted) had zero
    // spare lives and ended on the very first hit.
    tinvLive = 3;
    tinvShieldRemoved = 0;
    tinvMonsterRest = 0;
    tinvLevels = 0;
    // Upstream's NEWGAME: label (which runs at cold boot too, not just a
    // post-game-over restart) sets currentLevel = 1 here - this port had
    // split that label's logic between this init function and
    // tinvStartNewGame() (used only for the restart path), and only the
    // latter got the correct initial value, so the very first playthrough
    // of a session showed "LEVEL 0" instead of "LEVEL 1".
    tinvCurrentLevel = 1;
    tinvSpeedShootMonster = 0;
    tinvShipDead = 0;
    tinvShipPos = 56;
    tinvNewHighScore = false;
    tinvFirstRun = true;
    tinvNewLevelAnimation = false;
    tinvLevelShiftOffsetX = 0;
    tinvDisplayLevelNumber = false;
    tinvDecompte = 0;
    tinvVarPot = 54;
    tinvMyShootReady = TINV_SHOOTS;
    tinvWaitFrames = 0;
    tinvWaitAction = TINV_WAIT_NONE;

    // Direct translation of upstream's own initHighScoreStruct() load - a
    // plain 2-byte score at addr 128, guarded against a never-written
    // slot's own virgin 65535 read the same way as every other game in
    // this pass (upstream's own struct-CRC check served the same purpose
    // for its own dropped 6-byte struct).
    tinvHighScore = eeprom_read_word( 128 );
    if( tinvHighScore == 65535 ) tinvHighScore = 0;

    tinvBeginIntro();
}

void tinvStartNewGame()
{
    tinvNewHighScore = false;
    tinvClearTextBuffer();
    tinvFirstRun = false;
    tinvResetScore();
    tinvLive = 3;
    tinvLevels = 0;
    tinvCurrentLevel = 1;
    tinvNewLevelAnimation = false;
    tinvDisplayLevelNumber = false;
    tinvLevelShiftOffsetX = 0;
    tinvClearBattleground();
    tinvBeginIntro();
}

void tinvOnWaitComplete()
{
    // Reset before dispatching, not after: tinvBeginLevelStart() (the
    // TINV_WAIT_AFTER_LEVEL_CLEARED branch below) sets a brand new
    // tinvWaitAction (TINV_WAIT_AFTER_LEVEL_START) itself - resetting
    // unconditionally afterward clobbered that value back to
    // TINV_WAIT_NONE every time, so the subsequent 60-frame level-start
    // countdown always completed with a wait action matching no branch
    // here, and the game never reached tinvBeginPlaying(). Symptom: after
    // clearing a level, the "LEVEL N" flash showed and then the game froze
    // forever (stuck in TINV_STATE_LEVEL_START, a state with no per-frame
    // dispatch branch of its own).
    int completedAction = tinvWaitAction;
    tinvWaitAction = TINV_WAIT_NONE;

    if( completedAction == TINV_WAIT_AFTER_LEVEL_START )
    {
        tinvVarResetNewLevel();
        tinvSpeedControle();
        tinvVarPot = 54;
        tinvShipPos = 56;
        tinvBeginPlaying();
    }
    else if( completedAction == TINV_WAIT_AFTER_SHIP_DESTROYED )
    {
        bool monstersInvaded = ( tinvSpace->MonsterGroupeYpos + ( tinvSpace->MonsterFloorMax + 1 ) ) == 7;
        if( monstersInvaded )
        {
            tinvBeginGameOverDisplay();
        }
        else if( tinvLive > 0 )
        {
            tinvLive--;
            tinvBeginPlaying();
        }
        else
        {
            tinvBeginGameOverDisplay();
        }
    }
    else if( completedAction == TINV_WAIT_AFTER_LEVEL_CLEARED )
    {
        tinvBeginLevelStart();
    }
}

void tinvUpdateIntro()
{
    tinvClearTextBuffer();

    if( tinvIntroN < 20 )
      tinvTinyFlip( TINV_INTRO_SCREEN );
    else if( tinvIntroN < 35 )
    {
        tinvPrintText( 5, tinvTxtPointValues, 55 );
        tinvTinyFlip( TINV_BLANK_SCREEN );
    }
    else if( tinvIntroN < 50 )
    {
        tinvClearTextBuffer();
        tinvPrintText( 0, tinvTxtGameOver, 16 );
        // Only " HISCORE!" (skip "NEW ") overlays the "GAME OVER!" template
        // here - matches upstream's printText(1*16+3, txtNewHiScore+3,
        // sizeof(txtNewHiScore)-3). Writing the full 12-character string
        // instead of this 9-character substring was the bug: it shifted
        // where the overlay ends by 3 positions, so the very next line
        // (which upstream relies on to land exactly one past the overlay,
        // to erase a leftover '!' from the underlying template) instead
        // landed on top of the overlay's own 'O', erasing it.
        tinvPrintText( 1 * 16 + 3, &tinvTxtNewHiScore[ 3 ], 9 );
        tinvTextBuffer[ 1 * 16 + 11 ] = 0;
        tinvConvertValueToDigits( tinvHighScore, tinvTextBuffer, 2 * 16 + 8 );
        tinvTinyFlip( TINV_BLANK_SCREEN );
    }
    else
    {
        tinvIntroN = -1;
    }

    // Upstream advances this same counter once per loop() iteration, but
    // that loop is rate-limited to ~5 iterations/second by a trailing
    // _delay_ms(200) - so each "n" step is really 200ms of real time, not
    // one 60fps frame. Redrawing every real frame (for responsive input)
    // but only advancing n once every 12 of them reproduces that same
    // ~200ms-per-step cadence instead of cycling 12x too fast.
    tinvIntroSubFrame++;
    if( tinvIntroSubFrame >= 12 )
    {
        tinvIntroSubFrame = 0;
        tinvIntroN++;
    }

    if( isFirePressed() )
    {
        tinvBebeep();
        tinvNewLevelAnimation = false;
        tinvDisplayLevelNumber = false;
        tinvLevelShiftOffsetX = 0;
        tinvBeginLevelStart();
    }
}

void tinvUpdatePlaying()
{
    // adapt the left and right position limits to the remaining monsters
    // (this was upstream's own per-frame comment - dropped by mistake in
    // the first porting pass; without it MonsterOffsetGauche/Droite stay
    // at their zeroed reset value forever, so the formation gets stuck
    // oscillating at x=0 and just keeps stepping downward instead of
    // ever bouncing sideways again)
    tinvCalcMonsterPositionLimits();

    if( ( tinvSpace->MonsterGroupeYpos + ( tinvSpace->MonsterFloorMax + 1 ) ) == 7 && tinvDecompte == 0 )
      tinvShipDead = 1;

    if( tinvSpeedShootMonster <= ( 9 - tinvLevels ) )
      tinvSpeedShootMonster++;
    else
    {
        tinvSpeedShootMonster = 0;
        tinvMonsterShootGenerate();
    }

    tinvTinyFlip( TINV_GAME_SCREEN );
    tinvSpace->oneFrame = 1 - tinvSpace->oneFrame;
    tinvRemoveExplodOnMonsterGrid();
    tinvMonsterShootupdate();
    tinvUFOUpdate();

    if( tinvSpace->MonsterGroupeXpos >= 26 && tinvSpace->MonsterGroupeXpos <= 28 &&
        tinvSpace->MonsterGroupeYpos == 2 && tinvSpace->DecalageY8 == 4 )
      tinvSpace->UFOxPos = 127;

    if( tinvVarPot > tinvShipPos + 2 ) tinvShipPos = tinvShipPos + ( ( tinvVarPot - tinvShipPos ) / 3 );
    if( tinvVarPot < tinvShipPos - 2 ) tinvShipPos = tinvShipPos - ( ( tinvShipPos - tinvVarPot ) / 3 );

    if( tinvShipDead != 1 )
    {
        if( tinvSpace->frame < tinvSpace->frameMax )
        {
            tinvSpace->frame++;
        }
        else
        {
            tinvGRIDMonsterFloorY();
            tinvSpace->anim = !tinvSpace->anim;
            if( !tinvSpace->anim ) md_playTone( 100.0, 0.02 );
            else md_playTone( 200.0, 0.02 );
            tinvMonsterRefreshMove();
            tinvSpace->frame = 0;
        }

        if( isRightPressed() )
          if( tinvVarPot < 108 ) tinvVarPot += 6;
        if( isLeftPressed() )
          if( tinvVarPot > 5 ) tinvVarPot -= 6;

        if( isFirePressed() && tinvMyShootReady == TINV_SHOOTS )
        {
            md_playTone( 200.0, 0.02 );
            tinvMyShootReady = 0;
            tinvSpace->MyShootBall = 6;
            tinvSpace->MyShootBallxpos = tinvShipPos + 6;
        }
    }
    else
    {
        // Upstream: Sound(80,1);Sound(100,1); fired every real loop
        // iteration while "crawling" (real hardware genuinely alternates
        // between the two, since each Sound() call blocks) - md_playTone()
        // has no queue, so a 2-call burst here would only ever be audible
        // as the last one; alternating a single call by tinvDecompte's own
        // parity instead reproduces the same audible back-and-forth buzz,
        // the same technique this file's own monster-march step sound
        // (tinvSpace->anim toggle, above) already uses.
        if( tinvDecompte % 2 == 0 ) md_playTone( 2857.1, 0.01 );
        else md_playTone( 3225.8, 0.01 );
        tinvDecompte++;
        if( tinvDecompte >= 30 )
        {
            tinvWaitFrames = 36; // ~600ms
            tinvWaitAction = TINV_WAIT_AFTER_SHIP_DESTROYED;
        }
    }

    if( tinvSpace->MyShootBall == -1 )
      if( tinvMyShootReady < TINV_SHOOTS )
        tinvMyShootReady++;

    if( tinvMonsterRest == 0 )
      tinvBeginLevelCleared();
}

void tinvUpdateLevelCleared()
{
    tinvTinyFlip( TINV_GAME_SCREEN );
    if( !tinvNewLevelAnimation )
    {
        tinvWaitFrames = 30; // ~500ms
        tinvWaitAction = TINV_WAIT_AFTER_LEVEL_CLEARED;
    }
}

void gameTinyInvaders_update()
{
    tinvAdvanceSfx();

    if( tinvWaitFrames > 0 )
    {
        tinvWaitFrames--;
        if( tinvWaitFrames == 0 )
          tinvOnWaitComplete();
        return;
    }

    if( tinvState == TINV_STATE_GAME_OVER_DISPLAY )
    {
        tinvStartNewGame();
    }
    else if( tinvState == TINV_STATE_INTRO )
    {
        tinvUpdateIntro();
    }
    else if( tinvState == TINV_STATE_PLAYING )
    {
        // Unlike Trick/Pinball/Bert, this game's own tinvTinyFlip() isn't
        // a pure render function - the shot's own advancement (its
        // MyShootBall countdown) and its collision check
        // (tinvMyShoot()->tinvMonsterAttackCheck()/tinvUFOAttackCheck())
        // are embedded directly in its per-pixel draw loop as a
        // deliberate space-saving reuse of the same per-column pass (see
        // tinvMyShoot()'s own comment). Calling it alone on a skipped
        // real frame (to keep redraw at 60fps, the pattern used for the
        // other movement-throttled games) would silently let the shot
        // keep moving/colliding at full rate while monster movement stays
        // throttled to half rate, desyncing the two - exactly the
        // "shoot through" / "wrong monster dies" symptom reported. So
        // skipped frames do nothing at all here (previous frame persists,
        // same trick used by the whole-tick-throttled games) instead of
        // redrawing - the whole tick (shot+monsters+render) only ever
        // advances as one atomic unit.
        tinvMoveTickCounter++;
        if( tinvMoveTickCounter >= TINV_MOVE_DIVISOR )
        {
            tinvMoveTickCounter = 0;
            tinvUpdatePlaying();
        }
    }
    else if( tinvState == TINV_STATE_LEVEL_CLEARED )
    {
        tinvUpdateLevelCleared();
    }
}

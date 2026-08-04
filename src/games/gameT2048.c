// =============================================================================
// t2048 - ported from Obono's TinyJoypadWorks/t2048
// (https://github.com/obono/TinyJoypadWorks/tree/main/t2048), MIT License,
// Copyright (c) 2020-2025 OBONO.
//
// Same shim/port pattern as gameNumberPlace.c (obonoCoreShim.h - Obono's
// sprite/string engine reproduced on top of Vircon32's real video/input/
// audio). t2048's own core.h is nearly byte-identical to NumberPlace's -
// only real difference is a constant named SPECIAL here where NumberPlace
// used DIRECT for the same sprite-blit mode - so this port carries over
// almost none of the friction NumberPlace or Tiny Invaders needed: t2048
// was already mode/state-based upstream (STATE_IDLE/MOVING/OVER), no
// blocking loops to convert at all.
//
// Structural changes from upstream, beyond the usual dialect fixes (array-
// declaration syntax, no bitfields, no scoped/underlying-type enums, no
// ternary, no function overloading):
//  - Memory-card persistence isn't used by this game upstream either
//    (no high-score save), so nothing was dropped here.
// =============================================================================

// Upstream ran at a real FPS=30 - a later session's frame-pacing pass
// added a tick-skip throttle here to match it, but reverted per user
// request ("plays faster/nicer" without it) - this file's own update()
// just runs once per real Vircon32 frame again, same as every other
// non-throttled port here.
#define T2048_FPS 60

// t2048's own upstream core.h names the "blit exactly as stored, no AND/OR/
// XOR blend" sprite mode SPECIAL where NumberPlace's core.h (obonoCoreShim.h
// here) names the same mode/value DIRECT - just an alias, not a new mode.
#define SPECIAL DIRECT

// -----------------------------------------------------------------------------
//   common.h / common.cpp equivalent
// -----------------------------------------------------------------------------

enum T2048Mode
{
    T2048_MODE_LOGO = 0,
    T2048_MODE_TITLE,
    T2048_MODE_GAME
};

int t2048Mode = T2048_MODE_LOGO;
int t2048Counter;

int t2048Random( int n )
{
    return arand( n );
}

// -----------------------------------------------------------------------------
//   data.h
// -----------------------------------------------------------------------------

#define T2048_IMG_TILE_W 24
#define T2048_IMG_TILE_H 16

// 18 tile face bitmaps (2,4,8,...) - functional sprite data, ported
// unchanged from data.h's imgTile[][46].
int[18][46] t2048ImgTile =
{
    {
        0xAA, 0x55, 0xAA, 0x05, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01,
        0x02, 0x01, 0x02, 0x05, 0xAA, 0x55, 0xAA, 0x2A, 0x55, 0x2A, 0x50, 0x20, 0x40, 0x20, 0x40, 0x20,
        0x40, 0x20, 0x40, 0x20, 0x40, 0x20, 0x40, 0x20, 0x40, 0x20, 0x50, 0x2A, 0x55, 0x2A
    },
    {
        0xFE, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x11, 0x19, 0x0D, 0x05, 0x05, 0x85, 0xCD, 0x79, 0x31,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xFE, 0x3F, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x50, 0x58,
        0x5C, 0x56, 0x53, 0x51, 0x50, 0x50, 0x50, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x3F
    },
    {
        0xFE, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x81, 0xE1, 0x79, 0x19, 0x01, 0xFD, 0xFD, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xFE, 0x3F, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x43, 0x43,
        0x42, 0x42, 0x42, 0x5F, 0x5F, 0x42, 0x42, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x3F
    },
    {
        0xFE, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x31, 0x79, 0xCD, 0x85, 0x85, 0x85, 0xCD, 0x79, 0x31,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0xFE, 0x3F, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x46, 0x4F,
        0x59, 0x50, 0x50, 0x50, 0x59, 0x4F, 0x46, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x3F
    },
    {
        0xFE, 0x01, 0x01, 0x11, 0x09, 0xFD, 0xFD, 0x01, 0x01, 0x01, 0x01, 0xF1, 0xF9, 0xCD, 0x45, 0x45,
        0x45, 0xCD, 0x89, 0x09, 0x01, 0x01, 0xFE, 0x3F, 0x40, 0x40, 0x50, 0x50, 0x5F, 0x5F, 0x50, 0x50,
        0x40, 0x40, 0x47, 0x4F, 0x58, 0x50, 0x50, 0x50, 0x58, 0x4F, 0x47, 0x40, 0x40, 0x3F
    },
    {
        0xFE, 0x01, 0x09, 0x09, 0x0D, 0x05, 0x85, 0x85, 0xCD, 0x79, 0x31, 0x01, 0x11, 0x19, 0x0D, 0x05,
        0x05, 0x85, 0xCD, 0x79, 0x31, 0x01, 0xFE, 0x3F, 0x40, 0x44, 0x4C, 0x58, 0x50, 0x50, 0x50, 0x59,
        0x4F, 0x46, 0x40, 0x50, 0x58, 0x5C, 0x56, 0x53, 0x51, 0x50, 0x50, 0x50, 0x40, 0x3F
    },
    {
        0xFE, 0x01, 0xF1, 0xF9, 0xCD, 0x45, 0x45, 0x45, 0xCD, 0x89, 0x09, 0x01, 0x81, 0xE1, 0x79, 0x19,
        0x01, 0xFD, 0xFD, 0x01, 0x01, 0x01, 0xFE, 0x3F, 0x40, 0x47, 0x4F, 0x58, 0x50, 0x50, 0x50, 0x58,
        0x4F, 0x47, 0x40, 0x43, 0x43, 0x42, 0x42, 0x42, 0x5F, 0x5F, 0x42, 0x42, 0x40, 0x3F
    },
    {
        0xFE, 0x01, 0x11, 0x09, 0xFD, 0xFD, 0x01, 0x01, 0x19, 0x1D, 0x85, 0xE5, 0x7D, 0x19, 0x01, 0x79,
        0xFD, 0x85, 0x85, 0xFD, 0x79, 0x01, 0xFE, 0x3F, 0x40, 0x50, 0x50, 0x5F, 0x5F, 0x50, 0x40, 0x58,
        0x5E, 0x57, 0x51, 0x50, 0x50, 0x40, 0x4F, 0x5F, 0x50, 0x50, 0x5F, 0x4F, 0x40, 0x3F
    },
    {
        0xFE, 0x01, 0x19, 0x1D, 0x85, 0xE5, 0x7D, 0x19, 0xFD, 0xFD, 0x45, 0x45, 0xC5, 0x85, 0x01, 0xF9,
        0xFD, 0x45, 0x45, 0xCD, 0x89, 0x01, 0xFE, 0x3F, 0x40, 0x58, 0x5E, 0x57, 0x51, 0x50, 0x50, 0x48,
        0x58, 0x50, 0x50, 0x5F, 0x4F, 0x40, 0x4F, 0x5F, 0x50, 0x50, 0x5F, 0x4F, 0x40, 0x3F
    },
    {
        0xFE, 0x01, 0xFD, 0xFD, 0x45, 0x45, 0xC5, 0x85, 0x01, 0x11, 0x09, 0xFD, 0xFD, 0x01, 0x01, 0x19,
        0x1D, 0x85, 0xE5, 0x7D, 0x19, 0x01, 0xFE, 0x3F, 0x40, 0x48, 0x58, 0x50, 0x50, 0x5F, 0x4F, 0x40,
        0x50, 0x50, 0x5F, 0x5F, 0x50, 0x40, 0x58, 0x5E, 0x57, 0x51, 0x50, 0x50, 0x40, 0x3F
    },
    {
        0xFE, 0x01, 0x11, 0xFD, 0xFD, 0x01, 0x01, 0xE1, 0x11, 0xF1, 0xE1, 0x01, 0x21, 0x11, 0xF1, 0xE1,
        0x01, 0xE1, 0x01, 0xF1, 0xF1, 0x01, 0xFE, 0x3F, 0x40, 0x50, 0x5F, 0x5F, 0x50, 0x40, 0x4F, 0x50,
        0x5F, 0x4F, 0x40, 0x5C, 0x5F, 0x53, 0x50, 0x40, 0x47, 0x44, 0x5F, 0x5F, 0x40, 0x3F
    },
    {
        0xFE, 0xE7, 0xE3, 0x7B, 0x1B, 0x83, 0xE7, 0xFF, 0x1F, 0xEF, 0x0F, 0x1F, 0xFF, 0x1F, 0xFF, 0x0F,
        0x0F, 0xFF, 0x1F, 0xEF, 0x0F, 0x1F, 0xFE, 0x3F, 0x67, 0x61, 0x68, 0x6E, 0x6F, 0x6F, 0x7F, 0x70,
        0x6F, 0x60, 0x70, 0x7F, 0x78, 0x7B, 0x60, 0x60, 0x7F, 0x71, 0x6E, 0x60, 0x71, 0x3F
    },
    {
        0xFE, 0x07, 0x07, 0xFF, 0x03, 0x03, 0xFF, 0xFF, 0x1F, 0xEF, 0x0F, 0x1F, 0xFF, 0x1F, 0xEF, 0x0F,
        0x1F, 0xFF, 0x1F, 0x0F, 0x6F, 0xDF, 0xFE, 0x3F, 0x7C, 0x7C, 0x7D, 0x60, 0x60, 0x7D, 0x7F, 0x70,
        0x6F, 0x60, 0x70, 0x7F, 0x76, 0x6D, 0x60, 0x70, 0x7F, 0x70, 0x60, 0x6F, 0x70, 0x3F
    },
    {
        0xFE, 0x87, 0x03, 0x7B, 0x7B, 0x03, 0x87, 0xFF, 0xDF, 0x0F, 0x0F, 0xFF, 0xFF, 0x1F, 0xEF, 0x0F,
        0x1F, 0xFF, 0xDF, 0xEF, 0x0F, 0x1F, 0xFE, 0x3F, 0x70, 0x60, 0x6F, 0x6F, 0x60, 0x70, 0x7F, 0x6F,
        0x60, 0x60, 0x6F, 0x7F, 0x76, 0x6D, 0x60, 0x70, 0x7F, 0x63, 0x60, 0x6C, 0x6F, 0x3F
    },
    {
        0xFE, 0x7B, 0x01, 0x01, 0x7F, 0xFF, 0x83, 0x01, 0x75, 0x75, 0x05, 0x8F, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x3F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x5D,
        0x55, 0x41, 0x6B, 0x7F, 0x6B, 0x55, 0x41, 0x6B, 0x7F, 0x63, 0x6F, 0x41, 0x41, 0x3F
    },
    {
        0xFE, 0xBB, 0x39, 0x6D, 0x6D, 0x01, 0x93, 0xFF, 0x7B, 0x39, 0x1D, 0x4D, 0x61, 0x73, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x3F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7D,
        0x4D, 0x41, 0x71, 0x7F, 0x63, 0x41, 0x55, 0x6D, 0x7F, 0x6B, 0x55, 0x41, 0x6B, 0x3F
    },
    {
        0xFE, 0x83, 0x01, 0x75, 0x75, 0x05, 0x8F, 0xFF, 0xA1, 0x21, 0x75, 0x75, 0x05, 0x8D, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x3F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x51,
        0x51, 0x45, 0x6D, 0x7F, 0x5D, 0x55, 0x41, 0x6B, 0x7F, 0x63, 0x41, 0x55, 0x6D, 0x3F
    },
    {
        0xFE, 0x7B, 0x01, 0x01, 0x7F, 0xFF, 0xBB, 0x39, 0x6D, 0x6D, 0x01, 0x93, 0xFF, 0x7B, 0x01, 0x01,
        0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x3F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x63,
        0x5D, 0x41, 0x63, 0x7F, 0x7D, 0x4D, 0x41, 0x71, 0x7F, 0x4D, 0x45, 0x51, 0x5B, 0x3F
    }
};

int[70] t2048ImgLogo =
{
    0x00, 0x80, 0xC0, 0x60, 0x20, 0xE0, 0x20, 0x20, 0x40, 0x80, 0x00, 0x00, 0xFF, 0x57, 0xAB, 0x57,
    0x01, 0xFF, 0x20, 0x20, 0x40, 0x80, 0x00, 0x00, 0x00, 0x80, 0xC0, 0x60, 0x20, 0xE0, 0x20, 0x20,
    0x40, 0x80, 0x00, 0x1F, 0x37, 0x6A, 0x95, 0xA8, 0x97, 0xAC, 0x94, 0x4C, 0x24, 0x1F, 0x00, 0x1F,
    0x35, 0x6A, 0x95, 0xA8, 0x97, 0xAC, 0x94, 0x4C, 0x24, 0x1F, 0x00, 0xFF, 0x97, 0xAA, 0x95, 0x80,
    0xBF, 0xAC, 0x94, 0xAC, 0x84, 0xFF
};

int[240] t2048ImgTitle =
{
    0xE0, 0xF0, 0xF8, 0xFC, 0xFC, 0xFE, 0x7E, 0x3E, 0x3E, 0x3E, 0x3E, 0x7E, 0xFE, 0xFC, 0xFC, 0xF8,
    0xF0, 0xE0, 0x00, 0x00, 0xC0, 0xF0, 0xF8, 0xFC, 0xFC, 0xFE, 0x3E, 0x1E, 0x1E, 0x1E, 0x1E, 0x3E,
    0xFE, 0xFC, 0xFC, 0xF8, 0xF0, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xE0, 0xF0,
    0xF8, 0xFC, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0,
    0xF8, 0xFC, 0xFC, 0xFE, 0x3E, 0x1E, 0x1E, 0x1E, 0x1E, 0x3E, 0xFE, 0xFC, 0xFC, 0xF8, 0xF0, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x81, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0x7F, 0x3F, 0x1F, 0x0F,
    0x07, 0x03, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xE0, 0xF0, 0xF8, 0xFE, 0xFF, 0xFF, 0xDF, 0xC7,
    0xC3, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0xC0, 0xC0, 0xC0, 0x00, 0x00, 0xC0, 0xE3,
    0xF7, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x3C, 0x3C, 0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xF7, 0xE3, 0xC0,
    0x70, 0x78, 0x7C, 0x7E, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7B, 0x79, 0x78, 0x78, 0x78, 0x78, 0x78,
    0x78, 0x78, 0x00, 0x00, 0x03, 0x0F, 0x1F, 0x3F, 0x3F, 0x7F, 0x7C, 0x78, 0x78, 0x78, 0x78, 0x7C,
    0x7F, 0x3F, 0x3F, 0x1F, 0x0F, 0x03, 0x00, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x03, 0x03, 0x03, 0x03, 0x00, 0x00, 0x0F, 0x1F,
    0x3F, 0x3F, 0x7F, 0x7F, 0x7C, 0x78, 0x78, 0x78, 0x78, 0x7C, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x0F
};

int[11] t2048SoundStart = { 72, 12, 74, 12, 76, 12, 77, 12, 79, 36, 0xFF };
int[17] t2048SoundOver  = { 55, 13, 54, 16, 53, 19, 52, 22, 51, 25, 50, 28, 49, 31, 48, 34, 0xFF };
int[3]  t2048SoundMove  = { 59, 1, 0xFF };
int[5]  t2048SoundMerge4  = { 74, 2, 78, 2, 0xFF };
int[7]  t2048SoundMerge8  = { 77, 2, 81, 2, 85, 2, 0xFF };
int[9]  t2048SoundMerge16 = { 82, 2, 87, 2, 92, 2, 97, 2, 0xFF };
int[19] t2048SoundMerge32 = { 69, 3, 81, 3, 93, 3, 72, 3, 84, 3, 96, 3, 77, 3, 89, 3, 101, 3, 0xFF };
int[19] t2048SoundMerge64 = { 71, 4, 83, 4, 95, 4, 74, 4, 86, 4, 98, 4, 79, 4, 91, 4, 103, 4, 0xFF };
int[25] t2048SoundMerge128 = { 72, 4, 84, 4, 96, 4, 76, 4, 88, 4, 100, 4, 79, 4, 91, 4, 103, 4, 84, 4, 96, 4, 108, 4, 0xFF };
int[17] t2048SoundMerge256 = { 67, 6, 72, 6, 69, 6, 74, 6, 71, 6, 76, 6, 72, 6, 77, 6, 0xFF };
int[17] t2048SoundMerge512 = { 69, 7, 73, 7, 76, 7, 81, 7, 71, 7, 74, 7, 78, 7, 83, 7, 0xFF };
int[17] t2048SoundMerge1024 = { 71, 8, 75, 8, 78, 8, 77, 8, 75, 8, 77, 8, 78, 8, 83, 8, 0xFF };
int[19] t2048SoundMerge2048 = { 72, 10, 79, 10, 76, 10, 79, 10, 81, 10, 77, 10, 83, 10, 79, 10, 84, 15, 0xFF };

// Table of note-sequences to play per merged tile value (index 0 = plain
// slide, no merge; index 1 is unreachable - the smallest possible merge
// result is value 2 - kept as NULL to match upstream exactly).
int*[18] t2048SoundMergeTable;

void t2048InitSoundMergeTable()
{
    t2048SoundMergeTable[0] = t2048SoundMove;
    t2048SoundMergeTable[1] = NULL;
    t2048SoundMergeTable[2] = t2048SoundMerge4;
    t2048SoundMergeTable[3] = t2048SoundMerge8;
    t2048SoundMergeTable[4] = t2048SoundMerge16;
    t2048SoundMergeTable[5] = t2048SoundMerge32;
    t2048SoundMergeTable[6] = t2048SoundMerge64;
    t2048SoundMergeTable[7] = t2048SoundMerge128;
    t2048SoundMergeTable[8] = t2048SoundMerge256;
    t2048SoundMergeTable[9] = t2048SoundMerge512;
    t2048SoundMergeTable[10] = t2048SoundMerge1024;
    t2048SoundMergeTable[11] = t2048SoundMerge2048;
    t2048SoundMergeTable[12] = t2048SoundMerge2048;
    t2048SoundMergeTable[13] = t2048SoundMerge2048;
    t2048SoundMergeTable[14] = t2048SoundMerge2048;
    t2048SoundMergeTable[15] = t2048SoundMerge2048;
    t2048SoundMergeTable[16] = t2048SoundMerge2048;
    t2048SoundMergeTable[17] = t2048SoundMerge2048;
}

// -----------------------------------------------------------------------------
//   logo.cpp
// -----------------------------------------------------------------------------

#define T2048_IMG_LOGO_W 35
#define T2048_IMG_LOGO_H 16

void t2048InitLogo()
{
    setSprite( 0, ( WIDTH - T2048_IMG_LOGO_W ) / 2, ( HEIGHT - T2048_IMG_LOGO_H ) / 2, t2048ImgLogo,
               T2048_IMG_LOGO_W, T2048_IMG_LOGO_H, WHITE );
    setString( 8, 86, "OBN-T03", WHITE );
    setString( 9, 104, "V0.2", WHITE );
    t2048Counter = T2048_FPS * 2;
    isInvalid = true;
}

int t2048UpdateLogo()
{
    t2048Counter--;
    if( t2048Counter > 0 )
      return T2048_MODE_LOGO;
    return T2048_MODE_TITLE;
}

// -----------------------------------------------------------------------------
//   title.cpp
// -----------------------------------------------------------------------------

void t2048InitTitle()
{
    setSprite( 0, 24, 16, t2048ImgTitle, 80, 24, WHITE );
    setString( 8, 31, "PRESS BUTTON", WHITE );
    isInvalid = true;
}

int t2048UpdateTitle()
{
    srand( rand() ); // shuffle random, matches upstream's stirring of the RNG

    if( isButtonDown( A_BUTTON ) )
      return T2048_MODE_GAME;
    return T2048_MODE_TITLE;
}

// -----------------------------------------------------------------------------
//   game.cpp
// -----------------------------------------------------------------------------

#define T2048_BOARD_SIZE 4

#define T2048_STATE_IDLE   0
#define T2048_STATE_MOVING 1
#define T2048_STATE_OVER   2

// setString() only stores the pointer it's given, not a copy - these UI
// labels need to keep being valid for the rest of this game's lifetime
// (redrawn every frame via refreshScreen()), so they're named persistent
// globals rather than string literals passed inline at the call site.
int[6] t2048LabelScore = "SCORE";
int[5] t2048LabelTime  = "TIME";
int[5] t2048LabelGame  = "GAME";
int[5] t2048LabelOver  = "OVER";

struct T2048Tile
{
    int value;
    int merged;
    int move;
};

int t2048State;
T2048Tile[T2048_BOARD_SIZE][T2048_BOARD_SIZE] t2048Board;
int t2048Score;
int t2048Empty, t2048TimeMinutes, t2048TimeSeconds, t2048TimeFrames;
int t2048MoveVx, t2048MoveVy, t2048AddedIdx;
int[7] t2048ScoreString;
int[7] t2048TimeString;

int t2048Num2str( int* p, int pOffset, int value )
{
    while( true )
    {
        p[ pOffset ] = ( value % 10 ) + '0';
        pOffset--;
        value /= 10;
        if( value <= 0 )
          break;
    }
    return pOffset;
}

void t2048UpdateScoreString()
{
    int p = t2048Num2str( t2048ScoreString, 5, t2048Score );
    while( p >= 0 )
    {
        t2048ScoreString[ p ] = ' ';
        p--;
    }
    isInvalid = true;
}

void t2048UpdateTimeString()
{
    t2048Num2str( t2048TimeString, 5, t2048TimeSeconds + 100 );
    int p = t2048Num2str( t2048TimeString, 2, t2048TimeMinutes );
    while( p >= 0 )
    {
        t2048TimeString[ p ] = ' ';
        p--;
    }
    t2048TimeString[ 3 ] = ':';
    isInvalid = true;
}

void t2048InitBoard()
{
    for( int y = 0; y < T2048_BOARD_SIZE; y++ )
      for( int x = 0; x < T2048_BOARD_SIZE; x++ )
      {
          t2048Board[ y ][ x ].value = 0;
          t2048Board[ y ][ x ].merged = 0;
          t2048Board[ y ][ x ].move = 0;
      }
    t2048Empty = T2048_BOARD_SIZE * T2048_BOARD_SIZE;
}

int t2048GetTileValue( int x, int y )
{
    if( x >= 0 && x < T2048_BOARD_SIZE && y >= 0 && y < T2048_BOARD_SIZE )
      return t2048Board[ y ][ x ].value;
    return -1;
}

void t2048AddRandomTile()
{
    int position = t2048Random( t2048Empty );
    for( int y = 0; y < T2048_BOARD_SIZE; y++ )
      for( int x = 0; x < T2048_BOARD_SIZE; x++ )
      {
          T2048Tile* p = &t2048Board[ y ][ x ];
          if( p->value == 0 )
          {
              if( position == 0 )
              {
                  if( t2048Random( 10 ) == 0 )
                    p->value = 2;
                  else
                    p->value = 1;
                  t2048Empty--;
                  t2048AddedIdx = y * T2048_BOARD_SIZE + x;
                  return;
              }
              position--;
          }
      }
}

bool t2048MoveTiles( int vx, int vy )
{
    bool ret = false;

    for( int i = 0; i < T2048_BOARD_SIZE; i++ )
      for( int j = 0; j < T2048_BOARD_SIZE; j++ )
      {
          int x = j;
          if( vx > 0 ) x = T2048_BOARD_SIZE - 1 - j;
          int y = i;
          if( vy > 0 ) y = T2048_BOARD_SIZE - 1 - i;

          T2048Tile* p = &t2048Board[ y ][ x ];
          p->move = 0;

          if( p->value > 0 )
          {
              int value = p->value;
              int nextValue;
              p->value = 0;
              p->merged = false;

              while( true )
              {
                  p->move++;
                  x += vx;
                  y += vy;
                  nextValue = t2048GetTileValue( x, y );
                  if( nextValue != 0 )
                    break;
              }

              if( nextValue == value && !t2048Board[ y ][ x ].merged )
              {
                  t2048Board[ y ][ x ].merged = true;
                  t2048Empty++;
              }
              else
              {
                  p->move--;
                  t2048Board[ y - vy ][ x - vx ].value = value;
              }

              if( p->move > 0 )
                ret = true;
          }
      }

    return ret;
}

void t2048UpdateTiles()
{
    int soundValue = 0;
    int idx = 0;

    for( int y = 0; y < T2048_BOARD_SIZE; y++ )
      for( int x = 0; x < T2048_BOARD_SIZE; x++ )
      {
          T2048Tile* p = &t2048Board[ y ][ x ];
          if( p->merged )
          {
              p->value++;
              t2048Score += 1 << p->value;
              if( soundValue < p->value )
                soundValue = p->value;
          }

          if( p->value > 0 )
            setSprite( idx, x * T2048_IMG_TILE_W, y * T2048_IMG_TILE_H, t2048ImgTile[ p->value ],
                       T2048_IMG_TILE_W - 1, T2048_IMG_TILE_H - 1, SPECIAL );
          else
            clearSprite( idx );

          idx++;
      }

    if( t2048Score > 999999 )
      t2048Score = 999999;

    t2048UpdateScoreString();
    playScore( t2048SoundMergeTable[ soundValue ] );
    isInvalid = true;
}

bool t2048IsGameOver()
{
    if( t2048Empty > 0 )
      return false;

    for( int y = 0; y < T2048_BOARD_SIZE; y++ )
      for( int x = 0; x < T2048_BOARD_SIZE; x++ )
      {
          int value = t2048GetTileValue( x, y );
          if( value == 0 )
            return false;
          if( t2048GetTileValue( x - 1, y ) == value || t2048GetTileValue( x + 1, y ) == value ||
              t2048GetTileValue( x, y - 1 ) == value || t2048GetTileValue( x, y + 1 ) == value )
            return false;
      }

    return true;
}

void t2048ForwardTime()
{
    t2048TimeFrames++;
    if( t2048TimeFrames == T2048_FPS )
    {
        t2048TimeFrames = 0;
        t2048TimeSeconds++;
        if( t2048TimeSeconds == 60 )
        {
            t2048TimeSeconds = 0;
            t2048TimeMinutes++;
        }
        t2048UpdateTimeString();
    }
}

void t2048UpdateSprites()
{
    if( t2048State == T2048_STATE_IDLE )
    {
        if( t2048Counter < 18 )
        {
            int idx = 0;
            for( int y = 0; y < T2048_BOARD_SIZE; y++ )
              for( int x = 0; x < T2048_BOARD_SIZE; x++ )
              {
                  T2048Tile* p = &t2048Board[ y ][ x ];
                  int gx = 0;
                  int gy = 0;
                  if( p->merged && p->value >= t2048Counter )
                  {
                      int g = p->value - t2048Counter;
                      gx = t2048Random( g * 2 + 1 ) - g;
                      gy = g - abs( gx );
                      if( t2048Random( 2 ) )
                        gy = -gy;
                      isInvalid = true;
                  }
                  moveSprite( idx, x * T2048_IMG_TILE_W + gx, y * T2048_IMG_TILE_H + gy );
                  idx++;
              }

            if( t2048AddedIdx >= 0 && t2048Counter < 8 )
            {
                int targetY;
                if( ( t2048Counter & 1 ) != 0 )
                  targetY = t2048AddedIdx / T2048_BOARD_SIZE * T2048_IMG_TILE_H;
                else
                  targetY = HEIGHT;
                moveSprite( t2048AddedIdx, t2048AddedIdx % T2048_BOARD_SIZE * T2048_IMG_TILE_W, targetY );
                isInvalid = true;
            }
        }
    }
    else if( t2048State == T2048_STATE_MOVING )
    {
        if( t2048Counter <= 8 )
        {
            int idx = 0;
            for( int y = 0; y < T2048_BOARD_SIZE; y++ )
              for( int x = 0; x < T2048_BOARD_SIZE; x++ )
              {
                  T2048Tile* p = &t2048Board[ y ][ x ];
                  moveSprite( idx, x * T2048_IMG_TILE_W + p->move * t2048MoveVx * t2048Counter * 3,
                             y * T2048_IMG_TILE_H + p->move * t2048MoveVy * t2048Counter * 2 );
                  idx++;
              }
            isInvalid = true;
        }
    }
}

void t2048InitGame()
{
    initSprites();
    initStrings();
    setString( 0, 99, t2048LabelScore, WHITE );
    setString( 3, 105, t2048LabelTime, WHITE );
    t2048Score = 0;
    t2048ScoreString[ 6 ] = 0;
    t2048TimeMinutes = 0;
    t2048TimeSeconds = 0;
    t2048TimeFrames = 0;
    t2048TimeString[ 6 ] = 0;
    t2048UpdateTimeString();
    setString( 1, 93, t2048ScoreString, WHITE );
    setString( 4, 93, t2048TimeString, WHITE );

    t2048InitBoard();
    t2048AddRandomTile();
    t2048AddRandomTile();
    t2048UpdateTiles();
    t2048Counter = 0;
    t2048AddedIdx = -1;
    t2048State = T2048_STATE_IDLE;
    playScore( t2048SoundStart );
}

int t2048UpdateGame()
{
    int ret = T2048_MODE_GAME;

    if( t2048Counter < T2048_FPS )
      t2048Counter++;

    if( t2048State == T2048_STATE_IDLE )
    {
        t2048ForwardTime();
        t2048MoveVx = isButtonDown( RIGHT_BUTTON ) - isButtonDown( LEFT_BUTTON );
        t2048MoveVy = isButtonDown( DOWN_BUTTON ) - isButtonDown( UP_BUTTON );

        bool horizontalOnly = ( t2048MoveVx != 0 && t2048MoveVy == 0 );
        bool verticalOnly = ( t2048MoveVx == 0 && t2048MoveVy != 0 );

        if( horizontalOnly || verticalOnly )
        {
            if( t2048MoveTiles( t2048MoveVx, t2048MoveVy ) )
            {
                t2048Counter = 1;
                t2048State = T2048_STATE_MOVING;
            }
            else
            {
                if( t2048Counter < 18 )
                  isInvalid = true;
            }
        }
    }
    else if( t2048State == T2048_STATE_MOVING )
    {
        t2048ForwardTime();
        if( t2048Counter == 8 )
        {
            t2048AddRandomTile();
            t2048UpdateTiles();
            t2048Counter = 0;
            if( t2048IsGameOver() )
            {
                setString( 8, 96, t2048LabelGame, WHITE );
                setString( 9, 105, t2048LabelOver, WHITE );
                playScore( t2048SoundOver );
                t2048State = T2048_STATE_OVER;
            }
            else
            {
                t2048State = T2048_STATE_IDLE;
            }
        }
    }
    else if( t2048State == T2048_STATE_OVER )
    {
        if( isButtonDown( A_BUTTON ) )
          ret = T2048_MODE_TITLE;
    }

    t2048UpdateSprites();
    return ret;
}

void t2048DrawGame( int y, int* pBuffer )
{
    int tileRow = 0;
    if( ( y & 0x08 ) != 0 )
      tileRow = T2048_IMG_TILE_W - 1;

    int* p = &t2048ImgTile[ 0 ][ tileRow ];

    for( int i = 0; i < T2048_BOARD_SIZE; i++ )
    {
        for( int k = 0; k < T2048_IMG_TILE_W - 1; k++ )
          pBuffer[ i * T2048_IMG_TILE_W + k ] = p[ k ];
        pBuffer[ ( i + 1 ) * T2048_IMG_TILE_W - 1 ] = 0x00;
    }

    for( int k = T2048_IMG_TILE_W * T2048_BOARD_SIZE; k < WIDTH; k++ )
      pBuffer[ k ] = 0x00;
}

// -----------------------------------------------------------------------------
//   Top-level dispatch (replaces common.cpp's setup()/loop())
// -----------------------------------------------------------------------------

void gameT2048_init()
{
    t2048InitSoundMergeTable();
    initCore();
    t2048Mode = T2048_MODE_LOGO;
    t2048InitLogo();
}

void gameT2048_update()
{
    updateButtonState( 1 );

    int nextMode = T2048_MODE_LOGO;
    if( t2048Mode == T2048_MODE_LOGO )
      nextMode = t2048UpdateLogo();
    else if( t2048Mode == T2048_MODE_TITLE )
      nextMode = t2048UpdateTitle();
    else
      nextMode = t2048UpdateGame();

    DrawFunc* drawFn = NULL;
    if( t2048Mode == T2048_MODE_GAME )
      drawFn = &t2048DrawGame;
    refreshScreen( drawFn );

    if( t2048Mode != nextMode )
    {
        t2048Mode = nextMode;
        initSprites();
        initStrings();

        if( t2048Mode == T2048_MODE_LOGO )
          t2048InitLogo();
        else if( t2048Mode == T2048_MODE_TITLE )
          t2048InitTitle();
        else
          t2048InitGame();
    }
}

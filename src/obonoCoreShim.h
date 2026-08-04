#ifndef OBONOCORESHIM_H
#define OBONOCORESHIM_H

// -----------------------------------------------------------------------------
// Reproduces the public API of Obono's TinyJoypadWorks "core" (core.h/
// core.cpp - a small sprite/string compositing engine layered over raw
// SSD1306 pixel pushes) on top of machineDependent.h's md_* primitives, so
// games built on it (NumberPlace, HollowSeeker, t2048, ...) can keep their
// own game.cpp/title.cpp/logo.cpp almost untouched - only core.cpp's low-
// level tail (I2C, AVR button-ladder reads, an AVR hardware-timer tone
// sequencer, EEPROM) is actually replaced.
//
// Original overloaded setString() (flash-string vs RAM-string) collapses to
// one function here: __FlashStringHelper is aliased to a plain int in
// avrCompat.h, so both call shapes already produce the same argument type.
// -----------------------------------------------------------------------------

#define WIDTH  128
#define HEIGHT 64
#define PAGES  8

#define FONT_W 6
#define FONT_H 6

#define LEFT_BUTTON  bit(0)
#define RIGHT_BUTTON bit(1)
#define DOWN_BUTTON  bit(2)
#define UP_BUTTON    bit(3)
#define A_BUTTON     bit(4)

#define BLACK  0
#define WHITE  1
#define INVERT 2
#define DIRECT 3

typedef void(int, int*) DrawFunc;

void initCore();
void refreshScreen( DrawFunc* func );
void clearScreenBuffer();

// tickWindow: how many real Vircon32 frames pass between two calls to this
// function for the calling game - see machineDependent.h's
// md_recentlyPressed() and obonoCoreShim.c's own comment on this function.
void updateButtonState( int tickWindow );
bool isButtonPressed( int b );
bool isButtonDown( int b );
bool isButtonUp( int b );

void initSprites();
void setSprite( int idx, int x, int y, int* pBitmap, int w, int h, int color );
void moveSprite( int idx, int x, int y );
void clearSprite( int idx );
void drawSprites( int y );

void initStrings();
void setString( int idx, int x, int* pString, int color );
void clearString( int idx );
void drawStrings( int y );

void playTone( int frequency, int duration );
void playScore( int* pScore );

// Memory-card persistence (the original's EEPROM save/load) is deferred
// for this initial port - loadRecord() always reports "no saved record
// found" and storeRecord() is a no-op, so high scores/progress don't
// survive a cartridge restart yet. Games built on this still run and play
// correctly; only cross-session persistence is missing.
bool loadRecord( int signature, int address, void* pRecord, int size );
void storeRecord( int signature, int address, void* pRecord, int size );

extern bool isInvalid;

// Forces the next refreshScreen() call to actually redraw, regardless of
// whether anything in the game's own state changed. refreshScreen()
// skips its whole draw call outright when `isInvalid` is false (see
// obonoCoreShim.c) - fine during normal play since something always
// invalidates it again within a frame or two, but a game sitting on a
// static logo/title screen (nothing animates, so nothing re-invalidates)
// can leave `isInvalid` false indefinitely. Used as the shared onResume
// hook (see menu.h) for every obonoCoreShim-lineage game, so resuming
// from the quit-confirmation dialog can't leave the dialog's own pixels
// on screen instead of the game's.
void obonoCoreShimForceRedraw();

#define circulate(n, v, m) ( ( (n) + (v) + (m) ) % (m) )

#endif

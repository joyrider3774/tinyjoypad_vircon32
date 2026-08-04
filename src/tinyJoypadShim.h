#ifndef TINYJOYPADSHIM_H
#define TINYJOYPADSHIM_H

// -----------------------------------------------------------------------------
// Reproduces the public API of the Lorandil/phoenixbozo "tinyJoypadUtils" /
// "FastTinyDriver" driver lineage (tinyJoypadUtils.h/TinyJoypadUtils.h, used
// by Tiny Invaders, TinyDungeon, and other games from the same family) on
// top of machineDependent.h's md_* primitives, so those games' own source
// can be ported with their display/input/sound call sites left almost
// untouched.
// -----------------------------------------------------------------------------

void InitTinyJoypad();

// all three are equivalent here (addressing-mode selection is meaningless
// once there is no real SSD1306 to address) - kept so upstream call sites
// don't need editing
void InitDisplay();
void InitDisplayVertical();

void PrepareDisplayRow( int y );
void StartSendPixels();
void SendPixels( int pixels );
void StopSendPixels();
void FinishDisplayRow();
void DisplayBuffer();

bool isLeftPressed();
bool isRightPressed();
bool isUpPressed();
bool isDownPressed();
bool isFirePressed();

// A second, independent action button (Vircon32's B) - only TinyMinez uses
// this so far, as an alternate "instant flag toggle" shortcut alongside
// isFirePressed()'s own long-press-to-flag gesture. Every other
// tinyJoypadShim game only ever needed the one Fire button.
bool isFire2Pressed();

// freq/dur keep the original AVR delay-loop unit (freq: 0-254 raw "pitch",
// dur: number of half-cycles) - see tinyJoypadShim.c for the conversion to
// real Hz/seconds. Unlike the original (which busy-waits the whole ATtiny85
// CPU for the tone's duration), this returns immediately - see
// machineDependent.h's md_playTone().
void Sound( int freq, int dur );

#endif

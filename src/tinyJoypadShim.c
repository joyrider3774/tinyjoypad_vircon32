#include "avrCompat.h"
#include "tinyJoypadShim.h"
#include "machineDependent.h"

int tjShimCurrentPage = 0;
int tjShimCurrentCol = 0;

void InitTinyJoypad()
{
    // md_initVideo()/md_initAudio() already ran once, globally, before any
    // game started (see portVircon32.c's main()) - nothing left to do here.
}

void InitDisplay() {}
void InitDisplayVertical() {}

void PrepareDisplayRow( int y )
{
    if( y == 0 )
      md_beginFrame();

    tjShimCurrentPage = y;
    tjShimCurrentCol = 0;
}

void StartSendPixels() {}

void SendPixels( int pixels )
{
    md_drawColumn( tjShimCurrentCol, tjShimCurrentPage, pixels );
    tjShimCurrentCol++;
}

void StopSendPixels() {}
void FinishDisplayRow() {}
void DisplayBuffer() {}

bool isLeftPressed()  { return md_inputLeft(); }
bool isRightPressed() { return md_inputRight(); }
bool isUpPressed()    { return md_inputUp(); }
bool isDownPressed()  { return md_inputDown(); }
bool isFirePressed()  { return md_inputFire(); }
bool isFire2Pressed() { return md_inputFire2(); }

void Sound( int freq, int dur )
{
    // Mirrors the original bit-bang timing exactly (see
    // tinyJoypadUtils.cpp's Sound(): a square wave toggled every
    // (255-freq) microseconds, for `dur` full cycles) so ported games'
    // existing freq/dur call sites keep sounding the same, just played by
    // real SPU hardware (via md_playTone/PlayNote) instead of bit-banged.
    int periodUs = 255 - freq;
    if( periodUs < 1 )
      periodUs = 1;

    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;

    if( freq <= 0 )
    {
        // freq == 0 is a deliberate silent "rest" in the original - still
        // stop whatever is currently sounding, but play nothing new
        md_playTone( 0.0, durationSeconds );
        return;
    }

    md_playTone( freqHz, durationSeconds );
}

// =============================================================================
// Wren Rollercoaster (Andy Jackson, 2015-2017, non-commercial-with-
// attribution; ATtiny-Joypad port by Billy Cheung, 2018) - a Tiny-Wings
// style endless flyer: a bird glides over a scrolling sine-wave landscape,
// gaining speed/height boost by sliding through valleys and gently
// flapping over hills, until a fixed "distance" budget runs out.
//
// Structural notes:
// - Not `tinyJoypadShim`/`obonoCoreShim` lineage by name (its own hand-
//   rolled `ssd1306_send_byte()` bit-bang driver, confirmed via this
//   project's own earlier gametiny triage as needing "a new shim or per-
//   game adaptation") - but investigation before porting found this, like
//   Tiny Lander before it, doesn't actually need one: Billy Cheung's own
//   Tiny-Joypad button remap comment documents the exact same A0/A3
//   500-750/750-950 analog thresholds and digital-pin-1 fire button every
//   other game here already uses, and `ssd1306_send_byte()` is called
//   exactly once per column per page - the same "one byte per (column,
//   page)" model this whole project's `md_drawColumn()` already handles.
//   Ported straight onto `isLeftPressed()`/`isRightPressed()`/
//   `isFirePressed()` from `tinyJoypadShim`, no new shim needed.
// - Upstream never keeps a real framebuffer either - `drawLandscape()`/
//   `drawBird()` compute one SSD1306 page-byte at a time directly from
//   `landscape[128]` (a per-column terrain-height array) and the bird's
//   own Y position, using a byte-overlap-composite trick (`doDrawLS`/
//   `doDrawRS`/`doDrawLSP`/`doDrawRSP` - split-byte sprite blending, the
//   same shape as every other game's own `blitzSprite`-style split-byte
//   composite) - ported as one `wrenComputeColumn(x,y)` query function
//   matching `drawLandscape`'s own exact if/else-if chain, called once
//   per (column,page) from the shared render loop instead of upstream's
//   own per-row `ssd1306_send_byte()` stream.
// - A real VRAM-persistence partial-redraw assumption, found and fixed
//   proactively before ever compiling (matching the same class of bug
//   already found in Pinball/Doc/Bert/Tris/Pipe/Plaque): upstream's own
//   gameplay tick calls `drawBird(0,2)` (bird sprite ONLY, columns 8-15,
//   pages 0-1) and `drawLandscape(2,8)` (full composite, pages 2-7) -
//   pages 0-1's own columns 0-7 and 16-127 are *never* drawn during
//   normal play at all, relying on the real SSD1306's VRAM still holding
//   whatever was last written there (which, since nothing ever draws
//   there after the initial `ssd1306_fillscreen(0x00)`, is always black
//   in practice) - reproduced this exact visual result directly (`return
//   0` for that region in `wrenComputeColumn`) rather than needing an
//   actual persistence trick, since the intended appearance (blank sky)
//   is already known and constant.
// - `doDrawRS`/`doDrawLS`/`doDrawRSP`/`doDrawLSP`'s own small per-column
//   bird-sprite lookup (a `switch` upstream, cases 0-6 plus a default)
//   was ported as a flat `wrenBirdShape[8]` array instead, avoiding this
//   dialect's unverified `switch` support the same way every other port
//   here has (Doc/Bike's own established caution).
// - `floor(boost / 40)` and `floor(2 + speedBoost / 110)`: `boost` and
//   `speedBoost` are plain `int` upstream, and `40`/`110` are int
//   literals too, so `boost/40` is already a *truncating integer*
//   division before `floor()` ever sees it (floor of an already-integer
//   value is a no-op) - NOT a genuine float-floor operation the way the
//   landscape-height computation's `floor(62-height+height*sinfactor)`
//   genuinely is (`height`/`sinfactor` are real floats there). Ported the
//   first two as plain integer division (matching C's own truncate-
//   toward-zero `/`, identical on both AVR and Vircon32, no cross-
//   platform discrepancy to guard against) rather than literally calling
//   `floor()` on a float cast of an int quotient, which would have been
//   a needless, easy-to-get-subtly-wrong indirection for no behavioral
//   difference.
// - `random(min,max)` (Arduino's own exclusive-upper-bound range) ported
//   onto the shared `arand(n)` helper (`arand(max-min)+min`), matching
//   every other port's own random-range translation - `randomSeed(0)`
//   itself (a fixed, deterministic seed upstream - meaning the terrain
//   sequence is bit-identical between playthroughs on real hardware) has
//   no equivalent here, since Vircon32's own `rand()` isn't seedable the
//   same way - the terrain sequence will differ between playthroughs on
//   this port, a minor, unlikely-to-be-noticed deviation accepted rather
//   than engineering seedable determinism for a game whose terrain was
//   never meant to be memorized.
// - `beep(bCount, bDelay)` bit-bangs a square wave using raw NOP-loop
//   counts (not `_delay_us()`), so unlike ELECTROLIB.h's shared, already-
//   calibrated `Sound()` formula used throughout every other Daniel-C
//   game here, there is no exact NOP-count-to-real-Hz conversion to
//   reproduce faithfully - ported as a heuristic `wrenBeep()` mapping
//   (higher `bDelay` -> lower pitch, matching the qualitative intent)
//   onto the shared `Sound(freq,dur)`. The intro bounce animation's own
//   ~270-step tight loop (each step both redraws AND beeps at a
//   continuously-varying pitch) was simplified to one representative
//   beep per bounce cycle (3 total) rather than one per step - Vircon32's
//   queueless audio channel would only ever make the *last* of ~90 rapid
//   calls per cycle audible anyway (the same "collapses to the last
//   tone" finding already documented for every other oversized upstream
//   sound loop in this project), so reproducing every step's own call
//   would be pure wasted work for an inaudible result.
// - EEPROM high-score persistence dropped, matching every other port's
//   own precedent (`top` tracked in-memory for the cartridge session
//   only) - the "hold fire 2s to reset high score" secret menu action
//   still works, it just resets the session-local value instead of a
//   persisted one.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data: font (extracted + byte-diff verified against font6x8AJ.h)
// -----------------------------------------------------------------------------

int[378] wrenFONT =
{
0,0,0,0,0,0,0,8,8,8,8,8,0,0,96,96,0,0,
0,60,64,48,64,60,0,62,81,73,69,62,0,0,66,127,64,0,
0,66,97,81,73,70,0,33,65,69,75,49,0,24,20,18,127,16,
0,39,69,69,69,57,0,60,74,73,73,48,0,1,113,9,5,3,
0,54,73,73,73,54,0,6,73,73,41,30,0,0,54,54,0,0,
0,124,18,17,18,124,0,127,73,73,73,54,0,62,65,65,65,34,
0,127,65,65,34,28,0,127,73,73,73,65,0,127,9,9,9,1,
0,62,65,73,73,122,0,127,8,8,8,127,0,0,65,127,65,0,
0,32,64,65,63,1,0,127,8,20,34,65,0,127,64,64,64,64,
0,127,2,12,2,127,0,127,4,8,16,127,0,62,65,65,65,62,
0,127,9,9,9,6,0,62,65,81,33,94,0,127,9,25,41,70,
0,70,73,73,73,49,0,1,1,127,1,1,0,63,64,64,64,63,
0,31,32,64,32,31,0,63,64,56,64,63,0,99,20,8,20,99,
0,7,8,112,8,7,0,97,81,73,69,67,0,32,84,84,84,120,
0,127,72,68,68,56,0,56,68,68,68,32,0,56,68,68,72,127,
0,56,84,84,84,24,0,8,126,9,1,2,0,24,164,164,164,124,
0,28,160,160,160,124,0,0,68,125,64,0,0,64,128,132,125,0,
0,127,16,40,68,0,0,0,65,127,64,0,0,124,4,24,4,120,
0,124,8,4,4,120,0,56,68,68,68,56,0,252,36,36,36,24,
0,24,36,36,24,252,0,124,8,4,4,8,0,72,84,84,84,32,
0,4,63,68,64,32,0,60,64,64,32,124,0,28,32,64,32,28,
};

// Upstream's own bird sprite, one byte per column (0-7), used both
// right-shifted (doDrawRS-family) and left-shifted (doDrawLS-family)
// depending on which page half of the bird's position it lands in -
// ported as a flat array instead of upstream's switch(column) lookup,
// avoiding this dialect's unverified switch support.
int[8] wrenBirdShape =
{
0x3F, 0x7C, 0xFE, 0xE6, 0x66, 0x3C, 0x18, 0x10,
};

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

int wrenScore;
int wrenTop;
int wrenNewHigh;
int wrenMute;

int[128] wrenLandscape;

int wrenPlayerOffset;
int wrenLastPos;
int wrenBoost;
int wrenOnit;
int wrenSpeedBoost;
int wrenDoneUpdate;
int wrenTotalDistance;
int wrenInterscore;
int wrenGostep;
int wrenThisrun;
float wrenIncr;
float wrenSi;
float wrenHeight;

int wrenFireHeld;
int wrenFireHoldTicks;
int wrenSpecialActionDone;
int wrenAttractOverlay;   // 0=none, 1="HIGH SCORE RESET", 2="MUTE", 3="SOUND ON"

int wrenBounceK;
int wrenBounceDir;        // 0 = descending, 1 = ascending

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

void wrenBeep( int bCount, int bDelay )
{
    if( wrenMute ) return;
    int freq = 255 - bDelay;
    if( freq < 1 ) freq = 1;
    if( freq > 250 ) freq = 250;
    int dur = bCount + 1;
    if( dur > 40 ) dur = 40;
    Sound( freq, dur );
}

int wrenSeqActive;
int* wrenSeqNotes;
int wrenSeqCount;
int wrenSeqIndex;
int wrenSeqWaitFrames;

void wrenStartNoteSeq( int* notes, int count )
{
    wrenSeqNotes = notes;
    wrenSeqCount = count;
    wrenSeqIndex = 0;
    wrenSeqActive = 1;
    wrenSeqWaitFrames = 0;
}

void wrenAdvanceNoteSeq()
{
    if( !wrenSeqActive )
        return;
    if( wrenSeqWaitFrames > 0 )
    {
        wrenSeqWaitFrames--;
        return;
    }
    if( wrenSeqIndex >= wrenSeqCount )
    {
        wrenSeqActive = 0;
        return;
    }
    int freq = wrenSeqNotes[ wrenSeqIndex * 2 ];
    int dur = wrenSeqNotes[ wrenSeqIndex * 2 + 1 ];
    if( !wrenMute )
        Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    wrenSeqWaitFrames = waitFrames;
    wrenSeqIndex++;
}

// GAME OVER's own descending jingle: for(i=700;i>200;i-=50)beep(30,i);
// Converted to (freq,dur) pairs matching wrenBeep's own freq/dur mapping.
int[20] wrenGameOverNotes =
{
    -145,31, -95,31, -45,31, 5,31, 55,31, 105,31, 155,31, 205,31, 250,31, 250,31,
};
#define WREN_GAMEOVER_COUNT 10

// -----------------------------------------------------------------------------
//   Font / number rendering
// -----------------------------------------------------------------------------

int wrenCharIndex( int ch )
{
    int c = ch - 32;
    if( c > 0 ) c = c - 12;
    if( c > 15 ) c = c - 6;
    if( c > 40 ) c = c - 6;
    return c;
}

int wrenTextByte( int x, int y, int startX, int pageY, int* str )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    // The render loop calls this for every column on the matching row,
    // not just the columns the string itself actually occupies - x can
    // land past the string's real end while still being on the right
    // row. A bare `str[charIdx]==0` check only correctly stops AT the
    // exact null terminator; for charIdx values *past* it (reached once
    // x keeps increasing beyond the string's own length), it read
    // whatever undefined memory follows the string literal instead - not
    // guaranteed to be zero, so it could render as garbage "characters"
    // bleeding across the rest of the row. Bounding charIdx against the
    // string's own real length up front avoids ever reading past it.
    if( charIdx >= strlen( str ) ) return 0;
    int ch = str[charIdx];
    if( ch == 0 ) return 0;
    int within = rel - charIdx * 6;
    int fontIdx = wrenCharIndex( ch );
    return wrenFONT[ fontIdx * 6 + within ];
}

int wrenCountDigits( int value )
{
    if( value == 0 ) return 1;
    int v = value;
    if( v < 0 ) v = -v;
    int n = 0;
    while( v > 0 ) { n++; v = v / 10; }
    return n;
}

int wrenDigitAt( int value, int posFromLeft, int totalDigits )
{
    int v = value;
    if( v < 0 ) v = -v;
    int power = 1;
    int i;
    for( i = 0; i < totalDigits - 1 - posFromLeft; i++ )
        power = power * 10;
    return ( v / power ) % 10;
}

int wrenNumberByte( int x, int y, int startX, int pageY, int value )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int totalDigits = wrenCountDigits( value );
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= totalDigits ) return 0;
    int within = rel - charIdx * 6;
    int digit = wrenDigitAt( value, charIdx, totalDigits );
    int fontIdx = wrenCharIndex( 48 + digit );
    return wrenFONT[ fontIdx * 6 + within ];
}

// -----------------------------------------------------------------------------
//   Landscape / bird composite
// -----------------------------------------------------------------------------

int wrenDoDrawLS( int p2 )
{
    return 3 << p2;
}

int wrenDoDrawRS( int p2 )
{
    return 3 >> p2;
}

int wrenBirdShapeLS( int column, int p2 )
{
    return wrenBirdShape[column] << p2;
}

int wrenBirdShapeRS( int column, int p2 )
{
    return wrenBirdShape[column] >> p2;
}

int wrenComputeColumn( int x, int y )
{
    if( x >= 127 ) return 0;

    int pLine = wrenPlayerOffset / 8;
    int pOff = wrenPlayerOffset % 8;

    if( y < 2 )
    {
        // Matches upstream's own drawBird(0,2) call during gameplay -
        // only columns 8-15 (the bird) are ever drawn on these two pages;
        // everything else stays blank, matching the always-black sky
        // upstream relies on real SSD1306 VRAM persistence to show.
        if( x < 8 || x > 15 ) return 0;
        int birdCol = x - 8;
        if( y == pLine ) return wrenBirdShapeLS( birdCol, pOff );
        if( y == pLine + 1 ) return wrenBirdShapeRS( birdCol, 8 - pOff );
        return 0;
    }

    // Matches upstream's own drawLandscape(2,8) full composite.
    int yHeight = wrenLandscape[x];
    int yLine = yHeight / 8;
    int yOff = yHeight % 8;

    if( x < 8 || x > 15 )
    {
        if( y == yLine ) return wrenDoDrawLS( yOff );
        if( y == yLine + 1 ) return wrenDoDrawRS( 8 - yOff );
        return 0;
    }

    int birdCol = x - 8;
    if( y == yLine && y != pLine && y != pLine + 1 )
        return wrenDoDrawLS( yOff );
    if( y == yLine + 1 && y != pLine && y != pLine + 1 )
        return wrenDoDrawRS( 8 - yOff );
    if( y != yLine + 1 && y != yLine && y == pLine )
        return wrenBirdShapeLS( birdCol, pOff );
    if( y != yLine + 1 && y != yLine && y == pLine + 1 )
        return wrenBirdShapeRS( birdCol, 8 - pOff );
    if( y == yLine && y == pLine )
        return wrenBirdShapeLS( birdCol, pOff ) | wrenDoDrawLS( yOff );
    if( y == yLine + 1 && y == pLine + 1 )
        return wrenBirdShapeRS( birdCol, 8 - pOff ) | wrenDoDrawRS( 8 - yOff );
    if( y == yLine && y == pLine + 1 )
        return wrenBirdShapeRS( birdCol, 8 - pOff ) | wrenDoDrawLS( yOff );
    if( y == yLine + 1 && y == pLine )
        return wrenBirdShapeLS( birdCol, pOff ) | wrenDoDrawRS( 8 - yOff );
    return 0;
}

int wrenTimeBarByte( int x, int y )
{
    if( y != 0 ) return 0;
    if( x == 56 ) return 0xFF;
    if( x < 40 || x > 72 ) return 0;
    if( x == 40 || x == 72 ) return 0xFF;
    float threshold = 0.016 * (float)( 2000 - wrenTotalDistance );
    int sc = x - 40;
    if( (float)sc < threshold ) return 0xBD;
    return 0x81;
}

// -----------------------------------------------------------------------------
//   Render (one composite function per screen/mode, matching upstream's
//   own distinct full-screen redraws)
// -----------------------------------------------------------------------------

#define WREN_MODE_ATTRACT 0
#define WREN_MODE_BOUNCE 1
#define WREN_MODE_PLAYING 2
#define WREN_MODE_GAMEOVER 3
#define WREN_MODE_NEWHIGH 4

void wrenRenderFrame( int mode )
{
    int x, y, val;
    md_beginFrame();
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
        {
            // Every text/number layer below is self-gated to one exact
            // row internally (wrenTextByte/wrenNumberByte both early-
            // return on `y != pageY`), but a self-gated call still costs
            // a full call every time it's invoked - the same lesson this
            // project keeps finding - so each call is also gated by row
            // at the call site here, from the start rather than as a
            // later retrofit.
            val = 0;
            if( mode == WREN_MODE_ATTRACT )
            {
                if( y == 1 ) val = val | wrenTextByte( x, y, 8, 1, "   --------------" );
                else if( y == 2 ) val = val | wrenTextByte( x, y, 8, 2, "    W R E N      " );
                else if( y == 4 ) val = val | wrenTextByte( x, y, 8, 4, "    ROLLERCOASTER" );
                else if( y == 5 ) val = val | wrenTextByte( x, y, 8, 5, "   --------------" );
                else if( y == 7 ) val = val | wrenTextByte( x, y, 8, 7, "    andh jackson " );
                else if( y == 0 )
                {
                    if( wrenAttractOverlay == 1 )
                        val = val | wrenTextByte( x, y, 8, 0, "-HIGH SCORE RESET-" );
                    else if( wrenAttractOverlay == 2 )
                        val = val | wrenTextByte( x, y, 32, 0, "-- MUTE --" );
                    else if( wrenAttractOverlay == 3 )
                        val = val | wrenTextByte( x, y, 23, 0, "-- SOUND ON --" );
                }
            }
            else if( mode == WREN_MODE_BOUNCE )
            {
                if( x >= 8 && x <= 15 )
                {
                    int birdCol = x - 8;
                    int pLine = wrenPlayerOffset / 8;
                    int pOff = wrenPlayerOffset % 8;
                    if( y == pLine ) val = wrenBirdShapeLS( birdCol, pOff );
                    else if( y == pLine + 1 ) val = wrenBirdShapeRS( birdCol, 8 - pOff );
                }
            }
            else if( mode == WREN_MODE_PLAYING )
            {
                val = wrenComputeColumn( x, y );
                if( y == 0 )
                    val = val | wrenTimeBarByte( x, y ) | wrenNumberByte( x, y, 85, 0, wrenScore );
            }
            else if( mode == WREN_MODE_GAMEOVER )
            {
                if( y == 1 ) val = val | wrenTextByte( x, y, 11, 1, "----------------" );
                else if( y == 2 ) val = val | wrenTextByte( x, y, 11, 2, "G A M E  O V E R" );
                else if( y == 3 ) val = val | wrenTextByte( x, y, 11, 3, "----------------" );
                else if( y == 5 )
                    val = val | wrenTextByte( x, y, 37, 5, "SCORE:" ) | wrenNumberByte( x, y, 75, 5, wrenScore );
                else if( y == 7 && !wrenNewHigh )
                    val = val | wrenTextByte( x, y, 21, 7, "HIGH SCORE:" ) | wrenNumberByte( x, y, 88, 7, wrenTop );
            }
            else if( mode == WREN_MODE_NEWHIGH )
            {
                if( y == 1 ) val = val | wrenTextByte( x, y, 10, 1, "----------------" );
                else if( y == 3 ) val = val | wrenTextByte( x, y, 10, 3, " NEW HIGH SCORE " );
                else if( y == 7 ) val = val | wrenTextByte( x, y, 10, 7, "----------------" );
                else if( y == 5 ) val = val | wrenNumberByte( x, y, 50, 5, wrenTop );
            }
            md_drawColumn( x, y, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define WREN_STATE_ATTRACT 0
#define WREN_STATE_BOUNCE 1
#define WREN_STATE_PLAYING 2
#define WREN_STATE_WIN_LANDING 3
#define WREN_STATE_WIN_PAUSE1 4
#define WREN_STATE_WIN_PAUSE2 5
#define WREN_STATE_GAMEOVER_WAIT 6
#define WREN_STATE_NEWHIGH_WAIT 7

#define WREN_BOUNCE_STEP 3

int wrenState;
int wrenWaitFrames;

void wrenBeginAttract()
{
    wrenFireHeld = 0;
    wrenFireHoldTicks = 0;
    wrenSpecialActionDone = 0;
    wrenAttractOverlay = 0;
    wrenState = WREN_STATE_ATTRACT;
}

void wrenBeginBounce()
{
    wrenBounceK = 2;
    wrenBounceDir = 0;
    wrenPlayerOffset = 55;
    wrenBeep( 2, 200 + wrenPlayerOffset );
    wrenState = WREN_STATE_BOUNCE;
}

void wrenBeginWinLanding()
{
    wrenState = WREN_STATE_WIN_LANDING;
}

void wrenBeginGameOverSequence()
{
    wrenRenderFrame( WREN_MODE_GAMEOVER );
    wrenStartNoteSeq( wrenGameOverNotes, WREN_GAMEOVER_COUNT );
    wrenWaitFrames = 120;
    wrenState = WREN_STATE_GAMEOVER_WAIT;
}

void wrenBeginPlaying()
{
    wrenTotalDistance = 0;
    wrenInterscore = 0;
    wrenSi = 0.05;
    wrenGostep = 5;
    wrenHeight = 20.0;
    wrenPlayerOffset = 0;
    wrenLastPos = 0;
    wrenBoost = 0;
    wrenOnit = 0;
    wrenSpeedBoost = 0;
    wrenDoneUpdate = 0;
    wrenNewHigh = 0;
    wrenScore = 0;
    wrenThisrun = 0;

    wrenIncr = 3.14159265;
    int i;
    for( i = 0; i < 128; i++ )
    {
        float sinfactor = sin( wrenIncr );
        wrenLandscape[i] = (int)floor( 62.0 - wrenHeight + ( wrenHeight * sinfactor ) );
        wrenIncr += wrenSi;
    }

    wrenState = WREN_STATE_PLAYING;
}

void wrenPlayingTick()
{
    wrenTotalDistance++;
    wrenInterscore += wrenGostep;
    if( wrenInterscore >= 10 ) { wrenInterscore = 0; wrenScore++; }

    if( isLeftPressed() || isRightPressed() )
    {
        wrenThisrun++;
        wrenPlayerOffset += 2;
        wrenBoost -= 10;
    }
    else
    {
        wrenThisrun = 0;
    }

    if( wrenOnit && wrenLastPos < wrenPlayerOffset )
    {
        wrenSpeedBoost += 10;
        wrenBoost += 7;
    }
    else
    {
        if( wrenThisrun > 30 )
        {
            wrenBoost -= wrenThisrun;
            wrenSpeedBoost -= wrenThisrun;
        }
    }
    wrenLastPos = wrenPlayerOffset;

    if( isFirePressed() )
        wrenPlayerOffset -= 1;

    if( ( wrenPlayerOffset > 45 ) && wrenOnit && ( isLeftPressed() || isRightPressed() ) )
    {
        wrenBoost += 75;
        wrenSpeedBoost += 100;
        wrenBeep( 1, 350 - wrenSpeedBoost );
    }
    else
    {
        // floor(boost/40) upstream: boost/40 is already a truncating
        // integer division before floor() ever sees it - see header
        // comment. Plain int division, no float floor needed.
        wrenPlayerOffset -= wrenBoost / 40;
    }

    if( wrenSpeedBoost > 700 ) wrenSpeedBoost = 700;
    if( wrenBoost > 450 ) wrenBoost = 450;

    wrenPlayerOffset += 3;

    if( wrenBoost > 0 && wrenOnit != 1 ) wrenBoost -= 20;
    if( wrenBoost < 0 ) wrenBoost = 0;

    if( wrenSpeedBoost > 0 ) wrenSpeedBoost -= 7;
    if( wrenSpeedBoost < 0 ) wrenSpeedBoost = 0;

    // floor(2+speedBoost/110) upstream: same reasoning as above - already
    // an integer division before floor() ever sees it.
    wrenGostep = 2 + wrenSpeedBoost / 110;

    wrenOnit = 0;
    if( wrenPlayerOffset >= wrenLandscape[17] - 8 )
    {
        wrenOnit = 1;
        wrenPlayerOffset = wrenLandscape[17] - 8;
    }
    if( wrenPlayerOffset <= 0 ) wrenPlayerOffset = 0;

    if( wrenIncr >= 6.2831853 ) wrenIncr -= 6.2831853;

    int i;
    for( i = 0; i < 127 - wrenGostep; i++ )
        wrenLandscape[i] = wrenLandscape[i + wrenGostep];
    for( i = 127 - wrenGostep; i < 128; i++ )
    {
        float sinfactor = sin( wrenIncr );
        if( sinfactor < -0.2 ) sinfactor = -0.2 - ( -0.2 - sinfactor ) / 2.0;
        wrenLandscape[i] = (int)floor( 62.0 - wrenHeight + ( wrenHeight * sinfactor ) );
        if( sinfactor < 0.90 ) wrenDoneUpdate = 0;
        if( sinfactor > 0.97 )
        {
            if( wrenDoneUpdate == 0 )
            {
                int newheight = arand(18) + 7;
                while( (float)newheight > wrenHeight - 7.0 && (float)newheight < wrenHeight + 7.0 )
                    newheight = arand(18) + 7;
                wrenHeight = (float)newheight;
                wrenSi = (float)( arand(40) + 35 ) / 1000.0;
                wrenDoneUpdate = 1;
            }
        }
        wrenIncr += wrenSi;
    }

    wrenRenderFrame( WREN_MODE_PLAYING );

    if( wrenScore == 950 && wrenInterscore == 0 ) wrenTotalDistance -= 900;
    else if( wrenScore == 1450 && wrenInterscore == 0 ) wrenTotalDistance -= 900;
    else if( wrenScore == 1950 && wrenInterscore == 0 ) wrenTotalDistance -= 900;
    else if( wrenScore == 2400 && wrenInterscore == 0 ) wrenTotalDistance -= 900;
    if( wrenTotalDistance < 0 ) wrenTotalDistance = 0;

    if( wrenTotalDistance >= 2000 )
        wrenBeginWinLanding();
}

// -----------------------------------------------------------------------------
//   Top-level
// -----------------------------------------------------------------------------

void gameWrenRollercoaster_init()
{
    wrenTop = 0;
    wrenMute = 0;
    wrenSeqActive = 0;
    wrenBeginAttract();
}

void gameWrenRollercoaster_forceRedraw()
{
    if( wrenState == WREN_STATE_ATTRACT ) wrenRenderFrame( WREN_MODE_ATTRACT );
    else if( wrenState == WREN_STATE_BOUNCE ) wrenRenderFrame( WREN_MODE_BOUNCE );
    else if( wrenState == WREN_STATE_GAMEOVER_WAIT ) wrenRenderFrame( WREN_MODE_GAMEOVER );
    else if( wrenState == WREN_STATE_NEWHIGH_WAIT ) wrenRenderFrame( WREN_MODE_NEWHIGH );
    else wrenRenderFrame( WREN_MODE_PLAYING );
}

void gameWrenRollercoaster_update()
{
    wrenAdvanceNoteSeq();

    if( wrenState == WREN_STATE_ATTRACT )
    {
        int fireDown = isFirePressed();
        if( fireDown )
        {
            wrenFireHoldTicks++;
            if( wrenFireHoldTicks >= 120 && !wrenSpecialActionDone )
            {
                wrenSpecialActionDone = 1;
                if( isLeftPressed() || isRightPressed() )
                {
                    wrenTop = 0;
                    wrenAttractOverlay = 1;
                }
                else if( wrenMute == 0 )
                {
                    wrenMute = 1;
                    wrenAttractOverlay = 2;
                }
                else
                {
                    wrenMute = 0;
                    wrenAttractOverlay = 3;
                }
            }
        }
        else
        {
            if( wrenFireHeld && !wrenSpecialActionDone )
            {
                wrenFireHeld = fireDown;
                wrenBeginBounce();
                return;
            }
            if( wrenFireHeld )
                wrenAttractOverlay = 0;
            wrenFireHoldTicks = 0;
            wrenSpecialActionDone = 0;
        }
        wrenFireHeld = fireDown;
        wrenRenderFrame( WREN_MODE_ATTRACT );
    }
    else if( wrenState == WREN_STATE_BOUNCE )
    {
        int targetLow = wrenBounceK * 10;
        if( wrenBounceDir == 0 )
        {
            wrenPlayerOffset -= WREN_BOUNCE_STEP;
            if( wrenPlayerOffset <= targetLow )
            {
                wrenPlayerOffset = targetLow;
                wrenBounceDir = 1;
            }
        }
        else
        {
            wrenPlayerOffset += WREN_BOUNCE_STEP;
            if( wrenPlayerOffset >= 55 )
            {
                wrenPlayerOffset = 55;
                wrenBounceDir = 0;
                wrenBounceK--;
                if( wrenBounceK < 0 )
                {
                    wrenBeginPlaying();
                    return;
                }
                wrenBeep( 2, 200 + wrenPlayerOffset );
            }
        }
        wrenRenderFrame( WREN_MODE_BOUNCE );
    }
    else if( wrenState == WREN_STATE_PLAYING )
    {
        wrenPlayingTick();
    }
    else if( wrenState == WREN_STATE_WIN_LANDING )
    {
        if( wrenPlayerOffset < wrenLandscape[17] - 8 )
        {
            wrenPlayerOffset++;
            wrenRenderFrame( WREN_MODE_PLAYING );
        }
        else
        {
            wrenWaitFrames = 60;
            wrenState = WREN_STATE_WIN_PAUSE1;
        }
    }
    else if( wrenState == WREN_STATE_WIN_PAUSE1 )
    {
        if( wrenWaitFrames > 0 ) wrenWaitFrames--;
        else { wrenWaitFrames = 90; wrenState = WREN_STATE_WIN_PAUSE2; }
    }
    else if( wrenState == WREN_STATE_WIN_PAUSE2 )
    {
        if( wrenWaitFrames > 0 ) wrenWaitFrames--;
        else
        {
            if( wrenScore > wrenTop ) { wrenTop = wrenScore; wrenNewHigh = 1; }
            else { wrenNewHigh = 0; }
            wrenBeginGameOverSequence();
        }
    }
    else if( wrenState == WREN_STATE_GAMEOVER_WAIT )
    {
        if( wrenWaitFrames > 0 ) wrenWaitFrames--;
        else
        {
            if( wrenNewHigh )
            {
                wrenRenderFrame( WREN_MODE_NEWHIGH );
                wrenStartNoteSeq( wrenGameOverNotes, WREN_GAMEOVER_COUNT );
                wrenWaitFrames = 162;
                wrenState = WREN_STATE_NEWHIGH_WAIT;
            }
            else
            {
                wrenBeginAttract();
            }
        }
    }
    else if( wrenState == WREN_STATE_NEWHIGH_WAIT )
    {
        if( wrenWaitFrames > 0 ) wrenWaitFrames--;
        else wrenBeginAttract();
    }
}

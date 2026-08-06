// =============================================================================
// UFO (Ilya Titov, non-commercial-with-attribution; ATtiny-Joypad port by
// Billy Cheung, 2018; modified by Andy Jackson to fit alongside Stacker in
// one cartridge) - a Flappy-Bird-style flyer: hold up/down to fly, release
// to fall, weaving through gaps in oncoming obstacle walls; some gaps have
// a destructible dithered barrier that only clears if you're aligned with
// it and fire an active shot as it passes.
//
// From `more games/gametiny/UFO_Stacker_Attiny/` - the other half of the
// same combined cartridge Stacker was already ported from (see
// `gameStacker.c`'s own header for the full context on why this became two
// separate menu entries rather than one). This game's own font, credit-
// string substitutions, and shared game-over/new-high screens are the
// exact same upstream code Stacker's own port already had to replicate -
// each split-out game keeps its own self-contained copy, matching this
// project's established precedent (no cross-game-file sharing mechanism
// exists here).
//
// Structural notes:
// - Not `tinyJoypadShim`/`obonoCoreShim` lineage by name, but needed no
//   new shim - same A0/A3/fire-pin thresholds as every other game here.
//   Upstream's own up/down check (`analogRead(upDownPin)<950`) matches
//   *either* up or down being pressed with no distinction between them -
//   both make the ship fly up, released lets it fall - ported as
//   `isUpPressed() || isDownPressed()`, matching the same input-
//   redundancy-preservation approach as Stacker's own fire/up/down lock
//   trigger.
// - **A shift-safety rewrite, not a literal port, for the obstacle wall/
//   gap byte math**: upstream's `B11111111>>((row+1)*8-gapOffset[i])`
//   (and its `<<` sibling for the bottom transition) can receive a
//   *negative* shift amount whenever a gap's offset falls outside the
//   specific page being drawn (a real, reachable case - `gapOffset` can
//   exceed 8 while `row==0`, giving `(0+1)*8-gapOffset` well below zero,
//   yet still satisfying upstream's own `<=8` guard) - AVR's own behavior
//   for a variable negative shift count here isn't something this project
//   can safely assume, let alone replicate (this is a *different*
//   mechanism from the already-documented logical-vs-arithmetic-shift and
//   shift-count-wraparound bug classes - here the shift *amount itself*
//   can be negative, not just large). Rather than trying to preserve a
//   formula whose correctness depends on an unverified AVR quirk, the
//   wall/gap byte for a given page is computed directly with a small
//   fixed-range (0-7) per-pixel loop instead - independently correct by
//   construction, no shift-amount risk at all.
// - The dithered "gap is blocked" overlay (`tempB=0b10101010`, OR'd onto
//   the 2 middle columns of each 4-wide obstacle segment) has no visible
//   effect wherever the base byte is already solid wall (OR-ing more bits
//   into 0xFF changes nothing) - it only actually shows up over the gap's
//   own otherwise-clear area, which is exactly the intended "a barrier
//   floats in the gap" look. Ported with the same OR-based layering.
// - The player ship + its fire-trail are one continuous 35-byte sequence
//   upstream (8 ship bytes, masked by a 2-state `flameMask` for the
//   thrust-flame effect, followed by up to 27 trail bytes when a shot is
//   active) - ported as one `ufoShipTrailByte(idx)` lookup instead of
//   duplicating the ship-vs-trail distinction at every call site,
//   preserving upstream's own single-pass column layout (columns 8-42).
// - The attract screen's 30 "stars" are generated *once* upstream (part
//   of the combined cartridge's own one-time boot splash, never
//   regenerated) - upstream relies on real SSD1306 VRAM to keep showing
//   them indefinitely afterward; this project's always-clear-then-redraw
//   model needs them stored (not regenerated every frame, which would
//   make them flicker/jump instead of sitting still) - generated once via
//   `arand()` when entering the attract state, cached, and redrawn from
//   that cache every frame.
// - EEPROM high-score persistence restored (see the project-wide "Real
//   persistent high-score saving" section in CLAUDE.md - a 2-byte big-
//   endian score at address 0/1, matching upstream exactly; note this
//   game's own live score can go negative, per the clamps documented
//   below, but `ufoTop` itself can never actually be written negative,
//   since a negative live score can never exceed its own starting value
//   of 0).
// - **Two defensive clamps added beyond a faithful translation**, since
//   UFO's own score (unlike every other port's) can genuinely go
//   negative in real play (firing costs a point, `if(fire==1)score--;`,
//   with no floor) - `ufoBlockChance` and `ufoMaxGap` are both later fed
//   into `arand()` as a range (`arand(ufoBlockChance)`,
//   `arand(ufoMaxGap-25)`), and upstream's own formulas for both
//   (`11-score/50`, `60-score/100`) can drive them to zero or negative
//   once score is deeply negative - clamped to a safe positive floor
//   (`ufoBlockChance>=1`, `ufoMaxGap>=26`) rather than risk `arand()`
//   receiving a degenerate (zero/negative) range, something no other
//   port here needed to guard against since their own scores stay
//   non-negative by construction.
// - **8-bit-vs-32-bit audit came back clean**: every shift is either a
//   small fixed constant (the dither pattern's `>>1`) or bounded to 0-7
//   by construction (`playerOffset%8`, `arand(7)`'s own star-bit range),
//   all explicitly `&0xFF`-masked; the one genuinely risky shift-based
//   formula upstream had (the obstacle wall/gap byte, discussed above)
//   was avoided entirely via the per-pixel reimplementation instead of
//   patched with a mask, since the risk there was the shift *amount*
//   going negative, not the shifted *value* overflowing a byte.
// - **A real O(pixels x objects) optimization applied proactively during
//   this same session's audit pass, not retrofitted after a report**:
//   the obstacle list (up to 5) and the star field (30) were each
//   initially checked with their own per-object loop *inside* the 1024-
//   pixel/frame render loop (5120 and 30720 iterations/frame
//   respectively) - the same shape this project has repeatedly found and
//   fixed in other multi-object games (Bomber/Pacman/Missile/Frogger).
//   Fixed by compositing each into a shared `ufoPageBuffer[128]` once per
//   page/row instead (40 and 240 iterations/frame respectively), the same
//   fix shape applied elsewhere. The score's own digit count is also
//   cached once per frame (`ufoScoreByte`, mirroring Stacker's own
//   `stkScoreByte`) rather than recomputed on every one of the 128
//   columns in its row, since UFO's score is unbounded like Stacker's,
//   not single-digit-capped like Bat Bonanza's.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data: font (same font6x8AJ.h as gameStacker.c - this combined
//   cartridge's other half - re-declared here since games don't share
//   data across files in this project)
// -----------------------------------------------------------------------------

int[360] ufoFONT =
{
0,0,0,0,0,0,0,8,8,8,8,8,0,0,96,96,
0,0,0,60,64,48,64,60,0,62,81,73,69,62,0,0,
66,127,64,0,0,66,97,81,73,70,0,33,65,69,75,49,
0,24,20,18,127,16,0,39,69,69,69,57,0,60,74,73,
73,48,0,1,113,9,5,3,0,54,73,73,73,54,0,6,
73,73,41,30,0,0,54,54,0,0,0,124,18,17,18,124,
0,127,73,73,73,54,0,62,65,65,65,34,0,127,65,65,
34,28,0,127,73,73,73,65,0,127,9,9,9,1,0,62,
65,73,73,122,0,127,8,8,8,127,0,0,65,127,65,0,
0,32,64,65,63,1,0,127,8,20,34,65,0,127,64,64,
64,64,0,127,2,12,2,127,0,127,4,8,16,127,0,62,
65,65,65,62,0,127,9,9,9,6,0,62,65,81,33,94,
0,127,9,25,41,70,0,70,73,73,73,49,0,1,1,127,
1,1,0,63,64,64,64,63,0,31,32,64,32,31,0,63,
64,56,64,63,0,32,84,84,84,120,0,127,72,68,68,56,
0,56,68,68,68,32,0,56,68,68,72,127,0,56,84,84,
84,24,0,127,8,4,4,120,0,24,164,164,164,124,0,28,
160,160,160,124,0,0,68,125,64,0,0,64,128,132,125,0,
0,127,16,40,68,0,0,0,65,127,64,0,0,124,4,24,
4,120,0,124,8,4,4,120,0,56,68,68,68,56,0,252,
36,36,36,24,0,24,36,36,24,252,0,124,8,4,4,8,
0,72,84,84,84,32,0,4,63,68,64,32,0,60,64,64,
32,124,0,28,32,64,32,28,
};

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

int ufoMute;

void ufoBeepOnce( int bCount, int bDelay )
{
    if( ufoMute ) return;
    int scaled = bDelay;
    if( scaled > 1000 ) scaled = 1000;
    if( scaled < 0 ) scaled = 0;
    scaled = scaled * 250 / 1000;
    int freq = 255 - scaled;
    if( freq < 1 ) freq = 1;
    if( freq > 250 ) freq = 250;
    int dur = bCount + 1;
    if( dur > 40 ) dur = 40;
    Sound( freq, dur );
}

int ufoSeqActive;
int* ufoSeqNotes;
int ufoSeqCount;
int ufoSeqIndex;
int ufoSeqWaitFrames;

void ufoStartNoteSeq( int* notes, int count )
{
    ufoSeqNotes = notes;
    ufoSeqCount = count;
    ufoSeqIndex = 0;
    ufoSeqActive = 1;
    ufoSeqWaitFrames = 0;
}

void ufoAdvanceNoteSeq()
{
    if( !ufoSeqActive ) return;
    if( ufoSeqWaitFrames > 0 ) { ufoSeqWaitFrames--; return; }
    if( ufoSeqIndex >= ufoSeqCount ) { ufoSeqActive = 0; return; }
    int freq = ufoSeqNotes[ ufoSeqIndex * 2 ];
    int dur = ufoSeqNotes[ ufoSeqIndex * 2 + 1 ];
    if( !ufoMute ) Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    ufoSeqWaitFrames = waitFrames;
    ufoSeqIndex++;
}

// for(cp=400;cp>0;cp--) beep(1,cp) - a genuinely huge 400-call computed
// sweep played when a blocked gap is destroyed - downsampled (step 20)
// to a real, audible ~0.3s sweep instead of 400 frame-stepped notes.
int[40] ufoGapDestroyNotes =
{
155,2,160,2,165,2,170,2,175,2,180,2,185,2,190,2,195,2,200,2,
205,2,210,2,215,2,220,2,225,2,230,2,235,2,240,2,245,2,250,2,
};
#define UFO_GAPDESTROY_COUNT 20

// for(i=0;i<1000;i+=50) beep(50,i) - game over
int[40] ufoGameOverNotes =
{
250,40,243,40,230,40,218,40,205,40,193,40,180,40,168,40,155,40,143,40,
130,40,118,40,105,40,93,40,80,40,68,40,55,40,43,40,30,40,18,40,
};
#define UFO_GAMEOVER_COUNT 20

// for(i=700;i>200;i-=50) beep(30,i) - new high score
int[20] ufoNewHighNotes =
{
80,31,93,31,105,31,118,31,130,31,143,31,155,31,168,31,180,31,193,31,
};
#define UFO_NEWHIGH_COUNT 10

// -----------------------------------------------------------------------------
//   Font / number rendering
// -----------------------------------------------------------------------------

int ufoCharIndex( int ch )
{
    int c = ch - 32;
    if( c > 0 ) c = c - 12;
    if( c > 15 ) c = c - 6;
    if( c > 40 ) c = c - 9;
    return c;
}

int ufoTextByteLen( int x, int y, int startX, int pageY, int* str, int strLen )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= strLen ) return 0;
    int ch = str[charIdx];
    if( ch == 0 ) return 0;
    int within = rel - charIdx * 6;
    int fontIdx = ufoCharIndex( ch );
    return ufoFONT[ fontIdx * 6 + within ];
}

// Handles a possible leading minus sign - unlike every other port's own
// score (always non-negative), UFO's score genuinely can go negative
// (firing costs a point) during real play, not just as a theoretical edge
// case, so this needs proper signed rendering from the start.
int ufoCountDigits( int value )
{
    int v = value;
    if( v < 0 ) v = -v;
    if( v == 0 ) return 1;
    int n = 0;
    while( v > 0 ) { n++; v = v / 10; }
    return n;
}

int ufoDigitAt( int value, int posFromLeft, int totalDigits )
{
    int v = value;
    if( v < 0 ) v = -v;
    int power = 1;
    int i;
    for( i = 0; i < totalDigits - 1 - posFromLeft; i++ ) power = power * 10;
    return ( v / power ) % 10;
}

int ufoNumberByte( int x, int y, int startX, int pageY, int value )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int neg = ( value < 0 );
    int totalDigits = ufoCountDigits( value );
    int rel = x - startX;
    if( neg )
    {
        if( rel < 6 ) return ufoFONT[ ufoCharIndex( 45 ) * 6 + rel ]; // '-' glyph
        rel -= 6;
    }
    int charIdx = rel / 6;
    if( charIdx >= totalDigits ) return 0;
    int within = rel - charIdx * 6;
    int digit = ufoDigitAt( value, charIdx, totalDigits );
    int fontIdx = ufoCharIndex( 48 + digit );
    return ufoFONT[ fontIdx * 6 + within ];
}

// Same as ufoNumberByte(), but takes the digit count pre-computed once
// per frame instead of recomputing it on every one of the 128 columns in
// the score's own row - like Stacker's score (and unlike Bat Bonanza's
// WINSCORE-capped one), UFO's score grows unbounded, so this is a real,
// avoidable per-pixel cost, the same class already fixed via Frogger's
// own frgScoreDigits/Stacker's stkScoreByte.
int ufoScoreByte( int x, int y, int startX, int pageY, int value, int totalDigits )
{
    if( y != pageY ) return 0;
    if( x < startX ) return 0;
    int neg = ( value < 0 );
    int rel = x - startX;
    if( neg )
    {
        if( rel < 6 ) return ufoFONT[ ufoCharIndex( 45 ) * 6 + rel ];
        rel -= 6;
    }
    int charIdx = rel / 6;
    if( charIdx >= totalDigits ) return 0;
    int within = rel - charIdx * 6;
    int digit = ufoDigitAt( value, charIdx, totalDigits );
    int fontIdx = ufoCharIndex( 48 + digit );
    return ufoFONT[ fontIdx * 6 + within ];
}

// -----------------------------------------------------------------------------
//   Game state (direct translation of upstream's own globals - all plain
//   int/byte/bool upstream too, no narrow-type wraparound-reliance
//   concerns beyond the shift-safety rewrite documented above)
// -----------------------------------------------------------------------------

#define UFO_MAX_OBSTACLES_ARR 9

int[9] ufoObstacleX;
int[9] ufoGapOffset;
int[9] ufoGapSize;
int[9] ufoGapBlock;

int ufoMaxObstacles;
int ufoObstacleStep;
int ufoBlockChance;
int ufoMaxGap;
int ufoStepsSinceLastObstacle;

int ufoFireCount;
int ufoPlayerOffset;
int ufoFlames;
int ufoFire;
int ufoFireLock;

int ufoScore;
int ufoTop;
int ufoNewHigh;

// -----------------------------------------------------------------------------
//   Render byte helpers
// -----------------------------------------------------------------------------

// Ship icon bytes (8, masked by flameMask depending on thrust state) plus
// the fire-trail bytes that follow it (27, only shown while ufoFireCount
// is active) - one continuous sequence, matching upstream's own single
// unbroken column stream from x=8 through x=42.
int ufoShipTrailByte( int idx )
{
    if( idx < 8 )
    {
        int mask = 0x3F; // flameMask[0]
        if( ufoFlames ) mask = 0xFF; // flameMask[1]
        int shipBytes0 = 0x0C; // B00001100
        int shipBytes1 = 0x5E; // B01011110
        int shipBytes2 = 0x97; // B10010111
        int shipBytes3 = 0x53; // B01010011
        if( idx == 0 ) return shipBytes0 & mask;
        if( idx == 1 ) return shipBytes1 & mask;
        if( idx == 2 ) return shipBytes2 & mask;
        if( idx == 3 ) return shipBytes3 & mask;
        if( idx == 4 ) return shipBytes3 & mask;
        if( idx == 5 ) return shipBytes2 & mask;
        if( idx == 6 ) return shipBytes1 & mask;
        return shipBytes0 & mask;
    }
    if( ufoFireCount <= 0 ) return 0;
    if( idx < 8 + 25 ) return 0x04; // B00000100
    if( idx < 8 + 27 ) return 0x15; // B00010101
    return 0;
}

int ufoDoDrawLS( int idx, int p2 ) { return ( ufoShipTrailByte( idx ) << p2 ) & 0xFF; }
int ufoDoDrawRS( int idx, int p2 ) { return ( ufoShipTrailByte( idx ) >> p2 ) & 0xFF; }

int ufoShipByte( int x, int page )
{
    int idx = x - 8;
    if( idx < 0 || idx > 34 ) return 0;
    int topPage = ufoPlayerOffset / 8;
    int off = ufoPlayerOffset % 8;
    if( off != 0 )
    {
        if( page == topPage ) return ufoDoDrawLS( idx, off );
        if( page == topPage + 1 ) return ufoDoDrawRS( idx, 8 - off );
        return 0;
    }
    if( page == topPage ) return ufoDoDrawLS( idx, 0 );
    return 0;
}

// Computes the obstacle wall/gap byte for one page directly from the
// gap's own pixel range, rather than upstream's own shift-based formula -
// see the header comment on why that formula isn't safely portable here.
int ufoObstacleBaseByte( int i, int page )
{
    int gOff = ufoGapOffset[i];
    int gSize = ufoGapSize[i];
    int result = 0;
    int bit;
    for( bit = 0; bit < 8; bit++ )
    {
        int pixelY = page * 8 + bit;
        int inGap = ( pixelY >= gOff && pixelY < gOff + gSize );
        if( !inGap ) result = result | ( 1 << bit );
    }
    return result;
}

int ufoObstacleByte( int i, int colWithinObstacle, int page )
{
    if( colWithinObstacle >= 4 ) return 0;
    int base = ufoObstacleBaseByte( i, page );
    if( ufoGapBlock[i] == 0 ) return base;
    if( colWithinObstacle == 1 ) return ( base | ( 0xAA >> 1 ) ) & 0xFF;
    if( colWithinObstacle == 2 ) return ( base | 0xAA ) & 0xFF;
    return base;
}

// -----------------------------------------------------------------------------
//   Game logic
// -----------------------------------------------------------------------------

void ufoResetObstacles()
{
    int i;
    for( i = 0; i < UFO_MAX_OBSTACLES_ARR; i++ )
    {
        ufoObstacleX[i] = -50;
        ufoGapOffset[i] = 0;
        ufoGapSize[i] = 30;
        ufoGapBlock[i] = 0;
    }
    ufoStepsSinceLastObstacle = 0;
}

void ufoBeginGame()
{
    ufoFire = 0;
    ufoFireLock = 0;
    ufoFireCount = 0;
    ufoMaxObstacles = 3;
    ufoObstacleStep = 2;
    ufoResetObstacles();
    ufoPlayerOffset = 0;
    ufoFlames = 0;
    ufoScore = 0;
    ufoBlockChance = 11;
    ufoMaxGap = 60;
}

// -----------------------------------------------------------------------------
//   Top-level render dispatch
// -----------------------------------------------------------------------------

#define UFO_MODE_ATTRACT  0
#define UFO_MODE_PLAYING  1
#define UFO_MODE_GAMEOVER 2
#define UFO_MODE_NEWHIGH  3

#define UFO_STAR_COUNT 30
int[30] ufoStarX;
int[30] ufoStarY;
int[30] ufoStarBit;

// Composited once per page/row, not re-scanned at every one of 1024
// pixels/frame - both the obstacle list (up to 5) and the star field (30)
// were originally checked with a per-object loop *inside* the per-pixel
// loop (an O(pixels x objects) shape this project has repeatedly found
// and fixed in every other multi-object game - Bomber/Pacman/Missile/
// Frogger among them). Since each obstacle only ever occupies 6 columns
// and each star exactly 1, composing them into a shared row buffer once
// per page (up to 5*8=40 obstacle-checks, 30*8=240 star-checks total,
// instead of 1024*5=5120 / 1024*30=30720) is the same fix shape applied
// there, just proactively during this same optimization pass rather than
// waiting for a reported slowdown.
int[128] ufoPageBuffer;

void ufoCompositeObstaclesRow( int page )
{
    int x;
    for( x = 0; x < 128; x++ ) ufoPageBuffer[x] = 0;
    int i;
    for( i = 0; i < ufoMaxObstacles; i++ )
    {
        if( ufoObstacleX[i] < -5 || ufoObstacleX[i] > 128 ) continue;
        int col;
        for( col = 0; col < 6; col++ )
        {
            int x2 = ufoObstacleX[i] + col;
            if( x2 < 0 || x2 >= 128 ) continue;
            int b = ufoObstacleByte( i, col, page );
            if( b != 0 ) ufoPageBuffer[x2] = ufoPageBuffer[x2] | b;
        }
    }
}

void ufoCompositeStarsRow( int page )
{
    int x;
    for( x = 0; x < 128; x++ ) ufoPageBuffer[x] = 0;
    int s;
    for( s = 0; s < UFO_STAR_COUNT; s++ )
        if( ufoStarY[s] == page )
            ufoPageBuffer[ ufoStarX[s] ] = ufoPageBuffer[ ufoStarX[s] ] | ( 0x80 >> ufoStarBit[s] );
}

void ufoRenderFrame( int mode )
{
    int x, y, val;
    int scoreDigits = 0;
    if( mode == UFO_MODE_PLAYING ) scoreDigits = ufoCountDigits( ufoScore );
    md_beginFrame();
    for( y = 0; y < 8; y++ )
    {
        int* attractStr = "";
        int attractStartX = 0;
        int attractStrLen = 0;
        if( mode == UFO_MODE_ATTRACT )
        {
            ufoCompositeStarsRow( y );
            if( y == 2 ) { attractStr = "U F O  C H A O S"; attractStartX = 14; }
            else if( y == 4 ) { attractStr = "mods bh andh jackson"; attractStartX = 3; }
            else if( y == 6 ) { attractStr = "original game bh"; attractStartX = 17; }
            else if( y == 7 ) { attractStr = "/ebboggles.com"; attractStartX = 22; }
            attractStrLen = strlen( attractStr );
        }
        else if( mode == UFO_MODE_PLAYING )
        {
            ufoCompositeObstaclesRow( y );
        }
        else if( mode == UFO_MODE_GAMEOVER )
        {
            if( y == 1 ) { attractStr = "----------------"; attractStartX = 11; }
            else if( y == 2 ) { attractStr = "G A M E  O V E R"; attractStartX = 11; }
            else if( y == 3 ) { attractStr = "----------------"; attractStartX = 11; }
            else if( y == 5 ) { attractStr = "SCORE:"; attractStartX = 37; }
            else if( y == 7 && !ufoNewHigh ) { attractStr = "HIGH SCORE:"; attractStartX = 21; }
            attractStrLen = strlen( attractStr );
        }
        else if( mode == UFO_MODE_NEWHIGH )
        {
            if( y == 1 ) { attractStr = "----------------"; attractStartX = 10; }
            else if( y == 3 ) { attractStr = " NEW HIGH SCORE "; attractStartX = 10; }
            else if( y == 7 ) { attractStr = "----------------"; attractStartX = 10; }
            attractStrLen = strlen( attractStr );
        }

        for( x = 0; x < 128; x++ )
        {
            val = 0;
            if( mode == UFO_MODE_ATTRACT )
            {
                val = val | ufoPageBuffer[x];
                val = val | ufoTextByteLen( x, y, attractStartX, y, attractStr, attractStrLen );
            }
            else if( mode == UFO_MODE_PLAYING )
            {
                if( x >= 8 && x <= 42 ) val = val | ufoShipByte( x, y );
                val = val | ufoPageBuffer[x];
                if( y == 0 )
                {
                    int scoreByte = ufoScoreByte( x, y, 92, 0, ufoScore, scoreDigits );
                    if( scoreByte != 0 ) val = val | scoreByte;
                }
            }
            else if( mode == UFO_MODE_GAMEOVER )
            {
                val = val | ufoTextByteLen( x, y, attractStartX, y, attractStr, attractStrLen );
                if( y == 5 ) val = val | ufoNumberByte( x, y, 75, 5, ufoScore );
                else if( y == 7 && !ufoNewHigh ) val = val | ufoNumberByte( x, y, 88, 7, ufoTop );
            }
            else if( mode == UFO_MODE_NEWHIGH )
            {
                val = val | ufoTextByteLen( x, y, attractStartX, y, attractStr, attractStrLen );
                if( y == 5 ) val = val | ufoNumberByte( x, y, 50, 5, ufoTop );
            }
            md_drawColumn( x, y, val );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define UFO_STATE_ATTRACT      0
#define UFO_STATE_PLAYING      1
#define UFO_STATE_GAMEOVER_WAIT 2
#define UFO_STATE_NEWHIGH_WAIT  3

int ufoState;
int ufoWaitFrames;
int ufoFireHeld;
int ufoFireHoldTicks;
int ufoMuteActionDone;

void ufoBeginAttract()
{
    int s;
    for( s = 0; s < UFO_STAR_COUNT; s++ )
    {
        ufoStarX[s] = arand( 127 );
        ufoStarY[s] = arand( 7 );
        ufoStarBit[s] = arand( 7 );
    }
    ufoFireHeld = 0;
    ufoFireHoldTicks = 0;
    ufoMuteActionDone = 0;
    ufoState = UFO_STATE_ATTRACT;
}

void ufoBeginGameOverWait()
{
    ufoRenderFrame( UFO_MODE_GAMEOVER );
    ufoStartNoteSeq( ufoGameOverNotes, UFO_GAMEOVER_COUNT );
    ufoWaitFrames = 120;
    ufoState = UFO_STATE_GAMEOVER_WAIT;
}

void ufoBeginNewHighWait()
{
    ufoRenderFrame( UFO_MODE_NEWHIGH );
    ufoStartNoteSeq( ufoNewHighNotes, UFO_NEWHIGH_COUNT );
    ufoWaitFrames = 162;
    ufoState = UFO_STATE_NEWHIGH_WAIT;
}

void ufoEndGame()
{
    // Direct translation of upstream's own topScoreU, stored at EEPROM
    // addr 0/1, matching UFO_Stacker_Attiny's own combined-cartridge
    // layout (Stacker's own half uses addr 2/3 instead).
    if( ufoScore > ufoTop ) { ufoTop = ufoScore; ufoNewHigh = 1; eeprom_write_word( 0, ufoTop ); } else ufoNewHigh = 0;
    ufoBeginGameOverWait();
}

void ufoPlayingTick()
{
    int firePressed = isFirePressed();
    if( firePressed )
    {
        if( ufoFireLock == 0 ) { ufoFire = 1; ufoFireCount = 5; ufoFireLock = 1; }
    }
    else ufoFireLock = 0;

    if( ufoScore < 500 )
    {
        ufoBlockChance = 11 - ufoScore / 50;
        if( ufoBlockChance < 1 ) ufoBlockChance = 1;
        if( ufoMaxObstacles < 5 ) ufoMaxObstacles = ( ufoScore + 40 ) / 70 + 1;
    }
    if( ufoScore < 2000 )
    {
        ufoMaxGap = 60 - ufoScore / 100;
        if( ufoMaxGap < 26 ) ufoMaxGap = 26;
    }
    if( ufoFire == 1 ) ufoScore--;
    if( ufoFireCount > 0 ) ufoFireCount--;

    // Upstream resets `fire` back to 0 (and plays the fire sound) inside
    // doDrawLS()/doDrawRS() - the ship-render functions - once fireCount
    // is active. Converting those into pure byte-lookup functions for
    // this port's rendering model (ufoDoDrawLS/ufoDoDrawRS) dropped this
    // real side effect - without it, `ufoFire` never clears, so
    // `if(ufoFire==1)ufoScore--;` above fires every tick forever after
    // the first shot, driving score deeply negative and, in turn,
    // `ufoMaxObstacles` to 0 (stopping all obstacle spawning/movement,
    // since `for(i=0;i<ufoMaxObstacles;i++)` then never runs). Restored
    // as an explicit step here, at the same point in the tick upstream's
    // own render call would have reached it (fireCount already active
    // this same tick, right after its own decrement above).
    if( ufoFireCount > 0 && ufoFire == 1 )
    {
        ufoBeepOnce( 50, 100 );
        ufoFire = 0;
    }

    int flying = isUpPressed() || isDownPressed();
    int boo;
    for( boo = 0; boo < 3; boo++ )
    {
        if( flying && ufoPlayerOffset > 0 )
        {
            ufoPlayerOffset--;
            ufoFlames = 1;
        }
    }

    // Upstream's own thruster hum while climbing (a real quirk found only
    // via a project-wide missing-sound-cue audit, not present in this
    // port at all before now): a short beep(1,random(0,i*2)) pair per
    // "boo" iteration above. Approximated as one representative short
    // blip per tick instead of the full nested loop (which would just be
    // several near-identical blips colliding into the same
    // "burst collapses to one tone" issue fixed elsewhere this session).
    if( flying && ufoFlames )
      ufoBeepOnce( 1, arand( 4 ) );

    ufoStepsSinceLastObstacle += ufoObstacleStep;
    int i;
    for( i = 0; i < ufoMaxObstacles; i++ )
    {
        if( ufoObstacleX[i] >= 0 && ufoObstacleX[i] <= 128 )
        {
            ufoObstacleX[i] -= ufoObstacleStep;
            if( ufoGapBlock[i] > 0 && ufoObstacleX[i] < 36 &&
                ufoPlayerOffset > ufoGapOffset[i] &&
                ufoPlayerOffset + 5 < ufoGapOffset[i] + ufoGapSize[i] &&
                ufoFireCount > 0 )
            {
                ufoGapBlock[i] = 0;
                ufoScore += 5;
                ufoStartNoteSeq( ufoGapDestroyNotes, UFO_GAPDESTROY_COUNT );
            }
        }

        if( ufoObstacleX[i] <= 4 && ufoStepsSinceLastObstacle >= arand(70) + 30 )
        {
            ufoObstacleX[i] = 123;
            ufoGapSize[i] = arand( ufoMaxGap - 25 ) + 25;
            int room = 64 - ufoGapSize[i];
            if( room < 1 ) room = 1;
            ufoGapOffset[i] = arand( room );
            if( arand( ufoBlockChance ) == 0 ) ufoGapBlock[i] = 1; else ufoGapBlock[i] = 0;
            ufoStepsSinceLastObstacle = 0;
            ufoScore += 1;
        }
    }

    if( ufoFireCount == 0 && ufoPlayerOffset < 56 ) ufoPlayerOffset++;

    for( i = 0; i < ufoMaxObstacles; i++ )
    {
        if( ufoObstacleX[i] > 8 && ufoObstacleX[i] < 16 )
        {
            if( ufoPlayerOffset < ufoGapOffset[i] ||
                ufoPlayerOffset + 5 > ufoGapOffset[i] + ufoGapSize[i] ||
                ufoGapBlock[i] != 0 )
            {
                ufoEndGame();
                return;
            }
        }
    }

    ufoFlames = 0;
    ufoRenderFrame( UFO_MODE_PLAYING );
}

// -----------------------------------------------------------------------------
//   Top-level
// -----------------------------------------------------------------------------

void gameUFO_init()
{
    ufoMute = 0;
    // Direct translation of upstream's own topScoreU = EEPROM.read(0)<<8 |
    // EEPROM.read(1), guarded against a never-written slot's own virgin
    // 65535 read the same way as every other game in this pass.
    ufoTop = eeprom_read_word( 0 );
    if( ufoTop == 65535 ) ufoTop = 0;
    ufoSeqActive = 0;
    ufoBeginAttract();
}

void gameUFO_forceRedraw()
{
    if( ufoState == UFO_STATE_ATTRACT ) ufoRenderFrame( UFO_MODE_ATTRACT );
    else if( ufoState == UFO_STATE_PLAYING ) ufoRenderFrame( UFO_MODE_PLAYING );
    else if( ufoState == UFO_STATE_GAMEOVER_WAIT ) ufoRenderFrame( UFO_MODE_GAMEOVER );
    else ufoRenderFrame( UFO_MODE_NEWHIGH );
}

void gameUFO_update()
{
    ufoAdvanceNoteSeq();

    if( ufoState == UFO_STATE_ATTRACT )
    {
        int fireDown = isFirePressed();
        if( fireDown )
        {
            ufoFireHoldTicks++;
            if( ufoFireHoldTicks >= 120 && !ufoMuteActionDone )
            {
                ufoMuteActionDone = 1;
                if( ufoMute == 0 ) ufoMute = 1; else ufoMute = 0;
            }
        }
        else
        {
            if( ufoFireHeld && !ufoMuteActionDone )
            {
                ufoFireHeld = fireDown;
                ufoBeginGame();
                ufoState = UFO_STATE_PLAYING;
                ufoRenderFrame( UFO_MODE_PLAYING );
                return;
            }
            ufoFireHoldTicks = 0;
            ufoMuteActionDone = 0;
        }
        ufoFireHeld = fireDown;
        ufoRenderFrame( UFO_MODE_ATTRACT );
    }
    else if( ufoState == UFO_STATE_PLAYING )
    {
        ufoPlayingTick();
    }
    else if( ufoState == UFO_STATE_GAMEOVER_WAIT )
    {
        if( ufoWaitFrames > 0 ) { ufoWaitFrames--; return; }
        if( ufoNewHigh ) ufoBeginNewHighWait();
        else ufoBeginAttract();
    }
    else // UFO_STATE_NEWHIGH_WAIT
    {
        if( ufoWaitFrames > 0 ) { ufoWaitFrames--; return; }
        ufoBeginAttract();
    }
}

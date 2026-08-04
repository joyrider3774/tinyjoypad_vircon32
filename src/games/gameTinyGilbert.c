// =============================================================================
// Tiny Gilbert - ported from Daniel C's tinygilbert.ino (tinyjoypad.com,
// GPLv3). Same tinyJoypadShim lineage as every other Daniel-C game here
// (FastTinyDriver.h) - Sound()/isXPressed() reuse the existing shim as-is.
//
// A side-scrolling platformer: run left/right, jump gaps and hazards,
// collect all of a level's keys, then reach the door to advance - 10
// levels, 7 lives.
//
// Button mapping (matches every other Daniel-C game's own established A0
// thresholds, confirmed directly against this game's own analogRead(A0)
// reads):
//   analogRead(A0) in (500,750) = isRightPressed()
//   analogRead(A0) in [750,950) = isLeftPressed()
//   digitalRead(1) (active low) = isFirePressed() - doubles as both
//     "confirm/start" (on the title screen) and "jump" (in play), exactly
//     matching upstream's own single-action-button design. No up/down
//     input exists in this game at all (no analogRead(A3) anywhere in the
//     source).
//
// Structural changes from upstream:
//  - upstream's loop() is a RESTARTGAME:/RESTARTLEVEL:/NEXTLEVEL: goto-
//    chain around one big while(1), with a real busy-wait-for-press after
//    the intro (`while(1){if(digitalRead(1)==LOW){break;}}`) - rewritten
//    as an explicit frame-stepped state machine (gilbState), the same
//    approach as every other tinyJoypadShim port here.
//  - upstream's own `FPS_Control` (`while((currentMillis-MemMillis)<25)`)
//    is a genuine ~40fps whole-loop throttle (matching NumberPlace/
//    HollowSeeker/t2048/Doc's own "genuine fixed real-rate" category) -
//    but unlike those games, 40 does not evenly divide Vircon32's 60fps
//    engine rate (60/40 = 1.5, not an integer). A **new technique for
//    this project**: a Bresenham-style accumulator (`gilbTickAccum +=
//    GILB_TICK_NUM; if accum >= GILB_TICK_DEN then accum -= GILB_TICK_DEN
//    and run one tick`) produces exactly 40 ticks per 60 real frames
//    long-term (a run/skip pattern of skip,run,run repeating), rather
//    than the plain integer-divisor counter every earlier "genuine rate"
//    game could use. The whole per-tick body (movement, collision,
//    redraw) is skipped entirely on non-tick frames, matching the
//    established whole-function-throttle shape - the previous frame's
//    image simply persists.
//  - The intro jingle (`sound(1);sound(2);sound(2);sound(1);sound(2);
//    sound(2);`) is a genuinely blocking sequence upstream. `sound(1)` is
//    a harmless 3-note burst (Sound(210,10);Sound(240,2);Sound(180,5)) -
//    kept as-is. `sound(2)` (`for(t=255;t>2;t--) Sound(t,1);`, ~253 notes)
//    is the same class of bug already found and fixed in Tiny Arena's
//    death sweep and Tiny Missile's computed sweeps: Vircon32's audio
//    channel has no queue, so 253 synchronous calls would either take
//    253 real frames to play through a frame-stepped sequencer (way
//    longer than upstream's near-instant real sweep) or, played
//    synchronously, would only ever be *heard* as the last call's tone.
//    Converted to a small frame-stepped, downsampled sweep (step -15
//    instead of -1, ~18 notes) - preserves the audible descending
//    "whoosh" while finishing in a small handful of real frames. The
//    upstream sequence's two back-to-back `sound(2)` calls are collapsed
//    to one sweep each occurrence (a "preserve the character, don't
//    reproduce every literal repetition" simplification, same precedent
//    as Tiny Missile's own downsampled sweep) - so the ported intro
//    jingle is burst/sweep/burst/sweep instead of upstream's literal
//    burst/sweep/sweep/burst/sweep/sweep.
//  - Two genuine out-of-bounds risks found by inspection (not by a
//    crash), both harmless on real AVR's forgiving flat memory but a
//    real "ERROR: INVALID MEMORY READ" risk on Vircon32:
//     1. `delKey()`'s search loop scans `key[x]` for `x` in [0,23), but
//        `key[][]` is declared with only 20 real slots - reads 3 entries
//        past the array on every call. Fixed by using the correct bound
//        (20).
//     2. `CollisionCheck()`'s 4x4 neighborhood scan (`yscan` in [-1,3))
//        indexes `Map[gridV+yscan][...]` - and `gridV` (the sprite's grid
//        row) is transiently -1 inside `JumpProcedure()` (before its own
//        clamp reverts it) and reaches 7 inside `GravityUpdate()` (before
//        the main loop's own "fell off the bottom" check runs) - with
//        `yscan` extending +2/-1 further, real row indices as low as -2
//        and as high as 9 against an 8-row array are reachable. Fixed
//        with an explicit bounds guard (skip the probe, treat as "no
//        collision here") rather than trusting the array read to stay
//        in-bounds the way AVR's memory model tolerated.
//  - `TinyMainShift()` is declared upstream but never called anywhere -
//    dropped as genuinely dead code, same treatment as other provably-
//    inert upstream functions found in earlier ports (e.g. Tiny Bike's
//    own dead-function cleanup).
//  - Sprite/level/font-style data tables extracted from `spritebank.h`
//    with a small Python script (not hand-transcribed) - the first
//    attempt didn't strip `/* */` block comments (only `//` line
//    comments), so the `/*0*/`.../*12*/` index markers embedded inside
//    `map1coucheN[]`'s array literals were incorrectly parsed as extra
//    data values (65 elements read instead of the real 52) - caught by
//    checking the extracted count against a manual read of the source
//    before ever using the data, fixed by also stripping block comments.
//  - `DriftSprite`'s `DriftGrid[2][2]` member (a small 2x2 sprite-half
//    lookup) flattened to 4 named scalar fields rather than testing
//    whether a 2D array field inside a struct is supported by this
//    dialect - a trivial, low-risk flattening rather than being the
//    first port to find out.
//  - Tiny_Flip()'s HUD row (page 0) has a genuine upstream column-count
//    mismatch between its two key-icon branches: the "icon visible"
//    branch writes only 4 columns (the icon's own real width) while the
//    "icon hidden" branch writes 16 blank columns - on real hardware the
//    icon branch's missing 12 columns just keep whatever was already
//    there (always blank, so visually harmless), but this project's
//    always-clear-then-redraw model can't rely on stale VRAM the same
//    way - fixed by padding the icon branch to also emit the same 12
//    trailing blank columns, giving a consistent 128-column-wide page 0
//    in both cases (the same "assume nothing about leftover VRAM"
//    principle already applied to Pinball/Doc/Bert's own partial-redraw
//    fixes).
// =============================================================================

int[4] gilbSprite1 = {255,153,231,153};
int[4] gilbSprite2 = {129,231,153,255};
int[4] gilbSprite3 = {1,43,85,43};
int[4] gilbSprite4 = {85,43,85,255};
int[4] gilbSprite7 = {85,170,85,170};
int[4] gilbSprite8 = {240,255,252,192};
int[4] gilbSprite11 = {2,165,253,2};
int[4] gilbSprite12 = {2,253,165,2};
int[4] gilbSprite13 = {0,254,251,241};
int[4] gilbSprite14 = {241,219,254,0};
int[4] gilbSprite15 = {85,87,125,213};
int[4] gilbSprite16 = {85,87,117,221};
int[4] gilbSprite20 = {2,14,219,127};
int[4] gilbSprite21 = {255,159,14,0};
int[4] gilbSprite22 = {4,28,182,126};
int[4] gilbSprite23 = {254,62,28,0};
int[4] gilbSprite24 = {4,28,54,254};
int[4] gilbSprite25 = {126,190,28,0};
int[4] gilbSprite26 = {0,14,159,255};
int[4] gilbSprite27 = {127,219,14,2};
int[4] gilbSprite28 = {0,28,62,254};
int[4] gilbSprite29 = {126,182,28,4};
int[4] gilbSprite30 = {0,28,190,126};
int[4] gilbSprite31 = {254,54,28,4};

int[21] gilbLevel0 = {0,1,2,4,4,2,5,2,11,9,8,10,10,2,12,4,6,7,0,0,0};
int[21] gilbLevel1 = {0,1,2,3,7,7,6,5,8,9,2,1,3,4,6,7,1,11,0,0,0};
int[20] gilbLevel2 = {0,1,1,2,3,4,3,3,2,12,2,6,5,7,11,1,12,0,0,0};
int[15] gilbLevel3 = {0,11,10,10,8,7,6,10,6,5,3,2,0,0,0};
int[29] gilbLevel4 = {0,3,10,4,7,3,6,4,6,3,2,12,3,2,11,12,4,5,1,2,2,12,1,5,5,12,0,0,0};
int[18] gilbLevel5 = {0,1,12,2,10,11,10,8,12,2,3,3,12,3,3,0,0,0};
int[16] gilbLevel6 = {0,1,2,12,12,3,12,3,12,1,2,6,11,0,0,0};
int[19] gilbLevel7 = {0,12,2,1,1,3,3,3,3,12,1,1,1,8,7,11,0,0,0};
int[17] gilbLevel8 = {0,11,12,1,2,3,6,3,12,3,12,3,6,12,0,0,0};
int[23] gilbLevel9 = {0,12,11,12,2,12,3,7,12,8,10,2,12,2,12,2,3,4,7,12,0,0,0};

int[10] gilbKeyInLevel = {10,11,11,4,19,9,8,12,9,11};

int[52] gilbMap1Couche2 = {3,4,3,4, 3,4,3,4, 3,4,3,4, 3,4,3,4, 3,4,3,4, 3,4,3,4, 3,4,3,4, 3,4,3,4, 3,4,3,4, 3,4,3,4, 0,0,0,0, 3,4,3,4, 3,4,3,4};
int[52] gilbMap1Couche3 = {3,4,3,4, 0,0,0,0, 0,0,0,0, 0,0,11,0, 0,0,0,11, 0,0,0,0, 0,0,0,0, 0,0,3,4, 3,4,3,4, 3,4,0,0, 0,0,0,0, 3,4,3,4, 0,0,0,0};
int[52] gilbMap1Couche4 = {3,4,3,4, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,11,0,0, 0,0,0,0, 0,0,0,0, 3,4,3,4, 3,4,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
int[52] gilbMap1Couche5 = {3,4,3,4, 0,0,0,0, 0,0,0,0, 0,0,0,8, 0,0,3,4, 0,0,11,0, 3,4,3,4, 3,4,3,4, 0,0,3,4, 0,0,0,0, 3,4,3,4, 0,0,0,0, 11,0,0,0};
int[52] gilbMap1Couche6 = {3,4,3,4, 0,11,0,0, 0,0,0,0, 3,4,3,4, 3,4,3,4, 0,0,3,4, 0,0,0,0, 0,0,0,0, 0,0,3,4, 0,0,0,11, 0,0,0,0, 0,13,14,0, 0,0,0,0};
int[52] gilbMap1Couche7 = {3,4,3,4, 0,0,0,8, 3,4,3,4, 3,4,3,4, 3,4,3,4, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,11,0,0, 0,0,11,0, 0,0,0,0, 15,16,15,16, 0,0,0,0};
int[52] gilbMap1Couche8 = {15,16,15,16, 15,16,15,16, 15,16,15,16, 15,16,15,16, 15,16,15,16, 15,16,15,16, 0,0,0,0, 15,0,0,16, 15,16,15,16, 15,16,15,16, 15,16,15,16, 15,16,15,16, 0,0,0,0};

// A 64x32-pixel (64 columns, 4 pages) boxed logo, drawn at (32,4) - the
// header {64,4} is gilbSpeedBlitz()'s own width/height-in-pages
// convention. Anything outside that box reads as 0xFF (solid white) via
// gilbSpeedBlitz()'s own out-of-bounds sentinel.
int[258] gilbStart =
{
64,4,
255,255,255,255,255,127,127,191,191,159,255,223,223,223,255,191,
127,123,251,115,7,7,31,127,127,127,127,127,255,255,251,251,
251,3,251,251,251,255,13,255,15,223,239,239,31,255,239,31,
255,127,143,255,255,255,255,255,255,255,255,255,255,255,255,255,
31,7,7,3,0,70,195,227,225,241,241,241,249,240,249,152,
152,216,248,248,248,248,200,224,0,128,224,240,240,255,255,127,
189,190,191,191,255,253,254,223,252,31,255,255,30,253,255,239,
240,254,255,255,255,255,255,255,255,255,255,255,255,63,255,255,
255,254,254,252,252,248,225,227,135,7,15,15,15,15,15,15,
15,15,15,7,7,195,227,240,248,254,255,255,255,255,240,239,
223,223,93,93,97,127,94,64,127,64,127,127,64,94,94,94,
97,127,97,90,90,90,89,127,96,94,126,126,254,192,222,255,
255,255,255,255,255,255,255,255,255,252,248,128,0,0,4,76,
104,120,120,112,124,185,119,119,111,143,255,255,255,255,255,255,
255,255,0,127,1,1,73,85,37,1,9,125,9,1,121,21,
121,1,125,53,89,1,9,125,9,1,127,0,255,255,255,255
};

// -----------------------------------------------------------------------------
//   State
// -----------------------------------------------------------------------------

struct GilbDriftSprite
{
    int driftBL;
    int driftBR;
    int driftTL;
    int driftTR;
    int x4Decalage;
    int y8Decalage;
    int gridH;
    int gridV;
};

GilbDriftSprite gilbMainSprite;

int[8][34] gilbMap;
int gilbTimer;
int gilbScrool;
int gilbStep4;
int gilbMainAnim;
int gilbLorR;
int gilbJump;
int gilbJumpCancel;
int gilbLevelMult;
int gilbLevelType;
int gilbByteMem;
int gilbVisible;
int gilbInjur;
int gilbLive;
int[20][2] gilbKey;
int gilbKeyS;
float gilbVSlideOut;

// Bresenham-style accumulator producing exactly 40 ticks per 60 real
// engine frames (upstream's own genuine ~40fps FPS_Control rate) - see
// this file's own header comment for why a plain integer divisor (as
// used by NumberPlace/HollowSeeker/t2048/Doc) doesn't work here.
#define GILB_TICK_NUM 40
#define GILB_TICK_DEN 60
int gilbTickAccum;

int gilbSweepActive;
int gilbSweepT;
int gilbBurstActive;
int gilbBurstStep;
int gilbBurstWait;

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

void gilbSound1()
{
    gilbBurstActive = 1;
    gilbBurstStep = 0;
    gilbBurstWait = 0;
}

void gilbAdvanceBurst()
{
    if( !gilbBurstActive ) return;
    if( gilbBurstWait > 0 ) { gilbBurstWait--; return; }

    if( gilbBurstStep == 0 ) { Sound( 210, 10 ); gilbBurstWait = 3; }
    else if( gilbBurstStep == 1 ) { Sound( 240, 2 ); gilbBurstWait = 1; }
    else if( gilbBurstStep == 2 ) { Sound( 180, 5 ); gilbBurstWait = 2; }
    else { gilbBurstActive = 0; return; }
    gilbBurstStep++;
}

void gilbSound2()
{
    gilbSweepActive = 1;
    gilbSweepT = 255;
}

void gilbAdvanceSweep()
{
    if( !gilbSweepActive ) return;
    Sound( gilbSweepT, 1 );
    gilbSweepT -= 15;
    if( gilbSweepT <= 2 )
      gilbSweepActive = 0;
}

int gilbSoundBusy()
{
    return gilbBurstActive || gilbSweepActive;
}

// -----------------------------------------------------------------------------
//   Helpers
// -----------------------------------------------------------------------------

// Was `for (x=0;x<23;x++)` upstream - key[][] only has 20 real slots (see
// this file's own header comment on the out-of-bounds read this fixes).
int gilbDelKey( int xIn, int yIn )
{
    int x;
    for( x = 0; x < 20; x++ )
    {
        if( gilbKey[x][0] == 0 && gilbKey[x][1] == 0 ) return 11;
        if( gilbKey[x][0] == xIn && gilbKey[x][1] == yIn ) return 0;
    }
    return 11;
}

int gilbSpeedBlitz( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos ||
        yPass < yPos || yPass > ( yPos + ( hSprite - 1 ) ) )
      return 0xFF;
    return sprites[ 2 + ( ( xPass - xPos ) + ( yPass - yPos ) * wSprite ) + frame * ( hSprite * wSprite ) ];
}

// -----------------------------------------------------------------------------
//   Physics / collision
// -----------------------------------------------------------------------------

int gilbCollisionCheck( GilbDriftSprite* dSprite )
{
    int mx = dSprite->gridH;
    int my = dSprite->gridV;
    int mxDrift = dSprite->x4Decalage;
    int myDrift = dSprite->y8Decalage;
    int x1 = ( mx * 4 ) + mxDrift;
    int y1 = ( my * 8 ) + myDrift;
    int x2 = ( mx * 4 ) + mxDrift + 7;
    int y3 = ( my * 8 ) + myDrift + 7;

    int xscan, yscan;
    for( yscan = -1; yscan < 3; yscan++ )
    {
        for( xscan = -1; xscan < 3; xscan++ )
        {
            int row = my + yscan;
            int col = mx + xscan;
            // Explicit bounds guard - `row` can transiently be -1 (from
            // JumpProcedure, before its own clamp reverts it) or 7 (from
            // GravityUpdate, before the main loop's own "fell off the
            // bottom" check runs), and combined with this scan's own
            // +2/-1 spread that reaches genuinely out-of-bounds rows -
            // see this file's own header comment.
            if( row < 0 || row >= 8 || col < 0 || col >= 34 ) continue;

            int cell = gilbMap[ row ][ col ];
            int noTested = ( cell != 8 && cell != 0 && cell != 13 && cell != 14 &&
                              cell != 11 && cell != 5 && cell != 6 && cell != 55 && cell != 66 );
            if( noTested )
            {
                int bx1 = col * 4;
                int by1 = row * 8;
                int bx2 = col * 4 + 3;
                int by3 = row * 8 + 7;
                if( x1 > bx2 || x2 < bx1 || y1 > by3 || y3 < by1 ) {}
                else return 1;
            }
        }
    }
    return 0;
}

void gilbGravityUpdate( GilbDriftSprite* dSprite )
{
    int memY8Decalage = dSprite->y8Decalage;
    int memGridV = dSprite->gridV;
    dSprite->y8Decalage = dSprite->y8Decalage + 2;
    if( dSprite->y8Decalage > 7 )
    {
        dSprite->y8Decalage = 0;
        dSprite->gridV++;
    }
    if( gilbCollisionCheck( dSprite ) >= 1 )
    {
        dSprite->gridV = memGridV;
        dSprite->y8Decalage = memY8Decalage;
    }
}

void gilbJumpProcedure( GilbDriftSprite* dSprite )
{
    int memo2 = 0;
    if( gilbJump > 0 )
    {
        memo2 = dSprite->gridV;
        dSprite->y8Decalage = dSprite->y8Decalage - ( gilbJump * 2 );
        if( dSprite->y8Decalage < 0 )
        {
            dSprite->y8Decalage = 7;
            dSprite->gridV--;
            gilbJump--;
            if( dSprite->gridV <= 0 || gilbJumpCancel == 1 || gilbCollisionCheck( dSprite ) >= 1 )
            {
                gilbJump = 0;
                gilbJumpCancel = 1;
                dSprite->y8Decalage = 0;
                dSprite->gridV = memo2;
            }
            if( gilbJump == 0 ) gilbJumpCancel = 1;
        }
    }
}

void gilbScrollUpdate( GilbDriftSprite* dSprite )
{
    if( dSprite->gridH < 10 )
    {
        if( gilbScrool > 0 && gilbStep4 <= 3 ) gilbStep4 = gilbStep4 + 1;
        if( gilbStep4 > 3 && gilbScrool > 0 )
        {
            gilbStep4 = 0;
            gilbScrool--;
            dSprite->gridH = dSprite->gridH + 1;
        }
    }
    if( dSprite->gridH > 18 )
    {
        gilbStep4 = gilbStep4 - 1;
        if( gilbStep4 < 0 )
        {
            gilbStep4 = 3;
            gilbScrool++;
            dSprite->gridH = dSprite->gridH - 1;
        }
    }
}

void gilbUpdateVerticalSlide( GilbDriftSprite* dSprite )
{
    if( dSprite->y8Decalage == 0 )
    {
        if( gilbVisible == 1 ) { dSprite->driftBL = 5; dSprite->driftBR = 6; }
        else { dSprite->driftBL = 0; dSprite->driftBR = 0; }
    }
    else
    {
        if( gilbVisible == 1 )
        {
            dSprite->driftBL = 5; dSprite->driftBR = 6;
            dSprite->driftTL = 55; dSprite->driftTR = 66;
        }
        else
        {
            dSprite->driftBL = 0; dSprite->driftBR = 0;
            dSprite->driftTL = 0; dSprite->driftTR = 0;
        }
    }
}

void gilbSpriteShiftInitialise( GilbDriftSprite* dSprite )
{
    dSprite->driftBL = 0;
    dSprite->driftBR = 0;
    dSprite->driftTL = 0;
    dSprite->driftTR = 0;
    dSprite->x4Decalage = 0;
    dSprite->y8Decalage = 0;
    dSprite->gridH = 11;
    dSprite->gridV = 3;
}

// -----------------------------------------------------------------------------
//   Display
// -----------------------------------------------------------------------------

void gilbIntro()
{
    int y, x;
    md_beginFrame();
    for( y = 0; y < 8; y++ )
      for( x = 0; x < 128; x++ )
        md_drawColumn( x, y, gilbSpeedBlitz( 32, 4, x, y, 0, gilbStart ) );
}

void gilbTinyFlip( GilbDriftSprite* dSprite )
{
    int[128] pageBuf;
    int x, t, m, n, nn, start;

    md_beginFrame();

    // Page 0: lives icons + the flashing "key ready" icon, padded to a
    // consistent 128 columns in both branches (see this file's own
    // header comment on the upstream column-count mismatch this fixes).
    nn = 0;
    for( x = 0; x < gilbLive - 1; x++ )
    {
        for( t = 0; t < 4; t++ ) pageBuf[ nn++ ] = gilbSprite26[t];
        for( t = 0; t < 4; t++ ) pageBuf[ nn++ ] = gilbSprite27[t];
    }
    for( x = 0; x < 11 + ( 3 - ( gilbLive - 1 ) ); x++ )
      for( t = 0; t < 8; t++ )
        pageBuf[ nn++ ] = 0x00;
    if( gilbKeyInLevel[ gilbLevelType ] == gilbKeyS && gilbTimer <= 30 )
    {
        for( x = 0; x < 4; x++ ) pageBuf[ nn++ ] = gilbSprite12[x];
        for( x = 0; x < 12; x++ ) pageBuf[ nn++ ] = 0x00;
    }
    else
    {
        for( x = 0; x < 16; x++ ) pageBuf[ nn++ ] = 0x00;
    }
    for( x = 0; x < 128; x++ ) md_drawColumn( x, 0, pageBuf[x] );

    for( m = 1; m < 8; m++ )
    {
        n = 0;
        nn = 0;
        start = 4 - gilbStep4;

        while( nn < 128 )
        {
            int cell = gilbMap[m][n];

            if( cell == 7 )
            {
                for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite7[t];
                start = 0;
            }
            else if( cell == 5 || cell == 55 )
            {
                if( cell == 55 ) gilbVSlideOut = ( 100.0 / ( 1 << ( 8 - dSprite->y8Decalage ) ) ) / 100.0;
                else gilbVSlideOut = (float)( 1 << dSprite->y8Decalage );

                for( t = 0; t < dSprite->x4Decalage && nn < 128; t++ ) pageBuf[ nn++ ] = 0x00;

                if( gilbLorR == 1 )
                {
                    if( gilbMainAnim == 0 ) { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite20[t] * gilbVSlideOut ); }
                    else if( gilbMainAnim == 1 ) { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite22[t] * gilbVSlideOut ); }
                    else { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite24[t] * gilbVSlideOut ); }
                }
                else
                {
                    if( gilbMainAnim == 0 ) { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite26[t] * gilbVSlideOut ); }
                    else if( gilbMainAnim == 1 ) { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite28[t] * gilbVSlideOut ); }
                    else { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite30[t] * gilbVSlideOut ); }
                }
                start = 0;
            }
            else if( cell == 6 || cell == 66 )
            {
                if( cell == 66 ) gilbVSlideOut = ( 100.0 / ( 1 << ( 8 - dSprite->y8Decalage ) ) ) / 100.0;
                else gilbVSlideOut = (float)( 1 << dSprite->y8Decalage );

                if( gilbLorR == 1 )
                {
                    if( gilbMainAnim == 0 ) { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite21[t] * gilbVSlideOut ); }
                    else if( gilbMainAnim == 1 ) { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite23[t] * gilbVSlideOut ); }
                    else { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite25[t] * gilbVSlideOut ); }
                }
                else
                {
                    if( gilbMainAnim == 0 ) { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite27[t] * gilbVSlideOut ); }
                    else if( gilbMainAnim == 1 ) { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite29[t] * gilbVSlideOut ); }
                    else { for( t = 0; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = (int)( gilbSprite31[t] * gilbVSlideOut ); }
                }
                start = dSprite->x4Decalage;
            }
            else if( cell == 1 ) { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite1[t]; start = 0; }
            else if( cell == 2 ) { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite2[t]; start = 0; }
            else if( cell == 3 ) { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite3[t]; start = 0; }
            else if( cell == 4 ) { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite4[t]; start = 0; }
            else if( cell == 8 ) { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite8[t]; start = 0; }
            else if( cell == 15 ) { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite15[t]; start = 0; }
            else if( cell == 16 ) { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite16[t]; start = 0; }
            else if( cell == 11 )
            {
                if( gilbTimer > 30 ) { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite11[t]; }
                else { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite12[t]; }
                start = 0;
            }
            else if( cell == 13 ) { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite13[t]; start = 0; }
            else if( cell == 14 ) { for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = gilbSprite14[t]; start = 0; }
            else
            {
                for( t = start; t < 4 && nn < 128; t++ ) pageBuf[ nn++ ] = 0x00;
                start = 0;
            }

            n++;
        }

        for( x = 0; x < 128; x++ ) md_drawColumn( x, m, pageBuf[x] );
    }
}

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

void gilbResetVarNextLevel()
{
    gilbScrool = 0;
    gilbStep4 = 0;
    gilbMainAnim = 0;
    gilbLorR = 1;
    gilbJump = 0;
    gilbJumpCancel = 0;
    gilbVSlideOut = 0;
    int x;
    for( x = 0; x < 20; x++ ) { gilbKey[x][0] = 0; gilbKey[x][1] = 0; }
    gilbKeyS = 0;
    gilbLevelMult = 0;
}

void gilbResetVar()
{
    gilbResetVarNextLevel();
    gilbLevelType = 0;
    gilbLive = 7;
}

void gilbNextLevel()
{
    gilbResetVarNextLevel();
    gilbLevelType++;
    gilbSound2();
    // Upstream's own `NextLevel()` function doesn't touch the sprite's
    // position either - but every path that calls it is immediately
    // followed by a `goto NEXTLEVEL;`, and the shared NEXTLEVEL: label
    // *always* calls SpriteShiftInitialise() right after (whether
    // reached via a fresh game, a retried level after death, or here,
    // after completing a level) - a real bug, found via direct user
    // play, from missing that goto-driven "always reset position on
    // arrival" behavior when only translating the NextLevel() function
    // itself. Without this, the sprite's grid position (and drift
    // offsets) carried over unchanged from wherever the player happened
    // to touch the previous level's door - which the next level's own
    // tile layout has no reason to keep safe, since upstream's actual
    // design always resets to the same fixed (11,3) spawn cell instead.
    // Symptom: completing a level could spawn the player already
    // overlapping a wall or hazard tile in the next one, taking repeated
    // damage or dying immediately with no way to react.
    gilbSpriteShiftInitialise( &gilbMainSprite );
}

#define GILB_STATE_INTRO_JINGLE  0
#define GILB_STATE_INTRO_WAIT    1
#define GILB_STATE_TITLE_WAIT    2
#define GILB_STATE_PLAYING       3

int gilbState;
int gilbIntroPhase;
int gilbWaitFrames;
int gilbForceRedraw;

void gilbBeginIntroJingle()
{
    gilbIntroPhase = 0;
    gilbSound1();
    gilbState = GILB_STATE_INTRO_JINGLE;
}

void gilbBeginLevel()
{
    gilbResetVarNextLevel();
    gilbSpriteShiftInitialise( &gilbMainSprite );
    gilbState = GILB_STATE_PLAYING;
}

void gameTinyGilbert_init()
{
    InitTinyJoypad();
    gilbResetVar();
    gilbTimer = 0;
    gilbTickAccum = 0;
    gilbSweepActive = 0;
    gilbBurstActive = 0;
    // Upstream's `uint8_t visible=1;` is a non-zero global initializer -
    // Vircon32 zero-initializes globals, so without this explicit set the
    // player sprite's own driftBL/BR/TL/TR (computed each tick by
    // gilbUpdateVerticalSlide() from gilbVisible) stay permanently in the
    // "invisible" branch until the player takes damage at least once
    // (the only other place gilbVisible ever changes). Root cause of a
    // real "no player sprite visible, nothing seems to respond" report.
    gilbVisible = 1;
    gilbBeginIntroJingle();
}

// Quit-confirmation-dialog resume hook (see menuGameList.c's own comment
// on this pattern) - GILB_STATE_TITLE_WAIT is the actual attract/title
// screen and has no timer of its own (it waits for a Fire press), so
// without this the dialog's leftover pixels would persist indefinitely
// if opened from there and cancelled; the intro-jingle/wait states share
// the same static logo picture and get the same treatment for
// consistency, even though they're individually bounded to a couple
// seconds.
void gameTinyGilbert_forceRedraw()
{
    gilbForceRedraw = 1;
}

void gameTinyGilbert_update()
{
    if( gilbState == GILB_STATE_INTRO_JINGLE )
    {
        if( gilbForceRedraw )
        {
            gilbIntro();
            gilbForceRedraw = 0;
        }
        gilbAdvanceBurst();
        gilbAdvanceSweep();

        if( !gilbSoundBusy() )
        {
            gilbIntroPhase++;
            if( gilbIntroPhase == 1 ) gilbSound2();
            else if( gilbIntroPhase == 2 ) gilbSound1();
            else if( gilbIntroPhase == 3 ) gilbSound2();
            else
            {
                gilbResetVar();
                gilbIntro();
                gilbWaitFrames = 24;
                gilbState = GILB_STATE_INTRO_WAIT;
            }
        }
        return;
    }

    if( gilbState == GILB_STATE_INTRO_WAIT )
    {
        if( gilbForceRedraw )
        {
            gilbIntro();
            gilbForceRedraw = 0;
        }
        if( gilbWaitFrames > 0 ) { gilbWaitFrames--; return; }
        gilbState = GILB_STATE_TITLE_WAIT;
        return;
    }

    if( gilbState == GILB_STATE_TITLE_WAIT )
    {
        if( gilbForceRedraw )
        {
            gilbIntro();
            gilbForceRedraw = 0;
        }
        if( isFirePressed() )
        {
            gilbSound1();
            gilbBeginLevel();
        }
        return;
    }

    // GILB_STATE_PLAYING - throttled to a genuine ~40fps tick (see this
    // file's own header comment on the Bresenham accumulator).
    gilbAdvanceSweep();

    gilbTickAccum += GILB_TICK_NUM;
    if( gilbTickAccum < GILB_TICK_DEN ) return;
    gilbTickAccum -= GILB_TICK_DEN;

    if( gilbLevelType > 9 )
    {
        gilbBeginIntroJingle();
        return;
    }

    GilbDriftSprite* sprite = &gilbMainSprite;

    if( isRightPressed() )
    {
        if( gilbTimer % 4 == 0 ) { gilbMainAnim++; if( gilbMainAnim > 2 ) gilbMainAnim = 0; }
        gilbLorR = 0;
        sprite->x4Decalage++;
        if( gilbCollisionCheck( sprite ) == 1 ) sprite->x4Decalage--;
        if( sprite->x4Decalage > 3 ) { sprite->x4Decalage = 0; sprite->gridH++; }
    }
    if( isLeftPressed() )
    {
        if( gilbTimer % 4 == 0 ) { gilbMainAnim++; if( gilbMainAnim > 2 ) gilbMainAnim = 0; }
        gilbLorR = 1;
        sprite->x4Decalage--;
        if( gilbCollisionCheck( sprite ) == 1 ) sprite->x4Decalage++;
        if( sprite->x4Decalage < 0 ) { sprite->x4Decalage = 3; sprite->gridH--; }
    }

    gilbScrollUpdate( sprite );

    if( sprite->gridV >= 7 )
    {
        gilbSound2();
        gilbLive--;
        if( gilbLive == 0 ) gilbBeginIntroJingle();
        else gilbBeginLevel();
        return;
    }

    int x;
    for( x = 0; x < 33; x++ )
    {
        int levelS = ( x + gilbScrool ) / 4;
        if( gilbLevelType == 0 ) gilbLevelMult = gilbLevel0[levelS];
        else if( gilbLevelType == 1 ) gilbLevelMult = gilbLevel1[levelS];
        else if( gilbLevelType == 2 ) gilbLevelMult = gilbLevel2[levelS];
        else if( gilbLevelType == 3 ) gilbLevelMult = gilbLevel3[levelS];
        else if( gilbLevelType == 4 ) gilbLevelMult = gilbLevel4[levelS];
        else if( gilbLevelType == 5 ) gilbLevelMult = gilbLevel5[levelS];
        else if( gilbLevelType == 6 ) gilbLevelMult = gilbLevel6[levelS];
        else if( gilbLevelType == 7 ) gilbLevelMult = gilbLevel7[levelS];
        else if( gilbLevelType == 8 ) gilbLevelMult = gilbLevel8[levelS];
        else if( gilbLevelType == 9 ) gilbLevelMult = gilbLevel9[levelS];
        else { gilbBeginIntroJingle(); return; }

        int levelShift = ( ( x + gilbScrool ) % 4 ) + ( gilbLevelMult * 4 );

        gilbByteMem = gilbMap1Couche2[levelShift];
        if( gilbByteMem == 11 ) gilbMap[1][x] = gilbDelKey( x + gilbScrool, 1 );
        else gilbMap[1][x] = gilbByteMem;
        gilbByteMem = gilbMap1Couche3[levelShift];
        if( gilbByteMem == 11 ) gilbMap[2][x] = gilbDelKey( x + gilbScrool, 2 );
        else gilbMap[2][x] = gilbByteMem;
        gilbByteMem = gilbMap1Couche4[levelShift];
        if( gilbByteMem == 11 ) gilbMap[3][x] = gilbDelKey( x + gilbScrool, 3 );
        else gilbMap[3][x] = gilbByteMem;
        gilbByteMem = gilbMap1Couche5[levelShift];
        if( gilbByteMem == 11 ) gilbMap[4][x] = gilbDelKey( x + gilbScrool, 4 );
        else gilbMap[4][x] = gilbByteMem;
        gilbByteMem = gilbMap1Couche6[levelShift];
        if( gilbByteMem == 11 ) gilbMap[5][x] = gilbDelKey( x + gilbScrool, 5 );
        else gilbMap[5][x] = gilbByteMem;
        gilbByteMem = gilbMap1Couche7[levelShift];
        if( gilbByteMem == 11 ) gilbMap[6][x] = gilbDelKey( x + gilbScrool, 6 );
        else gilbMap[6][x] = gilbByteMem;
        gilbByteMem = gilbMap1Couche8[levelShift];
        if( gilbByteMem == 11 ) gilbMap[7][x] = gilbDelKey( x + gilbScrool, 7 );
        else gilbMap[7][x] = gilbByteMem;
    }

    if( gilbJump == 0 ) gilbGravityUpdate( sprite );

    if( isFirePressed() && gilbJump == 0 && gilbJumpCancel == 0 && gilbCollisionCheck( sprite ) == 0 )
    {
        if( sprite->y8Decalage == 0 ) gilbJump = 3;
    }
    if( !isFirePressed() ) gilbJumpCancel = 0;
    if( gilbJump > 0 ) gilbJumpProcedure( sprite );

    int gv = sprite->gridV;
    int gh = sprite->gridH;

    if( sprite->y8Decalage == 0 )
    {
        if( gilbMap[gv][gh] == 11 ) { gilbKey[gilbKeyS][1] = gv; gilbKey[gilbKeyS][0] = gilbScrool + gh; gilbKeyS++; gilbSound1(); }
        if( gilbMap[gv][gh+1] == 11 ) { gilbKey[gilbKeyS][1] = gv; gilbKey[gilbKeyS][0] = gilbScrool + gh + 1; gilbKeyS++; gilbSound1(); }
        if( gilbMap[gv][gh+2] == 11 ) { gilbKey[gilbKeyS][1] = gv; gilbKey[gilbKeyS][0] = gilbScrool + gh + 2; gilbKeyS++; gilbSound1(); }

        int hit8 = ( gilbMap[gv][gh] == 8 || gilbMap[gv][gh+1] == 8 || gilbMap[gv][gh+2] == 8 );
        if( hit8 && gilbInjur == 0 ) { gilbLive--; if( gilbLive == 0 ) { gilbBeginIntroJingle(); return; } gilbJump = 2; gilbInjur = 30; gilbSound2(); }

        int hitDoor = ( gilbMap[gv][gh] == 13 || gilbMap[gv][gh] == 14 ||
                         gilbMap[gv][gh+1] == 13 || gilbMap[gv][gh+1] == 14 ||
                         gilbMap[gv][gh+2] == 13 || gilbMap[gv][gh+2] == 14 );
        if( hitDoor && gilbKeyInLevel[gilbLevelType] == gilbKeyS && gilbJump > 0 ) { gilbNextLevel(); return; }

        gilbMap[gv][gh] = sprite->driftBL;
        gilbMap[gv][gh+1] = sprite->driftBR;
    }
    else
    {
        if( gilbMap[gv][gh] == 11 ) { gilbKey[gilbKeyS][1] = gv; gilbKey[gilbKeyS][0] = gilbScrool + gh; gilbKeyS++; gilbSound1(); }
        if( gilbMap[gv][gh+1] == 11 ) { gilbKey[gilbKeyS][1] = gv; gilbKey[gilbKeyS][0] = gilbScrool + gh + 1; gilbKeyS++; gilbSound1(); }
        if( gilbMap[gv][gh+2] == 11 ) { gilbKey[gilbKeyS][1] = gv; gilbKey[gilbKeyS][0] = gilbScrool + gh + 2; gilbKeyS++; gilbSound1(); }
        if( gilbMap[gv+1][gh] == 11 ) { gilbKey[gilbKeyS][1] = gv + 1; gilbKey[gilbKeyS][0] = gilbScrool + gh; gilbKeyS++; gilbSound1(); }
        if( gilbMap[gv+1][gh+1] == 11 ) { gilbKey[gilbKeyS][1] = gv + 1; gilbKey[gilbKeyS][0] = gilbScrool + gh + 1; gilbKeyS++; gilbSound1(); }
        if( gilbMap[gv+1][gh+2] == 11 ) { gilbKey[gilbKeyS][1] = gv + 1; gilbKey[gilbKeyS][0] = gilbScrool + gh + 2; gilbKeyS++; gilbSound1(); }

        int hit8 = ( gilbMap[gv][gh] == 8 || gilbMap[gv][gh+1] == 8 || gilbMap[gv][gh+2] == 8 ||
                      gilbMap[gv+1][gh] == 8 || gilbMap[gv+1][gh+1] == 8 || gilbMap[gv+1][gh+2] == 8 );
        if( hit8 && gilbInjur == 0 ) { gilbLive--; if( gilbLive == 0 ) { gilbBeginIntroJingle(); return; } gilbJump = 2; gilbInjur = 30; gilbSound2(); }

        int hitDoor = ( gilbMap[gv][gh] == 13 || gilbMap[gv][gh] == 14 ||
                         gilbMap[gv][gh+1] == 13 || gilbMap[gv][gh+1] == 14 ||
                         gilbMap[gv][gh+2] == 13 || gilbMap[gv][gh+2] == 14 ||
                         gilbMap[gv+1][gh] == 13 || gilbMap[gv+1][gh] == 14 ||
                         gilbMap[gv+1][gh+1] == 13 || gilbMap[gv+1][gh+1] == 14 ||
                         gilbMap[gv+1][gh+2] == 13 || gilbMap[gv+1][gh+2] == 14 );
        if( hitDoor && gilbKeyInLevel[gilbLevelType] == gilbKeyS && gilbJump > 0 ) { gilbNextLevel(); return; }

        gilbMap[gv][gh] = sprite->driftBL;
        gilbMap[gv][gh+1] = sprite->driftBR;
        gilbMap[gv+1][gh] = sprite->driftTL;
        gilbMap[gv+1][gh+1] = sprite->driftTR;
    }

    if( gilbTimer % 2 == 0 )
    {
        if( gilbInjur > 0 )
        {
            if( gilbVisible == 1 ) gilbVisible = 0; else gilbVisible = 1;
            gilbInjur--;
        }
    }

    gilbUpdateVerticalSlide( sprite );
    gilbTinyFlip( sprite );

    gilbTimer++;
    if( gilbTimer > 60 ) gilbTimer = 0;
}

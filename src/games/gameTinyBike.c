// =============================================================================
// Tiny Bike - ported from Daniel C's Tiny-Bike.ino v1.2 (tinyjoypad.com,
// GPLv3). Same tinyJoypadShim lineage as every other Daniel-C game here
// (FastTinyDriver.h) - Sound()/isXPressed() reuse the existing shim as-is.
//
// A BMX/motocross side-scroller: hold Fire to accelerate, tilt the wheelie
// angle with LEFT/RIGHT (upstream analogRead(A0)) and change track lane
// (height) with UP/DOWN (upstream analogRead(A3)) to jump ramps, dodge oil
// slicks/holes, and reach the finish line before the time bar runs out.
//
// Button mapping (cross-referenced against gameTinyPacman.c's own already-
// resolved thresholds for this exact analog ladder):
//   analogRead(A0) in [750,950) = isLeftPressed()  -> Wheel_up++
//   analogRead(A0) in (500,750) = isRightPressed() -> Wheel_up--
//   analogRead(A3) in [750,950) = isDownPressed()  -> Trackrun_progress++
//   analogRead(A3) in (500,750) = isUpPressed()    -> Trackrun_progress--
//   digitalRead(1) (active low) = isFirePressed()  -> accelerate
//
// Structural changes from upstream:
//  - upstream's loop() is a New_Games:/NEXT_LEVEL:/NEW_START: goto-chain
//    around one big while(1) - rewritten as an explicit frame-stepped state
//    machine (bikState), same approach as every other tinyJoypadShim port.
//    NEW_START's own busy-wait-for-release (`if(digitalRead(1)==0)goto
//    NEW_START;`) becomes BIK_STATE_WAIT_RELEASE, checked with a plain
//    isFirePressed() level read (matching upstream's own level check, not
//    an edge check) rather than md_armInputFireGate() - upstream re-enters
//    this exact wait a *second* time after the countdown if fire is still
//    down, which armInputFireGate()'s one-shot suppression doesn't model
//    as directly as just re-checking the level.
//  - intro_sound() (three Sound(100,255) calls each followed by a real
//    _delay_ms(400), then four back-to-back Sound(155,255) calls) and
//    End_Line_Win_sound() (5x paired Sound(100,100)/Sound(1,100)) are both
//    genuinely blocking multi-step sequences on real hardware - converted
//    to a small shared frame-stepped note sequencer (bikStartNoteSeq/
//    bikAdvanceNoteSeq), the same shape Arkanoid/Missile/Minez already use,
//    extended with a per-note `extraMs` field to reproduce the real
//    _delay_ms(400) gaps between the first three countdown beeps.
//  - `switch`/`case` (BACKGROUND/Higher_adj/analise_minutieuse/
//    SPLIT_MAP_BYTE/BLITZ_SPRITE_MAP/Return_live/Tiny_Flip's own mode
//    dispatch) rewritten as if/else-if chains - no other port in this
//    project has yet exercised a real `switch` statement (Tiny Doc
//    deliberately avoided being the first to test dialect support for it),
//    so this one follows the same established caution rather than being
//    the first to find out.
//  - `for(t=0;t<CHECK_SPEED_ADJ(ACCEL);t++)` re-evaluates CHECK_SPEED_ADJ()
//    (and its Higher_adj() side effect) on every loop iteration in the
//    original, even though ACCEL never changes inside that loop's own body
//    - the bound is computed once here instead, which produces the exact
//    same iteration count without the redundant recomputation.
//  - The 10us digitalWrite(4,...) pulse in INCREMENTE_SCROLL() drives the
//    same physical pin as the buzzer purely as a GPIO "tick" (not through
//    Sound() at all) - Vircon32 has no GPIO/pin concept to map this onto,
//    and a real 10-microsecond transient wouldn't be audible even if it
//    did, so it's dropped rather than approximated with an actual tone.
//  - `Sprite2PAINTinBLACK` (reset to 254 at the top of every Recupe() call,
//    only ever written inside the exact same condition that reads it in
//    the same statement) never actually influences control flow beyond
//    `SPRITERECUPE!=0` - dropped as genuinely dead state rather than ported
//    verbatim, the same treatment already given to other provably-inert
//    upstream state elsewhere in this project (e.g. Bomber/Doc's unused
//    data tables).
//  - `BigStepB`/`MinijumpB` (declared in spritebank.h, never referenced
//    anywhere in the .ino) are dead upstream data - not ported, same as
//    the also-unused `DScroll0`/`BScroll0` globals.
//  - Data tables extracted from spritebank.h with a small script (not
//    hand-transcribed), specifically to avoid repeating Tiny Bomber's
//    dropped-byte bug - every table's element count was verified against
//    the script's own parse rather than an eyeballed recount.
//  - No genuine upstream timing model (bare uncapped AVR loop, speed
//    itself already modulated by the ACCEL-driven repeat count in the
//    scroll-increment loop) - shipped unthrottled at native 60fps redraw,
//    matching this project's default for "no real rate to match" games;
//    revisit with a movement divisor if reported as too fast in play.
//  - Tiny_Flip(MODE)'s upstream PRINT/PRINT2 row-range table (mode 0 skips
//    row 7, mode 1 skips rows 0-1 - alternated every real gameplay frame
//    via Tiny_Flip(FOUL_BLITZ)) is the same real-SSD1306-VRAM-persistence
//    assumption already found and fixed in Pinball/Doc/Bert - bikTinyFlip()
//    always redraws all 8 rows regardless of mode from the start here,
//    avoiding the bug proactively rather than needing a later fix.
// =============================================================================

int[4] bikSTEP_BIKE_TRACKRUN =
{
23,28,33,38,
};

int[378] bikLevel0 =
{
54,23,162,242,48,242,2,13,162,13,242,48,13,162,21,49,13,242,162,49,
13,49,49,13,51,15,56,16,2,162,48,82,181,242,49,48,242,49,162,48,
49,49,49,82,82,49,101,86,166,48,51,15,65,18,48,48,48,13,162,21,
242,48,6,86,21,166,86,181,247,21,162,49,181,49,49,21,49,246,49,48,
51,15,65,18,49,49,48,6,86,21,162,21,242,166,246,48,6,242,86,21,
49,86,162,49,181,49,49,21,49,49,48,51,15,72,23,48,6,86,166,246,
48,162,21,86,6,246,166,48,2,82,48,162,242,49,82,162,181,86,166,49,
246,49,48,6,51,15,96,28,48,21,86,181,6,101,166,246,49,48,86,166,
166,246,48,6,86,247,246,48,181,246,49,86,181,6,101,48,86,162,242,166,
6,49,242,166,48,82,181,51,15,96,28,21,6,86,166,246,181,6,86,166,
246,49,181,48,181,49,166,246,48,6,48,21,246,166,86,181,6,101,166,86,
49,49,166,6,49,49,166,48,6,181,51,15,110,34,2,21,86,101,166,181,
48,48,48,49,6,49,86,49,166,49,246,49,162,101,6,101,246,101,6,101,
246,48,13,101,6,101,246,101,6,101,246,49,48,86,49,48,86,49,48,51,
15,124,34,49,48,246,49,166,49,48,6,49,181,48,49,6,49,6,21,181,
246,6,49,246,49,48,166,49,246,49,6,48,49,246,49,246,49,6,49,246,
49,6,21,181,246,247,48,49,48,49,86,49,48,51,15,97,25,48,246,48,
166,49,246,48,6,48,86,48,166,49,48,86,48,6,49,6,48,86,48,246,
49,166,49,6,246,48,13,48,13,49,86,48,166,48,13,49,51,14,
};

int[252] bikSTART_GAME =
{
50,5,254,1,5,125,5,1,117,1,125,9,121,1,13,113,13,1,62,124,
64,64,64,64,64,128,0,0,0,32,0,32,32,0,128,192,224,224,224,224,
192,128,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,62,
64,223,213,202,192,221,192,223,198,217,192,223,213,209,192,191,127,8,9,1,
193,240,248,255,255,249,240,49,51,59,31,15,3,6,4,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,18,0,18,18,18,18,
18,18,18,18,18,18,18,18,0,0,255,255,255,15,15,127,255,63,46,38,
224,240,242,254,60,56,16,0,0,0,0,0,0,0,16,0,17,16,17,17,
17,17,17,17,17,17,17,17,17,17,17,17,1,1,1,120,252,206,135,7,
15,31,159,252,248,120,124,15,7,15,31,23,247,251,159,31,15,7,143,254,
252,120,0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,0,0,0,1,3,3,3,3,3,1,0,0,0,0,0,0,
0,0,0,1,3,3,3,3,3,1,0,0,
};

int[102] bikSTART_RACE =
{
50,2,255,1,253,237,85,85,189,253,245,5,245,253,13,213,13,253,5,213,
45,253,245,5,245,253,253,253,253,253,5,181,149,77,253,253,13,213,213,13,
253,141,117,117,117,253,5,85,117,253,1,255,7,4,5,5,5,5,5,5,
5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
4,7,
};

int[102] bikNEXTRACE =
{
50,2,255,1,253,5,237,221,5,253,253,5,85,117,253,117,173,221,173,117,
253,245,245,5,245,245,253,253,253,253,5,181,149,77,253,253,13,213,213,13,
253,141,117,117,117,253,5,85,117,253,1,255,7,4,5,5,5,5,5,5,
5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
4,7,
};

int[8] bikFOUL =
{
128,140,129,140,128,176,132,176,
};

int[16] bikGRADIN =
{
86,174,86,170,86,174,86,170,5,2,9,2,5,2,5,2,
};

int[15] bikTIRE =
{
128,134,141,139,137,134,139,137,134,128,128,128,128,128,128,
};

int[30] bikROAD =
{
0,32,0,32,0,0,32,0,32,0,0,132,0,128,4,0,132,0,128,4,
176,16,80,16,80,16,80,16,176,240,
};

int[128] bikDISPLAY8 =
{
255,195,129,129,129,161,153,149,161,129,129,129,129,161,153,149,161,129,129,129,
129,161,153,149,161,129,129,129,195,255,255,195,129,129,133,189,133,129,129,165,
129,153,173,149,169,149,169,149,169,149,169,149,169,149,169,149,169,149,169,149,
169,149,169,149,169,149,169,149,169,149,169,149,169,181,153,129,195,255,255,195,
129,129,189,149,137,129,165,129,129,189,153,153,153,153,153,153,153,153,153,153,
153,153,153,153,153,153,153,153,153,153,153,153,153,153,153,153,153,153,153,153,
153,153,153,189,129,129,195,255,
};

int[106] bikBigStepA =
{
26,4,0,0,0,0,128,64,32,16,8,4,66,255,165,115,41,254,124,248,
240,224,192,128,0,0,0,0,248,4,66,1,16,0,132,0,33,0,8,255,
148,206,165,255,239,222,189,123,247,239,223,190,124,248,255,0,8,0,66,0,
16,0,132,64,33,31,18,25,20,63,125,251,247,239,222,189,123,247,239,222,
255,64,33,16,8,4,2,1,0,0,0,0,0,0,0,0,0,0,1,3,
7,15,31,62,125,251,
};

int[58] bikMinijumpA =
{
14,4,224,16,8,4,66,255,165,115,41,254,124,248,240,224,255,0,33,0,
8,255,148,206,165,255,239,222,189,123,255,0,132,64,33,31,18,25,20,63,
125,251,247,239,3,1,0,0,0,0,0,0,0,0,0,0,1,3,
};

int[17] bikhuile =
{
15,1,0,0,4,10,12,10,12,10,12,10,12,14,4,0,0,
};

int[20] bikLine =
{
6,3,255,51,51,204,204,255,255,51,51,204,204,255,15,3,3,12,12,15,
};

int[11] bikStart =
{
3,3,255,0,255,255,0,255,15,0,15,
};

int[35] bikSpeed =
{
11,3,0,0,0,128,192,224,240,248,60,30,255,252,30,143,199,227,255,127,
191,94,175,87,31,15,23,11,21,10,21,10,21,10,21,
};

int[13] bikplantageSprite =
{
11,1,1,3,14,14,12,8,9,11,14,14,12,
};

int[236] bikbike1 =
{
13,2,0,0,128,128,220,190,249,171,134,0,0,0,0,0,3,5,4,3,
0,1,15,22,18,12,0,0,0,0,0,240,124,242,214,140,128,128,0,0,
0,0,12,18,23,12,13,2,7,15,22,18,12,0,0,0,0,248,62,249,
181,227,224,160,128,0,0,0,12,18,19,14,6,0,1,3,5,4,3,0,
0,24,254,57,245,163,224,124,180,144,96,0,0,0,12,19,18,14,6,1,
0,0,0,0,0,0,30,57,117,179,96,180,24,44,36,24,0,0,0,0,
0,0,12,19,18,12,0,0,0,0,0,0,0,60,114,242,236,192,104,48,
88,72,48,0,0,0,0,0,0,0,1,14,19,18,12,0,0,0,0,0,
0,224,248,228,172,24,0,0,0,0,0,0,12,18,31,8,11,5,15,15,
31,18,12,0,0,0,0,224,120,228,204,152,192,160,0,0,0,0,12,18,
23,12,13,2,3,5,4,3,0,0,0,0,128,192,112,216,152,176,0,0,
0,0,0,6,9,11,7,12,13,2,7,31,37,24,0,0,
};

int[13] bikNewLive =
{
11,1,52,78,94,54,50,8,25,61,94,72,48,
};

int[16] bikDIM_SPRITE =
{
26,32,14,26,15,4,6,20,5,20,11,21,11,4,11,7,
};

// -----------------------------------------------------------------------------
//   Sprite state
// -----------------------------------------------------------------------------

#define BIK_NUM_SPRITE 2

struct BikSprite
{
    int active;
    int typeOfSprite;
    int xPos;
    int yPos;
    int yStart;
    int yEnd;
};

BikSprite[2] bikSprite;

int bikReWind;
int bikEndGame;
int bikLive;
int bikProgressBarInterval;
int bikProgressBarIntervalTimer;
int bikProgressBarValue;
int bikTimeBarInterval;
int bikTimeBarIntervalTimer;
int bikTimeBarValue;
int bikClimbActivate;
float bikHigherJump;
float bikAddPill;
float bikJumpDynamicDuration;
int bikPlancher;
int bikPlancherAdd;
int bikTransitionTrack;
int bikEndMap;
int bikLineY;
int bikDecalage;
int bikDiv1;
float bikAccel;
int bikTrackRun;
int bikTrackRunProgress;
float bikGravityExpo;
int bikBikePosY;
int bikTrigOk;
int bikMapPos;
int bikAnimBike;
int bikRenewSprite;
int bikNoSprite;
int bikFoulBlitz;
int bikVarScroll1;
int bikVarScroll2;
int bikVarScroll3;
int bikNotMove;
int bikNotTurn;
int bikWheelUp;
int bikFreeAir;
int bikBypassWheelUpReset;
int bikLatch1;
int bikPlantage0;
int bikPause;
int* bikIntroPic;

int bikDScroll1;
int bikDScroll2;
int bikDScroll3;
int bikBScroll1;
int bikBScroll2;
int bikBScroll3;

void bikResetSprite()
{
    int t;
    for( t = 0; t < BIK_NUM_SPRITE; t++ )
    {
        bikSprite[ t ].active = 0;
        bikSprite[ t ].typeOfSprite = 0;
        bikSprite[ t ].xPos = 0;
        bikSprite[ t ].yPos = 0;
        bikSprite[ t ].yStart = 0;
        bikSprite[ t ].yEnd = 0;
    }
}

void bikRestoreStartLine()
{
    bikSprite[ 0 ].active = 1;
    bikSprite[ 0 ].typeOfSprite = 5;
    bikSprite[ 0 ].xPos = 36;
    bikSprite[ 0 ].yPos = 32;
}

void bikNextLevel()
{
    bikEndMap = 0;
    bikClimbActivate = 0;
    bikHigherJump = 0;
    bikAddPill = 0;
    bikTransitionTrack = 0;
    bikDiv1 = 0;
    bikAccel = 0;
    bikTrackRun = 2;
    bikTrackRunProgress = 2;
    bikGravityExpo = 0;
    bikBikePosY = 33;
    bikTrigOk = 0;
    bikAnimBike = 1;
    bikRenewSprite = 0;
    bikNoSprite = 0;
    bikFoulBlitz = 0;
    bikVarScroll1 = 0;
    bikVarScroll2 = 0;
    bikVarScroll3 = 0;
    bikNotMove = 0;
    bikNotTurn = 0;
    bikWheelUp = 1;
    bikFreeAir = 0;
    bikBypassWheelUpReset = 0;
    bikLatch1 = 0;
    bikPlantage0 = 0;
    bikPause = 0;
    if( bikReWind == 1 ) { bikReWind = 0; bikMapPos = 0; }
    if( bikMapPos != 0 ) bikMapPos++;
    bikProgressBarInterval = bikLevel0[ bikMapPos ];
    bikProgressBarIntervalTimer = 0;
    bikProgressBarValue = 0;
    bikMapPos++;
    bikTimeBarInterval = bikLevel0[ bikMapPos ];
    bikTimeBarIntervalTimer = 0;
    bikTimeBarValue = 0;
    bikMapPos++;
    bikIntroPic = bikNEXTRACE;
}

void bikResetForNewGame()
{
    bikEndGame = 0;
    bikLive = 3;
    bikJumpDynamicDuration = 0;
    bikPlancher = 0;
    bikPlancherAdd = 0;
    bikLineY = 0;
    bikDecalage = 0;
    bikMapPos = 0;
    bikIntroPic = bikSTART_GAME;
}

// -----------------------------------------------------------------------------
//   Frame-stepped note sequencer - replaces intro_sound()'s three blocking
//   Sound()+_delay_ms(400) pairs followed by four back-to-back Sound()
//   calls, and End_Line_Win_sound()'s 5x paired Sound() calls. `extraMs`
//   reproduces a real _delay_ms() gap after that note's own tone finishes
//   (0 for notes with no gap upstream).
// -----------------------------------------------------------------------------

int* bikNoteFreqTable;
int* bikNoteDurTable;
int* bikNoteExtraMsTable;
int bikNoteCount;
int bikNoteIndex;
int bikNoteWaitFrames;
int bikNoteSeqActive;

int[7] bikIntroFreq = { 100, 100, 100, 155, 155, 155, 155 };
int[7] bikIntroDur  = { 255, 255, 255, 255, 255, 255, 255 };
int[7] bikIntroExtraMs = { 400, 400, 400, 0, 0, 0, 0 };

int[10] bikWinFreq = { 100, 1, 100, 1, 100, 1, 100, 1, 100, 1 };
int[10] bikWinDur  = { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 };
int[10] bikWinExtraMs = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

void bikStartNoteSeq( int* freqs, int* durs, int* extras, int count )
{
    bikNoteFreqTable = freqs;
    bikNoteDurTable = durs;
    bikNoteExtraMsTable = extras;
    bikNoteCount = count;
    bikNoteIndex = 0;
    bikNoteWaitFrames = 0;
    bikNoteSeqActive = 1;
}

// Returns true once the whole sequence has finished playing.
int bikAdvanceNoteSeq()
{
    if( !bikNoteSeqActive )
      return 1;

    if( bikNoteWaitFrames > 0 )
    {
        bikNoteWaitFrames--;
        return 0;
    }

    if( bikNoteIndex >= bikNoteCount )
    {
        bikNoteSeqActive = 0;
        return 1;
    }

    int freq = bikNoteFreqTable[ bikNoteIndex ];
    int dur = bikNoteDurTable[ bikNoteIndex ];
    int extraMs = bikNoteExtraMsTable[ bikNoteIndex ];

    Sound( freq, dur );

    int periodUs = 255 - freq;
    if( periodUs < 1 ) periodUs = 1;
    float freqHz = 500000.0 / (float)periodUs;
    float durationSeconds = (float)dur / freqHz;
    int waitFrames = (int)( durationSeconds * 60.0 );
    if( waitFrames < 1 ) waitFrames = 1;
    waitFrames += (int)( ( (float)extraMs / 1000.0 ) * 60.0 );

    bikNoteWaitFrames = waitFrames;
    bikNoteIndex++;
    return 0;
}

// -----------------------------------------------------------------------------
//   Gameplay logic
// -----------------------------------------------------------------------------

int bikGamePlay()
{
    if( bikLive == -1 )
      bikEndGame = 1;
    return 0;
}

void bikTimeTrack()
{
    if( bikTimeBarIntervalTimer < bikTimeBarInterval )
      bikTimeBarIntervalTimer++;
    else
    {
        bikTimeBarIntervalTimer = 0;
        if( bikTimeBarValue < 34 ) bikTimeBarValue++;
        else bikEndGame = 1;
    }
}

void bikPlantage()
{
    if( bikAccel > 1.4 )
      bikAccel = bikAccel - 0.10;
    else
    {
        bikPlantage0 = 0;
        bikWheelUp = 1;
        bikPause = 1;
        return;
    }
    if( bikWheelUp > 0 ) bikWheelUp--;
    else bikWheelUp = 5;
}

void bikHigherAdj( int test )
{
    if( test >= 0 && test <= 2 ) bikHigherJump = 0.24;
    else if( test >= 3 && test <= 5 ) bikHigherJump = 0.18;
    else if( test >= 6 && test <= 8 ) bikHigherJump = 0.046;
}

void bikGravityAdj()
{
    bikPlancher = bikSTEP_BIKE_TRACKRUN[ bikTrackRun ];
    if( bikTrackRun == bikTrackRunProgress )
    {
        if( bikBikePosY < ( bikPlancher - bikPlancherAdd ) )
        {
            bikBikePosY = bikBikePosY + bikGravityExpo;
            bikGravityExpo = bikGravityExpo + 0.2;
            bikFreeAir = 1;
        }
        else
        {
            bikFreeAir = 0;
            bikNotTurn = 0;
            bikBikePosY = bikPlancher - bikPlancherAdd;
            bikGravityExpo = 0;
        }
    }
}

void bikJumpAdj()
{
    if( bikJumpDynamicDuration >= 0.24 )
    {
        bikAddPill = bikAddPill + bikJumpDynamicDuration;
        bikJumpDynamicDuration = bikJumpDynamicDuration - bikHigherJump;
    }
    else
    {
        bikAddPill = 0;
        bikJumpDynamicDuration = 0;
    }
    if( bikAddPill >= 1 )
    {
        bikAddPill = bikAddPill - 1;
        if( bikBikePosY > 0 ) bikBikePosY = bikBikePosY - 1;
    }
}

void bikDynamicAdj()
{
    if( bikJumpDynamicDuration != 0 )
    {
        bikNotMove = 1;
        bikJumpAdj();
    }
    else
    {
        bikGravityAdj();
        bikClimbActivate = 0;
        bikNotMove = 0;
    }
}

// POS_JUMP - upstream's own #define, reused as a plain helper here since
// it depends on bikNoSprite (the currently-colliding sprite index), which
// isn't known until the caller has already found a collision.
int bikPosJump()
{
    return 30 - bikSprite[ bikNoSprite ].xPos;
}

void bikBreakGravity()
{
    if( bikPosJump() == 0 )
    {
        bikGravityExpo = 0;
        if( bikFreeAir == 0 && bikWheelUp == 1 )
          if( bikAccel > 5 )
            bikAccel = bikAccel - 2;
    }
}

void bikClimbAdj0()
{
    bikBreakGravity();
    bikWheelUp = 3;
    if( bikPosJump() < 11 ) { bikNotTurn = 1; bikClimbActivate = 1; bikPlancherAdd++; }
    else if( bikPosJump() > 15 ) bikPlancherAdd--;
}

void bikClimbAdj1()
{
    bikBreakGravity();
    bikWheelUp = 3;
    if( bikPosJump() < 5 ) { bikNotTurn = 1; bikClimbActivate = 1; bikPlancherAdd++; }
    else if( bikPosJump() > 9 ) bikPlancherAdd--;
}

void bikClimbAdj2()
{
    bikBreakGravity();
    bikWheelUp = 3;
    if( bikPosJump() < 9 ) { bikNotTurn = 1; bikClimbActivate = 1; bikPlancherAdd++; }
    else bikPlancherAdd = 0;
}

void bikClimbAdj3()
{
    if( bikPosJump() == 0 && bikFreeAir == 0 )
      bikPlantage0 = 1;
}

void bikAddLive( int spr )
{
    bikSprite[ spr ].active = 0;
    if( bikLive < 3 ) bikLive++;
    Sound( 60, 4 );
    Sound( 200, 4 );
    Sound( 120, 4 );
}

void bikAnaliseMinutieuse()
{
    int type = bikSprite[ bikNoSprite ].typeOfSprite;
    if( type == 1 ) bikClimbAdj0();
    else if( type == 2 ) bikClimbAdj1();
    else if( type == 3 ) { if( bikAccel > 2 && bikFreeAir == 0 ) bikAccel = bikAccel - 0.20; }
    else if( type == 4 ) { bikEndMap = 1; Sound( 200, 4 ); }
    else if( type == 5 ) { }
    else if( type == 6 ) bikClimbAdj2();
    else if( type == 7 ) bikClimbAdj3();
    else if( type == 8 ) { if( bikFreeAir == 0 ) bikAddLive( bikNoSprite ); }
}

int bikRecupeXSprite( int typeSprite )
{
    return bikDIM_SPRITE[ ( typeSprite - 1 ) * 2 ];
}

int bikTrouverSpriteCollisionner()
{
    int xBike = 30;
    int t;
    for( t = 0; t < BIK_NUM_SPRITE; t++ )
    {
        if( bikSprite[ t ].active != 0 )
        {
            if( bikSprite[ t ].xPos > xBike ||
                ( bikSprite[ t ].xPos + bikRecupeXSprite( bikSprite[ bikNoSprite ].typeOfSprite ) ) < xBike ||
                bikTrackRun > bikSprite[ t ].yEnd ||
                bikTrackRun < bikSprite[ t ].yStart )
              continue;
            bikNoSprite = t;
            return 1;
        }
    }
    return 0;
}

void bikCheckCollision()
{
    if( bikTrouverSpriteCollisionner() != 0 )
    {
        bikAnaliseMinutieuse();
        bikNotMove = 1;
    }
}

int bikCheckSpeedAdj( float recInt )
{
    int ret = 0;
    while( 1 )
    {
        if( recInt > 1 ) { ret++; recInt = recInt - 1; }
        else { bikHigherAdj( ret ); return ret; }
    }
}

void bikTrackRunAdj()
{
    if( bikTrackRun != bikTrackRunProgress )
    {
        if( bikTrackRun < bikTrackRunProgress && bikTransitionTrack == 0 ) bikTransitionTrack = 5;
        if( bikTrackRun > bikTrackRunProgress && bikTransitionTrack == 0 ) bikTransitionTrack = -5;

        if( bikTransitionTrack > 0 ) { bikTransitionTrack--; bikAnimBike = 8; bikBikePosY++; }
        else if( bikTransitionTrack < 0 ) { bikTransitionTrack++; bikAnimBike = 7; bikBikePosY--; }
        if( bikTransitionTrack == 0 ) { bikTrigOk = 0; bikTrackRun = bikTrackRunProgress; }
    }
}

int bikSplitMapByte( int byteVal, int l0R1 )
{
    if( l0R1 == 0 ) return byteVal >> 6;
    if( l0R1 == 1 ) return ( byteVal >> 4 ) & 3; // 0b00000011
    if( l0R1 == 2 ) return byteVal & 15; // 0b00001111
    return 0;
}

void bikRefreshPosSprite()
{
    int t;
    for( t = 0; t < BIK_NUM_SPRITE; t++ )
    {
        if( bikSprite[ t ].active != 0 )
        {
            if( bikSprite[ t ].xPos <= -26 ) bikSprite[ t ].active = 0;
            else bikSprite[ t ].xPos--;
        }
    }
    bikCheckCollision();
}

int bikCreateNewSprite()
{
    int type = bikSplitMapByte( bikLevel0[ bikMapPos ], 2 );
    int recupeYPos0 = bikSplitMapByte( bikLevel0[ bikMapPos ], 0 );
    int recupeYPos = recupeYPos0 * 5;
    int t;
    for( t = 0; t < BIK_NUM_SPRITE; t++ )
    {
        if( bikSprite[ t ].active == 0 )
        {
            bikSprite[ t ].xPos = 127;
            bikSprite[ t ].active = 1;
            bikSprite[ t ].yStart = recupeYPos0;
            bikSprite[ t ].yEnd = bikSplitMapByte( bikLevel0[ bikMapPos ], 1 );

            if( type == 0 ) { bikSprite[ t ].typeOfSprite = 1; bikSprite[ t ].yPos = 20; }
            else if( type == 1 ) { bikSprite[ t ].typeOfSprite = 2; bikSprite[ t ].yPos = 26; }
            else if( type == 2 ) { bikSprite[ t ].typeOfSprite = 3; bikSprite[ t ].yPos = recupeYPos + 32; }
            else if( type == 3 ) { bikSprite[ t ].typeOfSprite = 4; bikSprite[ t ].yPos = 32; }
            else if( type == 4 ) { bikSprite[ t ].typeOfSprite = 5; bikSprite[ t ].yPos = 32; }
            else if( type == 5 ) { bikSprite[ t ].typeOfSprite = 6; bikSprite[ t ].yPos = recupeYPos + 22; }
            else if( type == 6 ) { bikSprite[ t ].typeOfSprite = 7; bikSprite[ t ].yPos = recupeYPos + 34; }
            else if( type == 7 ) { bikSprite[ t ].typeOfSprite = 8; bikSprite[ t ].yPos = recupeYPos + 30; }
            else bikSprite[ t ].typeOfSprite = 16;

            bikRenewSprite = 0;
            if( type == 14 ) bikReWind = 1;
            if( type != 15 && bikReWind != 1 ) bikMapPos++;
            return 0;
        }
    }
    return 0;
}

void bikIncrementeScroll()
{
    if( bikVarScroll3 == 0 )
    {
        bikVarScroll3 = 1;
        if( bikVarScroll2 == 0 )
        {
            bikVarScroll2 = 1;
            if( bikVarScroll1 == 0 ) bikVarScroll1 = 1;
            else bikVarScroll1 = 0;
        }
        else bikVarScroll2 = 0;
    }
    else bikVarScroll3 = 0;

    if( bikVarScroll3 == 1 )
    {
        bikVarScroll3 = 2;
        if( bikProgressBarIntervalTimer < bikProgressBarInterval ) bikProgressBarIntervalTimer++;
        else
        {
            bikProgressBarIntervalTimer = 0;
            if( bikProgressBarValue < 32 ) bikProgressBarValue++;
        }
        if( bikRenewSprite < 64 ) bikRenewSprite++;
        else bikCreateNewSprite();

        if( bikJumpDynamicDuration != 0 ) { bikNotMove = 1; bikJumpAdj(); }
        if( bikClimbActivate ) { bikClimbActivate = 0; bikJumpDynamicDuration = 1; }

        bikRefreshPosSprite();
        if( bikDScroll3 < 9 ) bikDScroll3++; else bikDScroll3 = 0;
    }
    if( bikVarScroll2 == 1 )
    {
        bikVarScroll2 = 2;
        if( bikDScroll2 < 14 ) bikDScroll2++; else bikDScroll2 = 0;
    }
    if( bikVarScroll1 == 1 )
    {
        bikVarScroll1 = 2;
        if( bikDScroll1 < 7 ) bikDScroll1++; else bikDScroll1 = 0;
    }
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int bikFoul1()
{
    if( bikBScroll1 < 7 ) bikBScroll1++; else bikBScroll1 = 0;
    return bikFOUL[ bikBScroll1 ];
}

int bikGradin23( int yPass )
{
    int mul = 0;
    if( yPass == 2 ) return 0;
    if( bikBScroll1 < 7 ) bikBScroll1++; else bikBScroll1 = 0;
    return bikGRADIN[ bikBScroll1 + mul ];
}

int bikTire4()
{
    if( bikBScroll2 < 14 ) bikBScroll2++; else bikBScroll2 = 0;
    return bikTIRE[ bikBScroll2 ];
}

int bikRoad567( int yPass )
{
    int mul = 0;
    if( yPass == 5 ) mul = 10;
    if( yPass == 6 ) mul = 20;
    if( bikBScroll3 < 9 ) bikBScroll3++; else bikBScroll3 = 0;
    return bikROAD[ bikBScroll3 + mul ];
}

int bikReturnLive( int xPass )
{
    int startBlack = 26;
    if( bikLive == -1 || bikLive == 0 ) startBlack = 4;
    else if( bikLive == 1 ) startBlack = 11;
    else if( bikLive == 2 ) startBlack = 19;
    else if( bikLive == 3 ) startBlack = 26;

    if( xPass < startBlack ) return 0x00;
    if( xPass > 26 ) return 0x00;
    return 126; // 0b01111110
}

int bikReturnTime( int xPass )
{
    if( xPass >= ( 40 + ( 35 - bikTimeBarValue ) ) && xPass <= 75 ) return 60; // 0b00111100
    return 0x00;
}

int bikReturnProgress( int xPass )
{
    if( xPass == ( bikProgressBarValue + 90 ) ) return 24; // 0b00011000
    return 0x00;
}

int bikTableau8( int xPass )
{
    return ( 0xff - bikDISPLAY8[ xPass ] ) | bikReturnLive( xPass ) | bikReturnTime( xPass ) | bikReturnProgress( xPass );
}

int bikBackground( int xPass, int yPass )
{
    if( yPass == 0 ) return bikFoul1();
    if( yPass == 1 ) return bikGradin23( yPass );
    if( yPass == 2 ) return bikGradin23( yPass );
    if( yPass == 3 ) return bikTire4();
    if( yPass == 4 ) return bikRoad567( yPass );
    if( yPass == 5 ) return bikRoad567( yPass );
    if( yPass == 6 ) return bikRoad567( yPass );
    if( yPass == 7 ) return 0xff - bikTableau8( xPass );
    return 0x00;
}

void bikAdjustVarScroll()
{
    bikBScroll1 = bikDScroll1;
    bikBScroll2 = bikDScroll2;
    bikBScroll3 = bikDScroll3;
}

void bikRecupeDecalageY( int valeur )
{
    bikLineY = 0;
    bikDecalage = valeur;
    while( bikDecalage > 7 ) { bikDecalage = bikDecalage - 8; bikLineY++; }
}

int bikSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown ) return input << decalage;
    return input >> ( 8 - decalage );
}

int bikBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    bikRecupeDecalageY( yPos );
    int wSprite = sprites[ 0 ];
    int hSprite = sprites[ 1 ];
    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos ||
        bikLineY > yPass || ( bikLineY + hSprite ) < yPass )
      return 0x00;

    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int spriteYLine = yPass - bikLineY;
    int scanA = ( ( xPass - xPos ) + ( spriteYLine * wSprite ) ) + 2;
    int scanB = ( ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) ) + 2;

    int outByte;
    if( scanA > wMax )
      outByte = 0x00;
    else
      outByte = bikSplitSpriteDecalageY( bikDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = bikSplitSpriteDecalageY( bikDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

int bikBikeSprite( int xPass, int yPass )
{
    return bikBlitzSprite( 24, bikBikePosY, xPass, yPass, bikAnimBike, bikbike1 );
}

int* bikSpriteTableForType( int type )
{
    if( type == 1 ) return bikBigStepA;
    if( type == 2 ) return bikMinijumpA;
    if( type == 3 ) return bikhuile;
    if( type == 4 ) return bikLine;
    if( type == 5 ) return bikStart;
    if( type == 6 ) return bikSpeed;
    if( type == 7 ) return bikplantageSprite;
    if( type == 8 ) return bikNewLive;
    return NULL;
}

// bikBlitzSpriteMap()/bikBikeSprite() used to be called unconditionally for
// all 1024 pixels/frame (each internally self-gated via bikBlitzSprite()'s
// own bounds check, but still costing a full function call every time -
// this project's own established "self-gating avoids wasted work, not
// wasted calls" lesson, already fixed the same way for Bomber/Pacman/Doc/
// Bert). Measured directly with the perf overlay: CPU was pegged at a
// steady 100% throughout ordinary gameplay before this fix.
//
// bikCompositeSpriteMapRow(y) composites once per page row into a shared
// buffer instead, walking only the (up to 2) active obstacle sprites and
// writing just their own narrow column range - preserves the original
// per-pixel "first active sprite with a nonzero pixel wins" priority
// (sprite index 0 checked before 1) by only writing a column that isn't
// already set. bikBikeSprite's own call site is gated to its fixed
// x in [24,36] footprint (xPos=24, width 13 from bikbike1's own header) -
// its Y range varies with bikBikePosY so isn't similarly narrowed, but the
// x-range alone already cuts its call count by ~90%.
int[128] bikSpritePageBuffer;

void bikCompositeSpriteMapRow( int y )
{
    int cx;
    for( cx = 0; cx < 128; cx++ )
      bikSpritePageBuffer[ cx ] = 0;

    if( y < 2 || y > 6 )
      return;

    int t;
    for( t = 0; t < BIK_NUM_SPRITE; t++ )
    {
        if( bikSprite[ t ].active == 0 ) continue;

        int* table = bikSpriteTableForType( bikSprite[ t ].typeOfSprite );
        if( table == NULL ) continue;

        int xPos = bikSprite[ t ].xPos;
        int wSprite = table[ 0 ];
        int xs = xPos;
        int xe = xPos + wSprite - 1;
        if( xs < 0 ) xs = 0;
        if( xe > 127 ) xe = 127;

        int cx2;
        for( cx2 = xs; cx2 <= xe; cx2++ )
        {
            if( bikSpritePageBuffer[ cx2 ] != 0 ) continue;
            int v = bikBlitzSprite( xPos, bikSprite[ t ].yPos, cx2, y, 0, table );
            if( v != 0 ) bikSpritePageBuffer[ cx2 ] = v;
        }
    }
}

int bikRecupe( int xPass, int yPass )
{
    int bike = 0;
    if( xPass >= 24 && xPass <= 36 )
      bike = bikBikeSprite( xPass, yPass );
    return bikBackground( xPass, yPass ) | bike | bikSpritePageBuffer[ xPass ];
}

// mode 2 draws bikIntroPic full-screen (attract/level-intro pictures); mode
// 3 draws the real gameplay composite once (the start-line still frame);
// mode 0/1 (unused upstream after this porting pass folded them together)
// also draw the gameplay composite - kept distinct only so FOUL_BLITZ's
// alternating value (passed in as `mode` during real gameplay, matching
// upstream's own Tiny_Flip(FOUL_BLITZ) call) still reads naturally.
void bikTinyFlip( int mode )
{
    int x, y;
    md_beginFrame();
    for( y = 0; y < 8; y++ )
    {
        bikAdjustVarScroll();
        if( mode != 2 )
          bikCompositeSpriteMapRow( y );
        for( x = 0; x < 128; x++ )
        {
            int pixel;
            if( mode == 2 )
              pixel = bikBlitzSprite( 38, 30, x, y, 0, bikIntroPic );
            else
              pixel = bikRecupe( x, y );
            md_drawColumn( x, y, pixel );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine (replaces loop()'s New_Games:/NEXT_LEVEL:/NEW_START:
//   goto-chain and its enclosing while(1)s)
// -----------------------------------------------------------------------------

#define BIK_STATE_ATTRACT          0
#define BIK_STATE_LEVEL_INTRO_WAIT 1
#define BIK_STATE_WAIT_RELEASE     2
#define BIK_STATE_START_LINE       3
#define BIK_STATE_PLAYING          4
#define BIK_STATE_LEVEL_WIN_WAIT   5
#define BIK_STATE_GAME_OVER_WAIT   6

int bikState;
int bikWaitFrames;
int bikForceRedraw;

void bikBeginAttract()
{
    bikState = BIK_STATE_ATTRACT;
    bikResetForNewGame();
}

void bikBeginLevelIntroWait()
{
    bikState = BIK_STATE_LEVEL_INTRO_WAIT;
    bikWaitFrames = 120; // ~2000ms at 60fps
}

void bikBeginWaitRelease()
{
    bikState = BIK_STATE_WAIT_RELEASE;
}

void bikBeginStartLine()
{
    bikState = BIK_STATE_START_LINE;
    bikRestoreStartLine();
    bikStartNoteSeq( bikIntroFreq, bikIntroDur, bikIntroExtraMs, 7 );
}

void bikBeginPlaying()
{
    bikState = BIK_STATE_PLAYING;
    bikAnimBike = 6;
}

int bikWinWaitStarted;

void bikBeginLevelWinWait()
{
    bikState = BIK_STATE_LEVEL_WIN_WAIT;
    bikWinWaitStarted = 0;
    bikStartNoteSeq( bikWinFreq, bikWinDur, bikWinExtraMs, 10 );
}

void bikBeginGameOverWait()
{
    bikState = BIK_STATE_GAME_OVER_WAIT;
    bikWaitFrames = 120; // ~2000ms at 60fps
}

void gameTinyBike_init()
{
    InitTinyJoypad();
    bikBeginAttract();
}

// Quit-confirmation-dialog resume hook (see menuGameList.c's own comment
// on this pattern) - BIK_STATE_WAIT_RELEASE has no timer of its own (it
// waits for Fire to be released) and BIK_STATE_LEVEL_WIN_WAIT/
// GAME_OVER_WAIT only self-correct after a couple real seconds - all
// three skip their own redraw call entirely, so without this the
// dialog's leftover pixels could persist indefinitely (WAIT_RELEASE) or
// visibly linger for a couple seconds (the other two) after cancelling.
void gameTinyBike_forceRedraw()
{
    bikForceRedraw = 1;
}

void gameTinyBike_update()
{
    if( bikState == BIK_STATE_ATTRACT )
    {
        bikTinyFlip( 2 );
        if( isFirePressed() )
        {
            bikIntroPic = bikSTART_RACE;
            Sound( 200, 20 );
            Sound( 60, 20 );
            bikBeginLevelIntroWait();
        }
        return;
    }

    if( bikState == BIK_STATE_LEVEL_INTRO_WAIT )
    {
        bikTinyFlip( 2 );
        bikWaitFrames--;
        if( bikWaitFrames <= 0 )
        {
            bikResetSprite();
            bikNextLevel();
            bikBeginWaitRelease();
        }
        return;
    }

    if( bikState == BIK_STATE_WAIT_RELEASE )
    {
        // mode 2 (not mode 0) would be wrong here: bikNextLevel() already
        // reassigned bikIntroPic to bikNEXTRACE on the way into this
        // state (preparing the picture for the *next* LEVEL_INTRO_WAIT
        // screen, not this one) - redrawing mode 2 would show "NEXT
        // RACE" prematurely. mode 0 redraws the current gameplay
        // composite instead (the bike already reset to its start-line
        // position by this point), which is both correct and safe to
        // call again - bikAdjustVarScroll()/bikCompositeSpriteMapRow()
        // are pure reads of already-current state, no side effects from
        // an extra call.
        if( bikForceRedraw )
        {
            bikTinyFlip( 0 );
            bikForceRedraw = 0;
        }
        if( !isFirePressed() )
          bikBeginStartLine();
        return;
    }

    if( bikState == BIK_STATE_START_LINE )
    {
        bikTinyFlip( 3 );
        int seqDone = bikAdvanceNoteSeq();
        if( seqDone )
        {
            if( isFirePressed() )
              bikBeginWaitRelease();
            else
              bikBeginPlaying();
        }
        return;
    }

    if( bikState == BIK_STATE_PLAYING )
    {
        if( bikPlantage0 == 0 )
        {
            if( bikTrigOk == 0 )
            {
                if( isLeftPressed() && bikAccel > 1 )
                {
                    bikBypassWheelUpReset = 1;
                    if( bikWheelUp < 5 && bikLatch1 == 0 ) bikWheelUp++;
                }
                else if( isRightPressed() && bikAccel > 1 )
                {
                    bikBypassWheelUpReset = 1;
                    if( bikWheelUp > 0 && bikLatch1 == 0 )
                    {
                        bikWheelUp--;
                        if( bikWheelUp == 0 && bikFreeAir == 0 ) bikWheelUp = 1;
                    }
                }
                if( bikNotMove == 0 && bikNotTurn == 0 )
                {
                    if( isDownPressed() ) { if( bikTrackRunProgress < 3 ) { bikTrackRunProgress++; bikTrigOk = 1; } }
                    else if( isUpPressed() ) { if( bikTrackRunProgress > 0 ) { bikTrackRunProgress--; bikTrigOk = 2; } }
                }
                if( bikTrigOk == 0 && bikWheelUp == 1 )
                {
                    if( bikAnimBike == 1 ) bikAnimBike = 6;
                    else bikAnimBike = 1;
                }
            }
            if( isFirePressed() && bikEndMap != 1 && ( bikFreeAir == 0 || bikWheelUp <= 2 ) && bikEndGame == 0 )
            {
                if( bikAccel < 8 ) bikAccel = bikAccel + 0.10;
            }
            else
            {
                if( bikAccel > 1 ) bikAccel = bikAccel - 0.10;
                bikBypassWheelUpReset = 0;
                if( bikWheelUp > 1 && bikLatch1 == 0 && bikFreeAir == 0 ) bikWheelUp--;
            }
            if( bikBypassWheelUpReset == 0 )
            {
                if( bikWheelUp > 1 && bikLatch1 == 0 && bikFreeAir == 0 ) bikWheelUp--;
            }
            else bikBypassWheelUpReset = 0;
        }
        else bikPlantage();

        bikDynamicAdj();

        int speedTicks = bikCheckSpeedAdj( bikAccel );
        int t;
        for( t = 0; t < speedTicks; t++ )
        {
            bikIncrementeScroll();
            if( bikDiv1 == 3 )
            {
                bikTrackRunAdj();
                if( bikWheelUp != 1 ) bikAnimBike = bikWheelUp;
                bikDiv1 = 0;
            }
            else bikDiv1++;
        }

        if( bikPause == 1 ) { if( bikLive > -1 ) bikLive--; bikPause = 0; }
        bikLatch1++;
        if( bikLatch1 == 4 ) bikLatch1 = 0;
        if( bikFreeAir == 0 && ( bikWheelUp == 0 || bikWheelUp == 5 ) && bikPlantage0 != 1 )
          bikPlantage0 = 1;

        bikTinyFlip( bikFoulBlitz );
        bikFoulBlitz = !bikFoulBlitz;
        bikTimeTrack();

        if( bikEndMap == 1 )
        {
            if( bikAccel <= 1 )
            {
                bikEndGame = 0;
                bikBeginLevelWinWait();
                return;
            }
        }
        if( bikEndGame == 1 )
        {
            if( bikAccel <= 1 )
            {
                bikBeginGameOverWait();
                return;
            }
        }
        bikGamePlay();
        return;
    }

    if( bikState == BIK_STATE_LEVEL_WIN_WAIT )
    {
        // Real bug found via direct user report: mode 2 shows
        // bikIntroPic full-screen, but by the time the player has
        // actually reached the finish (this state), bikIntroPic is
        // already stale - it was last set by bikNextLevel() to
        // bikNEXTRACE, for the *next* LEVEL_INTRO_WAIT screen, not this
        // one. Cancelling the quit dialog from here incorrectly showed
        // "NEXT RACE" over the finish line instead of the frozen
        // gameplay frame this state is supposed to just leave on screen
        // (upstream/this state never redraws on its own, relying on the
        // last real gameplay frame persisting). Fixed by redrawing with
        // mode 0 (the gameplay composite) instead, using still-current
        // sprite/track state - reproduces the exact frozen frame.
        if( bikForceRedraw )
        {
            bikTinyFlip( 0 );
            bikForceRedraw = 0;
        }
        int seqDone = bikAdvanceNoteSeq();
        if( seqDone )
        {
            if( bikWinWaitStarted == 0 )
            {
                bikWinWaitStarted = 1;
                bikWaitFrames = 120; // ~2000ms at 60fps
            }
            bikWaitFrames--;
            if( bikWaitFrames <= 0 )
              bikBeginLevelIntroWait();
        }
        return;
    }

    if( bikState == BIK_STATE_GAME_OVER_WAIT )
    {
        // Same fix as BIK_STATE_LEVEL_WIN_WAIT above - mode 0 (gameplay
        // composite, the frozen crash frame) instead of mode 2
        // (bikIntroPic, which would show a stale/incorrect picture here
        // too).
        if( bikForceRedraw )
        {
            bikTinyFlip( 0 );
            bikForceRedraw = 0;
        }
        bikWaitFrames--;
        if( bikWaitFrames <= 0 )
          bikBeginAttract();
        return;
    }
}

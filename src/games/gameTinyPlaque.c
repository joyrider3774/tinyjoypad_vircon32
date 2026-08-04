// =============================================================================
// Tiny Plaque - ported from Daniel C's tiny-plaque.ino (tinyjoypad.com,
// GPLv3). Same tinyJoypadShim lineage (FastTinyDriver.h) as every other
// Daniel-C game here - button thresholds confirmed against this game's own
// PictureTPLAQUE.h, matching every other title's established A0/A3 reads.
//
// A dental-hygiene shooter: a toothpaste-tube submarine drifts in the
// water channel between two rows of teeth (top/bottom, 8 each). Loose
// food/plaque particles drift and, if they touch a healthy tooth, stick to
// it and start attacking it (turning it "dirty"); shoot particles with the
// toothpaste weapon before they attach. A tooth attacked too long is
// destroyed. Clearing every particle in a level banks any leftover tube
// fuel as bonus score/teeth, then the level advances.
//
// Structural changes from upstream:
//  - Real, 3-level-deep C++ inheritance (Sprite_TPLAQUE -> Moving_Sprite_
//    TPLAQUE -> Food_Sprite_TPLAQUE, plus Sprite_TPLAQUE -> Main_Sprite_
//    TPLAQUE and Sprite_TPLAQUE -> Weapon_Sprite_TPLAQUE) - the deepest
//    class hierarchy found in this project so far (goto=20, also the
//    highest goto count of any Tiny X title, confirmed via a fresh grep
//    before picking this game). Flattened into one combined `TplaqSprite`
//    struct holding the union of every field any of the 4 flavors ever
//    uses (base position/direction/active, moving-sprite accel
//    accumulators, food-only colapsed/startPos) - same "flatten to one
//    struct + explicit-pointer functions" treatment as Tiny Missile/Tiny
//    Pipe's own smaller class headers, just with more fields shared here.
//  - upstream's `loop()` is a `NEW_GAME:` goto-chain around a title-screen
//    wait and a gameplay `while(1)` with several genuine, felt
//    `_delay_ms()` sequences nested inside it (a fuel-to-score decounting
//    loop, then a per-tooth restore/dispense sequence) - rewritten as an
//    explicit frame-stepped state machine, same treatment as every other
//    tinyJoypadShim port here, just with more states than usual to cover
//    each real animated piece individually (`TPLAQ_STATE_DECOUNT_FUEL`,
//    `_DECOUNT_TEETH_UP/DOWN`, `_NEXTLEVEL_WAIT1/2`, `_ADD_TEETH[_FINAL_
//    WAIT]`). `_delay_ms()` values were converted to the nearest whole
//    real-frame count at 60fps (15ms~1, 30ms~2, 200ms~12, 400ms~24,
//    1000ms~60) - these are all cosmetic pacing numbers, not gameplay-
//    critical precision, matching every other port's own `_delay_ms()`
//    conversion here. `RESTORE_TEETH_TPLAQUE()`'s own per-tooth 1ms delays
//    are imperceptible even on real hardware (up to 16 teeth x 1ms = 16ms
//    total) - collapsed into one atomic pass (still one real redraw right
//    after, so the visual result is identical), rather than spending a
//    dedicated state on a delay too short to ever be seen.
//  - **A genuine VRAM-persistence partial-redraw bug, the same class
//    already found and fixed in Pinball/Doc/Bert/Trick/Pipe, found here
//    proactively rather than retrofitted after a report**: upstream's own
//    `Tiny_Flip_TPLAQUE(0)` (normal gameplay redraw) only ever draws pages
//    1-7, and `Tiny_Flip_TPLAQUE(2)` (the score-panel refresh) only ever
//    draws page 0 - each relies on the *other* mode's last real SSD1306
//    write to still be sitting in hardware VRAM for the page it itself
//    skips. This engine's `md_beginFrame()` clears the whole screen every
//    call, so porting either mode verbatim would blank the page the other
//    mode was relying on. Fixed by folding the score/extra-teeth overlay
//    (`recupe_SCORES_TPLAQUE`/`Recupe_ExtraTeeth_TPLAQUE`, upstream only
//    ever OR'd in by modes 1/2/3) directly into the same per-pixel
//    function mode 0 already uses, and having every mode always redraw
//    all 8 pages - mode 2 (`UPDATE_PANNEL`) becomes pixel-identical to
//    mode 0 once the score is folded in, so both now share one dispatch
//    branch.
//  - `switch`, ternary, and intra-function `goto`-as-control-flow were all
//    avoided proactively throughout (matching Tiny Doc/Bike/Pipe/Morpion's
//    established caution) - 15 switch statements and 18 ternary uses
//    upstream, all rewritten as `if`/`else if` chains; every `goto SUITE`/
//    `goto ENDING` early-exit-from-a-loop-body rewritten as `continue`/
//    `break`/early `return`. Binary literals (`0b10101010`/`0b01010101`/
//    `0b00011000`, the checkerboard-dither masks and the weapon sprite's
//    own single-byte bitmap) aren't accepted by this compiler (see Tiny
//    Bike's own finding) - rewritten as decimal (170/85/24) with a
//    `// 0bNNNNNNNN` comment each.
//  - `TSIA_TPLAQUE` (declared upstream, `uint8_t TSIA_TPLAQUE=0;`) is
//    never referenced anywhere else in the file - confirmed dead by grep,
//    dropped rather than ported, same as this project's other confirmed-
//    dead-declaration findings.
//  - No `rand()` calls anywhere in this game - `RND_TPLAQUE`/
//    `RND_POS_TPLAQUE` are both deterministic rotating-index/counter
//    lookups (not real randomness), so the shared `arand()` fix doesn't
//    apply here; ported as direct table/counter rotations, unchanged.
// =============================================================================

int[14] tplaqRnd_Table =
{
-1,0,-1,1,0,1,-1,1,0,0,-1,1,0,1,
};

int[208] tplaqBack1 =
{
128,128,128,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,128,128,128,255,255,255,255,255,255,254,254,240,224,192,128,192,224,192,128,
192,224,224,224,224,192,128,192,224,192,128,192,224,240,240,224,192,128,192,224,
192,128,192,224,240,240,224,192,128,192,224,192,128,192,224,240,240,224,192,128,
192,224,192,128,192,224,240,240,224,192,128,192,224,192,128,192,224,240,240,224,
192,128,192,224,192,128,192,224,240,240,224,192,128,192,224,192,128,192,224,240,
254,254,255,255,255,255,255,255,
};

int[208] tplaqBack2 =
{
192,192,192,192,192,192,192,192,192,192,192,64,192,192,192,64,192,192,192,192,
192,192,64,192,192,192,64,192,192,192,192,192,192,64,192,192,192,64,192,192,
192,192,192,192,64,192,192,192,64,192,192,192,192,192,192,64,192,192,192,64,
192,192,192,192,192,192,64,192,192,192,64,192,192,192,192,192,192,64,192,192,
192,64,192,192,192,192,192,192,64,192,192,192,64,192,192,192,192,192,192,192,
192,192,192,192,127,127,127,63,63,63,31,31,3,1,0,0,0,1,0,0,
0,1,1,1,1,0,0,0,1,0,0,0,1,3,3,1,0,0,0,1,
0,0,0,1,3,3,1,0,0,0,1,0,0,0,1,3,3,1,0,0,
0,1,0,0,0,1,3,3,1,0,0,0,1,0,0,0,1,3,3,1,
0,0,0,1,0,0,0,1,3,3,1,0,0,0,1,0,0,0,1,3,
31,31,63,63,63,127,127,127,
};

int[182] tplaqTubeSprite =
{
9,2,0,232,220,223,220,232,0,0,0,0,5,15,15,15,13,12,8,0,
0,122,191,191,191,123,3,1,0,0,1,3,15,3,1,0,0,0,0,232,
220,223,220,232,0,0,0,0,2,7,7,7,6,6,4,0,0,116,190,190,
190,118,6,2,0,0,1,3,15,3,1,0,0,0,0,208,184,190,184,208,
0,0,0,0,2,7,7,7,6,6,4,0,0,180,222,222,222,182,6,2,
0,0,0,1,7,1,0,0,0,0,0,160,112,124,112,160,0,0,0,0,
2,3,3,3,2,2,0,0,0,84,236,236,236,84,4,0,0,0,0,0,
3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,
};

int[11] tplaqTeethUpSprite =
{
120,254,255,110,124,94,135,254,120,0,0,
};

int[22] tplaqTeethDownSprite =
{
128,192,192,128,128,128,64,192,128,0,0,7,31,63,29,15,30,56,31,7,
0,0,
};

int[3] tplaqBallistic =
{
1,1,24, // 0b00011000
};

int[38] tplaqFood =
{
6,1,48,120,104,50,1,1,24,44,52,44,52,24,10,62,255,237,54,10,
0,252,2,2,4,0,28,58,50,50,58,28,84,198,214,214,198,84,
};

int[42] tplaqPolice =
{
4,1,31,17,31,0,0,31,0,0,29,21,23,0,17,21,31,0,7,4,
31,0,23,21,29,0,31,21,29,0,1,29,3,0,31,21,31,0,23,21,
31,0,
};

int[6] tplaqExtraTeethSprite =
{
0,15,31,14,31,15,
};

int[106] tplaqStartSprite =
{
52,2,254,1,1,145,41,73,145,1,9,249,9,1,241,41,41,241,1,249,
41,41,209,1,9,249,9,1,1,1,1,241,9,73,209,1,241,41,41,241,
1,249,17,33,17,249,1,249,41,9,1,1,1,254,7,8,8,8,9,9,
8,8,8,9,8,8,9,8,8,9,8,9,8,8,9,8,8,9,8,8,
8,8,8,8,9,9,9,8,9,8,8,9,8,9,8,8,8,9,8,9,
9,9,8,8,8,7,
};

// -----------------------------------------------------------------------------
//   Sprite state (flattened from the 3-level Sprite/Moving/Food/Main/Weapon
//   class hierarchy - see this file's own header comment)
// -----------------------------------------------------------------------------

struct TplaqSprite
{
    int x;
    int y;
    int directionX;
    int directionY;
    int active;
    int somX;
    int somY;
    int sx;
    int sy;
    int colapsed;
    int startPos;
};

#define TPLAQ_MAX_FOOD 4
#define TPLAQ_MAX_FOOD_D 8
#define TPLAQ_MAX_LEVEL 16
#define TPLAQ_MAIN_ACCEL_SPEED 32
#define TPLAQ_FOOD_ACCEL_SPEED 60
#define TPLAQ_RENEW_NUMBER 20

TplaqSprite tplaqMain;
TplaqSprite[8] tplaqFoodSpr;
TplaqSprite[8] tplaqTeethUp;
TplaqSprite[8] tplaqTeethDown;
TplaqSprite tplaqWeapon;

struct TplaqGameData
{
    int scores;
    int level;
    int extraTeeth;
    int extraTeethComp;
    int upDown;
    int sp;
    int renew;
    int regenNo;
    int notMove;
    int scanChangeDirection;
    int delayDirectionChange;
    int delayDirectionChangeCounter;
    int endOfGame;
    int renewFood;
    int xMoveActive;
    int foodType;
    int timerTeeth;
    int skipFrame;
    int teethCountUp;
    int teethCountDown;
    int tubeFuel;
    int tubeFuelTimer;
    int tubeFuelTimerRef;
    int tubeRefresh;
};

TplaqGameData tplaqGD;

int tplaqScanTeeth;
int tplaqScanCollision;
int tplaqBlinkStart;
int tplaqTc;
int tplaqTpc;
int tplaqAttaque1;
int tplaqAttaque2;
int tplaqM10000;
int tplaqM1000;
int tplaqM100;
int tplaqM10;
int tplaqM1;
int tplaqRndPosCounter;
int tplaqRdIndex;

// -----------------------------------------------------------------------------
//   Base Sprite accessors
// -----------------------------------------------------------------------------

int tplaqX( TplaqSprite* s ) { return s->x; }
int tplaqY( TplaqSprite* s ) { return s->y; }
int tplaqActive( TplaqSprite* s ) { return s->active; }
void tplaqPutX( TplaqSprite* s, int v ) { s->x = v; }
void tplaqPutY( TplaqSprite* s, int v ) { s->y = v; }
void tplaqPutActive( TplaqSprite* s, int v ) { s->active = v; }
int tplaqDirectionX( TplaqSprite* s ) { return s->directionX; }
int tplaqDirectionY( TplaqSprite* s ) { return s->directionY; }
void tplaqPutDirectionX( TplaqSprite* s, int v ) { s->directionX = v; }
void tplaqPutDirectionY( TplaqSprite* s, int v ) { s->directionY = v; }

int tplaqSomX( TplaqSprite* s ) { return s->somX; }
int tplaqSomY( TplaqSprite* s ) { return s->somY; }
int tplaqSX( TplaqSprite* s ) { return s->sx; }
int tplaqSY( TplaqSprite* s ) { return s->sy; }
void tplaqPutSX( TplaqSprite* s, int v ) { s->sx = v; }
void tplaqPutSY( TplaqSprite* s, int v ) { s->sy = v; }
void tplaqPutSomX( TplaqSprite* s, int v ) { s->somX = v; }
void tplaqPutSomY( TplaqSprite* s, int v ) { s->somY = v; }

// -----------------------------------------------------------------------------
//   Moving_Sprite_TPLAQUE's own constant-accel movement (used by food only -
//   Main_Sprite uses its own separate MHAUT/MDROITE/MBAS/MGAUCHE below)
// -----------------------------------------------------------------------------

void tplaqHaut( TplaqSprite* s )
{
    int tSomY = tplaqSomY( s );
    if( ( tSomY + ( -tplaqGD.sp ) ) <= -TPLAQ_FOOD_ACCEL_SPEED )
    {
        tplaqPutSomY( s, ( tSomY + ( -tplaqGD.sp ) ) + TPLAQ_FOOD_ACCEL_SPEED );
        tplaqPutY( s, tplaqY( s ) - 1 );
    }
    else tplaqPutSomY( s, tSomY + ( -tplaqGD.sp ) );
}

void tplaqDroite( TplaqSprite* s )
{
    int tSomX = tplaqSomX( s );
    if( ( tSomX + tplaqGD.sp ) >= TPLAQ_FOOD_ACCEL_SPEED )
    {
        tplaqPutSomX( s, ( tSomX + tplaqGD.sp ) - TPLAQ_FOOD_ACCEL_SPEED );
        tplaqPutX( s, tplaqX( s ) + 1 );
    }
    else tplaqPutSomX( s, tSomX + tplaqGD.sp );
}

void tplaqBas( TplaqSprite* s )
{
    int tSomY = tplaqSomY( s );
    if( ( tSomY + tplaqGD.sp ) >= TPLAQ_FOOD_ACCEL_SPEED )
    {
        tplaqPutSomY( s, ( tSomY + tplaqGD.sp ) - TPLAQ_FOOD_ACCEL_SPEED );
        tplaqPutY( s, tplaqY( s ) + 1 );
    }
    else tplaqPutSomY( s, tSomY + tplaqGD.sp );
}

void tplaqGauche( TplaqSprite* s )
{
    int tSomX = tplaqSomX( s );
    if( ( tSomX + ( -tplaqGD.sp ) ) <= -TPLAQ_FOOD_ACCEL_SPEED )
    {
        tplaqPutSomX( s, ( tSomX + ( -tplaqGD.sp ) ) + TPLAQ_FOOD_ACCEL_SPEED );
        tplaqPutX( s, tplaqX( s ) - 1 );
    }
    else tplaqPutSomX( s, tSomX + ( -tplaqGD.sp ) );
}

// -----------------------------------------------------------------------------
//   RND helpers (deterministic rotations, not real randomness - see header)
// -----------------------------------------------------------------------------

int tplaqRndPos()
{
    if( tplaqRndPosCounter != 85 ) tplaqRndPosCounter += 11;
    else tplaqRndPosCounter = 19;
    return tplaqRndPosCounter;
}

int tplaqRnd()
{
    if( tplaqRdIndex < 11 ) tplaqRdIndex++;
    else tplaqRdIndex = 0;
    return tplaqRnd_Table[ tplaqRdIndex ];
}

// -----------------------------------------------------------------------------
//   Food_Sprite_TPLAQUE
// -----------------------------------------------------------------------------

int tplaqColapsed( TplaqSprite* s ) { return s->colapsed; }
void tplaqPutColapsed( TplaqSprite* s, int v ) { s->colapsed = v; }
int tplaqStartPos( TplaqSprite* s ) { return s->startPos; }
void tplaqPutStartPos( TplaqSprite* s, int v ) { s->startPos = v; }

void tplaqCopyObj( TplaqSprite* dst, TplaqSprite* src )
{
    tplaqPutColapsed( dst, 1 );
    tplaqPutStartPos( dst, tplaqStartPos( src ) + 11 );
    tplaqPutX( dst, tplaqX( src ) + 11 );
    tplaqPutY( dst, tplaqY( src ) );
    tplaqPutDirectionX( dst, tplaqDirectionX( src ) );
    tplaqPutDirectionY( dst, tplaqDirectionY( src ) );
    tplaqPutSomX( dst, tplaqSomX( src ) );
    tplaqPutSomY( dst, tplaqSomY( src ) );
}

void tplaqAdjustX( TplaqSprite* s )
{
    if( tplaqX( s ) > tplaqStartPos( s ) ) tplaqGauche( s );
    else if( tplaqX( s ) < tplaqStartPos( s ) ) tplaqDroite( s );
    else
    {
        if( tplaqStartPos( s ) != 0 )
        {
            tplaqPutStartPos( s, 0 );
            tplaqPutSomX( s, 0 );
            tplaqPutSomY( s, 0 );
            tplaqPutDirectionX( s, 0 );
            if( tplaqGD.upDown == 0 ) { tplaqPutDirectionY( s, -1 ); tplaqPutSY( s, 0 ); }
            else if( tplaqGD.upDown == 1 ) { tplaqPutDirectionY( s, 1 ); tplaqPutSY( s, 0 ); }
        }
    }
}

void tplaqResetObj( TplaqSprite* s )
{
    tplaqPutStartPos( s, tplaqRndPos() );
    tplaqPutColapsed( s, 0 );
    tplaqPutActive( s, 1 );
    tplaqPutDirectionX( s, 0 );
    tplaqPutDirectionY( s, 0 );
    tplaqPutSomX( s, 0 );
    tplaqPutSomY( s, 0 );
}

void tplaqCreatFood( TplaqSprite* s )
{
    tplaqResetObj( s );
    if( tplaqStartPos( s ) > 55 ) tplaqPutX( s, 116 );
    else tplaqPutX( s, -4 );
    if( tplaqGD.upDown == 0 ) tplaqPutY( s, 46 );
    else if( tplaqGD.upDown == 1 ) tplaqPutY( s, 17 );
}

void tplaqDisableReset( TplaqSprite* s ) { tplaqPutActive( s, 0 ); }

void tplaqMoveUpdate( TplaqSprite* s )
{
    if( tplaqActive( s ) == 1 )
    {
        if( tplaqStartPos( s ) != 0 )
        {
            tplaqAdjustX( s );
            tplaqAdjustX( s );
            tplaqAdjustX( s );
        }
        else
        {
            int dx = tplaqDirectionX( s );
            if( dx == 1 ) { tplaqDroite( s ); if( tplaqX( s ) > 116 ) tplaqPutX( s, 6 ); }
            else if( dx == -1 ) { tplaqGauche( s ); if( tplaqX( s ) < 6 ) tplaqPutX( s, 116 ); }

            int dy = tplaqDirectionY( s );
            if( dy == 1 ) { tplaqBas( s ); if( tplaqY( s ) > 60 ) tplaqPutActive( s, 0 ); }
            else if( dy == -1 ) { tplaqHaut( s ); if( tplaqY( s ) < 1 ) tplaqPutActive( s, 0 ); }
        }
    }
}

// -----------------------------------------------------------------------------
//   Main_Sprite_TPLAQUE (accel/decel movement with clamped play bounds)
// -----------------------------------------------------------------------------

void tplaqMainInit( TplaqSprite* s )
{
    tplaqPutX( s, 60 );
    tplaqPutY( s, 38 );
    tplaqPutActive( s, 1 );
    tplaqPutDirectionY( s, -1 );
    tplaqPutDirectionX( s, 0 );
    tplaqPutSomX( s, 0 );
    tplaqPutSomY( s, 0 );
    tplaqPutSX( s, 0 );
    tplaqPutSY( s, 0 );
}

void tplaqLimitCheck( TplaqSprite* s )
{
    if( tplaqX( s ) < 11 ) { tplaqPutX( s, 11 ); tplaqPutSX( s, 0 ); tplaqPutSomX( s, 0 ); }
    if( tplaqX( s ) > 108 ) { tplaqPutX( s, 108 ); tplaqPutSX( s, 0 ); tplaqPutSomX( s, 0 ); }
    if( tplaqY( s ) < 17 ) { tplaqPutY( s, 17 ); tplaqPutSY( s, 0 ); tplaqPutSomY( s, 0 ); }
    if( tplaqY( s ) > 41 ) { tplaqPutY( s, 41 ); tplaqPutSY( s, 0 ); tplaqPutSomY( s, 0 ); }
    if( tplaqY( s ) < 18 ) tplaqPutDirectionY( s, 1 );
    if( tplaqY( s ) > 40 ) tplaqPutDirectionY( s, -1 );
}

void tplaqDecelY( TplaqSprite* s )
{
    int tSomY = tplaqSomY( s );
    int tSY = tplaqSY( s );
    if( ( tSomY + tSY ) >= 0 )
    {
        if( tSY > 0 ) tplaqPutSY( s, tSY - 1 );
        if( ( tSomY + tSY ) >= TPLAQ_MAIN_ACCEL_SPEED ) { tplaqPutSomY( s, ( tSomY + tSY ) - TPLAQ_MAIN_ACCEL_SPEED ); tplaqPutY( s, tplaqY( s ) + 1 ); }
        else tplaqPutSomY( s, tSomY + tSY );
    }
    else
    {
        if( tSY < 0 ) tplaqPutSY( s, tSY + 1 );
        if( ( tSomY + tSY ) <= -TPLAQ_MAIN_ACCEL_SPEED ) { tplaqPutSomY( s, ( tSomY + tSY ) + TPLAQ_MAIN_ACCEL_SPEED ); tplaqPutY( s, tplaqY( s ) - 1 ); }
        else tplaqPutSomY( s, tSomY + tSY );
    }
    tplaqLimitCheck( s );
}

void tplaqDecelX( TplaqSprite* s )
{
    int tSomX = tplaqSomX( s );
    int tSX = tplaqSX( s );
    if( ( tSomX + tSX ) >= 0 )
    {
        if( tSX > 0 ) tplaqPutSX( s, tSX - 1 );
        if( ( tSomX + tSX ) >= TPLAQ_MAIN_ACCEL_SPEED ) { tplaqPutSomX( s, ( tSomX + tSX ) - TPLAQ_MAIN_ACCEL_SPEED ); tplaqPutX( s, tplaqX( s ) + 1 ); }
        else tplaqPutSomX( s, tSomX + tSX );
    }
    else
    {
        if( tSX < 0 ) tplaqPutSX( s, tSX + 1 );
        if( ( tSomX + tSX ) <= -TPLAQ_MAIN_ACCEL_SPEED ) { tplaqPutSomX( s, ( tSomX + tSX ) + TPLAQ_MAIN_ACCEL_SPEED ); tplaqPutX( s, tplaqX( s ) - 1 ); }
        else tplaqPutSomX( s, tSomX + tSX );
    }
    tplaqLimitCheck( s );
}

void tplaqMHaut( TplaqSprite* s )
{
    int tSomY = tplaqSomY( s );
    int tSY = tplaqSY( s );
    if( tSY > -TPLAQ_MAIN_ACCEL_SPEED ) tplaqPutSY( s, tSY - 1 );
    if( ( tSomY + tSY ) <= 0 )
    {
        if( ( tSomY + tSY ) <= -TPLAQ_MAIN_ACCEL_SPEED ) { tplaqPutSomY( s, ( tSomY + tSY ) + TPLAQ_MAIN_ACCEL_SPEED ); tplaqPutY( s, tplaqY( s ) - 1 ); }
        else tplaqPutSomY( s, tSomY + tSY );
    }
    else tplaqDecelY( s );
    tplaqPutDirectionY( s, -1 );
    tplaqLimitCheck( s );
}

void tplaqMDroite( TplaqSprite* s )
{
    int tSomX = tplaqSomX( s );
    int tSX = tplaqSX( s );
    if( tSX < TPLAQ_MAIN_ACCEL_SPEED ) tplaqPutSX( s, tSX + 1 );
    if( ( tSomX + tSX ) >= 0 )
    {
        if( ( tSomX + tSX ) >= TPLAQ_MAIN_ACCEL_SPEED ) { tplaqPutSomX( s, ( tSomX + tSX ) - TPLAQ_MAIN_ACCEL_SPEED ); tplaqPutX( s, tplaqX( s ) + 1 ); }
        else tplaqPutSomX( s, tSomX + tSX );
    }
    else tplaqDecelX( s );
    tplaqLimitCheck( s );
}

void tplaqMBas( TplaqSprite* s )
{
    int tSomY = tplaqSomY( s );
    int tSY = tplaqSY( s );
    if( tSY < TPLAQ_MAIN_ACCEL_SPEED ) tplaqPutSY( s, tSY + 1 );
    if( ( tSomY + tSY ) >= 0 )
    {
        if( ( tSomY + tSY ) >= TPLAQ_MAIN_ACCEL_SPEED ) { tplaqPutSomY( s, ( tSomY + tSY ) - TPLAQ_MAIN_ACCEL_SPEED ); tplaqPutY( s, tplaqY( s ) + 1 ); }
        else tplaqPutSomY( s, tSomY + tSY );
    }
    else tplaqDecelY( s );
    tplaqPutDirectionY( s, 1 );
    tplaqLimitCheck( s );
}

void tplaqMGauche( TplaqSprite* s )
{
    int tSomX = tplaqSomX( s );
    int tSX = tplaqSX( s );
    if( tSX > -TPLAQ_MAIN_ACCEL_SPEED ) tplaqPutSX( s, tSX - 1 );
    if( ( tSomX + tSX ) <= 0 )
    {
        if( ( tSomX + tSX ) <= -TPLAQ_MAIN_ACCEL_SPEED ) { tplaqPutSomX( s, ( tSomX + tSX ) + TPLAQ_MAIN_ACCEL_SPEED ); tplaqPutX( s, tplaqX( s ) - 1 ); }
        else tplaqPutSomX( s, tSomX + tSX );
    }
    else tplaqDecelX( s );
    tplaqLimitCheck( s );
}

// -----------------------------------------------------------------------------
//   Weapon_Sprite_TPLAQUE
// -----------------------------------------------------------------------------

void tplaqWeaponStart( TplaqSprite* w, TplaqSprite* mainS )
{
    if( tplaqActive( w ) == 0 )
    {
        Sound( 200, 4 );
        tplaqPutActive( w, 1 );
        tplaqPutY( w, tplaqY( mainS ) + 2 );
        tplaqPutDirectionY( w, tplaqDirectionY( mainS ) );
    }
}

int tplaqWeaponCollisionDetect( TplaqSprite* w )
{
    if( tplaqY( w ) < 9 || tplaqY( w ) > 56 ) return 1;
    return 0;
}

void tplaqWeaponUpdate( TplaqSprite* w )
{
    if( tplaqActive( w ) > 0 )
    {
        int dy = tplaqDirectionY( w );
        if( dy == -1 ) tplaqPutY( w, tplaqY( w ) - 2 );
        else if( dy == 1 ) tplaqPutY( w, tplaqY( w ) + 2 );
    }
    if( tplaqWeaponCollisionDetect( w ) ) tplaqPutActive( w, 0 );
}

// -----------------------------------------------------------------------------
//   Helpers
// -----------------------------------------------------------------------------

int tplaqMymap( int x, int inMin, int inMax, int outMin, int outMax )
{
    return ( x - inMin ) * ( outMax - outMin ) / ( inMax - inMin ) + outMin;
}

// -----------------------------------------------------------------------------
//   Render / blit primitives
// -----------------------------------------------------------------------------

// Defensively negative-yPos-safe, matching Tiny Pipe/Morpion's own
// RecupeLineY fix, even though no call site here is ever actually fed a
// negative yPos in practice (Main/weapon/food Y are all kept non-negative
// by their own clamp/deactivate logic) - costs nothing extra to be safe.
int tplaqRecupeLineY( int val )
{
    if( val >= 0 ) return val >> 3;
    return -( ( -val + 7 ) >> 3 );
}

int tplaqRecupeDecalageY( int val )
{
    return val - ( tplaqRecupeLineY( val ) * 8 );
}

int tplaqSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown ) return input << decalage;
    return input >> ( 8 - decalage );
}

int tplaqBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = tplaqRecupeLineY( yPos );

    if( xPass > ( xPos + wSprite - 1 ) || xPass < xPos ||
        recupeLineY > yPass || ( recupeLineY + hSprite ) < yPass )
      return 0x00;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = tplaqRecupeDecalageY( yPos );
    int scanA = ( xPass - xPos ) + ( spriteYLine * wSprite ) + 2;
    int scanB = ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;

    int outByte;
    if( scanA > wMax ) outByte = 0x00;
    else outByte = tplaqSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = tplaqSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

int tplaqSpeedBlitz( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[0];
    int hSprite = sprites[1];
    if( xPass > ( xPos + wSprite - 1 ) || xPass < xPos ||
        yPass < yPos || yPass > ( yPos + hSprite - 1 ) )
      return 0x00;
    return sprites[ 2 + ( xPass - xPos ) + ( yPass - yPos ) * wSprite + frame * ( hSprite * wSprite ) ];
}

int tplaqBack( int xPass, int yPass )
{
    if( xPass > 115 ) return 0;
    if( xPass < 12 ) return 0;
    if( yPass < 2 ) return tplaqBack2[ ( xPass - 12 ) + ( 104 * yPass ) ];
    if( yPass > 5 ) return tplaqBack1[ ( xPass - 12 ) + ( 104 * ( yPass - 6 ) ) ];
    return 0;
}

int tplaqTrace( int xPass )
{
    if( xPass < 12 ) return 0;
    if( xPass > 115 ) return 0;
    return tplaqBack2[ xPass - 12 ];
}

// Advances the shared TC/TPC per-column teeth-scan counters and the
// per-column ATTAQUE2 dither toggle - matches upstream's own row-scoped
// state exactly (reset once per page row in tplaqTinyFlip below).
int tplaqTeeth( int xPass )
{
    int gris = 0xff;
    if( xPass < 21 ) return 0;
    if( xPass > 106 ) return 0;
    int tByte = tplaqTpc;
    if( tplaqTpc < 10 ) tplaqTpc++;
    else { tplaqTpc = 0; tplaqTc++; }
    if( tplaqActive( &tplaqTeethUp[ tplaqTc ] ) == 1 )
      return tplaqTeethUpSprite[ tByte ];
    else if( tplaqActive( &tplaqTeethUp[ tplaqTc ] ) > 1 )
    {
        if( tplaqAttaque1 )
        {
            if( tplaqAttaque2 ) gris = 170; // 0b10101010
            else gris = 85; // 0b01010101
        }
        return gris & tplaqTeethUpSprite[ tByte ];
    }
    return 0;
}

int tplaqTeethDownAt( int xPass, int mult )
{
    int gris = 0xff;
    if( xPass < 21 ) return 0;
    if( xPass > 106 ) return 0;
    int tByte = tplaqTpc;
    if( tplaqTpc < 10 ) tplaqTpc++;
    else { tplaqTpc = 0; tplaqTc++; }
    if( tplaqActive( &tplaqTeethDown[ tplaqTc ] ) == 1 )
      return tplaqTeethDownSprite[ tByte + mult ];
    else if( tplaqActive( &tplaqTeethDown[ tplaqTc ] ) > 1 )
    {
        if( tplaqAttaque1 )
        {
            if( tplaqAttaque2 ) gris = 170; // 0b10101010
            else gris = 85; // 0b01010101
        }
        return gris & tplaqTeethDownSprite[ tByte + mult ];
    }
    return 0;
}

// Row/column footprint precomputed once per page row in tplaqTinyFlip
// (mirrors this function's own bounds check exactly) - lets the call site
// skip the call entirely outside that footprint instead of paying a full
// call for a result that's provably 0, the same "self-gated call still
// costs a full call" lesson as the score/extra-teeth gating above.
int tplaqTubeRowOk;
int tplaqTubeXStart;
int tplaqTubeXEnd;

int tplaqTube( int xPass, int yPass )
{
    if( tplaqX( &tplaqMain ) > xPass ) return 0;
    if( tplaqY( &tplaqMain ) - 7 > ( yPass * 8 ) ) return 0;
    if( ( tplaqX( &tplaqMain ) + 9 ) < xPass ) return 0;
    if( ( tplaqY( &tplaqMain ) + 16 ) < ( yPass * 8 ) ) return 0;
    int frame;
    if( tplaqDirectionY( &tplaqMain ) == -1 ) frame = 0; else frame = 1;
    frame = frame + ( tplaqGD.tubeFuel * 2 );
    return tplaqBlitzSprite( tplaqX( &tplaqMain ), tplaqY( &tplaqMain ), xPass, yPass, frame, tplaqTubeSprite );
}

// Composited once per page row (see tplaqTinyFlip) instead of re-scanning
// all 8 food sprites at every one of ~1024 pixels/frame - the same
// O(pixels x objects) shape already fixed in Bomber/Pacman/Doc/Bert/Pipe.
// The row-membership/x-footprint gates here are copied verbatim from the
// original per-pixel version, so this changes call count, not output.
int[128] tplaqFoodPageBuffer;

void tplaqCompositeFoodRow( int y )
{
    int x;
    for( x = 0; x < 128; x++ ) tplaqFoodPageBuffer[x] = 0;
    if( y == 0 || y == 7 ) return;
    int t;
    for( t = 0; t < TPLAQ_MAX_FOOD_D; t++ )
    {
        if( tplaqActive( &tplaqFoodSpr[t] ) == 0 ) continue;
        int fx = tplaqX( &tplaqFoodSpr[t] );
        int fy = tplaqY( &tplaqFoodSpr[t] );
        if( fy - 7 > ( y * 8 ) ) continue;
        if( ( fy + 8 ) < ( y * 8 ) ) continue;
        int xStart = fx;
        if( xStart < 0 ) xStart = 0;
        int xEnd = fx + 5; // sprite width 6 (tplaqFood[0]==6)
        if( xEnd > 127 ) xEnd = 127;
        int col;
        for( col = xStart; col <= xEnd; col++ )
          tplaqFoodPageBuffer[col] = tplaqFoodPageBuffer[col] | tplaqBlitzSprite( fx, fy, col, y, tplaqGD.foodType, tplaqFood );
    }
}

int tplaqRecupeScores( int xPass, int yPass )
{
    if( xPass < 12 ) return 0;
    if( xPass > 34 ) return 0;
    if( yPass > 0 ) return 0;
    return tplaqSpeedBlitz( 12, 0, xPass, yPass, tplaqM10000, tplaqPolice ) |
           tplaqSpeedBlitz( 16, 0, xPass, yPass, tplaqM1000, tplaqPolice ) |
           tplaqSpeedBlitz( 20, 0, xPass, yPass, tplaqM100, tplaqPolice ) |
           tplaqSpeedBlitz( 24, 0, xPass, yPass, tplaqM10, tplaqPolice ) |
           tplaqSpeedBlitz( 28, 0, xPass, yPass, tplaqM1, tplaqPolice ) |
           tplaqSpeedBlitz( 32, 0, xPass, yPass, 0, tplaqPolice );
}

int tplaqRecupeExtraTeeth( int xPass, int yPass )
{
    if( yPass > 0 ) return 0;
    if( xPass > 116 ) return 0;
    if( xPass < ( 117 - ( tplaqGD.extraTeeth * 6 ) ) ) return 0;
    if( tplaqScanTeeth < 5 ) tplaqScanTeeth++; else tplaqScanTeeth = 0;
    return tplaqExtraTeethSprite[ tplaqScanTeeth ];
}

int tplaqIntro( int xPass, int yPass )
{
    if( tplaqBlinkStart > 11 ) return tplaqBlitzSprite( 38, 26, xPass, yPass, 0, tplaqStartSprite );
    return 0;
}

// Real gameplay scene: background + teeth + tube + food + weapon. Used for
// both normal play (mode 0) and the score-panel refresh (mode 2) - see
// this file's own header comment on why those two collapsed into one.
int tplaqRecupe( int xPass, int yPass )
{
    if( xPass > 115 ) return 0;
    if( xPass < 12 ) return 0;
    int recupBack = 0;
    if( yPass == 0 ) recupBack = tplaqBack( xPass, yPass );
    else if( yPass == 1 ) recupBack = tplaqBack( xPass, yPass ) | tplaqTeeth( xPass );
    else if( yPass == 6 ) recupBack = tplaqBack( xPass, yPass ) | tplaqTeethDownAt( xPass, 0 );
    else if( yPass == 7 ) recupBack = tplaqBack( xPass, yPass ) | tplaqTeethDownAt( xPass, 11 );

    int recupTube = 0;
    if( tplaqTubeRowOk && xPass >= tplaqTubeXStart && xPass <= tplaqTubeXEnd )
      recupTube = tplaqTube( xPass, yPass );
    recupTube = recupTube | tplaqFoodPageBuffer[ xPass ];
    int recupWeapon = 0;
    if( tplaqActive( &tplaqWeapon ) == 1 && xPass == tplaqX( &tplaqMain ) + 3 )
      recupWeapon = tplaqBlitzSprite( xPass, tplaqY( &tplaqWeapon ), xPass, yPass, 0, tplaqBallistic );

    return recupBack | recupTube | recupWeapon;
}

// Same as tplaqRecupe but without the weapon layer - matches upstream's
// own Recupe_DCOUNT_TPLAQUE, used while the weapon is guaranteed inactive
// (every DECOUNT_TPLAQUE-derived state here deactivates it on entry).
int tplaqRecupeDcount( int xPass, int yPass )
{
    if( xPass > 115 ) return 0;
    if( xPass < 12 ) return 0;
    int recupBack = 0;
    if( yPass == 0 ) recupBack = tplaqBack( xPass, yPass );
    else if( yPass == 1 ) recupBack = tplaqBack( xPass, yPass ) | tplaqTeeth( xPass );
    else if( yPass == 6 ) recupBack = tplaqBack( xPass, yPass ) | tplaqTeethDownAt( xPass, 0 );
    else if( yPass == 7 ) recupBack = tplaqBack( xPass, yPass ) | tplaqTeethDownAt( xPass, 11 );
    int recupTube = 0;
    if( tplaqTubeRowOk && xPass >= tplaqTubeXStart && xPass <= tplaqTubeXEnd )
      recupTube = tplaqTube( xPass, yPass );
    recupTube = recupTube | tplaqFoodPageBuffer[ xPass ];
    return recupBack | recupTube;
}

// T_FLIP: 0/2 = real gameplay scene incl. weapon (folded together, see
// header); 1 = decount scene (no weapon); 3 = attract/title screen.
// Always redraws all 8 pages regardless of mode - see this file's own
// header comment on the VRAM-persistence bug this avoids.
void tplaqTinyFlip( int tFlip )
{
    tplaqScanTeeth = 0;
    md_beginFrame();
    int y, x;
    for( y = 0; y < 8; y++ )
    {
        tplaqAttaque2 = 0;
        tplaqTpc = 0;
        tplaqTc = 0;
        if( tFlip == 0 || tFlip == 1 || tFlip == 2 ) tplaqCompositeFoodRow( y );

        // recupe_SCORES/Recupe_ExtraTeeth both self-gate to yPass==0 plus
        // their own narrow x range - gating the call site to the exact
        // same range (extraStart mirrors the callee's own formula) avoids
        // paying for ~1000 calls/frame that are guaranteed to return 0,
        // the same "self-gated call still costs a full call" lesson this
        // project's other ports already learned (see Arkanoid/Bert/Tris/
        // Trick/Morpion's own history).
        int scoresRow = ( y == 0 );
        int extraStart = 117 - ( tplaqGD.extraTeeth * 6 );
        if( extraStart < 0 ) extraStart = 0;

        // tplaqTube()'s own row/column footprint, precomputed once per row
        // (see this function's own struct-level comment above tplaqTube).
        tplaqTubeRowOk = 0;
        tplaqTubeXStart = 0;
        tplaqTubeXEnd = -1;
        if( tFlip == 0 || tFlip == 1 || tFlip == 2 )
        {
            int mainY = tplaqY( &tplaqMain );
            if( mainY - 7 <= y * 8 && y * 8 <= mainY + 16 )
            {
                tplaqTubeRowOk = 1;
                tplaqTubeXStart = tplaqX( &tplaqMain );
                tplaqTubeXEnd = tplaqTubeXStart + 9;
            }
        }

        for( x = 0; x < 128; x++ )
        {
            tplaqAttaque2 = !tplaqAttaque2;
            int pixel = 0;
            // Every render layer here is confined to columns 12-116 (Back/
            // Recupe/Tube stop at 115, RecupeExtraTeeth's own range can
            // reach 116) - skipping the call entirely outside that shared
            // range avoids ~200 wasted calls/frame that are provably 0.
            if( x < 12 || x > 116 )
            {
                md_drawColumn( x, y, 0 );
                continue;
            }
            if( tFlip == 0 || tFlip == 2 )
            {
                pixel = tplaqRecupe( x, y );
                if( scoresRow )
                {
                    if( x <= 34 ) pixel = pixel | tplaqRecupeScores( x, y );
                    if( x >= extraStart ) pixel = pixel | tplaqRecupeExtraTeeth( x, y );
                }
            }
            else if( tFlip == 1 )
            {
                pixel = tplaqRecupeDcount( x, y );
                if( scoresRow )
                {
                    if( x <= 34 ) pixel = pixel | tplaqRecupeScores( x, y );
                    if( x >= extraStart ) pixel = pixel | tplaqRecupeExtraTeeth( x, y );
                }
            }
            else if( tFlip == 3 )
            {
                pixel = tplaqIntro( x, y ) | tplaqBack( x, y );
                if( scoresRow && x >= 12 && x <= 34 ) pixel = pixel | tplaqRecupeScores( x, y );
            }
            md_drawColumn( x, y, pixel );
        }
    }
}

// -----------------------------------------------------------------------------
//   Sound helpers
// -----------------------------------------------------------------------------

void tplaqDSound( int tAdd )
{
    Sound( 5 + tAdd, 20 );
    Sound( 100, 10 );
}

void tplaqDSound2()
{
    Sound( 100, 10 );
    Sound( 50, 30 );
}

void tplaqSoundAddTeeth()
{
    Sound( 100, 20 );
    Sound( 1, 20 );
    Sound( 100, 20 );
}

// The game-over alarm (`for(t=1;t<20;t++){Sound(4,80);Sound(100,80);}`,
// 38 calls total) - each note has a genuine ~80ms felt duration, so this
// one just needs the standard frame-stepped treatment, no downsampling
// (unlike Tiny Pipe/Morpion's own much larger computed sweeps).
int tplaqAlarmActive;
int tplaqAlarmT;
int tplaqAlarmSub;

void tplaqStartAlarm()
{
    tplaqAlarmActive = 1;
    tplaqAlarmT = 1;
    tplaqAlarmSub = 0;
}

int tplaqAdvanceAlarm()
{
    if( !tplaqAlarmActive ) return 1;
    if( tplaqAlarmT >= 20 ) { tplaqAlarmActive = 0; return 1; }
    if( tplaqAlarmSub == 0 ) { Sound( 4, 80 ); tplaqAlarmSub = 1; }
    else { Sound( 100, 80 ); tplaqAlarmSub = 0; tplaqAlarmT++; }
    return 0;
}

// -----------------------------------------------------------------------------
//   Score / panel
// -----------------------------------------------------------------------------

void tplaqScoreAdd( int sc )
{
    tplaqGD.extraTeethComp = tplaqGD.extraTeethComp + sc;
    if( tplaqGD.extraTeethComp > 99 )
    {
        tplaqGD.extraTeethComp = tplaqGD.extraTeethComp - 100;
        tplaqGD.extraTeeth++;
    }
    tplaqGD.scores += sc;
}

void tplaqCompilSco()
{
    tplaqM10000 = tplaqGD.scores / 10000;
    tplaqM1000 = ( tplaqGD.scores - ( tplaqM10000 * 10000 ) ) / 1000;
    tplaqM100 = ( tplaqGD.scores - ( tplaqM1000 * 1000 ) - ( tplaqM10000 * 10000 ) ) / 100;
    tplaqM10 = ( tplaqGD.scores - ( tplaqM100 * 100 ) - ( tplaqM1000 * 1000 ) - ( tplaqM10000 * 10000 ) ) / 10;
    tplaqM1 = tplaqGD.scores - ( tplaqM10 * 10 ) - ( tplaqM100 * 100 ) - ( tplaqM1000 * 1000 ) - ( tplaqM10000 * 10000 );
}

void tplaqUpdatePannel()
{
    tplaqCompilSco();
    tplaqTinyFlip( 2 );
}

// Recomputes the score digits without a redraw - for call sites where a
// full tplaqTinyFlip() of the exact same scene is already guaranteed to
// happen later in the same real engine tick (mode 2 and mode 0 are pixel-
// identical since the score/extra-teeth overlay was folded into mode 0 -
// see this file's own header comment), so calling tplaqUpdatePannel()'s
// own tplaqTinyFlip(2) there would just render the same frame twice.
void tplaqRefreshScoreOnly()
{
    tplaqCompilSco();
}

void tplaqAdjustTube()
{
    if( tplaqGD.tubeRefresh == 0 )
      tplaqGD.tubeFuel = tplaqMymap( tplaqGD.tubeFuelTimer, tplaqGD.tubeFuelTimerRef, 0, 0, 4 );
}

void tplaqAdjustTubeTimer()
{
    if( tplaqGD.tubeFuelTimer > 0 ) tplaqGD.tubeFuelTimer--;
    tplaqAdjustTube();
}

// -----------------------------------------------------------------------------
//   Food / teeth mutation + collision
// -----------------------------------------------------------------------------

void tplaqDeleteTeeth()
{
    int t;
    for( t = 0; t < 8; t++ )
    {
        if( tplaqActive( &tplaqTeethUp[t] ) == 2 ) tplaqPutActive( &tplaqTeethUp[t], 0 );
        if( tplaqActive( &tplaqTeethDown[t] ) == 2 ) tplaqPutActive( &tplaqTeethDown[t], 0 );
    }
}

void tplaqInvertFoodDirection()
{
    int tv = -1;
    if( tplaqGD.upDown == 1 ) tplaqGD.upDown = 0; else tplaqGD.upDown = 1;
    if( tplaqGD.upDown == 0 ) tv = -1;
    else if( tplaqGD.upDown == 1 ) tv = 1;
    int t;
    for( t = 0; t < 8; t++ )
    {
        if( tplaqActive( &tplaqFoodSpr[t] ) == 1 )
        {
            tplaqPutDirectionY( &tplaqFoodSpr[t], tv );
            tplaqPutSomY( &tplaqFoodSpr[t], 0 );
            tplaqPutSY( &tplaqFoodSpr[t], 0 );
        }
    }
}

void tplaqTeethReset()
{
    int t;
    for( t = 0; t < 8; t++ )
    {
        tplaqPutActive( &tplaqTeethUp[t], 0 );
        tplaqPutX( &tplaqTeethUp[t], ( t * 11 ) + 21 );
        tplaqPutY( &tplaqTeethUp[t], 8 );
        tplaqPutActive( &tplaqTeethDown[t], 0 );
        tplaqPutX( &tplaqTeethDown[t], ( t * 11 ) + 21 );
        tplaqPutY( &tplaqTeethDown[t], 54 );
    }
    for( t = 2; t < 6; t++ )
    {
        tplaqPutActive( &tplaqTeethUp[t], -1 );
        tplaqPutActive( &tplaqTeethDown[t], -1 );
    }
}

void tplaqFoodReset()
{
    int t;
    for( t = 0; t < TPLAQ_MAX_FOOD_D; t++ ) tplaqDisableReset( &tplaqFoodSpr[t] );
}

void tplaqColapsFood( int idx )
{
    tplaqCopyObj( &tplaqFoodSpr[ idx + 1 ], &tplaqFoodSpr[ idx ] );
}

void tplaqUpdateFoodTriger()
{
    if( tplaqGD.renew > 0 ) tplaqGD.renew--;
    else tplaqGD.renew = tplaqGD.renewFood;
}

void tplaqUpdateChangeX()
{
    if( tplaqGD.scanChangeDirection < ( TPLAQ_MAX_FOOD - 1 ) ) tplaqGD.scanChangeDirection++;
    else tplaqGD.scanChangeDirection = 0;
    int tv = tplaqRnd();
    if( tplaqActive( &tplaqFoodSpr[ tplaqGD.scanChangeDirection * 2 ] ) == 1 )
    {
        tplaqPutDirectionX( &tplaqFoodSpr[ tplaqGD.scanChangeDirection * 2 ], tv );
        tplaqPutSomX( &tplaqFoodSpr[ tplaqGD.scanChangeDirection * 2 ], 0 );
    }
    if( tplaqActive( &tplaqFoodSpr[ ( tplaqGD.scanChangeDirection * 2 ) + 1 ] ) == 1 )
    {
        tplaqPutDirectionX( &tplaqFoodSpr[ ( tplaqGD.scanChangeDirection * 2 ) + 1 ], tv );
        tplaqPutSomX( &tplaqFoodSpr[ ( tplaqGD.scanChangeDirection * 2 ) + 1 ], 0 );
    }
}

void tplaqAddFood()
{
    if( tplaqGD.renew == 0 && tplaqGD.regenNo != 0 )
    {
        int t;
        for( t = 0; t < TPLAQ_MAX_FOOD; t++ )
        {
            if( tplaqActive( &tplaqFoodSpr[ t * 2 ] ) == 0 && tplaqActive( &tplaqFoodSpr[ ( t * 2 ) + 1 ] ) == 0 )
            {
                if( tplaqGD.regenNo > 0 ) tplaqGD.regenNo--;
                tplaqCreatFood( &tplaqFoodSpr[ t * 2 ] );
                tplaqCreatFood( &tplaqFoodSpr[ ( t * 2 ) + 1 ] );
                tplaqColapsFood( t * 2 );
                tplaqGD.renew = tplaqGD.renewFood;
                return;
            }
        }
    }
}

void tplaqFoodMoveUpdate()
{
    if( tplaqGD.notMove == 0 )
    {
        int t;
        for( t = 0; t < TPLAQ_MAX_FOOD_D; t++ )
        {
            if( tplaqY( &tplaqFoodSpr[t] ) < 18 ) tplaqPutDirectionX( &tplaqFoodSpr[t], 0 );
            if( tplaqY( &tplaqFoodSpr[t] ) > 45 ) tplaqPutDirectionX( &tplaqFoodSpr[t], 0 );
            tplaqMoveUpdate( &tplaqFoodSpr[t] );
        }
        tplaqUpdateFoodTriger();
    }
    tplaqAddFood();
}

void tplaqCheckCollisionWTeeth( int fx, int fw, int fy, int fh, TplaqSprite* teeth )
{
    if( tplaqActive( teeth ) == 1 )
    {
        int teethX = tplaqX( teeth );
        int teethW = teethX + 8;
        int teethY = tplaqY( teeth );
        int teethH = teethY + 7;
        fx = fx + 1;
        fw = fx + 2;
        if( fx > teethW ) return;
        if( fw < teethX ) return;
        if( fy > teethH ) return;
        if( fh < teethY ) return;
        if( tplaqGD.notMove == 0 ) tplaqGD.notMove = 1;
        tplaqPutActive( teeth, 2 );
    }
}

void tplaqCheckCollisionWBallistic( int idx )
{
    if( tplaqActive( &tplaqFoodSpr[idx] ) )
    {
        int foodX = tplaqX( &tplaqFoodSpr[idx] );
        int foodW = foodX + 5;
        int foodY = tplaqY( &tplaqFoodSpr[idx] ) + 1;
        int foodH = foodY + 5;
        int ballisticX = tplaqX( &tplaqMain ) + 3;
        int ballisticY = tplaqY( &tplaqWeapon );
        int ballisticH = ballisticY + 7;

        if( ballisticX <= foodW && ballisticX >= foodX && ballisticY <= foodH && ballisticH >= foodY )
        {
            if( tplaqActive( &tplaqWeapon ) == 1 )
            {
                tplaqPutActive( &tplaqFoodSpr[idx], 0 );
                tplaqScoreAdd( 1 );
                // Not tplaqUpdatePannel(): this runs inside tplaqHitBox(),
                // called from tplaqUpdatePlaying() before that function's
                // own unconditional trailing tplaqTinyFlip(0) - a second
                // full render here would just redraw the identical scene
                // twice in the same tick (found via a direct user report
                // of a 100% CPU spike on every hit, confirmed by tracing
                // the call stack rather than testing).
                tplaqRefreshScoreOnly();
                tplaqPutActive( &tplaqWeapon, 2 );
            }
            return;
        }

        if( tplaqGD.upDown == 0 )
          tplaqCheckCollisionWTeeth( foodX, foodW, foodY, foodH, &tplaqTeethUp[ tplaqScanCollision ] );
        else if( tplaqGD.upDown == 1 )
          tplaqCheckCollisionWTeeth( foodX, foodW, foodY, foodH, &tplaqTeethDown[ tplaqScanCollision ] );
    }
}

// Returns 1 if any active food sprite is still overlapping this (already
// plaque-stuck) tooth - see this file's own header comment: upstream took
// this "still stuck?" flag as a by-reference out-parameter accumulated
// across 8 calls; its own return value was always a dead constant 0, so
// this port uses the return value directly instead (caller ORs it in).
int tplaqCollisionWTeethAgain( TplaqSprite* teeth )
{
    if( tplaqGD.notMove > 0 )
    {
        if( tplaqActive( teeth ) == 2 )
        {
            int teethX = tplaqX( teeth );
            int teethW = teethX + 8;
            int teethY = tplaqY( teeth );
            int teethH = teethY + 7;
            int found = 0;
            int t;
            for( t = 0; t < TPLAQ_MAX_FOOD_D; t++ )
            {
                if( tplaqActive( &tplaqFoodSpr[t] ) == 1 )
                {
                    int foodX = tplaqX( &tplaqFoodSpr[t] ) + 1;
                    int foodW = foodX + 2;
                    int foodY = tplaqY( &tplaqFoodSpr[t] ) + 1;
                    int foodH = foodY + 5;
                    if( foodX <= teethW && foodW >= teethX && foodY <= teethH && foodH >= teethY )
                    {
                        found = 1;
                        break;
                    }
                }
            }
            if( found ) return 1;
            if( tplaqActive( teeth ) == 2 ) tplaqPutActive( teeth, 1 );
        }
    }
    return 0;
}

void tplaqHitBox()
{
    tplaqWeaponUpdate( &tplaqWeapon );
    if( tplaqScanCollision < 7 ) tplaqScanCollision++; else tplaqScanCollision = 0;
    int t;
    for( t = 0; t < 8; t++ ) tplaqCheckCollisionWBallistic( t );
}

void tplaqCheckNumberOfTeeth()
{
    tplaqGD.teethCountUp = tplaqGD.teethCountUp + tplaqActive( &tplaqTeethUp[ tplaqScanCollision ] );
    tplaqGD.teethCountDown = tplaqGD.teethCountDown + tplaqActive( &tplaqTeethDown[ tplaqScanCollision ] );
    if( tplaqScanCollision == 7 )
    {
        if( tplaqGD.teethCountUp != 0 && tplaqGD.teethCountDown == 0 )
        {
            if( tplaqGD.upDown == 1 ) tplaqInvertFoodDirection();
        }
        if( tplaqGD.teethCountUp == 0 && tplaqGD.teethCountDown != 0 )
        {
            if( tplaqGD.upDown == 0 ) tplaqInvertFoodDirection();
        }
        tplaqGD.teethCountUp = 0;
        tplaqGD.teethCountDown = 0;
    }
}

// One tick's worth of GAME_PLAY_TPLAQUE, minus the END_OF_LEVEL check
// (moved to the caller - see gameTinyPlaque_update's own PLAYING branch,
// since that decision now spans several real frames of its own states
// rather than a single synchronous call).
void tplaqGamePlayTick()
{
    tplaqCheckNumberOfTeeth();
    if( tplaqGD.notMove > 0 )
    {
        if( tplaqGD.notMove >= tplaqGD.timerTeeth )
        {
            tplaqDeleteTeeth();
            tplaqGD.notMove = 0;
            tplaqInvertFoodDirection();
        }
        else
        {
            int q = 0;
            int t;
            if( tplaqGD.upDown == 0 )
            {
                for( t = 0; t < 8; t++ ) if( tplaqCollisionWTeethAgain( &tplaqTeethUp[t] ) ) q = 1;
            }
            else if( tplaqGD.upDown == 1 )
            {
                for( t = 0; t < 8; t++ ) if( tplaqCollisionWTeethAgain( &tplaqTeethDown[t] ) ) q = 1;
            }
            tplaqGD.notMove++;
            if( q == 0 ) { tplaqGD.notMove = 0; tplaqInvertFoodDirection(); }
        }
    }
    if( tplaqGD.delayDirectionChangeCounter < 1 )
    {
        if( tplaqGD.xMoveActive == 1 ) tplaqUpdateChangeX();
        tplaqGD.delayDirectionChangeCounter = tplaqGD.delayDirectionChange;
    }
    else tplaqGD.delayDirectionChangeCounter--;
}

// -----------------------------------------------------------------------------
//   Level load / restore / dispense
// -----------------------------------------------------------------------------

void tplaqLoadLevel()
{
    tplaqGD.teethCountUp = 0;
    tplaqGD.teethCountDown = 0;
    tplaqGD.regenNo = TPLAQ_RENEW_NUMBER;
    tplaqGD.upDown = 1;
    tplaqGD.scanChangeDirection = 0;
    tplaqGD.delayDirectionChange = tplaqMymap( tplaqGD.level, 0, TPLAQ_MAX_LEVEL, 25, 4 );
    tplaqGD.sp = tplaqMymap( tplaqGD.level, 0, TPLAQ_MAX_LEVEL, 5, 15 );
    if( tplaqGD.level > 2 ) tplaqGD.xMoveActive = 1; else tplaqGD.xMoveActive = 0;
    tplaqGD.renewFood = tplaqMymap( tplaqGD.level, 0, TPLAQ_MAX_LEVEL, 180, 32 );
    tplaqGD.timerTeeth = tplaqMymap( tplaqGD.level, 0, TPLAQ_MAX_LEVEL, 200, 64 );
    tplaqGD.renew = tplaqGD.renewFood;
    tplaqGD.notMove = 0;
    tplaqGD.delayDirectionChangeCounter = tplaqGD.delayDirectionChange;
    tplaqPutDirectionY( &tplaqMain, 1 );
    tplaqMainInit( &tplaqMain );
    tplaqGD.tubeFuelTimerRef = tplaqMymap( tplaqGD.level, 0, TPLAQ_MAX_LEVEL, 1200, 200 );
    tplaqGD.tubeFuelTimer = tplaqGD.tubeFuelTimerRef;
    tplaqGD.tubeFuel = tplaqMymap( tplaqGD.tubeFuelTimer, tplaqGD.tubeFuelTimerRef, 0, 0, 4 );
    tplaqGD.tubeRefresh = 8;
    tplaqPutActive( &tplaqWeapon, 0 );
}

void tplaqNextLevel()
{
    if( tplaqGD.level < TPLAQ_MAX_LEVEL ) tplaqGD.level++; else tplaqGD.level = TPLAQ_MAX_LEVEL;
    if( tplaqGD.foodType < 5 ) tplaqGD.foodType++; else tplaqGD.foodType = 0;
    tplaqLoadLevel();
}

// Every restored tooth's own upstream `_delay_ms(1)` is imperceptible
// (up to 16 teeth x 1ms = 16ms total) - collapsed to one atomic pass,
// see this file's own header comment.
void tplaqRestoreTeeth()
{
    int t;
    int tAdd = 0;
    for( t = 0; t < 8; t++ )
    {
        if( tplaqActive( &tplaqTeethUp[t] ) == -1 )
        {
            tplaqPutActive( &tplaqTeethUp[t], 1 );
            tAdd += 10;
            tplaqDSound( tAdd );
        }
    }
    for( t = 0; t < 8; t++ )
    {
        if( tplaqActive( &tplaqTeethDown[t] ) == -1 )
        {
            tplaqPutActive( &tplaqTeethDown[t], 1 );
            tAdd += 10;
            tplaqDSound( tAdd );
        }
    }
}

void tplaqInitNewGame()
{
    tplaqGD.extraTeeth = 0;
    tplaqGD.extraTeethComp = 0;
    tplaqGD.scores = 0;
    tplaqGD.level = 0;
    tplaqGD.endOfGame = 0;
    tplaqGD.foodType = 0;
    tplaqGD.skipFrame = 0;
    tplaqTeethReset();
    tplaqFoodReset();
}

// ADD_TEETH_TPLAQUE's own candidate visit order: for each of 4 groups,
// TEETH_UP[g+4], TEETH_DOWN[g+4], TEETH_UP[3-g], TEETH_DOWN[3-g].
TplaqSprite* tplaqAddTeethCandidate( int idx )
{
    int group = idx / 4;
    int sub = idx % 4;
    if( sub == 0 ) return &tplaqTeethUp[ group + 4 ];
    if( sub == 1 ) return &tplaqTeethDown[ group + 4 ];
    if( sub == 2 ) return &tplaqTeethUp[ 3 - group ];
    return &tplaqTeethDown[ 3 - group ];
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define TPLAQ_STATE_ATTRACT                  0
#define TPLAQ_STATE_START_WAIT1              1
#define TPLAQ_STATE_START_WAIT2              2
#define TPLAQ_STATE_PLAYING                  3
#define TPLAQ_STATE_GAMEOVER_ALARM           4
#define TPLAQ_STATE_GAMEOVER_WAIT            5
#define TPLAQ_STATE_DECOUNT_FUEL             6
#define TPLAQ_STATE_DECOUNT_FUEL_FINAL_WAIT  7
#define TPLAQ_STATE_DECOUNT_TEETH_UP         8
#define TPLAQ_STATE_DECOUNT_TEETH_DOWN       9
#define TPLAQ_STATE_NEXTLEVEL_WAIT1          10
#define TPLAQ_STATE_NEXTLEVEL_WAIT2          11
#define TPLAQ_STATE_ADD_TEETH                12
#define TPLAQ_STATE_ADD_TEETH_FINAL_WAIT     13

int tplaqState;
int tplaqWaitFrames;
int tplaqDecountSubTick;
int tplaqDecountTeethIndex;
int tplaqAddTeethIndex;
int tplaqForceRedraw;

void tplaqBeginAttract()
{
    tplaqTinyFlip( 3 );
    tplaqState = TPLAQ_STATE_ATTRACT;
}

void tplaqBeginNewGame()
{
    tplaqInitNewGame();
    tplaqLoadLevel();
    // Not tplaqUpdatePannel(): the explicit tplaqTinyFlip(0) two lines
    // below already redraws this exact scene (mode 2 and mode 0 are
    // pixel-identical - see this file's own header comment), so calling
    // it here too would just render the same frame twice.
    tplaqRefreshScoreOnly();
    tplaqBlinkStart = 0;
    tplaqTinyFlip( 0 );
    Sound( 100, 250 );
    Sound( 20, 250 );
    tplaqWaitFrames = 60;
    tplaqState = TPLAQ_STATE_START_WAIT1;
}

// Upstream's own main while(1) loop has no _delay_ms()/millis() throttle
// anywhere in it (confirmed by re-reading the real .ino) - it runs at
// whatever raw, uncapped speed the bare AVR loop achieves each iteration.
// Skip_Frame's own redraw gating (see tplaqUpdatePlaying below) was about
// amortizing the slow real SSD1306 write, not capping a target rate - the
// non-redraw iterations were comparatively cheap on real hardware and
// could run many times faster than Vircon32's own hard 60Hz engine tick.
// A direct user comparison against a real Arduboy build ("seems to almost
// run twice as fast as ours") confirmed our own logic was capped below
// the reference rate, since one real engine frame here can only ever
// advance the simulation by exactly one tick. Fixed the same way Tiny
// Arkanoid's own real ~8x-too-slow bug was fixed: decouple the logic-tick
// rate from the render rate:
#define TPLAQ_TICKS_PER_FRAME 2

// One real iteration of upstream's own per-tick gameplay body. Returns 1
// if this tick transitioned out of PLAYING (caller should stop iterating
// immediately - the new state's own first update call renders
// appropriately, matching every other state transition in this file).
int tplaqPlayingTick()
{
    if( isRightPressed() ) { tplaqMDroite( &tplaqMain ); tplaqRndPos(); }
    else if( isLeftPressed() ) { tplaqMGauche( &tplaqMain ); tplaqRndPos(); }
    else tplaqDecelX( &tplaqMain );

    if( isUpPressed() ) { tplaqMHaut( &tplaqMain ); tplaqRndPos(); }
    else if( isDownPressed() ) { tplaqMBas( &tplaqMain ); tplaqRndPos(); }
    else tplaqDecelY( &tplaqMain );

    if( isFirePressed() && tplaqGD.tubeFuelTimer > 0 ) tplaqWeaponStart( &tplaqWeapon, &tplaqMain );

    tplaqFoodMoveUpdate();
    tplaqHitBox();
    tplaqGamePlayTick();

    if( tplaqGD.regenNo == 0 )
    {
        int anyFoodActive = 0;
        int t;
        for( t = 0; t < TPLAQ_MAX_FOOD_D; t++ ) if( tplaqActive( &tplaqFoodSpr[t] ) != 0 ) anyFoodActive = 1;
        if( !anyFoodActive )
        {
            tplaqPutActive( &tplaqWeapon, 0 );
            tplaqGD.endOfGame = 1;
            tplaqDecountSubTick = 0;
            tplaqWaitFrames = 0;
            tplaqState = TPLAQ_STATE_DECOUNT_FUEL;
            return 1;
        }
    }

    // Upstream's own Skip_Frame==0 branch is the only place the real
    // main loop calls Tiny_Flip_TPLAQUE(0) at all - but see this
    // function's own header comment on why a literal port of that
    // redraw-only-1-in-6-ticks gating was the wrong fix in spirit (it
    // made motion look choppier, not faster, since Vircon32's own redraw
    // is cheap and never needed the throttle to begin with). The render
    // itself now always happens once per real frame, in
    // tplaqUpdatePlaying() below - only the ATTAQUE1 dither-blink toggle
    // and the tube-fuel-timer/refresh pacing (Skip_Frame's other two
    // uses, matching upstream's own cadence for those specific sub-
    // systems) stay gated to specific Skip_Frame values here.
    if( tplaqGD.skipFrame == 0 ) tplaqAttaque1 = !tplaqAttaque1;
    else if( tplaqGD.skipFrame == 2 ) { tplaqAdjustTubeTimer(); tplaqAdjustTube(); }
    else if( tplaqGD.skipFrame == 3 ) { if( tplaqGD.tubeRefresh > 0 ) tplaqGD.tubeRefresh--; else tplaqGD.tubeRefresh = 8; }

    if( tplaqGD.skipFrame < 5 ) tplaqGD.skipFrame++; else tplaqGD.skipFrame = 0;

    return 0;
}

void tplaqUpdatePlaying()
{
    int i;
    for( i = 0; i < TPLAQ_TICKS_PER_FRAME; i++ )
    {
        if( tplaqPlayingTick() ) return;
    }
    tplaqTinyFlip( 0 );
}

void tplaqUpdateDecountFuel()
{
    if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }

    if( tplaqGD.tubeFuel < 4 )
    {
        if( tplaqDecountSubTick < 4 )
        {
            // Not a leading tplaqTinyFlip(1) here: upstream calls it, then
            // a few lines later (after the score changes)
            // UPDATE_PANNEL_TPLAQUE() redraws again - on real hardware
            // each of those is a real, separately-visible SSD1306 write
            // (absorbed by the loop's own _delay_ms(15)), but here only
            // the *last* draw before this tick ends is ever presented, so
            // the first render was pure wasted work every single substep
            // of this loop - a direct user report of a CPU spike during
            // exactly this sequence ("little music and score getting
            // added") traced back to this double-render, found by
            // inspection, not testing. tplaqUpdatePannel()'s own render
            // (after the score increments) is the one actually kept.
            tplaqDSound2();
            tplaqScoreAdd( 1 );
            tplaqUpdatePannel();
            tplaqDecountSubTick++;
            tplaqWaitFrames = 1;
            return;
        }
        tplaqGD.tubeFuel++;
        tplaqTinyFlip( 1 );
        tplaqDSound2();
        tplaqDecountSubTick = 0;
        tplaqWaitFrames = 2;
        return;
    }

    tplaqTinyFlip( 1 );
    tplaqWaitFrames = 12;
    tplaqState = TPLAQ_STATE_DECOUNT_FUEL_FINAL_WAIT;
}

void tplaqUpdateDecountTeethUp()
{
    if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }
    while( tplaqDecountTeethIndex < 8 )
    {
        TplaqSprite* t = &tplaqTeethUp[ tplaqDecountTeethIndex ];
        tplaqDecountTeethIndex++;
        if( tplaqActive( t ) == 1 )
        {
            tplaqGD.endOfGame = 0;
            tplaqPutActive( t, -1 );
            tplaqScoreAdd( 2 );
            tplaqCompilSco();
            tplaqDSound( 0 );
            tplaqTinyFlip( 1 );
            tplaqWaitFrames = 2;
            return;
        }
    }
    tplaqDecountTeethIndex = 0;
    tplaqState = TPLAQ_STATE_DECOUNT_TEETH_DOWN;
}

void tplaqUpdateDecountTeethDown()
{
    if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }
    while( tplaqDecountTeethIndex < 8 )
    {
        TplaqSprite* t = &tplaqTeethDown[ tplaqDecountTeethIndex ];
        tplaqDecountTeethIndex++;
        if( tplaqActive( t ) == 1 )
        {
            tplaqGD.endOfGame = 0;
            tplaqPutActive( t, -1 );
            tplaqScoreAdd( 2 );
            tplaqCompilSco();
            tplaqDSound( 0 );
            tplaqTinyFlip( 1 );
            tplaqWaitFrames = 2;
            return;
        }
    }

    if( tplaqGD.endOfGame )
    {
        if( tplaqGD.extraTeeth > 0 ) tplaqGD.endOfGame = 0;
    }
    if( tplaqGD.endOfGame )
    {
        tplaqTinyFlip( 0 );
        tplaqStartAlarm();
        tplaqState = TPLAQ_STATE_GAMEOVER_ALARM;
        return;
    }
    tplaqNextLevel();
    tplaqWaitFrames = 24;
    tplaqState = TPLAQ_STATE_NEXTLEVEL_WAIT1;
}

void tplaqUpdateAddTeeth()
{
    if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }

    while( tplaqAddTeethIndex < 16 )
    {
        TplaqSprite* cand = tplaqAddTeethCandidate( tplaqAddTeethIndex );
        tplaqAddTeethIndex++;
        if( tplaqActive( cand ) == 0 )
        {
            if( tplaqGD.extraTeeth > 0 )
            {
                tplaqPutActive( cand, 1 );
                tplaqSoundAddTeeth();
                tplaqGD.extraTeeth--;
                tplaqTinyFlip( 1 );
                tplaqWaitFrames = 24;
                if( tplaqGD.extraTeeth == 0 ) tplaqAddTeethIndex = 16;
                return;
            }
            tplaqAddTeethIndex = 16;
            break;
        }
    }
    tplaqWaitFrames = 24;
    tplaqState = TPLAQ_STATE_ADD_TEETH_FINAL_WAIT;
}

void tplaqForceRedrawNow()
{
    if( tplaqState == TPLAQ_STATE_ATTRACT ) tplaqTinyFlip( 3 );
    else if( tplaqState == TPLAQ_STATE_PLAYING || tplaqState == TPLAQ_STATE_START_WAIT1 ||
             tplaqState == TPLAQ_STATE_GAMEOVER_ALARM || tplaqState == TPLAQ_STATE_GAMEOVER_WAIT )
      tplaqTinyFlip( 0 );
    else
      tplaqTinyFlip( 1 );
}

void gameTinyPlaque_init()
{
    InitTinyJoypad();
    tplaqBlinkStart = 0;
    tplaqRndPosCounter = 19;
    tplaqRdIndex = 0;
    tplaqBeginAttract();
}

// Quit-confirmation-dialog resume hook - checked proactively against the
// onResume audit before shipping (matching Tiny Pipe/Morpion's own
// practice) - TPLAQ_STATE_ATTRACT has no timer of its own (real,
// indefinite risk); every other state is bounded but still wired for
// consistency, matching this project's established precedent.
void gameTinyPlaque_forceRedraw()
{
    tplaqForceRedraw = 1;
}

void gameTinyPlaque_update()
{
    if( tplaqForceRedraw )
    {
        tplaqForceRedrawNow();
        tplaqForceRedraw = 0;
    }

    if( tplaqState == TPLAQ_STATE_ATTRACT )
    {
        if( isFirePressed() )
        {
            md_armInputFireGate();
            tplaqBeginNewGame();
            return;
        }
        tplaqTinyFlip( 3 );
        if( tplaqBlinkStart > 0 ) tplaqBlinkStart--; else tplaqBlinkStart = 22;
        return;
    }

    if( tplaqState == TPLAQ_STATE_START_WAIT1 )
    {
        if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }
        tplaqRestoreTeeth();
        tplaqTinyFlip( 1 );
        tplaqWaitFrames = 60;
        tplaqState = TPLAQ_STATE_START_WAIT2;
        return;
    }

    if( tplaqState == TPLAQ_STATE_START_WAIT2 )
    {
        if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }
        tplaqState = TPLAQ_STATE_PLAYING;
        return;
    }

    if( tplaqState == TPLAQ_STATE_PLAYING ) { tplaqUpdatePlaying(); return; }

    if( tplaqState == TPLAQ_STATE_GAMEOVER_ALARM )
    {
        if( tplaqAdvanceAlarm() )
        {
            tplaqWaitFrames = 60;
            tplaqState = TPLAQ_STATE_GAMEOVER_WAIT;
        }
        return;
    }

    if( tplaqState == TPLAQ_STATE_GAMEOVER_WAIT )
    {
        if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }
        tplaqBeginAttract();
        return;
    }

    if( tplaqState == TPLAQ_STATE_DECOUNT_FUEL ) { tplaqUpdateDecountFuel(); return; }

    if( tplaqState == TPLAQ_STATE_DECOUNT_FUEL_FINAL_WAIT )
    {
        if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }
        tplaqDecountTeethIndex = 0;
        tplaqState = TPLAQ_STATE_DECOUNT_TEETH_UP;
        return;
    }

    if( tplaqState == TPLAQ_STATE_DECOUNT_TEETH_UP ) { tplaqUpdateDecountTeethUp(); return; }
    if( tplaqState == TPLAQ_STATE_DECOUNT_TEETH_DOWN ) { tplaqUpdateDecountTeethDown(); return; }

    if( tplaqState == TPLAQ_STATE_NEXTLEVEL_WAIT1 )
    {
        if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }
        tplaqRestoreTeeth();
        tplaqTinyFlip( 1 );
        tplaqWaitFrames = 24;
        tplaqState = TPLAQ_STATE_NEXTLEVEL_WAIT2;
        return;
    }

    if( tplaqState == TPLAQ_STATE_NEXTLEVEL_WAIT2 )
    {
        if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }
        tplaqAddTeethIndex = 0;
        tplaqState = TPLAQ_STATE_ADD_TEETH;
        return;
    }

    if( tplaqState == TPLAQ_STATE_ADD_TEETH ) { tplaqUpdateAddTeeth(); return; }

    if( tplaqState == TPLAQ_STATE_ADD_TEETH_FINAL_WAIT )
    {
        if( tplaqWaitFrames > 0 ) { tplaqWaitFrames--; return; }
        tplaqState = TPLAQ_STATE_PLAYING;
        return;
    }
}

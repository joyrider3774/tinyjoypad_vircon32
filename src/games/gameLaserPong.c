// =============================================================================
// Laser Pong (Winston-Lu, MIT) - an enhanced Pong: a cooldown-gated
// deflecting "shoot" projectile, a speed-burst "spike" ability, and
// adjustable AI difficulty on top of the classic 2-paddle formula. From
// `more games/ATTiny85_Pong/` - staged during the "very very deep scan"
// search batch; the game's own title screen literally reads "Laser
// Pong" (`showTitle()`), which is what this port's own menu title and
// internal identifier prefix (`lpg`) both take their name from - the
// repo's own name (`ATTiny85_Pong`) would have collided with this
// project's already-shipped `gamePong.c` (Bat Bonanza, a different,
// unrelated Pong clone with its own `pong` prefix).
//
// Not `tinyJoypadShim`/`obonoCoreShim` lineage - genuine bespoke
// hardware, an external `ssd1306.h`/`SPRITE` dependency (the
// `lexus2k/ssd1306` library, the same family Dino Game's own display
// driver and Lode Runner's rejected candidate both come from - not
// itself needed here, since this port composites columns directly the
// same way every other game in this project does) and a single-analog-
// pin voltage ladder decoding up to 4 simultaneous directions (the
// `switch(analogRead(A1))` case-range table in the real source is a
// classic multi-button ladder, structurally the same shape as
// TinyJoypad's own scheme). **Needed no new shim at all**: every one of
// upstream's 9 non-zero ladder bands turns out to be some combination of
// exactly 4 independent booleans - Up/Down move the paddle, Left "spikes"
// the ball, Right fires a deflecting laser (confirmed directly from the
// real `switch` cases, not from the upstream README's own prose, which
// actually states the opposite left/right mapping - see below) - so
// `isUpPressed()`/`isDownPressed()`/`isLeftPressed()`/`isRightPressed()`
// checked independently reproduce every one of upstream's 9 combined
// band cases exactly, without needing to replicate the ladder-threshold
// table at all. `isFirePressed()` (unused by native gameplay) was used
// for the attract-screen "press to start" gesture, matching this
// project's own standing convention.
//
// **A real discrepancy between the upstream README and the upstream
// code, resolved by trusting the code**: the README states "left shoots
// a projectile... right causes the ball to spike" - but the actual
// `switch` cases show band 35-37 (Left alone, per the source's own
// threshold-comment table) calls `startSpike()`, and band 61-63 (Right
// alone) calls `startShoot()` - the exact opposite of the README's own
// claim. Ported to match the real code (Left=spike, Right=shoot), not
// the README's prose.
//
// **Confirmed via direct source reading (`#define HEIGHT 32`) that this
// targets a genuine 128x32 display, not 128x64** - placed within
// Vircon32's fixed 128x64 canvas the same way every other 128x32-native
// port in this same batch was handled (ATtiny Tetromino, Tiny Bulls And
// Cows) - gameplay only touches pages 0-3, with pages 4-7 explicitly
// redrawn blank every frame to avoid the VRAM-persistence bug class.
//
// **Every sprite (paddle/ball/laser) is a genuinely sub-page-positioned,
// arbitrary-column-value column byte** - not a page-aligned block. The
// paddle (`{0xFF,0xFF}`, 2 columns) and laser (`{0x03,0x03}`, 2 columns)
// are uniform per-column values; the ball (`ball[8]`) has real per-
// column bitmap variation (an actual round/oval shape, not a solid
// square). All three are composited through one shared sub-page byte-
// split helper (`lpgSpriteColByte()`), the same explicit-shift-and-mask
// technique already proven in this project (Meteor Storm/Run Dude Run's
// own sub-page sprite math) - both Y values here are confirmed always
// non-negative (paddles clamp to `[0, HEIGHT-PADDLELENGTH]`, the ball's
// own Y is bounced back at 0/HEIGHT-BALLDIAMETER before ever going
// negative), so there's no logical-vs-arithmetic-shift hazard to guard
// against here.
//
// Physics use real floats (`sqrt`/`abs`/`round`, all confirmed available
// via Vircon32's own `math.h`) - `generateRandomDirection()`'s own
// Pythagorean deltaY derivation is safe by construction (`|deltaX|` is
// always strictly less than `ballSpeed` by the formula's own 0.7-0.959
// multiplier range, so `ballSpeed^2 - deltaX^2` can never go negative,
// which would otherwise trigger a hardware error on Vircon32's own
// `sqrt`). One genuine-looking upstream quirk was traced and preserved
// exactly, not "fixed": `deltaY *= SPIKESPEED/10;` computes
// `SPIKESPEED/10` as **integer** division (6/10 = 0) before the float
// multiply ever happens, meaning every spike shot's own Y-velocity is
// silently zeroed - very likely an unintentional integer-truncation bug
// in the original game (the author probably intended a fractional
// dampening factor like 0.6, not a hard zero), but ported literally
// rather than assumed and "corrected", matching this project's own
// standing practice of preserving upstream behavior faithfully unless
// asked to change it.
//
// Upstream's own status LEDs (spike-ready/shoot-ready cooldown
// indicators on real hardware pins) have no Vircon32 equivalent and were
// dropped entirely, matching this project's established treatment of
// hardware-only outputs with no platform analogue. EEPROM/persistent
// high-score tracking doesn't exist in this game to begin with (no
// EEPROM calls anywhere in the source). Built with per-page compositing
// from the start (ball/laser/paddle column ranges are all narrow and
// precomputed once per frame, not scanned per pixel) - no O(pixels x
// objects) shape to retrofit.
// =============================================================================

// -----------------------------------------------------------------------------
//   Data
// -----------------------------------------------------------------------------

#define LPG_WIDTH 128
#define LPG_HEIGHT 32
#define LPG_BALL_DIAMETER 8
#define LPG_PADDLE_LENGTH 8
#define LPG_PADDLE_WIDTH 2
#define LPG_SCORE_TO_WIN 10
#define LPG_AI_DIFFICULTY 65
#define LPG_SPIKE_SPEED 6
#define LPG_SPIKE_COOLDOWN_LENGTH 1500
#define LPG_LASER_SPEED 3
#define LPG_SHOOT_COOLDOWN_LENGTH 300

// Byte-diff-verified against upstream's own paddle[]/projectile[]/ball[].
int[2] lpgPaddleCol = { 0xFF, 0xFF };
int[2] lpgProjectileCol = { 0x03, 0x03 };
int[8] lpgBallCol =
{
0x0E, 0x1F, 0x3F, 0x7E, 0x7E, 0x3D, 0x19, 0x0E
};

// Standard 95-char font, already proven for several ports in this
// project - reused for the title/score text, each game keeping its own
// self-contained copy per this project's convention.
int[570] lpgFont =
{
0,0,0,0,0,0,0,0,0,47,0,0,0,0,7,0,
7,0,0,20,127,20,127,20,0,36,42,127,42,18,0,98,
100,8,19,35,0,54,73,85,34,80,0,0,5,3,0,0,
0,0,28,34,65,0,0,0,65,34,28,0,0,20,8,62,
8,20,0,8,8,62,8,8,0,0,0,160,96,0,0,8,
8,8,8,8,0,0,96,96,0,0,0,32,16,8,4,2,
0,62,81,73,69,62,0,0,66,127,64,0,0,66,97,81,
73,70,0,33,65,69,75,49,0,24,20,18,127,16,0,39,
69,69,69,57,0,60,74,73,73,48,0,1,113,9,5,3,
0,54,73,73,73,54,0,6,73,73,41,30,0,0,54,54,
0,0,0,0,86,54,0,0,0,8,20,34,65,0,0,20,
20,20,20,20,0,0,65,34,20,8,0,2,1,81,9,6,
0,50,73,89,81,62,0,124,18,17,18,124,0,127,73,73,
73,54,0,62,65,65,65,34,0,127,65,65,34,28,0,127,
73,73,73,65,0,127,9,9,9,1,0,62,65,73,73,122,
0,127,8,8,8,127,0,0,65,127,65,0,0,32,64,65,
63,1,0,127,8,20,34,65,0,127,64,64,64,64,0,127,
2,12,2,127,0,127,4,8,16,127,0,62,65,65,65,62,
0,127,9,9,9,6,0,62,65,81,33,94,0,127,9,25,
41,70,0,70,73,73,73,49,0,1,1,127,1,1,0,63,
64,64,64,63,0,31,32,64,32,31,0,63,64,56,64,63,
0,99,20,8,20,99,0,7,8,112,8,7,0,97,81,73,
69,67,0,0,127,65,65,0,0,2,4,8,16,32,0,0,
65,65,127,0,0,4,2,1,2,4,0,64,64,64,64,64,
0,0,1,2,4,0,0,32,84,84,84,120,0,127,72,68,
68,56,0,56,68,68,68,32,0,56,68,68,72,127,0,56,
84,84,84,24,0,8,126,9,1,2,0,24,164,164,164,124,
0,127,8,4,4,120,0,0,68,125,64,0,0,64,128,132,
125,0,0,127,16,40,68,0,0,0,65,127,64,0,0,124,
4,24,4,120,0,124,8,4,4,120,0,56,68,68,68,56,
0,252,36,36,36,24,0,24,36,36,24,252,0,124,8,4,
4,8,0,72,84,84,84,32,0,4,63,68,64,32,0,60,
64,64,32,124,0,28,32,64,32,28,0,60,64,48,64,60,
0,68,40,16,40,68,0,28,160,160,160,124,0,68,100,84,
76,68,0,8,54,65,65,0,0,0,0,127,0,0,0,0,
65,65,54,8,0,8,4,8,16,8,
};

int lpgFontByte( int ch, int col )
{
    if( ( ch < 32 ) || ( ch > 126 ) ) return 0;
    return lpgFont[ ( ch - 32 ) * 6 + col ];
}

int lpgTextByteAt( int* text, int textLen, int startX, int x )
{
    if( x < startX ) return 0;
    int rel = x - startX;
    int charIdx = rel / 6;
    if( charIdx >= textLen ) return 0;
    return lpgFontByte( text[ charIdx ], rel % 6 );
}

// -----------------------------------------------------------------------------
//   Game state
// -----------------------------------------------------------------------------

int lpgPlayerY;
int lpgCpuY;

float lpgBallXF;
float lpgBallYF;
float lpgBallDirX;
float lpgBallDirY;
float lpgBallSpeed;

bool lpgSpikeBall;
bool lpgShoot;
int lpgSpikeCooldown;
int lpgShootCooldown;

int lpgLaserX;
int lpgLaserY;

int lpgPlayerScore;
int lpgCpuScore;

// -----------------------------------------------------------------------------
//   Game logic
// -----------------------------------------------------------------------------

// Direct translation of generateRandomDirection() - operates on the
// global ball direction rather than upstream's own C++ float& reference
// parameters, since this dialect has no reference-parameter support;
// behaviorally identical either way, since upstream only ever calls it
// against the same ball-direction globals anyway.
void lpgGenerateRandomDirection( bool conserveDirection )
{
    lpgBallDirX = lpgBallSpeed * ( arand( 260 ) / 1000.0 + 0.7 );
    if( arand( 2 ) == 1 ) lpgBallDirX = -lpgBallDirX;

    bool yNeg = ( lpgBallDirY < 0 );
    lpgBallDirY = sqrt( lpgBallSpeed * lpgBallSpeed - lpgBallDirX * lpgBallDirX );
    if( yNeg ) lpgBallDirY = -lpgBallDirY;
    if( !conserveDirection && arand( 2 ) == 1 ) lpgBallDirY = -lpgBallDirY;
}

void lpgStartShoot()
{
    if( lpgShootCooldown > LPG_SHOOT_COOLDOWN_LENGTH )
    {
        lpgLaserY = lpgPlayerY + LPG_PADDLE_LENGTH / 2 - 1;
        lpgShootCooldown = 0;
        lpgShoot = true;
    }
}

void lpgStartSpike()
{
    if( lpgSpikeCooldown > LPG_SPIKE_COOLDOWN_LENGTH ) lpgSpikeBall = true;
}

void lpgMoveAI()
{
    if( arand( 100 ) < LPG_AI_DIFFICULTY )
    {
        if( lpgBallYF > lpgCpuY + LPG_PADDLE_WIDTH / 2 )
        {
            if( lpgCpuY + LPG_PADDLE_WIDTH < LPG_HEIGHT ) lpgCpuY++;
        }
        else
        {
            if( lpgCpuY > 0 ) lpgCpuY--;
        }
    }
}

void lpgResetRound()
{
    lpgBallXF = ( LPG_WIDTH - LPG_BALL_DIAMETER ) / 2;
    lpgBallYF = ( LPG_HEIGHT - LPG_BALL_DIAMETER ) / 2;
    lpgCpuY = ( LPG_HEIGHT + LPG_PADDLE_LENGTH ) / 2;
    lpgBallSpeed = 1;
    lpgGenerateRandomDirection( false );

    lpgLaserX = 0;
    lpgShoot = false;
    lpgShootCooldown = 0;
    lpgSpikeBall = false;
    lpgSpikeCooldown = 0;
}

// Returns true if this tick's ball movement scored a point (either
// side) - direct translation of moveBall()'s own reset-when-hitting-the-
// side branch, paddle bounce checks, and top/bottom wall bounce.
bool lpgMoveBall()
{
    bool scored = false;
    int ballX = round( lpgBallXF );

    // Upstream keeps this as a genuinely separate statement from the
    // paddle-bounce/wall-bounce/move logic below (not an if/else) - so
    // on the exact tick a point is scored, the freshly-reset ball
    // position/direction still falls through the bounce checks (both
    // trivially false, since the reset position sits far from either
    // paddle) and the final move step, taking one small extra step away
    // from dead-center that same frame. Preserved exactly rather than
    // short-circuited into an if/else, even though the resulting visual
    // difference is a single sub-pixel frame.
    if( ballX <= 1 || ballX >= LPG_WIDTH - LPG_BALL_DIAMETER - 1 )
    {
        if( ballX <= 1 ) lpgCpuScore++;
        if( ballX >= LPG_WIDTH - LPG_BALL_DIAMETER - 1 ) lpgPlayerScore++;
        lpgResetRound();
        scored = true;
    }

    if( lpgBallDirX < 0 && ballX <= LPG_PADDLE_WIDTH + 1 )
    {
        int ballY = round( lpgBallYF );
        if( ballY + LPG_BALL_DIAMETER >= lpgPlayerY && ballY <= lpgPlayerY + LPG_PADDLE_LENGTH )
        {
            lpgGenerateRandomDirection( true );
            lpgBallDirX = fabs( lpgBallDirX );
            if( lpgSpikeBall )
            {
                lpgBallDirX *= ( LPG_SPIKE_SPEED / 2 ) * 2;
                lpgBallDirY *= LPG_SPIKE_SPEED / 10; // see this file's own header comment - a faithfully-preserved upstream integer-truncation quirk (always zero)
                lpgSpikeBall = false;
                lpgSpikeCooldown = 0;
            }
        }
    }
    else if( lpgBallDirX > 0 && ballX >= LPG_WIDTH - LPG_BALL_DIAMETER - LPG_PADDLE_WIDTH - 1 )
    {
        int ballY = round( lpgBallYF );
        if( ballY + LPG_BALL_DIAMETER >= lpgCpuY && ballY <= lpgCpuY + LPG_PADDLE_LENGTH )
        {
            lpgGenerateRandomDirection( true );
            lpgBallDirX = -fabs( lpgBallDirX );
        }
    }

    if( lpgBallDirY > 0 && lpgBallYF + lpgBallDirY + LPG_BALL_DIAMETER > LPG_HEIGHT ) lpgBallDirY = -lpgBallDirY;
    if( lpgBallDirY < 0 && lpgBallYF + lpgBallDirY < 0 ) lpgBallDirY = -lpgBallDirY;

    lpgBallXF += lpgBallDirX;
    lpgBallYF += lpgBallDirY;

    return scored;
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int[128] lpgPageBuffer;

// One column's own byte for a sub-page-positioned sprite - the same
// explicit shift-and-mask technique already proven in this project
// (Meteor Storm/Run Dude Run's own sub-page sprite math). `colByte`
// already encodes the sprite's real height as its own zero/nonzero bits
// (e.g. the 2px-tall laser is `0x03` - only bits 0-1 ever set), so no
// separate height parameter is needed.
int lpgSpriteColByte( int colByte, int y, int page )
{
    int basePage = y / 8;
    int offset = y % 8;
    if( page == basePage ) return ( colByte << offset ) & 0xFF;
    if( page == basePage + 1 ) return ( colByte >> ( 8 - offset ) ) & 0xFF;
    return 0;
}

void lpgComposeRow( int page )
{
    int col;
    for( col = 0; col < 128; col++ ) lpgPageBuffer[ col ] = 0;

    if( page > 3 ) return; // gameplay only ever touches the top half

    // Player paddle - columns 0-1.
    int c;
    for( c = 0; c < 2; c++ )
      lpgPageBuffer[ c ] |= lpgSpriteColByte( lpgPaddleCol[ c ], lpgPlayerY, page );

    // CPU paddle - columns 126-127.
    for( c = 0; c < 2; c++ )
      lpgPageBuffer[ 126 + c ] |= lpgSpriteColByte( lpgPaddleCol[ c ], lpgCpuY, page );

    // Ball - its own real 8-column-wide bitmap.
    int ballX = round( lpgBallXF );
    int ballY = round( lpgBallYF );
    for( c = 0; c < 8; c++ )
    {
        int x = ballX + c;
        if( x >= 0 && x < 128 )
          lpgPageBuffer[ x ] |= lpgSpriteColByte( lpgBallCol[ c ], ballY, page );
    }

    // Laser - 2 columns, only while active.
    if( lpgShoot )
    {
        for( c = 0; c < 2; c++ )
        {
            int x = lpgLaserX + c;
            if( x >= 0 && x < 128 )
              lpgPageBuffer[ x ] |= lpgSpriteColByte( lpgProjectileCol[ c ], lpgLaserY, page );
        }
    }

    // Score text - page 0 only, matching upstream's own 3 separate
    // updateScore() draws: the player score right-aligned against the
    // fixed colon position (1 digit at x=54, 2 digits "10" at x=48, both
    // ending just before the colon), the colon at a fixed x=61, and the
    // CPU score at a fixed x=68 regardless of its own digit count.
    if( page == 0 )
    {
        int[2] playerText;
        int playerLen;
        int playerX;
        if( lpgPlayerScore >= 10 ) { playerText[0]='1'; playerText[1]='0'; playerLen=2; playerX=48; }
        else { playerText[0] = 48 + lpgPlayerScore; playerLen=1; playerX=54; }

        int[2] cpuText;
        int cpuLen;
        if( lpgCpuScore >= 10 ) { cpuText[0]='1'; cpuText[1]='0'; cpuLen=2; }
        else { cpuText[0] = 48 + lpgCpuScore; cpuLen=1; }

        int[1] colonText = { ':' };

        for( col = 0; col < 128; col++ )
        {
            lpgPageBuffer[ col ] |= lpgTextByteAt( playerText, playerLen, playerX, col );
            lpgPageBuffer[ col ] |= lpgTextByteAt( colonText, 1, 61, col );
            lpgPageBuffer[ col ] |= lpgTextByteAt( cpuText, cpuLen, 68, col );
        }
    }
}

void lpgRenderAttract()
{
    int page, col;
    for( page = 0; page < 8; page++ )
    {
        for( col = 0; col < 128; col++ ) lpgPageBuffer[ col ] = 0;

        if( page == 2 )
        {
            int* t = "LASER PONG";
            for( col = 0; col < 128; col++ ) lpgPageBuffer[ col ] |= lpgTextByteAt( t, 10, 34, col );
        }
        if( page == 4 )
        {
            int* t = "UP/DOWN MOVE";
            for( col = 0; col < 128; col++ ) lpgPageBuffer[ col ] |= lpgTextByteAt( t, 12, 28, col );
        }
        if( page == 5 )
        {
            int* t = "LEFT SPIKE RIGHT SHOOT";
            for( col = 0; col < 128; col++ ) lpgPageBuffer[ col ] |= lpgTextByteAt( t, 22, 4, col );
        }
        if( page == 7 )
        {
            int* t = "PRESS FIRE";
            for( col = 0; col < 128; col++ ) lpgPageBuffer[ col ] |= lpgTextByteAt( t, 10, 34, col );
        }

        for( col = 0; col < 128; col++ )
          md_drawColumn( col, page, lpgPageBuffer[ col ] );
    }
}

void lpgRenderWinScreen( bool playerWon )
{
    int page, col;
    for( page = 0; page < 8; page++ )
    {
        lpgComposeRow( page );

        if( page == 2 )
        {
            if( playerWon )
            {
                int* t = "PLAYER WINS";
                for( col = 0; col < 128; col++ ) lpgPageBuffer[ col ] |= lpgTextByteAt( t, 11, 31, col );
            }
            else
            {
                int* t = "CPU WINS";
                for( col = 0; col < 128; col++ ) lpgPageBuffer[ col ] |= lpgTextByteAt( t, 8, 43, col );
            }
        }

        for( col = 0; col < 128; col++ )
          md_drawColumn( col, page, lpgPageBuffer[ col ] );
    }
}

void lpgRenderPlaying()
{
    int page, col;
    for( page = 0; page < 8; page++ )
    {
        lpgComposeRow( page );
        for( col = 0; col < 128; col++ )
          md_drawColumn( col, page, lpgPageBuffer[ col ] );
    }
}

// -----------------------------------------------------------------------------
//   Input / per-tick update
// -----------------------------------------------------------------------------

// Direct translation of startGame()'s own per-tick body, MINUS the
// moveBall() call at its very top (upstream's own comment: "move ball
// first to avoid pixel overlap bugs with ball size <8") - called
// separately, once, by the state machine below, matching upstream's own
// real call order (moveBall, then the win check, then this). Up/Down
// move the paddle, Left spikes, Right shoots (see this file's own header
// comment for why this is the correct mapping, not the upstream README's
// own reversed claim).
void lpgUpdateInputAndAI()
{
    if( isUpPressed() ) { if( lpgPlayerY > 0 ) lpgPlayerY--; }
    if( isDownPressed() ) { if( lpgPlayerY + LPG_PADDLE_LENGTH < LPG_HEIGHT ) lpgPlayerY++; }
    if( isLeftPressed() ) lpgStartSpike();
    if( isRightPressed() ) lpgStartShoot();

    if( lpgShoot )
    {
        int ballX = round( lpgBallXF );
        int ballY = round( lpgBallYF );
        bool hitBall = ( lpgLaserX + 2 >= ballX ) && ( lpgLaserX <= ballX + LPG_BALL_DIAMETER )
                     && ( lpgLaserY <= ballY + LPG_BALL_DIAMETER ) && ( lpgLaserY + 2 >= ballY );
        if( hitBall )
        {
            lpgBallDirX = fabs( lpgBallDirX );
            lpgShoot = false;
            lpgLaserX = 0;
        }
        else if( lpgLaserX < LPG_WIDTH - 2 )
        {
            lpgLaserX += LPG_LASER_SPEED;
        }
        else
        {
            lpgShoot = false;
            lpgLaserX = 0;
        }
    }

    lpgMoveAI();

    lpgSpikeCooldown++;
    lpgShootCooldown++;

    lpgBallSpeed += 0.0005;
}

// -----------------------------------------------------------------------------
//   State machine
// -----------------------------------------------------------------------------

#define LPG_STATE_ATTRACT 0
#define LPG_STATE_PLAYING 1
#define LPG_STATE_SCORE_PAUSE 2
#define LPG_STATE_WIN_PAUSE 3

int lpgState;
bool lpgPrevFire;
int lpgWaitFrames;
bool lpgPlayerWon;

void lpgBeginAttract()
{
    lpgPrevFire = false;
    lpgState = LPG_STATE_ATTRACT;
}

void lpgBeginPlaying()
{
    lpgPlayerY = ( LPG_HEIGHT - LPG_PADDLE_LENGTH ) / 2;
    lpgCpuY = ( LPG_HEIGHT - LPG_PADDLE_LENGTH ) / 2;
    lpgPlayerScore = 0;
    lpgCpuScore = 0;
    lpgResetRound();
    lpgState = LPG_STATE_PLAYING;
}

// Matches updateScore()'s own real delay(1000) - a brief pause showing
// the just-updated score after every point.
void lpgBeginScorePause()
{
    lpgWaitFrames = 60;
    lpgState = LPG_STATE_SCORE_PAUSE;
}

// Matches the win branch's own real delay(5000).
void lpgBeginWinPause( bool playerWon )
{
    lpgPlayerWon = playerWon;
    lpgWaitFrames = 300;
    lpgState = LPG_STATE_WIN_PAUSE;
}

void gameLaserPong_init()
{
    lpgBeginAttract();
}

void gameLaserPong_forceRedraw()
{
    if( lpgState == LPG_STATE_ATTRACT ) lpgRenderAttract();
    else if( lpgState == LPG_STATE_WIN_PAUSE ) lpgRenderWinScreen( lpgPlayerWon );
    else lpgRenderPlaying();
}

void gameLaserPong_update()
{
    md_beginFrame();

    if( lpgState == LPG_STATE_ATTRACT )
    {
        bool fireNow = isFirePressed();
        if( fireNow && !lpgPrevFire )
        {
            lpgBeginPlaying();
            lpgRenderPlaying();
            return;
        }
        lpgPrevFire = fireNow;
        lpgRenderAttract();
    }
    else if( lpgState == LPG_STATE_PLAYING )
    {
        // Matches upstream's own real per-tick order exactly: moveBall()
        // first ("to avoid pixel overlap bugs with ball size <8", per
        // its own comment), then the win check (using whatever score
        // moveBall() itself may have just updated), then input/AI - not
        // the other way around.
        bool scored = lpgMoveBall();

        if( lpgPlayerScore == LPG_SCORE_TO_WIN || lpgCpuScore == LPG_SCORE_TO_WIN )
        {
            lpgBeginWinPause( lpgPlayerScore == LPG_SCORE_TO_WIN );
            lpgRenderWinScreen( lpgPlayerWon );
            return;
        }

        if( scored )
        {
            lpgRenderPlaying();
            lpgBeginScorePause();
            return;
        }

        lpgUpdateInputAndAI();
        lpgRenderPlaying();
    }
    else if( lpgState == LPG_STATE_SCORE_PAUSE )
    {
        lpgWaitFrames--;
        if( lpgWaitFrames <= 0 ) { lpgState = LPG_STATE_PLAYING; lpgRenderPlaying(); return; }
        lpgRenderPlaying();
    }
    else // LPG_STATE_WIN_PAUSE
    {
        lpgWaitFrames--;
        if( lpgWaitFrames <= 0 ) { lpgBeginAttract(); lpgRenderAttract(); return; }
        lpgRenderWinScreen( lpgPlayerWon );
    }
}

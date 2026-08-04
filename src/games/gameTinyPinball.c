// =============================================================================
// Tiny Pinball - ported from Daniel C's tinyPinball.ino (tinyjoypad.com,
// GPLv3). Same tinyJoypadShim lineage as Tiny Invaders (FastTinyDriver.h)
// - the button reads below reuse that shim as-is, no shim changes needed.
//
// Button mapping: upstream reads the analog ladder/discrete pin directly
// rather than going through isXPressed()-style helpers (`analogRead(A3)`
// ranges + `digitalRead(1)`), and its own ELECTROLIB.h confirms A3 is the
// up/down axis (`TINYJOYPAD_UP`/`TINYJOYPAD_DOWN` use the exact same
// thresholds as tinyJoypadShim's isUpPressed()/isDownPressed()) - so
// upstream's actual physical mapping is left flipper=up, right flipper/
// launch=down-or-fire (digitalRead(1) is the same discrete pin
// tinyJoypadShim maps to Fire, repurposed here as an alternate input, and
// upstream shares one input for both the right flipper *and* charging the
// launch spring). Remapped for a real gamepad: left flipper=left d-pad,
// right flipper=right d-pad, launch spring=down d-pad - three independent
// inputs instead of upstream's two-inputs-doing-double-duty. Up/Down were
// later added as aliases for the left/right flippers too (per user
// request) - Up doubling as left-flipper happens to restore upstream's
// own original physical mapping (left flipper was Up there), while Down
// doubling as right-flipper means holding Down both charges the launch
// spring and flips the right flipper simultaneously. "Any input" (title
// screen) is the OR of all three original d-pad inputs, plus Fire (added
// later, per user request, since a d-pad tap felt like an unintuitive way
// to start next to every other game's "press Fire/A to start" convention)
// - upstream
// itself never used Fire for anything here at all.
//
// Structural changes from upstream, beyond the usual dialect fixes (no
// goto - the two bounded, non-blocking-loop gotos became plain `while`
// loops; struct-without-typedef; no per-field struct initializers):
//  - upstream's loop() is NEWGAME/start labels + two while(1) loops -
//    rewritten as an explicit frame-stepped state machine, the same
//    approach gameTinyInvaders.c's own header comment describes.
//  - Dead code dropped: the SSD64x32==1 branches (that compile-time
//    constant is 0 in upstream too - never built), the unused SelectByte()
//    function, and the BALL struct's never-read resetBall/DecelX/DecelY/
//    grid fields (write-only or fully unused upstream).
//  - Tiny_Flip2's Min/Max partial-redraw-band optimization is dropped -
//    always draws the full 8 pages. Upstream relied on the physical
//    SSD1306's VRAM *persisting* pixels it didn't rewrite that frame (a
//    real hardware detail: skipping an i2c_write leaves that region
//    showing whatever was drawn there last time); this project's model
//    clears via md_beginFrame() every frame, so skipping columns would
//    make the playfield outside the ball's row-band flash black instead
//    of staying visible. Always redrawing everything costs a few hundred
//    more draw calls/frame - trivial against the documented budget.
//  - The intro's ~1.5s real-time descending/ascending tone sweep (510
//    individual blocking Sound() calls in two tight loops) is approximated
//    with a handful of representative tones - this port's md_playTone() is
//    fire-and-forget, not blocking, so replicating a 510-step sweep would
//    need its own per-frame sub-state machine for a purely cosmetic intro
//    flourish; not worth it.
// =============================================================================

#define TP_FPS 60

// -----------------------------------------------------------------------------
//   spritebank.h data (functional bitmap/sprite data, not creative text)
// -----------------------------------------------------------------------------

int[30] tpScreenBallA =
{
0xFF, 0x03, 0x01, 0x01, 0x03, 0xFF, 0xFF, 0x03, 0x19, 0x19, 0x03, 0xFF, 0xFF, 0x03, 0xD9, 0xD9,
0x03, 0xFF, 0xFF, 0x03, 0xD9, 0xD9, 0x03, 0xFF, 0xFF, 0x03, 0xD9, 0xD9, 0x03, 0xFF
};

int[30] tpScreenBallB =
{
0xFF, 0x80, 0x80, 0x80, 0x80, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80, 0xFF, 0xFF, 0x80, 0x80, 0x80,
0x80, 0xFF, 0xFF, 0x80, 0x86, 0x86, 0x80, 0xFF, 0xFF, 0x80, 0xB6, 0xB6, 0x80, 0xFF
};

int[48] tpPusherA =
{
0xFF, 0x01, 0x01, 0x01, 0x01, 0xFF, 0xFF, 0x05, 0x03, 0x05, 0x03, 0xFF, 0xFF, 0x15, 0x0B, 0x15,
0x0B, 0xFF, 0xFF, 0x55, 0x2B, 0x55, 0x2B, 0xFF, 0xFF, 0x55, 0xAB, 0x55, 0xAB, 0xFF, 0xFF, 0x55,
0xAB, 0x55, 0xAB, 0xFF, 0xFF, 0x55, 0xAB, 0x55, 0xAB, 0xFF, 0xFF, 0x55, 0xAB, 0x55, 0xAB, 0xFF
};

int[48] tpPusherB =
{
0xFF, 0xC0, 0x80, 0x80, 0xC0, 0xFF, 0xFF, 0xC0, 0x80, 0x80, 0xC0, 0xFF, 0xFF, 0xC0, 0x80, 0x80,
0xC0, 0xFF, 0xFF, 0xC0, 0x80, 0x80, 0xC0, 0xFF, 0xFF, 0xC1, 0x80, 0x81, 0xC0, 0xFF, 0xFF, 0xC5,
0x82, 0x85, 0xC2, 0xFF, 0xFF, 0xD5, 0x8A, 0x95, 0xCA, 0xFF, 0xFF, 0xD5, 0xAA, 0xD5, 0xEA, 0xFF
};

int[6] tpFlipDetGauche = { 0x30, 0x38, 0x7C, 0x7F, 0x73, 0x60 };
int[6] tpFlipDetDroit  = { 0x0C, 0x1C, 0x3E, 0xFE, 0xCE, 0x06 };

int[18] tpFlipGauche =
{
0x30, 0x18, 0x0C, 0x07, 0x03, 0x00, 0x00, 0x00, 0x78, 0x0F, 0x03, 0x00, 0x00, 0x00, 0x08, 0x1F,
0x33, 0x60
};

int[18] tpFlipDroite =
{
0x0C, 0x18, 0x30, 0xE0, 0xC0, 0x00, 0x00, 0x00, 0x1E, 0xF0, 0xC0, 0x00, 0x00, 0x00, 0x10, 0xF8,
0xCC, 0x06
};

// Each of these three is [width, height, ...WIDTH*HEIGHT bytes] - the shape
// tpSpeedBlitz() (SPEED_BLITZ) expects.
int[22] tpReady =
{
10, 2,
0xFF, 0x01, 0x7D, 0x45, 0x75, 0x05, 0x45, 0x7D, 0x01, 0xFF, 0xFF, 0x80, 0xAF, 0x89, 0xA9, 0xA9,
0xA9, 0xAF, 0x80, 0xFF
};

// 512 bytes: two 256-byte frames (normal, then "bouncing pusher") of the
// 64x32 (doubled to 128x64) playfield background, selected via tpBouncePush.
int[512] tpStart =
{
0xAB, 0xAB, 0x2B, 0x2B, 0x0B, 0x0B, 0x03, 0x03, 0x83, 0xC3, 0xE3, 0x73, 0x33, 0x3B, 0x3B, 0x3B,
0x3B, 0x13, 0x03, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x3F, 0x3F, 0x1F, 0x1F, 0x0F, 0x0F, 0x0F, 0x07,
0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
0x07, 0x07, 0x07, 0x0F, 0x0F, 0x0F, 0x1F, 0x3F, 0xFF, 0xFF, 0xFF, 0x03, 0xD9, 0xD9, 0x03, 0xFF,
0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x07, 0x03, 0x01, 0x00, 0x00, 0x00, 0x04, 0x06, 0x03,
0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x80, 0xC0, 0xC0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x1E, 0x1E, 0x0C, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x80, 0xB6, 0xB6, 0x80, 0xFF,
0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xE0, 0xC0, 0x80, 0x00, 0x00, 0x00, 0x20, 0x60, 0xC0,
0xC0, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x01, 0x03, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x78, 0x78, 0x30, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x01, 0xAB, 0xAB, 0x01, 0xFF,
0xD5, 0xD5, 0xD4, 0xD4, 0xD0, 0xD0, 0xC0, 0xC0, 0xC1, 0xC3, 0xC7, 0xCE, 0xCC, 0xDC, 0xDC, 0xDC,
0xDC, 0xC8, 0xC0, 0xC0, 0xE0, 0xF0, 0xB8, 0xBC, 0xBC, 0xBC, 0xB8, 0xB8, 0xB0, 0xB0, 0xB0, 0xB0,
0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0,
0x80, 0x80, 0xC0, 0xC0, 0xE0, 0xE0, 0xF0, 0xFC, 0xFF, 0xFF, 0xFF, 0xC0, 0xAA, 0xAA, 0xC0, 0xFF,
0xAB, 0xAB, 0x2B, 0x2B, 0x0B, 0x0B, 0x03, 0x03, 0x83, 0xC3, 0xE3, 0x73, 0x33, 0x3B, 0x3B, 0x3B,
0x3B, 0x13, 0x03, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x3F, 0x3F, 0x1F, 0x1F, 0x0F, 0x0F, 0x0F, 0x07,
0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
0x07, 0x07, 0x07, 0x0F, 0x0F, 0x0F, 0x1F, 0x3F, 0xFF, 0xFF, 0xFF, 0x03, 0xD9, 0xD9, 0x03, 0xFF,
0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x07, 0x03, 0x01, 0x00, 0x00, 0x00, 0x04, 0x06, 0x03,
0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0xC0, 0x60, 0xA0, 0xA0, 0x60, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x33, 0x2D, 0x2D, 0x33, 0x1E,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x80, 0xB6, 0xB6, 0x80, 0xFF,
0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xE0, 0xC0, 0x80, 0x00, 0x00, 0x00, 0x20, 0x60, 0xC0,
0xC0, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x03, 0x06, 0x05, 0x05, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x78, 0xCC, 0xB4, 0xB4, 0xCC, 0x78,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x01, 0xAB, 0xAB, 0x01, 0xFF,
0xD5, 0xD5, 0xD4, 0xD4, 0xD0, 0xD0, 0xC0, 0xC0, 0xC1, 0xC3, 0xC7, 0xCE, 0xCC, 0xDC, 0xDC, 0xDC,
0xDC, 0xC8, 0xC0, 0xC0, 0xE0, 0xF0, 0xB8, 0xBC, 0xBC, 0xBC, 0xB8, 0xB8, 0xB0, 0xB0, 0xB0, 0xB0,
0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0,
0x80, 0x80, 0xC0, 0xC0, 0xE0, 0xE0, 0xF0, 0xFC, 0xFF, 0xFF, 0xFF, 0xC0, 0xAA, 0xAA, 0xC0, 0xFF
};

int[262] tpIntro =
{
65, 4,
0x00, 0xFC, 0xFE, 0x1E, 0x0E, 0xEE, 0x2E, 0xEE, 0x6E, 0xAE, 0x6E, 0xEE, 0xEE, 0xEE, 0x0E, 0xFE,
0xFE, 0xFE, 0x06, 0xFA, 0x9C, 0x0C, 0x0C, 0x08, 0x98, 0xF2, 0x02, 0x06, 0xFE, 0xFE, 0xFE, 0xFE,
0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x0E, 0xFE, 0x3E, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xDE, 0xDE, 0x1E,
0xDE, 0xDE, 0xDE, 0x1E, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x7E, 0xFE, 0xFC,
0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x4E, 0x6D, 0x6E, 0xEF, 0x44, 0xEF, 0xEF, 0xFF, 0x00,
0xFF, 0xFF, 0xFF, 0xFE, 0xF8, 0xC7, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x3F, 0xFF, 0xFF,
0x7F, 0x3F, 0x1F, 0x1F, 0x0F, 0x0F, 0x08, 0x0F, 0x10, 0x1F, 0x20, 0x7F, 0x83, 0xFF, 0xFF, 0xFF,
0xAB, 0xAA, 0xAA, 0xAA, 0xCB, 0xFF, 0xFF, 0xFF, 0x8D, 0xDD, 0xDD, 0xDD, 0xCD, 0xFD, 0xD0, 0xFF,
0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xE8, 0xED, 0xEC, 0xED, 0x4C, 0xFF, 0xFF, 0xFF,
0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xE1, 0x1F, 0xFF, 0xFF, 0xFF, 0xC0, 0x00, 0x3F,
0xC1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC0, 0x30, 0x00, 0xC1, 0xFF, 0xFF,
0xFF, 0x8C, 0xAA, 0x88, 0xBA, 0xCC, 0xFF, 0xFF, 0x9F, 0xB6, 0xB6, 0x36, 0x56, 0x58, 0xEF, 0xFF,
0xFF, 0xFF, 0x00, 0x00, 0x3F, 0x7F, 0x60, 0x60, 0x67, 0x64, 0x66, 0x66, 0x66, 0x64, 0x66, 0x66,
0x67, 0x70, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x78, 0x77, 0x6F, 0x67, 0x60,
0x70, 0x7F, 0x7F, 0x7E, 0x7C, 0x7C, 0x78, 0x78, 0x78, 0x79, 0x7C, 0x7C, 0x7E, 0x7F, 0x7F, 0x7F,
0x7F, 0x7F, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7E, 0x7E,
0x7F, 0x7F, 0x3F, 0x00
};

int[80] tpGameOver =
{
19, 4,
0xFF, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x7D, 0x45, 0x75, 0x05, 0x45,
0x7D, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x60, 0x90, 0x90, 0x90, 0x90, 0x60, 0x00, 0x00, 0x29, 0x29,
0x2F, 0xA9, 0xA9, 0x46, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xE4, 0x2A, 0x2A, 0x6A, 0x2A, 0xEA, 0x00,
0x00, 0x3A, 0x0A, 0x0A, 0x1A, 0x0A, 0x39, 0x00, 0xFF, 0xFF, 0x80, 0x80, 0x92, 0x8A, 0x86, 0x8E,
0x92, 0x8E, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xFF
};

// -----------------------------------------------------------------------------
//   ELECTROLIB.h - only SPEED_BLITZ is actually used by this game
// -----------------------------------------------------------------------------

int tpSpeedBlitz( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[ 0 ];
    int hSprite = sprites[ 1 ];
    if( xPass > xPos + ( wSprite - 1 ) || xPass < xPos || yPass < yPos || yPass > yPos + ( hSprite - 1 ) )
      return 0x00;
    return sprites[ 2 + ( xPass - xPos ) + ( yPass - yPos ) * wSprite + frame * ( hSprite * wSprite ) ];
}

// -----------------------------------------------------------------------------
//   Ball state
// -----------------------------------------------------------------------------

struct TpBall
{
    float x, y;
    float Speedx, Speedy;
    float SIMx, SIMy;
    float SIMSpeedx, SIMSpeedy;
};

TpBall tpBallInstance;
TpBall* tpBall = &tpBallInstance;

#define TP_SPRINGLONG 72

int tpTotalBall = 4;
int tpTotalPush = 0;
int tpFirstTime = 0;
int tpBouncePush = 0;
int tpFrameCount = 0;
int tpSpringBar = TP_SPRINGLONG;
int tpTrigFlipG = 0;
int tpTrigFlipD = 0;

// Upstream shares one input (down-or-fire) for both the right flipper and
// charging the launch spring - split into three independent d-pad inputs
// here instead, see this file's header comment.
// Up/Down also alias the left/right flippers (added later, per user
// request) alongside Left/Right - Down still charges the launch spring
// too (tpLaunchHeld() below), so holding Down both charges the spring and
// flips the right flipper at the same time.
bool tpLeftFlipHeld() { return isLeftPressed() || isUpPressed(); }
bool tpRightFlipHeld() { return isRightPressed() || isDownPressed(); }
bool tpLaunchHeld() { return isDownPressed(); }
bool tpAnyInput() { return isLeftPressed() || isRightPressed() || isDownPressed() || isFirePressed(); }

// -----------------------------------------------------------------------------
//   Physics
// -----------------------------------------------------------------------------

void tpTrimXY( TpBall* b )
{
    if( b->Speedx > 1 ) b->Speedx = 1;
    if( b->Speedx < -1 ) b->Speedx = -1;
    if( b->Speedy > 1 ) b->Speedy = 1;
    if( b->Speedy < -1 ) b->Speedy = -1;
}

void tpSimulateMove( TpBall* b )
{
    tpTrimXY( b );
    if( b->SIMx >= 0 )
    {
        if( b->SIMSpeedx > -1 )
          if( b->SIMSpeedx - 0.05 > -1 )
            b->SIMSpeedx = b->SIMSpeedx - 0.05;
        b->SIMx = b->SIMx + b->SIMSpeedx;
    }
    b->SIMy = b->SIMy + b->SIMSpeedy;
}

void tpSimulateRebounce( int sim, TpBall* b )
{
    b->SIMx = b->x;
    b->SIMy = b->y;
    if( sim == 0 ) { b->SIMSpeedx = b->Speedx; b->SIMSpeedy = b->Speedy; }
    else if( sim == 1 ) { b->SIMSpeedx = -b->Speedx; b->SIMSpeedy = b->Speedy; }
    else if( sim == 2 ) { b->SIMSpeedx = b->Speedx; b->SIMSpeedy = -b->Speedy; }
    else if( sim == 3 ) { b->SIMSpeedx = -b->Speedx; b->SIMSpeedy = -b->Speedy; }
    else if( sim == 4 ) { b->SIMSpeedx = -b->Speedy; b->SIMSpeedy = -b->Speedx; }
    else if( sim == 5 ) { b->SIMx = b->x + 1; b->SIMy = b->y; b->SIMSpeedx = -0.2; b->SIMSpeedy = 0.2; }
    else if( sim == 6 ) { b->SIMx = b->x + 1; b->SIMy = b->y; b->SIMSpeedx = -0.2; b->SIMSpeedy = -0.2; }
}

int tpPixelAsign( int value )
{
    while( value >= 8 )
      value = value - 8;
    if( value == 0 ) return 0x01;
    if( value == 1 ) return 0x02;
    if( value == 2 ) return 0x04;
    if( value == 3 ) return 0x08;
    if( value == 4 ) return 0x10;
    if( value == 5 ) return 0x20;
    if( value == 6 ) return 0x40;
    if( value == 7 ) return 0x80;
    return 0x00;
}

int tpColisionCheck( float x, float y, TpBall* b )
{
    tpTrimXY( b );
    int X = (int)round( x );
    int Y = (int)round( y );
    int verticalStrip = Y / 8;
    int serialCount = verticalStrip * 64 + X;
    bool back = ( tpStart[ serialCount ] & tpPixelAsign( Y ) ) != 0x00;

    if( X >= 4 && X <= 9 && Y >= 11 && Y <= 14 )
    {
        if( tpTrigFlipG == 0 )
        {
            if( ( ( tpFlipGauche[ X - 4 ] ) & tpPixelAsign( Y ) ) != 0x00 || back )
              return 1;
            return 0;
        }
        else if( ( tpFlipDetGauche[ X - 4 ] & tpPixelAsign( Y ) ) != 0x00 || back )
        {
            if( tpTrigFlipG != 3 ) { b->SIMSpeedx = 2; md_playTone( 20.0, 0.12 ); }
            else { b->SIMSpeedx = 1; md_playTone( 1.0, 0.06 ); }
            return 0;
        }
        return 0;
    }
    else if( X >= 4 && X <= 9 && Y >= 17 && Y <= 20 )
    {
        if( tpTrigFlipD == 0 )
        {
            if( ( ( tpFlipDroite[ X - 4 ] ) & tpPixelAsign( Y ) ) != 0x00 || back )
              return 1;
            return 0;
        }
        else if( ( tpFlipDetDroit[ X - 4 ] & tpPixelAsign( Y ) ) != 0x00 || back )
        {
            if( tpTrigFlipD != 3 ) { b->SIMSpeedx = 2; md_playTone( 20.0, 0.12 ); }
            else { b->SIMSpeedx = 1; md_playTone( 1.0, 0.06 ); }
            return 0;
        }
        return 0;
    }
    else
    {
        if( X + 32 < tpSpringBar && b->y == 30 )
          return 1;
        if( ( tpStart[ serialCount ] & tpPixelAsign( Y ) ) != 0x00 )
          return 1;
        return 0;
    }
}

void tpTrimBallOnSpring( TpBall* b )
{
    int counter = 0;
    if( b->SIMx + 32 < tpSpringBar - 1 )
    {
        while( b->SIMx + 32 < tpSpringBar - 1 )
        {
            b->SIMx++;
            counter++;
        }
        if( counter > 4 )
          b->SIMSpeedx = 2;
    }
}

int tpCheckColisionType( TpBall* b )
{
    tpTrimXY( b );
    b->SIMx = b->x;
    b->SIMy = b->y;
    b->SIMSpeedx = b->Speedx;
    b->SIMSpeedy = b->Speedy;

    if( b->SIMy == 30 && b->SIMx <= 79 - 32 )
    {
        if( !tpColisionCheck( b->SIMx, b->SIMy, b ) )
        {
            tpSimulateMove( b );
            return 0;
        }
        else
        {
            tpTrimBallOnSpring( b );
            if( b->SIMSpeedx <= 0 ) b->SIMSpeedx = -b->SIMSpeedx;
            tpSimulateMove( b );
            return 0;
        }
    }

    int sim = 0;
    while( true )
    {
        tpSimulateRebounce( sim, b );
        tpSimulateMove( b );
        if( !tpColisionCheck( b->SIMx, b->SIMy, b ) )
          return 0;
        sim++;
        if( sim == 7 ) sim = 0;
    }
}

void tpWriteMove( TpBall* b )
{
    b->x = b->SIMx;
    b->y = b->SIMy;
    b->Speedx = b->SIMSpeedx;
    b->Speedy = b->SIMSpeedy;
}

void tpWriteMoveBounce( TpBall* b )
{
    b->x = b->SIMx;
    b->y = b->SIMy;
    b->SIMSpeedx = b->SIMSpeedx * 8;
    b->SIMSpeedy = b->SIMSpeedy * 8;
    if( b->SIMSpeedx > 1.4 ) b->SIMSpeedx = 1.4;
    if( b->SIMSpeedx < -1.4 ) b->SIMSpeedx = -1.4;
    if( b->SIMSpeedy > 1.4 ) b->SIMSpeedy = 1.4;
    if( b->SIMSpeedy < -1.4 ) b->SIMSpeedy = -1.4;
    b->Speedx = b->SIMSpeedx;
    b->Speedy = b->SIMSpeedy;
}

// Small non-blocking multi-note SFX player, same shape as
// gameTinyPacman.c's/gameTinyBomber.c's/gameTinyInvaders.c's own -
// upstream's real Sound(freq,dur) calls genuinely block on real hardware,
// but md_playTone() has no queue: a burst of N calls with no real time
// between them is only ever audible as the very last one. Declared here,
// ahead of its first call site (tpFalseBall() below), since this dialect
// requires definition before use.
#define TP_SFX_MAX_NOTES 13
float[TP_SFX_MAX_NOTES] tpSfxFreq;
float[TP_SFX_MAX_NOTES] tpSfxDur;
int tpSfxLen;
int tpSfxPos;
int tpSfxWaitFrames;

void tpAdvanceSfx()
{
    if( tpSfxPos >= tpSfxLen )
      return;

    if( tpSfxWaitFrames > 0 )
    {
        tpSfxWaitFrames--;
        return;
    }

    md_playTone( tpSfxFreq[ tpSfxPos ], tpSfxDur[ tpSfxPos ] );

    int waitFrames = (int)( tpSfxDur[ tpSfxPos ] * (float)TP_FPS );
    if( waitFrames < 1 )
      waitFrames = 1;
    tpSfxWaitFrames = waitFrames;

    tpSfxPos++;
}

void tpStartSfx3( float freq0, float dur0, float freq1, float dur1, float freq2, float dur2 )
{
    tpSfxFreq[ 0 ] = freq0; tpSfxDur[ 0 ] = dur0;
    tpSfxFreq[ 1 ] = freq1; tpSfxDur[ 1 ] = dur1;
    tpSfxFreq[ 2 ] = freq2; tpSfxDur[ 2 ] = dur2;
    tpSfxLen = 3;
    tpSfxPos = 0;
    tpSfxWaitFrames = 0;
}

void tpStartSfx5( float freq0, float dur0, float freq1, float dur1, float freq2, float dur2, float freq3, float dur3, float freq4, float dur4 )
{
    tpSfxFreq[ 0 ] = freq0; tpSfxDur[ 0 ] = dur0;
    tpSfxFreq[ 1 ] = freq1; tpSfxDur[ 1 ] = dur1;
    tpSfxFreq[ 2 ] = freq2; tpSfxDur[ 2 ] = dur2;
    tpSfxFreq[ 3 ] = freq3; tpSfxDur[ 3 ] = dur3;
    tpSfxFreq[ 4 ] = freq4; tpSfxDur[ 4 ] = dur4;
    tpSfxLen = 5;
    tpSfxPos = 0;
    tpSfxWaitFrames = 0;
}

// Upstream's game-over buzzer: for(t=0;t<5;t++){Sound(100,100);Sound(1,100);}
void tpStartGameOverBuzzer()
{
    int i;
    for( i = 0; i < 5; i++ )
    {
        tpSfxFreq[ i * 2 ] = 3225.8; tpSfxDur[ i * 2 ] = 0.031;
        tpSfxFreq[ i * 2 + 1 ] = 1968.5; tpSfxDur[ i * 2 + 1 ] = 0.0508;
    }
    tpSfxLen = 10;
    tpSfxPos = 0;
    tpSfxWaitFrames = 0;
}

// Upstream's own "falseBall" sweep: for(t=50;t>0;t--){Sound(t,6);} - 50
// real notes, each only a few microseconds on real hardware. Downsampled
// (stride 4, ~13 notes) rather than reproducing all 50 one-per-real-frame,
// matching the established fix for every other oversized computed sweep
// in this project.
void tpStartFalseBallSweep()
{
    int i;
    int t = 50;
    for( i = 0; i < TP_SFX_MAX_NOTES; i++ )
    {
        if( t < 1 ) t = 1;
        int periodUs = 255 - t;
        if( periodUs < 1 )
          periodUs = 1;
        tpSfxFreq[ i ] = 500000.0 / (float)periodUs;
        tpSfxDur[ i ] = (float)( 6 * 2 * periodUs ) / 1000000.0;
        t = t - 4;
    }
    tpSfxLen = TP_SFX_MAX_NOTES;
    tpSfxPos = 0;
    tpSfxWaitFrames = 0;
}

// Upstream's own bonus-ball-slot sweep:
// for(ttt=60;ttt<240;ttt+=20){Sound(ttt,6);} - a real, already-modest
// 9-note sweep, reproduced in full (no downsampling needed).
void tpStartBonusSweep()
{
    int i;
    int ttt = 60;
    for( i = 0; i < 9; i++ )
    {
        int periodUs = 255 - ttt;
        if( periodUs < 1 )
          periodUs = 1;
        tpSfxFreq[ i ] = 500000.0 / (float)periodUs;
        tpSfxDur[ i ] = (float)( 6 * 2 * periodUs ) / 1000000.0;
        ttt = ttt + 20;
    }
    tpSfxLen = 9;
    tpSfxPos = 0;
    tpSfxWaitFrames = 0;
}

void tpFalseBall()
{
    tpStartFalseBallSweep();
}

void tpBallUpdate( TpBall* b )
{
    tpSimulateMove( b );
    if( tpColisionCheck( b->SIMx, b->SIMy, b ) )
    {
        tpCheckColisionType( b );
        if( b->y < 7 || b->y > 24 || b->x < 31 || b->x > 48 )
        {
            tpWriteMove( b );
            if( b->y < 29 ) md_playTone( 1.0, 0.06 );
        }
        else
        {
            tpWriteMoveBounce( b );
            tpBouncePush = 256;
            if( tpTotalPush < 7 )
              tpTotalPush++;
            else
            {
                if( tpTotalBall < 4 ) tpTotalBall++;
                tpTotalPush = 0;
                tpStartBonusSweep();
            }
        }
        if( b->SIMSpeedy >= 0.15 ) b->SIMSpeedy = b->SIMSpeedy - 0.1;
        if( b->SIMSpeedy <= -0.15 ) b->SIMSpeedy = b->SIMSpeedy + 0.1;
    }
    else
    {
        tpWriteMove( b );
    }

    if( b->y > 29 && b->x > 22 )
      b->y = 30;

    if( round( b->x ) >= 49 && round( b->y ) == 30 )
    {
        b->x = 50;
        b->y = 29;
        b->Speedx = 0.8 - arand( 10 ) / 100.0;
        b->Speedy = -1.0 + arand( 30 ) / 100.0;
    }
}

// -----------------------------------------------------------------------------
//   Rendering
// -----------------------------------------------------------------------------

int tpRecupIntroPic( int xPass, int yPass )
{
    if( xPass < 31 ) return 0xFF;
    if( xPass > 95 ) return 0xFF;
    if( yPass < 2 ) return 0xFF;
    if( yPass > 5 ) return 0xFF;
    return tpSpeedBlitz( 31, 2, xPass, yPass, 0, tpIntro );
}

void tpPicDraw( int pic )
{
    md_beginFrame();
    int y, x;
    for( y = 0; y < 8; y++ )
    {
        for( x = 0; x < 128; x++ )
        {
            int pixel = 0;
            if( pic == 0 ) pixel = tpRecupIntroPic( x, y );
            else if( pic == 1 ) pixel = tpSpeedBlitz( 59, 3, x, y, 0, tpReady );
            else if( pic == 2 ) pixel = tpSpeedBlitz( 54, 2, x, y, 0, tpGameOver );
            md_drawColumn( x, y, pixel );
        }
    }
}

int tpRecupeScreen( int nn, int mm )
{
    int screenCount = nn - 90;
    if( mm == 4 ) return tpScreenBallA[ screenCount + tpTotalBall * 6 ];
    if( mm == 5 ) return tpScreenBallB[ screenCount + tpTotalBall * 6 ];
    if( mm == 6 ) return tpPusherA[ screenCount + tpTotalPush * 6 ];
    if( mm == 7 ) return tpPusherB[ screenCount + tpTotalPush * 6 ];
    return 0x00;
}

int tpRecupeByte( int x, int y )
{
    return tpStart[ ( x - 32 ) + ( y - 4 ) * 64 + tpBouncePush ];
}

int tpRecupeFlip( int x, int y )
{
    int trigG = tpTrigFlipG; if( trigG > 2 ) trigG = 2;
    int trigD = tpTrigFlipD; if( trigD > 2 ) trigD = 2;
    if( y == 5 && x > 35 && x < 42 )
      return tpFlipGauche[ ( x - 36 ) + trigG * 6 ];
    else if( y == 6 && x > 35 && x < 42 )
      return tpFlipDroite[ ( x - 36 ) + trigD * 6 ];
    return 0x00;
}

int tpRecupeSpring( int x, int y )
{
    if( y == 7 && x > 53 && x < tpSpringBar )
      return 0x40;
    return 0x00;
}

int tpPixelConvert( int horiz, int verti, TpBall* b )
{
    int ballx = (int)round( b->x ) + 32;
    int bally = (int)round( b->y ) + 32;
    if( horiz > 31 && horiz < 97 && verti > 3 )
    {
        if( bally / 8 == verti && ballx == horiz )
          return tpPixelAsign( bally );
        return 0x00;
    }
    return 0x00;
}

int tpSliceByte( int vertical, int byteIn )
{
    int toto = 0;
    if( ( vertical % 2 ) != 0 )
    {
        if( ( byteIn & 0x80 ) != 0 ) toto = toto | 0xC0;
        if( ( byteIn & 0x40 ) != 0 ) toto = toto | 0x30;
        if( ( byteIn & 0x20 ) != 0 ) toto = toto | 0x0C;
        if( ( byteIn & 0x10 ) != 0 ) toto = toto | 0x03;
    }
    else
    {
        if( ( byteIn & 0x08 ) != 0 ) toto = toto | 0xC0;
        if( ( byteIn & 0x04 ) != 0 ) toto = toto | 0x30;
        if( ( byteIn & 0x02 ) != 0 ) toto = toto | 0x0C;
        if( ( byteIn & 0x01 ) != 0 ) toto = toto | 0x03;
    }
    return toto;
}

void tpTinyFlip2( TpBall* b )
{
    md_beginFrame();
    int x, y, n, mem, m;
    for( y = 0; y <= 7; y++ )
    {
        m = ( y / 2 ) + 4;
        for( x = 0; x < 64; x++ )
        {
            n = x + 32;
            if( n < 90 )
              mem = tpSliceByte( y, tpPixelConvert( n, m, b ) | tpRecupeByte( n, m ) | tpRecupeFlip( n, m ) | tpRecupeSpring( n, m ) );
            else
              mem = tpSliceByte( y, tpRecupeScreen( n, m ) );
            md_drawColumn( 2 * x, y, mem );
            md_drawColumn( 2 * x + 1, y, mem );
        }
    }

    if( tpBouncePush == 256 )
    {
        tpFrameCount++;
        if( tpFrameCount > 1 )
        {
            // Upstream: Sound(1,20);Sound(20,20);Sound(1,20); - real
            // freq/dur, non-blocking sequenced instead of a synchronous
            // burst.
            tpStartSfx3( 1968.5, 0.0102, 2127.7, 0.0094, 1968.5, 0.0102 );
            tpBouncePush = 0;
            tpFrameCount = 0;
        }
    }
    tpFirstTime = 0;
}

// -----------------------------------------------------------------------------
//   Top-level dispatch (replaces loop()'s NEWGAME:/start: goto-chain)
// -----------------------------------------------------------------------------

#define TP_STATE_TITLE    0
#define TP_STATE_READY    1
#define TP_STATE_PLAYING  2
#define TP_STATE_GAMEOVER 3

int tpState;
int tpWaitFrames;
int tpTitleSettleFrames;

// Upstream never had a real timing model (a flat delay(3) negligible next
// to render cost, not a deliberate FPS - see CLAUDE.md's frame-pacing
// survey), so there's no genuine original rate to reproduce; only the
// ball/flipper/spring physics step is gated to run every
// TP_MOVE_DIVISOR real frames (the same decouple-logic-from-redraw
// approach already used for Tiny Arkanoid), while tpTinyFlip2() keeps
// rendering every real frame for smooth visuals.
#define TP_MOVE_DIVISOR 2
int tpMoveTickCounter = 0;

void tpInitBall()
{
    tpBall->x = 46.0;
    tpBall->y = 30.0;
    tpBall->Speedx = 0;
    tpBall->Speedy = 0;
    tpBall->SIMx = 46.0;
    tpBall->SIMy = 30.0;
    tpBall->SIMSpeedx = 0;
    tpBall->SIMSpeedy = 0;
}

void tpBeginTitle()
{
    tpState = TP_STATE_TITLE;
    tpTotalPush = 0;
    tpTotalBall = 5;
    tpPicDraw( 0 );
    // Approximation of upstream's real-time tone sweep - see file header.
    // Now properly sequenced non-blocking instead of a synchronous burst
    // (md_playTone() has no queue - all 5 calls used to collapse to just
    // the last one).
    tpStartSfx5( 900.0, 0.15, 600.0, 0.15, 300.0, 0.15, 600.0, 0.15, 900.0, 0.15 );
    tpTitleSettleFrames = TP_FPS * 3 / 2;
}

void tpBeginReady()
{
    tpState = TP_STATE_READY;
    tpPicDraw( 1 );
    tpFirstTime = 1;
    tpWaitFrames = TP_FPS; // ~1000ms
}

void tpBeginPlaying()
{
    // Upstream's "start:" label (reached here, once, via the READY screen's
    // break out of its title-wait loop) does `if (totalBall!=0) totalBall--`
    // before the very first ball - matching the same decrement the "ball
    // lost, balls remain" path below does directly. Missing this left
    // tpTotalBall at 5 during the first ball - one past the 0-4 range the
    // ball-count sprite arrays (tpScreenBallA/B, 5 frames each) index into,
    // so the first ball rendered a garbage/out-of-bounds frame instead of
    // "4 balls left", only correcting itself once losing that first ball
    // hit the (correct) decrement in the other path.
    tpTotalBall--;
    tpState = TP_STATE_PLAYING;
    tpInitBall();
}

void tpBeginGameOver()
{
    tpState = TP_STATE_GAMEOVER;
    tpPicDraw( 2 );
    tpStartGameOverBuzzer();
    tpWaitFrames = TP_FPS * 13 / 10; // ~300ms pre-wait folded in + ~1000ms
}

void gameTinyPinball_init()
{
    InitTinyJoypad();
    tpTotalBall = 4;
    tpTotalPush = 0;
    tpFirstTime = 0;
    tpBouncePush = 0;
    tpFrameCount = 0;
    tpSpringBar = TP_SPRINGLONG;
    tpTrigFlipG = 0;
    tpTrigFlipD = 0;
    tpBeginTitle();
}

void gameTinyPinball_update()
{
    tpAdvanceSfx();

    if( tpState == TP_STATE_TITLE )
    {
        if( tpTitleSettleFrames > 0 )
        {
            tpTitleSettleFrames--;
            return;
        }
        if( tpAnyInput() )
        {
            tpBeginReady();
        }
        return;
    }

    if( tpState == TP_STATE_READY )
    {
        tpWaitFrames--;
        if( tpWaitFrames <= 0 )
          tpBeginPlaying();
        return;
    }

    if( tpState == TP_STATE_GAMEOVER )
    {
        tpWaitFrames--;
        if( tpWaitFrames <= 0 )
          tpBeginTitle();
        return;
    }

    // TP_STATE_PLAYING
    tpMoveTickCounter++;
    bool tpDoMoveTick = ( tpMoveTickCounter >= TP_MOVE_DIVISOR );
    if( tpDoMoveTick )
    {
        tpMoveTickCounter = 0;
        tpBallUpdate( tpBall );
    }
    tpTinyFlip2( tpBall );

    if( tpBall->x < 0.5 )
    {
        tpFalseBall();
        tpFirstTime = 1;
        if( tpTotalBall != 0 )
        {
            tpTotalBall--;
            tpInitBall();
        }
        else
        {
            tpBeginGameOver();
        }
        return;
    }

    if( !tpDoMoveTick )
      return;

    if( tpBall->y >= 29 && tpBall->x >= 18 )
    {
        if( tpLaunchHeld() )
        {
            if( tpSpringBar > 54 ) tpSpringBar = tpSpringBar - 2;
        }
        else
        {
            if( tpSpringBar > TP_SPRINGLONG - 10 && tpSpringBar < TP_SPRINGLONG )
              tpSpringBar = TP_SPRINGLONG;
            if( tpSpringBar < TP_SPRINGLONG )
              tpSpringBar = tpSpringBar + 8;
        }
    }
    else
    {
        tpSpringBar = TP_SPRINGLONG;
    }

    if( tpLeftFlipHeld() ) { if( tpTrigFlipG < 3 ) tpTrigFlipG++; }
    else { if( tpTrigFlipG > 0 ) tpTrigFlipG--; }

    if( tpRightFlipHeld() ) { if( tpTrigFlipD < 3 ) tpTrigFlipD++; }
    else { if( tpTrigFlipD > 0 ) tpTrigFlipD--; }
}

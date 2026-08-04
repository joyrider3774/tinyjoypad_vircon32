// =============================================================================
// Tiny Arena - ported from Daniel C's TinyArena.ino (tinyjoypad.com,
// GPLv3). Same tinyJoypadShim lineage as every other Daniel-C game here
// (FastTinyDriver.h) - Sound()/isXPressed() reuse the existing shim as-is.
//
// A DOOM-style raycaster arena shooter: rotate/move with the d-pad, shoot
// tentacle enemies with Fire before they reach you, survive as long as
// possible. This project's very first raycaster port - picked specifically
// to prove the technique out on a plain (non-C++-class) game before
// attempting the much bigger TinyDungeon (a C++-class raycaster with
// combat/dice/inventory on top), which had been deferred as out of scope.
// If this port holds up in play, TinyDungeon is now meaningfully de-risked
// for a dedicated future session instead of being permanently shelved.
//
// Button mapping (matches every other Daniel-C game's own established A0/A3
// thresholds exactly, confirmed directly against this game's own
// ELECTROLIB.h rather than assumed):
//   analogRead(A0) in [750,950) = isLeftPressed()  (TINYJOYPAD_LEFT)
//   analogRead(A0) in (500,750) = isRightPressed() (TINYJOYPAD_RIGHT)
//   analogRead(A3) in [750,950) = isDownPressed()  (TINYJOYPAD_DOWN)
//   analogRead(A3) in (500,750) = isUpPressed()    (TINYJOYPAD_UP)
//   digitalRead(1)==0 (active low) = isFirePressed() (BUTTON_DOWN)
//   digitalRead(1)==1              = !isFirePressed() (BUTTON_UP)
//
// Key architectural finding: despite being a raycaster, this game needs no
// new machineDependent primitives at all. Upstream never writes real
// pixels - it renders into a *half-resolution* 64x32 monochrome buffer
// (`VBuffer[4][64]`, 4 hardware-page-equivalents tall) and its own
// Tiny_Flip() doubles that buffer 2x both ways to fill the real 128x64
// display: horizontally via writing each output byte twice
// (`i2c_write(out);i2c_write(out);`), and vertically by expanding each
// half-res byte's relevant nibble into a full byte via a lookup table
// (`SliceByte`) before then *also* writing it out twice more (once per real
// page derived from one half-res row). The exact same "compute a byte value
// per real (column,page), call md_drawColumn()" model already used by
// every other port here covers this without changes - `arTinyFlip()` below
// just derives that byte differently (via the half-res buffer + nibble
// expansion) instead of reading a pre-baked column-atlas byte.
//
// Structural changes from upstream:
//  - Vircon32 has no `static` local variables (confirmed unsupported in
//    VIRCON32_C_DIALECT.md) - the two upstream statics (`static uint8_t
//    val` in the game-over blink loop, `static int prevHitX/prevHitY` in
//    the raycast loop) become ordinary file-scope globals instead.
//  - `val`'s own `val += 2` relies on `uint8_t` wraparound (0-255) to
//    drive the blink cycle - this project's own established "byte
//    wraparound needs an explicit mask now" lesson applies here exactly
//    like it did for HollowSeeker's cave-phase counter - wrapped
//    explicitly with `& 0xFF`.
//  - upstream's own ternary-operator uses (`SkipAnim ? ... : ...` in
//    BobPosMove(), and one inside drawSprite2Bit()) rewritten as
//    `if`/`else`, matching the already-established finding (Tiny Bike)
//    that this dialect's compiler doesn't accept `?:` at all.
//  - `fabsf()` isn't declared by this project's math.h (only a single
//    float-only `fabs()`) - every call site uses `fabs()` instead, which
//    is exactly the same operation for this game's already-all-float
//    usage.
//  - loop()'s own body already *is* shaped like a non-blocking per-frame
//    update (Arduino's runtime supplies the outer forever-loop, not a
//    goto-chain like every other Daniel-C game here) - no state-machine
//    restructuring needed for ordinary gameplay. The only two genuinely
//    blocking constructs are the health-reaches-zero screen's own nested
//    `while(1)` loops (wait for the confirming button to release, then
//    blink a "START" prompt until the player presses Fire again) -
//    converted to two explicit states (`AR_STATE_GAMEOVER_WAIT_RELEASE`/
//    `AR_STATE_GAMEOVER_BLINK`) the same way every other port's blocking
//    waits have been.
//  - Upstream's own design doubles as its "attract screen" for free:
//    `playerHealth` starts at 0 (global default) and `setup()` never calls
//    `INIT_NEW_GAME()`, so the very first real frame already satisfies
//    `playerHealth==0` and falls straight into the same game-over/blink
//    states a real death would - ported as-is rather than adding a
//    separate attract state, matching upstream's own actual behavior.
//  - Data tables extracted from the .ino directly with a small Python
//    script (not hand-transcribed), verifying each table's real element
//    count against the script's own parse - the usual anti-Bomber-bug
//    technique, extended here to also handle a 2D PROGMEM array (the
//    9x9 level map) and PROGMEM float arrays (enemy respawn positions).
//  - A genuine upstream off-by-one, found by inspection while porting
//    (not by a crash): `isWall()`, the DDA loop's out-of-map check, and
//    the enemy-respawn validity check all bounds-check against `>= 10`,
//    but `Lvl1` is declared `[9][9]` (valid indices 0-8) - on real AVR
//    hardware this silently reads one PROGMEM byte past the array at
//    x==9/y==9 (harmless garbage from flash, no crash possible), but
//    Vircon32 has no such forgiving out-of-bounds behavior for a real
//    array read - fixed by using the array's actual bound (9) at all
//    three sites rather than porting the bug verbatim.
// =============================================================================

int[58] arGun =
{
14,15,0,0,0,0,0,128,224,192,152,164,170,36,152,0,64,32,80,40,
84,39,95,95,47,15,7,3,5,0,0,0,0,0,192,112,24,60,102,91,
85,219,102,252,48,88,44,86,43,88,32,32,80,112,24,12,10,7,
};

int[70] arFace =
{
17,16,0,206,244,56,124,252,196,144,168,144,196,168,80,32,132,14,0,0,
3,15,30,56,51,96,121,99,121,96,51,56,20,10,0,0,223,49,11,198,
130,2,59,111,87,111,59,86,174,222,123,241,223,3,12,48,33,71,76,159,
134,156,134,159,76,71,43,53,15,3,
};

int[26] arStart =
{
70,73,73,49,0,1,1,127,1,1,0,126,9,9,126,0,127,25,41,70,
0,1,1,127,1,1,
};

int[40] arBOB =
{
5,2,6,1,7,0,8,0,9,1,10,2,9,3,8,4,7,4,6,3,
5,2,4,1,3,0,2,0,1,1,0,2,1,3,2,4,3,4,4,3,
};

int[9][9] arLvl1 =
{
{1,1,1,1,1,1,1,1,1},
{1,0,0,0,0,0,0,0,0},
{1,0,1,1,1,1,1,1,0},
{1,0,0,0,0,1,0,0,0},
{1,1,1,1,0,1,0,1,0},
{1,0,0,0,0,1,0,0,0},
{1,0,0,1,0,1,1,1,0},
{1,1,1,1,0,0,0,0,0},
{1,0,0,0,0,1,1,0,0},
};

float[3] arSpawnX = { 1.0, 6.0, 7.0 };
float[3] arSpawnY = { 6.0, 5.0, 1.0 };

// SliceByte's own nibble-expansion table - a `static const` local upstream,
// hoisted to a real global here (no static locals in this dialect).
// Values shown in binary in the header comment for readability - this
// dialect's compiler doesn't accept 0b literals (see Tiny Bike's own
// finding).
int[16] arExpand =
{
0, 3, 12, 15,       // 0b00000000, 0b00000011, 0b00001100, 0b00001111
48, 51, 60, 63,     // 0b00110000, 0b00110011, 0b00111100, 0b00111111
192, 195, 204, 207, // 0b11000000, 0b11000011, 0b11001100, 0b11001111
240, 243, 252, 255  // 0b11110000, 0b11110011, 0b11111100, 0b11111111
};

// -----------------------------------------------------------------------------
//   State
// -----------------------------------------------------------------------------

#define AR_NUM_SPRITES 3

struct ArWorldSprite
{
    float x;
    float y;
    int active;
    int health;
};

ArWorldSprite[3] arWorldSprites;

struct ArSpriteOrder
{
    float dist;
    int index;
};

ArSpriteOrder[3] arOrder;

int arBobPos;
int arSkipAnim;
int[4][64] arVBuffer;

int arBulletActive;
int arBulletSize;
int arBulletX;
int arBulletY;

float arPosX, arPosY;
float arDirX, arDirY;
float arPlaneX, arPlaneY;
#define AR_PLAYER_SIZE 0.08

int[64] arWallDist;

int arPlayerHealth;
int arDamageCooldown;
int arKillCount;

// Was `static int prevHitX = -1, prevHitY = -1;` inside renderRaycast().
int arPrevHitX;
int arPrevHitY;

// Was `static uint8_t val = 0;` inside loop()'s game-over blink loop.
int arGameOverVal;

// Frame-stepped replacement for upstream's death-sweep loop (see
// arAdvanceDeathSweep()'s own comment, near arUpdateTentacles()).
int arDeathSweepActive;
int arDeathSweepT;

void arBobPosMove()
{
    if( arSkipAnim ) arSkipAnim--;
    else
    {
        if( arBobPos < 18 ) arBobPos++;
        else arBobPos = 0;
        arSkipAnim = 1;
    }
}

int arIsWall( int x, int y )
{
    if( x < 0 || x >= 9 || y < 0 || y >= 9 ) return 1;
    return arLvl1[ y ][ x ] == 1;
}

int arCheckCollision( float px, float py )
{
    float l = px - AR_PLAYER_SIZE;
    float r = px + AR_PLAYER_SIZE;
    float t = py - AR_PLAYER_SIZE;
    float b = py + AR_PLAYER_SIZE;
    return arIsWall( (int)l, (int)t ) || arIsWall( (int)r, (int)t ) ||
           arIsWall( (int)l, (int)b ) || arIsWall( (int)r, (int)b );
}

void arUpdateTentacles()
{
    if( arDamageCooldown > 0 ) arDamageCooldown--;

    float speed = 0.018 + ( arKillCount * 0.003 );
    if( speed > 0.108 ) speed = 0.108;

    int i;
    for( i = 0; i < AR_NUM_SPRITES; i++ )
    {
        if( !arWorldSprites[ i ].active ) continue;

        float dx = arWorldSprites[ i ].x - arPosX;
        float dy = arWorldSprites[ i ].y - arPosY;
        float distSq = dx * dx + dy * dy;

        if( distSq < 0.09 && arDamageCooldown == 0 && arPlayerHealth > 0 )
        {
            arPlayerHealth--;
            arDamageCooldown = 30;
            Sound( 10, 22 );
            if( arPlayerHealth == 0 )
            {
                arDeathSweepActive = 1;
                arDeathSweepT = 220;
            }
        }

        if( distSq < 256.0 && distSq > 0.01 )
        {
            float newX = arWorldSprites[ i ].x - dx * speed;
            float newY = arWorldSprites[ i ].y - dy * speed;

            int ix = (int)newX;
            int iy = (int)newY;

            if( ix >= 0 && ix < 9 && iy >= 0 && iy < 9 )
            {
                if( arLvl1[ iy ][ ix ] != 1 )
                {
                    arWorldSprites[ i ].x = newX;
                    arWorldSprites[ i ].y = newY;
                }
            }
        }
    }
}

// Upstream's own death sweep ( for(t=220;t>3;t--) Sound(t,2); ) fires all
// ~217 Sound() calls synchronously within a single frame - harmless on
// real AVR hardware (each Sound() call there blocks for a genuine but tiny
// slice of real time, so the whole loop finishes in well under a real
// frame - an almost-instant descending "zap"), but Vircon32's async audio
// channel has no queue: md_playTone() unconditionally stops whatever the
// previous call started before beginning the new tone (see its own
// comment in portVircon32.c), so 217 calls issued back-to-back with no
// real time between them can only ever be *heard* as the very last call's
// tone - the intended descending sweep would be silently reduced to one
// inaudible click, while still costing 217 real playnote_start()/
// stop_all() invocations in a single frame for an effect nobody perceives.
// Same root cause as Tiny Missile's own computed-sweep bug (see its own
// header comment) - spread across real frames instead of one synchronous
// loop, with a downsampled step (30 instead of 1) so the whole sweep still
// finishes in a small, snappy handful of real frames rather than
// stretching upstream's near-instant sweep out to a full ~3.6 real
// seconds (217 frames at one note each, the same "sounds play too long"
// mistake already found and fixed for Tiny Missile).
void arAdvanceDeathSweep()
{
    if( !arDeathSweepActive ) return;

    Sound( arDeathSweepT, 2 );
    arDeathSweepT -= 30;
    if( arDeathSweepT <= 3 )
      arDeathSweepActive = 0;
}

// -----------------------------------------------------------------------------
//   2-bit sprite drawing (Gun/face) - each sprite stores two parallel bit-
//   planes back to back (a "white" plane and a "black" plane); neither bit
//   set means transparent (background shows through), giving real
//   silhouette masking on top of a 1-bit display, same idea as every other
//   port's own separate color+black-mask sprite pair, just packed into one
//   table here instead of two.
// -----------------------------------------------------------------------------

void arDrawSprite2Bit( int x0, int y0, int* sprite )
{
    int w = sprite[ 0 ];
    int h = sprite[ 1 ];
    int whiteBase = 2;
    int blackBase = 2 + w * ( ( h + 7 ) >> 3 );

    if( x0 >= 64 || y0 >= 32 || x0 + w <= 0 || y0 + h <= 0 )
      return;

    int pages = ( h + 7 ) >> 3;
    int x;
    for( x = 0; x < w; x++ )
    {
        int sx = x0 + x;
        if( sx < 0 || sx >= 64 ) continue;
        int p;
        for( p = 0; p < pages; p++ )
        {
            int offset = x + p * w;
            int wb = sprite[ whiteBase + offset ];
            int bb = sprite[ blackBase + offset ];
            if( !wb && !bb ) continue;

            int baseY = p << 3;
            int maxBit;
            if( h - baseY > 8 ) maxBit = 8;
            else maxBit = h - baseY;

            int bit;
            for( bit = 0; bit < maxBit; bit++ )
            {
                int mask = 1 << bit;
                int sy = y0 + baseY + bit;
                if( sy < 0 || sy >= 32 ) continue;
                if( bb & mask )
                  arVBuffer[ sy >> 3 ][ sx ] = arVBuffer[ sy >> 3 ][ sx ] & ~( 1 << ( sy & 7 ) );
                else if( wb & mask )
                  arVBuffer[ sy >> 3 ][ sx ] = arVBuffer[ sy >> 3 ][ sx ] | ( 1 << ( sy & 7 ) );
            }
        }
    }
}

// -----------------------------------------------------------------------------
//   3D sprite drawing (enemies)
// -----------------------------------------------------------------------------

void arDrawWorldSprites()
{
    int numActive = 0;
    int i;
    for( i = 0; i < AR_NUM_SPRITES; i++ )
    {
        if( !arWorldSprites[ i ].active ) continue;
        float dx = arWorldSprites[ i ].x - arPosX;
        float dy = arWorldSprites[ i ].y - arPosY;
        arOrder[ numActive ].dist = dx * dx + dy * dy;
        arOrder[ numActive ].index = i;
        numActive++;
    }

    // Sort sprites by distance (farthest first) - a tiny bubble sort, at
    // most 3 elements, matching upstream exactly.
    int j;
    for( i = 0; i < numActive; i++ )
      for( j = i + 1; j < numActive; j++ )
        if( arOrder[ i ].dist < arOrder[ j ].dist )
        {
            ArSpriteOrder tmp = arOrder[ i ];
            arOrder[ i ] = arOrder[ j ];
            arOrder[ j ] = tmp;
        }

    float det = arPlaneX * arDirY - arDirX * arPlaneY;
    float invDet;
    if( det != 0.0 ) invDet = 1.0 / det;
    else invDet = 100.0;
    if( fabs( invDet ) > 100.0 )
    {
        if( invDet > 0 ) invDet = 100.0;
        else invDet = -100.0;
    }

    int s;
    for( s = 0; s < numActive; s++ )
    {
        i = arOrder[ s ].index;

        float dx = arWorldSprites[ i ].x - arPosX;
        float dy = arWorldSprites[ i ].y - arPosY;

        float transformX = invDet * ( arDirY * dx - arDirX * dy );
        float transformY = invDet * ( -arPlaneY * dx + arPlaneX * dy );

        if( transformY < 0.1 ) continue;

        int screenX = (int)( 32.5 + transformX * ( 32.0 / transformY ) );

        int spriteDist8;
        if( transformY > 5.1 ) spriteDist8 = 255;
        else spriteDist8 = (int)( transformY * 50.0 );

        int sprW = arFace[ 0 ];
        int sprH = arFace[ 1 ];
        float scale = 1.0 / transformY;
        int w = (int)( sprW * scale );
        int h = (int)( sprH * scale );
        if( w < 1 || h < 1 ) continue;

        int drawX1 = max( 0, screenX - w / 2 );
        int drawX2 = min( 64, screenX + w / 2 );
        if( drawX1 >= 64 || drawX2 <= 0 ) continue;

        int drawY1 = max( 0, 16 - h / 2 );
        int drawY2 = min( 32, 16 + h / 2 );

        // `texY` (and everything derived from it: the texture row-byte
        // offset component, page, bit mask) depends only on `y`, `h` and
        // `sprH` - never on `stripe` - but was being recomputed from
        // scratch (including a real division) for every one of up to
        // 64x32=2048 pixels when a close enemy fills most of the screen,
        // exactly the "performance tanks when enemy is very near" report.
        // Precomputed once per row here (at most 32 rows) instead, before
        // the stripe loop, cutting the division count from up to 2048 to
        // at most 32 per sprite per frame.
        int[32] rowValid;
        int[32] rowTexYHigh;
        int[32] rowMask;
        int[32] rowPage;
        int[32] rowBit;
        int y;
        for( y = drawY1; y < drawY2; y++ )
        {
            int idx = y - drawY1;
            int d = ( y - 16 ) * 256 / h + 128;
            int texY = ( d * sprH ) >> 8;
            if( texY < 0 || texY >= sprH )
              rowValid[ idx ] = 0;
            else
            {
                rowValid[ idx ] = 1;
                rowTexYHigh[ idx ] = ( texY >> 3 ) * sprW;
                rowMask[ idx ] = 1 << ( texY & 7 );
                rowPage[ idx ] = y >> 3;
                rowBit[ idx ] = y & 7;
            }
        }

        // When the sprite is scaled up past 1x (scale = 1/transformY > 1,
        // i.e. exactly the "enemy very near" case), many consecutive
        // destination columns (`stripe`) map to the *same* source texture
        // column (`texX`, since it advances by 1 only once every `w/sprW`
        // stripes) - texX is monotonically non-decreasing as stripe
        // increases, so equal values only ever appear in one contiguous
        // run, never revisited later. Caches each run's per-row white/
        // black lookup once (`colDraw`) instead of re-reading `arFace[]`
        // for every stripe in the run - only the actual `arVBuffer` write
        // (genuinely different per destination column) still happens per
        // stripe. At a 4x scale (transformY ~0.25) this cuts the number of
        // texture-array-read passes from up to 64 down to as few as 17
        // (the sprite's own native width).
        int lastTexX = -999;
        int[32] colDraw; // 0 = transparent, 1 = white, 2 = black

        int stripe;
        for( stripe = drawX1; stripe < drawX2; stripe++ )
        {
            if( spriteDist8 >= arWallDist[ stripe ] ) continue;

            int texX = ( ( stripe - ( screenX - w / 2 ) ) * sprW ) / w;

            if( texX != lastTexX )
            {
                lastTexX = texX;
                for( y = drawY1; y < drawY2; y++ )
                {
                    int idx = y - drawY1;
                    if( !rowValid[ idx ] ) { colDraw[ idx ] = 0; continue; }

                    int offset = texX + rowTexYHigh[ idx ];
                    int mask = rowMask[ idx ];
                    int whiteByte = arFace[ 2 + offset ];
                    int blackByte = arFace[ 2 + ( ( sprH + 7 ) >> 3 ) * sprW + offset ];

                    if( blackByte & mask ) colDraw[ idx ] = 2;
                    else if( whiteByte & mask ) colDraw[ idx ] = 1;
                    else colDraw[ idx ] = 0;
                }
            }

            for( y = drawY1; y < drawY2; y++ )
            {
                int idx = y - drawY1;
                if( colDraw[ idx ] == 0 ) continue;

                int page = rowPage[ idx ];
                int bit = rowBit[ idx ];
                if( colDraw[ idx ] == 2 )
                  arVBuffer[ page ][ stripe ] = arVBuffer[ page ][ stripe ] & ~( 1 << bit );
                else
                  arVBuffer[ page ][ stripe ] = arVBuffer[ page ][ stripe ] | ( 1 << bit );
            }
        }
    }
}

// -----------------------------------------------------------------------------
//   Raycasting + rendering
// -----------------------------------------------------------------------------

void arRenderRaycast()
{
    const float moveSpeed = 0.085;

    if( isLeftPressed() || isRightPressed() )
    {
        float ang;
        if( isLeftPressed() ) ang = -0.07;
        else ang = 0.07;

        float s = ang;
        float c = 1.0 - ang * ang * 0.5;

        float tmp = arDirX * c - arDirY * s;
        arDirY = arDirX * s + arDirY * c;
        arDirX = tmp;

        tmp = arPlaneX * c - arPlaneY * s;
        arPlaneY = arPlaneX * s + arPlaneY * c;
        arPlaneX = tmp;
    }

    float moveX = 0, moveY = 0;
    if( isUpPressed() ) { moveX = arDirX * moveSpeed; moveY = arDirY * moveSpeed; }
    if( isDownPressed() ) { moveX = -arDirX * moveSpeed; moveY = -arDirY * moveSpeed; }

    if( moveX != 0 || moveY != 0 )
    {
        int canMoveX = !arCheckCollision( arPosX + moveX, arPosY );
        int canMoveY = !arCheckCollision( arPosX, arPosY + moveY );

        if( canMoveX && canMoveY )
        {
            arPosX += moveX;
            arPosY += moveY;
            arBobPosMove();
        }
        else if( canMoveX )
        {
            arPosX += moveX;
            arBobPosMove();
        }
        else if( canMoveY )
        {
            arPosY += moveY;
            arBobPosMove();
        }
    }

    int x;
    for( x = 0; x < 64; x++ )
    {
        float cameraX = 2.0 * x / 64.0 - 1.0;
        float rayDirX = arDirX + arPlaneX * cameraX;
        float rayDirY = arDirY + arPlaneY * cameraX;

        int mapX = (int)arPosX;
        int mapY = (int)arPosY;

        float deltaDistX = fabs( 1.0 / ( rayDirX + 0.000001 ) );
        float deltaDistY = fabs( 1.0 / ( rayDirY + 0.000001 ) );

        int stepX, stepY;
        if( rayDirX < 0 ) stepX = -1; else stepX = 1;
        if( rayDirY < 0 ) stepY = -1; else stepY = 1;

        float sideDistX, sideDistY;
        if( rayDirX < 0 ) sideDistX = ( arPosX - mapX ) * deltaDistX;
        else sideDistX = ( mapX + 1.0 - arPosX ) * deltaDistX;
        if( rayDirY < 0 ) sideDistY = ( arPosY - mapY ) * deltaDistY;
        else sideDistY = ( mapY + 1.0 - arPosY ) * deltaDistY;

        int side = 0;
        int hitMapX, hitMapY;

        while( 1 )
        {
            if( sideDistX < sideDistY )
            {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            }
            else
            {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }

            if( mapX < 0 || mapX >= 9 || mapY < 0 || mapY >= 9 )
            {
                hitMapX = mapX;
                hitMapY = mapY;
                break;
            }

            if( arLvl1[ mapY ][ mapX ] == 1 )
            {
                hitMapX = mapX;
                hitMapY = mapY;
                break;
            }
        }

        float perpWallDist;
        if( side == 0 )
          perpWallDist = ( mapX - arPosX + ( 1 - stepX ) / 2.0 ) / rayDirX;
        else
          perpWallDist = ( mapY - arPosY + ( 1 - stepY ) / 2.0 ) / rayDirY;

        if( perpWallDist < 0.01 ) perpWallDist = 0.01;

        if( perpWallDist > 5.1 ) arWallDist[ x ] = 255;
        else arWallDist[ x ] = (int)( perpWallDist * 50.0 );

        int lineHeight = (int)( 32.0 / perpWallDist );
        int drawStart = max( 0, 16 - lineHeight / 2 );
        int drawEnd = min( 32, 16 + lineHeight / 2 );

        int newTile = ( hitMapX != arPrevHitX || hitMapY != arPrevHitY );

        // arVBuf(x,y) inlined directly (was a real function call per site) -
        // this loop can fire up to ~50 times/column when a wall fills the
        // whole 32-row height (player standing close to a wall, the direct
        // analogue of the "enemy very near" case already fixed in
        // arDrawWorldSprites() - up to ~3200 calls/frame worst case across
        // all 64 columns), so the per-call overhead (params, stack frame)
        // was worth removing here too, same technique as Tiny Trick's own
        // background-lookup inlining.
        int y;
        if( drawStart < 32 )
          arVBuffer[ drawStart >> 3 ][ x ] &= ~( 1 << ( drawStart & 7 ) );
        if( drawEnd > 0 )
          arVBuffer[ ( drawEnd - 1 ) >> 3 ][ x ] &= ~( 1 << ( ( drawEnd - 1 ) & 7 ) );

        for( y = drawStart; y < drawEnd; y++ )
          if( ( x + y ) & 1 )
            arVBuffer[ y >> 3 ][ x ] &= ~( 1 << ( y & 7 ) );

        if( newTile && x > 0 )
        {
            for( y = drawStart; y < drawEnd; y++ )
              arVBuffer[ y >> 3 ][ x - 1 ] &= ~( 1 << ( y & 7 ) );
        }

        arPrevHitX = hitMapX;
        arPrevHitY = hitMapY;
    }

    arDrawWorldSprites();
}

void arShoot()
{
    Sound( 120, 15 );
    arBulletActive = 5;
    arBulletSize = 12;
    arBulletY = 31;
    arBulletX = 28 + arBOB[ arBobPos << 1 ];
}

// -----------------------------------------------------------------------------
//   Display
// -----------------------------------------------------------------------------

int arSliceByte( int page, int data )
{
    // Central defensive mask - every arVBuffer read funnels through here,
    // so this is the one place that needs to guard against a stray value
    // above 0xFF (matching md_drawColumn()'s own established "fix it once,
    // centrally" precedent) regardless of which call site produced it.
    data = data & 0xFF;
    int nibble;
    if( page & 1 ) nibble = data >> 4;
    else nibble = data & 0x0F;
    return arExpand[ nibble ];
}

void arTinyFlip()
{
    md_beginFrame();
    int p;
    for( p = 0; p < 8; p++ )
    {
        int bufPage = p >> 1;
        int x;
        for( x = 0; x < 64; x++ )
        {
            int out = arSliceByte( p, arVBuffer[ bufPage ][ x ] );
            md_drawColumn( x * 2, p, out );
            md_drawColumn( x * 2 + 1, p, out );
        }
    }
}

// -----------------------------------------------------------------------------
//   State machine (replaces loop()'s two nested game-over while(1) loops -
//   ordinary gameplay needs no restructuring at all, since Arduino's own
//   runtime already called loop() itself in a non-blocking-per-frame shape)
// -----------------------------------------------------------------------------

#define AR_STATE_GAMEOVER_WAIT_RELEASE 0
#define AR_STATE_GAMEOVER_BLINK        1
#define AR_STATE_PLAYING               2

int arState;
int arForceRedraw;

void arInitNewGame()
{
    arBobPos = 0;
    arSkipAnim = 0;

    arBulletActive = 0;
    arBulletSize = 0;
    arBulletX = 32;
    arBulletY = 31;

    arPosX = 4.5;
    arPosY = 6.0;
    arDirX = 0.0;
    arDirY = -1.0;
    arPlaneX = 0.66;
    arPlaneY = 0.0;

    arPlayerHealth = 3;
    arDamageCooldown = 0;

    arKillCount = 0;

    int i;
    for( i = 0; i < AR_NUM_SPRITES; i++ )
    {
        arWorldSprites[ i ].x = arSpawnX[ i ];
        arWorldSprites[ i ].y = arSpawnY[ i ];
        arWorldSprites[ i ].active = 1;
        arWorldSprites[ i ].health = 5;
    }

    arDeathSweepActive = 0;
    arState = AR_STATE_PLAYING;
}

// Upstream's own buffer-clear-to-white loop runs unconditionally at the
// very top of loop() - including the single call that then blocks forever
// inside the game-over screen's nested while(1)s, so that white backdrop
// is set exactly *once* per game-over sequence and never touched again
// until the player restarts (only the small "START" text region keeps
// changing). Ported as its own explicit one-time call rather than folding
// it into the per-tick blink state, matching that real one-time-then-
// frozen behavior instead of a global default (Vircon32 zero-initializes
// globals, not 0xff, so without this the game-over backdrop would render
// as black instead of upstream's intended white).
void arClearBufferWhite()
{
    int y, x;
    for( y = 0; y < 4; y++ )
      for( x = 0; x < 64; x++ )
        arVBuffer[ y ][ x ] = 0xff;
}

void gameTinyArena_init()
{
    InitTinyJoypad();

    // Matches upstream exactly: playerHealth defaults to 0 and setup()
    // never calls INIT_NEW_GAME(), so the very first real frame already
    // falls into the game-over/blink states a real death would - this
    // doubles as the attract screen, no separate state needed.
    arPlayerHealth = 0;
    arWorldSprites[ 0 ].active = 0;
    arWorldSprites[ 1 ].active = 0;
    arWorldSprites[ 2 ].active = 0;
    arGameOverVal = 0;
    arClearBufferWhite();
    arState = AR_STATE_GAMEOVER_WAIT_RELEASE;
}

// Quit-confirmation-dialog resume hook (see menuGameList.c's own comment
// on this pattern) - the attract/game-over-wait screen above has no timer
// of its own and would otherwise leave the dialog's pixels on screen
// indefinitely if the player opens it from there and cancels.
void gameTinyArena_forceRedraw()
{
    arForceRedraw = 1;
}

void gameTinyArena_update()
{
    // Ticks independently of arState - a death can be triggered mid-
    // AR_STATE_PLAYING (inside arUpdateTentacles()), but the state machine
    // itself moves on to AR_STATE_GAMEOVER_WAIT_RELEASE the very next
    // frame, whose own branch below returns early - the sweep still needs
    // to keep advancing across those frames instead of freezing partway.
    arAdvanceDeathSweep();

    if( arState == AR_STATE_GAMEOVER_WAIT_RELEASE )
    {
        // Matches upstream's own busy-wait exactly: no redraw at all while
        // waiting - whatever was last drawn (the frozen death frame, or a
        // blank screen on first boot) just stays on screen. Upstream's own
        // buffer clear (see arClearBufferWhite()'s comment) happens in
        // memory before this wait but is never actually flipped to the
        // display until the blink state's own first draw call - so the
        // white background only needs to exist by the time *that* happens.
        //
        // This state is reachable right at boot (the attract screen) and
        // has no timer of its own - if the quit-confirmation dialog opens
        // while sitting here and the player cancels ("NO"), nothing would
        // ever redraw to clear the dialog's leftover pixels without this
        // explicit force (see gameTinyArena_forceRedraw()).
        if( arForceRedraw )
        {
            arTinyFlip();
            arForceRedraw = 0;
        }
        if( !isFirePressed() )
        {
            arClearBufferWhite();
            arState = AR_STATE_GAMEOVER_BLINK;
        }
        return;
    }

    if( arState == AR_STATE_GAMEOVER_BLINK )
    {
        int i;
        int show = ( arGameOverVal > 128 );
        for( i = 0; i < 26; i++ )
        {
            int bit;
            if( show ) bit = arStart[ i ];
            else bit = 0;
            // Vircon32 ints are full 32-bit words with no implicit
            // truncation the way AVR's uint8_t VBuffer gave upstream -
            // `~bit` on a small value sets all the upper 24 bits too,
            // which later fed a huge value into arSliceByte()'s `data >>
            // 4` and read arExpand[] out of bounds (a real crash, caught
            // via the assemble -g debug-info technique). Mask back down
            // to a real byte, the same fix this project's very first
            // byte-truncation bug already established for md_drawColumn().
            arVBuffer[ 2 ][ 19 + i ] = ( ~bit ) & 0xFF;
        }
        arTinyFlip();

        if( isFirePressed() )
        {
            arInitNewGame();
            return;
        }

        arGameOverVal = ( arGameOverVal + 2 ) & 0xFF;
        return;
    }

    // AR_STATE_PLAYING
    int y, x;
    for( y = 0; y < 4; y++ )
      for( x = 0; x < 64; x++ )
        arVBuffer[ y ][ x ] = 0xff;

    if( isFirePressed() && arBulletActive == 0 )
      arShoot();

    arUpdateTentacles();
    arRenderRaycast();

    if( arBulletActive )
    {
        arBulletActive--;
        arBulletY = 31 - ( 11 - arBulletActive ) * 15 / 11;
        arBulletSize = 2 + arBulletActive * 10 / 11;
        int r = arBulletSize >> 1;

        int dx;
        for( dx = -r - 1; dx <= r + 1; dx++ )
        {
            int px = arBulletX + dx;
            if( px < 0 || px >= 64 ) continue;
            int dy;
            for( dy = -r - 1; dy <= r + 1; dy++ )
            {
                int py = arBulletY + dy;
                if( py < 0 || py >= 32 ) continue;
                if( dx * dx + dy * dy <= ( r + 1 ) * ( r + 1 ) )
                  arVBuffer[ py >> 3 ][ px ] = arVBuffer[ py >> 3 ][ px ] & ~( 1 << ( py & 7 ) );
            }
        }

        if( arBulletActive == 0 )
        {
            int centerWallDist8 = arWallDist[ 32 ];
            int killedIndex = 255;

            int i;
            for( i = 0; i < AR_NUM_SPRITES; i++ )
            {
                if( !arWorldSprites[ i ].active ) continue;

                float dx2 = arWorldSprites[ i ].x - arPosX;
                float dy2 = arWorldSprites[ i ].y - arPosY;

                float distSq = dx2 * dx2 + dy2 * dy2;
                if( distSq < 0.1 || distSq > 25.0 ) continue;

                float proj = dx2 * arDirX + dy2 * arDirY;
                if( proj < 0.15 ) continue;

                float perp = fabs( dx2 * arDirY - dy2 * arDirX ) / ( proj + 0.1 );
                if( perp > 0.55 ) continue;

                int enemyDist8;
                if( proj > 5.1 ) enemyDist8 = 255;
                else enemyDist8 = (int)( proj * 50.0 );
                if( enemyDist8 >= centerWallDist8 + 8 ) continue;

                arWorldSprites[ i ].health--;
                Sound( 200, 4 );

                if( arWorldSprites[ i ].health == 0 )
                {
                    arWorldSprites[ i ].active = 0;
                    killedIndex = i;
                    arKillCount++;
                    Sound( 3, 15 );

                    int candidate = 255;
                    int j;
                    for( j = 0; j < AR_NUM_SPRITES; j++ )
                    {
                        if( !arWorldSprites[ j ].active && j != killedIndex )
                        {
                            candidate = j;
                            break;
                        }
                    }
                    if( candidate != 255 )
                    {
                        arWorldSprites[ candidate ].active = 1;
                        arWorldSprites[ candidate ].health = 5;
                        arWorldSprites[ candidate ].x = arSpawnX[ candidate ];
                        arWorldSprites[ candidate ].y = arSpawnY[ candidate ];
                    }
                }
                break;
            }
        }
    }

    int bobOffset = arBobPos << 1;
    arDrawSprite2Bit( 18 + arBOB[ bobOffset ], 19 + arBOB[ bobOffset + 1 ], arGun );

    arTinyFlip();

    if( arPlayerHealth == 0 )
      arState = AR_STATE_GAMEOVER_WAIT_RELEASE;
}

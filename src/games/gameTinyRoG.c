// =============================================================================
// TinyRoG - ported from Cave11Item.ino ("more games/sample/Cave11Item/"),
// one of 3 AI-assisted original ATtiny85 games in `more games/sample/` -
// see that folder's own CLAUDE.md catalog entry for how all 3 were
// identified/triaged, and Jump Slime's own writeup for the shared driver-
// lineage finding (this game #includes "ELECTROLIB.h" directly, removing
// any doubt about which lineage it's on - same as Jump Slime, no new shim
// needed here either).
//
// Author: 近藤さんちの研究室 ("Kondo-san's Laboratory"), note.com handle
// "kondolab" - credited "KONDOLAB" in the menu, same treatment as Jump
// Slime. Two articles cover this game: a concept/part-1 piece
// (https://note.com/kondolab/n/ndb6bf9c5b9d6, 2025-08-23, no source code)
// and the part-2/finished-game piece that gives the game its real name
// "TinyRoG" and the source download
// (https://note.com/kondolab/n/n1806e4234495, 2025-08-23). No license is
// stated for this game's own code on either article page - listed as
// "None specified" for licensing purposes only, same distinction already
// made for Jump Slime (a known author, an unstated license).
//
// A roguelike RPG: explore a procedurally-generated maze each floor,
// grab the key to reveal the stairs, fight or dodge wandering monsters,
// occasionally find a healing item box, and reach floor 30 to win.
// Picked as the second of the 3 sample games to port (after Jump Slime),
// over TinY Fi (the fighting game) specifically because TinY Fi's own
// render dispatch is a dense, easy-to-mistranscribe tangle of per-
// animation-state, per-layer blitzSpriteDir() calls with many small
// magic pixel offsets, whereas this game's own complexity is concentrated
// in self-contained algorithmic logic (maze generation, tile-based
// movement/combat) rather than a sprawling render table - and its own
// tinyDraw() already precomputes enemy screen positions once per frame
// (calcEnmDraw()) rather than recomputing them per pixel, a sign the
// original AI-assisted code was already somewhat performance-conscious.
//
// Every ternary expression, `switch` statement, and `enum` was converted
// to plain if/else-if chains and `#define` constants respectively,
// matching this project's own standing caution around those 3 constructs
// (never confirmed unsupported, but never tested here either, so not
// worth being the first port to find out). Upstream's own debounce
// mechanism (`lastBtnAState`/`DEBOUNCE`, a real blocking `_delay_ms(30)`)
// is dropped in favor of a single shared edge-detect flag, matching every
// other port in this project. `random(n)`/`random(min,max)` calls are
// routed through the shared `arand()` helper (a `trogRandRange(min,max)`
// wrapper for the two-argument form) rather than raw `rand()%n`, matching
// the established fix for AVR's `rand()`-range mismatch. `randomSeed()`
// has no equivalent call here (Vircon32's `rand()` isn't seedable the
// same way) - matching Wren Rollercoaster's own precedent, a minor
// accepted deviation (maze layout won't be bit-identical to a real
// device's own specific seed).
//
// `dig()`'s own local stack arrays (`stack_x[MAZE_STACK_SIZE]`,
// `stack_y[MAZE_STACK_SIZE]`) are a variable-length array upstream - the
// size is computed at runtime from the current stage's own maze
// dimensions via a macro. Variable-length local arrays are not something
// this project's dialect has ever been confirmed to support (every other
// port here uses fixed-size arrays) - ported as fixed-size globals sized
// to the true worst case instead: `((17+1)/2)*((11+1)/2) = 54`, using
// this game's own maximum possible `TILES_W`/`TILES_H` (17x11, reached at
// `nowStageNo>=20`). Safe because `dig()`'s own algorithm only ever
// pushes a given odd-coordinate cell onto the stack once (it's marked
// carved - value 0 - immediately when pushed, so no future search path
// can ever find it as a wall and push it again), so 54 is a true upper
// bound on stack depth at any moment, not just the *count* pushed overall
// - the same reasoning upstream's own comment gives for its own
// dynamically-sized version.
// =============================================================================

#define TROG_SCREEN_WIDTH 128
#define TROG_SCREEN_HEIGHT 64
#define TROG_CHAR_X 60 // 128/2 - 8/2
#define TROG_CHAR_Y 28 // 64/2 - 8/2
#define TROG_CLEAR_FLOOR 30
#define TROG_WAIT_FLM 3
#define TROG_ONETIME_STEP 8
#define TROG_CNTCYCLE 10
#define TROG_MAX_ENEMIES 5
#define TROG_WAIT_GAMEOVER 50

#define TROG_TILE_ROAD 0
#define TROG_TILE_WALL 1
#define TROG_TILE_KEY 2
#define TROG_TILE_STAIRS 3
#define TROG_TILE_ITMBOX 4
#define TROG_TILE_ENM1 10

#define TROG_STATE_STAGE 0
#define TROG_STATE_CAVE 1

#define TROG_MAZE_MAX_W 17
#define TROG_MAZE_MAX_H 11
#define TROG_MAZE_STACK_SIZE 54

// Upstream has no genuine real-time throttle at all (same "no timing
// model whatsoever" category as Jump Slime's own frame-pacing survey
// entry) - a deliberate slowdown added on direct request, not a restored
// original rate. Whole-function tick-skip (gates logic AND redraw
// together), matching Jump Slime's own precedent and the majority of
// this project's history (NumberPlace/HollowSeeker/t2048/Doc/Pacman/
// Pipe). Every existing frame-counted constant in this file (trogCnt's
// own 0-99 cycle and its TROG_WAIT_GAMEOVER=50 clamp, the enemy attack-
// windup countdown starting at 7, TROG_WAIT_FLM=3) is deliberately left
// unrescaled - they simply now take twice as long in real time, matching
// this project's own standing "one divisor, no dual bookkeeping" practice.
#define TROG_FPS 30
#define TROG_TICK_DIVISOR ( 60 / TROG_FPS )
int trogTickSkipCounter;

// -----------------------------------------------------------------------------
// Sprite/font data - byte-diff-verified against the original source via a
// small Python script before ever building, not hand-copied blind
// (matching this project's own "byte-diff transcribed tables" discipline,
// reinforced hard by the SnakeGame85 port earlier this session where
// skipping exactly this step shipped a corrupted bitmap array).
//
// Tables consumed via jslmBlitzSprite-equivalent/SPEED_BLITZ keep their
// upstream {W,H,...} header (trogMiniTitle/trogMiniYN/trogMiniNum/
// trogOneEnm/trogMyCh/trogMyAtk); tables read via a raw direct index
// (trogOneBlock/trogOneDown/trogOneKey/trogOneBox/trogEnemyCh) have no
// header upstream either (confirmed by inspection: `EnemyCh[][2][2+8]`'s
// own "+8" declared size is never actually filled with a real 2-byte
// header value in the literal data, and every read site indexes it
// directly by column 0-7, never reading a header - the declared size is
// a documentation artifact, not a real header, so ported as a clean
// 8-byte-per-frame table instead of preserving 2 always-zero phantom
// slots that are never read).
// -----------------------------------------------------------------------------

int[50] trogMiniTitle =
{
24,1,
4,252,4,208,0,192,64,128,32,192,32,0,252,36,216,0,96,144,96,0,96,144,208,0, // Title
124,24,48,124,0,124,84,84,0,108,16,108,0,4,124,4,0,6,82,14,0,0,0,0, // Next
};

int[58] trogMiniYN =
{
8,1,
26,98,26,0,120,16,32,120, // YES
24,96,24,0,122,18,34,122, // NO
248,32,248,0,248,40,56,0, // HP
248,136,112,0,248,0,248,168, // DIE
56,68,116,0,48,72,48,0, // GO
248,40,0,248,128,0,128,0, // Fl.
248,40,0,232,0,224,32,192, // Fin
};

int[42] trogMiniNum =
{
4,1,
248,136,248,0, 0,248,0,0, 232,168,184,0, 136,168,248,0, 56,32,248,0,
184,168,232,0, 248,168,232,0, 8,232,24,0, 248,168,248,0, 184,168,248,0,
};

int[10] trogOneEnm = { 8,1, 0,56,124,124,124,124,56,0, };
int[8] trogOneBlock = { 95,111,126,125,127,127,54,0, };
int[8] trogOneDown  = { 255,129,249,129,225,129,193,255, };
int[8] trogOneKey   = { 0,0,6,122,106,6,0,0, };
int[8] trogOneBox   = { 0,124,5,85,109,124,0,0, };

// 6 enemy types (slime=0,crab=1,alien=2,UFO=3,UFO4=4,crabMini=5) x 2 anim
// frames x 8 columns, flat-indexed as type*16 + frame*8 + col
int[96] trogEnemyCh =
{
112,136,180,132,180,136,112,0,  120,132,154,130,154,132,120,0, // slime
214,124,118,112,118,124,214,0,  211,126,107,120,107,126,211,0, // crab
240,122,220,120,220,122,240,0,  248,125,238,124,238,125,248,0, // alien
152,52,190,54,190,52,152,0,     12,154,31,155,31,154,12,0,     // UFO
152,52,190,54,190,52,152,0,     12,154,31,155,31,154,12,0,     // UFO4 (same art as UFO upstream)
208,120,112,112,112,120,208,0,  208,124,104,120,104,124,208,0, // crabMini
};

// 4 directions (0 down,1 up,2 left,3 right) x 2 walk frames, each row
// {8,1,...8 data...} - flat-indexed as dir*20 + frame*10 + col
int[80] trogMyCh =
{
8,1,31,136,236,191,63,204,56,56,    8,1,62,16,204,63,191,236,152,24,   // DOWN
8,1,56,56,236,255,255,236,136,31,   8,1,24,152,236,255,255,236,16,62,  // UP
8,1,0,62,16,191,255,60,24,0,        8,1,0,159,200,63,127,220,0,0,      // LEFT
8,1,0,24,60,255,191,16,62,0,        8,1,0,0,220,127,63,200,159,0,      // RIGHT
};

// 4 directions, each {8,1,...8 data...} - flat-indexed as dir*10 + col
int[40] trogMyAtk =
{
8,1,16,248,20,127,31,114,70,6,
8,1,96,112,254,254,248,159,8,0,
8,1,16,16,144,216,63,63,92,128,
8,1,128,92,63,63,216,144,16,16,
};

int* trogMyChFrame( int dir, int frame )
{
    return &trogMyCh[ ( dir * 20 ) + ( frame * 10 ) ];
}

int* trogMyAtkFrame( int dir )
{
    return &trogMyAtk[ dir * 10 ];
}

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

struct TrogEnemy
{
    int isAlive, nowEnmAtk, type;
};

struct TrogNumDisp
{
    int tens, units, xStart, xEnd;
};

int trogState;
int trogNowStageNo;
int trogNowHp;
int trogStatusFloor; // 0 normal, 1 on stairs (no prompt yet), 2 Y selected, 3 N selected
int trogPrevFire;
int trogCnt; // 0-99 cycling animation/wait counter

int trogMoveDelay;
int trogNowAtk;
int trogCmp; // facing: 0 down, 1 up, 2 left, 3 right

int trogMapX, trogMapY; // player position, pixel space

int trogTilesW, trogTilesH;
int[11][17] trogTileMap; // [y][x], sized to the true max (11 rows x 17 cols)

TrogNumDisp trogHpDisp;
TrogNumDisp trogStageDisp;

TrogEnemy[5] trogEnemies;

int[54] trogStackX;
int[54] trogStackY;

// -----------------------------------------------------------------------------
// Sprite blitting - a direct, already-proven translation of the same
// blitzSprite() algorithm this project's gameTinyBert.c/gameJumpSlime.c
// already carry under their own prefixes.
// -----------------------------------------------------------------------------

int trogRecupeLineY( int valeur )
{
    return valeur >> 3;
}

int trogRecupeDecalageY( int valeur )
{
    return valeur - ( ( valeur >> 3 ) << 3 );
}

int trogSplitSpriteDecalageY( int decalage, int input, int upOrDown )
{
    if( upOrDown )
      return ( input << decalage ) & 0xFF;
    return input >> ( 8 - decalage );
}

int trogBlitzSprite( int xPos, int yPos, int xPass, int yPass, int frame, int* sprites )
{
    int wSprite = sprites[ 0 ];
    int hSprite = sprites[ 1 ];
    int wMax = ( hSprite * wSprite ) + 1;
    int picByte = frame * ( wMax - 1 );
    int recupeLineY = trogRecupeLineY( yPos );

    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos ||
        recupeLineY > yPass || ( recupeLineY + hSprite ) < yPass )
      return 0x00;

    int spriteYLine = yPass - recupeLineY;
    int spriteYDecalage = trogRecupeDecalageY( yPos );
    int scanA = ( xPass - xPos ) + ( spriteYLine * wSprite ) + 2;
    int scanB = ( xPass - xPos ) + ( ( spriteYLine - 1 ) * wSprite ) + 2;

    int outByte;
    if( scanA > wMax )
      outByte = 0x00;
    else
      outByte = trogSplitSpriteDecalageY( spriteYDecalage, sprites[ scanA + picByte ], 1 );

    if( spriteYLine > 0 )
    {
        int outByte2 = trogSplitSpriteDecalageY( spriteYDecalage, sprites[ scanB + picByte ], 0 );
        if( scanB > wMax ) return outByte;
        return outByte | outByte2;
    }
    return outByte;
}

int trogSpeedBlitz( int xPos, int xPass, int frame, int* sprites )
{
    int wSprite = sprites[ 0 ];
    if( xPass > ( xPos + ( wSprite - 1 ) ) || xPass < xPos ) return 0;
    return sprites[ 2 + ( xPass - xPos ) + ( frame * wSprite ) ];
}

// -----------------------------------------------------------------------------
// Numeric display
// -----------------------------------------------------------------------------

void trogMakeNumData( int value, TrogNumDisp* data )
{
    int temp = value;
    data->tens = 0;
    while( temp >= 10 )
    {
        temp = temp - 10;
        data->tens = data->tens + 1;
    }
    data->units = temp;
}

int trogGetByteNum( int xPix, TrogNumDisp* data )
{
    if( xPix < data->xStart || xPix > data->xEnd ) return 0;
    int tmpBmp = trogSpeedBlitz( data->xStart + 0, xPix, data->tens, trogMiniNum );
    tmpBmp = tmpBmp | trogSpeedBlitz( data->xStart + 4, xPix, data->units, trogMiniNum );
    return tmpBmp;
}

// -----------------------------------------------------------------------------
// RNG helpers
// -----------------------------------------------------------------------------

// matches Arduino's random(minV,maxV): returns a value in [minV, maxV-1]
int trogRandRange( int minV, int maxV )
{
    return minV + arand( maxV - minV );
}

// -----------------------------------------------------------------------------
// Maze generation
// -----------------------------------------------------------------------------

void trogJump( int tx, int ty )
{
    trogMapX = tx * TROG_ONETIME_STEP;
    trogMapY = ty * TROG_ONETIME_STEP;
}

void trogDig( int sx, int sy )
{
    int sp = 0;
    trogStackX[ sp ] = sx;
    trogStackY[ sp ] = sy;
    sp = sp + 1;
    trogTileMap[ sy ][ sx ] = TROG_TILE_ROAD;

    int[8] dirs = { 0,-1, 0,1, -1,0, 1,0 };

    while( sp > 0 )
    {
        sp = sp - 1;
        int x = trogStackX[ sp ];
        int y = trogStackY[ sp ];

        int[4] dirIndices = { 0, 1, 2, 3 };
        int i;
        for( i = 3; i > 0; i = i - 1 )
        {
            int j = arand( i + 1 );
            int tmp = dirIndices[ i ];
            dirIndices[ i ] = dirIndices[ j ];
            dirIndices[ j ] = tmp;
        }

        int k;
        for( k = 0; k < 4; k = k + 1 )
        {
            int dirIdx = dirIndices[ k ];
            int dx = dirs[ dirIdx * 2 + 0 ];
            int dy = dirs[ dirIdx * 2 + 1 ];

            int nx = x + dx * 2;
            int ny = y + dy * 2;

            if( nx > 0 && nx < trogTilesW - 1 && ny > 0 && ny < trogTilesH - 1 && trogTileMap[ ny ][ nx ] == TROG_TILE_WALL )
            {
                trogTileMap[ y + dy ][ x + dx ] = TROG_TILE_ROAD;
                trogTileMap[ ny ][ nx ] = TROG_TILE_ROAD;

                trogStackX[ sp ] = nx;
                trogStackY[ sp ] = ny;
                sp = sp + 1;
            }
        }
    }
}

void trogHandleInit()
{
    trogTilesW = TROG_MAZE_MAX_W;
    trogTilesH = TROG_MAZE_MAX_H;
    int y, x;
    for( y = 0; y < trogTilesH; y = y + 1 )
      for( x = 0; x < trogTilesW; x = x + 1 )
        trogTileMap[ y ][ x ] = TROG_TILE_WALL;

    trogCmp = 0;
    trogNowStageNo = 1;
    trogNowHp = 5;
    trogMakeNumData( trogNowHp, &trogHpDisp );
    trogState = TROG_STATE_STAGE;
    trogStatusFloor = 0;
}

void trogHandleGoCave()
{
    int sizeStep = trogNowStageNo / 5;
    trogTilesW = min( 8, 4 + sizeStep ) * 2 + 1;
    trogTilesH = min( 5, 2 + sizeStep ) * 2 + 1;
    int y, x;
    for( y = 0; y < trogTilesH; y = y + 1 )
      for( x = 0; x < trogTilesW; x = x + 1 )
        trogTileMap[ y ][ x ] = TROG_TILE_WALL;

    trogDig( 1, 1 );

    int wallsToRemove = min( 2 + ( trogNowStageNo / 5 ) * 3, 14 );
    int i;
    for( i = 0; i < wallsToRemove; i = i + 1 )
    {
        int randTx, randTy;
        do
        {
            randTx = trogRandRange( 1, trogTilesW - 1 );
            randTy = trogRandRange( 1, trogTilesH - 1 );
        }
        while( trogTileMap[ randTy ][ randTx ] != TROG_TILE_WALL ||
               ( trogTileMap[ randTy - 1 ][ randTx ] == TROG_TILE_WALL && trogTileMap[ randTy + 1 ][ randTx ] == TROG_TILE_WALL &&
                 trogTileMap[ randTy ][ randTx - 1 ] == TROG_TILE_WALL && trogTileMap[ randTy ][ randTx + 1 ] == TROG_TILE_WALL ) );
        trogTileMap[ randTy ][ randTx ] = TROG_TILE_ROAD;
    }

    int randTx, randTy;
    do
    {
        randTx = arand( trogTilesW );
        randTy = arand( trogTilesH );
    }
    while( trogTileMap[ randTy ][ randTx ] != 0 );
    trogTileMap[ randTy ][ randTx ] = TROG_TILE_KEY;

    if( ( trogNowStageNo < 23 && trogNowStageNo % 5 == 0 ) || ( trogNowStageNo >= 23 && ( trogNowStageNo - 23 ) % 3 == 0 ) )
    {
        do
        {
            randTx = arand( trogTilesW );
            randTy = arand( trogTilesH );
        }
        while( trogTileMap[ randTy ][ randTx ] != 0 );
        trogTileMap[ randTy ][ randTx ] = TROG_TILE_ITMBOX;
    }

    do
    {
        randTx = arand( trogTilesW );
        randTy = arand( trogTilesH );
    }
    while( trogTileMap[ randTy ][ randTx ] != 0 );
    trogJump( randTx, randTy );

    int maxEnm = min( sizeStep + 1, TROG_MAX_ENEMIES );
    for( i = maxEnm; i < TROG_MAX_ENEMIES; i = i + 1 )
      trogEnemies[ i ].isAlive = 0;

    for( i = 0; i < maxEnm; i = i + 1 )
    {
        int enemyX, enemyY;
        int isOverlap;
        do
        {
            isOverlap = 0;
            enemyX = arand( trogTilesW );
            enemyY = arand( trogTilesH );
            int j;
            for( j = 0; j < i; j = j + 1 )
            {
                if( trogTileMap[ enemyY ][ enemyX ] == TROG_TILE_ENM1 + ( j * 10 ) ) { isOverlap = 1; break; }
            }
        }
        while( isOverlap || trogTileMap[ enemyY ][ enemyX ] != TROG_TILE_ROAD ||
               ( abs( enemyX - randTx ) < 3 && abs( enemyY - randTy ) < 3 ) );
        trogTileMap[ enemyY ][ enemyX ] = TROG_TILE_ENM1 + ( i * 10 );
        trogEnemies[ i ].isAlive = 1;
        trogEnemies[ i ].type = i;
    }

    trogState = TROG_STATE_CAVE;
}

// -----------------------------------------------------------------------------
// Tile events / stairs dialog
// -----------------------------------------------------------------------------

void trogHandleTileEvt()
{
    if( trogMoveDelay == 0 && trogNowAtk == 0 )
    {
        int currentTileType = trogTileMap[ trogMapY / TROG_ONETIME_STEP ][ trogMapX / TROG_ONETIME_STEP ];
        if( currentTileType != TROG_TILE_STAIRS && trogStatusFloor != TROG_TILE_ROAD )
          trogStatusFloor = TROG_TILE_ROAD;

        if( currentTileType == TROG_TILE_KEY )
        {
            trogTileMap[ trogMapY / TROG_ONETIME_STEP ][ trogMapX / TROG_ONETIME_STEP ] = TROG_TILE_ROAD;
            int randTx, randTy;
            do
            {
                randTx = arand( trogTilesW );
                randTy = arand( trogTilesH );
            }
            while( trogTileMap[ randTy ][ randTx ] != TROG_TILE_ROAD ||
                   ( abs( randTx - trogMapX / TROG_ONETIME_STEP ) < 3 && abs( randTy - trogMapY / TROG_ONETIME_STEP ) < 3 ) );
            trogTileMap[ randTy ][ randTx ] = TROG_TILE_STAIRS;
        }
        else if( currentTileType == TROG_TILE_STAIRS )
        {
            if( trogStatusFloor == TROG_TILE_ROAD ) trogStatusFloor = TROG_TILE_KEY;
        }
        else if( currentTileType == TROG_TILE_ITMBOX )
        {
            Sound( 200, 20 );
            trogTileMap[ trogMapY / TROG_ONETIME_STEP ][ trogMapX / TROG_ONETIME_STEP ] = TROG_TILE_ROAD;
            trogNowHp = 5;
            trogMakeNumData( trogNowHp, &trogHpDisp );
        }
    }
}

void trogHandleStai( bool fireJustPressed )
{
    if( trogStatusFloor == 2 && isRightPressed() ) trogStatusFloor = 3;
    else if( trogStatusFloor == 3 && isLeftPressed() ) trogStatusFloor = 2;

    if( fireJustPressed )
    {
        if( trogStatusFloor == 2 )
        {
            trogStatusFloor = 0;
            trogNowStageNo = trogNowStageNo + 1;
            trogCnt = 0;
            trogState = TROG_STATE_STAGE;
        }
        else if( trogStatusFloor == 3 )
        {
            trogStatusFloor = 1;
        }
    }
}

// -----------------------------------------------------------------------------
// Combat / enemy AI
// -----------------------------------------------------------------------------

bool trogFindTile( int targetTile, int* outX, int* outY )
{
    int y, x;
    for( y = 0; y < trogTilesH; y = y + 1 )
    {
        for( x = 0; x < trogTilesW; x = x + 1 )
        {
            if( ( trogTileMap[ y ][ x ] / 10 * 10 ) == targetTile )
            {
                *outX = x;
                *outY = y;
                return true;
            }
        }
    }
    return false;
}

void trogGetCharacterOffset( int direc, int* offsetX, int* offsetY, int value )
{
    if( direc == 2 ) *offsetX = -value;
    else if( direc == 3 ) *offsetX = value;
    else *offsetX = 0;

    if( direc == 0 ) *offsetY = value;
    else if( direc == 1 ) *offsetY = -value;
    else *offsetY = 0;
}

// matches enmChange[]: what a damaged enemy's type becomes, 0 means "dies instead"
int trogEnmChange( int type )
{
    if( type == 1 ) return 5; // CRAB -> CRAB_MINI
    if( type == 3 ) return 1; // UFO -> CRAB
    if( type == 4 ) return 5; // UFO4 -> CRAB_MINI
    return 0;
}

void trogDamageEnemy()
{
    int atkTileX = trogMapX / TROG_ONETIME_STEP;
    int atkTileY = trogMapY / TROG_ONETIME_STEP;
    int offsetX, offsetY;
    trogGetCharacterOffset( trogCmp, &offsetX, &offsetY, 1 );
    atkTileX = atkTileX + offsetX;
    atkTileY = atkTileY + offsetY;

    if( atkTileX >= 0 && atkTileX < trogTilesW && atkTileY >= 0 && atkTileY < trogTilesH )
    {
        int tileVal = trogTileMap[ atkTileY ][ atkTileX ];
        if( tileVal >= TROG_TILE_ENM1 && tileVal <= TROG_MAX_ENEMIES * 10 )
        {
            Sound( 150, 20 );
            int enmIndex = ( tileVal - TROG_TILE_ENM1 ) / 10;
            int nextType = trogEnmChange( trogEnemies[ enmIndex ].type );
            if( nextType != 0 )
            {
                trogEnemies[ enmIndex ].type = nextType;
            }
            else
            {
                trogEnemies[ enmIndex ].isAlive = 0;
                trogTileMap[ atkTileY ][ atkTileX ] = tileVal % 10;
            }
        }
    }
}

bool trogMoveEnm( int i, int enmX, int enmY, int dx, int dy )
{
    int nextEnmX = enmX + dx;
    int nextEnmY = enmY + dy;
    int nextTile = trogTileMap[ nextEnmY ][ nextEnmX ];

    if( ( nextTile % 10 ) != TROG_TILE_WALL && ( nextTile / 10 * 10 ) < TROG_TILE_ENM1 &&
        ( nextEnmX != trogMapX / TROG_ONETIME_STEP || nextEnmY != trogMapY / TROG_ONETIME_STEP ) )
    {
        trogTileMap[ enmY ][ enmX ] = trogTileMap[ enmY ][ enmX ] % 10;
        trogTileMap[ nextEnmY ][ nextEnmX ] = TROG_TILE_ENM1 + ( i * 10 ) + ( nextTile % 10 );
        return true;
    }
    return false;
}

void trogActEnemy()
{
    int i;
    for( i = 0; i < TROG_MAX_ENEMIES; i = i + 1 )
    {
        if( !trogEnemies[ i ].isAlive ) continue;
        if( trogEnemies[ i ].nowEnmAtk > 0 ) continue;

        int enmX = -1, enmY = -1;
        if( trogFindTile( TROG_TILE_ENM1 + ( i * 10 ), &enmX, &enmY ) )
        {
            int diffX = trogMapX / TROG_ONETIME_STEP - enmX;
            int diffY = trogMapY / TROG_ONETIME_STEP - enmY;

            if( ( abs( diffX ) <= 1 && diffY == 0 ) || ( abs( diffY ) <= 1 && diffX == 0 ) )
            {
                trogEnemies[ i ].nowEnmAtk = 7;
                continue;
            }

            int dx = 0, dy = 0;
            if( trogEnemies[ i ].type == 2 || trogEnemies[ i ].type == 3 )
            {
                int dir = arand( 4 );
                int j;
                for( j = 0; j < 4; j = j + 1 )
                {
                    int currentDir = ( dir + j ) % 4;
                    if( currentDir == 3 ) dx = 1; else if( currentDir == 2 ) dx = -1; else dx = 0;
                    if( currentDir == 0 ) dy = 1; else if( currentDir == 1 ) dy = -1; else dy = 0;
                    if( trogMoveEnm( i, enmX, enmY, dx, dy ) ) break;
                }
            }
            else
            {
                if( abs( diffX ) > abs( diffY ) )
                {
                    if( diffX > 0 ) dx = 1; else dx = -1;
                }
                else
                {
                    if( diffY > 0 ) dy = 1; else dy = -1;
                }
                if( !trogMoveEnm( i, enmX, enmY, dx, dy ) )
                {
                    if( dx == 0 ) { if( diffX > 0 ) dx = 1; else dx = -1; } else dx = 0;
                    if( dy == 0 ) { if( diffY > 0 ) dy = 1; else dy = -1; } else dy = 0;
                    trogMoveEnm( i, enmX, enmY, dx, dy );
                }
            }
        }
    }
}

bool trogIsMovePossible( int nextX, int nextY )
{
    if( nextX > ( trogTilesW * TROG_ONETIME_STEP ) - 8 || nextY > ( trogTilesH * TROG_ONETIME_STEP ) - 8 ) return false;

    int startTileX = nextX / 8, endTileX = ( nextX + 7 ) / 8;
    int startTileY = nextY / 8, endTileY = ( nextY + 7 ) / 8;

    int ty, tx;
    for( ty = startTileY; ty <= endTileY; ty = ty + 1 )
    {
        for( tx = startTileX; tx <= endTileX; tx = tx + 1 )
        {
            if( tx < trogTilesW && ty < trogTilesH )
            {
                int tileVal = trogTileMap[ ty ][ tx ];
                if( tileVal == TROG_TILE_WALL ) return false;
                if( tileVal >= TROG_TILE_ENM1 && tileVal <= TROG_MAX_ENEMIES * 10 )
                {
                    int enmIndex = ( tileVal - TROG_TILE_ENM1 ) / 10;
                    if( enmIndex < TROG_MAX_ENEMIES && trogEnemies[ enmIndex ].isAlive ) return false;
                }
            }
            else return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

// trogMakeTile() runs once per pixel (1024/frame), but a tile is an 8x8
// block - 8 *consecutive* screen columns always resolve to the same
// (tileX,tileY), so the "which sprite bytes apply here" work below was
// being redone up to 8 times in a row for a result that hadn't changed.
// Not the O(pixels x objects) shape found and fixed in Jump Slime (this
// function only ever looks at the *one* tile under a given pixel, not
// every tile on the map), but the same underlying "cache what doesn't
// actually change every pixel" lesson already proven in this project
// (Tiny DDug's own wall-mask cache, Tiny Doc's own row cache) - the
// resolved 8-byte pattern for the current tile is cached and only
// recomputed when (tileX,tileY) actually changes from the previous call,
// cutting the sprite-lookup/branch cost to roughly 1/8th on average. No
// invalidation logic needed: a cache miss just means "recompute," never
// a correctness risk, since trogMakeTile() itself doesn't care whether
// the cache was warm or cold - only ever reads its own hot result.
int[8] trogTileCacheBytes;
int trogTileCacheX = -999;
int trogTileCacheY = -999;

void trogResolveTileBytes( int tileX, int tileY )
{
    if( tileX == trogTileCacheX && tileY == trogTileCacheY ) return;
    trogTileCacheX = tileX;
    trogTileCacheY = tileY;

    int bgTile = trogTileMap[ tileY ][ tileX ] % 10;
    int chTile = trogTileMap[ tileY ][ tileX ] / 10 * 10;

    int enmIdx = -1;
    if( chTile >= TROG_TILE_ENM1 && chTile <= TROG_MAX_ENEMIES * 10 )
    {
        enmIdx = chTile / 10 - 1;
        if( trogEnemies[ enmIdx ].nowEnmAtk > 2 && trogEnemies[ enmIdx ].nowEnmAtk < 5 )
          enmIdx = -1; // mid-attack: enemy is drawn separately (see trogCompositeOverlayRow), not here
    }

    int col;
    for( col = 0; col < 8; col = col + 1 )
    {
        int b;
        if( bgTile == TROG_TILE_WALL ) b = trogOneBlock[ col ];
        else if( bgTile == TROG_TILE_KEY ) b = trogOneKey[ col ];
        else if( bgTile == TROG_TILE_STAIRS ) b = trogOneDown[ col ];
        else if( bgTile == TROG_TILE_ITMBOX ) b = trogOneBox[ col ];
        else b = 0x00;

        if( enmIdx >= 0 )
          b = b | trogEnemyCh[ trogEnemies[ enmIdx ].type * 16 + ( ( trogCnt / TROG_CNTCYCLE ) % 2 ) * 8 + col ];

        trogTileCacheBytes[ col ] = b;
    }
}

// Even with trogResolveTileBytes()'s own caching above, calling
// trogMakeTile() once per pixel (1024/frame) still means up to 1024 real
// function calls, each re-deriving tileX/mapYByteStart/bounds-checks
// before ever reaching the (usually-cached) sprite lookup - the same
// "self-gated call still costs a full call every time it's invoked"
// lesson found repeatedly elsewhere in this project (Jump Slime's own
// block/sprite buffers, Arkanoid/Bert/Tris/Trick/Morpion before that).
// Measured via the perf overlay: the per-pixel version alone still read a
// pegged 100% even with trogResolveTileBytes()'s caching in place.
// Restructured the same way as Jump Slime's own blocks fix: composite
// the whole page row by walking *tiles* (at most 17 per row) instead of
// *pixels* (always 128), writing each visible tile's own cached 8-byte
// pattern - already shifted, using the exact same shiftAmount conditions
// trogMakeTile() itself used - directly into a shared row buffer.
int[128] trogTileRowBuffer;

void trogCompositeTileRow( int page, int mapXScreenOrigin, int mapYScreenOrigin )
{
    int i;
    for( i = 0; i < TROG_SCREEN_WIDTH; i = i + 1 ) trogTileRowBuffer[ i ] = 0;

    int mapYByteStart = mapYScreenOrigin + page * 8;
    int tileY;
    for( tileY = mapYByteStart / 8; tileY <= ( mapYByteStart + 7 ) / 8; tileY = tileY + 1 )
    {
        if( tileY < 0 || tileY >= trogTilesH ) continue;

        int blockScreenY = ( tileY * 8 ) - mapYScreenOrigin;
        int shiftAmount = ( page * 8 ) - blockScreenY;
        bool shiftValid = ( shiftAmount == 0 ) || ( shiftAmount > 0 && shiftAmount < 8 ) || ( shiftAmount < 0 && shiftAmount > -8 );
        if( !shiftValid ) continue;

        int tileX;
        for( tileX = 0; tileX < trogTilesW; tileX = tileX + 1 )
        {
            if( trogTileMap[ tileY ][ tileX ] == 0 ) continue;

            int blockScreenX = ( tileX * 8 ) - mapXScreenOrigin;
            if( blockScreenX + 8 <= 0 || blockScreenX >= TROG_SCREEN_WIDTH ) continue;

            trogResolveTileBytes( tileX, tileY );

            int col;
            for( col = 0; col < 8; col = col + 1 )
            {
                int screenX = blockScreenX + col;
                if( screenX < 0 || screenX >= TROG_SCREEN_WIDTH ) continue;

                int blockDataByte = trogTileCacheBytes[ col ];
                int shifted;
                if( shiftAmount == 0 ) shifted = blockDataByte;
                else if( shiftAmount > 0 ) shifted = blockDataByte >> shiftAmount;
                else shifted = ( blockDataByte << ( -shiftAmount ) ) & 0xFF;

                trogTileRowBuffer[ screenX ] = trogTileRowBuffer[ screenX ] | shifted;
            }
        }
    }
}

// Player + attacking-enemy overlay icons, composited once per page (8x/
// frame) instead of once per pixel (1024x/frame) - matching the same
// "self-gated call still costs a full call every time it's invoked"
// lesson already found and fixed in Jump Slime's own port earlier this
// session (and Arkanoid/Bert/Tris/Trick/Morpion before that) - applied
// proactively here from the start rather than waiting for a CPU report,
// since Jump Slime's own experience showed this exact shape (a handful of
// small sprites called unconditionally across all 1024 pixels/frame) is
// enough on its own to peg the CPU at 100%.
int[128] trogOverlayRowBuffer;
int[5] trogEnmScreenX;
int[5] trogEnmScreenY;
int[5] trogEnmOffsetX;
int[5] trogEnmOffsetY;

void trogCalcEnmDraw( int mapXScreenOrigin, int mapYScreenOrigin )
{
    int i;
    for( i = 0; i < TROG_MAX_ENEMIES; i = i + 1 )
    {
        trogEnmScreenX[ i ] = -1;
        if( trogEnemies[ i ].isAlive )
        {
            int enmX = -1, enmY = -1;
            if( trogFindTile( TROG_TILE_ENM1 + ( i * 10 ), &enmX, &enmY ) )
            {
                trogEnmScreenX[ i ] = ( enmX * TROG_ONETIME_STEP ) - mapXScreenOrigin;
                trogEnmScreenY[ i ] = ( enmY * TROG_ONETIME_STEP ) - mapYScreenOrigin;

                int diffX = trogMapX / TROG_ONETIME_STEP - enmX;
                int diffY = trogMapY / TROG_ONETIME_STEP - enmY;

                if( abs( diffX ) > abs( diffY ) )
                {
                    if( diffX > 0 ) trogEnmOffsetX[ i ] = 6; else trogEnmOffsetX[ i ] = -6;
                    trogEnmOffsetY[ i ] = 0;
                }
                else
                {
                    trogEnmOffsetX[ i ] = 0;
                    if( diffY > 0 ) trogEnmOffsetY[ i ] = 6; else trogEnmOffsetY[ i ] = -6;
                }
            }
        }
    }
}

void trogCompositeOverlayRow( int page, int nowAtk, int cmp )
{
    int i;
    for( i = 0; i < TROG_SCREEN_WIDTH; i = i + 1 ) trogOverlayRowBuffer[ i ] = 0;

    int playerX = TROG_CHAR_X;
    int playerY = TROG_CHAR_Y;
    int* playerSprite = trogMyChFrame( cmp, ( trogCnt / TROG_CNTCYCLE ) % 2 );
    if( nowAtk > 1 )
    {
        int offsetX, offsetY;
        trogGetCharacterOffset( cmp, &offsetX, &offsetY, 4 );
        playerX = playerX + offsetX;
        playerY = playerY + offsetY;
        playerSprite = trogMyAtkFrame( cmp );
    }

    int playerLine = trogRecupeLineY( playerY );
    if( page == playerLine || page == playerLine + 1 )
    {
        int col;
        for( col = playerX; col <= playerX + 7; col = col + 1 )
        {
            if( col >= 0 && col < TROG_SCREEN_WIDTH )
              trogOverlayRowBuffer[ col ] = trogOverlayRowBuffer[ col ] | trogBlitzSprite( playerX, playerY, col, page, 0, playerSprite );
        }
    }

    for( i = 0; i < TROG_MAX_ENEMIES; i = i + 1 )
    {
        if( trogEnemies[ i ].isAlive && trogEnemies[ i ].nowEnmAtk > 2 && trogEnemies[ i ].nowEnmAtk < 5 && trogEnmScreenX[ i ] != -1 )
        {
            int ex = trogEnmScreenX[ i ] + trogEnmOffsetX[ i ];
            int ey = trogEnmScreenY[ i ] + trogEnmOffsetY[ i ];
            int enemyLine = trogRecupeLineY( ey );
            if( page == enemyLine || page == enemyLine + 1 )
            {
                int col;
                for( col = ex; col <= ex + 7; col = col + 1 )
                {
                    if( col >= 0 && col < TROG_SCREEN_WIDTH )
                      trogOverlayRowBuffer[ col ] = trogOverlayRowBuffer[ col ] | trogBlitzSprite( ex, ey, col, page, 0, trogOneEnm );
                }
            }
        }
    }
}

void trogRenderStage()
{
    md_beginFrame();
    trogStageDisp.xStart = TROG_SCREEN_WIDTH / 2;
    trogStageDisp.xEnd = TROG_SCREEN_WIDTH / 2 + 8;

    int page, x;
    for( page = 0; page < 8; page = page + 1 )
    {
        for( x = 0; x < TROG_SCREEN_WIDTH; x = x + 1 )
        {
            int displayByte = 0x00;
            if( page == 3 )
            {
                if( trogNowStageNo == TROG_CLEAR_FLOOR )
                  displayByte = displayByte | trogSpeedBlitz( TROG_SCREEN_WIDTH / 2 - 4, x, 6, trogMiniYN ); // Fin
                else if( trogNowHp == 0 )
                  displayByte = displayByte | trogSpeedBlitz( TROG_SCREEN_WIDTH / 2 - 4, x, 3, trogMiniYN ); // DIE
                else if( trogNowHp == 99 )
                  displayByte = displayByte | trogSpeedBlitz( TROG_SCREEN_WIDTH / 2 - 12, x, 0, trogMiniTitle ); // Title
                else
                {
                    trogMakeNumData( trogNowStageNo, &trogStageDisp );
                    displayByte = displayByte | trogGetByteNum( x, &trogStageDisp );
                    displayByte = displayByte | trogSpeedBlitz( TROG_SCREEN_WIDTH / 2 - 8, x, 5, trogMiniYN ); // Fl.
                }
            }
            md_drawColumn( x, page, displayByte );
        }
    }
}

void trogRenderCave()
{
    md_beginFrame();

    int startTire = TROG_SCREEN_WIDTH - 16;
    trogStageDisp.xStart = TROG_SCREEN_WIDTH - 7;
    trogStageDisp.xEnd = TROG_SCREEN_WIDTH;
    trogHpDisp.xStart = trogStageDisp.xStart;
    trogHpDisp.xEnd = trogStageDisp.xEnd;
    trogMakeNumData( trogNowStageNo, &trogStageDisp );
    trogMakeNumData( trogNowHp, &trogHpDisp );

    int mapXScreenOrigin = trogMapX - TROG_CHAR_X;
    int mapYScreenOrigin = trogMapY - TROG_CHAR_Y;
    if( trogMoveDelay >= 2 )
    {
        if( trogCmp == 2 ) mapXScreenOrigin = mapXScreenOrigin + 4;
        else if( trogCmp == 3 ) mapXScreenOrigin = mapXScreenOrigin - 4;
        if( trogCmp == 0 ) mapYScreenOrigin = mapYScreenOrigin - 4;
        else if( trogCmp == 1 ) mapYScreenOrigin = mapYScreenOrigin + 4;
    }

    trogCalcEnmDraw( mapXScreenOrigin, mapYScreenOrigin );

    int page, x;
    for( page = 0; page < 8; page = page + 1 )
    {
        trogCompositeOverlayRow( page, trogNowAtk, trogCmp );
        trogCompositeTileRow( page, mapXScreenOrigin, mapYScreenOrigin );

        for( x = 0; x < TROG_SCREEN_WIDTH; x = x + 1 )
        {
            int displayByte = 0x00;
            if( page == 0 && x >= startTire && x <= trogStageDisp.xEnd )
            {
                displayByte = displayByte | trogSpeedBlitz( startTire + 1, x, 5, trogMiniYN ); // Fl.
                displayByte = displayByte | trogGetByteNum( x, &trogStageDisp );
            }
            else if( page == 7 && x >= startTire && x <= trogHpDisp.xEnd )
            {
                displayByte = displayByte | trogSpeedBlitz( startTire + 1, x, 2, trogMiniYN ); // HP
                displayByte = displayByte | trogGetByteNum( x, &trogHpDisp );
            }
            else if( ( page == 6 || page == 7 ) && ( trogStatusFloor == 2 || trogStatusFloor == 3 ) )
            {
                if( page == 7 )
                {
                    displayByte = displayByte | trogSpeedBlitz( 52 - 8, x, 4, trogMiniYN ); // GO
                    displayByte = displayByte | trogSpeedBlitz( 52 + 2, x, 1, trogMiniTitle );
                    displayByte = displayByte | trogSpeedBlitz( 80, x, trogStatusFloor - 2, trogMiniYN );
                }
            }
            else
            {
                displayByte = displayByte | trogTileRowBuffer[ x ];
                displayByte = displayByte | trogOverlayRowBuffer[ x ];
            }
            md_drawColumn( x, page, displayByte );
        }
    }
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

// Every state falls through to trogRenderStage()/trogRenderCave()
// unconditionally at the end of gameTinyRoG_update() (no dirty-flag skip
// anywhere in this port), so NULL is the correct addGame() hook, matching
// every other confirmed-correct precedent in this project.

void gameTinyRoG_init()
{
    InitTinyJoypad();
    trogNowHp = 99;
    trogNowStageNo = 0;
    trogState = TROG_STATE_STAGE;
    trogPrevFire = 0;
    trogCnt = 0;
}

void gameTinyRoG_update()
{
    trogTickSkipCounter = trogTickSkipCounter + 1;
    if( trogTickSkipCounter < TROG_TICK_DIVISOR )
      return;
    trogTickSkipCounter = 0;

    trogCnt = trogCnt + 1;
    if( trogCnt >= 100 ) trogCnt = 0;

    int fireNow = isFirePressed();
    bool fireJustPressed = fireNow && !trogPrevFire;

    if( trogState == TROG_STATE_STAGE )
    {
        // matches upstream exactly: cnt is clamped unconditionally while
        // in this state (not just while dead/cleared) - the fire-press
        // branch below is what actually restricts the clamp's own real
        // effect to the death/ending screens, by only reacting to
        // trogCnt==TROG_WAIT_GAMEOVER when trogNowHp==0 or the floor was
        // cleared. An earlier draft mistakenly added an extra
        // `trogNowStageNo >= TROG_WAIT_GAMEOVER` condition here that
        // upstream doesn't have - since nowStageNo is normally 1-30, that
        // condition was almost always false, so trogCnt would just keep
        // cycling 0-99 forever instead of locking at 50, meaning a fire
        // press on the DIE screen would only work on the rare frame where
        // trogCnt happened to land on exactly 50. Caught by inspection
        // while answering a direct question about death-screen behavior,
        // before ever verifying it live.
        if( trogCnt >= TROG_WAIT_GAMEOVER ) trogCnt = TROG_WAIT_GAMEOVER;
        if( fireJustPressed )
        {
            if( trogNowHp == 0 || trogNowStageNo == TROG_CLEAR_FLOOR )
            {
                if( trogCnt == TROG_WAIT_GAMEOVER ) { trogNowHp = 99; trogNowStageNo = 1; }
            }
            else if( trogNowHp == 99 )
            {
                trogHandleInit();
            }
            else
            {
                trogHandleGoCave();
            }
        }
    }
    else if( trogState == TROG_STATE_CAVE )
    {
        bool isEnmAttacking = false;
        int i;
        for( i = 0; i < TROG_MAX_ENEMIES; i = i + 1 )
        {
            if( trogEnemies[ i ].isAlive && trogEnemies[ i ].nowEnmAtk > 0 )
            {
                isEnmAttacking = true;
                trogEnemies[ i ].nowEnmAtk = trogEnemies[ i ].nowEnmAtk - 1;
                if( trogEnemies[ i ].nowEnmAtk == 1 )
                {
                    trogNowHp = trogNowHp - 1;
                    trogMakeNumData( trogNowHp, &trogHpDisp );
                    Sound( 50, 20 );
                    if( trogNowHp == 0 )
                    {
                        trogState = TROG_STATE_STAGE;
                        trogCnt = 0;
                        trogPrevFire = fireNow;
                        trogRenderStage();
                        return;
                    }
                }
            }
        }

        if( trogMoveDelay > 0 ) trogMoveDelay = trogMoveDelay - 1;
        if( trogNowAtk > 0 ) trogNowAtk = trogNowAtk - 1;
        if( trogNowAtk == 2 ) trogDamageEnemy();
        if( trogMoveDelay == 1 || trogNowAtk == 1 ) trogActEnemy();

        if( trogMoveDelay == 0 && trogNowAtk == 0 && !isEnmAttacking )
        {
            trogHandleTileEvt();
            if( trogStatusFloor == 2 || trogStatusFloor == 3 )
            {
                trogHandleStai( fireJustPressed );
            }
            else
            {
                int inputCmp = 5;
                if( isRightPressed() ) { trogCmp = 3; inputCmp = trogCmp; }
                else if( isLeftPressed() ) { trogCmp = 2; inputCmp = trogCmp; }
                else if( isDownPressed() ) { trogCmp = 0; inputCmp = trogCmp; }
                else if( isUpPressed() ) { trogCmp = 1; inputCmp = trogCmp; }
                else if( fireJustPressed )
                {
                    trogNowAtk = 7;
                }

                if( inputCmp != 5 )
                {
                    int dx = 0, dy = 0;
                    if( inputCmp == 0 ) dy = 1;
                    else if( inputCmp == 1 ) dy = -1;
                    else if( inputCmp == 2 ) dx = -1;
                    else if( inputCmp == 3 ) dx = 1;

                    int nextX = trogMapX + dx * TROG_ONETIME_STEP;
                    int nextY = trogMapY + dy * TROG_ONETIME_STEP;
                    if( trogIsMovePossible( nextX, nextY ) )
                    {
                        trogMapX = nextX;
                        trogMapY = nextY;
                        trogMoveDelay = TROG_WAIT_FLM;
                    }
                }
            }
        }
    }

    trogPrevFire = fireNow;

    if( trogState == TROG_STATE_STAGE )
      trogRenderStage();
    else
      trogRenderCave();
}

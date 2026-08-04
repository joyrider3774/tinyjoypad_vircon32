#include "avrCompat.h"
#include "obonoCoreShim.h"
#include "machineDependent.h"
#include "time.h"

// Sized for the neediest game ported so far, not just NumberPlace (which
// only ever needed 5): t2048's 4x4 board calls setSprite() with indices up
// to 15 (one sprite per occupied cell), and setSprite()/moveSprite()/
// clearSprite() do no bounds checking - an index at or past SPRITES here
// silently overruns into whatever's declared next (the string[] array),
// corrupting unrelated state (this was a real bug: t2048's SCORE label
// rendered at the wrong X position, and bottom-board-row tiles glitched,
// because sprite writes for cell indices 8+ were landing in string[]'s
// memory instead). Bump this again if a future game's own board/sprite
// count exceeds it - there's no cost to headroom here beyond a few words
// of RAM.
#define SPRITES 20
#define STRINGS 10

struct SpriteT
{
    int x, y, w, h, color;
    int* pBitmap;
};

struct StringT
{
    int x;
    int color;
    int* pString;
};

bool isInvalid;

SpriteT[SPRITES] sprite;
StringT[STRINGS] string;
int buttonState = 0;
int lastButtonState = 0;
int justPressedState = 0;
int[ WIDTH + 1 ] wireBuffer;

// -----------------------------------------------------------------------------
//   Core
// -----------------------------------------------------------------------------

void initCore()
{
    isInvalid = true;
    initSprites();
    initStrings();
}

void obonoCoreShimForceRedraw()
{
    isInvalid = true;
}

void clearScreenBuffer()
{
    for( int i = 1; i <= WIDTH; i++ )
      wireBuffer[ i ] = 0;
}

void refreshScreen( DrawFunc* func )
{
    if( !isInvalid )
      return;

    md_beginFrame();

    for( int y = 0; y < HEIGHT; y += 8 )
    {
        if( func != NULL )
          func( y, &wireBuffer[ 1 ] );
        else
          clearScreenBuffer();

        drawSprites( y );
        drawStrings( y );

        int page = y / 8;
        for( int x = 0; x < WIDTH; x++ )
          md_drawColumn( x, page, wireBuffer[ 1 + x ] );
    }

    isInvalid = false;
}

// -----------------------------------------------------------------------------
//   Button state
// -----------------------------------------------------------------------------

// `tickWindow` is how many real Vircon32 frames pass between two calls to
// this function for the calling game (1 for an unthrottled 60fps game, 2
// for a halved-rate 30fps one, 3 for NumberPlace's 20fps) - see
// md_recentlyPressed()'s own comment in machineDependent.h. Passing 1
// reduces justPressedState to a plain single-frame edge, identical to the
// original XOR-based isButtonDown behavior this replaced.
void updateButtonState( int tickWindow )
{
    lastButtonState = buttonState;
    buttonState = 0;
    if( md_inputLeft()  ) buttonState |= LEFT_BUTTON;
    if( md_inputRight() ) buttonState |= RIGHT_BUTTON;
    if( md_inputDown()  ) buttonState |= DOWN_BUTTON;
    if( md_inputUp()    ) buttonState |= UP_BUTTON;
    if( md_inputFire()  ) buttonState |= A_BUTTON;

    justPressedState = 0;
    if( md_recentlyPressed( md_inputLeftFrames(),  tickWindow ) ) justPressedState |= LEFT_BUTTON;
    if( md_recentlyPressed( md_inputRightFrames(), tickWindow ) ) justPressedState |= RIGHT_BUTTON;
    if( md_recentlyPressed( md_inputDownFrames(),  tickWindow ) ) justPressedState |= DOWN_BUTTON;
    if( md_recentlyPressed( md_inputUpFrames(),    tickWindow ) ) justPressedState |= UP_BUTTON;
    if( md_recentlyPressed( md_inputFireFrames(),  tickWindow ) ) justPressedState |= A_BUTTON;
}

bool isButtonPressed( int b )
{
    return ( buttonState & b ) != 0;
}

// "Just pressed since our last logic tick" - safe under frame-skipped
// (reduced-fps) games, unlike a plain buttonState/lastButtonState XOR,
// which only catches a press still held at the moment we happen to
// sample it. See updateButtonState()'s own comment.
bool isButtonDown( int b )
{
    return ( justPressedState & b ) != 0;
}

bool isButtonUp( int b )
{
    return ( ~buttonState & lastButtonState & b ) != 0;
}

// -----------------------------------------------------------------------------
//   Sprites
// -----------------------------------------------------------------------------

void initSprites()
{
    for( int i = 0; i < SPRITES; i++ )
      sprite[ i ].pBitmap = NULL;
}

void setSprite( int idx, int x, int y, int* pBitmap, int w, int h, int color )
{
    sprite[ idx ].x = x;
    sprite[ idx ].y = y;
    sprite[ idx ].w = w;
    sprite[ idx ].h = h;
    sprite[ idx ].color = color;
    sprite[ idx ].pBitmap = pBitmap;
}

void moveSprite( int idx, int x, int y )
{
    sprite[ idx ].x = x;
    sprite[ idx ].y = y;
}

void clearSprite( int idx )
{
    sprite[ idx ].pBitmap = NULL;
}

void drawSprites( int y )
{
    for( int i = 0; i < SPRITES; i++ )
    {
        SpriteT* p = &sprite[ i ];
        if( p->pBitmap == NULL || p->y > y + 7 || p->y + p->h <= y ||
            p->x >= WIDTH || p->x + p->w <= 0 )
          continue;

        int row = ( y + 7 - p->y ) >> 3;
        int odd = p->y & 7;
        int x = p->x;
        int* pSrc = p->pBitmap + p->w * row;
        int destBase = 1 + x;

        int w = p->w;
        while( w > 0 )
        {
            if( x >= 0 && x < WIDTH )
            {
                int ptn = 0;
                if( row < ( ( p->h + 7 ) >> 3 ) )
                  ptn |= pgm_read_byte( pSrc ) << odd;
                if( odd > 0 && row > 0 )
                  ptn |= pgm_read_byte( pSrc - p->w ) >> ( 8 - odd );

                if( p->color == BLACK )
                  wireBuffer[ destBase ] &= ~ptn;
                else if( p->color == WHITE )
                  wireBuffer[ destBase ] |= ptn;
                else if( p->color == INVERT )
                  wireBuffer[ destBase ] ^= ptn;
                else
                  wireBuffer[ destBase ] = ptn;
            }

            w--;
            x++;
            pSrc++;
            destBase++;
        }
    }
}

// -----------------------------------------------------------------------------
//   Strings
// -----------------------------------------------------------------------------

// 6x6 font, '!' (0x21) through '_' (0x5F), one 32-bit word per glyph (6
// columns x 6 rows, 5 bits used per column - see drawStrings()). Widened
// from NumberPlace/t2048's original '-' (0x2D) through 'Z' (0x5A) range to
// hollowseeker's superset (its core.cpp's own imgFont[] adds 12 glyphs
// before and 5 after that same middle section, byte-for-byte identical in
// the overlap) - one shared table covers every obonoCoreShim game's
// strings now, the same way md_drawColumn()'s byte-masking fix and the
// SPRITES capacity bump each ended up covering every game once fixed in
// the shim rather than per-game. Functional bitmap data, not creative text.
int[63] imgFont = {
    0x00017000, 0x000C00C0, 0x0A7CA7CA, 0x0855F542, 0x19484253, 0x1251F55E, 0x00003000,
    0x00452700, 0x001C9440, 0x0519F314, 0x0411F104, 0x00000420, 0x04104104, 0x00000400, 0x01084210,
    0x0F45145E, 0x0001F040, 0x13555559, 0x0D5D5551, 0x087C928C, 0x0D555557, 0x0D55555E, 0x010C5251,
    0x0F55555E, 0x0F555556, 0x0000A000, 0x0000A400, 0x0028C200, 0x0028A280, 0x00086280, 0x000D5040,
    0x0755745E, 0x1F24929C, 0x0D5D555F, 0x1145149C, 0x0725145F, 0x1155555F, 0x0114515F, 0x1D55545E,
    0x1F10411F, 0x0045F440, 0x07210410, 0x1D18411F, 0x1041041F, 0x1F04F05E, 0x1F04109C, 0x0F45545E,
    0x0314925F, 0x1F45D45E, 0x1B34925F, 0x0D555556, 0x0105F041, 0x0721041F, 0x0108421F, 0x0F41E41F,
    0x1D184317, 0x0109C107, 0x114D5651, 0x0045F000, 0x0001F000, 0x0001F440, 0x000C1080, 0x10410410
};

void initStrings()
{
    for( int i = 0; i < STRINGS; i++ )
      string[ i ].pString = NULL;
}

void setString( int idx, int x, int* pString, int color )
{
    string[ idx ].x = x;
    string[ idx ].color = color;
    string[ idx ].pString = pString;
}

void clearString( int idx )
{
    string[ idx ].pString = NULL;
}

void drawStrings( int y )
{
    int yy = y + 4;
    int stringIndex = yy / FONT_H - 1;
    int shift = -( yy % FONT_H );

    while( shift < 8 )
    {
        if( stringIndex >= 0 && stringIndex < STRINGS && string[ stringIndex ].pString != NULL )
        {
            StringT* p = &string[ stringIndex ];
            int x = p->x;
            int* pChar = p->pString;
            int c = *pChar;
            pChar++;

            while( c != 0 )
            {
                if( c <= ' ' || c > '_' )
                {
                    x += FONT_W;
                }
                else
                {
                    int glyph = imgFont[ c - '!' ];
                    int destBase = 1 + x;

                    int w = FONT_W;
                    while( w > 0 )
                    {
                        if( x >= 0 && x < WIDTH )
                        {
                            int ptn = glyph & 0x3F;
                            if( shift > 0 )
                              ptn <<= shift;
                            if( shift < 0 )
                              ptn >>= -shift;

                            if( p->color != 0 )
                              wireBuffer[ destBase ] |= ptn;
                            else
                              wireBuffer[ destBase ] &= ~ptn;
                        }

                        w--;
                        x++;
                        glyph >>= FONT_H;
                        destBase++;
                    }
                }

                c = *pChar;
                pChar++;
            }
        }

        stringIndex++;
        shift += FONT_H;
    }
}

// -----------------------------------------------------------------------------
//   Sound
// -----------------------------------------------------------------------------

// The original drives a hardware timer interrupt to step through a score
// note-by-note in the background, independent of the main loop. Vircon32
// exposes no interrupts/ISRs to games, so the sequencer becomes an explicit
// per-frame poll instead: obonoCoreShim_updateSound(), called once per
// frame from portVircon32.c exactly like md_updateAudio(), advances to the
// next note once the current one's frame countdown reaches zero.
int scorePos = -1;
int* playingScore = NULL;
int scoreStopAtFrame = -1;

// Equal-tempered note frequency table for one octave (functional lookup
// data, identical role to upstream's noteFrquency[] - not creative content).
int[12] noteFrequency = {
    8372, 8870, 9397, 9956, 10548, 11175, 11840, 12544, 13290, 14080, 14917, 15804
};

void playTone( int frequency, int duration )
{
    playingScore = NULL;
    md_playTone( (float)frequency, (float)duration / 1000.0 );
}

void obonoCoreShimAdvanceScore()
{
    int note = playingScore[ scorePos ];
    scorePos++;

    if( bitRead( note, 7 ) )
    {
        playingScore = NULL;
        return;
    }

    int frequency = noteFrequency[ note % 12 ];
    int octaveShift = ( 131 - note ) / 12;
    frequency = frequency >> octaveShift;

    int durationMs = playingScore[ scorePos ] * 10;
    scorePos++;

    md_playTone( (float)frequency, (float)durationMs / 1000.0 );
    scoreStopAtFrame = get_frame_counter() + (int)( ( (float)durationMs / 1000.0 ) * frames_per_second );
}

void playScore( int* pScore )
{
    playingScore = pScore;
    scorePos = 0;
    if( pScore != NULL )
      obonoCoreShimAdvanceScore();
}

void obonoCoreShimUpdateSound()
{
    if( playingScore != NULL && get_frame_counter() >= scoreStopAtFrame )
      obonoCoreShimAdvanceScore();
}

// -----------------------------------------------------------------------------
//   Memory-card persistence - deferred (see obonoCoreShim.h)
// -----------------------------------------------------------------------------

bool loadRecord( int signature, int address, void* pRecord, int size )
{
    // Self-assign to silence "unused parameter" warnings - this dialect
    // has no (void)param; cast-to-void idiom to suppress them otherwise.
    signature = signature;
    address = address;
    pRecord = pRecord;
    size = size;
    return true; // "isRecordVirgin" - always report no saved record (yet)
}

void storeRecord( int signature, int address, void* pRecord, int size )
{
    // no-op for now - self-assigned to silence unused-parameter warnings
    signature = signature;
    address = address;
    pRecord = pRecord;
    size = size;
}

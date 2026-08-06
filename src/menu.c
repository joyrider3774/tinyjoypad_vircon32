#include "menu.h"
#include "machineDependent.h"
#include "video.h"
#include "string.h"

// Bumped 32->48 when Dino Game (the 33rd registered game) was silently
// dropped by addGame()'s own capacity guard below - then bumped again,
// 48->64, when Laser Pong (the 49th registered game) hit the exact same
// silent-drop bug the same way - gives more headroom this time
// specifically to avoid a third repeat of this same mistake.
#define MAX_GAMES 64

// How many entries fit in the vertical space between the list's start
// (y=140) and the bottom of the 360px-tall screen at 24px/row - matches
// exactly what already fit before paging was needed (9 games), so the
// on-screen layout doesn't shift for anyone already used to it.
#define GAMES_PER_PAGE 9

// Top of the game list/thumbnail area - the header lines above it
// (title + 2 hint lines) are centered independently of this.
#define LIST_AREA_TOP 140

// Centers a fixed-width-bios-font string horizontally on screen.
int menuCenteredX( int* text )
{
    return ( screen_width - strlen( text ) * bios_character_width ) / 2;
}

int gameCount = 0;
Game[MAX_GAMES] games;
int selection = 0;

// `games[]` stays in addGame()'s own registration order always - that's
// also the order assets/thumbnails.png's texture regions were baked in
// (region id == registration index, see md_drawGameThumbnail()), and it's
// what a launched game's init/update function pointers are looked up by
// (menu_getGame(), called from main()'s dispatch loop). Alphabetical
// sorting is purely a *display* concern, so it lives in a separate
// indirection array instead of reordering games[] itself: displayOrder[i]
// holds the original games[]/thumbnail index shown at display position i.
// `selection` walks display positions (0..gameCount-1, same range as
// before) - every place that used to index games[]/the thumbnail
// directly with `selection` must go through displayOrder[selection]
// instead, or it'll show/launch/thumbnail the wrong game the moment the
// alphabetical order differs from registration order.
int[MAX_GAMES] displayOrder;
bool displayOrderBuilt = false;

bool prevUp = false;
bool prevDown = false;
bool prevFire = false;
bool prevLeft = false;
bool prevRight = false;

void addGame( int* title, int* author, GameFunc* init, GameFunc* update, GameFunc* onResume )
{
    if( gameCount >= MAX_GAMES )
      return;

    games[ gameCount ].title = title;
    games[ gameCount ].author = author;
    games[ gameCount ].init = init;
    games[ gameCount ].update = update;
    games[ gameCount ].onResume = onResume;
    gameCount++;
}

Game* menu_getGame( int index )
{
    return &games[ index ];
}

// Selection sort on displayOrder (by games[].title) - gameCount is always
// a small handful of entries, so O(n^2) costs nothing measurable here.
void menu_buildDisplayOrder()
{
    for( int i = 0; i < gameCount; i++ )
      displayOrder[ i ] = i;

    for( int i = 0; i < gameCount - 1; i++ )
    {
        int best = i;
        for( int j = i + 1; j < gameCount; j++ )
          if( strcmp( games[ displayOrder[ j ] ].title, games[ displayOrder[ best ] ].title ) < 0 )
            best = j;

        if( best != i )
        {
            int tmp = displayOrder[ i ];
            displayOrder[ i ] = displayOrder[ best ];
            displayOrder[ best ] = tmp;
        }
    }

    displayOrderBuilt = true;
}

void menu_init()
{
    prevUp = false;
    prevDown = false;
    prevFire = false;
    prevLeft = false;
    prevRight = false;

    // Built once (addGames() has already run by the time menu_init() is
    // first called, and gameCount/games[] never change afterward) - not
    // redone on every return-to-menu.
    if( !displayOrderBuilt )
      menu_buildDisplayOrder();
}

int menu_update()
{
    bool up = md_inputUp();
    bool down = md_inputDown();
    bool fire = md_inputFire();
    bool left = md_inputLeft();
    bool right = md_inputRight();

    if( down && !prevDown )
    {
        selection++;
        if( selection >= gameCount )
          selection = 0;
    }
    if( up && !prevUp )
    {
        selection--;
        if( selection < 0 )
          selection = gameCount - 1;
    }

    int totalPages = ( gameCount + GAMES_PER_PAGE - 1 ) / GAMES_PER_PAGE;

    // LEFT/RIGHT jump a whole page at a time (wrapping past the last/first
    // page), same idea as UP/DOWN moving one entry at a time - modeled on
    // the sibling crisp-game-lib-portable_vircon32 project's own menu
    // paging, adapted to this menu's simpler 0-indexed/no-category shape.
    if( right && !prevRight )
    {
        int currentPage = selection / GAMES_PER_PAGE;
        currentPage++;
        if( currentPage >= totalPages )
          currentPage = 0;
        selection = currentPage * GAMES_PER_PAGE;
        if( selection >= gameCount )
          selection = gameCount - 1;
    }
    if( left && !prevLeft )
    {
        int currentPage = selection / GAMES_PER_PAGE;
        currentPage--;
        if( currentPage < 0 )
          currentPage = totalPages - 1;
        selection = currentPage * GAMES_PER_PAGE;
        if( selection >= gameCount )
          selection = gameCount - 1;
    }

    bool justFired = ( fire && !prevFire );

    prevUp = up;
    prevDown = down;
    prevFire = fire;
    prevLeft = left;
    prevRight = right;

    // ---- draw ----
    clear_screen( color_black );
    print_at( menuCenteredX( "TINYJOYPAD FOR VIRCON32" ), 40, "TINYJOYPAD FOR VIRCON32" );
    print_at( menuCenteredX( "UP/DOWN: SELECT     A: PLAY" ), 80, "UP/DOWN: SELECT     A: PLAY" );

    int currentPage = selection / GAMES_PER_PAGE;

    if( totalPages > 1 )
    {
        int[8] pageNumText;
        int[8] totalPagesText;
        int[48] pageHintText;
        itoa( currentPage + 1, pageNumText, 10 );
        itoa( totalPages, totalPagesText, 10 );
        strcpy( pageHintText, "LEFT/RIGHT: CHANGE PAGE " );
        strcat( pageHintText, pageNumText );
        strcat( pageHintText, "/" );
        strcat( pageHintText, totalPagesText );
        print_at( menuCenteredX( pageHintText ), 105, pageHintText );
    }

    int startIndex = currentPage * GAMES_PER_PAGE;

    int y = LIST_AREA_TOP;
    for( int i = 0; i < GAMES_PER_PAGE; i++ )
    {
        int idx = startIndex + i;
        if( idx >= gameCount )
          break;

        // Kept close to the left edge (small margin only) so the right
        // side of the screen stays free for the current game's thumbnail
        // screenshot (see THUMBNAIL_* below).
        int x = 60;
        if( idx == selection )
          x = 40;

        if( idx == selection )
          print_at( x, y, ">" );

        // Zero-padded "NN " position number (1-based on the whole
        // alphabetized list, not per-page) prepended to the title.
        int[8] numText;
        itoa( idx + 1, numText, 10 );
        int[64] labelText;
        if( idx + 1 < 10 )
          strcpy( labelText, "0" );
        else
          strcpy( labelText, "" );
        strcat( labelText, numText );
        strcat( labelText, ". " );
        strcat( labelText, games[ displayOrder[ idx ] ].title );

        print_at( x + 20, y, labelText );
        y += 24;
    }

    // Real gameplay screenshot of the currently-selected game, in the
    // margin freed up on the right by keeping the list itself close to
    // the left edge - switches immediately whenever the selection moves,
    // since it's just read straight off `selection` every frame. Centered
    // vertically (as a group with the "BY <author>" line below it) within
    // the list/selection area (LIST_AREA_TOP down to the bottom of the
    // screen) rather than top-aligned with the list. Indexed through
    // displayOrder[] like the title above - the thumbnail atlas is keyed
    // by registration index, not by alphabetical position.
    int selectedGameIndex = displayOrder[ selection ];
    if( selectedGameIndex < md_getThumbnailCount() )
    {
        int authorGapY = 8;
        int blockHeight = MD_THUMBNAIL_HEIGHT + authorGapY + bios_character_height;
        int blockY = LIST_AREA_TOP + ( ( screen_height - LIST_AREA_TOP ) - blockHeight ) / 2;

        md_drawGameThumbnail( selectedGameIndex, 340, blockY );

        int[32] authorText;
        strcpy( authorText, "BY " );
        strcat( authorText, games[ selectedGameIndex ].author );
        int authorX = 340 + ( MD_THUMBNAIL_WIDTH - strlen( authorText ) * bios_character_width ) / 2;
        print_at( authorX, blockY + MD_THUMBNAIL_HEIGHT + authorGapY, authorText );
    }

    if( justFired )
      return selectedGameIndex;

    return -1;
}

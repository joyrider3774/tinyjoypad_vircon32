#ifndef MENU_H
#define MENU_H

// Game-select menu. Deliberately simpler than crisp-game-lib-portable's
// menu.c/cglp.c pair (no custom glyph renderer, no per-character collision
// hit-testing) - this project draws menu text with Vircon32's own built-in
// BIOS font (print_at(), see video.h) instead, since there is no shared
// sprite-font engine here the way crisp-game-lib brings one.

typedef void(void) GameFunc;

struct Game
{
    int* title;
    // Original game's author/credit (e.g. "OBONO", "DANIEL C", "LORANDIL")
    // - shown as "BY <author>" under the menu's thumbnail screenshot.
    int* author;
    GameFunc* init;
    GameFunc* update;
    // Optional (pass 0/NULL if not needed) - called once when this game
    // resumes after being fully frozen for the quit-confirmation dialog
    // (see portVircon32.c's main()). Most games redraw their whole screen
    // unconditionally every update() call, so freezing/resuming them is
    // transparent - but a game that skips its own redraw entirely on
    // frames where nothing changed (a dirty-flag optimization, e.g. Tiny
    // Tris's attract screen) needs this hook to force that flag back to
    // true, or its next real update() could also skip drawing and leave
    // the dialog's pixels on screen instead of the game's own content.
    GameFunc* onResume;
};

extern int gameCount;

void addGame( int* title, int* author, GameFunc* init, GameFunc* update, GameFunc* onResume );
Game* menu_getGame( int index );
void menu_init();

// draws the menu and handles its own navigation input; returns the game
// just chosen (Fire/A pressed on it) this frame, or -1 if none was
int menu_update();

#endif

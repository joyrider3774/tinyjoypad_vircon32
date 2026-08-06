#include "menu.h"
#include "menuGameList.h"

void addGames()
{
    // NumberPlace/HollowSeeker/t2048 all share obonoCoreShim's own
    // refreshScreen(), which skips its whole draw call outright whenever
    // `isInvalid` is false - true most of the time on a static logo/title
    // screen, since nothing there re-invalidates it every frame the way
    // active gameplay does. Left alone, resuming one of these games from
    // the quit-confirmation dialog while sitting on such a screen could
    // leave the dialog's pixels on screen indefinitely (worse than Tris's
    // own version of this bug below, which self-corrects within a few
    // frames) - obonoCoreShimForceRedraw() (shared, since `isInvalid`
    // lives in the shim, not per-game) fixes it for all three at once.
    addGame( "NUMBERPLACE", "OBONO", &gameNumberPlace_init, &gameNumberPlace_update, &obonoCoreShimForceRedraw );
    // Credited to both names: the .ino's own header says "Programmer:
    // Daniel C 2018-2020, Enhancements: Sven B 2021" for this specific
    // v4.2 release - "Lorandil" (this file's earlier, imprecise single
    // credit) turned out to be Sven B's own GitHub/contact-email handle,
    // not a separate third person (see the Licensing section).
    addGame( "TINY INVADERS", "DANIEL C / SVEN B", &gameTinyInvaders_init, &gameTinyInvaders_update, NULL );
    addGame( "2048", "OBONO", &gameT2048_init, &gameT2048_update, &obonoCoreShimForceRedraw );
    addGame( "HOLLOWSEEKER", "OBONO", &gameHollowSeeker_init, &gameHollowSeeker_update, &obonoCoreShimForceRedraw );
    addGame( "TINY PINBALL", "DANIEL C", &gameTinyPinball_init, &gameTinyPinball_update, NULL );
    addGame( "TINY PACMAN", "DANIEL C", &gameTinyPacman_init, &gameTinyPacman_update, NULL );
    addGame( "TINY BOMBER", "DANIEL C", &gameTinyBomber_init, &gameTinyBomber_update, NULL );
    addGame( "TINY DOC", "DANIEL C", &gameTinyDoc_init, &gameTinyDoc_update, NULL );
    addGame( "TINY BERT", "DANIEL C", &gameTinyBert_init, &gameTinyBert_update, NULL );
    // Tiny Tris's attract screen skips its own redraw entirely on frames
    // where nothing changed (trisAttractDirty) - same class of bug as the
    // obono games above, just game-local instead of shim-shared.
    addGame( "TINY TRIS", "DANIEL C", &gameTinyTris_init, &gameTinyTris_update, &gameTinyTris_forceRedraw );
    addGame( "TINY ARKANOID", "DANIEL C", &gameTinyArkanoid_init, &gameTinyArkanoid_update, NULL );
    // Tiny Trick's title screen only redraws on two exact blink-timer
    // values (same shape as Tiny Tris's own attract screen) - same
    // onResume treatment for the same reason.
    addGame( "TINY TRICK", "DANIEL C", &gameTinyTrick_init, &gameTinyTrick_update, &gameTinyTrick_forceRedraw );
    // Credited to both names: the .ino's own header says "Programmer:
    // Sven B", contact address Lorandil@gmx.de - the same contact address
    // TinyDungeon's own header uses for its own "Sven B"-credited author,
    // confirming (not just guessing) that "Lorandil" is Sven B's own
    // GitHub/online handle rather than a separate collaborator.
    // Tiny Minez: audited directly - every state branch in its own update()
    // calls tmzRenderImage() unconditionally, so NULL here is confirmed
    // correct, not just an oversight.
    addGame( "TINY MINEZ", "SVEN B / LORANDIL", &gameTinyMinez_init, &gameTinyMinez_update, NULL );
    // Tiny Missile: its only redraw skip is the global TMIS_TICK_DIVISOR=3
    // throttle, which self-corrects within 3 real frames regardless of
    // dialog interference - negligible enough to leave as NULL rather
    // than add a hook for an effectively imperceptible case.
    addGame( "TINY MISSILE", "DANIEL C", &gameTinyMissile_init, &gameTinyMissile_update, NULL );
    // Tiny Bike/Arena/Gilbert were all ported after this dialog feature
    // existed and were never audited for it until a direct user report -
    // each has at least one wait state with no timer of its own
    // (BIK_STATE_WAIT_RELEASE, AR_STATE_GAMEOVER_WAIT_RELEASE,
    // GILB_STATE_TITLE_WAIT) that would otherwise leave the dialog's
    // pixels on screen indefinitely if cancelled from there.
    addGame( "TINY BIKE", "DANIEL C", &gameTinyBike_init, &gameTinyBike_update, &gameTinyBike_forceRedraw );
    addGame( "TINY ARENA", "DANIEL C", &gameTinyArena_init, &gameTinyArena_update, &gameTinyArena_forceRedraw );
    addGame( "TINY GILBERT", "DANIEL C", &gameTinyGilbert_init, &gameTinyGilbert_update, &gameTinyGilbert_forceRedraw );
    // Tiny Pipe: checked proactively against the onResume audit (see
    // CLAUDE.md/memory's own "re-audit against every future port" lesson)
    // before shipping, rather than waiting for another report -
    // TPIPE_STATE_INTRO_WAIT_RELEASE has no timer of its own.
    addGame( "TINY PIPE", "DANIEL C", &gameTinyPipe_init, &gameTinyPipe_update, &gameTinyPipe_forceRedraw );
    // Tiny Morpion: checked proactively against the onResume audit before
    // shipping - TMORPION_STATE_MENU_WAIT_RELEASE has no timer of its own.
    addGame( "TINY MORPION", "DANIEL C", &gameTinyMorpion_init, &gameTinyMorpion_update, &gameTinyMorpion_forceRedraw );
    // Tiny Plaque: checked proactively against the onResume audit before
    // shipping - TPLAQ_STATE_ATTRACT has no timer of its own.
    addGame( "TINY PLAQUE", "DANIEL C", &gameTinyPlaque_init, &gameTinyPlaque_update, &gameTinyPlaque_forceRedraw );
    // Tiny SQuest: checked proactively against the onResume audit before
    // shipping - TSQ_STATE_ATTRACT has no timer of its own.
    addGame( "TINY SQUEST", "DANIEL C", &gameTinySQuest_init, &gameTinySQuest_update, &gameTinySQuest_forceRedraw );
    // Tiny DDug: while waiting for the attract screen's confirm press to be
    // released (tddugAttractFireHeld), there is no timer at all and no
    // redraw happens - a genuine indefinite risk, same class as Arena's own
    // AR_STATE_GAMEOVER_WAIT_RELEASE - checked proactively before shipping.
    addGame( "TINY DDUG", "DANIEL C", &gameTinyDDug_init, &gameTinyDDug_update, &gameTinyDDug_forceRedraw );
    // Tiny Lander: all of its own wait states (level-clear stars/tally,
    // death wait) are bounded (~1-2s) rather than indefinite, but the
    // forceRedraw hook is wired anyway for consistency with every other
    // port's own onResume audit.
    addGame( "TINY LANDER", "ROGER BUEHLER", &gameTinyLander_init, &gameTinyLander_update, &gameTinyLander_forceRedraw );
    // Wren Rollercoaster: not tinyJoypadShim/obonoCoreShim lineage by
    // name (gametiny's own hand-rolled driver family), but its A0/A3/
    // fire-pin thresholds and byte-per-column render model matched
    // exactly, so no new shim was needed - see the game's own header
    // comment. All of its own wait states are bounded, but forceRedraw
    // is wired anyway for consistency with every other port's audit.
    addGame( "WREN ROLLERCOASTER", "ANDY JACKSON", &gameWrenRollercoaster_init, &gameWrenRollercoaster_update, &gameWrenRollercoaster_forceRedraw );
    // Frogger: not tinyJoypadShim/obonoCoreShim lineage by name either, but
    // matched the same A0/A3/fire-pin thresholds as every other game here -
    // no new shim needed. FRG_STATE_ATTRACT has no timer of its own (waits
    // for a fire press), so forceRedraw is wired (not just for consistency
    // this time - a genuine indefinite-wait state).
    addGame( "FROGGER", "ANDY JACKSON", &gameFrogger_init, &gameFrogger_update, &gameFrogger_forceRedraw );
    // Pong (menu title "BAT BONANZA", not "PONG"): the .ino's own header
    // comment credits this as "Pong game by Andy Jackson", but the game's
    // own title screen literally spells out "BAT" / "BONANZA" on-screen
    // (ssd1306_char_f6x8 calls in the original source) - matching what a
    // player actually sees takes priority over the header's attribution
    // comment, which stays as its own credit ("ANDY JACKSON" below) rather
    // than the display title. Also not tinyJoypadShim/obonoCoreShim
    // lineage by name, same A0/A3/fire-pin thresholds as every other game
    // here - no new shim needed. PONG_STATE_ATTRACT has no timer of its
    // own (waits for a fire press), so forceRedraw is wired for a genuine
    // reason, not just consistency.
    addGame( "BAT BONANZA", "ANDY JACKSON", &gamePong_init, &gamePong_update, &gamePong_forceRedraw );
    // Stacker: one half of UFO_Stacker_Attiny's own combined cartridge
    // (the other half, UFO, is a separate future port) - split into its
    // own menu entry rather than replicating upstream's in-cartridge
    // sub-menu, matching this project's own precedent for combined-file
    // sources (Obono's TinyJoypadWorks monorepo -> 3 separate entries).
    // Also not tinyJoypadShim/obonoCoreShim lineage by name, same A0/A3/
    // fire-pin thresholds as every other game here - no new shim needed.
    // STK_STATE_ATTRACT has no timer of its own (waits for a fire press),
    // so forceRedraw is wired for a genuine reason, not just consistency.
    addGame( "STACKER", "ANDY JACKSON", &gameStacker_init, &gameStacker_update, &gameStacker_forceRedraw );
    // UFO: the other half of UFO_Stacker_Attiny's own combined cartridge
    // (Stacker, above, is the other half) - also not tinyJoypadShim/
    // obonoCoreShim lineage by name, same A0/A3/fire-pin thresholds as
    // every other game here - no new shim needed. UFO_STATE_ATTRACT has
    // no timer of its own (waits for a fire press), so forceRedraw is
    // wired for a genuine reason, not just consistency.
    addGame( "UFO", "ILYA TITOV", &gameUFO_init, &gameUFO_update, &gameUFO_forceRedraw );
    // Tiny Dungeon: every state in its own update() calls tdRenderImage()
    // unconditionally at the end (no dirty-flag skipping anywhere in this
    // port), so NULL here is confirmed correct, not an oversight.
    addGame( "TINY DUNGEON", "SVEN B / LORANDIL", &gameTinyDungeon_init, &gameTinyDungeon_update, NULL );
    addGame( "OROBOROS", "ILYA TITOV", &gameOroboros_init, &gameOroboros_update, &gameOroboros_forceRedraw );
    addGame( "RUN DUDE RUN", "ILYA TITOV", &gameRunDudeRun_init, &gameRunDudeRun_update, &gameRunDudeRun_forceRedraw );
    // Four in a Row: gameFourInRow_update() calls firoRenderImage()
    // unconditionally at the end of every single branch (no dirty-flag/
    // skip-redraw path anywhere in this port), so NULL here is confirmed
    // correct, not an oversight.
    addGame( "FOUR IN A ROW", "UNKNOWN", &gameFourInRow_init, &gameFourInRow_update, NULL );
    // Dino Game: gameDinoGame_update() calls dinoRenderImage() unconditionally
    // at the end regardless of state (dinoUpdateState()'s own early return
    // only exits that helper, not the outer update function), so NULL here
    // is confirmed correct, not an oversight.
    addGame( "DINO GAME", "TINY HANDHELD", &gameDinoGame_init, &gameDinoGame_update, NULL );
    // SnakeGame85: gameSnakeGame85_update() calls snkRenderImage()
    // unconditionally at the end of every single state branch (no dirty-
    // flag/skip-redraw path anywhere in this port), so NULL here is
    // confirmed correct, not an oversight.
    addGame( "SNAKEGAME85", "TEREZAZA", &gameSnakeGame85_init, &gameSnakeGame85_update, NULL );
    // Jump Slime: gameJumpSlime_update() calls jslmRender() unconditionally
    // at the end of every single state branch (no dirty-flag/skip-redraw
    // path anywhere in this port), so NULL here is confirmed correct, not
    // an oversight.
    addGame( "JUMP SLIME", "KONDOLAB", &gameJumpSlime_init, &gameJumpSlime_update, NULL );
    // TinyRoG: gameTinyRoG_update() calls trogRenderStage()/trogRenderCave()
    // unconditionally at the end of every single state branch (no dirty-
    // flag/skip-redraw path anywhere in this port), so NULL here is
    // confirmed correct, not an oversight.
    addGame( "TINYROG", "KONDOLAB", &gameTinyRoG_init, &gameTinyRoG_update, NULL );
    // TinY Fi: gameTinYFi_update() calls tfiRender() unconditionally at
    // the end of every single state branch (no dirty-flag/skip-redraw
    // path anywhere in this port), so NULL here is confirmed correct, not
    // an oversight.
    addGame( "TINY FI", "KONDOLAB", &gameTinYFi_init, &gameTinYFi_update, NULL );
    addGame( "BREAKOUT", "ILYA TITOV", &gameBreakout_init, &gameBreakout_update, &gameBreakout_forceRedraw );
    addGame( "SPACE ATTACK", "ANDY JACKSON", &gameSpaceAttack_init, &gameSpaceAttack_update, &gameSpaceAttack_forceRedraw );
    // Menu title deliberately avoids the trademarked falling-block puzzle
    // genre name this game is a clone of - the game's own attract screen
    // originally spelled it out via a plain font-rendered string (not
    // baked bitmap data), so that string was also changed in the source
    // (see gameFallingBlocks.c) rather than left as shipped. The menu
    // entry and every mention in this project's own documentation use
    // "Falling Blocks" instead, at direct user request.
    addGame( "FALLING BLOCKS", "ANDY JACKSON", &gameFallingBlocks_init, &gameFallingBlocks_update, &gameFallingBlocks_forceRedraw );
    // No onResume needed - gameTinyMania_update() unconditionally reaches
    // tmnTinyFlip() at the end of every single tick (both the attract and
    // playing branches), with no dirty-flag/skip-redraw path anywhere in
    // this port, so NULL here is correct by the same reasoning as Tiny Fi
    // above, not an oversight.
    addGame( "TINY MANIA", "DANIEL C", &gameTinyMania_init, &gameTinyMania_update, NULL );
    // Menu title/attract-screen text deliberately avoid the trademarked
    // genre name this game's own upstream title spells out - see
    // gameBlocksGold.c's own header comment (same treatment already
    // established for Falling Blocks above). gldState==GLD_STATE_ATTRACT
    // has no timer of its own (waits for a fire press) - forceRedraw is
    // wired for a genuine reason, matching Falling Blocks' own precedent.
    addGame( "BLOCKS GOLD", "ANDY JACKSON / JAROMAZ", &gameBlocksGold_init, &gameBlocksGold_update, &gameBlocksGold_forceRedraw );
}

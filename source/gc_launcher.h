#ifndef GC_LAUNCHER_H
#define GC_LAUNCHER_H

#include <SDL2/SDL.h>

#include <stdbool.h>

#define GC_MAX_GAMES 5

typedef struct
{
    const char *name;
    const char *iwadPath;
    bool available;
} gc_game_entry_t;

/*
 * Scan dvd:/ for supported IWADs.
 *
 * Returns the number of games found.
 */
int GC_LauncherScanGames(void);

/*
 * Display the launcher and wait for the player to select a game.
 *
 * Returns the game index, or -1 if the launcher exits.
 */
int GC_LauncherRun(SDL_Renderer *renderer);

/*
 * Return information about a launcher entry.
 */
const gc_game_entry_t *GC_LauncherGetGame(int index);

#endif
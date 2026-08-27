#ifndef GC_LAUNCHER_H
#define GC_LAUNCHER_H

#include <stdbool.h>

#include <SDL2/SDL.h>

typedef struct
{
    const char *iwadPath;
    const char *pwadPath;
} gc_launch_selection_t;

void GC_LauncherRunStoragePreflight(
    SDL_Renderer *renderer
);

bool GC_LauncherSelectGame(
    SDL_Renderer *renderer,
    gc_launch_selection_t *selection);

#endif

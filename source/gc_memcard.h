#ifndef DOOMCUBE_GC_MEMCARD_H
#define DOOMCUBE_GC_MEMCARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Legacy v2 storage knows five IWAD families.
 *
 * Save system v3 exposes Doom's six normal save slots. The stdio bridge
 * already names them doomsav0.dsg through doomsav5.dsg; the live backend
 * switch follows after production v3 container initialization is proven.
 */
#define GC_MEMCARD_GAME_COUNT 5
#define GC_MEMCARD_SAVE_SLOTS 6

typedef enum
{
    GC_SAVEGAME_DOOM1 = 0,
    GC_SAVEGAME_DOOM,
    GC_SAVEGAME_DOOM2,
    GC_SAVEGAME_TNT,
    GC_SAVEGAME_PLUTONIA
} gc_savegame_id_t;

void GC_MemoryCardSetGame(gc_savegame_id_t game);
bool GC_MemoryCardSetGameFromIWAD(const char *iwadPath);

/*
 * Select the complete DoomCube launch identity.
 *
 * v3 save storage keys saves by IWAD + optional PWAD instead of only by
 * the five historical IWAD families.
 *
 * This currently establishes the identity plumbing. Content
 * fingerprints are added by the v3 container implementation.
 */
void GC_MemoryCardSetLaunchIdentity(
    const char *iwadPath,
    const char *pwadPath
);

bool GC_MemoryCardInit(void);
void GC_MemoryCardShutdown(void);

bool GC_MemoryCardSaveExists(int slot);

bool GC_MemoryCardWriteSave(
    int slot,
    const void *data,
    size_t size
);

bool GC_MemoryCardReadSave(
    int slot,
    void *buffer,
    size_t bufferSize,
    size_t *actualSize
);

uint32_t GC_MemoryCardSaveTimestamp(int slot);

/*
 * Global DoomCube configuration storage.
 *
 * This region is shared by every IWAD and is intended for controls,
 * sensitivity, audio/video settings, etc.
 */
bool GC_MemoryCardWriteConfig(
    const void *data,
    size_t size
);

bool GC_MemoryCardReadConfig(
    void *buffer,
    size_t bufferSize,
    size_t *actualSize
);

#endif
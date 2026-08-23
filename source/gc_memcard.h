#ifndef DOOMCUBE_GC_MEMCARD_H
#define DOOMCUBE_GC_MEMCARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * DoomCube supports five IWAD families, with exactly one Doom save
 * slot per game.
 *
 * GC_MEMCARD_SAVE_SLOTS remains 1 so the existing gc_save_stdio.c
 * bridge recognizes only doomsav0.dsg.
 */
#define GC_MEMCARD_GAME_COUNT 5
#define GC_MEMCARD_SAVE_SLOTS 1

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
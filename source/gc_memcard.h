#ifndef DOOMCUBE_GC_MEMCARD_H
#define DOOMCUBE_GC_MEMCARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GC_MEMCARD_SAVE_SLOTS 3

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

#endif
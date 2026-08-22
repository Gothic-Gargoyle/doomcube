#ifndef DOOMCUBE_GC_MEMCARD_H
#define DOOMCUBE_GC_MEMCARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool GC_MemoryCardInit(void);
void GC_MemoryCardShutdown(void);

bool GC_MemoryCardSaveExists(void);

bool GC_MemoryCardWriteSave(
    const void *data,
    size_t size
);

bool GC_MemoryCardReadSave(
    void *buffer,
    size_t bufferSize,
    size_t *actualSize
);

uint32_t GC_MemoryCardSaveTimestamp(void);

#endif
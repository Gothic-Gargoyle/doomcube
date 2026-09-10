#ifndef DOOMCUBE_GC_CONFIG_H
#define DOOMCUBE_GC_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define GC_CONFIG_SNAPSHOT_CAPACITY 8192u

typedef struct
{
    unsigned char data[GC_CONFIG_SNAPSHOT_CAPACITY];
    size_t size;
} gc_config_snapshot_t;

bool GC_ConfigSave(
    const void *data,
    size_t size
);

bool GC_ConfigLoad(
    void *buffer,
    size_t bufferSize,
    size_t *actualSize
);


/*
 * Read-only launcher/configuration view of the existing global config blob.
 * These helpers do not bind Doom variables and never write the Memory Card.
 */
bool GC_ConfigSnapshotLoad(
    gc_config_snapshot_t *snapshot);

bool GC_ConfigSnapshotFindInt(
    const gc_config_snapshot_t *snapshot,
    const char *name,
    int *valueOut);

#endif
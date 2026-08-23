#ifndef DOOMCUBE_GC_CONFIG_H
#define DOOMCUBE_GC_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

bool GC_ConfigSave(
    const void *data,
    size_t size
);

bool GC_ConfigLoad(
    void *buffer,
    size_t bufferSize,
    size_t *actualSize
);

#endif
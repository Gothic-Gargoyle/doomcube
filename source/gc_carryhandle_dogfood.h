#ifndef DOOMCUBE_GC_CARRYHANDLE_DOGFOOD_H
#define DOOMCUBE_GC_CARRYHANDLE_DOGFOOD_H

#include <stdbool.h>
#include <stddef.h>


/*
 * Temporary CarryHandle dogfood bridge.
 *
 * Only logical Doom save slot 0 is routed through this adapter.
 * The existing gc_memcard backend remains responsible for every other
 * storage function while the CarryHandle path is being live-proven.
 */

void GC_CHDogfoodSetLaunchIdentity(
    const char *iwadPath,
    const char *pwadPath
);

/*
 * Validate and cache slot 0 once after launch identity is established.
 *
 * This deliberately moves the transaction-log scan out of Doom's
 * Save/Load menus. Subsequent reads are served from the verified cache.
 */
void GC_CHDogfoodPrimeSaveCache(void);

bool GC_CHDogfoodReadSave(
    int slot,
    void *buffer,
    size_t bufferSize,
    size_t *actualSize
);

bool GC_CHDogfoodWriteSave(
    int slot,
    const void *data,
    size_t size
);


#endif

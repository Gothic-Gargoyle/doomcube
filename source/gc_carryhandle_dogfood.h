#ifndef DOOMCUBE_GC_CARRYHANDLE_DOGFOOD_H
#define DOOMCUBE_GC_CARRYHANDLE_DOGFOOD_H

#include <stdbool.h>
#include <stddef.h>


/*
 * DoomCube CarryHandle persistence bridge.
 *
 * One fixed 64-block application-save container owns both:
 *
 *   - the global DoomCube configuration
 *   - one Doom save object per selected IWAD/PWAD launch identity
 *
 * Every launch identity uses the fixed doomsav0.dsg key.  The identity scope,
 * not a second Doom save-slot number, separates DOOM, DOOM II, PWADs, etc.
 * Global configuration uses an unscoped key in the same physical container.
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

/*
 * Create the one physical DoomCube application-save container.
 *
 * The caller owns the player-facing CREATE decision and obsolete-file
 * deletion policy.  This function creates only the CarryHandle container.
 */
bool GC_CHDogfoodCreateContainer(void);

bool GC_CHDogfoodReadConfig(
    void *buffer,
    size_t bufferSize,
    size_t *actualSize
);

bool GC_CHDogfoodWriteConfig(
    const void *data,
    size_t size
);

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

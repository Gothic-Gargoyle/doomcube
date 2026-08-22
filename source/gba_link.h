#ifndef DOOMCUBE_GBA_LINK_H
#define DOOMCUBE_GBA_LINK_H

#include <gccore.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int GBA_LinkBoot(
    int channel,
    const void *rom,
    size_t rom_size
);

#ifdef __cplusplus
}
#endif

#endif
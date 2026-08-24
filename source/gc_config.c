#include "gc_debug.h"

#include "gc_config.h"

#include "gc_memcard.h"

#include <ogcsys.h>

bool GC_ConfigSave(
    const void *data,
    size_t size)
{
    if (!data || size == 0)
    {
        DC_WARN(
            "DoomCube: refusing to save empty configuration\n");

        return false;
    }

    if (!GC_MemoryCardWriteConfig(
            data,
            size))
    {
        DC_WARN(
            "DoomCube: global configuration write failed\n");

        return false;
    }

    DC_LOG(
        "DoomCube: global configuration saved (%u bytes)\n",
        (unsigned int)size);

    return true;
}


bool GC_ConfigLoad(
    void *buffer,
    size_t bufferSize,
    size_t *actualSize)
{
    if (!buffer ||
        bufferSize == 0 ||
        !actualSize)
    {
        return false;
    }

    if (!GC_MemoryCardReadConfig(
            buffer,
            bufferSize,
            actualSize))
    {
        DC_DEBUG(
            "DoomCube: no global configuration found\n");

        return false;
    }

    DC_LOG(
        "DoomCube: global configuration loaded (%u bytes)\n",
        (unsigned int)*actualSize);

    return true;
}
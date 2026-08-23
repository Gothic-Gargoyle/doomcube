#include "gc_config.h"

#include "gc_memcard.h"

#include <ogcsys.h>

bool GC_ConfigSave(
    const void *data,
    size_t size)
{
    if (!data || size == 0)
    {
        SYS_Report(
            "DoomCube: refusing to save empty configuration\n");

        return false;
    }

    if (!GC_MemoryCardWriteConfig(
            data,
            size))
    {
        SYS_Report(
            "DoomCube: global configuration write failed\n");

        return false;
    }

    SYS_Report(
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
        SYS_Report(
            "DoomCube: no global configuration found\n");

        return false;
    }

    SYS_Report(
        "DoomCube: global configuration loaded (%u bytes)\n",
        (unsigned int)*actualSize);

    return true;
}
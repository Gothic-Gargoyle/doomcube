#include "gc_debug.h"

#include "gc_config.h"

#include "gc_memcard.h"

#include <ogcsys.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

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

    DC_DEBUG(
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

    DC_DEBUG(
        "DoomCube: global configuration loaded (%u bytes)\n",
        (unsigned int)*actualSize);

    return true;
}


static bool GC_ConfigParseSnapshotInt(
    const unsigned char *text,
    size_t length,
    int *valueOut)
{
    char buffer[32];
    char *end;
    long value;

    while (length > 0 &&
           (*text == ' ' || *text == '\t'))
    {
        ++text;
        --length;
    }

    while (length > 0 &&
           (text[length - 1] == ' ' ||
            text[length - 1] == '\t' ||
            text[length - 1] == '\r'))
    {
        --length;
    }

    if (length == 0 ||
        length >= sizeof(buffer) ||
        valueOut == NULL)
    {
        return false;
    }

    memcpy(
        buffer,
        text,
        length);

    buffer[length] = '\0';

    errno = 0;

    value =
        strtol(
            buffer,
            &end,
            10);

    if (errno == ERANGE ||
        end == buffer ||
        *end != '\0' ||
        value < INT_MIN ||
        value > INT_MAX)
    {
        return false;
    }

    *valueOut =
        (int)value;

    return true;
}


bool GC_ConfigSnapshotLoad(
    gc_config_snapshot_t *snapshot)
{
    size_t size = 0;

    if (snapshot == NULL)
    {
        return false;
    }

    snapshot->size = 0;

    if (!GC_ConfigLoad(
            snapshot->data,
            sizeof(snapshot->data),
            &size))
    {
        return false;
    }

    if (size > sizeof(snapshot->data))
    {
        DC_WARN(
            "DoomCube: impossible global config snapshot size: %u > %u\n",
            (unsigned int)size,
            (unsigned int)sizeof(snapshot->data));

        return false;
    }

    snapshot->size =
        size;

    return true;
}


bool GC_ConfigSnapshotFindInt(
    const gc_config_snapshot_t *snapshot,
    const char *name,
    int *valueOut)
{
    size_t cursor;
    size_t nameLength;
    bool found = false;

    if (snapshot == NULL ||
        name == NULL ||
        *name == '\0' ||
        valueOut == NULL ||
        snapshot->size > sizeof(snapshot->data))
    {
        return false;
    }

    nameLength =
        strlen(name);

    cursor = 0;

    while (cursor < snapshot->size)
    {
        size_t lineStart =
            cursor;

        size_t lineEnd;
        size_t valueStart;
        int parsedValue;

        while (cursor < snapshot->size &&
               snapshot->data[cursor] != '\n')
        {
            ++cursor;
        }

        lineEnd =
            cursor;

        if (cursor < snapshot->size &&
            snapshot->data[cursor] == '\n')
        {
            ++cursor;
        }

        if (lineEnd > lineStart &&
            snapshot->data[lineEnd - 1] == '\r')
        {
            --lineEnd;
        }

        if (lineEnd - lineStart <= nameLength)
        {
            continue;
        }

        if (memcmp(
                snapshot->data + lineStart,
                name,
                nameLength) != 0)
        {
            continue;
        }

        valueStart =
            lineStart + nameLength;

        if (snapshot->data[valueStart] != ' ' &&
            snapshot->data[valueStart] != '\t')
        {
            continue;
        }

        while (valueStart < lineEnd &&
               (snapshot->data[valueStart] == ' ' ||
                snapshot->data[valueStart] == '\t'))
        {
            ++valueStart;
        }

        if (GC_ConfigParseSnapshotInt(
                snapshot->data + valueStart,
                lineEnd - valueStart,
                &parsedValue))
        {
            /*
             * Match the effective top-to-bottom config behavior:
             * if a key appears more than once, the last parseable value wins.
             */
            *valueOut =
                parsedValue;

            found =
                true;
        }
    }

    return found;
}

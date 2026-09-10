#include "gc_debug.h"

#include "gc_config.h"

#include "gc_memcard.h"

#include <ogcsys.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
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

    GC_ConfigSnapshotInit(
        snapshot);

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


void GC_ConfigSnapshotInit(
    gc_config_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }

    snapshot->size = 0;
}


static bool GC_ConfigSnapshotNameValid(
    const char *name,
    size_t *lengthOut)
{
    size_t length;

    if (name == NULL ||
        *name == '\0' ||
        lengthOut == NULL)
    {
        return false;
    }

    length = 0;

    while (name[length] != '\0')
    {
        unsigned char ch =
            (unsigned char)name[length];

        /*
         * Config variable names are one printable, whitespace-free token.
         * Reject whitespace/control bytes so a caller cannot inject another
         * config line through the key name.
         */
        if (ch <= 0x20u ||
            ch >= 0x7fu)
        {
            return false;
        }

        ++length;

        if (length >= GC_CONFIG_SNAPSHOT_CAPACITY)
        {
            return false;
        }
    }

    *lengthOut =
        length;

    return true;
}


static bool GC_ConfigSnapshotLineHasName(
    const gc_config_snapshot_t *snapshot,
    size_t lineStart,
    size_t lineContentEnd,
    const char *name,
    size_t nameLength)
{
    if (snapshot == NULL ||
        lineContentEnd <= lineStart ||
        lineContentEnd - lineStart <= nameLength)
    {
        return false;
    }

    if (memcmp(
            snapshot->data + lineStart,
            name,
            nameLength) != 0)
    {
        return false;
    }

    /*
     * Match the GameCube config parser: it separates a name from its value
     * at the first literal space.  A tab-separated lookalike therefore
     * remains untouched rather than being rewritten as a recognized key.
     */
    return
        snapshot->data[lineStart + nameLength] == ' ';
}


bool GC_ConfigSnapshotSetInt(
    gc_config_snapshot_t *snapshot,
    const char *name,
    int value)
{
    unsigned char *updated;
    char valueText[32];

    size_t nameLength;
    size_t valueLength;
    size_t cursor;
    size_t outputSize;

    int formatted;

    if (snapshot == NULL ||
        snapshot->size > sizeof(snapshot->data) ||
        !GC_ConfigSnapshotNameValid(
            name,
            &nameLength))
    {
        return false;
    }

    formatted =
        snprintf(
            valueText,
            sizeof(valueText),
            "%d",
            value);

    if (formatted <= 0 ||
        (size_t)formatted >= sizeof(valueText))
    {
        return false;
    }

    valueLength =
        (size_t)formatted;

    updated =
        malloc(
            sizeof(snapshot->data));

    if (updated == NULL)
    {
        return false;
    }

    cursor = 0;
    outputSize = 0;

    while (cursor < snapshot->size)
    {
        size_t lineStart =
            cursor;

        size_t lineContentEnd;
        size_t lineEnd;
        size_t matchContentEnd;
        size_t lineSize;

        while (cursor < snapshot->size &&
               snapshot->data[cursor] != '\n')
        {
            ++cursor;
        }

        lineContentEnd =
            cursor;

        if (cursor < snapshot->size &&
            snapshot->data[cursor] == '\n')
        {
            ++cursor;
        }

        lineEnd =
            cursor;

        matchContentEnd =
            lineContentEnd;

        if (matchContentEnd > lineStart &&
            snapshot->data[matchContentEnd - 1] == '\r')
        {
            --matchContentEnd;
        }

        if (GC_ConfigSnapshotLineHasName(
                snapshot,
                lineStart,
                matchContentEnd,
                name,
                nameLength))
        {
            /*
             * Drop every parser-visible occurrence of the key.  The one
             * canonical entry appended below becomes the sole effective
             * launcher-owned value while every unrelated line stays intact.
             */
            continue;
        }

        lineSize =
            lineEnd - lineStart;

        if (lineSize >
            sizeof(snapshot->data) - outputSize)
        {
            free(updated);
            return false;
        }

        memcpy(
            updated + outputSize,
            snapshot->data + lineStart,
            lineSize);

        outputSize +=
            lineSize;
    }

    /*
     * If the preserved final line had no newline, add only the delimiter
     * needed before the new canonical entry.  The preserved line's original
     * bytes themselves remain unchanged.
     */
    if (outputSize > 0 &&
        updated[outputSize - 1] != '\n')
    {
        if (outputSize >= sizeof(snapshot->data))
        {
            free(updated);
            return false;
        }

        updated[outputSize++] =
            '\n';
    }

    if (nameLength >
        sizeof(snapshot->data) - outputSize)
    {
        free(updated);
        return false;
    }

    memcpy(
        updated + outputSize,
        name,
        nameLength);

    outputSize +=
        nameLength;

    if (outputSize >= sizeof(snapshot->data))
    {
        free(updated);
        return false;
    }

    updated[outputSize++] =
        ' ';

    if (valueLength >
        sizeof(snapshot->data) - outputSize)
    {
        free(updated);
        return false;
    }

    memcpy(
        updated + outputSize,
        valueText,
        valueLength);

    outputSize +=
        valueLength;

    if (outputSize >= sizeof(snapshot->data))
    {
        free(updated);
        return false;
    }

    updated[outputSize++] =
        '\n';

    memcpy(
        snapshot->data,
        updated,
        outputSize);

    snapshot->size =
        outputSize;

    free(updated);

    return true;
}


bool GC_ConfigSnapshotSave(
    const gc_config_snapshot_t *snapshot)
{
    if (snapshot == NULL ||
        snapshot->size == 0 ||
        snapshot->size > sizeof(snapshot->data))
    {
        return false;
    }

    return
        GC_ConfigSave(
            snapshot->data,
            snapshot->size);
}

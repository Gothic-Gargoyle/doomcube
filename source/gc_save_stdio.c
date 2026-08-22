#include "gc_save_stdio.h"
#include "gc_memcard.h"

#include <ogcsys.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GC_SAVE_BUFFER_SIZE 32736u

typedef struct
{
    unsigned char *data;

    size_t size;
    size_t position;
    size_t capacity;

    bool writable;
    bool temporary;

    int slot;
} gc_save_stream_t;


/* ------------------------------------------------------------------------- */
/* Temporary Doom save                                                       */
/* ------------------------------------------------------------------------- */

static unsigned char *tempSaveData;
static size_t tempSaveSize;
static bool tempSaveValid;


/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static const char *baseName(const char *path)
{
    const char *slash;

    if (!path)
        return "";

    slash = strrchr(
        path,
        '/'
    );

    return slash
        ? slash + 1
        : path;
}


static int saveSlotFromPath(const char *path)
{
    const char *name =
        baseName(path);

    int slot;

    for (slot = 0;
         slot < GC_MEMCARD_SAVE_SLOTS;
         ++slot)
    {
        char expected[32];

        snprintf(
            expected,
            sizeof(expected),
            "doomsav%d.dsg",
            slot
        );

        if (strcmp(
                name,
                expected) == 0)
        {
            return slot;
        }
    }

    return -1;
}


static bool isTempSave(const char *path)
{
    return strcmp(
        baseName(path),
        "temp.dsg"
    ) == 0;
}


static bool isRecoverySave(const char *path)
{
    return strcmp(
        baseName(path),
        "recovery.dsg"
    ) == 0;
}


static gc_save_stream_t *newStream(void)
{
    gc_save_stream_t *stream;

    stream = calloc(
        1,
        sizeof(*stream)
    );

    if (!stream)
        return NULL;

    stream->capacity =
        GC_SAVE_BUFFER_SIZE;

    stream->slot =
        -1;

    stream->data = malloc(
        stream->capacity
    );

    if (!stream->data)
    {
        free(stream);
        return NULL;
    }

    return stream;
}


static void destroyStream(
    gc_save_stream_t *stream)
{
    if (!stream)
        return;

    free(
        stream->data
    );

    free(
        stream
    );
}


/* ------------------------------------------------------------------------- */
/* fopen                                                                     */
/* ------------------------------------------------------------------------- */

FILE *GC_SaveFOpen(
    const char *path,
    const char *mode)
{
    gc_save_stream_t *stream;

    bool reading;
    bool writing;

    int slot;

    reading =
        mode &&
        strchr(mode, 'r') != NULL;

    writing =
        mode &&
        strchr(mode, 'w') != NULL;

    slot =
        saveSlotFromPath(path);


    /*
     * Reading doomsav0.dsg ... doomsav5.dsg
     */
    if (reading &&
        slot >= 0)
    {
        size_t actualSize = 0;

        stream =
            newStream();

        if (!stream)
            return NULL;

        if (!GC_MemoryCardReadSave(
                slot,
                stream->data,
                stream->capacity,
                &actualSize))
        {
            destroyStream(
                stream
            );

            return NULL;
        }

        stream->size =
            actualSize;

        stream->position =
            0;

        stream->slot =
            slot;

        SYS_Report(
            "DoomCube: fopen slot %d read (%u bytes)\n",
            slot,
            (unsigned int)actualSize
        );

        return (FILE *)stream;
    }


    /*
     * Doom writes to temp.dsg first.
     */
    if (writing &&
        (isTempSave(path) ||
         isRecoverySave(path)))
    {
        stream =
            newStream();

        if (!stream)
            return NULL;

        stream->writable =
            true;

        stream->temporary =
            true;

        tempSaveValid =
            false;

        tempSaveSize =
            0;

        SYS_Report(
            "DoomCube: fopen temporary save for write\n"
        );

        return (FILE *)stream;
    }


    return NULL;
}


/* ------------------------------------------------------------------------- */
/* fread                                                                     */
/* ------------------------------------------------------------------------- */

size_t GC_SaveFRead(
    void *ptr,
    size_t size,
    size_t nmemb,
    FILE *file)
{
    gc_save_stream_t *stream =
        (gc_save_stream_t *)file;

    size_t requested;
    size_t available;
    size_t bytes;

    if (!stream ||
        !ptr ||
        size == 0 ||
        nmemb == 0)
    {
        return 0;
    }

    requested =
        size * nmemb;

    if (stream->position >=
        stream->size)
    {
        return 0;
    }

    available =
        stream->size -
        stream->position;

    bytes =
        requested;

    if (bytes > available)
        bytes = available;

    memcpy(
        ptr,
        stream->data +
            stream->position,
        bytes
    );

    stream->position +=
        bytes;

    return
        bytes / size;
}


/* ------------------------------------------------------------------------- */
/* fwrite                                                                    */
/* ------------------------------------------------------------------------- */

size_t GC_SaveFWrite(
    const void *ptr,
    size_t size,
    size_t nmemb,
    FILE *file)
{
    gc_save_stream_t *stream =
        (gc_save_stream_t *)file;

    size_t requested;
    size_t available;
    size_t bytes;

    if (!stream ||
        !stream->writable ||
        !ptr ||
        size == 0 ||
        nmemb == 0)
    {
        return 0;
    }

    requested =
        size * nmemb;

    if (stream->position >=
        stream->capacity)
    {
        return 0;
    }

    available =
        stream->capacity -
        stream->position;

    bytes =
        requested;

    if (bytes > available)
        bytes = available;

    memcpy(
        stream->data +
            stream->position,
        ptr,
        bytes
    );

    stream->position +=
        bytes;

    if (stream->position >
        stream->size)
    {
        stream->size =
            stream->position;
    }

    return
        bytes / size;
}


/* ------------------------------------------------------------------------- */
/* ftell                                                                     */
/* ------------------------------------------------------------------------- */

long GC_SaveFTell(FILE *file)
{
    gc_save_stream_t *stream =
        (gc_save_stream_t *)file;

    if (!stream)
        return -1;

    return
        (long)stream->position;
}


/* ------------------------------------------------------------------------- */
/* fclose                                                                    */
/* ------------------------------------------------------------------------- */

int GC_SaveFClose(FILE *file)
{
    gc_save_stream_t *stream =
        (gc_save_stream_t *)file;

    if (!stream)
        return -1;

    if (stream->temporary &&
        stream->writable)
    {
        unsigned char *newData;

        if (stream->size == 0)
        {
            free(
                tempSaveData
            );

            tempSaveData =
                NULL;

            tempSaveSize =
                0;

            tempSaveValid =
                false;
        }
        else
        {
            newData = realloc(
                tempSaveData,
                stream->size
            );

            if (!newData)
            {
                destroyStream(
                    stream
                );

                return -1;
            }

            tempSaveData =
                newData;

            memcpy(
                tempSaveData,
                stream->data,
                stream->size
            );

            tempSaveSize =
                stream->size;

            tempSaveValid =
                true;

            SYS_Report(
                "DoomCube: temporary Doom save complete (%u bytes)\n",
                (unsigned int)tempSaveSize
            );
        }
    }

    destroyStream(
        stream
    );

    return 0;
}


/* ------------------------------------------------------------------------- */
/* remove                                                                    */
/* ------------------------------------------------------------------------- */

int GC_SaveRemove(const char *path)
{
    int slot =
        saveSlotFromPath(path);

    /*
     * Doom removes the old save immediately before rename().
     *
     * Do nothing; rename commits the replacement.
     */
    if (slot >= 0)
        return 0;

    if (isTempSave(path) ||
        isRecoverySave(path))
    {
        free(
            tempSaveData
        );

        tempSaveData =
            NULL;

        tempSaveSize =
            0;

        tempSaveValid =
            false;

        return 0;
    }

    return -1;
}


/* ------------------------------------------------------------------------- */
/* rename                                                                    */
/* ------------------------------------------------------------------------- */

int GC_SaveRename(
    const char *oldpath,
    const char *newpath)
{
    int slot;

    if (!isTempSave(oldpath) &&
        !isRecoverySave(oldpath))
    {
        return -1;
    }

    slot =
        saveSlotFromPath(newpath);

    if (slot < 0)
        return -1;

    if (!tempSaveValid ||
        !tempSaveData ||
        tempSaveSize == 0)
    {
        SYS_Report(
            "DoomCube: rename without temporary save\n"
        );

        return -1;
    }

    SYS_Report(
        "DoomCube: committing slot %d (%u bytes)\n",
        slot,
        (unsigned int)tempSaveSize
    );

    if (!GC_MemoryCardWriteSave(
            slot,
            tempSaveData,
            tempSaveSize))
    {
        SYS_Report(
            "DoomCube: slot %d commit FAILED\n",
            slot
        );

        return -1;
    }

    SYS_Report(
        "DoomCube: slot %d commit OK\n",
        slot
    );

    free(
        tempSaveData
    );

    tempSaveData =
        NULL;

    tempSaveSize =
        0;

    tempSaveValid =
        false;

    return 0;
}
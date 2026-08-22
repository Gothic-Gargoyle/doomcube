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
    bool slot0;
} gc_save_stream_t;


/* ------------------------------------------------------------------------- */
/* RAM temporary save                                                        */
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

    if (!slash)
        return path;

    return slash + 1;
}


static bool isSlot0(const char *path)
{
    const char *name =
        baseName(path);

    return strcmp(
        name,
        "doomsav0.dsg"
    ) == 0;
}


static bool isTempSave(const char *path)
{
    const char *name =
        baseName(path);

    return strcmp(
        name,
        "temp.dsg"
    ) == 0;
}


static bool isRecoverySave(const char *path)
{
    const char *name =
        baseName(path);

    return strcmp(
        name,
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

    reading =
        mode &&
        strchr(mode, 'r') != NULL;

    writing =
        mode &&
        strchr(mode, 'w') != NULL;


    /*
     * Slot 0 load.
     */
    if (reading &&
        isSlot0(path))
    {
        size_t actualSize = 0;

        stream =
            newStream();

        if (!stream)
            return NULL;

        if (!GC_MemoryCardReadSave(
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

        stream->slot0 =
            true;

        SYS_Report(
            "DoomCube: fopen slot0 read (%u bytes)\n",
            (unsigned int)actualSize
        );

        return (FILE *)stream;
    }


    /*
     * Chocolate Doom writes to temp.dsg first.
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


    /*
     * The menu probes every save slot.
     *
     * For this milestone only slot 0 exists.
     */
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

    return bytes / size;
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

    return bytes / size;
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

    return (long)
        stream->position;
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

    /*
     * Closing temp.dsg preserves it in RAM.
     *
     * Chocolate Doom will rename it to doomsav0.dsg
     * immediately afterward.
     */
    if (stream->temporary &&
        stream->writable)
    {
        unsigned char *newData;

        newData = realloc(
            tempSaveData,
            stream->size
        );

        if (stream->size > 0 &&
            !newData)
        {
            destroyStream(
                stream
            );

            return -1;
        }

        tempSaveData =
            newData;

        if (stream->size > 0)
        {
            memcpy(
                tempSaveData,
                stream->data,
                stream->size
            );
        }

        tempSaveSize =
            stream->size;

        tempSaveValid =
            true;

        SYS_Report(
            "DoomCube: temporary Doom save complete (%u bytes)\n",
            (unsigned int)tempSaveSize
        );
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
    /*
     * Chocolate Doom removes the old doomsav0.dsg immediately
     * before renaming temp.dsg over it.
     *
     * Do nothing here. The following rename commits atomically-ish
     * to the GameCube card.
     */
    if (isSlot0(path))
        return 0;

    if (isTempSave(path))
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
    if (!isTempSave(oldpath) &&
        !isRecoverySave(oldpath))
    {
        return -1;
    }

    if (!isSlot0(newpath))
        return -1;

    if (!tempSaveValid ||
        !tempSaveData ||
        tempSaveSize == 0)
    {
        SYS_Report(
            "DoomCube: rename attempted without temporary save\n"
        );

        return -1;
    }

    SYS_Report(
        "DoomCube: committing doomsav0.dsg (%u bytes)\n",
        (unsigned int)tempSaveSize
    );

    if (!GC_MemoryCardWriteSave(
            tempSaveData,
            tempSaveSize))
    {
        SYS_Report(
            "DoomCube: GameCube save commit FAILED\n"
        );

        return -1;
    }

    SYS_Report(
        "DoomCube: GameCube save commit OK\n"
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
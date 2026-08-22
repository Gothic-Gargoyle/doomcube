#include "gc_memcard.h"

#include <ogc/card.h>
#include <ogcsys.h>

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CARD_SLOT              CARD_SLOTA
#define CARD_FILENAME          "DOOMCUBE"

#define CARD_GAMECODE          "DOOM"
#define CARD_COMPANY           "DC"

#define DOOMCUBE_MAGIC         0x44434D43u
#define DOOMCUBE_VERSION       1u

/*
 * Actual Doom save observed:
 *
 *     25350 bytes
 *
 * Four 8192-byte card sectors gives:
 *
 *     32768 bytes total
 *
 * minus our tiny header.
 */
#define SAVE_SECTORS           4u

static unsigned char cardWorkArea[CARD_WORKAREA]
    __attribute__((aligned(32)));

static bool cardMounted;
static s32 sectorSize;


/* ------------------------------------------------------------------------- */
/* Save format                                                               */
/* ------------------------------------------------------------------------- */

typedef struct
{
    uint32_t magic;
    uint32_t version;

    uint32_t valid;
    uint32_t size;

    uint32_t timestamp;

    uint32_t reserved[3];
} doomcube_save_header_t;


/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static size_t cardFileSize(void)
{
    return
        (size_t)sectorSize *
        SAVE_SECTORS;
}


static size_t saveCapacity(void)
{
    return
        cardFileSize() -
        sizeof(doomcube_save_header_t);
}


static unsigned char *allocSaveBuffer(void)
{
    return memalign(
        32,
        cardFileSize()
    );
}


static void cardRemoved(s32 channel, s32 result)
{
    (void)result;

    if (channel == CARD_SLOT)
    {
        cardMounted = false;

        SYS_Report(
            "DoomCube: Memory Card A removed\n"
        );
    }
}


/* ------------------------------------------------------------------------- */
/* Init                                                                      */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardInit(void)
{
    card_file file;

    s32 result;
    s32 memorySize = 0;

    SYS_Report("\n");
    SYS_Report(
        "DoomCube: ---- MEMORY CARD A ----\n"
    );

    SYS_Report(
        "DoomCube: initializing memory card...\n"
    );

    result = CARD_Init(
        CARD_GAMECODE,
        CARD_COMPANY
    );

    if (result < 0)
    {
        SYS_Report(
            "DoomCube: CARD_Init failed: %ld\n",
            (long)result
        );

        return false;
    }

    do
    {
        result = CARD_ProbeEx(
            CARD_SLOT,
            &memorySize,
            &sectorSize
        );
    }
    while (result == CARD_ERROR_BUSY);

    if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_ProbeEx failed: %ld\n",
            (long)result
        );

        return false;
    }

    SYS_Report(
        "DoomCube: card size=%ld sector=%ld\n",
        (long)memorySize,
        (long)sectorSize
    );

    result = CARD_Mount(
        CARD_SLOT,
        cardWorkArea,
        cardRemoved
    );

    if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_Mount failed: %ld\n",
            (long)result
        );

        return false;
    }

    cardMounted = true;

    SYS_Report(
        "DoomCube: Memory Card A mounted\n"
    );

    /*
     * See whether our save already exists.
     */
    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result == CARD_ERROR_READY)
    {
        CARD_Close(
            &file
        );

        SYS_Report(
            "DoomCube: existing save file found\n"
        );

        return true;
    }

    if (result != CARD_ERROR_NOFILE)
    {
        SYS_Report(
            "DoomCube: CARD_Open failed: %ld\n",
            (long)result
        );

        return false;
    }

    /*
     * Create the save once.
     *
     * Unlike our doomed giant-container experiments, this is only
     * four sectors and we do not touch the contents yet.
     */
    SYS_Report(
        "DoomCube: creating %u-byte save file\n",
        (unsigned int)cardFileSize()
    );

    result = CARD_Create(
        CARD_SLOT,
        CARD_FILENAME,
        cardFileSize(),
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_Create failed: %ld\n",
            (long)result
        );

        return false;
    }

    result = CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_Close failed: %ld\n",
            (long)result
        );

        return false;
    }

    SYS_Report(
        "DoomCube: save file created\n"
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Exists                                                                    */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardSaveExists(void)
{
    card_file file;

    unsigned char *buffer;
    doomcube_save_header_t *header;

    s32 result;
    bool exists;

    if (!cardMounted)
        return false;

    buffer =
        allocSaveBuffer();

    if (!buffer)
        return false;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        free(buffer);
        return false;
    }

    result = CARD_Read(
        &file,
        buffer,
        cardFileSize(),
        0
    );

    CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        free(buffer);
        return false;
    }

    header =
        (doomcube_save_header_t *)buffer;

    exists =
        header->magic == DOOMCUBE_MAGIC &&
        header->version == DOOMCUBE_VERSION &&
        header->valid != 0 &&
        header->size <= saveCapacity();

    free(
        buffer
    );

    return exists;
}


/* ------------------------------------------------------------------------- */
/* Write                                                                     */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardWriteSave(
    const void *data,
    size_t size)
{
    card_file file;

    unsigned char *buffer;
    doomcube_save_header_t *header;

    s32 result;

    if (!cardMounted ||
        !data ||
        size == 0)
    {
        return false;
    }

    if (size > saveCapacity())
    {
        SYS_Report(
            "DoomCube: save too large: %u bytes, capacity %u\n",
            (unsigned int)size,
            (unsigned int)saveCapacity()
        );

        return false;
    }

    buffer =
        allocSaveBuffer();

    if (!buffer)
        return false;

    memset(
        buffer,
        0,
        cardFileSize()
    );

    header =
        (doomcube_save_header_t *)buffer;

    header->magic =
        DOOMCUBE_MAGIC;

    header->version =
        DOOMCUBE_VERSION;

    header->valid =
        1;

    header->size =
        (uint32_t)size;

    header->timestamp =
        (uint32_t)time(NULL);

    memcpy(
        buffer +
            sizeof(doomcube_save_header_t),
        data,
        size
    );

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        free(buffer);

        SYS_Report(
            "DoomCube: CARD_Open for write failed: %ld\n",
            (long)result
        );

        return false;
    }

    result = CARD_Write(
        &file,
        buffer,
        cardFileSize(),
        0
    );

    CARD_Close(
        &file
    );

    free(
        buffer
    );

    if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_Write failed: %ld\n",
            (long)result
        );

        return false;
    }

    SYS_Report(
        "DoomCube: save written: %u bytes\n",
        (unsigned int)size
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Read                                                                      */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardReadSave(
    void *output,
    size_t outputSize,
    size_t *actualSize)
{
    card_file file;

    unsigned char *buffer;
    doomcube_save_header_t *header;

    s32 result;

    if (actualSize)
        *actualSize = 0;

    if (!cardMounted)
        return false;

    buffer =
        allocSaveBuffer();

    if (!buffer)
        return false;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        free(buffer);
        return false;
    }

    result = CARD_Read(
        &file,
        buffer,
        cardFileSize(),
        0
    );

    CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        free(buffer);

        SYS_Report(
            "DoomCube: CARD_Read failed: %ld\n",
            (long)result
        );

        return false;
    }

    header =
        (doomcube_save_header_t *)buffer;

    if (header->magic != DOOMCUBE_MAGIC ||
        header->version != DOOMCUBE_VERSION ||
        !header->valid ||
        header->size > saveCapacity())
    {
        free(buffer);
        return false;
    }

    if (actualSize)
    {
        *actualSize =
            header->size;
    }

    if (!output)
    {
        free(buffer);
        return true;
    }

    if (outputSize < header->size)
    {
        SYS_Report(
            "DoomCube: destination buffer too small\n"
        );

        free(buffer);
        return false;
    }

    memcpy(
        output,
        buffer +
            sizeof(doomcube_save_header_t),
        header->size
    );

    SYS_Report(
        "DoomCube: save loaded: %u bytes\n",
        header->size
    );

    free(
        buffer
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Timestamp                                                                 */
/* ------------------------------------------------------------------------- */

uint32_t GC_MemoryCardSaveTimestamp(void)
{
    card_file file;

    unsigned char *buffer;
    doomcube_save_header_t *header;

    s32 result;
    uint32_t timestamp = 0;

    if (!cardMounted)
        return 0;

    buffer =
        allocSaveBuffer();

    if (!buffer)
        return 0;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        free(buffer);
        return 0;
    }

    result = CARD_Read(
        &file,
        buffer,
        cardFileSize(),
        0
    );

    CARD_Close(
        &file
    );

    if (result == CARD_ERROR_READY)
    {
        header =
            (doomcube_save_header_t *)buffer;

        if (header->magic == DOOMCUBE_MAGIC &&
            header->version == DOOMCUBE_VERSION &&
            header->valid)
        {
            timestamp =
                header->timestamp;
        }
    }

    free(
        buffer
    );

    return timestamp;
}


/* ------------------------------------------------------------------------- */
/* Shutdown                                                                  */
/* ------------------------------------------------------------------------- */

void GC_MemoryCardShutdown(void)
{
    if (!cardMounted)
        return;

    CARD_Unmount(
        CARD_SLOT
    );

    cardMounted = false;

    SYS_Report(
        "DoomCube: Memory Card A unmounted\n"
    );
}
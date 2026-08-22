#include "gc_memcard.h"

#include <ogc/card.h>

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CARD_SLOT              CARD_SLOTA
#define CARD_FILENAME          "DOOMCUBE"

#define CARD_GAMECODE          "DOOM"
#define CARD_COMPANY           "DC"

#define DOOMCUBE_MAGIC         0x44434D43u
#define DOOMCUBE_VERSION       1

#define TEST_VALUE             0x12345678u

static unsigned char cardWorkArea[CARD_WORKAREA]
    __attribute__((aligned(32)));

static bool cardMounted;
static s32 sectorSize;


/* ------------------------------------------------------------------------- */
/* On-card format                                                            */
/* ------------------------------------------------------------------------- */

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t test_value;
    uint32_t reserved[5];
} doomcube_card_header_t;


/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static const char *cardErrorName(s32 result)
{
    switch (result)
    {
        case CARD_ERROR_READY:
            return "ready";

        case CARD_ERROR_BUSY:
            return "busy";

        case CARD_ERROR_WRONGDEVICE:
            return "wrong device";

        case CARD_ERROR_NOCARD:
            return "no card";

        case CARD_ERROR_NOFILE:
            return "file not found";

        case CARD_ERROR_IOERROR:
            return "I/O error";

        case CARD_ERROR_BROKEN:
            return "broken filesystem";

        case CARD_ERROR_EXIST:
            return "already exists";

        case CARD_ERROR_NOENT:
            return "no free directory entry";

        case CARD_ERROR_INSSPACE:
            return "insufficient space";

        case CARD_ERROR_NOPERM:
            return "permission denied";

        case CARD_ERROR_LIMIT:
            return "card limit reached";

        case CARD_ERROR_NAMETOOLONG:
            return "filename too long";

        case CARD_ERROR_ENCODING:
            return "encoding mismatch";

        case CARD_ERROR_CANCELED:
            return "cancelled";

        case CARD_ERROR_FATAL_ERROR:
            return "fatal error";

        default:
            return "unknown";
    }
}


static void cardRemoved(s32 channel, s32 result)
{
    (void)result;

    if (channel == CARD_SLOT)
    {
        printf("DoomCube: Memory Card A removed\n");
        cardMounted = false;
    }
}


/* ------------------------------------------------------------------------- */
/* Init                                                                      */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardInit(void)
{
    s32 result;
    s32 memorySize = 0;

    printf("DoomCube: Memory Card init...\n");

    result = CARD_Init(
        CARD_GAMECODE,
        CARD_COMPANY
    );

    if (result < 0)
    {
        printf(
            "DoomCube: CARD_Init failed: %s (%ld)\n",
            cardErrorName(result),
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
        printf(
            "DoomCube: CARD_ProbeEx failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        return false;
    }

    printf(
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
        printf(
            "DoomCube: CARD_Mount failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        return false;
    }

    cardMounted = true;

    printf("DoomCube: Memory Card A mounted\n");

    return true;
}


/* ------------------------------------------------------------------------- */
/* Write test                                                                */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardWriteTest(void)
{
    card_file file;
    unsigned char *buffer;
    doomcube_card_header_t *header;
    s32 result;

    if (!cardMounted)
        return false;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result == CARD_ERROR_NOFILE)
    {
        printf(
            "DoomCube: creating card file '%s'\n",
            CARD_FILENAME
        );

        result = CARD_Create(
            CARD_SLOT,
            CARD_FILENAME,
            sectorSize,
            &file
        );

        if (result != CARD_ERROR_READY)
        {
            printf(
                "DoomCube: CARD_Create failed: %s (%ld)\n",
                cardErrorName(result),
                (long)result
            );

            return false;
        }
    }
    else if (result != CARD_ERROR_READY)
    {
        printf(
            "DoomCube: CARD_Open failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        return false;
    }

    buffer = memalign(
        32,
        sectorSize
    );

    if (!buffer)
    {
        printf("DoomCube: memalign failed\n");
        CARD_Close(&file);
        return false;
    }

    memset(
        buffer,
        0,
        sectorSize
    );

    header =
        (doomcube_card_header_t *)buffer;

    header->magic =
        DOOMCUBE_MAGIC;

    header->version =
        DOOMCUBE_VERSION;

    header->test_value =
        TEST_VALUE;

    result = CARD_Write(
        &file,
        buffer,
        sectorSize,
        0
    );

    free(buffer);

    if (result != CARD_ERROR_READY)
    {
        printf(
            "DoomCube: CARD_Write failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        CARD_Close(&file);
        return false;
    }

    result = CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        printf(
            "DoomCube: CARD_Close failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        return false;
    }

    printf(
        "DoomCube: wrote test value 0x%08x\n",
        TEST_VALUE
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Read test                                                                 */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardReadTest(void)
{
    card_file file;
    unsigned char *buffer;
    doomcube_card_header_t *header;
    s32 result;
    bool ok = false;

    if (!cardMounted)
        return false;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        printf(
            "DoomCube: CARD_Open read failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        return false;
    }

    buffer = memalign(
        32,
        sectorSize
    );

    if (!buffer)
    {
        printf("DoomCube: memalign failed\n");
        CARD_Close(&file);
        return false;
    }

    memset(
        buffer,
        0,
        sectorSize
    );

    result = CARD_Read(
        &file,
        buffer,
        sectorSize,
        0
    );

    if (result != CARD_ERROR_READY)
    {
        printf(
            "DoomCube: CARD_Read failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        free(buffer);
        CARD_Close(&file);

        return false;
    }

    header =
        (doomcube_card_header_t *)buffer;

    printf(
        "DoomCube: read magic=0x%08x version=%u test=0x%08x\n",
        header->magic,
        header->version,
        header->test_value
    );

    if (header->magic == DOOMCUBE_MAGIC &&
        header->version == DOOMCUBE_VERSION &&
        header->test_value == TEST_VALUE)
    {
        printf("DoomCube: memory-card persistence OK\n");
        ok = true;
    }
    else
    {
        printf("DoomCube: memory-card persistence FAILED\n");
    }

    free(buffer);
    CARD_Close(&file);

    return ok;
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

    printf("DoomCube: Memory Card A unmounted\n");
}
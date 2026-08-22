#include "gc_memcard.h"

#include <ogc/card.h>
#include <ogcsys.h>

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CARD_SLOT              CARD_SLOTA
#define CARD_FILENAME          "DOOMCUBE"

#define CARD_GAMECODE          "DOOM"
#define CARD_COMPANY           "DC"

#define DOOMCUBE_MAGIC         0x44434D43u
#define DOOMCUBE_VERSION       1u

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
    uint32_t boot_counter;
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
        SYS_Report(
            "DoomCube: Memory Card A removed\n"
        );

        cardMounted = false;
    }
}


static unsigned char *allocSectorBuffer(void)
{
    unsigned char *buffer;

    buffer = memalign(
        32,
        sectorSize
    );

    if (!buffer)
    {
        SYS_Report(
            "DoomCube: failed allocating %ld-byte card buffer\n",
            (long)sectorSize
        );
    }

    return buffer;
}


/* ------------------------------------------------------------------------- */
/* Init                                                                      */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardInit(void)
{
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
        SYS_Report(
            "DoomCube: CARD_ProbeEx failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        return false;
    }

    SYS_Report(
        "DoomCube: card size=%ld sector=%ld\n",
        (long)memorySize,
        (long)sectorSize
    );

    if (sectorSize <= 0)
    {
        SYS_Report(
            "DoomCube: invalid card sector size\n"
        );

        return false;
    }

    result = CARD_Mount(
        CARD_SLOT,
        cardWorkArea,
        cardRemoved
    );

    if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_Mount failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        return false;
    }

    cardMounted = true;

    SYS_Report(
        "DoomCube: Memory Card A mounted\n"
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Counter test                                                              */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardCounterTest(void)
{
    card_file file;
    unsigned char *buffer;
    doomcube_card_header_t *header;

    s32 result;

    uint32_t oldCounter;
    uint32_t newCounter;

    bool created = false;

    if (!cardMounted)
    {
        SYS_Report(
            "DoomCube: counter test skipped; card not mounted\n"
        );

        return false;
    }

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result == CARD_ERROR_NOFILE)
    {
        SYS_Report(
            "DoomCube: '%s' not found; creating new save\n",
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
            SYS_Report(
                "DoomCube: CARD_Create failed: %s (%ld)\n",
                cardErrorName(result),
                (long)result
            );

            return false;
        }

        created = true;
    }
    else if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_Open failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        return false;
    }

    buffer = allocSectorBuffer();

    if (!buffer)
    {
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

    if (!created)
    {
        result = CARD_Read(
            &file,
            buffer,
            sectorSize,
            0
        );

        if (result != CARD_ERROR_READY)
        {
            SYS_Report(
                "DoomCube: CARD_Read failed: %s (%ld)\n",
                cardErrorName(result),
                (long)result
            );

            free(buffer);
            CARD_Close(&file);

            return false;
        }

        if (header->magic != DOOMCUBE_MAGIC)
        {
            SYS_Report(
                "DoomCube: invalid save magic: 0x%08x\n",
                header->magic
            );

            free(buffer);
            CARD_Close(&file);

            return false;
        }

        if (header->version != DOOMCUBE_VERSION)
        {
            SYS_Report(
                "DoomCube: unsupported save version: %u\n",
                header->version
            );

            free(buffer);
            CARD_Close(&file);

            return false;
        }

        oldCounter =
            header->boot_counter;

        SYS_Report(
            "DoomCube: stored boot counter = %u\n",
            oldCounter
        );
    }
    else
    {
        oldCounter = 0;

        SYS_Report(
            "DoomCube: new save; stored boot counter = 0\n"
        );
    }

    newCounter =
        oldCounter + 1;

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

    header->boot_counter =
        newCounter;

    SYS_Report(
        "DoomCube: writing boot counter = %u\n",
        newCounter
    );

    result = CARD_Write(
        &file,
        buffer,
        sectorSize,
        0
    );

    if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_Write failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        free(buffer);
        CARD_Close(&file);

        return false;
    }

    /*
     * Read it back immediately.
     */
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
        SYS_Report(
            "DoomCube: verification CARD_Read failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        free(buffer);
        CARD_Close(&file);

        return false;
    }

    header =
        (doomcube_card_header_t *)buffer;

    SYS_Report(
        "DoomCube: verification counter = %u\n",
        header->boot_counter
    );

    if (header->magic != DOOMCUBE_MAGIC ||
        header->version != DOOMCUBE_VERSION ||
        header->boot_counter != newCounter)
    {
        SYS_Report(
            "DoomCube: MEMORY CARD VERIFY FAILED\n"
        );

        free(buffer);
        CARD_Close(&file);

        return false;
    }

    SYS_Report(
        "DoomCube: MEMORY CARD VERIFY OK\n"
    );

    free(buffer);

    result = CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_Close failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        return false;
    }

    return true;
}


/* ------------------------------------------------------------------------- */
/* Shutdown                                                                  */
/* ------------------------------------------------------------------------- */

void GC_MemoryCardShutdown(void)
{
    s32 result;

    if (!cardMounted)
        return;

    result = CARD_Unmount(
        CARD_SLOT
    );

    cardMounted = false;

    if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_Unmount failed: %s (%ld)\n",
            cardErrorName(result),
            (long)result
        );

        return;
    }

    SYS_Report(
        "DoomCube: Memory Card A unmounted\n"
    );

    SYS_Report(
        "DoomCube: -----------------------\n"
    );
}
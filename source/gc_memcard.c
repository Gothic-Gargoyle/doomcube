#include "gc_memcard.h"

#include <ogc/card.h>
#include <ogcsys.h>

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CARD_SLOT      CARD_SLOTA
#define CARD_FILENAME  "DOOMCUBE"

#define CARD_GAMECODE  "DOOM"
#define CARD_COMPANY   "Sperge Brigade Studios"

#define DOOMCUBE_MAGIC    0x44434D43u
#define DOOMCUBE_VERSION  1u

/*
 Vanilla Doom reserves up to 0x2c000 bytes (180224 bytes)
 * for a savegame.
 *
 * 22 GameCube sectors = exactly 180224 bytes.
 * DoomCube also stores a 32-byte slot header, so each slot
 * gets one additional sector.
 *
 * 23 * 8192 = 188416 bytes per slot.
*/
#define SAVE_SECTORS 23u

static unsigned char cardWorkArea[CARD_WORKAREA]
    __attribute__((aligned(32)));

static unsigned char *slotWorkBuffer;
static bool cardMounted;
static s32 sectorSize;


/* ------------------------------------------------------------------------- */
/* Slot format                                                               */
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

static bool validSlot(int slot)
{
    return
        slot >= 0 &&
        slot < GC_MEMCARD_SAVE_SLOTS;
}


static size_t slotRegionSize(void)
{
    return
        (size_t)sectorSize *
        SAVE_SECTORS;
}


static size_t slotCapacity(void)
{
    return
        slotRegionSize() -
        sizeof(doomcube_save_header_t);
}


static size_t cardFileSize(void)
{
    return
        slotRegionSize() *
        GC_MEMCARD_SAVE_SLOTS;
}


static u32 slotOffset(int slot)
{
    return
        (u32)(
            slotRegionSize() *
            (size_t)slot
        );
}


static unsigned char *allocSlotBuffer(void)
{
    return slotWorkBuffer;
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

     slotWorkBuffer = memalign(32, slotRegionSize());

    if (!slotWorkBuffer)
    {
        SYS_Report(
            "DoomCube: failed to allocate %u-byte slot work buffer\n",
            (unsigned int)slotRegionSize()
        );

        CARD_Unmount(CARD_SLOT);
        cardMounted = false;
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
            "DoomCube: CARD_Mount failed: %ld\n",
            (long)result
        );
         free(slotWorkBuffer);
        slotWorkBuffer = NULL;

        return false;
    }

    cardMounted = true;

    SYS_Report(
        "DoomCube: Memory Card A mounted\n"
    );

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
            "DoomCube: existing multi-slot save file found\n"
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

    SYS_Report(
        "DoomCube: creating %u-byte multi-slot save file\n",
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
        "DoomCube: multi-slot save file created\n"
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Exists                                                                    */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardSaveExists(int slot)
{
    card_file file;

    unsigned char *buffer;
    doomcube_save_header_t *header;

    s32 result;
    bool exists;

    if (!cardMounted ||
        !validSlot(slot))
    {
        return false;
    }

    buffer =
        allocSlotBuffer();

    if (!buffer)
        return false;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        return false;
    }

    result = CARD_Read(
        &file,
        buffer,
        slotRegionSize(),
        slotOffset(slot)
    );

    CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        return false;
    }

    header =
        (doomcube_save_header_t *)buffer;

    exists =
        header->magic == DOOMCUBE_MAGIC &&
        header->version == DOOMCUBE_VERSION &&
        header->valid != 0 &&
        header->size <= slotCapacity();

    return exists;
}


/* ------------------------------------------------------------------------- */
/* Write                                                                     */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardWriteSave(
    int slot,
    const void *data,
    size_t size)
{
    card_file file;

    unsigned char *buffer;
    doomcube_save_header_t *header;

    s32 result;

    if (!cardMounted ||
        !validSlot(slot) ||
        !data ||
        size == 0)
    {
        SYS_Report(
            "DoomCube: write rejected: mounted=%d slot=%d data=%p size=%u\n",
            cardMounted,
            slot,
            data,
            (unsigned int)size
        );
        return false;
    }

    if (size > slotCapacity())
    {
        SYS_Report(
            "DoomCube: slot %d save too large: %u > %u\n",
            slot,
            (unsigned int)size,
            (unsigned int)slotCapacity()
        );

        return false;
    }

    buffer =
        allocSlotBuffer();

    if (!buffer)
{
        SYS_Report(
            "DoomCube: slot %d work buffer unavailable (%u bytes)\n",
            slot,
            (unsigned int)slotRegionSize()
        );

         return false;
    }

    memset(
        buffer,
        0,
        slotRegionSize()
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

        SYS_Report(
            "DoomCube: CARD_Open slot %d failed: %ld\n",
            slot,
            (long)result
        );

        return false;
    }

    result = CARD_Write(
        &file,
        buffer,
        slotRegionSize(),
        slotOffset(slot)
    );

    CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        SYS_Report(
            "DoomCube: CARD_Write slot %d failed: %ld\n",
            slot,
            (long)result
        );

        return false;
    }

    SYS_Report(
        "DoomCube: slot %d saved: %u bytes\n",
        slot,
        (unsigned int)size
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Read                                                                      */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardReadSave(
    int slot,
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

    if (!cardMounted ||
        !validSlot(slot))
    {
        return false;
    }

    buffer =
        allocSlotBuffer();

    if (!buffer)
        return false;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        return false;
    }

    result = CARD_Read(
        &file,
        buffer,
        slotRegionSize(),
        slotOffset(slot)
    );

    CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {

        SYS_Report(
            "DoomCube: CARD_Read slot %d failed: %ld\n",
            slot,
            (long)result
        );

        return false;
    }

    header =
        (doomcube_save_header_t *)buffer;

    if (header->magic != DOOMCUBE_MAGIC ||
        header->version != DOOMCUBE_VERSION ||
        !header->valid ||
        header->size > slotCapacity())
    {
        return false;
    }

    if (actualSize)
    {
        *actualSize =
            header->size;
    }

    if (!output)
    {
        return true;
    }

    if (outputSize < header->size)
    {

        SYS_Report(
            "DoomCube: destination buffer too small for slot %d\n",
            slot
        );

        return false;
    }

    memcpy(
        output,
        buffer +
            sizeof(doomcube_save_header_t),
        header->size
    );

    SYS_Report(
        "DoomCube: slot %d loaded: %u bytes\n",
        slot,
        header->size
    );


    return true;
}


/* ------------------------------------------------------------------------- */
/* Timestamp                                                                 */
/* ------------------------------------------------------------------------- */

uint32_t GC_MemoryCardSaveTimestamp(int slot)
{
    card_file file;

    unsigned char *buffer;
    doomcube_save_header_t *header;

    s32 result;

    uint32_t timestamp = 0;

    if (!cardMounted ||
        !validSlot(slot))
    {
        return 0;
    }

    buffer =
        allocSlotBuffer();

    if (!buffer)
        return 0;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        return 0;
    }

    result = CARD_Read(
        &file,
        buffer,
        slotRegionSize(),
        slotOffset(slot)
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



    return timestamp;
}


/* ------------------------------------------------------------------------- */
/* Shutdown                                                                  */
/* ------------------------------------------------------------------------- */

void GC_MemoryCardShutdown(void)
{
  if (cardMounted)
   {
       CARD_Unmount(
           CARD_SLOT
       );

       cardMounted = false;
   }

   if (slotWorkBuffer)
   {
       free(slotWorkBuffer);
       slotWorkBuffer = NULL;
   }


    SYS_Report(
        "DoomCube: Memory Card A unmounted\n"
    );
}
#include "gc_debug.h"

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
#define CARD_COMPANY   "SB"

#define DOOMCUBE_SAVE_MAGIC    0x44434D43u
#define DOOMCUBE_CONFIG_MAGIC  0x44434346u
#define DOOMCUBE_VERSION       2u

/*
 * Layout:
 *
 *   sector 0       global DoomCube configuration
 *   sectors 1-23   DOOM Shareware
 *   sectors 24-46  DOOM / Ultimate DOOM
 *   sectors 47-69  DOOM II
 *   sectors 70-92  TNT: Evilution
 *   sectors 93-115 Plutonia
 *
 * One save region is 23 sectors:
 *
 *   22 sectors = 180224 bytes, Doom's vanilla maximum save size
 *   1 sector   = room for the DoomCube save header/alignment
 *
 * Total at 8192-byte sectors:
 *
 *   1 + (5 * 23) = 116 blocks = 950272 bytes
 */
#define CONFIG_SECTORS 1u
#define SAVE_SECTORS   23u

static unsigned char cardWorkArea[CARD_WORKAREA]
    __attribute__((aligned(32)));

static unsigned char *slotWorkBuffer;
static unsigned char *configWorkBuffer;

static bool cardMounted;
static s32 sectorSize;

static gc_savegame_id_t currentGame =
    GC_SAVEGAME_DOOM1;


/* ------------------------------------------------------------------------- */
/* Region formats                                                            */
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


typedef struct
{
    uint32_t magic;
    uint32_t version;

    uint32_t valid;
    uint32_t size;

    uint32_t reserved[4];
} doomcube_config_header_t;


/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static bool validGame(gc_savegame_id_t game)
{
    return
        game >= GC_SAVEGAME_DOOM1 &&
        game < GC_MEMCARD_GAME_COUNT;
}


static bool validLogicalSlot(int slot)
{
    /*
     * DoomCube intentionally exposes exactly one Doom save slot
     * for the currently selected game.
     */
    return slot == 0;
}


static size_t configRegionSize(void)
{
    return
        (size_t)sectorSize *
        CONFIG_SECTORS;
}


static size_t configCapacity(void)
{
    return
        configRegionSize() -
        sizeof(doomcube_config_header_t);
}


static size_t saveRegionSize(void)
{
    return
        (size_t)sectorSize *
        SAVE_SECTORS;
}


static size_t saveCapacity(void)
{
    return
        saveRegionSize() -
        sizeof(doomcube_save_header_t);
}


static size_t cardFileSize(void)
{
    return
        configRegionSize() +
        saveRegionSize() *
        GC_MEMCARD_GAME_COUNT;
}


static u32 configOffset(void)
{
    return 0;
}


static u32 saveOffset(gc_savegame_id_t game)
{
    return
        (u32)(
            configRegionSize() +
            saveRegionSize() *
            (size_t)game
        );
}


static const char *gameName(gc_savegame_id_t game)
{
    switch (game)
    {
        case GC_SAVEGAME_DOOM1:
            return "DOOM SHAREWARE";

        case GC_SAVEGAME_DOOM:
            return "DOOM";

        case GC_SAVEGAME_DOOM2:
            return "DOOM II";

        case GC_SAVEGAME_TNT:
            return "TNT: EVILUTION";

        case GC_SAVEGAME_PLUTONIA:
            return "PLUTONIA";

        default:
            return "UNKNOWN";
    }
}


static const char *baseName(const char *path)
{
    const char *slash;

    if (!path)
        return "";

    slash = strrchr(path, '/');

    return slash
        ? slash + 1
        : path;
}


static void freeWorkBuffers(void)
{
    if (slotWorkBuffer)
    {
        free(slotWorkBuffer);
        slotWorkBuffer = NULL;
    }

    if (configWorkBuffer)
    {
        free(configWorkBuffer);
        configWorkBuffer = NULL;
    }
}


static void cardRemoved(s32 channel, s32 result)
{
    (void)result;

    if (channel == CARD_SLOT)
    {
        cardMounted = false;

        DC_LOG(
            "DoomCube: Memory Card A removed\n"
        );
    }
}


/* ------------------------------------------------------------------------- */
/* Active game                                                               */
/* ------------------------------------------------------------------------- */

void GC_MemoryCardSetGame(gc_savegame_id_t game)
{
    if (!validGame(game))
    {
        DC_WARN(
            "DoomCube: invalid memory-card game id %d\n",
            (int)game
        );

        return;
    }

    currentGame = game;

    DC_DEBUG(
        "DoomCube: memory-card save selected for %s\n",
        gameName(currentGame)
    );
}


/*
bool GC_MemoryCardSetGameFromIWAD(const char *iwadPath)
{
    if (!iwadPath)
    {
        return false;
    }

    if (strcmp(iwadPath, "dvd:/doom1.wad") == 0)
    {
        currentGame =
            GC_SAVEGAME_DOOM1;

        return true;
    }

    if (strcmp(iwadPath, "dvd:/doom.wad") == 0)
    {
        currentGame =
            GC_SAVEGAME_DOOM;

        return true;
    }

    if (strcmp(iwadPath, "dvd:/doom2.wad") == 0)
    {
        currentGame =
            GC_SAVEGAME_DOOM2;

        return true;
    }

    if (strcmp(iwadPath, "dvd:/tnt.wad") == 0)
    {
        currentGame =
            GC_SAVEGAME_TNT;

        return true;
    }

    if (strcmp(iwadPath, "dvd:/plutonia.wad") == 0)
    {
        currentGame =
            GC_SAVEGAME_PLUTONIA;

        return true;
    }

    return false;
}
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardSetGameFromIWAD(const char *iwadPath)
{
    (void)iwadPath;

    currentGame =
        GC_SAVEGAME_DOOM1;

    return true;
}

/* Init                                                                      */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardInit(void)
{
    card_file file;

    s32 result;
    s32 memorySize = 0;

    DC_DEBUG(
        "DoomCube: ---- MEMORY CARD A ----\n"
    );

    DC_DEBUG(
        "DoomCube: initializing memory card...\n"
    );

    result = CARD_Init(
        CARD_GAMECODE,
        CARD_COMPANY
    );

    if (result < 0)
    {
        DC_WARN(
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
        DC_WARN(
            "DoomCube: CARD_ProbeEx failed: %ld\n",
            (long)result
        );

        return false;
    }

    DC_DEBUG(
        "DoomCube: card size=%ld sector=%ld\n",
        (long)memorySize,
        (long)sectorSize
    );

    slotWorkBuffer =
        memalign(
            32,
            saveRegionSize()
        );

    if (!slotWorkBuffer)
    {
        DC_WARN(
            "DoomCube: failed to allocate %u-byte save work buffer\n",
            (unsigned int)saveRegionSize()
        );

        return false;
    }

    configWorkBuffer =
        memalign(
            32,
            configRegionSize()
        );

    if (!configWorkBuffer)
    {
        DC_WARN(
            "DoomCube: failed to allocate %u-byte config work buffer\n",
            (unsigned int)configRegionSize()
        );

        freeWorkBuffers();

        return false;
    }

    result = CARD_Mount(
        CARD_SLOT,
        cardWorkArea,
        cardRemoved
    );

    if (result != CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD_Mount failed: %ld\n",
            (long)result
        );

        freeWorkBuffers();

        return false;
    }

    cardMounted = true;

    DC_LOG(
        "DoomCube: Memory Card A mounted\n"
    );

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result == CARD_ERROR_READY)
    {
        if ((size_t)file.len == cardFileSize())
        {
            CARD_Close(
                &file
            );

            DC_DEBUG(
                "DoomCube: existing v2 save file found (%u blocks)\n",
                (unsigned int)(
                    cardFileSize() /
                    (size_t)sectorSize
                )
            );

            return true;
        }

        DC_WARN(
            "DoomCube: old save file is %ld bytes; v2 requires %u bytes\n",
            (long)file.len,
            (unsigned int)cardFileSize()
        );

        DC_DEBUG(
            "DoomCube: recreating DOOMCUBE save container\n"
        );

        CARD_Close(
            &file
        );

        result = CARD_Delete(
            CARD_SLOT,
            CARD_FILENAME
        );

        if (result != CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: CARD_Delete failed: %ld\n",
                (long)result
            );

            return false;
        }
    }
    else if (result != CARD_ERROR_NOFILE)
    {
        DC_WARN(
            "DoomCube: CARD_Open failed: %ld\n",
            (long)result
        );

        return false;
    }

    DC_DEBUG(
        "DoomCube: creating %u-byte save file (%u blocks)\n",
        (unsigned int)cardFileSize(),
        (unsigned int)(
            cardFileSize() /
            (size_t)sectorSize
        )
    );

    result = CARD_Create(
        CARD_SLOT,
        CARD_FILENAME,
        cardFileSize(),
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        DC_WARN(
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
        DC_WARN(
            "DoomCube: CARD_Close failed: %ld\n",
            (long)result
        );

        return false;
    }

    DC_DEBUG(
        "DoomCube: v2 save file created\n"
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Save exists                                                               */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardSaveExists(int slot)
{
    card_file file;

    unsigned char *buffer;
    doomcube_save_header_t *header;

    s32 result;
    bool exists;

    if (!cardMounted ||
        !validGame(currentGame) ||
        !validLogicalSlot(slot))
    {
        return false;
    }

    buffer =
        slotWorkBuffer;

    if (!buffer)
        return false;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
        return false;

    result = CARD_Read(
        &file,
        buffer,
        saveRegionSize(),
        saveOffset(currentGame)
    );

    CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
        return false;

    header =
        (doomcube_save_header_t *)buffer;

    exists =
        header->magic == DOOMCUBE_SAVE_MAGIC &&
        header->version == DOOMCUBE_VERSION &&
        header->valid != 0 &&
        header->size <= saveCapacity();

    return exists;
}


/* ------------------------------------------------------------------------- */
/* Write save                                                                */
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
        !validGame(currentGame) ||
        !validLogicalSlot(slot) ||
        !data ||
        size == 0)
    {
        DC_WARN(
            "DoomCube: save write rejected: mounted=%d game=%d slot=%d data=%p size=%u\n",
            cardMounted,
            (int)currentGame,
            slot,
            data,
            (unsigned int)size
        );

        return false;
    }

    if (size > saveCapacity())
    {
        DC_WARN(
            "DoomCube: %s save too large: %u > %u\n",
            gameName(currentGame),
            (unsigned int)size,
            (unsigned int)saveCapacity()
        );

        return false;
    }

    buffer =
        slotWorkBuffer;

    if (!buffer)
        return false;

    memset(
        buffer,
        0,
        saveRegionSize()
    );

    header =
        (doomcube_save_header_t *)buffer;

    header->magic =
        DOOMCUBE_SAVE_MAGIC;

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
        DC_WARN(
            "DoomCube: CARD_Open for %s save failed: %ld\n",
            gameName(currentGame),
            (long)result
        );

        return false;
    }

    result = CARD_Write(
        &file,
        buffer,
        saveRegionSize(),
        saveOffset(currentGame)
    );

    CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD_Write for %s failed: %ld\n",
            gameName(currentGame),
            (long)result
        );

        return false;
    }

    DC_LOG(
        "DoomCube: %s saved: %u bytes\n",
        gameName(currentGame),
        (unsigned int)size
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Read save                                                                 */
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
        !validGame(currentGame) ||
        !validLogicalSlot(slot))
    {
        return false;
    }

    buffer =
        slotWorkBuffer;

    if (!buffer)
        return false;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
        return false;

    result = CARD_Read(
        &file,
        buffer,
        saveRegionSize(),
        saveOffset(currentGame)
    );

    CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD_Read for %s failed: %ld\n",
            gameName(currentGame),
            (long)result
        );

        return false;
    }

    header =
        (doomcube_save_header_t *)buffer;

    if (header->magic != DOOMCUBE_SAVE_MAGIC ||
        header->version != DOOMCUBE_VERSION ||
        !header->valid ||
        header->size > saveCapacity())
    {
        return false;
    }

    if (actualSize)
    {
        *actualSize =
            header->size;
    }

    if (!output)
        return true;

    if (outputSize < header->size)
    {
        DC_WARN(
            "DoomCube: destination buffer too small for %s save\n",
            gameName(currentGame)
        );

        return false;
    }

    memcpy(
        output,
        buffer +
            sizeof(doomcube_save_header_t),
        header->size
    );

    DC_LOG(
        "DoomCube: %s loaded: %u bytes\n",
        gameName(currentGame),
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
        !validGame(currentGame) ||
        !validLogicalSlot(slot))
    {
        return 0;
    }

    buffer =
        slotWorkBuffer;

    if (!buffer)
        return 0;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
        return 0;

    result = CARD_Read(
        &file,
        buffer,
        saveRegionSize(),
        saveOffset(currentGame)
    );

    CARD_Close(
        &file
    );

    if (result == CARD_ERROR_READY)
    {
        header =
            (doomcube_save_header_t *)buffer;

        if (header->magic == DOOMCUBE_SAVE_MAGIC &&
            header->version == DOOMCUBE_VERSION &&
            header->valid &&
            header->size <= saveCapacity())
        {
            timestamp =
                header->timestamp;
        }
    }

    return timestamp;
}


/* ------------------------------------------------------------------------- */
/* Global configuration                                                      */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardWriteConfig(
    const void *data,
    size_t size)
{
    card_file file;

    unsigned char *buffer;
    doomcube_config_header_t *header;

    s32 result;

    if (!cardMounted ||
        !data ||
        size == 0)
    {
        return false;
    }

    if (size > configCapacity())
    {
        DC_WARN(
            "DoomCube: global config too large: %u > %u\n",
            (unsigned int)size,
            (unsigned int)configCapacity()
        );

        return false;
    }

    buffer =
        configWorkBuffer;

    if (!buffer)
        return false;

    memset(
        buffer,
        0,
        configRegionSize()
    );

    header =
        (doomcube_config_header_t *)buffer;

    header->magic =
        DOOMCUBE_CONFIG_MAGIC;

    header->version =
        DOOMCUBE_VERSION;

    header->valid =
        1;

    header->size =
        (uint32_t)size;

    memcpy(
        buffer +
            sizeof(doomcube_config_header_t),
        data,
        size
    );

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
        return false;

    result = CARD_Write(
        &file,
        buffer,
        configRegionSize(),
        configOffset()
    );

    CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: global config CARD_Write failed: %ld\n",
            (long)result
        );

        return false;
    }

    DC_LOG(
        "DoomCube: global config saved: %u bytes\n",
        (unsigned int)size
    );

    return true;
}


bool GC_MemoryCardReadConfig(
    void *output,
    size_t outputSize,
    size_t *actualSize)
{
    card_file file;

    unsigned char *buffer;
    doomcube_config_header_t *header;

    s32 result;

    if (actualSize)
        *actualSize = 0;

    if (!cardMounted)
        return false;

    buffer =
        configWorkBuffer;

    if (!buffer)
        return false;

    result = CARD_Open(
        CARD_SLOT,
        CARD_FILENAME,
        &file
    );

    if (result != CARD_ERROR_READY)
        return false;

    result = CARD_Read(
        &file,
        buffer,
        configRegionSize(),
        configOffset()
    );

    CARD_Close(
        &file
    );

    if (result != CARD_ERROR_READY)
        return false;

    header =
        (doomcube_config_header_t *)buffer;

    if (header->magic != DOOMCUBE_CONFIG_MAGIC ||
        header->version != DOOMCUBE_VERSION ||
        !header->valid ||
        header->size > configCapacity())
    {
        return false;
    }

    if (actualSize)
    {
        *actualSize =
            header->size;
    }

    if (!output)
        return true;

    if (outputSize < header->size)
        return false;

    memcpy(
        output,
        buffer +
            sizeof(doomcube_config_header_t),
        header->size
    );

    DC_LOG(
        "DoomCube: global config loaded: %u bytes\n",
        header->size
    );

    return true;
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

    freeWorkBuffers();

    DC_LOG(
        "DoomCube: Memory Card A unmounted\n"
    );
}

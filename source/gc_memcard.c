#include "gc_debug.h"

#include "gc_memcard.h"
#include "gc_save_v3.h"
#include "gc_save_v3_card.h"

#include <ogc/card.h>
#include <ogcsys.h>

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <time.h>

#define CARD_SLOT          CARD_SLOTA
#define CARD_FILENAME      "DOOMCUBE"

#define CARD_V3_FILENAME_A "DOOMCUBE0"
#define CARD_V3_FILENAME_B "DOOMCUBE1"
#define CARD_V3_PROBE_FILENAME "DCV3TEST"

#define CARD_GAMECODE      "DOOM"
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

#define GC_MEMCARD_PATH_MAX 256
#define GC_MEMCARD_NAME_MAX 64

static char currentIwadPath[GC_MEMCARD_PATH_MAX];
static char currentPwadPath[GC_MEMCARD_PATH_MAX];

static char currentIwadName[GC_MEMCARD_NAME_MAX];
static char currentPwadName[GC_MEMCARD_NAME_MAX];

static uint32_t currentIwadSize;
static uint32_t currentPwadSize;

static uint32_t currentIwadCrc32;
static uint32_t currentPwadCrc32;

static gc_save_v3_launch_identity_t currentV3Identity;

static bool currentLaunchIdentityValid;


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
    return
        slot >= 0 &&
        slot < GC_MEMCARD_SAVE_SLOTS;
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

        DC_INFO(
            "DoomCube: Memory Card A removed\n"
        );
    }
}



#ifdef DOOMCUBE_REGRESSION

static bool regressionV3CardContainerProbe(void)
{
    card_file file;

    unsigned char *sectorBuffer = NULL;
    unsigned char *rawBuffer = NULL;
    unsigned char *compressedBuffer = NULL;
    unsigned char *decodedRaw = NULL;

    gc_save_v3_container_header_t containerHeader;
    gc_save_v3_container_header_t decodedContainer;

    gc_save_v3_superblock_t superblock;
    gc_save_v3_superblock_t decodedSuperblock;
    gc_save_v3_superblock_t authoritativeSuperblock;

    gc_save_v3_record_header_t syntheticRecord;
    gc_save_v3_record_header_t committedRecord;
    gc_save_v3_record_header_t decodedRecord;

    gc_save_v3_slot_index_t saveIndex[
        GC_SAVE_V3_SLOT_COUNT
    ];

    gc_save_v3_launch_identity_t mismatchedIdentity;

    uint32_t authoritativeSector;
    uint32_t recordSector;

    s32 result;

    uLongf compressedCapacity;
    uLongf compressedSize;

    uLong rawCrc;

    size_t rawSize;
    size_t readCompressedSize;

    uLongf decodedRawSize;

    uint32_t recordSectors;
    uint32_t lcg;

    size_t i;

    uint64_t fileSize64;
    u32 fileSize;

    bool fileOpen = false;
    bool fileCreated = false;
    bool success = false;

    if (!cardMounted ||
        sectorSize <= 0)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE invalid card state\n"
        );

        return false;
    }

    fileSize64 =
        (uint64_t)(uint32_t)sectorSize *
        (uint64_t)GC_SAVE_V3_INITIAL_SECTORS;

    if (fileSize64 > 0xffffffffULL)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE container too large\n"
        );

        return false;
    }

    fileSize =
        (u32)fileSize64;

    /*
     * Never destroy a pre-existing v3 file during a regression probe.
     */
    result =
        CARD_Open(
            CARD_SLOT,
            CARD_V3_PROBE_FILENAME,
            &file
        );

    if (result == CARD_ERROR_READY)
    {
        CARD_Close(
            &file
        );

        DC_ERROR(
            "DoomCube: V3 CARD PROBE refused to overwrite existing %s\n",
            CARD_V3_PROBE_FILENAME
        );

        return false;
    }

    if (result != CARD_ERROR_NOFILE)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE initial CARD_Open failed: %ld\n",
            (long)result
        );

        return false;
    }

    sectorBuffer =
        memalign(
            32,
            (size_t)sectorSize
        );

    if (!sectorBuffer)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE sector allocation failed\n"
        );

        return false;
    }

    result =
        CARD_Create(
            CARD_SLOT,
            CARD_V3_PROBE_FILENAME,
            fileSize,
            &file
        );

    if (result != CARD_ERROR_READY)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE CARD_Create failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    fileOpen = true;
    fileCreated = true;

    /*
     * CARD_Create() does not guarantee that the returned card_file has
     * its directory metadata (including len) populated immediately.
     *
     * The authoritative size check happens after close + CARD_Open()
     * below, once libogc2 has reloaded the directory entry.
     */


    /* ------------------------------------------------------------------ */
    /* Sector 0: container header                                         */
    /* ------------------------------------------------------------------ */

    memset(
        &containerHeader,
        0,
        sizeof(containerHeader)
    );

    containerHeader.sector_size =
        (uint32_t)sectorSize;

    containerHeader.container_sectors =
        GC_SAVE_V3_INITIAL_SECTORS;

    containerHeader.file_index =
        0;

    memset(
        sectorBuffer,
        0,
        (size_t)sectorSize
    );

    if (!GC_SaveV3EncodeContainerHeader(
            sectorBuffer,
            (size_t)sectorSize,
            &containerHeader))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE container encode failed\n"
        );

        goto cleanup;
    }

    result =
        CARD_Write(
            &file,
            sectorBuffer,
            (u32)sectorSize,
            (u32)(
                GC_SAVE_V3_METADATA_SECTOR *
                (uint32_t)sectorSize
            )
        );

    if (result != CARD_ERROR_READY)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE metadata write failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }


    /* ------------------------------------------------------------------ */
    /* Sector 1: superblock A, generation 1                               */
    /* ------------------------------------------------------------------ */

    memset(
        &superblock,
        0,
        sizeof(superblock)
    );

    superblock.generation =
        1;

    superblock.sector_size =
        (uint32_t)sectorSize;

    superblock.container_sectors =
        GC_SAVE_V3_INITIAL_SECTORS;

    superblock.log_start_sector =
        GC_SAVE_V3_DATA_START_SECTOR;

    superblock.log_end_sector =
        GC_SAVE_V3_DATA_START_SECTOR;

    memset(
        sectorBuffer,
        0,
        (size_t)sectorSize
    );

    if (!GC_SaveV3EncodeSuperblock(
            sectorBuffer,
            (size_t)sectorSize,
            &superblock))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE superblock A encode failed\n"
        );

        goto cleanup;
    }

    result =
        CARD_Write(
            &file,
            sectorBuffer,
            (u32)sectorSize,
            (u32)(
                GC_SAVE_V3_SUPERBLOCK_A_SECTOR *
                (uint32_t)sectorSize
            )
        );

    if (result != CARD_ERROR_READY)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE superblock A write failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }


    /* ------------------------------------------------------------------ */
    /* Sector 2: superblock B, generation 0                               */
    /* ------------------------------------------------------------------ */

    superblock.generation =
        0;

    memset(
        sectorBuffer,
        0,
        (size_t)sectorSize
    );

    if (!GC_SaveV3EncodeSuperblock(
            sectorBuffer,
            (size_t)sectorSize,
            &superblock))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE superblock B encode failed\n"
        );

        goto cleanup;
    }

    result =
        CARD_Write(
            &file,
            sectorBuffer,
            (u32)sectorSize,
            (u32)(
                GC_SAVE_V3_SUPERBLOCK_B_SECTOR *
                (uint32_t)sectorSize
            )
        );

    if (result != CARD_ERROR_READY)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE superblock B write failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }


    /* ------------------------------------------------------------------ */
    /* Synthetic compressed SAVE record                                   */
    /* ------------------------------------------------------------------ */

    /*
     * Deterministic pseudo-random data keeps this a real multi-sector
     * DEFLATE payload.
     */
    rawSize =
        12000u;

    rawBuffer =
        malloc(
            rawSize
        );

    if (!rawBuffer)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE raw allocation failed\n"
        );

        goto cleanup;
    }

    lcg =
        0x12345678u;

    for (i = 0;
         i < rawSize;
         ++i)
    {
        lcg =
            lcg *
            1664525u +
            1013904223u;

        rawBuffer[i] =
            (unsigned char)(
                lcg >> 24
            );
    }

    compressedCapacity =
        compressBound(
            (uLong)rawSize
        );

    compressedBuffer =
        malloc(
            (size_t)compressedCapacity
        );

    if (!compressedBuffer)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE compressed allocation failed\n"
        );

        goto cleanup;
    }

    compressedSize =
        compressedCapacity;

    if (compress2(
            compressedBuffer,
            &compressedSize,
            rawBuffer,
            (uLong)rawSize,
            Z_BEST_COMPRESSION) !=
        Z_OK)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE compression failed\n"
        );

        goto cleanup;
    }

    rawCrc =
        crc32(
            0L,
            Z_NULL,
            0
        );

    rawCrc =
        crc32(
            rawCrc,
            rawBuffer,
            (uInt)rawSize
        );

    memset(
        &syntheticRecord,
        0,
        sizeof(syntheticRecord)
    );

    syntheticRecord.record_type =
        GC_SAVE_V3_RECORD_SAVE;

    /*
     * Slot 5 proves the sixth Doom slot is represented through the
     * production transaction path.
     */
    syntheticRecord.slot =
        5;

    syntheticRecord.timestamp =
        1704067200u;

    syntheticRecord.raw_size =
        (uint32_t)rawSize;

    syntheticRecord.raw_crc32 =
        (uint32_t)rawCrc;

    snprintf(
        syntheticRecord.identity.iwad.name,
        sizeof(syntheticRecord.identity.iwad.name),
        "%s",
        "probe-iwad.wad"
    );

    syntheticRecord.identity.iwad.size =
        12345678u;

    syntheticRecord.identity.iwad.crc32 =
        0x10203040u;

    snprintf(
        syntheticRecord.identity.pwad.name,
        sizeof(syntheticRecord.identity.pwad.name),
        "%s",
        "probe-pwad.wad"
    );

    syntheticRecord.identity.pwad.size =
        2345678u;

    syntheticRecord.identity.pwad.crc32 =
        0x50607080u;

    syntheticRecord.identity.has_pwad =
        true;

    if (!GC_SaveV3CardAppendRecord(
            &file,
            sectorBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_INITIAL_SECTORS,
            &syntheticRecord,
            compressedBuffer,
            (size_t)compressedSize,
            &committedRecord,
            &authoritativeSuperblock,
            &authoritativeSector,
            &recordSector))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE production append failed\n"
        );

        goto cleanup;
    }

    recordSectors =
        committedRecord.record_sectors;

    if (authoritativeSector !=
            GC_SAVE_V3_SUPERBLOCK_B_SECTOR ||
        authoritativeSuperblock.generation !=
            2 ||
        authoritativeSuperblock.log_end_sector !=
            recordSector +
            recordSectors)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE production append committed wrong state\n"
        );

        goto cleanup;
    }

    DC_INFO(
        "DoomCube: V3 CARD PROBE production append: "
        "sector=%u raw=%u compressed=%u sectors=%u "
        "-> B generation=2\n",
        (unsigned int)recordSector,
        (unsigned int)rawSize,
        (unsigned int)compressedSize,
        (unsigned int)recordSectors
    );


    /* ------------------------------------------------------------------ */
    /* Close and reopen: prove persistence through the CARD layer.         */
    /* ------------------------------------------------------------------ */

    result =
        CARD_Close(
            &file
        );

    fileOpen = false;

    if (result != CARD_ERROR_READY)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE first CARD_Close failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    result =
        CARD_Open(
            CARD_SLOT,
            CARD_V3_PROBE_FILENAME,
            &file
        );

    if (result != CARD_ERROR_READY)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE reopen failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    fileOpen = true;

    if ((u32)file.len != fileSize)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE reopened size mismatch: %ld != %u\n",
            (long)file.len,
            (unsigned int)fileSize
        );

        goto cleanup;
    }


    /* ------------------------------------------------------------------ */
    /* Read + validate sector 0.                                          */
    /* ------------------------------------------------------------------ */

    memset(
        sectorBuffer,
        0,
        (size_t)sectorSize
    );

    result =
        CARD_Read(
            &file,
            sectorBuffer,
            (u32)sectorSize,
            (u32)(
                GC_SAVE_V3_METADATA_SECTOR *
                (uint32_t)sectorSize
            )
        );

    if (result != CARD_ERROR_READY ||
        !GC_SaveV3DecodeContainerHeader(
            &decodedContainer,
            sectorBuffer,
            (size_t)sectorSize))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE metadata read/validation failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    if (decodedContainer.sector_size !=
            (uint32_t)sectorSize ||
        decodedContainer.container_sectors !=
            GC_SAVE_V3_INITIAL_SECTORS ||
        decodedContainer.file_index != 0)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE metadata contents invalid\n"
        );

        goto cleanup;
    }


    /* ------------------------------------------------------------------ */
    /* Read + validate superblock A.                                      */
    /* ------------------------------------------------------------------ */

    memset(
        sectorBuffer,
        0,
        (size_t)sectorSize
    );

    result =
        CARD_Read(
            &file,
            sectorBuffer,
            (u32)sectorSize,
            (u32)(
                GC_SAVE_V3_SUPERBLOCK_A_SECTOR *
                (uint32_t)sectorSize
            )
        );

    if (result != CARD_ERROR_READY ||
        !GC_SaveV3DecodeSuperblock(
            &decodedSuperblock,
            sectorBuffer,
            (size_t)sectorSize))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE superblock A validation failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    if (decodedSuperblock.generation != 1 ||
        decodedSuperblock.log_start_sector !=
            GC_SAVE_V3_DATA_START_SECTOR ||
        decodedSuperblock.log_end_sector !=
            GC_SAVE_V3_DATA_START_SECTOR)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE superblock A contents invalid\n"
        );

        goto cleanup;
    }


    /* ------------------------------------------------------------------ */
    /* Read + validate superblock B.                                      */
    /* ------------------------------------------------------------------ */

    memset(
        sectorBuffer,
        0,
        (size_t)sectorSize
    );

    result =
        CARD_Read(
            &file,
            sectorBuffer,
            (u32)sectorSize,
            (u32)(
                GC_SAVE_V3_SUPERBLOCK_B_SECTOR *
                (uint32_t)sectorSize
            )
        );

    if (result != CARD_ERROR_READY ||
        !GC_SaveV3DecodeSuperblock(
            &decodedSuperblock,
            sectorBuffer,
            (size_t)sectorSize))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE superblock B validation failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    if (decodedSuperblock.generation != 2 ||
        decodedSuperblock.log_start_sector !=
            GC_SAVE_V3_DATA_START_SECTOR ||
        decodedSuperblock.log_end_sector !=
            GC_SAVE_V3_DATA_START_SECTOR +
            recordSectors)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE superblock B contents invalid\n"
        );

        goto cleanup;
    }

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            &file,
            sectorBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_INITIAL_SECTORS,
            &authoritativeSuperblock,
            &authoritativeSector))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE authoritative selector failed\n"
        );

        goto cleanup;
    }

    if (authoritativeSector !=
            GC_SAVE_V3_SUPERBLOCK_B_SECTOR ||
        authoritativeSuperblock.generation != 2 ||
        authoritativeSuperblock.log_end_sector !=
            GC_SAVE_V3_DATA_START_SECTOR +
            recordSectors)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE authoritative selector chose wrong state\n"
        );

        goto cleanup;
    }

    DC_INFO(
        "DoomCube: V3 CARD PROBE production selector: "
        "B generation=2 log_end=%u\n",
        (unsigned int)authoritativeSuperblock.log_end_sector
    );


    /* ------------------------------------------------------------------ */
    /* Read committed record through production helper.                   */
    /* ------------------------------------------------------------------ */

    if (!GC_SaveV3CardReadRecord(
            &file,
            sectorBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_INITIAL_SECTORS,
            authoritativeSuperblock.log_end_sector,
            recordSector,
            &decodedRecord,
            compressedBuffer,
            (size_t)compressedCapacity,
            &readCompressedSize))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE production record read failed\n"
        );

        goto cleanup;
    }

    if (decodedRecord.generation !=
            committedRecord.generation ||
        decodedRecord.slot !=
            committedRecord.slot ||
        decodedRecord.raw_size !=
            committedRecord.raw_size ||
        decodedRecord.compressed_size !=
            committedRecord.compressed_size ||
        decodedRecord.raw_crc32 !=
            committedRecord.raw_crc32 ||
        decodedRecord.compressed_crc32 !=
            committedRecord.compressed_crc32 ||
        decodedRecord.record_sectors !=
            committedRecord.record_sectors ||
        !GC_SaveV3LaunchIdentityEqual(
            &decodedRecord.identity,
            &committedRecord.identity) ||
        readCompressedSize !=
            committedRecord.compressed_size)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE production record metadata mismatch\n"
        );

        goto cleanup;
    }

    decodedRaw =
        malloc(
            rawSize
        );

    if (!decodedRaw)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE decoded allocation failed\n"
        );

        goto cleanup;
    }

    decodedRawSize =
        (uLongf)rawSize;

    if (uncompress(
            decodedRaw,
            &decodedRawSize,
            compressedBuffer,
            (uLong)readCompressedSize) !=
            Z_OK ||
        decodedRawSize !=
            (uLongf)rawSize ||
        memcmp(
            decodedRaw,
            rawBuffer,
            rawSize) != 0)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE production record decompression mismatch\n"
        );

        goto cleanup;
    }

    rawCrc =
        crc32(
            0L,
            Z_NULL,
            0
        );

    rawCrc =
        crc32(
            rawCrc,
            decodedRaw,
            (uInt)decodedRawSize
        );

    if ((uint32_t)rawCrc !=
        decodedRecord.raw_crc32)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE production raw CRC mismatch\n"
        );

        goto cleanup;
    }

    DC_INFO(
        "DoomCube: V3 CARD PROBE production record read: "
        "CRC + decompression byte-perfect\n"
    );


    /* ------------------------------------------------------------------ */
    /* Production six-slot index                                         */
    /* ------------------------------------------------------------------ */

    if (!GC_SaveV3CardBuildSaveIndex(
            &file,
            sectorBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_INITIAL_SECTORS,
            &committedRecord.identity,
            saveIndex,
            &authoritativeSuperblock,
            &authoritativeSector))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE save index build failed\n"
        );

        goto cleanup;
    }

    if (authoritativeSector !=
            GC_SAVE_V3_SUPERBLOCK_B_SECTOR ||
        authoritativeSuperblock.generation !=
            committedRecord.generation)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE save index used wrong superblock\n"
        );

        goto cleanup;
    }

    for (i = 0;
         i < GC_SAVE_V3_SLOT_COUNT;
         ++i)
    {
        if (i == 5)
        {
            if (!saveIndex[i].present ||
                saveIndex[i].record_sector !=
                    recordSector ||
                saveIndex[i].record.generation !=
                    committedRecord.generation ||
                saveIndex[i].record.slot != 5)
            {
                DC_ERROR(
                    "DoomCube: V3 CARD PROBE slot 5 index mismatch\n"
                );

                goto cleanup;
            }
        }
        else if (saveIndex[i].present)
        {
            DC_ERROR(
                "DoomCube: V3 CARD PROBE unexpected indexed slot %u\n",
                (unsigned int)i
            );

            goto cleanup;
        }
    }

    DC_INFO(
        "DoomCube: V3 CARD PROBE production index: "
        "slot5 generation=%u sector=%u, slots0-4 empty\n",
        (unsigned int)saveIndex[5].record.generation,
        (unsigned int)saveIndex[5].record_sector
    );


    /*
     * Keep the readable names identical and change only the PWAD
     * content fingerprint. The save must disappear.
     */
    mismatchedIdentity =
        committedRecord.identity;

    mismatchedIdentity.pwad.crc32 ^=
        0x00000001u;

    if (!GC_SaveV3CardBuildSaveIndex(
            &file,
            sectorBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_INITIAL_SECTORS,
            &mismatchedIdentity,
            saveIndex,
            NULL,
            NULL))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE mismatched identity scan failed\n"
        );

        goto cleanup;
    }

    for (i = 0;
         i < GC_SAVE_V3_SLOT_COUNT;
         ++i)
    {
        if (saveIndex[i].present)
        {
            DC_ERROR(
                "DoomCube: V3 CARD PROBE cross-PWAD slot leaked: %u\n",
                (unsigned int)i
            );

            goto cleanup;
        }
    }

    DC_INFO(
        "DoomCube: V3 CARD PROBE identity isolation PASS: "
        "mismatched PWAD fingerprint -> 0 slots\n"
    );


    /* ------------------------------------------------------------------ */
    /* Recovery: corrupt newer B and prove valid A wins.                  */
    /* ------------------------------------------------------------------ */

    memset(
        sectorBuffer,
        0,
        (size_t)sectorSize
    );

    result =
        CARD_Read(
            &file,
            sectorBuffer,
            (u32)sectorSize,
            (u32)(
                GC_SAVE_V3_SUPERBLOCK_B_SECTOR *
                (uint32_t)sectorSize
            )
        );

    if (result != CARD_ERROR_READY)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE recovery read B failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    /*
     * Damage the encoded magic without updating its CRC.
     */
    sectorBuffer[0] ^=
        0x01u;

    result =
        CARD_Write(
            &file,
            sectorBuffer,
            (u32)sectorSize,
            (u32)(
                GC_SAVE_V3_SUPERBLOCK_B_SECTOR *
                (uint32_t)sectorSize
            )
        );

    if (result != CARD_ERROR_READY)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE recovery corruption write failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            &file,
            sectorBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_INITIAL_SECTORS,
            &authoritativeSuperblock,
            &authoritativeSector))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE recovery selector failed\n"
        );

        goto cleanup;
    }

    if (authoritativeSector !=
            GC_SAVE_V3_SUPERBLOCK_A_SECTOR ||
        authoritativeSuperblock.generation != 1 ||
        authoritativeSuperblock.log_end_sector !=
            GC_SAVE_V3_DATA_START_SECTOR)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE recovery selector did not fall back to A\n"
        );

        goto cleanup;
    }

    if (!GC_SaveV3CardBuildSaveIndex(
            &file,
            sectorBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_INITIAL_SECTORS,
            &committedRecord.identity,
            saveIndex,
            &authoritativeSuperblock,
            &authoritativeSector))
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE recovery index build failed\n"
        );

        goto cleanup;
    }

    if (authoritativeSector !=
            GC_SAVE_V3_SUPERBLOCK_A_SECTOR ||
        authoritativeSuperblock.generation !=
            1)
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE recovery index used wrong state\n"
        );

        goto cleanup;
    }

    for (i = 0;
         i < GC_SAVE_V3_SLOT_COUNT;
         ++i)
    {
        if (saveIndex[i].present)
        {
            DC_ERROR(
                "DoomCube: V3 CARD PROBE recovery index exposed slot %u\n",
                (unsigned int)i
            );

            goto cleanup;
        }
    }

    DC_INFO(
        "DoomCube: V3 CARD PROBE recovery index PASS: "
        "A generation=1 exposes 0 slots\n"
    );

    DC_INFO(
        "DoomCube: V3 CARD PROBE recovery PASS: "
        "corrupt newer B -> A generation=1\n"
    );

    success = true;


cleanup:

    if (fileOpen)
    {
        result =
            CARD_Close(
                &file
            );

        fileOpen = false;

        if (result != CARD_ERROR_READY)
        {
            DC_ERROR(
                "DoomCube: V3 CARD PROBE cleanup CARD_Close failed: %ld\n",
                (long)result
            );

            success = false;
        }
    }

    if (fileCreated)
    {
        result =
            CARD_Delete(
                CARD_SLOT,
                CARD_V3_PROBE_FILENAME
            );

        if (result != CARD_ERROR_READY)
        {
            DC_ERROR(
                "DoomCube: V3 CARD PROBE cleanup CARD_Delete failed: %ld\n",
                (long)result
            );

            success = false;
        }
    }

    free(
        decodedRaw
    );

    free(
        compressedBuffer
    );

    free(
        rawBuffer
    );

    free(
        sectorBuffer
    );

    if (success)
    {
        DC_INFO(
            "DoomCube: V3 CARD PROBE PASS: "
            "%u-sector container, "
            "record raw=%u compressed=%u record_sectors=%u, "
            "generation 2 committed\n",
            (unsigned int)GC_SAVE_V3_INITIAL_SECTORS,
            (unsigned int)rawSize,
            (unsigned int)compressedSize,
            (unsigned int)recordSectors
        );
    }

    return success;
}

#endif


/* ------------------------------------------------------------------------- */
/* Active launch identity                                                    */
/* ------------------------------------------------------------------------- */

static bool fingerprintFile(
    const char *path,
    uint32_t *sizeOut,
    uint32_t *crcOut)
{
    static unsigned char buffer[8192]
        __attribute__((aligned(32)));

    FILE *fp;
    size_t count;
    uint64_t total;
    uLong crc;

    if (!path ||
        !sizeOut ||
        !crcOut)
    {
        return false;
    }

    fp =
        fopen(
            path,
            "rb"
        );

    if (!fp)
    {
        DC_WARN(
            "DoomCube: could not fingerprint %s\n",
            path
        );

        return false;
    }

    total = 0;

    crc =
        crc32(
            0L,
            Z_NULL,
            0
        );

    while ((count =
        fread(
            buffer,
            1,
            sizeof(buffer),
            fp)) > 0)
    {
        total +=
            (uint64_t)count;

        if (total > 0xffffffffULL)
        {
            fclose(fp);

            DC_WARN(
                "DoomCube: fingerprint file too large: %s\n",
                path
            );

            return false;
        }

        crc =
            crc32(
                crc,
                buffer,
                (uInt)count
            );
    }

    if (ferror(fp))
    {
        fclose(fp);

        DC_WARN(
            "DoomCube: error fingerprinting %s\n",
            path
        );

        return false;
    }

    fclose(fp);

    *sizeOut =
        (uint32_t)total;

    *crcOut =
        (uint32_t)crc;

    return true;
}


void GC_MemoryCardSetLaunchIdentity(
    const char *iwadPath,
    const char *pwadPath)
{
    const char *name;

    currentLaunchIdentityValid = false;

    memset(
        &currentV3Identity,
        0,
        sizeof(currentV3Identity)
    );

    currentIwadPath[0] = '\0';
    currentPwadPath[0] = '\0';

    currentIwadName[0] = '\0';
    currentPwadName[0] = '\0';

    currentIwadSize = 0;
    currentPwadSize = 0;

    currentIwadCrc32 = 0;
    currentPwadCrc32 = 0;

    if (!iwadPath ||
        iwadPath[0] == '\0')
    {
        DC_WARN(
            "DoomCube: invalid save launch identity\n"
        );

        return;
    }

    snprintf(
        currentIwadPath,
        sizeof(currentIwadPath),
        "%s",
        iwadPath
    );

    name =
        baseName(
            currentIwadPath
        );

    snprintf(
        currentIwadName,
        sizeof(currentIwadName),
        "%s",
        name
    );

    if (!fingerprintFile(
            currentIwadPath,
            &currentIwadSize,
            &currentIwadCrc32))
    {
        DC_WARN(
            "DoomCube: IWAD save identity unavailable\n"
        );

        return;
    }

    snprintf(
        currentV3Identity.iwad.name,
        sizeof(currentV3Identity.iwad.name),
        "%s",
        currentIwadName
    );

    currentV3Identity.iwad.size =
        currentIwadSize;

    currentV3Identity.iwad.crc32 =
        currentIwadCrc32;

    if (pwadPath &&
        pwadPath[0] != '\0')
    {
        snprintf(
            currentPwadPath,
            sizeof(currentPwadPath),
            "%s",
            pwadPath
        );

        name =
            baseName(
                currentPwadPath
            );

        snprintf(
            currentPwadName,
            sizeof(currentPwadName),
            "%s",
            name
        );

        if (!fingerprintFile(
                currentPwadPath,
                &currentPwadSize,
                &currentPwadCrc32))
        {
            DC_WARN(
                "DoomCube: PWAD save identity unavailable\n"
            );

            return;
        }

        snprintf(
            currentV3Identity.pwad.name,
            sizeof(currentV3Identity.pwad.name),
            "%s",
            currentPwadName
        );

        currentV3Identity.pwad.size =
            currentPwadSize;

        currentV3Identity.pwad.crc32 =
            currentPwadCrc32;

        currentV3Identity.has_pwad =
            true;
    }

    currentLaunchIdentityValid = true;

    DC_INFO(
        "DoomCube: save identity IWAD: %s size=%u crc32=%08x\n",
        currentIwadName,
        (unsigned int)currentIwadSize,
        (unsigned int)currentIwadCrc32
    );

    DC_INFO(
        "DoomCube: v3 identity key: "
        "iwad=%08x/%u pwad=%08x/%u has_pwad=%u\n",
        (unsigned int)currentV3Identity.iwad.crc32,
        (unsigned int)currentV3Identity.iwad.size,
        (unsigned int)currentV3Identity.pwad.crc32,
        (unsigned int)currentV3Identity.pwad.size,
        currentV3Identity.has_pwad ? 1u : 0u
    );

    if (currentPwadPath[0] != '\0')
    {
        DC_INFO(
            "DoomCube: save identity PWAD: %s size=%u crc32=%08x\n",
            currentPwadName,
            (unsigned int)currentPwadSize,
            (unsigned int)currentPwadCrc32
        );
    }
    else
    {
        DC_INFO(
            "DoomCube: save identity PWAD: <none>\n"
        );
    }
}


/* ------------------------------------------------------------------------- */
/* Legacy active game                                                        */
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

    if (strcmp(iwadPath, "dvd:/data/wad/doom1.wad") == 0)
    {
        currentGame =
            GC_SAVEGAME_DOOM1;

        return true;
    }

    if (strcmp(iwadPath, "dvd:/data/wad/doom.wad") == 0)
    {
        currentGame =
            GC_SAVEGAME_DOOM;

        return true;
    }

    if (strcmp(iwadPath, "dvd:/data/wad/doom2.wad") == 0)
    {
        currentGame =
            GC_SAVEGAME_DOOM2;

        return true;
    }

    if (strcmp(iwadPath, "dvd:/data/wad/tnt.wad") == 0)
    {
        currentGame =
            GC_SAVEGAME_TNT;

        return true;
    }

    if (strcmp(iwadPath, "dvd:/data/wad/plutonia.wad") == 0)
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


/* ------------------------------------------------------------------------- */
/* Production v3 container                                                   */
/* ------------------------------------------------------------------------- */

static bool validateOpenV3Container(
    card_file *file,
    unsigned char *scratch,
    uint32_t expectedFileIndex,
    uint32_t *containerSectorsOut,
    uint32_t *generationOut)
{
    gc_save_v3_container_header_t header;
    gc_save_v3_superblock_t superblock;

    uint32_t containerSectors;
    uint32_t superblockSector;

    s32 result;

    if (!file ||
        !scratch ||
        sectorSize <= 0 ||
        file->len <= 0)
    {
        return false;
    }

    if (((uint32_t)file->len %
            (uint32_t)sectorSize) != 0)
    {
        DC_WARN(
            "DoomCube: v3 container size is not sector aligned: %ld\n",
            (long)file->len
        );

        return false;
    }

    containerSectors =
        (uint32_t)file->len /
        (uint32_t)sectorSize;

    if (containerSectors <
        GC_SAVE_V3_DATA_START_SECTOR)
    {
        DC_WARN(
            "DoomCube: v3 container too small: %u sectors\n",
            (unsigned int)containerSectors
        );

        return false;
    }

    result =
        CARD_Read(
            file,
            scratch,
            (u32)sectorSize,
            (u32)(
                GC_SAVE_V3_METADATA_SECTOR *
                (uint32_t)sectorSize
            )
        );

    if (result != CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: v3 metadata CARD_Read failed: %ld\n",
            (long)result
        );

        return false;
    }

    memset(
        &header,
        0,
        sizeof(header)
    );

    if (!GC_SaveV3DecodeContainerHeader(
            &header,
            scratch,
            (size_t)sectorSize))
    {
        DC_WARN(
            "DoomCube: invalid/corrupt v3 container header\n"
        );

        return false;
    }

    if (header.sector_size !=
            (uint32_t)sectorSize ||
        header.container_sectors !=
            containerSectors ||
        header.file_index !=
            expectedFileIndex)
    {
        DC_WARN(
            "DoomCube: v3 container geometry mismatch: "
            "sector=%u/%u blocks=%u/%u index=%u/%u\n",
            (unsigned int)header.sector_size,
            (unsigned int)sectorSize,
            (unsigned int)header.container_sectors,
            (unsigned int)containerSectors,
            (unsigned int)header.file_index,
            (unsigned int)expectedFileIndex
        );

        return false;
    }

    memset(
        &superblock,
        0,
        sizeof(superblock)
    );

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            file,
            scratch,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            containerSectors,
            &superblock,
            &superblockSector))
    {
        DC_WARN(
            "DoomCube: no valid authoritative v3 superblock\n"
        );

        return false;
    }

    if (containerSectorsOut)
    {
        *containerSectorsOut =
            containerSectors;
    }

    if (generationOut)
    {
        *generationOut =
            superblock.generation;
    }

    return true;
}


static bool writeInitialV3Superblock(
    card_file *file,
    unsigned char *scratch,
    uint32_t superblockSector,
    uint32_t generation)
{
    gc_save_v3_superblock_t superblock;

    s32 result;

    memset(
        &superblock,
        0,
        sizeof(superblock)
    );

    superblock.generation =
        generation;

    superblock.sector_size =
        (uint32_t)sectorSize;

    superblock.container_sectors =
        GC_SAVE_V3_INITIAL_SECTORS;

    superblock.log_start_sector =
        GC_SAVE_V3_DATA_START_SECTOR;

    superblock.log_end_sector =
        GC_SAVE_V3_DATA_START_SECTOR;

    memset(
        scratch,
        0,
        (size_t)sectorSize
    );

    if (!GC_SaveV3EncodeSuperblock(
            scratch,
            (size_t)sectorSize,
            &superblock))
    {
        return false;
    }

    result =
        CARD_Write(
            file,
            scratch,
            (u32)sectorSize,
            (u32)(
                superblockSector *
                (uint32_t)sectorSize
            )
        );

    return
        result ==
        CARD_ERROR_READY;
}


static bool ensureProductionV3Container(void)
{
    card_file file;

    gc_save_v3_container_header_t header;

    unsigned char *scratch =
        configWorkBuffer;

    uint64_t fileSize64;
    u32 fileSize;

    uint32_t containerSectors = 0;
    uint32_t generation = 0;

    s32 result;
    s32 closeResult;

    bool fileOpen = false;
    bool created = false;
    bool success = false;

    if (!cardMounted ||
        sectorSize <= 0 ||
        !scratch)
    {
        return false;
    }

    /*
     * First try the production file already on the card.
     *
     * Existing corrupt/incompatible data is never deleted automatically.
     */
    result =
        CARD_Open(
            CARD_SLOT,
            CARD_V3_FILENAME_A,
            &file
        );

    if (result == CARD_ERROR_READY)
    {
        fileOpen = true;

        success =
            validateOpenV3Container(
                &file,
                scratch,
                0,
                &containerSectors,
                &generation
            );

        closeResult =
            CARD_Close(
                &file
            );

        fileOpen = false;

        if (closeResult !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: CARD_Close for %s failed: %ld\n",
                CARD_V3_FILENAME_A,
                (long)closeResult
            );

            return false;
        }

        if (!success)
        {
            DC_WARN(
                "DoomCube: existing %s is invalid; "
                "preserving it and disabling saves\n",
                CARD_V3_FILENAME_A
            );

            return false;
        }

        DC_INFO(
            "DoomCube: existing v3 container validated: "
            "%s blocks=%u generation=%u\n",
            CARD_V3_FILENAME_A,
            (unsigned int)containerSectors,
            (unsigned int)generation
        );

        return true;
    }

    if (result !=
        CARD_ERROR_NOFILE)
    {
        DC_WARN(
            "DoomCube: CARD_Open for %s failed: %ld\n",
            CARD_V3_FILENAME_A,
            (long)result
        );

        return false;
    }

    /*
     * No v3 data yet: create only the 16-block initial container.
     */
    fileSize64 =
        (uint64_t)(uint32_t)sectorSize *
        (uint64_t)GC_SAVE_V3_INITIAL_SECTORS;

    if (fileSize64 >
        0xffffffffULL)
    {
        DC_WARN(
            "DoomCube: initial v3 container size overflow\n"
        );

        return false;
    }

    fileSize =
        (u32)fileSize64;

    result =
        CARD_Create(
            CARD_SLOT,
            CARD_V3_FILENAME_A,
            fileSize,
            &file
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD_Create %s failed: %ld\n",
            CARD_V3_FILENAME_A,
            (long)result
        );

        return false;
    }

    fileOpen = true;
    created = true;

    /*
     * Sector 0: container metadata.
     */
    memset(
        &header,
        0,
        sizeof(header)
    );

    header.sector_size =
        (uint32_t)sectorSize;

    header.container_sectors =
        GC_SAVE_V3_INITIAL_SECTORS;

    header.file_index =
        0;

    memset(
        scratch,
        0,
        (size_t)sectorSize
    );

    if (!GC_SaveV3EncodeContainerHeader(
            scratch,
            (size_t)sectorSize,
            &header))
    {
        DC_WARN(
            "DoomCube: v3 container header encode failed\n"
        );

        goto cleanup;
    }

    result =
        CARD_Write(
            &file,
            scratch,
            (u32)sectorSize,
            (u32)(
                GC_SAVE_V3_METADATA_SECTOR *
                (uint32_t)sectorSize
            )
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: v3 metadata write failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    /*
     * Sector 1 is authoritative generation 1.
     * Sector 2 is its valid generation-0 fallback.
     */
    if (!writeInitialV3Superblock(
            &file,
            scratch,
            GC_SAVE_V3_SUPERBLOCK_A_SECTOR,
            1))
    {
        DC_WARN(
            "DoomCube: initial v3 superblock A write failed\n"
        );

        goto cleanup;
    }

    if (!writeInitialV3Superblock(
            &file,
            scratch,
            GC_SAVE_V3_SUPERBLOCK_B_SECTOR,
            0))
    {
        DC_WARN(
            "DoomCube: initial v3 superblock B write failed\n"
        );

        goto cleanup;
    }

    closeResult =
        CARD_Close(
            &file
        );

    fileOpen = false;

    if (closeResult !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: initial v3 CARD_Close failed: %ld\n",
            (long)closeResult
        );

        goto cleanup;
    }

    /*
     * Reopen from directory metadata and validate exactly as a later boot
     * will. A successful create is not trusted until this passes.
     */
    result =
        CARD_Open(
            CARD_SLOT,
            CARD_V3_FILENAME_A,
            &file
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: created v3 container could not be reopened: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    fileOpen = true;

    if (!validateOpenV3Container(
            &file,
            scratch,
            0,
            &containerSectors,
            &generation))
    {
        DC_WARN(
            "DoomCube: created v3 container failed validation\n"
        );

        goto cleanup;
    }

    closeResult =
        CARD_Close(
            &file
        );

    fileOpen = false;

    if (closeResult !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: validated v3 CARD_Close failed: %ld\n",
            (long)closeResult
        );

        goto cleanup;
    }

    success = true;

    DC_INFO(
        "DoomCube: v3 container created: "
        "%s blocks=%u generation=%u\n",
        CARD_V3_FILENAME_A,
        (unsigned int)containerSectors,
        (unsigned int)generation
    );


cleanup:

    if (fileOpen)
    {
        CARD_Close(
            &file
        );

        fileOpen = false;
    }

    if (!success &&
        created)
    {
        /*
         * Only remove a file created by this failed invocation.
         * Never delete a file that existed before boot.
         */
        result =
            CARD_Delete(
                CARD_SLOT,
                CARD_V3_FILENAME_A
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: failed to clean incomplete %s: %ld\n",
                CARD_V3_FILENAME_A,
                (long)result
            );
        }
    }

    return success;
}


/* ------------------------------------------------------------------------- */
/* Memory card initialization                                                */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardInit(void)
{
#ifdef DOOMCUBE_REGRESSION
    if (!GC_SaveV3CodecSelfTest())
    {
        DC_ERROR(
            "DoomCube: V3 CODEC SELFTEST FAILED\n"
        );

        return false;
    }

    DC_INFO(
        "DoomCube: V3 CODEC SELFTEST PASS: "
        "container + superblock + record\n"
    );
#endif

    card_file legacyFile;

    s32 result;
    s32 memorySize = 0;

    DC_DEBUG(
        "DoomCube: ---- MEMORY CARD A ----\n"
    );

    DC_DEBUG(
        "DoomCube: initializing memory card...\n"
    );

    result =
        CARD_Init(
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
        result =
            CARD_ProbeEx(
                CARD_SLOT,
                &memorySize,
                &sectorSize
            );
    }
    while (result ==
        CARD_ERROR_BUSY);

    if (result !=
        CARD_ERROR_READY)
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

    /*
     * Keep the old buffers during the transition because the legacy
     * config backend and v2 fallback code still use them.
     */
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

    result =
        CARD_Mount(
            CARD_SLOT,
            cardWorkArea,
            cardRemoved
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD_Mount failed: %ld\n",
            (long)result
        );

        freeWorkBuffers();

        return false;
    }

    cardMounted = true;

    DC_INFO(
        "DoomCube: Memory Card A mounted\n"
    );

#ifdef DOOMCUBE_REGRESSION
    if (!regressionV3CardContainerProbe())
    {
        DC_ERROR(
            "DoomCube: V3 CARD PROBE FAILED\n"
        );

        return false;
    }
#endif

    if (!ensureProductionV3Container())
    {
        DC_WARN(
            "DoomCube: v3 save container unavailable; "
            "continuing without saves\n"
        );

        return false;
    }

    /*
     * Migration rule: never recreate, resize or delete legacy DOOMCUBE.
     *
     * It may still contain v2 saves/configuration. Later migration code
     * can inspect it deliberately; init merely preserves it.
     */
    result =
        CARD_Open(
            CARD_SLOT,
            CARD_FILENAME,
            &legacyFile
        );

    if (result ==
        CARD_ERROR_READY)
    {
        long legacySize =
            (long)legacyFile.len;

        CARD_Close(
            &legacyFile
        );

        DC_INFO(
            "DoomCube: legacy v2 container preserved: "
            "%s size=%ld bytes\n",
            CARD_FILENAME,
            legacySize
        );
    }
    else if (result ==
        CARD_ERROR_NOFILE)
    {
        DC_DEBUG(
            "DoomCube: legacy v2 container not present\n"
        );
    }
    else
    {
        /*
         * The production v3 container is already valid, so failure to
         * inspect legacy data must not destroy current save availability.
         */
        DC_WARN(
            "DoomCube: legacy %s inspection failed: %ld\n",
            CARD_FILENAME,
            (long)result
        );
    }

    return true;
}


/* ------------------------------------------------------------------------- */
/* Live v3 save-container access                                             */
/* ------------------------------------------------------------------------- */

static bool openProductionV3Container(
    card_file *file,
    uint32_t *containerSectors)
{
    uint32_t sectors = 0;

    s32 result;

    if (!file ||
        !cardMounted ||
        sectorSize <= 0 ||
        !configWorkBuffer)
    {
        return false;
    }

    result =
        CARD_Open(
            CARD_SLOT,
            CARD_V3_FILENAME_A,
            file
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD_Open %s failed: %ld\n",
            CARD_V3_FILENAME_A,
            (long)result
        );

        return false;
    }

    if (!validateOpenV3Container(
            file,
            configWorkBuffer,
            0,
            &sectors,
            NULL))
    {
        CARD_Close(
            file
        );

        DC_WARN(
            "DoomCube: live v3 container validation failed\n"
        );

        return false;
    }

    if (containerSectors)
    {
        *containerSectors =
            sectors;
    }

    return true;
}


/* ------------------------------------------------------------------------- */
/* Save exists                                                               */
/* ------------------------------------------------------------------------- */

bool GC_MemoryCardSaveExists(int slot)
{
    card_file file;

    gc_save_v3_slot_index_t slots[
        GC_SAVE_V3_SLOT_COUNT
    ];

    uint32_t containerSectors;

    bool success;

    if (!cardMounted ||
        !currentLaunchIdentityValid ||
        !validLogicalSlot(slot))
    {
        return false;
    }

    if (!openProductionV3Container(
            &file,
            &containerSectors))
    {
        return false;
    }

    success =
        GC_SaveV3CardBuildSaveIndex(
            &file,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            containerSectors,
            &currentV3Identity,
            slots,
            NULL,
            NULL
        );

    CARD_Close(
        &file
    );

    if (!success)
    {
        DC_WARN(
            "DoomCube: v3 save index failed while checking slot %d\n",
            slot
        );

        return false;
    }

    return
        slots[slot].present;
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

    gc_save_v3_record_header_t record;
    gc_save_v3_record_header_t committedRecord;

    gc_save_v3_superblock_t committedSuperblock;

    unsigned char *compressedData = NULL;

    uLongf compressedCapacity;
    uLongf compressedSize;

    uLong rawCrc;

    uint32_t containerSectors;
    uint32_t committedSuperblockSector;
    uint32_t recordSector;

    bool success = false;

    if (!cardMounted ||
        !currentLaunchIdentityValid ||
        !validLogicalSlot(slot) ||
        !data ||
        size == 0 ||
        size > 0xffffffffu)
    {
        DC_WARN(
            "DoomCube: v3 save write rejected: "
            "mounted=%d identity=%d slot=%d data=%p size=%u\n",
            cardMounted,
            currentLaunchIdentityValid,
            slot,
            data,
            (unsigned int)size
        );

        return false;
    }

    compressedCapacity =
        compressBound(
            (uLong)size
        );

    if (compressedCapacity == 0)
    {
        DC_WARN(
            "DoomCube: v3 compressBound failed for slot %d\n",
            slot
        );

        return false;
    }

    compressedData =
        malloc(
            (size_t)compressedCapacity
        );

    if (!compressedData)
    {
        DC_WARN(
            "DoomCube: v3 compressed-save allocation failed: %u bytes\n",
            (unsigned int)compressedCapacity
        );

        return false;
    }

    compressedSize =
        compressedCapacity;

    if (compress2(
            compressedData,
            &compressedSize,
            data,
            (uLong)size,
            Z_BEST_COMPRESSION) !=
        Z_OK)
    {
        DC_WARN(
            "DoomCube: v3 save compression failed for slot %d\n",
            slot
        );

        goto cleanup;
    }

    rawCrc =
        crc32(
            0L,
            Z_NULL,
            0
        );

    rawCrc =
        crc32(
            rawCrc,
            data,
            (uInt)size
        );

    memset(
        &record,
        0,
        sizeof(record)
    );

    record.record_type =
        GC_SAVE_V3_RECORD_SAVE;

    record.slot =
        (uint32_t)slot;

    record.timestamp =
        (uint32_t)time(NULL);

    record.raw_size =
        (uint32_t)size;

    record.raw_crc32 =
        (uint32_t)rawCrc;

    record.identity =
        currentV3Identity;

    if (!openProductionV3Container(
            &file,
            &containerSectors))
    {
        goto cleanup;
    }

    success =
        GC_SaveV3CardAppendRecord(
            &file,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            containerSectors,
            &record,
            compressedData,
            (size_t)compressedSize,
            &committedRecord,
            &committedSuperblock,
            &committedSuperblockSector,
            &recordSector
        );

    CARD_Close(
        &file
    );

    if (!success)
    {
        DC_WARN(
            "DoomCube: v3 save append failed for slot %d "
            "(container=%u blocks; growth not yet available)\n",
            slot,
            (unsigned int)containerSectors
        );

        goto cleanup;
    }

    DC_INFO(
        "DoomCube: v3 save committed: "
        "slot=%d raw=%u compressed=%u sectors=%u "
        "record_sector=%u generation=%u superblock=%c\n",
        slot,
        (unsigned int)size,
        (unsigned int)compressedSize,
        (unsigned int)committedRecord.record_sectors,
        (unsigned int)recordSector,
        (unsigned int)committedRecord.generation,
        committedSuperblockSector ==
            GC_SAVE_V3_SUPERBLOCK_A_SECTOR
            ? 'A'
            : 'B'
    );


cleanup:

    free(
        compressedData
    );

    return success;
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

    gc_save_v3_slot_index_t slots[
        GC_SAVE_V3_SLOT_COUNT
    ];

    gc_save_v3_superblock_t active;
    gc_save_v3_record_header_t record;

    unsigned char *compressedData = NULL;

    size_t compressedSize = 0;

    uLongf decodedSize;
    uLong rawCrc;

    uint32_t containerSectors;

    bool success = false;

    if (actualSize)
    {
        *actualSize = 0;
    }

    if (!cardMounted ||
        !currentLaunchIdentityValid ||
        !validLogicalSlot(slot))
    {
        return false;
    }

    if (!openProductionV3Container(
            &file,
            &containerSectors))
    {
        return false;
    }

    if (!GC_SaveV3CardBuildSaveIndex(
            &file,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            containerSectors,
            &currentV3Identity,
            slots,
            &active,
            NULL))
    {
        DC_WARN(
            "DoomCube: v3 save index failed while loading slot %d\n",
            slot
        );

        goto close_file;
    }

    if (!slots[slot].present)
    {
        goto close_file;
    }

    record =
        slots[slot].record;

    if (actualSize)
    {
        *actualSize =
            record.raw_size;
    }

    /*
     * Size-query path used by the stdio shim.
     */
    if (!output)
    {
        success = true;
        goto close_file;
    }

    if (outputSize <
        record.raw_size)
    {
        DC_WARN(
            "DoomCube: destination buffer too small for slot %d: "
            "%u < %u\n",
            slot,
            (unsigned int)outputSize,
            (unsigned int)record.raw_size
        );

        goto close_file;
    }

    compressedData =
        malloc(
            record.compressed_size
        );

    if (!compressedData)
    {
        DC_WARN(
            "DoomCube: compressed-load allocation failed: %u bytes\n",
            (unsigned int)record.compressed_size
        );

        goto close_file;
    }

    if (!GC_SaveV3CardReadRecord(
            &file,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            containerSectors,
            active.log_end_sector,
            slots[slot].record_sector,
            &record,
            compressedData,
            slots[slot].record.compressed_size,
            &compressedSize))
    {
        DC_WARN(
            "DoomCube: v3 record read failed for slot %d\n",
            slot
        );

        goto close_file;
    }

    if (compressedSize !=
        record.compressed_size)
    {
        DC_WARN(
            "DoomCube: v3 compressed size mismatch for slot %d\n",
            slot
        );

        goto close_file;
    }

    decodedSize =
        (uLongf)record.raw_size;

    if (uncompress(
            output,
            &decodedSize,
            compressedData,
            (uLong)compressedSize) !=
        Z_OK)
    {
        DC_WARN(
            "DoomCube: v3 decompression failed for slot %d\n",
            slot
        );

        goto close_file;
    }

    if (decodedSize !=
        (uLongf)record.raw_size)
    {
        DC_WARN(
            "DoomCube: v3 raw size mismatch for slot %d: "
            "%u != %u\n",
            slot,
            (unsigned int)decodedSize,
            (unsigned int)record.raw_size
        );

        goto close_file;
    }

    rawCrc =
        crc32(
            0L,
            Z_NULL,
            0
        );

    rawCrc =
        crc32(
            rawCrc,
            output,
            (uInt)decodedSize
        );

    if ((uint32_t)rawCrc !=
        record.raw_crc32)
    {
        DC_WARN(
            "DoomCube: v3 raw CRC mismatch for slot %d\n",
            slot
        );

        goto close_file;
    }

    success = true;

    DC_INFO(
        "DoomCube: v3 save loaded: "
        "slot=%d raw=%u compressed=%u generation=%u\n",
        slot,
        (unsigned int)record.raw_size,
        (unsigned int)record.compressed_size,
        (unsigned int)record.generation
    );


close_file:

    CARD_Close(
        &file
    );

    free(
        compressedData
    );

    return success;
}


/* ------------------------------------------------------------------------- */
/* Timestamp                                                                 */
/* ------------------------------------------------------------------------- */

uint32_t GC_MemoryCardSaveTimestamp(int slot)
{
    card_file file;

    gc_save_v3_slot_index_t slots[
        GC_SAVE_V3_SLOT_COUNT
    ];

    uint32_t containerSectors;
    uint32_t timestamp = 0;

    if (!cardMounted ||
        !currentLaunchIdentityValid ||
        !validLogicalSlot(slot))
    {
        return 0;
    }

    if (!openProductionV3Container(
            &file,
            &containerSectors))
    {
        return 0;
    }

    if (GC_SaveV3CardBuildSaveIndex(
            &file,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            containerSectors,
            &currentV3Identity,
            slots,
            NULL,
            NULL) &&
        slots[slot].present)
    {
        timestamp =
            slots[slot].record.timestamp;
    }

    CARD_Close(
        &file
    );

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

    DC_DEBUG(
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

    DC_DEBUG(
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

    DC_DEBUG(
        "DoomCube: Memory Card A unmounted\n"
    );
}

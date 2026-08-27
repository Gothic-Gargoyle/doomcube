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

static gc_memcard_status_t memoryCardStatus =
    GC_MEMCARD_STATUS_UNAVAILABLE;

/*
 * CARD_GetBlockCount() returns the card's physical block count.
 * GameCube memory cards reserve physical blocks 0..4 for system
 * metadata, leaving blocks 5 onward available for save files.
 */
#define GC_MEMCARD_SYSTEM_BLOCKS 5u

/*
 * DoomCube's transactional v3 save format requires enough card space for
 * copy-on-write growth and compaction.  Memory Card 59 is therefore not
 * supported.  The minimum supported standard card is Memory Card 251
 * (16 Mbit / 256 physical blocks / 251 usable blocks).
 */
#define GC_MEMCARD_MIN_SIZE_MBIT 16u

/*
 * Hard upper bound for one DoomCube v3 container.
 *
 * Card 251 is the minimum supported card and provides 251 usable blocks.
 * Copy-on-write compaction temporarily requires both the current and
 * replacement containers to coexist.
 *
 * 64 + 64 = 128 blocks fits.
 * 128 + 128 = 256 blocks does not.
 *
 * Therefore 64 blocks is the largest power-of-two container size for which
 * DoomCube can retain its transactional A/B lifecycle on the minimum card.
 */
#define GC_SAVE_V3_MAX_SECTORS 64u

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

        memoryCardStatus =
            GC_MEMCARD_STATUS_UNAVAILABLE;

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
*/

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



static const char *v3FilenameForIndex(
    uint32_t fileIndex)
{
    return
        fileIndex == 0
        ? CARD_V3_FILENAME_A
        : CARD_V3_FILENAME_B;
}


static bool v3GenerationNewer(
    uint32_t a,
    uint32_t b)
{
    uint32_t delta;

    if (a == b)
    {
        return false;
    }

    delta =
        a - b;

    return
        delta < 0x80000000u;
}


static bool inspectProductionV3Candidate(
    uint32_t fileIndex,
    bool *validOut,
    uint32_t *containerSectorsOut,
    uint32_t *generationOut)
{
    card_file file;

    const char *filename;

    uint32_t containerSectors = 0;
    uint32_t generation = 0;

    s32 result;
    s32 closeResult;

    bool valid = false;

    if (!validOut ||
        fileIndex > 1 ||
        !configWorkBuffer ||
        sectorSize <= 0)
    {
        return false;
    }

    *validOut =
        false;

    if (containerSectorsOut)
    {
        *containerSectorsOut =
            0;
    }

    if (generationOut)
    {
        *generationOut =
            0;
    }

    filename =
        v3FilenameForIndex(
            fileIndex
        );

    result =
        CARD_Open(
            CARD_SLOT,
            filename,
            &file
        );

    if (result ==
        CARD_ERROR_NOFILE)
    {
        return true;
    }

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD_Open while inspecting %s failed: %ld\n",
            filename,
            (long)result
        );

        return false;
    }

    valid =
        validateOpenV3Container(
            &file,
            configWorkBuffer,
            fileIndex,
            &containerSectors,
            &generation
        );

    closeResult =
        CARD_Close(
            &file
        );

    if (closeResult !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD_Close while inspecting %s failed: %ld\n",
            filename,
            (long)closeResult
        );

        return false;
    }

    *validOut =
        valid;

    if (valid)
    {
        if (containerSectorsOut)
        {
            *containerSectorsOut =
                containerSectors;
        }

        if (generationOut)
        {
            *generationOut =
                generation;
        }
    }

    return true;
}


static bool selectProductionV3Container(
    card_file *file,
    uint32_t *fileIndexOut,
    uint32_t *containerSectorsOut,
    uint32_t *generationOut)
{
    bool valid[2] = {
        false,
        false
    };

    uint32_t sectors[2] = {
        0,
        0
    };

    uint32_t generation[2] = {
        0,
        0
    };

    uint32_t selected;

    const char *filename;

    s32 result;

    if (!file ||
        !cardMounted ||
        !configWorkBuffer ||
        sectorSize <= 0)
    {
        return false;
    }

    if (!inspectProductionV3Candidate(
            0,
            &valid[0],
            &sectors[0],
            &generation[0]) ||
        !inspectProductionV3Candidate(
            1,
            &valid[1],
            &sectors[1],
            &generation[1]))
    {
        return false;
    }

    if (!valid[0] &&
        !valid[1])
    {
        return false;
    }

    if (valid[0] &&
        !valid[1])
    {
        selected =
            0;
    }
    else if (!valid[0] &&
             valid[1])
    {
        selected =
            1;
    }
    else if (generation[0] ==
             generation[1])
    {
        /*
         * An interrupted migration may leave two valid snapshots
         * carrying the same generation.  Prefer the larger physical
         * image, otherwise keep DOOMCUBE0 deterministic.
         */
        selected =
            sectors[1] >
                sectors[0]
            ? 1
            : 0;
    }
    else
    {
        selected =
            v3GenerationNewer(
                generation[1],
                generation[0]
            )
            ? 1
            : 0;
    }

    filename =
        v3FilenameForIndex(
            selected
        );

    result =
        CARD_Open(
            CARD_SLOT,
            filename,
            file
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: selected v3 container %s could not be opened: %ld\n",
            filename,
            (long)result
        );

        return false;
    }

    if (!validateOpenV3Container(
            file,
            configWorkBuffer,
            selected,
            &sectors[selected],
            &generation[selected]))
    {
        CARD_Close(
            file
        );

        DC_WARN(
            "DoomCube: selected v3 container %s failed revalidation\n",
            filename
        );

        return false;
    }

    if (fileIndexOut)
    {
        *fileIndexOut =
            selected;
    }

    if (containerSectorsOut)
    {
        *containerSectorsOut =
            sectors[selected];
    }

    if (generationOut)
    {
        *generationOut =
            generation[selected];
    }

    return true;
}

static bool queryCardSpaceBlocks(
    uint32_t *physicalBlocksOut,
    uint32_t *usableBlocksOut,
    uint32_t *usedBlocksOut,
    uint32_t *freeBlocksOut)
{
    card_dir dir;

    u16 physicalBlocks16 = 0;

    uint32_t physicalBlocks;
    uint32_t usableBlocks;
    uint32_t usedBlocks = 0;
    uint32_t freeBlocks;

    s32 result;

    if (!cardMounted ||
        sectorSize <= 0)
    {
        return false;
    }

    result =
        CARD_GetBlockCount(
            CARD_SLOT,
            &physicalBlocks16
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD_GetBlockCount failed: %ld\n",
            (long)result
        );

        return false;
    }

    physicalBlocks =
        (uint32_t)physicalBlocks16;

    if (physicalBlocks <=
        GC_MEMCARD_SYSTEM_BLOCKS)
    {
        DC_WARN(
            "DoomCube: invalid memory-card block count: %u\n",
            (unsigned int)physicalBlocks
        );

        return false;
    }

    usableBlocks =
        physicalBlocks -
        GC_MEMCARD_SYSTEM_BLOCKS;

    memset(
        &dir,
        0,
        sizeof(dir)
    );

    result =
        CARD_FindFirst(
            CARD_SLOT,
            &dir,
            true
        );

    while (result ==
           CARD_ERROR_READY)
    {
        uint64_t fileBlocks64;

        fileBlocks64 =
            (
                (uint64_t)dir.filelen +
                (uint64_t)(uint32_t)sectorSize -
                1u
            ) /
            (uint64_t)(uint32_t)sectorSize;

        if (fileBlocks64 >
                0xffffffffULL ||
            usedBlocks >
                0xffffffffu -
                (uint32_t)fileBlocks64)
        {
            DC_WARN(
                "DoomCube: memory-card directory size overflow\n"
            );

            return false;
        }

        usedBlocks +=
            (uint32_t)fileBlocks64;

        result =
            CARD_FindNext(
                &dir
            );
    }

    if (result !=
        CARD_ERROR_NOFILE)
    {
        DC_WARN(
            "DoomCube: memory-card directory scan failed: %ld\n",
            (long)result
        );

        return false;
    }

    freeBlocks =
        usedBlocks >= usableBlocks
        ? 0u
        : usableBlocks - usedBlocks;

    if (physicalBlocksOut)
    {
        *physicalBlocksOut =
            physicalBlocks;
    }

    if (usableBlocksOut)
    {
        *usableBlocksOut =
            usableBlocks;
    }

    if (usedBlocksOut)
    {
        *usedBlocksOut =
            usedBlocks;
    }

    if (freeBlocksOut)
    {
        *freeBlocksOut =
            freeBlocks;
    }

    return true;
}


static bool productionV3CardHasFreeBlocks(
    uint32_t requiredBlocks,
    const char *operation)
{
    uint32_t physicalBlocks = 0;
    uint32_t usableBlocks = 0;
    uint32_t usedBlocks = 0;
    uint32_t freeBlocks = 0;

    const char *operationName =
        operation
        ? operation
        : "v3 allocation";

    if (!queryCardSpaceBlocks(
            &physicalBlocks,
            &usableBlocks,
            &usedBlocks,
            &freeBlocks))
    {
        DC_WARN(
            "DoomCube: could not determine Memory Card A free space "
            "for %s\n",
            operationName
        );

        return false;
    }

    DC_INFO(
        "DoomCube: Memory Card A space: "
        "physical=%u usable=%u used=%u free=%u blocks; "
        "%s requires=%u\n",
        (unsigned int)physicalBlocks,
        (unsigned int)usableBlocks,
        (unsigned int)usedBlocks,
        (unsigned int)freeBlocks,
        operationName,
        (unsigned int)requiredBlocks
    );

    if (freeBlocks <
        requiredBlocks)
    {
        DC_WARN(
            "DoomCube: Memory Card A does not have enough free space "
            "for %s: free=%u required=%u blocks; "
            "existing save data preserved\n",
            operationName,
            (unsigned int)freeBlocks,
            (unsigned int)requiredBlocks
        );

        return false;
    }

    return true;
}



static bool productionV3ContainerFilesExist(
    bool *existsOut)
{
    card_file file;

    s32 result;

    bool exists = false;

    if (!existsOut ||
        !cardMounted)
    {
        return false;
    }

    result =
        CARD_Open(
            CARD_SLOT,
            CARD_V3_FILENAME_A,
            &file
        );

    if (result ==
        CARD_ERROR_READY)
    {
        CARD_Close(
            &file
        );

        exists =
            true;
    }
    else if (result !=
             CARD_ERROR_NOFILE)
    {
        DC_WARN(
            "DoomCube: could not inspect %s: %ld\n",
            CARD_V3_FILENAME_A,
            (long)result
        );

        return false;
    }

    result =
        CARD_Open(
            CARD_SLOT,
            CARD_V3_FILENAME_B,
            &file
        );

    if (result ==
        CARD_ERROR_READY)
    {
        CARD_Close(
            &file
        );

        exists =
            true;
    }
    else if (result !=
             CARD_ERROR_NOFILE)
    {
        DC_WARN(
            "DoomCube: could not inspect %s: %ld\n",
            CARD_V3_FILENAME_B,
            (long)result
        );

        return false;
    }

    *existsOut =
        exists;

    return true;
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
     * A grown installation may legitimately contain only DOOMCUBE1.
     * If both images survived an interrupted cleanup, the selector
     * chooses the newest valid generation.
     */
    {
        card_file selectedFile;

        uint32_t selectedIndex = 0;
        uint32_t selectedSectors = 0;
        uint32_t selectedGeneration = 0;

        if (selectProductionV3Container(
                &selectedFile,
                &selectedIndex,
                &selectedSectors,
                &selectedGeneration))
        {
            closeResult =
                CARD_Close(
                    &selectedFile
                );

            if (closeResult !=
                CARD_ERROR_READY)
            {
                DC_WARN(
                    "DoomCube: CARD_Close for selected %s failed: %ld\n",
                    v3FilenameForIndex(selectedIndex),
                    (long)closeResult
                );

                return false;
            }

            DC_INFO(
                "DoomCube: existing v3 container validated: "
                "%s blocks=%u generation=%u\n",
                v3FilenameForIndex(selectedIndex),
                (unsigned int)selectedSectors,
                (unsigned int)selectedGeneration
            );

            return true;
        }
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
     * The selector found no valid image and DOOMCUBE0 is absent.
     * Before creating a new A image, make sure an existing unusable
     * DOOMCUBE1 is not silently ignored or overwritten indirectly.
     */
    result =
        CARD_Open(
            CARD_SLOT,
            CARD_V3_FILENAME_B,
            &file
        );

    if (result ==
        CARD_ERROR_READY)
    {
        CARD_Close(
            &file
        );

        DC_WARN(
            "DoomCube: existing %s is invalid/unselectable; "
            "preserving it and disabling saves\n",
            CARD_V3_FILENAME_B
        );

        return false;
    }

    if (result !=
        CARD_ERROR_NOFILE)
    {
        DC_WARN(
            "DoomCube: CARD_Open for %s failed: %ld\n",
            CARD_V3_FILENAME_B,
            (long)result
        );

        return false;
    }

    /*
     * No v3 data yet: create only the 16-block initial container.
     * Check the allocation that is actually about to be attempted.
     */
    if (!productionV3CardHasFreeBlocks(
            GC_SAVE_V3_INITIAL_SECTORS,
            "initial v3 save creation"))
    {
        return false;
    }

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
/* Production v3 container growth                                            */
/* ------------------------------------------------------------------------- */

static bool validateCommittedV3Log(
    card_file *file,
    uint32_t containerSectors,
    const gc_save_v3_superblock_t *superblock)
{
    gc_save_v3_record_header_t record;

    uint32_t sector;

    if (!file ||
        !superblock ||
        superblock->log_start_sector !=
            GC_SAVE_V3_DATA_START_SECTOR ||
        superblock->log_end_sector <
            superblock->log_start_sector ||
        superblock->log_end_sector >
            containerSectors)
    {
        return false;
    }

    sector =
        superblock->log_start_sector;

    while (sector <
           superblock->log_end_sector)
    {
        memset(
            &record,
            0,
            sizeof(record)
        );

        if (!GC_SaveV3CardReadRecord(
                file,
                configWorkBuffer,
                (size_t)sectorSize,
                (uint32_t)sectorSize,
                containerSectors,
                superblock->log_end_sector,
                sector,
                &record,
                NULL,
                0,
                NULL))
        {
            DC_WARN(
                "DoomCube: v3 growth validation failed "
                "at record sector %u\n",
                (unsigned int)sector
            );

            return false;
        }

        if (record.record_sectors == 0 ||
            record.record_sectors >
                superblock->log_end_sector -
                sector)
        {
            DC_WARN(
                "DoomCube: v3 growth encountered invalid "
                "record geometry at sector %u\n",
                (unsigned int)sector
            );

            return false;
        }

        sector +=
            record.record_sectors;
    }

    return
        sector ==
        superblock->log_end_sector;
}


static bool growProductionV3Container(
    uint32_t targetSectors)
{
    card_file sourceFile;
    card_file targetFile;

    gc_save_v3_container_header_t header;

    gc_save_v3_superblock_t active;
    gc_save_v3_superblock_t migrated;
    gc_save_v3_superblock_t verified;

    uint32_t sourceIndex = 0;
    uint32_t targetIndex;

    uint32_t sourceSectors = 0;
    uint32_t sourceGeneration = 0;

    uint32_t verifiedSectors = 0;
    uint32_t verifiedGeneration = 0;
    uint32_t verifiedSuperblockSector = 0;

    uint32_t sector;

    uint64_t fileSize64;
    u32 fileSize;

    const char *sourceName;
    const char *targetName;

    s32 result;
    s32 closeResult;

    bool sourceOpen = false;
    bool targetOpen = false;
    bool targetCreated = false;
    bool targetValidated = false;
    bool success = false;

    if (!cardMounted ||
        sectorSize <= 0 ||
        !configWorkBuffer)
    {
        return false;
    }

    if (!selectProductionV3Container(
            &sourceFile,
            &sourceIndex,
            &sourceSectors,
            &sourceGeneration))
    {
        DC_WARN(
            "DoomCube: v3 growth could not select source container\n"
        );

        return false;
    }

    sourceOpen =
        true;

    sourceName =
        v3FilenameForIndex(
            sourceIndex
        );

    targetIndex =
        sourceIndex == 0
        ? 1
        : 0;

    targetName =
        v3FilenameForIndex(
            targetIndex
        );

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            &sourceFile,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            sourceSectors,
            &active,
            NULL))
    {
        DC_WARN(
            "DoomCube: v3 growth could not read source superblock\n"
        );

        goto cleanup;
    }

    if (active.generation !=
            sourceGeneration ||
        active.log_end_sector >
            sourceSectors ||
        targetSectors <=
            sourceSectors ||
        targetSectors <
            active.log_end_sector)
    {
        DC_WARN(
            "DoomCube: v3 growth geometry rejected: "
            "source=%u target=%u log_end=%u generation=%u/%u\n",
            (unsigned int)sourceSectors,
            (unsigned int)targetSectors,
            (unsigned int)active.log_end_sector,
            (unsigned int)active.generation,
            (unsigned int)sourceGeneration
        );

        goto cleanup;
    }

    /*
     * Never overwrite a pre-existing alternate file.  A stale file
     * from an interrupted migration remains recoverable evidence.
     */
    result =
        CARD_Open(
            CARD_SLOT,
            targetName,
            &targetFile
        );

    if (result ==
        CARD_ERROR_READY)
    {
        CARD_Close(
            &targetFile
        );

        DC_WARN(
            "DoomCube: v3 growth refused to overwrite existing %s\n",
            targetName
        );

        goto cleanup;
    }

    if (result !=
        CARD_ERROR_NOFILE)
    {
        DC_WARN(
            "DoomCube: v3 growth CARD_Open %s failed: %ld\n",
            targetName,
            (long)result
        );

        goto cleanup;
    }

    if (targetSectors >
        GC_SAVE_V3_MAX_SECTORS)
    {
        DC_WARN(
            "DoomCube: v3 container growth exceeds configured maximum: "
            "requested=%u maximum=%u blocks\n",
            (unsigned int)targetSectors,
            (unsigned int)GC_SAVE_V3_MAX_SECTORS
        );

        goto cleanup;
    }

    if (!productionV3CardHasFreeBlocks(
            targetSectors,
            "v3 container growth"))
    {
        goto cleanup;
    }

    fileSize64 =
        (uint64_t)(uint32_t)sectorSize *
        (uint64_t)targetSectors;

    if (fileSize64 >
        0xffffffffULL)
    {
        DC_WARN(
            "DoomCube: v3 growth target size overflow\n"
        );

        goto cleanup;
    }

    fileSize =
        (u32)fileSize64;

    DC_INFO(
        "DoomCube: v3 growth starting: "
        "%s %u blocks -> %s %u blocks "
        "generation=%u log_end=%u\n",
        sourceName,
        (unsigned int)sourceSectors,
        targetName,
        (unsigned int)targetSectors,
        (unsigned int)active.generation,
        (unsigned int)active.log_end_sector
    );

    result =
        CARD_Create(
            CARD_SLOT,
            targetName,
            fileSize,
            &targetFile
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: v3 growth CARD_Create %s failed: %ld\n",
            targetName,
            (long)result
        );

        goto cleanup;
    }

    targetOpen =
        true;

    targetCreated =
        true;

    /*
     * Copy sector 0 first, then rewrite only the encoded container
     * header.  Bytes outside the header are preserved for future
     * banner/icon/comment payloads.
     */
    result =
        CARD_Read(
            &sourceFile,
            configWorkBuffer,
            (u32)sectorSize,
            0
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: v3 growth metadata read failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    memset(
        &header,
        0,
        sizeof(header)
    );

    if (!GC_SaveV3DecodeContainerHeader(
            &header,
            configWorkBuffer,
            (size_t)sectorSize))
    {
        DC_WARN(
            "DoomCube: v3 growth source metadata decode failed\n"
        );

        goto cleanup;
    }

    header.container_sectors =
        targetSectors;

    header.file_index =
        targetIndex;

    if (!GC_SaveV3EncodeContainerHeader(
            configWorkBuffer,
            (size_t)sectorSize,
            &header))
    {
        DC_WARN(
            "DoomCube: v3 growth target metadata encode failed\n"
        );

        goto cleanup;
    }

    result =
        CARD_Write(
            &targetFile,
            configWorkBuffer,
            (u32)sectorSize,
            0
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: v3 growth metadata write failed: %ld\n",
            (long)result
        );

        goto cleanup;
    }

    /*
     * Copy every committed record sector exactly as stored.
     * Uncommitted garbage beyond log_end is deliberately discarded.
     */
    for (sector =
            active.log_start_sector;
         sector <
            active.log_end_sector;
         ++sector)
    {
        u32 offset =
            (u32)(
                sector *
                (uint32_t)sectorSize
            );

        result =
            CARD_Read(
                &sourceFile,
                configWorkBuffer,
                (u32)sectorSize,
                offset
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: v3 growth source read failed "
                "at sector %u: %ld\n",
                (unsigned int)sector,
                (long)result
            );

            goto cleanup;
        }

        result =
            CARD_Write(
                &targetFile,
                configWorkBuffer,
                (u32)sectorSize,
                offset
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: v3 growth target write failed "
                "at sector %u: %ld\n",
                (unsigned int)sector,
                (long)result
            );

            goto cleanup;
        }
    }

    /*
     * Migration itself is a committed state transition, so advance the
     * container generation even though no logical Doom record changed.
     * This makes the new image unambiguously newer if deletion of the
     * old image is interrupted.
     */
    migrated =
        active;

    migrated.generation =
        active.generation +
        1u;

    migrated.container_sectors =
        targetSectors;

    /*
     * Both superblocks describe the same complete migrated snapshot.
     * The normal append path will alternate them again on the next save.
     */
    if (!GC_SaveV3CardWriteSuperblock(
            &targetFile,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_SUPERBLOCK_A_SECTOR,
            &migrated) ||
        !GC_SaveV3CardWriteSuperblock(
            &targetFile,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_SUPERBLOCK_B_SECTOR,
            &migrated))
    {
        DC_WARN(
            "DoomCube: v3 growth superblock write failed\n"
        );

        goto cleanup;
    }

    closeResult =
        CARD_Close(
            &targetFile
        );

    targetOpen =
        false;

    if (closeResult !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: v3 growth target CARD_Close failed: %ld\n",
            (long)closeResult
        );

        goto cleanup;
    }

    /*
     * Reopen and validate exactly as a later boot will.
     */
    result =
        CARD_Open(
            CARD_SLOT,
            targetName,
            &targetFile
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: grown container %s could not be reopened: %ld\n",
            targetName,
            (long)result
        );

        goto cleanup;
    }

    targetOpen =
        true;

    if (!validateOpenV3Container(
            &targetFile,
            configWorkBuffer,
            targetIndex,
            &verifiedSectors,
            &verifiedGeneration))
    {
        DC_WARN(
            "DoomCube: grown container %s failed header/superblock validation\n",
            targetName
        );

        goto cleanup;
    }

    if (verifiedSectors !=
            targetSectors ||
        verifiedGeneration !=
            migrated.generation)
    {
        DC_WARN(
            "DoomCube: grown container validation mismatch: "
            "blocks=%u/%u generation=%u/%u\n",
            (unsigned int)verifiedSectors,
            (unsigned int)targetSectors,
            (unsigned int)verifiedGeneration,
            (unsigned int)migrated.generation
        );

        goto cleanup;
    }

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            &targetFile,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            targetSectors,
            &verified,
            &verifiedSuperblockSector))
    {
        DC_WARN(
            "DoomCube: grown container authoritative superblock read failed\n"
        );

        goto cleanup;
    }

    if (verified.generation !=
            migrated.generation ||
        verified.log_end_sector !=
            active.log_end_sector ||
        verified.container_sectors !=
            targetSectors)
    {
        DC_WARN(
            "DoomCube: grown container authoritative state mismatch\n"
        );

        goto cleanup;
    }

    if (!validateCommittedV3Log(
            &targetFile,
            targetSectors,
            &verified))
    {
        DC_WARN(
            "DoomCube: grown container committed-log validation failed\n"
        );

        goto cleanup;
    }

    closeResult =
        CARD_Close(
            &targetFile
        );

    targetOpen =
        false;

    if (closeResult !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: grown container final CARD_Close failed: %ld\n",
            (long)closeResult
        );

        goto cleanup;
    }

    /*
     * From here onward the alternate image is independently bootable.
     * Never remove it during cleanup.
     */
    targetValidated =
        true;

    closeResult =
        CARD_Close(
            &sourceFile
        );

    sourceOpen =
        false;

    if (closeResult !=
        CARD_ERROR_READY)
    {
        /*
         * Keep both images.  The new generation is higher, so normal
         * selection will use it on the next open.
         */
        DC_WARN(
            "DoomCube: old v3 container close failed after growth: %ld; "
            "keeping both valid images\n",
            (long)closeResult
        );

        success =
            true;

        goto cleanup;
    }

    result =
        CARD_Delete(
            CARD_SLOT,
            sourceName
        );

    if (result !=
        CARD_ERROR_READY)
    {
        /*
         * This is safe: both images are valid and the migrated image has
         * the newer generation.  Do not destroy the new copy merely
         * because cleanup of the old one failed.
         */
        DC_WARN(
            "DoomCube: old v3 container %s could not be deleted: %ld; "
            "newer %s remains authoritative\n",
            sourceName,
            (long)result,
            targetName
        );
    }
    else
    {
        DC_INFO(
            "DoomCube: old v3 container removed after validated growth: %s\n",
            sourceName
        );
    }

    success =
        true;

    DC_INFO(
        "DoomCube: v3 growth committed: "
        "%s blocks=%u -> %s blocks=%u "
        "generation=%u log_end=%u\n",
        sourceName,
        (unsigned int)sourceSectors,
        targetName,
        (unsigned int)targetSectors,
        (unsigned int)migrated.generation,
        (unsigned int)migrated.log_end_sector
    );


cleanup:

    if (targetOpen)
    {
        CARD_Close(
            &targetFile
        );

        targetOpen =
            false;
    }

    if (sourceOpen)
    {
        CARD_Close(
            &sourceFile
        );

        sourceOpen =
            false;
    }

    if (!success &&
        targetCreated &&
        !targetValidated)
    {
        /*
         * Only remove the alternate image created by this failed
         * invocation.  The previously authoritative source is untouched.
         */
        result =
            CARD_Delete(
                CARD_SLOT,
                targetName
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: failed to clean incomplete grown %s: %ld\n",
                targetName,
                (long)result
            );
        }
    }

    return success;
}



/* ------------------------------------------------------------------------- */
/* Production v3 compaction                                                  */
/* ------------------------------------------------------------------------- */

static bool v3CompactionRecordTypeValid(
    const gc_save_v3_record_header_t *record)
{
    if (!record)
    {
        return false;
    }

    if (record->record_type ==
        GC_SAVE_V3_RECORD_SAVE)
    {
        return
            record->slot <
            GC_SAVE_V3_SLOT_COUNT;
    }

    if (record->record_type ==
        GC_SAVE_V3_RECORD_CONFIG)
    {
        return
            record->slot == 0;
    }

    return false;
}


static bool v3CompactionSameLogicalKey(
    const gc_save_v3_record_header_t *a,
    const gc_save_v3_record_header_t *b)
{
    if (!a ||
        !b ||
        a->record_type !=
            b->record_type)
    {
        return false;
    }

    if (a->record_type ==
        GC_SAVE_V3_RECORD_CONFIG)
    {
        /*
         * Configuration is global rather than tied to an IWAD/PWAD.
         */
        return true;
    }

    if (a->record_type ==
        GC_SAVE_V3_RECORD_SAVE)
    {
        return
            a->slot ==
                b->slot &&
            GC_SaveV3LaunchIdentityEqual(
                &a->identity,
                &b->identity
            );
    }

    return false;
}


/*
 * Decide whether candidate_sector is the authoritative record for its
 * logical key.
 *
 * This deliberately scans the complete committed log rather than only
 * looking forward.  Compaction is rare and current containers are small;
 * the O(n^2) scan avoids arbitrary identity-count limits while retaining
 * correct wrap-aware generation semantics.
 */
typedef struct
{
    uint32_t record_sector;

    gc_save_v3_record_header_t record;

    bool keep;

} v3_compaction_entry_t;


/*
 * Read the committed log exactly once from CARD and cache its headers.
 *
 * Newest-record selection is then performed entirely in RAM.
 */
static bool buildV3CompactionSnapshot(
    card_file *file,
    uint32_t containerSectors,
    const gc_save_v3_superblock_t *active,
    v3_compaction_entry_t **entriesOut,
    size_t *entryCountOut,
    uint32_t *compactedEndOut,
    uint32_t *liveRecordCountOut)
{
    v3_compaction_entry_t *entries = NULL;

    gc_save_v3_record_header_t record;

    uint32_t cursor;
    uint32_t maxEntries;
    uint32_t compactedEnd;
    uint32_t liveRecords;

    size_t entryCount;
    size_t i;

    bool candidateKeep;

    if (!file ||
        !active ||
        !entriesOut ||
        !entryCountOut ||
        !compactedEndOut ||
        active->log_start_sector !=
            GC_SAVE_V3_DATA_START_SECTOR ||
        active->log_end_sector <
            active->log_start_sector ||
        active->log_end_sector >
            containerSectors)
    {
        return false;
    }

    *entriesOut =
        NULL;

    *entryCountOut =
        0;

    *compactedEndOut =
        GC_SAVE_V3_DATA_START_SECTOR;

    if (liveRecordCountOut)
    {
        *liveRecordCountOut =
            0;
    }

    maxEntries =
        active->log_end_sector -
        active->log_start_sector;

    if (maxEntries == 0)
    {
        return true;
    }

    if ((size_t)maxEntries >
        ((size_t)-1) /
            sizeof(*entries))
    {
        return false;
    }

    entries =
        calloc(
            (size_t)maxEntries,
            sizeof(*entries)
        );

    if (!entries)
    {
        DC_WARN(
            "DoomCube: v3 compaction snapshot allocation "
            "failed for %u entries\n",
            (unsigned int)maxEntries
        );

        return false;
    }

    cursor =
        active->log_start_sector;

    entryCount =
        0;

    /*
     * The only CARD-reading pass used to determine the live set.
     */
    while (cursor <
           active->log_end_sector)
    {
        memset(
            &record,
            0,
            sizeof(record)
        );

        if (!GC_SaveV3CardReadRecord(
                file,
                configWorkBuffer,
                (size_t)sectorSize,
                (uint32_t)sectorSize,
                containerSectors,
                active->log_end_sector,
                cursor,
                &record,
                NULL,
                0,
                NULL))
        {
            DC_WARN(
                "DoomCube: v3 compaction snapshot read "
                "failed at sector %u\n",
                (unsigned int)cursor
            );

            goto failure;
        }

        if (!v3CompactionRecordTypeValid(
                &record) ||
            record.record_sectors == 0 ||
            record.record_sectors >
                active->log_end_sector -
                cursor)
        {
            DC_WARN(
                "DoomCube: v3 compaction snapshot found "
                "invalid record at sector %u\n",
                (unsigned int)cursor
            );

            goto failure;
        }

        if (entryCount >=
            (size_t)maxEntries)
        {
            goto failure;
        }

        candidateKeep =
            true;

        /*
         * Compare logical keys against cached headers only.
         * No CARD I/O occurs inside this nested loop.
         */
        for (i = 0;
             i < entryCount;
             ++i)
        {
            bool candidateNewer;
            bool previousNewer;

            if (!v3CompactionSameLogicalKey(
                    &record,
                    &entries[i].record))
            {
                continue;
            }

            if (record.generation ==
                entries[i].record.generation)
            {
                DC_WARN(
                    "DoomCube: v3 compaction found ambiguous "
                    "generation %u at sectors %u and %u\n",
                    (unsigned int)record.generation,
                    (unsigned int)cursor,
                    (unsigned int)entries[i].record_sector
                );

                goto failure;
            }

            candidateNewer =
                v3GenerationNewer(
                    record.generation,
                    entries[i].record.generation
                );

            previousNewer =
                v3GenerationNewer(
                    entries[i].record.generation,
                    record.generation
                );

            /*
             * Equal was handled above.  If neither direction wins,
             * generation ordering is ambiguous at the half-wrap point.
             */
            if (!candidateNewer &&
                !previousNewer)
            {
                DC_WARN(
                    "DoomCube: v3 compaction generation ordering "
                    "ambiguous: %u vs %u\n",
                    (unsigned int)record.generation,
                    (unsigned int)entries[i].record.generation
                );

                goto failure;
            }

            if (candidateNewer)
            {
                entries[i].keep =
                    false;
            }
            else
            {
                candidateKeep =
                    false;
            }
        }

        entries[entryCount].record_sector =
            cursor;

        entries[entryCount].record =
            record;

        entries[entryCount].keep =
            candidateKeep;

        ++entryCount;

        cursor +=
            record.record_sectors;
    }

    if (cursor !=
        active->log_end_sector)
    {
        goto failure;
    }

    compactedEnd =
        GC_SAVE_V3_DATA_START_SECTOR;

    liveRecords =
        0;

    for (i = 0;
         i < entryCount;
         ++i)
    {
        if (!entries[i].keep)
        {
            continue;
        }

        if (compactedEnd >
            UINT32_MAX -
                entries[i].record.record_sectors)
        {
            goto failure;
        }

        compactedEnd +=
            entries[i].record.record_sectors;

        ++liveRecords;
    }

    *entriesOut =
        entries;

    *entryCountOut =
        entryCount;

    *compactedEndOut =
        compactedEnd;

    if (liveRecordCountOut)
    {
        *liveRecordCountOut =
            liveRecords;
    }

    return true;


failure:

    free(
        entries
    );

    return false;
}


static bool copyV3RecordSectors(
    card_file *sourceFile,
    card_file *targetFile,
    uint32_t sourceSector,
    uint32_t targetSector,
    uint32_t recordSectors)
{
    uint32_t i;

    if (!sourceFile ||
        !targetFile ||
        recordSectors == 0)
    {
        return false;
    }

    for (i = 0;
         i < recordSectors;
         ++i)
    {
        uint64_t sourceOffset64;
        uint64_t targetOffset64;

        s32 result;

        sourceOffset64 =
            (uint64_t)(
                sourceSector + i
            ) *
            (uint64_t)(uint32_t)sectorSize;

        targetOffset64 =
            (uint64_t)(
                targetSector + i
            ) *
            (uint64_t)(uint32_t)sectorSize;

        if (sourceOffset64 >
                0xffffffffULL ||
            targetOffset64 >
                0xffffffffULL)
        {
            return false;
        }

        result =
            CARD_Read(
                sourceFile,
                configWorkBuffer,
                (u32)sectorSize,
                (u32)sourceOffset64
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: v3 compaction source read "
                "failed at sector %u: %ld\n",
                (unsigned int)(
                    sourceSector + i
                ),
                (long)result
            );

            return false;
        }

        result =
            CARD_Write(
                targetFile,
                configWorkBuffer,
                (u32)sectorSize,
                (u32)targetOffset64
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: v3 compaction target write "
                "failed at sector %u: %ld\n",
                (unsigned int)(
                    targetSector + i
                ),
                (long)result
            );

            return false;
        }
    }

    return true;
}


/*
 * Rewrite the authoritative container into its same-sized alternate file,
 * retaining exactly one newest record for every:
 *
 *     SAVE   -> IWAD/PWAD identity + slot
 *     CONFIG -> global configuration
 *
 * The source remains authoritative until the complete target has been
 * written, reopened and validated.
 *
 * Return value:
 *
 *     false -> error; source remains authoritative
 *     true  -> operation was safely assessed/completed
 *
 * compactedOut tells the caller whether a rewrite actually happened.
 */
static bool compactProductionV3Container(
    bool *compactedOut)
{
    card_file sourceFile;
    card_file targetFile;

    gc_save_v3_container_header_t header;

    gc_save_v3_superblock_t active;
    gc_save_v3_superblock_t compacted;
    gc_save_v3_superblock_t verified;

    v3_compaction_entry_t *entries = NULL;
    v3_compaction_entry_t *verifyEntries = NULL;

    size_t entryCount = 0;
    size_t verifyEntryCount = 0;
    size_t entryIndex;

    uint32_t sourceIndex = 0;
    uint32_t targetIndex;

    uint32_t sourceSectors = 0;
    uint32_t sourceGeneration = 0;

    uint32_t targetCandidateSectors = 0;
    uint32_t targetCandidateGeneration = 0;

    uint32_t measuredEnd = 0;
    uint32_t measuredRecords = 0;

    uint32_t verifiedSectors = 0;
    uint32_t verifiedGeneration = 0;

    uint32_t targetCursor;

    uint32_t verifyEnd = 0;
    uint32_t verifyRecords = 0;

    uint64_t fileSize64;

    const char *sourceName;
    const char *targetName;

    s32 result;
    s32 closeResult;

    bool sourceOpen = false;
    bool targetOpen = false;
    bool targetCreated = false;
    bool targetValidated = false;
    bool success = false;

    if (compactedOut)
    {
        *compactedOut =
            false;
    }

    if (!compactedOut ||
        !cardMounted ||
        sectorSize <= 0 ||
        !configWorkBuffer)
    {
        return false;
    }

    if (!selectProductionV3Container(
            &sourceFile,
            &sourceIndex,
            &sourceSectors,
            &sourceGeneration))
    {
        DC_WARN(
            "DoomCube: v3 compaction could not select source container\n"
        );

        return false;
    }

    sourceOpen =
        true;

    sourceName =
        v3FilenameForIndex(
            sourceIndex
        );

    targetIndex =
        sourceIndex == 0
        ? 1
        : 0;

    targetName =
        v3FilenameForIndex(
            targetIndex
        );

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            &sourceFile,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            sourceSectors,
            &active,
            NULL))
    {
        DC_WARN(
            "DoomCube: v3 compaction could not read source state\n"
        );

        goto cleanup;
    }

    if (active.generation !=
            sourceGeneration ||
        active.log_start_sector !=
            GC_SAVE_V3_DATA_START_SECTOR ||
        active.log_end_sector >
            sourceSectors)
    {
        DC_WARN(
            "DoomCube: v3 compaction source geometry rejected\n"
        );

        goto cleanup;
    }

    /*
     * Check the alternate before performing the comparatively expensive
     * live-record scan.
     *
     * A valid alternate with an older generation is a stale image left
     * behind by an earlier migration/cleanup and is safe to remove:
     * the selected source is already the newer authoritative image.
     *
     * Equal/newer or invalid alternates are preserved rather than
     * destroyed automatically.
     */
    result =
        CARD_Open(
            CARD_SLOT,
            targetName,
            &targetFile
        );

    if (result ==
        CARD_ERROR_READY)
    {
        bool targetValid;

        targetValid =
            validateOpenV3Container(
                &targetFile,
                configWorkBuffer,
                targetIndex,
                &targetCandidateSectors,
                &targetCandidateGeneration
            );

        closeResult =
            CARD_Close(
                &targetFile
            );

        if (closeResult !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: v3 compaction alternate %s "
                "CARD_Close failed: %ld\n",
                targetName,
                (long)closeResult
            );

            goto cleanup;
        }

        if (!targetValid)
        {
            DC_WARN(
                "DoomCube: v3 compaction found invalid/unusable "
                "alternate %s; preserving it\n",
                targetName
            );

            goto cleanup;
        }

        if (!v3GenerationNewer(
                sourceGeneration,
                targetCandidateGeneration))
        {
            DC_WARN(
                "DoomCube: v3 compaction alternate %s is not "
                "strictly older: source_generation=%u "
                "alternate_generation=%u; preserving it\n",
                targetName,
                (unsigned int)sourceGeneration,
                (unsigned int)targetCandidateGeneration
            );

            goto cleanup;
        }

        DC_INFO(
            "DoomCube: v3 compaction removing stale alternate: "
            "%s blocks=%u generation=%u "
            "(authoritative %s generation=%u)\n",
            targetName,
            (unsigned int)targetCandidateSectors,
            (unsigned int)targetCandidateGeneration,
            sourceName,
            (unsigned int)sourceGeneration
        );

        result =
            CARD_Delete(
                CARD_SLOT,
                targetName
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: v3 compaction could not remove "
                "stale alternate %s: %ld\n",
                targetName,
                (long)result
            );

            goto cleanup;
        }
    }
    else if (result !=
             CARD_ERROR_NOFILE)
    {
        DC_WARN(
            "DoomCube: v3 compaction alternate preflight "
            "CARD_Open %s failed: %ld\n",
            targetName,
            (long)result
        );

        goto cleanup;
    }


    if (!buildV3CompactionSnapshot(
            &sourceFile,
            sourceSectors,
            &active,
            &entries,
            &entryCount,
            &measuredEnd,
            &measuredRecords))
    {
        DC_WARN(
            "DoomCube: v3 compaction live-record snapshot failed\n"
        );

        goto cleanup;
    }

    DC_INFO(
        "DoomCube: v3 compaction snapshot: "
        "records=%u live=%u log_end=%u compacted_end=%u\n",
        (unsigned int)entryCount,
        (unsigned int)measuredRecords,
        (unsigned int)active.log_end_sector,
        (unsigned int)measuredEnd
    );

    if (measuredEnd >
        sourceSectors)
    {
        DC_WARN(
            "DoomCube: v3 compaction measurement exceeds container: "
            "%u > %u\n",
            (unsigned int)measuredEnd,
            (unsigned int)sourceSectors
        );

        goto cleanup;
    }

    if (measuredEnd >=
        active.log_end_sector)
    {
        DC_INFO(
            "DoomCube: v3 compaction found nothing reclaimable: "
            "%s blocks=%u log_end=%u live_records=%u\n",
            sourceName,
            (unsigned int)sourceSectors,
            (unsigned int)active.log_end_sector,
            (unsigned int)measuredRecords
        );

        closeResult =
            CARD_Close(
                &sourceFile
            );

        sourceOpen =
            false;

        if (closeResult !=
            CARD_ERROR_READY)
        {
            return false;
        }

        return true;
    }

    /* Alternate filename was resolved by early preflight. */

    if (!productionV3CardHasFreeBlocks(
            sourceSectors,
            "v3 container compaction"))
    {
        goto cleanup;
    }

    fileSize64 =
        (uint64_t)(uint32_t)sectorSize *
        (uint64_t)sourceSectors;

    if (fileSize64 >
        0xffffffffULL)
    {
        DC_WARN(
            "DoomCube: v3 compaction size overflow\n"
        );

        goto cleanup;
    }

    DC_INFO(
        "DoomCube: v3 compaction starting: "
        "%s blocks=%u generation=%u log_end=%u "
        "-> %s live_records=%u compacted_end=%u\n",
        sourceName,
        (unsigned int)sourceSectors,
        (unsigned int)active.generation,
        (unsigned int)active.log_end_sector,
        targetName,
        (unsigned int)measuredRecords,
        (unsigned int)measuredEnd
    );

    result =
        CARD_Create(
            CARD_SLOT,
            targetName,
            (u32)fileSize64,
            &targetFile
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: v3 compaction CARD_Create %s failed: %ld\n",
            targetName,
            (long)result
        );

        goto cleanup;
    }

    targetOpen =
        true;

    targetCreated =
        true;

    /*
     * Preserve all future GameCube metadata in sector 0; rewrite only
     * the v3 header's physical file index.
     */
    result =
        CARD_Read(
            &sourceFile,
            configWorkBuffer,
            (u32)sectorSize,
            0
        );

    if (result !=
        CARD_ERROR_READY)
    {
        goto cleanup;
    }

    memset(
        &header,
        0,
        sizeof(header)
    );

    if (!GC_SaveV3DecodeContainerHeader(
            &header,
            configWorkBuffer,
            (size_t)sectorSize))
    {
        goto cleanup;
    }

    header.container_sectors =
        sourceSectors;

    header.file_index =
        targetIndex;

    if (!GC_SaveV3EncodeContainerHeader(
            configWorkBuffer,
            (size_t)sectorSize,
            &header))
    {
        goto cleanup;
    }

    result =
        CARD_Write(
            &targetFile,
            configWorkBuffer,
            (u32)sectorSize,
            0
        );

    if (result !=
        CARD_ERROR_READY)
    {
        goto cleanup;
    }

    /*
     * Pack only authoritative logical records from sector 3 upward.
     *
     * Keep/discard decisions came from the source snapshot above.
     * The only CARD reads here are the sectors actually retained.
     */
    targetCursor =
        GC_SAVE_V3_DATA_START_SECTOR;

    for (entryIndex = 0;
         entryIndex < entryCount;
         ++entryIndex)
    {
        if (!entries[entryIndex].keep)
        {
            continue;
        }

        if (targetCursor >
                sourceSectors ||
            entries[entryIndex].record.record_sectors >
                sourceSectors -
                targetCursor)
        {
            goto cleanup;
        }

        if (!copyV3RecordSectors(
                &sourceFile,
                &targetFile,
                entries[entryIndex].record_sector,
                targetCursor,
                entries[entryIndex].record.record_sectors))
        {
            goto cleanup;
        }

        targetCursor +=
            entries[entryIndex].record.record_sectors;
    }

    if (targetCursor !=
        measuredEnd)
    {
        DC_WARN(
            "DoomCube: v3 compaction packed geometry mismatch\n"
        );

        goto cleanup;
    }


    /*
     * Compaction itself is a committed container transition.  Records
     * retain their original generations; the container generation moves
     * forward so this alternate wins selection after an interrupted old
     * file cleanup.
     */
    compacted =
        active;

    compacted.generation =
        active.generation +
        1u;

    compacted.container_sectors =
        sourceSectors;

    compacted.log_start_sector =
        GC_SAVE_V3_DATA_START_SECTOR;

    compacted.log_end_sector =
        targetCursor;

    if (!GC_SaveV3CardWriteSuperblock(
            &targetFile,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_SUPERBLOCK_A_SECTOR,
            &compacted) ||
        !GC_SaveV3CardWriteSuperblock(
            &targetFile,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            GC_SAVE_V3_SUPERBLOCK_B_SECTOR,
            &compacted))
    {
        DC_WARN(
            "DoomCube: v3 compaction superblock write failed\n"
        );

        goto cleanup;
    }

    closeResult =
        CARD_Close(
            &targetFile
        );

    targetOpen =
        false;

    if (closeResult !=
        CARD_ERROR_READY)
    {
        goto cleanup;
    }

    /*
     * Reopen exactly as a future boot will and validate every retained
     * record before making the compacted image authoritative.
     */
    result =
        CARD_Open(
            CARD_SLOT,
            targetName,
            &targetFile
        );

    if (result !=
        CARD_ERROR_READY)
    {
        goto cleanup;
    }

    targetOpen =
        true;

    if (!validateOpenV3Container(
            &targetFile,
            configWorkBuffer,
            targetIndex,
            &verifiedSectors,
            &verifiedGeneration))
    {
        goto cleanup;
    }

    if (verifiedSectors !=
            sourceSectors ||
        verifiedGeneration !=
            compacted.generation)
    {
        goto cleanup;
    }

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            &targetFile,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            sourceSectors,
            &verified,
            NULL))
    {
        goto cleanup;
    }

    if (verified.log_end_sector !=
            measuredEnd ||
        verified.generation !=
            compacted.generation)
    {
        goto cleanup;
    }

    /*
     * One sequential target snapshot validates every retained record's
     * header, geometry and compressed CRC, while also proving that no
     * stale logical duplicates survived compaction.
     */
    if (!buildV3CompactionSnapshot(
            &targetFile,
            sourceSectors,
            &verified,
            &verifyEntries,
            &verifyEntryCount,
            &verifyEnd,
            &verifyRecords) ||
        verifyEnd !=
            verified.log_end_sector ||
        verifyRecords !=
            measuredRecords ||
        verifyEntryCount !=
            (size_t)measuredRecords)
    {
        DC_WARN(
            "DoomCube: compacted target verification failed\n"
        );

        goto cleanup;
    }

    for (entryIndex = 0;
         entryIndex < verifyEntryCount;
         ++entryIndex)
    {
        if (!verifyEntries[entryIndex].keep)
        {
            DC_WARN(
                "DoomCube: compacted target contains "
                "stale logical records\n"
            );

            goto cleanup;
        }
    }

    closeResult =
        CARD_Close(
            &targetFile
        );

    targetOpen =
        false;

    if (closeResult !=
        CARD_ERROR_READY)
    {
        goto cleanup;
    }

    targetValidated =
        true;

    closeResult =
        CARD_Close(
            &sourceFile
        );

    sourceOpen =
        false;

    if (closeResult !=
        CARD_ERROR_READY)
    {
        /*
         * Both images are now valid.  The compacted image has the newer
         * container generation, so selection remains deterministic.
         */
        success =
            true;

        *compactedOut =
            true;

        goto cleanup;
    }

    result =
        CARD_Delete(
            CARD_SLOT,
            sourceName
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: old v3 container %s could not be deleted "
            "after compaction: %ld; newer %s remains authoritative\n",
            sourceName,
            (long)result,
            targetName
        );
    }
    else
    {
        DC_INFO(
            "DoomCube: old v3 container removed after "
            "validated compaction: %s\n",
            sourceName
        );
    }

    success =
        true;

    *compactedOut =
        true;

    DC_INFO(
        "DoomCube: v3 compaction committed: "
        "%s -> %s blocks=%u generation=%u "
        "log_end=%u->%u reclaimed=%u sectors live_records=%u\n",
        sourceName,
        targetName,
        (unsigned int)sourceSectors,
        (unsigned int)compacted.generation,
        (unsigned int)active.log_end_sector,
        (unsigned int)compacted.log_end_sector,
        (unsigned int)(
            active.log_end_sector -
            compacted.log_end_sector
        ),
        (unsigned int)measuredRecords
    );


cleanup:

    free(
        verifyEntries
    );

    verifyEntries =
        NULL;

    free(
        entries
    );

    entries =
        NULL;

    if (targetOpen)
    {
        CARD_Close(
            &targetFile
        );

        targetOpen =
            false;
    }

    if (sourceOpen)
    {
        CARD_Close(
            &sourceFile
        );

        sourceOpen =
            false;
    }

    if (!success &&
        targetCreated &&
        !targetValidated)
    {
        result =
            CARD_Delete(
                CARD_SLOT,
                targetName
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: failed to clean incomplete compacted %s: %ld\n",
                targetName,
                (long)result
            );
        }
    }

    return success;
}


/* ------------------------------------------------------------------------- */
/* Memory card initialization                                                */
/* ------------------------------------------------------------------------- */


gc_memcard_status_t GC_MemoryCardGetStatus(void)
{
    return memoryCardStatus;
}


uint32_t GC_MemoryCardSaveFileInitialBlocks(void)
{
    return GC_SAVE_V3_INITIAL_SECTORS;
}


uint32_t GC_MemoryCardSaveFileMaxBlocks(void)
{
    return GC_SAVE_V3_MAX_SECTORS;
}


bool GC_MemoryCardCreateSaveFile(void)
{
    if (!cardMounted ||
        memoryCardStatus !=
            GC_MEMCARD_STATUS_NEEDS_CREATE)
    {
        return false;
    }

    DC_INFO(
        "DoomCube: player approved creation of DoomCube save file\n"
    );

    if (!ensureProductionV3Container())
    {
        DC_WARN(
            "DoomCube: player-requested DoomCube save-file creation failed\n"
        );

        return false;
    }

    memoryCardStatus =
        GC_MEMCARD_STATUS_READY;

    DC_INFO(
        "DoomCube: player-requested DoomCube save file ready\n"
    );

    return true;
}


bool GC_MemoryCardInit(void)
{
    /*
     * Assume unavailable until the complete initialization path succeeds.
     * Specific failure reasons may replace this below.
     */
    memoryCardStatus =
        GC_MEMCARD_STATUS_UNAVAILABLE;


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
     * Reject undersized cards before mounting or writing anything.
     *
     * CARD_ProbeEx reports Memory Card 59 as 4 Mbit and Memory Card 251
     * as 16 Mbit.  v3 deliberately does not try to contort its safe
     * copy-on-write lifecycle to fit a Card 59.
     */
    if ((uint32_t)memorySize <
        GC_MEMCARD_MIN_SIZE_MBIT)
    {
        memoryCardStatus =
            GC_MEMCARD_STATUS_TOO_SMALL;

        DC_WARN(
            "DoomCube: Memory Card A is too small: %ld Mbit; "
            "Memory Card 251 or larger (16 Mbit minimum) required; "
            "saving disabled\n",
            (long)memorySize
        );

        return false;
    }

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

    {
        bool productionContainerExists = false;

        if (!productionV3ContainerFilesExist(
                &productionContainerExists))
        {
            DC_WARN(
                "DoomCube: could not determine whether a v3 "
                "save file exists\n"
            );

            return false;
        }

        if (!productionContainerExists)
        {
            memoryCardStatus =
                GC_MEMCARD_STATUS_NEEDS_CREATE;

            DC_INFO(
                "DoomCube: no DoomCube v3 save file found; "
                "awaiting player choice\n"
            );
        }
        else if (!ensureProductionV3Container())
        {
            DC_WARN(
                "DoomCube: v3 save container unavailable; "
                "continuing without saves\n"
            );

            return false;
        }
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

    if (memoryCardStatus !=
        GC_MEMCARD_STATUS_NEEDS_CREATE)
    {
        memoryCardStatus =
            GC_MEMCARD_STATUS_READY;
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
    uint32_t fileIndex = 0;
    uint32_t sectors = 0;

    if (!file ||
        !cardMounted ||
        sectorSize <= 0 ||
        !configWorkBuffer)
    {
        return false;
    }

    if (!selectProductionV3Container(
            file,
            &fileIndex,
            &sectors,
            NULL))
    {
        DC_WARN(
            "DoomCube: no valid live v3 production container\n"
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

    gc_save_v3_superblock_t activeSuperblock;
    gc_save_v3_superblock_t committedSuperblock;

    unsigned char *compressedData = NULL;

    uLongf compressedCapacity;
    uLongf compressedSize;

    uLong rawCrc;

    uint32_t containerSectors;
    uint32_t committedSuperblockSector;
    uint32_t activeSuperblockSector;
    uint32_t recordSector;

    uint32_t recordSectorsNeeded;
    uint32_t requiredEnd;
    uint32_t targetSectors;

    bool success = false;
    bool compacted = false;

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

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            &file,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            containerSectors,
            &activeSuperblock,
            &activeSuperblockSector))
    {
        CARD_Close(
            &file
        );

        DC_WARN(
            "DoomCube: could not read v3 state before saving slot %d\n",
            slot
        );

        goto cleanup;
    }

    recordSectorsNeeded =
        GC_SaveV3RecordSectorCount(
            GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE,
            (size_t)compressedSize,
            (uint32_t)sectorSize
        );

    if (recordSectorsNeeded == 0 ||
        activeSuperblock.log_end_sector >
            UINT32_MAX -
            recordSectorsNeeded)
    {
        CARD_Close(
            &file
        );

        DC_WARN(
            "DoomCube: invalid v3 save geometry for slot %d\n",
            slot
        );

        goto cleanup;
    }

    requiredEnd =
        activeSuperblock.log_end_sector +
        recordSectorsNeeded;

    if (requiredEnd >
        containerSectors)
    {
        CARD_Close(
            &file
        );

        /*
         * Preserve the already-proven first transition:
         *
         *     16 -> 32 blocks
         *
         * Once the normal 32-block target is reached, stale append-only
         * history must not force 32 -> 64.  Compact first and grow only
         * when the actual live logical state still cannot fit.
         */
        if (containerSectors >=
            GC_SAVE_V3_NORMAL_TARGET_SECTORS)
        {
            if (!compactProductionV3Container(
                    &compacted))
            {
                DC_WARN(
                    "DoomCube: v3 compaction failed before "
                    "saving slot %d\n",
                    slot
                );

                goto cleanup;
            }

            if (compacted)
            {
                if (!openProductionV3Container(
                        &file,
                        &containerSectors))
                {
                    DC_WARN(
                        "DoomCube: compacted v3 container "
                        "could not be reopened for slot %d\n",
                        slot
                    );

                    goto cleanup;
                }

                if (!GC_SaveV3CardReadAuthoritativeSuperblock(
                        &file,
                        configWorkBuffer,
                        (size_t)sectorSize,
                        (uint32_t)sectorSize,
                        containerSectors,
                        &activeSuperblock,
                        &activeSuperblockSector))
                {
                    CARD_Close(
                        &file
                    );

                    DC_WARN(
                        "DoomCube: compacted v3 state could "
                        "not be read for slot %d\n",
                        slot
                    );

                    goto cleanup;
                }

                if (activeSuperblock.log_end_sector >
                        UINT32_MAX -
                        recordSectorsNeeded)
                {
                    CARD_Close(
                        &file
                    );

                    goto cleanup;
                }

                requiredEnd =
                    activeSuperblock.log_end_sector +
                    recordSectorsNeeded;

                if (requiredEnd <=
                    containerSectors)
                {
                    DC_INFO(
                        "DoomCube: v3 compaction reclaimed enough space: "
                        "slot=%d log_end=%u record_sectors=%u "
                        "container=%u blocks\n",
                        slot,
                        (unsigned int)activeSuperblock.log_end_sector,
                        (unsigned int)recordSectorsNeeded,
                        (unsigned int)containerSectors
                    );

                    goto capacity_ready;
                }

                CARD_Close(
                    &file
                );

                DC_INFO(
                    "DoomCube: v3 compacted live state still requires "
                    "growth: slot=%d required_end=%u container=%u\n",
                    slot,
                    (unsigned int)requiredEnd,
                    (unsigned int)containerSectors
                );
            }
        }

        /*
         * Either this is the normal 16 -> 32 transition, compaction found
         * nothing reclaimable, or the genuinely live state still exceeds
         * the current container.
         */
        targetSectors =
            containerSectors;

        if (targetSectors <
            GC_SAVE_V3_NORMAL_TARGET_SECTORS)
        {
            targetSectors =
                GC_SAVE_V3_NORMAL_TARGET_SECTORS;
        }

        while (targetSectors <
               requiredEnd)
        {
            if (targetSectors >
                UINT32_MAX / 2u)
            {
                DC_WARN(
                    "DoomCube: v3 growth overflow for slot %d\n",
                    slot
                );

                goto cleanup;
            }

            targetSectors *=
                2u;
        }

        if (targetSectors <=
            containerSectors)
        {
            if (containerSectors >
                UINT32_MAX / 2u)
            {
                DC_WARN(
                    "DoomCube: v3 growth overflow for slot %d\n",
                    slot
                );

                goto cleanup;
            }

            targetSectors =
                containerSectors *
                2u;
        }

        DC_INFO(
            "DoomCube: v3 growth required: "
            "slot=%d log_end=%u record_sectors=%u "
            "container=%u -> %u blocks\n",
            slot,
            (unsigned int)activeSuperblock.log_end_sector,
            (unsigned int)recordSectorsNeeded,
            (unsigned int)containerSectors,
            (unsigned int)targetSectors
        );

        if (!growProductionV3Container(
                targetSectors))
        {
            DC_WARN(
                "DoomCube: v3 growth failed for slot %d\n",
                slot
            );

            goto cleanup;
        }

        if (!openProductionV3Container(
                &file,
                &containerSectors))
        {
            DC_WARN(
                "DoomCube: grown v3 container could not be reopened "
                "for slot %d\n",
                slot
            );

            goto cleanup;
        }
    }


capacity_ready:

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
            "(container=%u blocks after preflight/growth)\n",
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

/*
 * Open the production v3 container with enough committed-log capacity for
 * one additional record.
 *
 * This mirrors the savegame lifecycle:
 *
 *     append directly when space exists
 *     16 -> 32 block growth for the initial transition
 *     compact stale append history once at normal size
 *     grow only when the live compacted state genuinely needs it
 *
 * On success, *file remains open.
 */
static bool prepareProductionV3Append(
    uint32_t recordSectorsNeeded,
    card_file *file,
    uint32_t *containerSectorsOut)
{
    gc_save_v3_superblock_t active;

    uint32_t containerSectors;
    uint32_t requiredEnd;
    uint32_t targetSectors;

    bool compacted = false;

    if (!file ||
        !containerSectorsOut ||
        recordSectorsNeeded == 0)
    {
        return false;
    }

    if (!openProductionV3Container(
            file,
            &containerSectors))
    {
        return false;
    }

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            file,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            containerSectors,
            &active,
            NULL))
    {
        CARD_Close(
            file
        );

        return false;
    }

    if (active.log_end_sector >
        UINT32_MAX -
            recordSectorsNeeded)
    {
        CARD_Close(
            file
        );

        return false;
    }

    requiredEnd =
        active.log_end_sector +
        recordSectorsNeeded;

    if (requiredEnd <=
        containerSectors)
    {
        *containerSectorsOut =
            containerSectors;

        return true;
    }

    CARD_Close(
        file
    );


    /*
     * At the normal 32-block size, compact stale records before considering
     * further growth.
     */
    if (containerSectors >=
        GC_SAVE_V3_NORMAL_TARGET_SECTORS)
    {
        if (!compactProductionV3Container(
                &compacted))
        {
            DC_WARN(
                "DoomCube: v3 compaction failed before config write\n"
            );

            return false;
        }

        if (compacted)
        {
            if (!openProductionV3Container(
                    file,
                    &containerSectors))
            {
                return false;
            }

            if (!GC_SaveV3CardReadAuthoritativeSuperblock(
                    file,
                    configWorkBuffer,
                    (size_t)sectorSize,
                    (uint32_t)sectorSize,
                    containerSectors,
                    &active,
                    NULL))
            {
                CARD_Close(
                    file
                );

                return false;
            }

            if (active.log_end_sector >
                UINT32_MAX -
                    recordSectorsNeeded)
            {
                CARD_Close(
                    file
                );

                return false;
            }

            requiredEnd =
                active.log_end_sector +
                recordSectorsNeeded;

            if (requiredEnd <=
                containerSectors)
            {
                DC_INFO(
                    "DoomCube: v3 compaction reclaimed enough "
                    "space for global config: "
                    "log_end=%u record_sectors=%u container=%u\n",
                    (unsigned int)active.log_end_sector,
                    (unsigned int)recordSectorsNeeded,
                    (unsigned int)containerSectors
                );

                *containerSectorsOut =
                    containerSectors;

                return true;
            }

            CARD_Close(
                file
            );
        }
    }


    targetSectors =
        containerSectors;

    if (targetSectors <
        GC_SAVE_V3_NORMAL_TARGET_SECTORS)
    {
        targetSectors =
            GC_SAVE_V3_NORMAL_TARGET_SECTORS;
    }

    while (targetSectors <
           requiredEnd)
    {
        if (targetSectors >
            UINT32_MAX / 2u)
        {
            DC_WARN(
                "DoomCube: v3 config growth overflow\n"
            );

            return false;
        }

        targetSectors *=
            2u;
    }

    if (targetSectors <=
        containerSectors)
    {
        if (containerSectors >
            UINT32_MAX / 2u)
        {
            return false;
        }

        targetSectors =
            containerSectors *
            2u;
    }

    DC_INFO(
        "DoomCube: v3 growth required for global config: "
        "log_end=%u record_sectors=%u container=%u -> %u blocks\n",
        (unsigned int)active.log_end_sector,
        (unsigned int)recordSectorsNeeded,
        (unsigned int)containerSectors,
        (unsigned int)targetSectors
    );

    if (!growProductionV3Container(
            targetSectors))
    {
        DC_WARN(
            "DoomCube: v3 growth failed for global config\n"
        );

        return false;
    }

    if (!openProductionV3Container(
            file,
            &containerSectors))
    {
        return false;
    }

    *containerSectorsOut =
        containerSectors;

    return true;
}


/*
 * Locate the newest committed global CONFIG record.
 *
 * CONFIG has no IWAD/PWAD identity.  There is exactly one logical config
 * key across the entire DoomCube installation.
 */
static bool findProductionV3ConfigRecord(
    card_file *file,
    uint32_t containerSectors,
    gc_save_v3_superblock_t *activeOut,
    gc_save_v3_record_header_t *recordOut,
    uint32_t *recordSectorOut)
{
    gc_save_v3_superblock_t active;

    gc_save_v3_record_header_t candidate;
    gc_save_v3_record_header_t newest;

    uint32_t cursor;
    uint32_t newestSector = 0;

    bool found = false;

    if (!file ||
        !recordOut ||
        !recordSectorOut)
    {
        return false;
    }

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            file,
            configWorkBuffer,
            (size_t)sectorSize,
            (uint32_t)sectorSize,
            containerSectors,
            &active,
            NULL))
    {
        return false;
    }

    if (active.log_start_sector !=
            GC_SAVE_V3_DATA_START_SECTOR ||
        active.log_end_sector <
            active.log_start_sector ||
        active.log_end_sector >
            containerSectors)
    {
        return false;
    }

    memset(
        &newest,
        0,
        sizeof(newest)
    );

    cursor =
        active.log_start_sector;

    while (cursor <
           active.log_end_sector)
    {
        memset(
            &candidate,
            0,
            sizeof(candidate)
        );

        if (!GC_SaveV3CardReadRecord(
                file,
                configWorkBuffer,
                (size_t)sectorSize,
                (uint32_t)sectorSize,
                containerSectors,
                active.log_end_sector,
                cursor,
                &candidate,
                NULL,
                0,
                NULL))
        {
            DC_WARN(
                "DoomCube: v3 config scan failed at sector %u\n",
                (unsigned int)cursor
            );

            return false;
        }

        if (candidate.record_sectors == 0 ||
            candidate.record_sectors >
                active.log_end_sector -
                    cursor)
        {
            return false;
        }

        if (candidate.record_type ==
            GC_SAVE_V3_RECORD_CONFIG)
        {
            if (!found ||
                v3GenerationNewer(
                    candidate.generation,
                    newest.generation))
            {
                newest =
                    candidate;

                newestSector =
                    cursor;

                found =
                    true;
            }
        }

        cursor +=
            candidate.record_sectors;
    }

    if (cursor !=
        active.log_end_sector)
    {
        return false;
    }

    if (!found)
    {
        return false;
    }

    if (activeOut)
    {
        *activeOut =
            active;
    }

    *recordOut =
        newest;

    *recordSectorOut =
        newestSector;

    return true;
}


bool GC_MemoryCardWriteConfig(
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
    uint32_t recordSectorsNeeded;

    uint32_t committedSuperblockSector;
    uint32_t recordSector;

    bool success = false;

    if (!cardMounted ||
        !data ||
        size == 0 ||
        size > 0xffffffffu)
    {
        return false;
    }

    /*
     * Keep the historical Doom config size ceiling.  The persistence
     * backend changes here, not the public configuration-size contract.
     */
    if (size >
        configCapacity())
    {
        DC_WARN(
            "DoomCube: global config too large: %u > %u\n",
            (unsigned int)size,
            (unsigned int)configCapacity()
        );

        return false;
    }

    compressedCapacity =
        compressBound(
            (uLong)size
        );

    if (compressedCapacity == 0)
    {
        return false;
    }

    compressedData =
        malloc(
            (size_t)compressedCapacity
        );

    if (!compressedData)
    {
        DC_WARN(
            "DoomCube: v3 config compression allocation failed: %u bytes\n",
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
            "DoomCube: v3 config compression failed\n"
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
        GC_SAVE_V3_RECORD_CONFIG;

    /*
     * CONFIG uses slot zero by format definition but is not a save slot
     * and is not tied to currentV3Identity.
     */
    record.slot =
        0;

    record.timestamp =
        (uint32_t)time(NULL);

    record.raw_size =
        (uint32_t)size;

    record.raw_crc32 =
        (uint32_t)rawCrc;

    recordSectorsNeeded =
        GC_SaveV3RecordSectorCount(
            GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE,
            (size_t)compressedSize,
            (uint32_t)sectorSize
        );

    if (recordSectorsNeeded == 0)
    {
        goto cleanup;
    }

    if (!prepareProductionV3Append(
            recordSectorsNeeded,
            &file,
            &containerSectors))
    {
        DC_WARN(
            "DoomCube: could not prepare v3 container "
            "for global config\n"
        );

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
            "DoomCube: v3 global config append failed\n"
        );

        goto cleanup;
    }

    DC_INFO(
        "DoomCube: v3 config committed: "
        "raw=%u compressed=%u sectors=%u "
        "record_sector=%u generation=%u superblock=%c\n",
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


bool GC_MemoryCardReadConfig(
    void *output,
    size_t outputSize,
    size_t *actualSize)
{
    card_file file;

    gc_save_v3_superblock_t active;
    gc_save_v3_record_header_t record;

    unsigned char *compressedData = NULL;

    size_t compressedSize = 0;

    uLongf decodedSize;
    uLong rawCrc;

    uint32_t containerSectors;
    uint32_t recordSector;

    bool success = false;

    if (actualSize)
    {
        *actualSize =
            0;
    }

    if (!cardMounted)
    {
        return false;
    }

    if (!openProductionV3Container(
            &file,
            &containerSectors))
    {
        return false;
    }

    if (!findProductionV3ConfigRecord(
            &file,
            containerSectors,
            &active,
            &record,
            &recordSector))
    {
        goto close_file;
    }

    if (actualSize)
    {
        *actualSize =
            record.raw_size;
    }

    /*
     * Permit the same useful size-query convention as the save backend.
     */
    if (!output)
    {
        success =
            true;

        goto close_file;
    }

    if (outputSize <
        record.raw_size)
    {
        DC_WARN(
            "DoomCube: destination buffer too small for global config: "
            "%u < %u\n",
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
            "DoomCube: compressed config allocation failed: %u bytes\n",
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
            recordSector,
            &record,
            compressedData,
            record.compressed_size,
            &compressedSize))
    {
        DC_WARN(
            "DoomCube: v3 config record read failed\n"
        );

        goto close_file;
    }

    if (record.record_type !=
            GC_SAVE_V3_RECORD_CONFIG ||
        compressedSize !=
            record.compressed_size)
    {
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
            "DoomCube: v3 config decompression failed\n"
        );

        goto close_file;
    }

    if (decodedSize !=
        (uLongf)record.raw_size)
    {
        DC_WARN(
            "DoomCube: v3 config raw size mismatch\n"
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
            "DoomCube: v3 config raw CRC mismatch\n"
        );

        goto close_file;
    }

    success =
        true;

    DC_INFO(
        "DoomCube: v3 config loaded: "
        "raw=%u compressed=%u generation=%u record_sector=%u\n",
        (unsigned int)record.raw_size,
        (unsigned int)record.compressed_size,
        (unsigned int)record.generation,
        (unsigned int)recordSector
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

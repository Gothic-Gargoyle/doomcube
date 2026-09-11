#include "gc_carryhandle_dogfood.h"

#include "gc_card_presentation_data.h"
#include "gc_debug.h"
#include "gc_memcard.h"

#include <carryhandle/carryhandle.h>

#include <ogc/card.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>


#define GC_CH_DOGFOOD_SLOT          0
#define GC_CH_DOGFOOD_FILENAME      "DOOMCUBE0"
#define GC_CH_DOGFOOD_SECTORS       64u

#define GC_CH_DOGFOOD_PATH_MAX      256u
#define GC_CH_DOGFOOD_SCOPE_MAX     640u


static const unsigned char dogfoodSaveKey[] =
{
    'd', 'o', 'o', 'm', 's', 'a', 'v', '0', '.', 'd', 's', 'g'
};

static const unsigned char dogfoodConfigKey[] =
{
    'd', 'o', 'o', 'm', 'c', 'u', 'b', 'e', '.', 'c', 'f', 'g'
};

#define GC_CH_DOGFOOD_CONFIG_MAX 8192u

static unsigned char dogfoodConfigCompareBuffer[
    GC_CH_DOGFOOD_CONFIG_MAX
] __attribute__((aligned(32)));


static const CH_ApplicationSaveDescriptor dogfoodDescriptor =
{
    .filename =
        GC_CH_DOGFOOD_FILENAME,

    .sector_count =
        GC_CH_DOGFOOD_SECTORS,

    .presentation_offset =
        CH_APPLICATION_SAVE_PRESENTATION_OFFSET,

    .presentation_data =
        gc_card_presentation_data,

    .presentation_size =
        GC_CARD_PRESENTATION_DATA_SIZE
};


static char dogfoodIwadPath[
    GC_CH_DOGFOOD_PATH_MAX
];

static char dogfoodPwadPath[
    GC_CH_DOGFOOD_PATH_MAX
];

static unsigned char dogfoodScope[
    GC_CH_DOGFOOD_SCOPE_MAX
];

static size_t dogfoodScopeSize;

static bool dogfoodIdentityValid;


/*
 * Vanilla Doom's maximum serialized save size is 0x2c000 / 180224 bytes.
 *
 * Keep one verified raw slot-0 save resident while Doom is running.
 * The cache is launch-identity scoped and is invalidated whenever a new
 * IWAD/PWAD identity is selected.
 *
 * This does NOT weaken on-card validation:
 *
 *   - startup prime comes only from a successful CH_ApplicationSaveGet()
 *   - writes replace the cache only after a definitely successful
 *     CH_ApplicationSavePut() + Close()
 *
 * It merely prevents Doom's menu from rescanning the complete append-only
 * transaction history every time it asks whether slot 0 exists.
 */
#define GC_CH_DOGFOOD_SAVE_MAX 180224u

static unsigned char dogfoodSaveCache[
    GC_CH_DOGFOOD_SAVE_MAX
];

static size_t dogfoodSaveCacheSize;

static bool dogfoodSaveCacheValid;



static void invalidateDogfoodSaveCache(void)
{
    dogfoodSaveCacheSize =
        0u;

    dogfoodSaveCacheValid =
        false;
}


static bool updateDogfoodSaveCache(
    const void *data,
    size_t size)
{
    if (!data ||
        size == 0u ||
        size >
            sizeof(dogfoodSaveCache))
    {
        return false;
    }

    /*
     * memmove also permits priming directly into dogfoodSaveCache.
     */
    memmove(
        dogfoodSaveCache,
        data,
        size);

    dogfoodSaveCacheSize =
        size;

    dogfoodSaveCacheValid =
        true;

    return true;
}


/*
 * DoomCube-specific CarryHandle object encoding.
 *
 * CarryHandle deliberately stores opaque application payloads. DoomCube's
 * old v3 backend already proved that normal .dsg files compress extremely
 * well, so keep compression here at the application boundary rather than
 * teaching the generic framework about Doom.
 *
 * On-card object payload:
 *
 *     0x00  u32 BE  magic "DCF1"
 *     0x04  u32 BE  format version
 *     0x08  u32 BE  raw byte count
 *     0x0c  u32 BE  compressed byte count
 *     0x10  u32 BE  CRC32 of raw Doom save
 *     0x14  ...     zlib/DEFLATE stream
 *
 * Older framed/raw object decoding remains harmless internally, but the
 * physical v1.1.2 card-file contract is the new 64-block DOOMCUBE0.
 */

#define GC_CH_DOGFOOD_PAYLOAD_MAGIC       0x44434631u
#define GC_CH_DOGFOOD_PAYLOAD_VERSION     1u
#define GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE 20u


/*
 * DOOMCUBE_FIXED_ZLIB_V23
 *
 * The CARD representation stays compressed, but saving/loading may not depend
 * on the fragmented gameplay heap.
 *
 * The largest vanilla Doom serialized save is 180224 bytes. zlib's worst-case
 * stored output for an input this small is only slightly larger than input;
 * 2048 bytes of fixed slop is deliberately generous and is checked against
 * compressBound() before every PUT.
 *
 * zlib itself normally allocates its deflate/inflate state from malloc().
 *
 * DoomCube uses deflate memLevel 6 below, so the fixed workspace can be much
 * smaller than zlib's default memLevel 8 footprint. Deflate and inflate
 * operations are serialized, so one 192 KiB arena is reused by both
 * directions.
 */
#define GC_CH_DOGFOOD_STORED_SLOP       2048u
#define GC_CH_DOGFOOD_STORED_MAX \
    (GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE + \
     GC_CH_DOGFOOD_SAVE_MAX + \
     GC_CH_DOGFOOD_STORED_SLOP)

#define GC_CH_DOGFOOD_ZLIB_ARENA_SIZE   (192u * 1024u)
#define GC_CH_DOGFOOD_ZLIB_ALIGNMENT     32u

static unsigned char dogfoodStoredBuffer[
    GC_CH_DOGFOOD_STORED_MAX
] __attribute__((aligned(32)));

static unsigned char dogfoodZlibArena[
    GC_CH_DOGFOOD_ZLIB_ARENA_SIZE
] __attribute__((aligned(32)));

static size_t dogfoodZlibUsed;
static size_t dogfoodZlibPeak;
static size_t dogfoodZlibFailedRequest;
static bool dogfoodZlibAllocationFailed;


static void resetDogfoodZlibArena(void)
{
    dogfoodZlibUsed =
        0u;

    dogfoodZlibPeak =
        0u;

    dogfoodZlibFailedRequest =
        0u;

    dogfoodZlibAllocationFailed =
        false;
}


static voidpf dogfoodZAlloc(
    voidpf opaque,
    uInt items,
    uInt size)
{
    size_t bytes;
    size_t aligned;
    size_t end;

    (void)opaque;


    bytes =
        (size_t)items *
        (size_t)size;

    if (items != 0u &&
        bytes / (size_t)items !=
            (size_t)size)
    {
        dogfoodZlibAllocationFailed =
            true;

        dogfoodZlibFailedRequest =
            SIZE_MAX;

        return Z_NULL;
    }


    if (bytes == 0u)
    {
        bytes =
            1u;
    }


    if (dogfoodZlibUsed >
        SIZE_MAX -
            (GC_CH_DOGFOOD_ZLIB_ALIGNMENT - 1u))
    {
        dogfoodZlibAllocationFailed =
            true;

        dogfoodZlibFailedRequest =
            bytes;

        return Z_NULL;
    }


    aligned =
        (dogfoodZlibUsed +
         (GC_CH_DOGFOOD_ZLIB_ALIGNMENT - 1u)) &
        ~(size_t)(
            GC_CH_DOGFOOD_ZLIB_ALIGNMENT - 1u
        );


    if (aligned >
            sizeof(dogfoodZlibArena) ||
        bytes >
            sizeof(dogfoodZlibArena) -
                aligned)
    {
        dogfoodZlibAllocationFailed =
            true;

        dogfoodZlibFailedRequest =
            bytes;

        return Z_NULL;
    }


    end =
        aligned +
        bytes;

    dogfoodZlibUsed =
        end;

    if (dogfoodZlibPeak <
        dogfoodZlibUsed)
    {
        dogfoodZlibPeak =
            dogfoodZlibUsed;
    }


    return
        (voidpf)(
            dogfoodZlibArena +
            aligned
        );
}


static void dogfoodZFree(
    voidpf opaque,
    voidpf address)
{
    /*
     * One bump arena is reset wholesale after deflateEnd()/inflateEnd().
     * Individual frees are intentionally no-ops.
     */
    (void)opaque;
    (void)address;
}


static void logDogfoodZlibFailure(
    const char *operation,
    int zResult)
{
    DC_WARN(
        "DoomCube: CarryHandle save %s zlib failed: %d "
        "fixed_used=%lu fixed_peak=%lu fixed_capacity=%lu "
        "failed_request=%lu allocator_failed=%d\n",
        operation ? operation : "UNKNOWN",
        zResult,
        (unsigned long)dogfoodZlibUsed,
        (unsigned long)dogfoodZlibPeak,
        (unsigned long)sizeof(dogfoodZlibArena),
        (unsigned long)dogfoodZlibFailedRequest,
        dogfoodZlibAllocationFailed ? 1 : 0
    );
}


static uint32_t readBe32(
    const unsigned char *p)
{
    return
        ((uint32_t)p[0] << 24) |
        ((uint32_t)p[1] << 16) |
        ((uint32_t)p[2] << 8) |
        (uint32_t)p[3];
}


static void writeBe32(
    unsigned char *p,
    uint32_t value)
{
    p[0] =
        (unsigned char)(
            value >> 24
        );

    p[1] =
        (unsigned char)(
            value >> 16
        );

    p[2] =
        (unsigned char)(
            value >> 8
        );

    p[3] =
        (unsigned char)value;
}


/* ------------------------------------------------------------------------- */
/* Launch identity                                                           */
/* ------------------------------------------------------------------------- */

void GC_CHDogfoodSetLaunchIdentity(
    const char *iwadPath,
    const char *pwadPath)
{
    int written;

    const char *pwad =
        pwadPath
            ? pwadPath
            : "";


    dogfoodIdentityValid =
        false;

    dogfoodScopeSize =
        0u;

    invalidateDogfoodSaveCache();

    dogfoodIwadPath[0] =
        '\0';

    dogfoodPwadPath[0] =
        '\0';


    if (!iwadPath ||
        iwadPath[0] == '\0')
    {
        DC_WARN(
            "DoomCube: CarryHandle save launch identity missing IWAD\n"
        );

        return;
    }


    written =
        snprintf(
            dogfoodIwadPath,
            sizeof(dogfoodIwadPath),
            "%s",
            iwadPath
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(dogfoodIwadPath))
    {
        DC_WARN(
            "DoomCube: CarryHandle save IWAD path too long\n"
        );

        return;
    }


    written =
        snprintf(
            dogfoodPwadPath,
            sizeof(dogfoodPwadPath),
            "%s",
            pwad
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(dogfoodPwadPath))
    {
        DC_WARN(
            "DoomCube: CarryHandle save PWAD path too long\n"
        );

        return;
    }


    written =
        snprintf(
            (char *)dogfoodScope,
            sizeof(dogfoodScope),
            "iwad=%s\npwad=%s",
            dogfoodIwadPath,
            dogfoodPwadPath
        );

    if (written < 0 ||
        (size_t)written >=
            sizeof(dogfoodScope))
    {
        DC_WARN(
            "DoomCube: CarryHandle save scope too long\n"
        );

        return;
    }


    dogfoodScopeSize =
        (size_t)written;

    dogfoodIdentityValid =
        true;


    DC_INFO(
        "DoomCube: CarryHandle save identity: "
        "IWAD=%s PWAD=%s\n",
        dogfoodIwadPath,
        dogfoodPwadPath[0]
            ? dogfoodPwadPath
            : "<none>"
    );
}


/* ------------------------------------------------------------------------- */
/* Verified launch-time save cache                                           */
/* ------------------------------------------------------------------------- */

void GC_CHDogfoodPrimeSaveCache(void)
{
    size_t actualSize =
        0u;


    if (!dogfoodIdentityValid)
    {
        return;
    }


    invalidateDogfoodSaveCache();


    DC_INFO(
        "DoomCube: CarryHandle save priming slot 0 cache\n"
    );


    /*
     * GC_CHDogfoodReadSave() performs the normal full transaction recovery,
     * committed-log validation, payload verification, Doom framing inflate,
     * and CRC check.
     *
     * Since the cache is currently invalid, this call cannot take the cache
     * fast path.
     */
    if (!GC_CHDogfoodReadSave(
            GC_CH_DOGFOOD_SLOT,
            dogfoodSaveCache,
            sizeof(dogfoodSaveCache),
            &actualSize))
    {
        invalidateDogfoodSaveCache();

        DC_DEBUG(
            "DoomCube: CarryHandle save slot 0 cache not primed\n"
        );

        return;
    }


    /*
     * ReadSave normally populates the cache itself. Set these explicitly too
     * so the prime contract remains obvious even if ReadSave is refactored.
     */
    dogfoodSaveCacheSize =
        actualSize;

    dogfoodSaveCacheValid =
        true;


    DC_INFO(
        "DoomCube: CarryHandle save slot 0 cache READY "
        "(%lu bytes)\n",
        (unsigned long)dogfoodSaveCacheSize
    );
}


/* ------------------------------------------------------------------------- */
/* CARD ownership hand-off                                                   */
/* ------------------------------------------------------------------------- */

static bool restoreLegacyCard(void)
{
    bool initialized;


    initialized =
        GC_MemoryCardInit();


    if (!initialized)
    {
        DC_WARN(
            "DoomCube: CarryHandle save could not remount "
            "legacy Memory Card backend\n"
        );

        return false;
    }


    /*
     * GC_MemoryCardShutdown() releases CARD ownership and work buffers;
     * it does not represent a new Doom launch.
     *
     * The launch identity was already established once after the launcher
     * selected the IWAD/PWAD.  Re-running SetLaunchIdentity() here caused
     * DoomCube to reread and CRC the complete IWAD/PWAD after every
     * CarryHandle Get/Put, producing multi-second menu stalls.
     *
     * Remount the DoomCube preflight CARD backend only.  Keep the existing launch
     * identity in memory.
     */
    DC_DEBUG(
        "DoomCube: CarryHandle save returned CARD A "
        "to preflight backend without identity rebuild\n"
    );


    return true;
}


static bool openDogfoodSave(
    CH_ApplicationSaveSession *save)
{
    CH_ApplicationSaveResult result;


    if (!save ||
        GC_MemoryCardGetStatus() !=
            GC_MEMCARD_STATUS_READY)
    {
        return false;
    }


    /*
     * Single-container ownership hand-off.
     *
     * DoomCube's existing backend owns a long-lived mount on CARD A.
     * CH_ApplicationSaveOpen() intentionally owns its own mount lifecycle.
     *
     * Hand the card to CarryHandle for this one operation, then remount the
     * old backend afterward.  Once the real Doom path is proven, card mount
     * ownership can be unified cleanly instead of guessed at beforehand.
     */
    GC_MemoryCardShutdown();


    result =
        CH_ApplicationSaveOpen(
            save,
            CH_ApplicationGetInfo(),
            &dogfoodDescriptor,
            CARD_SLOTA
        );


    if (result !=
        CH_APPLICATION_SAVE_RESULT_OK)
    {
        DC_WARN(
            "DoomCube: CarryHandle save Open failed: "
            "result=%d CARD=%ld TX=%d\n",
            (int)result,
            (long)save->card_result,
            (int)save->tx_result
        );


        if (!restoreLegacyCard())
        {
            DC_WARN(
                "DoomCube: CarryHandle save legacy restore "
                "also failed\n"
            );
        }


        return false;
    }


    DC_DEBUG(
        "DoomCube: CarryHandle save %s %s "
        "(%lu sectors)\n",
        dogfoodDescriptor.filename,
        CH_ApplicationSaveWasCreated(save)
            ? "created"
            : "opened",
        (unsigned long)dogfoodDescriptor.sector_count
    );


    return true;
}


static bool closeDogfoodSave(
    CH_ApplicationSaveSession *save)
{
    CH_ApplicationSaveResult closeResult;

    bool restored;


    closeResult =
        CH_ApplicationSaveClose(
            save
        );


    restored =
        restoreLegacyCard();


    if (closeResult !=
        CH_APPLICATION_SAVE_RESULT_OK)
    {
        DC_WARN(
            "DoomCube: CarryHandle save Close failed: %d\n",
            (int)closeResult
        );
    }


    return
        closeResult ==
            CH_APPLICATION_SAVE_RESULT_OK &&
        restored;
}


/* ------------------------------------------------------------------------- */
/* Physical single-container creation                                        */
/* ------------------------------------------------------------------------- */

bool GC_CHDogfoodCreateContainer(void)
{
    CH_ApplicationSaveSession save =
        {0};

    CH_ApplicationSaveResult openResult;
    CH_ApplicationSaveResult closeResult;

    bool created;
    bool restored;


    /*
     * The player has already approved destructive CREATE in gc_memcard.c.
     * Release the preflight mount and let CarryHandle own initialization.
     */
    GC_MemoryCardShutdown();

    openResult =
        CH_ApplicationSaveOpen(
            &save,
            CH_ApplicationGetInfo(),
            &dogfoodDescriptor,
            CARD_SLOTA);

    if (openResult !=
        CH_APPLICATION_SAVE_RESULT_OK)
    {
        DC_WARN(
            "DoomCube: single-card CarryHandle create failed: "
            "result=%d CARD=%ld TX=%d\n",
            (int)openResult,
            (long)save.card_result,
            (int)save.tx_result);

        (void)restoreLegacyCard();

        return false;
    }

    created =
        CH_ApplicationSaveWasCreated(
            &save);

    closeResult =
        CH_ApplicationSaveClose(
            &save);

    restored =
        restoreLegacyCard();

    if (!created ||
        closeResult !=
            CH_APPLICATION_SAVE_RESULT_OK ||
        !restored ||
        GC_MemoryCardGetStatus() !=
            GC_MEMCARD_STATUS_READY)
    {
        DC_WARN(
            "DoomCube: single-card create verification failed: "
            "created=%d close=%d restored=%d status=%d\n",
            created ? 1 : 0,
            (int)closeResult,
            restored ? 1 : 0,
            (int)GC_MemoryCardGetStatus());

        return false;
    }

    DC_INFO(
        "DoomCube: single 64-block DOOMCUBE0 container created\n");

    /*
     * A brand-new application container has no config object yet.
     *
     * Seed one valid, intentionally empty text configuration exactly once
     * during explicit CREATE. This lets launcher OPTIONS/CONTROLS become
     * writable immediately, before Doom has ever run.
     *
     * The normal config writer owns the one logical PUT. We deliberately do
     * not add another CH_ApplicationSavePut() call site here.
     */
    if (!GC_CHDogfoodWriteConfig(
            "\n",
            1u))
    {
        DC_WARN(
            "DoomCube: failed to seed initial global config object\n");

        return false;
    }

    DC_INFO(
        "DoomCube: initial global config object created\n");

    return true;
}


/* ------------------------------------------------------------------------- */
/* Global configuration object                                               */
/* ------------------------------------------------------------------------- */

bool GC_CHDogfoodReadConfig(
    void *buffer,
    size_t bufferSize,
    size_t *actualSize)
{
    CH_ApplicationSaveSession save =
        {0};

    CH_PersistResult result;

    size_t objectSize =
        0u;

    bool closeOk;


    if (actualSize)
    {
        *actualSize =
            0u;
    }

    if (!buffer ||
        bufferSize == 0u ||
        !actualSize)
    {
        return false;
    }

    if (!openDogfoodSave(
            &save))
    {
        return false;
    }

    result =
        CH_ApplicationSaveGet(
            &save,
            NULL,
            0u,
            dogfoodConfigKey,
            sizeof(dogfoodConfigKey),
            buffer,
            bufferSize,
            &objectSize);

    closeOk =
        closeDogfoodSave(
            &save);

    if (result ==
        CH_PERSIST_RESULT_NOT_FOUND)
    {
        DC_DEBUG(
            "DoomCube: CarryHandle global config not found\n");

        return false;
    }

    if (result !=
            CH_PERSIST_RESULT_OK ||
        !closeOk)
    {
        DC_WARN(
            "DoomCube: CarryHandle global config GET failed: "
            "result=%d close=%d\n",
            (int)result,
            closeOk ? 1 : 0);

        return false;
    }

    *actualSize =
        objectSize;

    DC_DEBUG(
        "DoomCube: CarryHandle global config GET PASS (%lu bytes)\n",
        (unsigned long)objectSize);

    return true;
}


bool GC_CHDogfoodWriteConfig(
    const void *data,
    size_t size)
{
    CH_ApplicationSaveSession save =
        {0};

    CH_PersistResult getResult;
    CH_PersistResult putResult =
        CH_PERSIST_RESULT_OK;

    size_t existingSize =
        0u;

    bool unchanged =
        false;

    bool closeOk;


    if (!data ||
        size == 0u ||
        size > GC_CH_DOGFOOD_CONFIG_MAX)
    {
        return false;
    }

    if (!openDogfoodSave(
            &save))
    {
        return false;
    }

    /*
     * Avoid wearing the card for an unchanged exit-time M_SaveDefaults().
     * This GET is read-only. A physical PUT occurs only when bytes changed.
     */
    getResult =
        CH_ApplicationSaveGet(
            &save,
            NULL,
            0u,
            dogfoodConfigKey,
            sizeof(dogfoodConfigKey),
            dogfoodConfigCompareBuffer,
            sizeof(dogfoodConfigCompareBuffer),
            &existingSize);

    if (getResult ==
            CH_PERSIST_RESULT_OK &&
        existingSize == size &&
        memcmp(
            dogfoodConfigCompareBuffer,
            data,
            size) == 0)
    {
        unchanged =
            true;
    }
    else if (getResult !=
                 CH_PERSIST_RESULT_OK &&
             getResult !=
                 CH_PERSIST_RESULT_NOT_FOUND)
    {
        DC_WARN(
            "DoomCube: CarryHandle config compare GET failed: %d\n",
            (int)getResult);

        closeOk =
            closeDogfoodSave(
                &save);

        (void)closeOk;

        return false;
    }

    if (!unchanged)
    {
        /*
         * Exactly one logical persistent PUT for one changed config commit.
         */
        putResult =
            CH_ApplicationSavePut(
                &save,
                NULL,
                0u,
                dogfoodConfigKey,
                sizeof(dogfoodConfigKey),
                data,
                size);
    }

    closeOk =
        closeDogfoodSave(
            &save);

    if (unchanged)
    {
        if (!closeOk)
        {
            return false;
        }

        DC_DEBUG(
            "DoomCube: global config unchanged; physical PUT skipped\n");

        return true;
    }

    if (putResult !=
            CH_PERSIST_RESULT_OK ||
        !closeOk)
    {
        DC_WARN(
            "DoomCube: CarryHandle global config PUT failed: "
            "result=%d close=%d\n",
            (int)putResult,
            closeOk ? 1 : 0);

        return false;
    }

    DC_INFO(
        "DoomCube: CarryHandle global config PUT PASS (%lu bytes)\n",
        (unsigned long)size);

    return true;
}



/* ------------------------------------------------------------------------- */
/* Slot 0 persistence                                                        */
/* ------------------------------------------------------------------------- */

bool GC_CHDogfoodReadSave(
    int slot,
    void *buffer,
    size_t bufferSize,
    size_t *actualSize)
{
    CH_ApplicationSaveSession save =
        {0};

    CH_PersistResult result;


    size_t storedSize =
        0u;

    bool closeOk;
    bool success =
        false;


    if (slot !=
            GC_CH_DOGFOOD_SLOT ||
actualSize)
    {
        *actualSize =
            0u;
    }


    if (        !buffer ||
        bufferSize == 0u ||
        !actualSize ||
        !dogfoodIdentityValid)
    {
        return false;
    }




    /*
     * Doom probes save slots repeatedly while entering/drawing the
     * Save/Load menus. Once this launch has a fully verified slot-0 image,
     * those probes do not need another CARD transaction-log scan.
     */
    if (dogfoodSaveCacheValid)
    {
        if (dogfoodSaveCacheSize >
            bufferSize)
        {
            DC_WARN(
                "DoomCube: CarryHandle save slot 0 cache "
                "exceeds caller buffer\n"
            );

            return false;
        }


        memcpy(
            buffer,
            dogfoodSaveCache,
            dogfoodSaveCacheSize
        );


        *actualSize =
            dogfoodSaveCacheSize;


        DC_DEBUG(
            "DoomCube: CarryHandle save slot 0 "
            "GET CACHE PASS (%lu bytes)\n",
            (unsigned long)dogfoodSaveCacheSize
        );


        return true;
    }


    if (!openDogfoodSave(
            &save))
    {
        return false;
    }


    /*
     * Read the compressed/framed object into one permanent scratch buffer.
     * No gameplay-heap allocation occurs here.
     */
    result =
        CH_ApplicationSaveGet(
            &save,
            dogfoodScope,
            dogfoodScopeSize,
            dogfoodSaveKey,
            sizeof(dogfoodSaveKey),
            dogfoodStoredBuffer,
            sizeof(dogfoodStoredBuffer),
            &storedSize
        );


    if (result ==
        CH_PERSIST_RESULT_OK)
    {
        if (storedSize >=
                GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE &&
            readBe32(
                dogfoodStoredBuffer + 0u) ==
                GC_CH_DOGFOOD_PAYLOAD_MAGIC &&
            readBe32(
                dogfoodStoredBuffer + 4u) ==
                GC_CH_DOGFOOD_PAYLOAD_VERSION)
        {
            uint32_t rawSize =
                readBe32(
                    dogfoodStoredBuffer + 8u
                );

            uint32_t compressedSize =
                readBe32(
                    dogfoodStoredBuffer + 12u
                );

            uint32_t expectedCrc =
                readBe32(
                    dogfoodStoredBuffer + 16u
                );

            z_stream stream =
                {0};

            uLong actualCrc;

            int zResult;
            int zEndResult =
                Z_OK;


            if (rawSize == 0u ||
                (size_t)rawSize >
                    bufferSize ||
                (size_t)rawSize >
                    GC_CH_DOGFOOD_SAVE_MAX ||
                (size_t)compressedSize !=
                    storedSize -
                    GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE ||
                compressedSize == 0u)
            {
                DC_WARN(
                    "DoomCube: CarryHandle save slot 0 "
                    "compressed frame invalid "
                    "raw=%lu compressed=%lu stored=%lu\n",
                    (unsigned long)rawSize,
                    (unsigned long)compressedSize,
                    (unsigned long)storedSize
                );
            }
            else
            {
                resetDogfoodZlibArena();

                stream.zalloc =
                    dogfoodZAlloc;

                stream.zfree =
                    dogfoodZFree;

                stream.opaque =
                    Z_NULL;

                stream.next_in =
                    (Bytef *)(
                        dogfoodStoredBuffer +
                        GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE
                    );

                stream.avail_in =
                    (uInt)compressedSize;

                stream.next_out =
                    (Bytef *)buffer;

                stream.avail_out =
                    (uInt)rawSize;


                zResult =
                    inflateInit(
                        &stream
                    );


                if (zResult ==
                    Z_OK)
                {
                    zResult =
                        inflate(
                            &stream,
                            Z_FINISH
                        );

                    zEndResult =
                        inflateEnd(
                            &stream
                        );
                }


                if (zResult !=
                        Z_STREAM_END ||
                    zEndResult !=
                        Z_OK ||
                    stream.total_out !=
                        (uLong)rawSize ||
                    stream.total_in !=
                        (uLong)compressedSize)
                {
                    logDogfoodZlibFailure(
                        "INFLATE",
                        zResult
                    );
                }
                else
                {
                    actualCrc =
                        crc32(
                            0L,
                            Z_NULL,
                            0
                        );

                    actualCrc =
                        crc32(
                            actualCrc,
                            (const Bytef *)buffer,
                            (uLong)rawSize
                        );


                    if ((uint32_t)actualCrc !=
                        expectedCrc)
                    {
                        DC_WARN(
                            "DoomCube: CarryHandle save slot 0 "
                            "CRC mismatch expected=%08lx actual=%08lx\n",
                            (unsigned long)expectedCrc,
                            (unsigned long)actualCrc
                        );
                    }
                    else
                    {
                        *actualSize =
                            (size_t)rawSize;

                        success =
                            true;

                        DC_INFO(
                            "DoomCube: CarryHandle save slot 0 "
                            "GET PASS raw=%lu compressed=%lu "
                            "fixed_zlib_peak=%lu\n",
                            (unsigned long)rawSize,
                            (unsigned long)compressedSize,
                            (unsigned long)dogfoodZlibPeak
                        );
                    }
                }


                resetDogfoodZlibArena();
            }
        }
        else
        {
            /*
             * Keep the historical raw-object fallback. It costs no extra
             * memory and lets development cards made before DCF1 framing
             * remain readable.
             */
            if (storedSize == 0u ||
                storedSize >
                    bufferSize ||
                storedSize >
                    GC_CH_DOGFOOD_SAVE_MAX)
            {
                DC_WARN(
                    "DoomCube: CarryHandle save slot 0 "
                    "legacy raw object too large (%lu bytes)\n",
                    (unsigned long)storedSize
                );
            }
            else
            {
                memcpy(
                    buffer,
                    dogfoodStoredBuffer,
                    storedSize
                );

                *actualSize =
                    storedSize;

                success =
                    true;

                DC_INFO(
                    "DoomCube: CarryHandle save slot 0 "
                    "GET PASS legacy raw=%lu\n",
                    (unsigned long)storedSize
                );
            }
        }
    }
    else if (result ==
             CH_PERSIST_RESULT_NOT_FOUND)
    {
        DC_DEBUG(
            "DoomCube: CarryHandle save slot 0 empty\n"
        );
    }
    else
    {
        DC_WARN(
            "DoomCube: CarryHandle save slot 0 "
            "GET failed: %d\n",
            (int)result
        );
    }


    closeOk =
        closeDogfoodSave(
            &save
        );


    if (success &&
        closeOk)
    {
        if (!updateDogfoodSaveCache(
                buffer,
                *actualSize))
        {
            DC_WARN(
                "DoomCube: CarryHandle save slot 0 "
                "cache update failed after GET\n"
            );

            success =
                false;
        }
    }


    return
        success &&
        closeOk;
}

bool GC_CHDogfoodWriteSave(
    int slot,
    const void *data,
    size_t size)
{
    CH_ApplicationSaveSession save =
        {0};

    CH_PersistResult result;


    z_stream stream =
        {0};

    uLong compressedBound;
    uLong compressedSize;
    uLong rawCrc;

    size_t compressedCapacity;
    size_t storedSize;

    int zResult;
    int zEndResult =
        Z_OK;

    bool closeOk;


    if (slot !=
            GC_CH_DOGFOOD_SLOT ||
        !data ||
        size == 0u ||
        size >
            GC_CH_DOGFOOD_SAVE_MAX ||
        !dogfoodIdentityValid)
    {
        return false;
    }




    if (size >
        (size_t)ULONG_MAX)
    {
        DC_WARN(
            "DoomCube: CarryHandle save slot 0 "
            "too large for zlib\n"
        );

        return false;
    }


    compressedCapacity =
        sizeof(dogfoodStoredBuffer) -
        GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE;

    compressedBound =
        compressBound(
            (uLong)size
        );


    if ((size_t)compressedBound >
        compressedCapacity)
    {
        DC_WARN(
            "DoomCube: CarryHandle save fixed compressed buffer "
            "too small: need=%lu have=%lu\n",
            (unsigned long)compressedBound,
            (unsigned long)compressedCapacity
        );

        return false;
    }


    /*
     * Use the fixed zlib allocator. No malloc/calloc/free is reachable from
     * this DoomCube compression operation.
     */
    resetDogfoodZlibArena();

    stream.zalloc =
        dogfoodZAlloc;

    stream.zfree =
        dogfoodZFree;

    stream.opaque =
        Z_NULL;

    stream.next_in =
        (Bytef *)data;

    stream.avail_in =
        (uInt)size;

    stream.next_out =
        (Bytef *)(
            dogfoodStoredBuffer +
            GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE
        );

    stream.avail_out =
        (uInt)compressedCapacity;


    /*
     * memLevel 6 deliberately trades a small amount of compression ratio for
     * substantially lower deterministic MEM1 use. The zlib stream remains
     * ordinary DEFLATE and therefore fully compatible with existing DCF1
     * readers.
     */
    zResult =
        deflateInit2(
            &stream,
            Z_BEST_SPEED,
            Z_DEFLATED,
            MAX_WBITS,
            6,
            Z_DEFAULT_STRATEGY
        );


    if (zResult ==
        Z_OK)
    {
        zResult =
            deflate(
                &stream,
                Z_FINISH
            );

        compressedSize =
            stream.total_out;

        zEndResult =
            deflateEnd(
                &stream
            );
    }
    else
    {
        compressedSize =
            0u;
    }


    if (zResult !=
            Z_STREAM_END ||
        zEndResult !=
            Z_OK ||
        stream.total_in !=
            (uLong)size)
    {
        logDogfoodZlibFailure(
            "DEFLATE",
            zResult
        );

        resetDogfoodZlibArena();

        return false;
    }


    if (compressedSize >
        UINT32_MAX)
    {
        DC_WARN(
            "DoomCube: CarryHandle save slot 0 "
            "compressed payload exceeds framing limits\n"
        );

        resetDogfoodZlibArena();

        return false;
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
            (const Bytef *)data,
            (uLong)size
        );


    writeBe32(
        dogfoodStoredBuffer + 0u,
        GC_CH_DOGFOOD_PAYLOAD_MAGIC
    );

    writeBe32(
        dogfoodStoredBuffer + 4u,
        GC_CH_DOGFOOD_PAYLOAD_VERSION
    );

    writeBe32(
        dogfoodStoredBuffer + 8u,
        (uint32_t)size
    );

    writeBe32(
        dogfoodStoredBuffer + 12u,
        (uint32_t)compressedSize
    );

    writeBe32(
        dogfoodStoredBuffer + 16u,
        (uint32_t)rawCrc
    );


    storedSize =
        GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE +
        (size_t)compressedSize;


    DC_INFO(
        "DoomCube: CarryHandle save slot 0 DEFLATE "
        "raw=%lu compressed=%lu object=%lu fixed_zlib_peak=%lu\n",
        (unsigned long)size,
        (unsigned long)compressedSize,
        (unsigned long)storedSize,
        (unsigned long)dogfoodZlibPeak
    );


    /*
     * zlib no longer needs its arena once deflateEnd() returned.
     * The compressed bytes live separately in dogfoodStoredBuffer.
     */
    resetDogfoodZlibArena();


    if (!openDogfoodSave(
            &save))
    {
        return false;
    }


    result =
        CH_ApplicationSavePut(
            &save,
            dogfoodScope,
            dogfoodScopeSize,
            dogfoodSaveKey,
            sizeof(dogfoodSaveKey),
            dogfoodStoredBuffer,
            storedSize
        );


    if (result ==
        CH_PERSIST_RESULT_OK)
    {
        DC_INFO(
            "DoomCube: CarryHandle save slot 0 PUT PASS "
            "raw=%lu stored=%lu\n",
            (unsigned long)size,
            (unsigned long)storedSize
        );
    }
    else
    {
        DC_WARN(
            "DoomCube: CarryHandle save slot 0 PUT "
            "failed: %d\n",
            (int)result
        );
    }


    closeOk =
        closeDogfoodSave(
            &save
        );


    /*
     * Replace the cache only after CarryHandle reported a definite commit
     * and its owned application-save session closed successfully.
     */
    if (result ==
            CH_PERSIST_RESULT_OK &&
        closeOk)
    {
        if (!updateDogfoodSaveCache(
                data,
                size))
        {
            DC_WARN(
                "DoomCube: CarryHandle save slot 0 "
                "cache update failed after PUT\n"
            );
        }
        else
        {
            DC_DEBUG(
                "DoomCube: CarryHandle save slot 0 "
                "cache updated after PUT (%lu bytes)\n",
                (unsigned long)size
            );
        }
    }


    return
        result ==
            CH_PERSIST_RESULT_OK &&
        closeOk;
}

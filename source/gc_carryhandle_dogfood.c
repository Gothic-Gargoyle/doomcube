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
#define GC_CH_DOGFOOD_FILENAME      "DCHDOG00"
#define GC_CH_DOGFOOD_SECTORS       64u

#define GC_CH_DOGFOOD_PATH_MAX      256u
#define GC_CH_DOGFOOD_SCOPE_MAX     640u


static const unsigned char dogfoodSaveKey[] =
{
    'd', 'o', 'o', 'm', 's', 'a', 'v', '0', '.', 'd', 's', 'g'
};


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
        size
    );


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
 * Existing dogfood objects written before this framing contain the raw Doom
 * save directly. Reads retain that legacy path so DCHDOG00 does not need to
 * be deleted or recreated.
 */

#define GC_CH_DOGFOOD_PAYLOAD_MAGIC       0x44434631u
#define GC_CH_DOGFOOD_PAYLOAD_VERSION     1u
#define GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE 20u


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
            "DoomCube: CarryHandle dogfood launch identity missing IWAD\n"
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
            "DoomCube: CarryHandle dogfood IWAD path too long\n"
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
            "DoomCube: CarryHandle dogfood PWAD path too long\n"
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
            "DoomCube: CarryHandle dogfood scope too long\n"
        );

        return;
    }


    dogfoodScopeSize =
        (size_t)written;

    dogfoodIdentityValid =
        true;


    DC_INFO(
        "DoomCube: CarryHandle dogfood identity: "
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
        "DoomCube: CarryHandle dogfood priming slot 0 cache\n"
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
            "DoomCube: CarryHandle dogfood slot 0 cache not primed\n"
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
        "DoomCube: CarryHandle dogfood slot 0 cache READY "
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
            "DoomCube: CarryHandle dogfood could not remount "
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
     * Remount the legacy CARD backend only.  Keep the existing launch
     * identity in memory.
     */
    DC_DEBUG(
        "DoomCube: CarryHandle dogfood returned CARD A "
        "to legacy backend without identity rebuild\n"
    );


    return true;
}


static bool openDogfoodSave(
    CH_ApplicationSaveSession *save)
{
    CH_ApplicationSaveResult result;


    if (!save ||
        !dogfoodIdentityValid)
    {
        return false;
    }


    /*
     * Transitional dogfood ownership model.
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
            "DoomCube: CarryHandle dogfood Open failed: "
            "result=%d CARD=%ld TX=%d\n",
            (int)result,
            (long)save->card_result,
            (int)save->tx_result
        );


        if (!restoreLegacyCard())
        {
            DC_WARN(
                "DoomCube: CarryHandle dogfood legacy restore "
                "also failed\n"
            );
        }


        return false;
    }


    DC_DEBUG(
        "DoomCube: CarryHandle dogfood %s %s "
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
            "DoomCube: CarryHandle dogfood Close failed: %d\n",
            (int)closeResult
        );
    }


    return
        closeResult ==
            CH_APPLICATION_SAVE_RESULT_OK &&
        restored;
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

    unsigned char *stored =
        NULL;

    size_t storedCapacity;
    size_t storedSize =
        0u;

    bool closeOk;
    bool success =
        false;


    if (actualSize)
    {
        *actualSize =
            0u;
    }


    if (slot !=
            GC_CH_DOGFOOD_SLOT ||
        !buffer ||
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
                "DoomCube: CarryHandle dogfood slot 0 cache "
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
            "DoomCube: CarryHandle dogfood slot 0 "
            "GET CACHE PASS (%lu bytes)\n",
            (unsigned long)dogfoodSaveCacheSize
        );


        return true;
    }


    /*
     * The framed compressed object can be a few bytes larger than the raw
     * payload in the worst case.  Give CH_Get enough room for either the
     * current compressed representation or the old raw representation.
     */
    {
        uLong bound =
            compressBound(
                (uLong)bufferSize
            );

        if ((uint64_t)bound +
                GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE >
            (uint64_t)SIZE_MAX)
        {
            DC_WARN(
                "DoomCube: CarryHandle dogfood GET capacity overflow\n"
            );

            return false;
        }

        storedCapacity =
            (size_t)bound +
            GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE;
    }


    stored =
        malloc(
            storedCapacity
        );

    if (!stored)
    {
        DC_WARN(
            "DoomCube: CarryHandle dogfood GET allocation failed\n"
        );

        return false;
    }


    if (!openDogfoodSave(
            &save))
    {
        free(
            stored
        );

        return false;
    }


    result =
        CH_ApplicationSaveGet(
            &save,
            dogfoodScope,
            dogfoodScopeSize,
            dogfoodSaveKey,
            sizeof(dogfoodSaveKey),
            stored,
            storedCapacity,
            &storedSize
        );


    if (result ==
        CH_PERSIST_RESULT_OK)
    {
        /*
         * New DoomCube framing.
         */
        if (storedSize >=
                GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE &&
            readBe32(
                stored + 0u) ==
                GC_CH_DOGFOOD_PAYLOAD_MAGIC &&
            readBe32(
                stored + 4u) ==
                GC_CH_DOGFOOD_PAYLOAD_VERSION)
        {
            uint32_t rawSize =
                readBe32(
                    stored + 8u
                );

            uint32_t compressedSize =
                readBe32(
                    stored + 12u
                );

            uint32_t expectedCrc =
                readBe32(
                    stored + 16u
                );

            uLongf outputSize =
                (uLongf)bufferSize;

            int zResult;


            if ((size_t)rawSize >
                    bufferSize ||
                (size_t)compressedSize !=
                    storedSize -
                    GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE)
            {
                DC_WARN(
                    "DoomCube: CarryHandle dogfood compressed "
                    "slot 0 header invalid: raw=%lu compressed=%lu "
                    "stored=%lu capacity=%lu\n",
                    (unsigned long)rawSize,
                    (unsigned long)compressedSize,
                    (unsigned long)storedSize,
                    (unsigned long)bufferSize
                );
            }
            else
            {
                zResult =
                    uncompress(
                        (Bytef *)buffer,
                        &outputSize,
                        (const Bytef *)(
                            stored +
                            GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE
                        ),
                        (uLong)compressedSize
                    );


                if (zResult !=
                    Z_OK)
                {
                    DC_WARN(
                        "DoomCube: CarryHandle dogfood slot 0 "
                        "inflate failed: %d\n",
                        zResult
                    );
                }
                else if (outputSize !=
                         (uLongf)rawSize)
                {
                    DC_WARN(
                        "DoomCube: CarryHandle dogfood slot 0 "
                        "inflate size mismatch: expected=%lu actual=%lu\n",
                        (unsigned long)rawSize,
                        (unsigned long)outputSize
                    );
                }
                else
                {
                    uLong rawCrc =
                        crc32(
                            0L,
                            Z_NULL,
                            0
                        );

                    rawCrc =
                        crc32(
                            rawCrc,
                            (const Bytef *)buffer,
                            outputSize
                        );


                    if ((uint32_t)rawCrc !=
                        expectedCrc)
                    {
                        DC_WARN(
                            "DoomCube: CarryHandle dogfood slot 0 "
                            "raw CRC mismatch\n"
                        );
                    }
                    else
                    {
                        *actualSize =
                            (size_t)rawSize;

                        success =
                            true;


                        if (!updateDogfoodSaveCache(
                                buffer,
                                (size_t)rawSize))
                        {
                            DC_WARN(
                                "DoomCube: CarryHandle dogfood slot 0 "
                                "cache update failed after GET\n"
                            );
                        }


                        DC_INFO(
                            "DoomCube: CarryHandle dogfood slot 0 "
                            "GET PASS raw=%lu compressed=%lu\n",
                            (unsigned long)rawSize,
                            (unsigned long)compressedSize
                        );
                    }
                }
            }
        }
        else
        {
            /*
             * Compatibility with DOGFOOD 2/3 and LATENCY FIX 1.
             *
             * Those builds stored the raw .dsg directly under the same
             * CarryHandle object key.
             */
            if (storedSize <=
                bufferSize)
            {
                memcpy(
                    buffer,
                    stored,
                    storedSize
                );

                *actualSize =
                    storedSize;

                success =
                    true;


                if (!updateDogfoodSaveCache(
                        buffer,
                        storedSize))
                {
                    DC_WARN(
                        "DoomCube: CarryHandle dogfood slot 0 "
                        "cache update failed after legacy GET\n"
                    );
                }


                DC_INFO(
                    "DoomCube: CarryHandle dogfood slot 0 "
                    "GET PASS legacy-raw=%lu bytes\n",
                    (unsigned long)storedSize
                );
            }
            else
            {
                DC_WARN(
                    "DoomCube: CarryHandle dogfood legacy raw "
                    "slot 0 exceeds caller buffer\n"
                );
            }
        }
    }
    else if (result ==
             CH_PERSIST_RESULT_NOT_FOUND)
    {
        DC_DEBUG(
            "DoomCube: CarryHandle dogfood slot 0 "
            "not present\n"
        );
    }
    else
    {
        DC_WARN(
            "DoomCube: CarryHandle dogfood slot 0 GET "
            "failed: %d\n",
            (int)result
        );
    }


    closeOk =
        closeDogfoodSave(
            &save
        );


    free(
        stored
    );


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

    unsigned char *stored =
        NULL;

    uLongf compressedCapacity;
    uLongf compressedSize;

    size_t storedSize;

    uLong rawCrc;

    int zResult;

    bool closeOk;


    if (slot !=
            GC_CH_DOGFOOD_SLOT ||
        !data ||
        size == 0u ||
        !dogfoodIdentityValid)
    {
        return false;
    }


    if (size >
        (size_t)ULONG_MAX)
    {
        DC_WARN(
            "DoomCube: CarryHandle dogfood slot 0 "
            "too large for zlib\n"
        );

        return false;
    }


    compressedCapacity =
        compressBound(
            (uLong)size
        );


    if ((uint64_t)compressedCapacity +
            GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE >
        (uint64_t)SIZE_MAX)
    {
        DC_WARN(
            "DoomCube: CarryHandle dogfood PUT capacity overflow\n"
        );

        return false;
    }


    storedSize =
        GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE +
        (size_t)compressedCapacity;


    stored =
        malloc(
            storedSize
        );

    if (!stored)
    {
        DC_WARN(
            "DoomCube: CarryHandle dogfood PUT allocation failed\n"
        );

        return false;
    }


    compressedSize =
        compressedCapacity;


    /*
     * Doom save data has historically compressed to a small fraction of
     * its raw size. Z_BEST_SPEED keeps the PowerPC-side pause small while
     * still avoiding several CARD sectors per transaction.
     */
    zResult =
        compress2(
            (Bytef *)(
                stored +
                GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE
            ),
            &compressedSize,
            (const Bytef *)data,
            (uLong)size,
            Z_BEST_SPEED
        );


    if (zResult !=
        Z_OK)
    {
        DC_WARN(
            "DoomCube: CarryHandle dogfood slot 0 "
            "deflate failed: %d\n",
            zResult
        );

        free(
            stored
        );

        return false;
    }


    if (size >
            UINT32_MAX ||
        compressedSize >
            UINT32_MAX)
    {
        DC_WARN(
            "DoomCube: CarryHandle dogfood slot 0 "
            "payload exceeds framing limits\n"
        );

        free(
            stored
        );

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
        stored + 0u,
        GC_CH_DOGFOOD_PAYLOAD_MAGIC
    );

    writeBe32(
        stored + 4u,
        GC_CH_DOGFOOD_PAYLOAD_VERSION
    );

    writeBe32(
        stored + 8u,
        (uint32_t)size
    );

    writeBe32(
        stored + 12u,
        (uint32_t)compressedSize
    );

    writeBe32(
        stored + 16u,
        (uint32_t)rawCrc
    );


    storedSize =
        GC_CH_DOGFOOD_PAYLOAD_HEADER_SIZE +
        (size_t)compressedSize;


    DC_INFO(
        "DoomCube: CarryHandle dogfood slot 0 DEFLATE "
        "raw=%lu compressed=%lu object=%lu\n",
        (unsigned long)size,
        (unsigned long)compressedSize,
        (unsigned long)storedSize
    );


    if (!openDogfoodSave(
            &save))
    {
        free(
            stored
        );

        return false;
    }


    result =
        CH_ApplicationSavePut(
            &save,
            dogfoodScope,
            dogfoodScopeSize,
            dogfoodSaveKey,
            sizeof(dogfoodSaveKey),
            stored,
            storedSize
        );


    if (result ==
        CH_PERSIST_RESULT_OK)
    {
        DC_INFO(
            "DoomCube: CarryHandle dogfood slot 0 PUT PASS "
            "raw=%lu stored=%lu\n",
            (unsigned long)size,
            (unsigned long)storedSize
        );
    }
    else
    {
        DC_WARN(
            "DoomCube: CarryHandle dogfood slot 0 PUT "
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
     *
     * On any failed/uncertain write, retain the previously verified cache.
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
                "DoomCube: CarryHandle dogfood slot 0 "
                "cache update failed after PUT\n"
            );
        }
        else
        {
            DC_DEBUG(
                "DoomCube: CarryHandle dogfood slot 0 "
                "cache updated after PUT (%lu bytes)\n",
                (unsigned long)size
            );
        }
    }


    free(
        stored
    );


    return
        result ==
            CH_PERSIST_RESULT_OK &&
        closeOk;
}

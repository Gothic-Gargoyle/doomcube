#include "gc_save_v3_card.h"

#include <stdint.h>
#include <string.h>

#include <zlib.h>


static bool validSuperblockSector(
    uint32_t sector)
{
    return
        sector == GC_SAVE_V3_SUPERBLOCK_A_SECTOR ||
        sector == GC_SAVE_V3_SUPERBLOCK_B_SECTOR;
}


static bool superblockGeometryMatches(
    const gc_save_v3_superblock_t *superblock,
    uint32_t sector_size,
    uint32_t container_sectors)
{
    if (!superblock)
    {
        return false;
    }

    return
        superblock->sector_size == sector_size &&
        superblock->container_sectors == container_sectors &&
        superblock->log_start_sector ==
            GC_SAVE_V3_DATA_START_SECTOR &&
        superblock->log_end_sector >=
            superblock->log_start_sector &&
        superblock->log_end_sector <=
            container_sectors;
}


static bool superblocksEquivalent(
    const gc_save_v3_superblock_t *a,
    const gc_save_v3_superblock_t *b)
{
    if (!a || !b)
    {
        return false;
    }

    return
        a->generation == b->generation &&
        a->sector_size == b->sector_size &&
        a->container_sectors == b->container_sectors &&
        a->log_start_sector == b->log_start_sector &&
        a->log_end_sector == b->log_end_sector &&
        a->flags == b->flags;
}


/*
 * Standard serial-number comparison.
 *
 * If candidate is ahead of baseline by less than half the uint32_t
 * range, candidate is newer. This permits generation wraparound.
 */
static bool generationIsNewer(
    uint32_t candidate,
    uint32_t baseline)
{
    uint32_t distance;

    if (candidate == baseline)
    {
        return false;
    }

    distance =
        candidate - baseline;

    return
        distance < 0x80000000u;
}


bool GC_SaveV3CardReadSuperblock(
    card_file *file,
    void *scratch,
    size_t scratch_size,
    uint32_t sector_size,
    uint32_t container_sectors,
    uint32_t superblock_sector,
    gc_save_v3_superblock_t *superblock)
{
    s32 result;

    if (!file ||
        !scratch ||
        !superblock ||
        sector_size == 0 ||
        scratch_size < sector_size ||
        !validSuperblockSector(superblock_sector))
    {
        return false;
    }

    memset(
        scratch,
        0,
        sector_size
    );

    result =
        CARD_Read(
            file,
            scratch,
            sector_size,
            superblock_sector *
                sector_size
        );

    if (result != CARD_ERROR_READY)
    {
        return false;
    }

    if (!GC_SaveV3DecodeSuperblock(
            superblock,
            scratch,
            sector_size))
    {
        return false;
    }

    if (!superblockGeometryMatches(
            superblock,
            sector_size,
            container_sectors))
    {
        memset(
            superblock,
            0,
            sizeof(*superblock)
        );

        return false;
    }

    return true;
}


bool GC_SaveV3CardWriteSuperblock(
    card_file *file,
    void *scratch,
    size_t scratch_size,
    uint32_t sector_size,
    uint32_t superblock_sector,
    const gc_save_v3_superblock_t *superblock)
{
    s32 result;

    if (!file ||
        !scratch ||
        !superblock ||
        sector_size == 0 ||
        scratch_size < sector_size ||
        !validSuperblockSector(superblock_sector) ||
        !superblockGeometryMatches(
            superblock,
            sector_size,
            superblock->container_sectors))
    {
        return false;
    }

    memset(
        scratch,
        0,
        sector_size
    );

    if (!GC_SaveV3EncodeSuperblock(
            scratch,
            sector_size,
            superblock))
    {
        return false;
    }

    result =
        CARD_Write(
            file,
            scratch,
            sector_size,
            superblock_sector *
                sector_size
        );

    return
        result == CARD_ERROR_READY;
}


static bool sectorOffset(
    uint32_t sector,
    uint32_t sector_size,
    u32 *offset)
{
    uint64_t value;

    if (!offset ||
        sector_size == 0)
    {
        return false;
    }

    value =
        (uint64_t)sector *
        (uint64_t)sector_size;

    if (value > 0xffffffffULL)
    {
        return false;
    }

    *offset =
        (u32)value;

    return true;
}


static bool scratchValid(
    const void *scratch,
    size_t scratch_size,
    uint32_t sector_size)
{
    if (!scratch ||
        sector_size == 0 ||
        scratch_size < sector_size)
    {
        return false;
    }

    /*
     * libogc CARD buffers must be 32-byte aligned.
     */
    return
        (((uintptr_t)scratch) & 31u) == 0;
}


static bool recordHeadersEquivalent(
    const gc_save_v3_record_header_t *a,
    const gc_save_v3_record_header_t *b)
{
    if (!a ||
        !b)
    {
        return false;
    }

    return
        a->record_type == b->record_type &&
        a->generation == b->generation &&
        a->slot == b->slot &&
        a->timestamp == b->timestamp &&
        a->raw_size == b->raw_size &&
        a->compressed_size == b->compressed_size &&
        a->raw_crc32 == b->raw_crc32 &&
        a->compressed_crc32 == b->compressed_crc32 &&
        a->record_sectors == b->record_sectors &&
        a->flags == b->flags &&
        GC_SaveV3LaunchIdentityEqual(
            &a->identity,
            &b->identity) &&
        strncmp(
            a->identity.iwad.name,
            b->identity.iwad.name,
            GC_SAVE_V3_NAME_MAX) == 0 &&
        strncmp(
            a->identity.pwad.name,
            b->identity.pwad.name,
            GC_SAVE_V3_NAME_MAX) == 0;
}


bool GC_SaveV3CardReadRecord(
    card_file *file,
    void *scratch,
    size_t scratch_size,
    uint32_t sector_size,
    uint32_t container_sectors,
    uint32_t read_limit_sector,
    uint32_t record_sector,
    gc_save_v3_record_header_t *record,
    void *compressed_output,
    size_t compressed_capacity,
    size_t *compressed_size)
{
    unsigned char *sectorBuffer;
    unsigned char *output;

    uint32_t expectedSectors;
    uint32_t sectorIndex;

    size_t remaining;
    size_t copied;
    size_t dataOffset;
    size_t chunk;

    uLong crc;

    u32 cardOffset;

    s32 result;

    if (compressed_size)
    {
        *compressed_size = 0;
    }

    if (!file ||
        !record ||
        !scratchValid(
            scratch,
            scratch_size,
            sector_size) ||
        sector_size <
            GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE ||
        container_sectors <
            GC_SAVE_V3_DATA_START_SECTOR ||
        read_limit_sector >
            container_sectors ||
        record_sector <
            GC_SAVE_V3_DATA_START_SECTOR ||
        record_sector >=
            read_limit_sector)
    {
        return false;
    }

    memset(
        record,
        0,
        sizeof(*record)
    );

    sectorBuffer =
        scratch;

    output =
        compressed_output;

    if (!sectorOffset(
            record_sector,
            sector_size,
            &cardOffset))
    {
        return false;
    }

    memset(
        sectorBuffer,
        0,
        sector_size
    );

    result =
        CARD_Read(
            file,
            sectorBuffer,
            sector_size,
            cardOffset
        );

    if (result != CARD_ERROR_READY)
    {
        return false;
    }

    if (!GC_SaveV3DecodeRecordHeader(
            record,
            sectorBuffer,
            sector_size))
    {
        return false;
    }

    expectedSectors =
        GC_SaveV3RecordSectorCount(
            record->header_size,
            record->compressed_size,
            sector_size
        );

    if (expectedSectors == 0 ||
        record->record_sectors !=
            expectedSectors ||
        record->record_sectors >
            read_limit_sector -
            record_sector)
    {
        memset(
            record,
            0,
            sizeof(*record)
        );

        return false;
    }

    if (output &&
        compressed_capacity <
            record->compressed_size)
    {
        memset(
            record,
            0,
            sizeof(*record)
        );

        return false;
    }

    remaining =
        record->compressed_size;

    copied =
        0;

    crc =
        crc32(
            0L,
            Z_NULL,
            0
        );

    for (sectorIndex = 0;
         sectorIndex <
            record->record_sectors;
         ++sectorIndex)
    {
        if (sectorIndex != 0)
        {
            if (!sectorOffset(
                    record_sector +
                        sectorIndex,
                    sector_size,
                    &cardOffset))
            {
                goto failure;
            }

            memset(
                sectorBuffer,
                0,
                sector_size
            );

            result =
                CARD_Read(
                    file,
                    sectorBuffer,
                    sector_size,
                    cardOffset
                );

            if (result !=
                CARD_ERROR_READY)
            {
                goto failure;
            }
        }

        dataOffset =
            sectorIndex == 0
                ? record->header_size
                : 0;

        if (dataOffset >
            sector_size)
        {
            goto failure;
        }

        chunk =
            sector_size -
            dataOffset;

        if (chunk >
            remaining)
        {
            chunk =
                remaining;
        }

        if (chunk > 0)
        {
            crc =
                crc32(
                    crc,
                    sectorBuffer +
                        dataOffset,
                    (uInt)chunk
                );

            if (output)
            {
                memcpy(
                    output + copied,
                    sectorBuffer +
                        dataOffset,
                    chunk
                );
            }

            copied +=
                chunk;

            remaining -=
                chunk;
        }
    }

    if (remaining != 0 ||
        copied !=
            record->compressed_size ||
        (uint32_t)crc !=
            record->compressed_crc32)
    {
        goto failure;
    }

    if (compressed_size)
    {
        *compressed_size =
            copied;
    }

    return true;


failure:

    memset(
        record,
        0,
        sizeof(*record)
    );

    if (compressed_size)
    {
        *compressed_size = 0;
    }

    return false;
}


bool GC_SaveV3CardAppendRecord(
    card_file *file,
    void *scratch,
    size_t scratch_size,
    uint32_t sector_size,
    uint32_t container_sectors,
    const gc_save_v3_record_header_t *record_template,
    const void *compressed_payload,
    size_t compressed_size,
    gc_save_v3_record_header_t *committed_record,
    gc_save_v3_superblock_t *committed_superblock,
    uint32_t *committed_superblock_sector,
    uint32_t *record_sector_out)
{
    gc_save_v3_superblock_t active;
    gc_save_v3_superblock_t committed;

    gc_save_v3_record_header_t record;
    gc_save_v3_record_header_t verified;

    unsigned char *sectorBuffer;
    const unsigned char *payload;

    uint32_t activeSector;
    uint32_t targetSector;

    uint32_t recordSector;
    uint32_t recordEnd;

    uint32_t sectorIndex;

    size_t remaining;
    size_t consumed;
    size_t dataOffset;
    size_t chunk;

    uLong crc;

    u32 cardOffset;

    s32 result;

    if (!file ||
        !record_template ||
        !compressed_payload ||
        compressed_size == 0 ||
        compressed_size >
            0xffffffffu ||
        !scratchValid(
            scratch,
            scratch_size,
            sector_size) ||
        sector_size <
            GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE ||
        container_sectors <
            GC_SAVE_V3_DATA_START_SECTOR)
    {
        return false;
    }

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            file,
            scratch,
            scratch_size,
            sector_size,
            container_sectors,
            &active,
            &activeSector))
    {
        return false;
    }

    record =
        *record_template;

    record.generation =
        active.generation +
        1u;

    record.compressed_size =
        (uint32_t)compressed_size;

    crc =
        crc32(
            0L,
            Z_NULL,
            0
        );

    crc =
        crc32(
            crc,
            compressed_payload,
            (uInt)compressed_size
        );

    record.compressed_crc32 =
        (uint32_t)crc;

    record.record_sectors =
        GC_SaveV3RecordSectorCount(
            GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE,
            compressed_size,
            sector_size
        );

    if (record.record_sectors == 0)
    {
        return false;
    }

    recordSector =
        active.log_end_sector;

    if (recordSector >
            container_sectors ||
        record.record_sectors >
            container_sectors -
            recordSector)
    {
        return false;
    }

    recordEnd =
        recordSector +
        record.record_sectors;

    sectorBuffer =
        scratch;

    payload =
        compressed_payload;

    remaining =
        compressed_size;

    consumed =
        0;


    /*
     * Phase 1: append the complete record while the old superblock
     * remains authoritative.
     */
    for (sectorIndex = 0;
         sectorIndex <
            record.record_sectors;
         ++sectorIndex)
    {
        memset(
            sectorBuffer,
            0,
            sector_size
        );

        if (sectorIndex == 0)
        {
            if (!GC_SaveV3EncodeRecordHeader(
                    sectorBuffer,
                    sector_size,
                    &record))
            {
                return false;
            }

            dataOffset =
                GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE;
        }
        else
        {
            dataOffset =
                0;
        }

        chunk =
            sector_size -
            dataOffset;

        if (chunk >
            remaining)
        {
            chunk =
                remaining;
        }

        if (chunk > 0)
        {
            memcpy(
                sectorBuffer +
                    dataOffset,
                payload +
                    consumed,
                chunk
            );

            consumed +=
                chunk;

            remaining -=
                chunk;
        }

        if (!sectorOffset(
                recordSector +
                    sectorIndex,
                sector_size,
                &cardOffset))
        {
            return false;
        }

        result =
            CARD_Write(
                file,
                sectorBuffer,
                sector_size,
                cardOffset
            );

        if (result !=
            CARD_ERROR_READY)
        {
            return false;
        }
    }

    if (remaining != 0 ||
        consumed != compressed_size)
    {
        return false;
    }


    /*
     * Read the complete record back before changing the authoritative
     * superblock. A failed verification leaves it physically present
     * but logically invisible.
     */
    if (!GC_SaveV3CardReadRecord(
            file,
            scratch,
            scratch_size,
            sector_size,
            container_sectors,
            recordEnd,
            recordSector,
            &verified,
            NULL,
            0,
            NULL))
    {
        return false;
    }

    if (!recordHeadersEquivalent(
            &record,
            &verified))
    {
        return false;
    }


    /*
     * Phase 2: publish the new record by replacing the inactive
     * superblock.
     */
    committed =
        active;

    committed.generation =
        record.generation;

    committed.log_end_sector =
        recordEnd;

    targetSector =
        activeSector ==
            GC_SAVE_V3_SUPERBLOCK_A_SECTOR
        ? GC_SAVE_V3_SUPERBLOCK_B_SECTOR
        : GC_SAVE_V3_SUPERBLOCK_A_SECTOR;

    if (!GC_SaveV3CardWriteSuperblock(
            file,
            scratch,
            scratch_size,
            sector_size,
            targetSector,
            &committed))
    {
        return false;
    }

    if (committed_record)
    {
        *committed_record =
            verified;
    }

    if (committed_superblock)
    {
        *committed_superblock =
            committed;
    }

    if (committed_superblock_sector)
    {
        *committed_superblock_sector =
            targetSector;
    }

    if (record_sector_out)
    {
        *record_sector_out =
            recordSector;
    }

    return true;
}


bool GC_SaveV3CardReadAuthoritativeSuperblock(
    card_file *file,
    void *scratch,
    size_t scratch_size,
    uint32_t sector_size,
    uint32_t container_sectors,
    gc_save_v3_superblock_t *superblock,
    uint32_t *superblock_sector)
{
    gc_save_v3_superblock_t a;
    gc_save_v3_superblock_t b;

    bool aValid;
    bool bValid;

    if (!file ||
        !scratch ||
        !superblock)
    {
        return false;
    }

    memset(
        &a,
        0,
        sizeof(a)
    );

    memset(
        &b,
        0,
        sizeof(b)
    );

    aValid =
        GC_SaveV3CardReadSuperblock(
            file,
            scratch,
            scratch_size,
            sector_size,
            container_sectors,
            GC_SAVE_V3_SUPERBLOCK_A_SECTOR,
            &a
        );

    bValid =
        GC_SaveV3CardReadSuperblock(
            file,
            scratch,
            scratch_size,
            sector_size,
            container_sectors,
            GC_SAVE_V3_SUPERBLOCK_B_SECTOR,
            &b
        );

    if (!aValid &&
        !bValid)
    {
        return false;
    }

    if (aValid &&
        !bValid)
    {
        *superblock =
            a;

        if (superblock_sector)
        {
            *superblock_sector =
                GC_SAVE_V3_SUPERBLOCK_A_SECTOR;
        }

        return true;
    }

    if (!aValid &&
        bValid)
    {
        *superblock =
            b;

        if (superblock_sector)
        {
            *superblock_sector =
                GC_SAVE_V3_SUPERBLOCK_B_SECTOR;
        }

        return true;
    }

    /*
     * Both valid.
     */
    if (a.generation ==
        b.generation)
    {
        if (!superblocksEquivalent(
                &a,
                &b))
        {
            /*
             * Two different states claiming the same generation are
             * ambiguous. Do not guess which one is authoritative.
             */
            return false;
        }

        *superblock =
            a;

        if (superblock_sector)
        {
            *superblock_sector =
                GC_SAVE_V3_SUPERBLOCK_A_SECTOR;
        }

        return true;
    }

    if (generationIsNewer(
            b.generation,
            a.generation))
    {
        *superblock =
            b;

        if (superblock_sector)
        {
            *superblock_sector =
                GC_SAVE_V3_SUPERBLOCK_B_SECTOR;
        }
    }
    else
    {
        *superblock =
            a;

        if (superblock_sector)
        {
            *superblock_sector =
                GC_SAVE_V3_SUPERBLOCK_A_SECTOR;
        }
    }

    return true;
}


/* ------------------------------------------------------------------------- */
/* Committed save index                                                      */
/* ------------------------------------------------------------------------- */

bool GC_SaveV3CardBuildSaveIndex(
    card_file *file,
    void *scratch,
    size_t scratch_size,
    uint32_t sector_size,
    uint32_t container_sectors,
    const gc_save_v3_launch_identity_t *identity,
    gc_save_v3_slot_index_t slots[GC_SAVE_V3_SLOT_COUNT],
    gc_save_v3_superblock_t *authoritative_superblock,
    uint32_t *authoritative_superblock_sector)
{
    gc_save_v3_superblock_t active;
    gc_save_v3_record_header_t record;

    uint32_t activeSector;
    uint32_t cursor;
    uint32_t nextSector;
    uint32_t slot;

    if (!file ||
        !identity ||
        !slots ||
        !scratchValid(
            scratch,
            scratch_size,
            sector_size))
    {
        return false;
    }

    memset(
        slots,
        0,
        sizeof(*slots) *
            GC_SAVE_V3_SLOT_COUNT
    );

    if (!GC_SaveV3CardReadAuthoritativeSuperblock(
            file,
            scratch,
            scratch_size,
            sector_size,
            container_sectors,
            &active,
            &activeSector))
    {
        return false;
    }

    cursor =
        active.log_start_sector;

    while (cursor <
        active.log_end_sector)
    {
        memset(
            &record,
            0,
            sizeof(record)
        );

        /*
         * ReadRecord validates the header, geometry and compressed CRC.
         *
         * No payload buffer is required while constructing the index.
         */
        if (!GC_SaveV3CardReadRecord(
                file,
                scratch,
                scratch_size,
                sector_size,
                container_sectors,
                active.log_end_sector,
                cursor,
                &record,
                NULL,
                0,
                NULL))
        {
            return false;
        }

        if (record.record_sectors == 0 ||
            record.record_sectors >
                active.log_end_sector -
                cursor)
        {
            return false;
        }

        nextSector =
            cursor +
            record.record_sectors;

        if (record.record_type ==
                GC_SAVE_V3_RECORD_SAVE &&
            GC_SaveV3LaunchIdentityEqual(
                &record.identity,
                identity))
        {
            slot =
                record.slot;

            if (slot >=
                GC_SAVE_V3_SLOT_COUNT)
            {
                return false;
            }

            if (!slots[slot].present ||
                generationIsNewer(
                    record.generation,
                    slots[slot].record.generation))
            {
                slots[slot].present =
                    true;

                slots[slot].record_sector =
                    cursor;

                slots[slot].record =
                    record;
            }
            else if (
                record.generation ==
                    slots[slot].record.generation &&
                cursor !=
                    slots[slot].record_sector)
            {
                /*
                 * Two different physical records claiming the same
                 * identity + slot + generation are ambiguous.
                 */
                return false;
            }
        }

        cursor =
            nextSector;
    }

    if (cursor !=
        active.log_end_sector)
    {
        return false;
    }

    if (authoritative_superblock)
    {
        *authoritative_superblock =
            active;
    }

    if (authoritative_superblock_sector)
    {
        *authoritative_superblock_sector =
            activeSector;
    }

    return true;
}

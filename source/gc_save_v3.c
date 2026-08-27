#include "gc_save_v3.h"

#include <stdio.h>
#include <string.h>

#include <zlib.h>


/* ------------------------------------------------------------------------- */
/* Big-endian persistent integers                                            */
/* ------------------------------------------------------------------------- */

static void putU32(
    uint8_t *buffer,
    size_t offset,
    uint32_t value)
{
    buffer[offset + 0] =
        (uint8_t)((value >> 24) & 0xffu);

    buffer[offset + 1] =
        (uint8_t)((value >> 16) & 0xffu);

    buffer[offset + 2] =
        (uint8_t)((value >> 8) & 0xffu);

    buffer[offset + 3] =
        (uint8_t)(value & 0xffu);
}


static uint32_t getU32(
    const uint8_t *buffer,
    size_t offset)
{
    return
        ((uint32_t)buffer[offset + 0] << 24)
        | ((uint32_t)buffer[offset + 1] << 16)
        | ((uint32_t)buffer[offset + 2] << 8)
        | (uint32_t)buffer[offset + 3];
}


static void putFixedString(
    uint8_t *buffer,
    size_t offset,
    size_t field_size,
    const char *value)
{
    size_t i;

    memset(
        buffer + offset,
        0,
        field_size
    );

    if (!value ||
        field_size == 0)
    {
        return;
    }

    for (i = 0;
         i + 1 < field_size &&
         value[i] != '\0';
         ++i)
    {
        buffer[offset + i] =
            (uint8_t)value[i];
    }
}


static void getFixedString(
    char *output,
    size_t output_size,
    const uint8_t *buffer,
    size_t offset,
    size_t field_size)
{
    size_t copy_size;

    if (!output ||
        output_size == 0)
    {
        return;
    }

    copy_size =
        field_size;

    if (copy_size >= output_size)
    {
        copy_size =
            output_size - 1;
    }

    memcpy(
        output,
        buffer + offset,
        copy_size
    );

    output[copy_size] =
        '\0';
}


static uint32_t encodedCrc32(
    const uint8_t *buffer,
    size_t size)
{
    uLong crc;

    crc =
        crc32(
            0L,
            Z_NULL,
            0
        );

    crc =
        crc32(
            crc,
            buffer,
            (uInt)size
        );

    return (uint32_t)crc;
}


/* ------------------------------------------------------------------------- */
/* Container header                                                          */
/* ------------------------------------------------------------------------- */

enum
{
    CONTAINER_MAGIC_OFFSET = 0,
    CONTAINER_VERSION_OFFSET = 4,
    CONTAINER_HEADER_SIZE_OFFSET = 8,
    CONTAINER_SECTOR_SIZE_OFFSET = 12,
    CONTAINER_SECTORS_OFFSET = 16,
    CONTAINER_FILE_INDEX_OFFSET = 20,
    CONTAINER_FLAGS_OFFSET = 24,
    CONTAINER_CRC_OFFSET = 28
};


bool GC_SaveV3EncodeContainerHeader(
    uint8_t *buffer,
    size_t buffer_size,
    const gc_save_v3_container_header_t *header)
{
    uint32_t crc;

    if (!buffer ||
        !header ||
        buffer_size < GC_SAVE_V3_CONTAINER_HEADER_ENCODED_SIZE)
    {
        return false;
    }

    if (header->sector_size == 0 ||
        header->container_sectors < GC_SAVE_V3_DATA_START_SECTOR ||
        header->file_index > 1u)
    {
        return false;
    }

    memset(
        buffer,
        0,
        GC_SAVE_V3_CONTAINER_HEADER_ENCODED_SIZE
    );

    putU32(
        buffer,
        CONTAINER_MAGIC_OFFSET,
        GC_SAVE_V3_CONTAINER_MAGIC
    );

    putU32(
        buffer,
        CONTAINER_VERSION_OFFSET,
        GC_SAVE_V3_VERSION
    );

    putU32(
        buffer,
        CONTAINER_HEADER_SIZE_OFFSET,
        GC_SAVE_V3_CONTAINER_HEADER_ENCODED_SIZE
    );

    putU32(
        buffer,
        CONTAINER_SECTOR_SIZE_OFFSET,
        header->sector_size
    );

    putU32(
        buffer,
        CONTAINER_SECTORS_OFFSET,
        header->container_sectors
    );

    putU32(
        buffer,
        CONTAINER_FILE_INDEX_OFFSET,
        header->file_index
    );

    putU32(
        buffer,
        CONTAINER_FLAGS_OFFSET,
        header->flags
    );

    /*
     * CRC field remains zero while calculating the checksum.
     */
    crc =
        encodedCrc32(
            buffer,
            GC_SAVE_V3_CONTAINER_HEADER_ENCODED_SIZE
        );

    putU32(
        buffer,
        CONTAINER_CRC_OFFSET,
        crc
    );

    return true;
}


bool GC_SaveV3DecodeContainerHeader(
    gc_save_v3_container_header_t *header,
    const uint8_t *buffer,
    size_t buffer_size)
{
    uint8_t checked[GC_SAVE_V3_CONTAINER_HEADER_ENCODED_SIZE];
    uint32_t stored_crc;
    uint32_t calculated_crc;

    if (!header ||
        !buffer ||
        buffer_size < GC_SAVE_V3_CONTAINER_HEADER_ENCODED_SIZE)
    {
        return false;
    }

    memcpy(
        checked,
        buffer,
        sizeof(checked)
    );

    stored_crc =
        getU32(
            checked,
            CONTAINER_CRC_OFFSET
        );

    putU32(
        checked,
        CONTAINER_CRC_OFFSET,
        0
    );

    calculated_crc =
        encodedCrc32(
            checked,
            sizeof(checked)
        );

    if (stored_crc != calculated_crc)
    {
        return false;
    }

    memset(
        header,
        0,
        sizeof(*header)
    );

    header->magic =
        getU32(
            buffer,
            CONTAINER_MAGIC_OFFSET
        );

    header->version =
        getU32(
            buffer,
            CONTAINER_VERSION_OFFSET
        );

    header->header_size =
        getU32(
            buffer,
            CONTAINER_HEADER_SIZE_OFFSET
        );

    header->sector_size =
        getU32(
            buffer,
            CONTAINER_SECTOR_SIZE_OFFSET
        );

    header->container_sectors =
        getU32(
            buffer,
            CONTAINER_SECTORS_OFFSET
        );

    header->file_index =
        getU32(
            buffer,
            CONTAINER_FILE_INDEX_OFFSET
        );

    header->flags =
        getU32(
            buffer,
            CONTAINER_FLAGS_OFFSET
        );

    header->header_crc32 =
        stored_crc;

    if (header->magic != GC_SAVE_V3_CONTAINER_MAGIC ||
        header->version != GC_SAVE_V3_VERSION ||
        header->header_size !=
            GC_SAVE_V3_CONTAINER_HEADER_ENCODED_SIZE ||
        header->sector_size == 0 ||
        header->container_sectors <
            GC_SAVE_V3_DATA_START_SECTOR ||
        header->file_index > 1u)
    {
        memset(
            header,
            0,
            sizeof(*header)
        );

        return false;
    }

    return true;
}


/* ------------------------------------------------------------------------- */
/* Superblock                                                                */
/* ------------------------------------------------------------------------- */

enum
{
    SUPER_MAGIC_OFFSET = 0,
    SUPER_VERSION_OFFSET = 4,
    SUPER_GENERATION_OFFSET = 8,
    SUPER_SECTOR_SIZE_OFFSET = 12,
    SUPER_CONTAINER_SECTORS_OFFSET = 16,
    SUPER_LOG_START_OFFSET = 20,
    SUPER_LOG_END_OFFSET = 24,
    SUPER_FLAGS_OFFSET = 28,
    SUPER_CRC_OFFSET = 32
};


bool GC_SaveV3EncodeSuperblock(
    uint8_t *buffer,
    size_t buffer_size,
    const gc_save_v3_superblock_t *superblock)
{
    uint32_t crc;

    if (!buffer ||
        !superblock ||
        buffer_size < GC_SAVE_V3_SUPERBLOCK_ENCODED_SIZE)
    {
        return false;
    }

    if (superblock->sector_size == 0 ||
        superblock->container_sectors <
            GC_SAVE_V3_DATA_START_SECTOR ||
        superblock->log_start_sector !=
            GC_SAVE_V3_DATA_START_SECTOR ||
        superblock->log_end_sector <
            superblock->log_start_sector ||
        superblock->log_end_sector >
            superblock->container_sectors)
    {
        return false;
    }

    memset(
        buffer,
        0,
        GC_SAVE_V3_SUPERBLOCK_ENCODED_SIZE
    );

    putU32(
        buffer,
        SUPER_MAGIC_OFFSET,
        GC_SAVE_V3_SUPERBLOCK_MAGIC
    );

    putU32(
        buffer,
        SUPER_VERSION_OFFSET,
        GC_SAVE_V3_VERSION
    );

    putU32(
        buffer,
        SUPER_GENERATION_OFFSET,
        superblock->generation
    );

    putU32(
        buffer,
        SUPER_SECTOR_SIZE_OFFSET,
        superblock->sector_size
    );

    putU32(
        buffer,
        SUPER_CONTAINER_SECTORS_OFFSET,
        superblock->container_sectors
    );

    putU32(
        buffer,
        SUPER_LOG_START_OFFSET,
        superblock->log_start_sector
    );

    putU32(
        buffer,
        SUPER_LOG_END_OFFSET,
        superblock->log_end_sector
    );

    putU32(
        buffer,
        SUPER_FLAGS_OFFSET,
        superblock->flags
    );

    crc =
        encodedCrc32(
            buffer,
            GC_SAVE_V3_SUPERBLOCK_ENCODED_SIZE
        );

    putU32(
        buffer,
        SUPER_CRC_OFFSET,
        crc
    );

    return true;
}


bool GC_SaveV3DecodeSuperblock(
    gc_save_v3_superblock_t *superblock,
    const uint8_t *buffer,
    size_t buffer_size)
{
    uint8_t checked[GC_SAVE_V3_SUPERBLOCK_ENCODED_SIZE];
    uint32_t stored_crc;
    uint32_t calculated_crc;

    if (!superblock ||
        !buffer ||
        buffer_size < GC_SAVE_V3_SUPERBLOCK_ENCODED_SIZE)
    {
        return false;
    }

    memcpy(
        checked,
        buffer,
        sizeof(checked)
    );

    stored_crc =
        getU32(
            checked,
            SUPER_CRC_OFFSET
        );

    putU32(
        checked,
        SUPER_CRC_OFFSET,
        0
    );

    calculated_crc =
        encodedCrc32(
            checked,
            sizeof(checked)
        );

    if (stored_crc != calculated_crc)
    {
        return false;
    }

    memset(
        superblock,
        0,
        sizeof(*superblock)
    );

    superblock->magic =
        getU32(
            buffer,
            SUPER_MAGIC_OFFSET
        );

    superblock->version =
        getU32(
            buffer,
            SUPER_VERSION_OFFSET
        );

    superblock->generation =
        getU32(
            buffer,
            SUPER_GENERATION_OFFSET
        );

    superblock->sector_size =
        getU32(
            buffer,
            SUPER_SECTOR_SIZE_OFFSET
        );

    superblock->container_sectors =
        getU32(
            buffer,
            SUPER_CONTAINER_SECTORS_OFFSET
        );

    superblock->log_start_sector =
        getU32(
            buffer,
            SUPER_LOG_START_OFFSET
        );

    superblock->log_end_sector =
        getU32(
            buffer,
            SUPER_LOG_END_OFFSET
        );

    superblock->flags =
        getU32(
            buffer,
            SUPER_FLAGS_OFFSET
        );

    superblock->superblock_crc32 =
        stored_crc;

    if (superblock->magic != GC_SAVE_V3_SUPERBLOCK_MAGIC ||
        superblock->version != GC_SAVE_V3_VERSION ||
        superblock->sector_size == 0 ||
        superblock->container_sectors <
            GC_SAVE_V3_DATA_START_SECTOR ||
        superblock->log_start_sector !=
            GC_SAVE_V3_DATA_START_SECTOR ||
        superblock->log_end_sector <
            superblock->log_start_sector ||
        superblock->log_end_sector >
            superblock->container_sectors)
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


/* ------------------------------------------------------------------------- */
/* Record header                                                             */
/* ------------------------------------------------------------------------- */

enum
{
    RECORD_MAGIC_OFFSET = 0,
    RECORD_VERSION_OFFSET = 4,
    RECORD_HEADER_SIZE_OFFSET = 8,
    RECORD_TYPE_OFFSET = 12,
    RECORD_GENERATION_OFFSET = 16,
    RECORD_SLOT_OFFSET = 20,
    RECORD_TIMESTAMP_OFFSET = 24,
    RECORD_RAW_SIZE_OFFSET = 28,
    RECORD_COMPRESSED_SIZE_OFFSET = 32,
    RECORD_RAW_CRC_OFFSET = 36,
    RECORD_COMPRESSED_CRC_OFFSET = 40,
    RECORD_SECTORS_OFFSET = 44,

    RECORD_IWAD_NAME_OFFSET = 48,
    RECORD_IWAD_SIZE_OFFSET = 112,
    RECORD_IWAD_CRC_OFFSET = 116,

    RECORD_PWAD_NAME_OFFSET = 120,
    RECORD_PWAD_SIZE_OFFSET = 184,
    RECORD_PWAD_CRC_OFFSET = 188,

    RECORD_HAS_PWAD_OFFSET = 192,
    RECORD_FLAGS_OFFSET = 196,
    RECORD_HEADER_CRC_OFFSET = 200
};


static bool validRecordHeader(
    const gc_save_v3_record_header_t *record)
{
    if (!record)
    {
        return false;
    }

    if (record->record_type !=
            GC_SAVE_V3_RECORD_SAVE &&
        record->record_type !=
            GC_SAVE_V3_RECORD_CONFIG)
    {
        return false;
    }

    if (record->raw_size == 0 ||
        record->compressed_size == 0 ||
        record->record_sectors == 0)
    {
        return false;
    }

    if (record->record_type ==
        GC_SAVE_V3_RECORD_SAVE)
    {
        if (record->slot >=
            GC_SAVE_V3_SLOT_COUNT)
        {
            return false;
        }

        if (record->identity.iwad.size == 0)
        {
            return false;
        }

        if (record->identity.has_pwad &&
            record->identity.pwad.size == 0)
        {
            return false;
        }
    }
    else
    {
        if (record->slot != 0)
        {
            return false;
        }
    }

    return true;
}


bool GC_SaveV3EncodeRecordHeader(
    uint8_t *buffer,
    size_t buffer_size,
    const gc_save_v3_record_header_t *record)
{
    uint32_t crc;

    if (!buffer ||
        !record ||
        buffer_size <
            GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE ||
        !validRecordHeader(record))
    {
        return false;
    }

    memset(
        buffer,
        0,
        GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE
    );

    putU32(
        buffer,
        RECORD_MAGIC_OFFSET,
        GC_SAVE_V3_RECORD_MAGIC
    );

    putU32(
        buffer,
        RECORD_VERSION_OFFSET,
        GC_SAVE_V3_VERSION
    );

    putU32(
        buffer,
        RECORD_HEADER_SIZE_OFFSET,
        GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE
    );

    putU32(
        buffer,
        RECORD_TYPE_OFFSET,
        record->record_type
    );

    putU32(
        buffer,
        RECORD_GENERATION_OFFSET,
        record->generation
    );

    putU32(
        buffer,
        RECORD_SLOT_OFFSET,
        record->slot
    );

    putU32(
        buffer,
        RECORD_TIMESTAMP_OFFSET,
        record->timestamp
    );

    putU32(
        buffer,
        RECORD_RAW_SIZE_OFFSET,
        record->raw_size
    );

    putU32(
        buffer,
        RECORD_COMPRESSED_SIZE_OFFSET,
        record->compressed_size
    );

    putU32(
        buffer,
        RECORD_RAW_CRC_OFFSET,
        record->raw_crc32
    );

    putU32(
        buffer,
        RECORD_COMPRESSED_CRC_OFFSET,
        record->compressed_crc32
    );

    putU32(
        buffer,
        RECORD_SECTORS_OFFSET,
        record->record_sectors
    );

    if (record->record_type ==
        GC_SAVE_V3_RECORD_SAVE)
    {
        putFixedString(
            buffer,
            RECORD_IWAD_NAME_OFFSET,
            GC_SAVE_V3_NAME_MAX,
            record->identity.iwad.name
        );

        putU32(
            buffer,
            RECORD_IWAD_SIZE_OFFSET,
            record->identity.iwad.size
        );

        putU32(
            buffer,
            RECORD_IWAD_CRC_OFFSET,
            record->identity.iwad.crc32
        );

        if (record->identity.has_pwad)
        {
            putFixedString(
                buffer,
                RECORD_PWAD_NAME_OFFSET,
                GC_SAVE_V3_NAME_MAX,
                record->identity.pwad.name
            );

            putU32(
                buffer,
                RECORD_PWAD_SIZE_OFFSET,
                record->identity.pwad.size
            );

            putU32(
                buffer,
                RECORD_PWAD_CRC_OFFSET,
                record->identity.pwad.crc32
            );

            putU32(
                buffer,
                RECORD_HAS_PWAD_OFFSET,
                1
            );
        }
    }

    putU32(
        buffer,
        RECORD_FLAGS_OFFSET,
        record->flags
    );

    /*
     * CRC field is zero while calculating the encoded-header CRC.
     */
    crc =
        encodedCrc32(
            buffer,
            GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE
        );

    putU32(
        buffer,
        RECORD_HEADER_CRC_OFFSET,
        crc
    );

    return true;
}


bool GC_SaveV3DecodeRecordHeader(
    gc_save_v3_record_header_t *record,
    const uint8_t *buffer,
    size_t buffer_size)
{
    uint8_t checked[
        GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE
    ];

    uint32_t stored_crc;
    uint32_t calculated_crc;
    uint32_t has_pwad;

    if (!record ||
        !buffer ||
        buffer_size <
            GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE)
    {
        return false;
    }

    memcpy(
        checked,
        buffer,
        sizeof(checked)
    );

    stored_crc =
        getU32(
            checked,
            RECORD_HEADER_CRC_OFFSET
        );

    putU32(
        checked,
        RECORD_HEADER_CRC_OFFSET,
        0
    );

    calculated_crc =
        encodedCrc32(
            checked,
            sizeof(checked)
        );

    if (stored_crc != calculated_crc)
    {
        return false;
    }

    if (getU32(
            buffer,
            RECORD_MAGIC_OFFSET) !=
            GC_SAVE_V3_RECORD_MAGIC ||
        getU32(
            buffer,
            RECORD_VERSION_OFFSET) !=
            GC_SAVE_V3_VERSION ||
        getU32(
            buffer,
            RECORD_HEADER_SIZE_OFFSET) !=
            GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE)
    {
        return false;
    }

    has_pwad =
        getU32(
            buffer,
            RECORD_HAS_PWAD_OFFSET
        );

    if (has_pwad > 1)
    {
        return false;
    }

    memset(
        record,
        0,
        sizeof(*record)
    );

    record->magic =
        GC_SAVE_V3_RECORD_MAGIC;

    record->version =
        GC_SAVE_V3_VERSION;

    record->header_size =
        GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE;

    record->record_type =
        getU32(
            buffer,
            RECORD_TYPE_OFFSET
        );

    record->generation =
        getU32(
            buffer,
            RECORD_GENERATION_OFFSET
        );

    record->slot =
        getU32(
            buffer,
            RECORD_SLOT_OFFSET
        );

    record->timestamp =
        getU32(
            buffer,
            RECORD_TIMESTAMP_OFFSET
        );

    record->raw_size =
        getU32(
            buffer,
            RECORD_RAW_SIZE_OFFSET
        );

    record->compressed_size =
        getU32(
            buffer,
            RECORD_COMPRESSED_SIZE_OFFSET
        );

    record->raw_crc32 =
        getU32(
            buffer,
            RECORD_RAW_CRC_OFFSET
        );

    record->compressed_crc32 =
        getU32(
            buffer,
            RECORD_COMPRESSED_CRC_OFFSET
        );

    record->record_sectors =
        getU32(
            buffer,
            RECORD_SECTORS_OFFSET
        );

    getFixedString(
        record->identity.iwad.name,
        sizeof(record->identity.iwad.name),
        buffer,
        RECORD_IWAD_NAME_OFFSET,
        GC_SAVE_V3_NAME_MAX
    );

    record->identity.iwad.size =
        getU32(
            buffer,
            RECORD_IWAD_SIZE_OFFSET
        );

    record->identity.iwad.crc32 =
        getU32(
            buffer,
            RECORD_IWAD_CRC_OFFSET
        );

    getFixedString(
        record->identity.pwad.name,
        sizeof(record->identity.pwad.name),
        buffer,
        RECORD_PWAD_NAME_OFFSET,
        GC_SAVE_V3_NAME_MAX
    );

    record->identity.pwad.size =
        getU32(
            buffer,
            RECORD_PWAD_SIZE_OFFSET
        );

    record->identity.pwad.crc32 =
        getU32(
            buffer,
            RECORD_PWAD_CRC_OFFSET
        );

    record->identity.has_pwad =
        has_pwad != 0;

    record->flags =
        getU32(
            buffer,
            RECORD_FLAGS_OFFSET
        );

    record->header_crc32 =
        stored_crc;

    if (!validRecordHeader(record))
    {
        memset(
            record,
            0,
            sizeof(*record)
        );

        return false;
    }

    return true;
}


/* ------------------------------------------------------------------------- */
/* In-memory regression                                                      */
/* ------------------------------------------------------------------------- */

bool GC_SaveV3CodecSelfTest(void)
{
    uint8_t container_bytes[
        GC_SAVE_V3_CONTAINER_HEADER_ENCODED_SIZE
    ];

    uint8_t super_bytes[
        GC_SAVE_V3_SUPERBLOCK_ENCODED_SIZE
    ];

    gc_save_v3_container_header_t container_in;
    gc_save_v3_container_header_t container_out;

    gc_save_v3_superblock_t super_in;
    gc_save_v3_superblock_t super_out;

    memset(
        &container_in,
        0,
        sizeof(container_in)
    );

    container_in.sector_size =
        8192;

    container_in.container_sectors =
        GC_SAVE_V3_INITIAL_SECTORS;

    container_in.file_index =
        1;

    container_in.flags =
        0x12345678u;

    if (!GC_SaveV3EncodeContainerHeader(
            container_bytes,
            sizeof(container_bytes),
            &container_in))
    {
        return false;
    }

    if (!GC_SaveV3DecodeContainerHeader(
            &container_out,
            container_bytes,
            sizeof(container_bytes)))
    {
        return false;
    }

    if (container_out.magic != GC_SAVE_V3_CONTAINER_MAGIC ||
        container_out.version != GC_SAVE_V3_VERSION ||
        container_out.header_size !=
            GC_SAVE_V3_CONTAINER_HEADER_ENCODED_SIZE ||
        container_out.sector_size != container_in.sector_size ||
        container_out.container_sectors !=
            container_in.container_sectors ||
        container_out.file_index != container_in.file_index ||
        container_out.flags != container_in.flags ||
        container_out.header_crc32 == 0)
    {
        return false;
    }

    /*
     * Corrupt one encoded byte and prove CRC validation rejects it.
     */
    container_bytes[CONTAINER_FLAGS_OFFSET] ^= 0x01u;

    if (GC_SaveV3DecodeContainerHeader(
            &container_out,
            container_bytes,
            sizeof(container_bytes)))
    {
        return false;
    }

    /*
     * Re-encode after intentional corruption.
     */
    if (!GC_SaveV3EncodeContainerHeader(
            container_bytes,
            sizeof(container_bytes),
            &container_in))
    {
        return false;
    }


    memset(
        &super_in,
        0,
        sizeof(super_in)
    );

    super_in.generation =
        42;

    super_in.sector_size =
        8192;

    super_in.container_sectors =
        GC_SAVE_V3_INITIAL_SECTORS;

    super_in.log_start_sector =
        GC_SAVE_V3_DATA_START_SECTOR;

    super_in.log_end_sector =
        GC_SAVE_V3_DATA_START_SECTOR + 5;

    super_in.flags =
        0xa5a55a5au;

    if (!GC_SaveV3EncodeSuperblock(
            super_bytes,
            sizeof(super_bytes),
            &super_in))
    {
        return false;
    }

    if (!GC_SaveV3DecodeSuperblock(
            &super_out,
            super_bytes,
            sizeof(super_bytes)))
    {
        return false;
    }

    if (super_out.magic != GC_SAVE_V3_SUPERBLOCK_MAGIC ||
        super_out.version != GC_SAVE_V3_VERSION ||
        super_out.generation != super_in.generation ||
        super_out.sector_size != super_in.sector_size ||
        super_out.container_sectors !=
            super_in.container_sectors ||
        super_out.log_start_sector !=
            super_in.log_start_sector ||
        super_out.log_end_sector !=
            super_in.log_end_sector ||
        super_out.flags != super_in.flags ||
        super_out.superblock_crc32 == 0)
    {
        return false;
    }

    /*
     * Corrupt the encoded generation and prove rejection again.
     */
    super_bytes[SUPER_GENERATION_OFFSET + 3] ^= 0x01u;

    if (GC_SaveV3DecodeSuperblock(
            &super_out,
            super_bytes,
            sizeof(super_bytes)))
    {
        return false;
    }


    /*
     * Record header round trip.
     */
    {
        uint8_t record_bytes[
            GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE
        ];

        gc_save_v3_record_header_t record_in;
        gc_save_v3_record_header_t record_out;

        memset(
            &record_in,
            0,
            sizeof(record_in)
        );

        record_in.record_type =
            GC_SAVE_V3_RECORD_SAVE;

        record_in.generation =
            7;

        record_in.slot =
            5;

        record_in.timestamp =
            1704067200u;

        record_in.raw_size =
            72638u;

        record_in.compressed_size =
            10007u;

        record_in.raw_crc32 =
            0x11223344u;

        record_in.compressed_crc32 =
            0x55667788u;

        record_in.record_sectors =
            GC_SaveV3RecordSectorCount(
                GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE,
                record_in.compressed_size,
                8192
            );

        snprintf(
            record_in.identity.iwad.name,
            sizeof(record_in.identity.iwad.name),
            "%s",
            "doom.wad"
        );

        record_in.identity.iwad.size =
            12408292u;

        record_in.identity.iwad.crc32 =
            0xbf0eaac0u;

        snprintf(
            record_in.identity.pwad.name,
            sizeof(record_in.identity.pwad.name),
            "%s",
            "SIGIL_V1_23.wad"
        );

        record_in.identity.pwad.size =
            4640555u;

        record_in.identity.pwad.crc32 =
            0xc913116eu;

        record_in.identity.has_pwad =
            true;

        record_in.flags =
            0x13579bdfu;

        if (!GC_SaveV3EncodeRecordHeader(
                record_bytes,
                sizeof(record_bytes),
                &record_in))
        {
            return false;
        }

        if (!GC_SaveV3DecodeRecordHeader(
                &record_out,
                record_bytes,
                sizeof(record_bytes)))
        {
            return false;
        }

        if (record_out.magic !=
                GC_SAVE_V3_RECORD_MAGIC ||
            record_out.version !=
                GC_SAVE_V3_VERSION ||
            record_out.header_size !=
                GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE ||
            record_out.record_type !=
                record_in.record_type ||
            record_out.generation !=
                record_in.generation ||
            record_out.slot !=
                record_in.slot ||
            record_out.timestamp !=
                record_in.timestamp ||
            record_out.raw_size !=
                record_in.raw_size ||
            record_out.compressed_size !=
                record_in.compressed_size ||
            record_out.raw_crc32 !=
                record_in.raw_crc32 ||
            record_out.compressed_crc32 !=
                record_in.compressed_crc32 ||
            record_out.record_sectors !=
                record_in.record_sectors ||
            record_out.flags !=
                record_in.flags ||
            record_out.header_crc32 == 0 ||
            strcmp(
                record_out.identity.iwad.name,
                record_in.identity.iwad.name) != 0 ||
            strcmp(
                record_out.identity.pwad.name,
                record_in.identity.pwad.name) != 0 ||
            !GC_SaveV3LaunchIdentityEqual(
                &record_out.identity,
                &record_in.identity))
        {
            return false;
        }

        /*
         * Corrupt identity data and prove the record CRC rejects it.
         */
        record_bytes[
            RECORD_IWAD_NAME_OFFSET
        ] ^= 0x01u;

        if (GC_SaveV3DecodeRecordHeader(
                &record_out,
                record_bytes,
                sizeof(record_bytes)))
        {
            return false;
        }
    }

    return true;
}

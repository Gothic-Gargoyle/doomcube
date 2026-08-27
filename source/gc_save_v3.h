#ifndef GC_SAVE_V3_H
#define GC_SAVE_V3_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/*
 * DoomCube Memory Card format v3
 *
 * Physical layout:
 *
 *   sector 0    GameCube metadata / v3 file header
 *   sector 1    superblock A
 *   sector 2    superblock B
 *   sector 3+   append-only records
 *
 * The complete CARD file is always sector-sized.
 *
 * Normal save commit:
 *
 *   1. write a complete new record
 *   2. verify it
 *   3. write the inactive superblock with generation + 1
 *
 * The highest valid superblock generation is authoritative.
 *
 * Growth/compaction is performed into the alternate GameCube file.
 */

#define GC_SAVE_V3_VERSION               3u

#define GC_SAVE_V3_SLOT_COUNT            6u

#define GC_SAVE_V3_INITIAL_SECTORS       16u
#define GC_SAVE_V3_NORMAL_TARGET_SECTORS 32u

#define GC_SAVE_V3_METADATA_SECTOR       0u
#define GC_SAVE_V3_SUPERBLOCK_A_SECTOR   1u
#define GC_SAVE_V3_SUPERBLOCK_B_SECTOR   2u
#define GC_SAVE_V3_DATA_START_SECTOR     3u

#define GC_SAVE_V3_NAME_MAX              64u


/*
 * ASCII:
 *
 *   DCV3  container metadata
 *   DCS3  superblock
 *   DCR3  record
 */

#define GC_SAVE_V3_CONTAINER_MAGIC       0x44435633u
#define GC_SAVE_V3_SUPERBLOCK_MAGIC      0x44435333u
#define GC_SAVE_V3_RECORD_MAGIC          0x44435233u


/*
 * Persistent encoded sizes.
 *
 * These are part of the on-card format contract. Never replace these
 * with sizeof(struct): the logical C structures may contain padding.
 */
#define GC_SAVE_V3_CONTAINER_HEADER_ENCODED_SIZE 32u
#define GC_SAVE_V3_SUPERBLOCK_ENCODED_SIZE       36u
#define GC_SAVE_V3_RECORD_HEADER_ENCODED_SIZE    204u


typedef enum
{
    GC_SAVE_V3_RECORD_INVALID = 0,

    /*
     * Normal Doom .dsg payload compressed with zlib/DEFLATE.
     */
    GC_SAVE_V3_RECORD_SAVE = 1,

    /*
     * Global DoomCube configuration.
     *
     * Configuration is not tied to a launch identity.
     */
    GC_SAVE_V3_RECORD_CONFIG = 2

} gc_save_v3_record_type_t;


/*
 * Content identity for one WAD.
 *
 * Name is retained for diagnostics/UI.
 * size + crc32 are the actual content identity.
 */
typedef struct
{
    char name[GC_SAVE_V3_NAME_MAX];

    uint32_t size;
    uint32_t crc32;

} gc_save_v3_file_identity_t;


/*
 * Complete launch identity.
 *
 * IWAD is always present.
 * PWAD is optional.
 */
typedef struct
{
    gc_save_v3_file_identity_t iwad;
    gc_save_v3_file_identity_t pwad;

    bool has_pwad;

} gc_save_v3_launch_identity_t;


/*
 * Logical contents of sector 0.
 *
 * Banner/icon/comment payloads will also live in sector 0. Their exact
 * offsets are deliberately not frozen here yet; libogc2 CARD metadata
 * will point at them explicitly.
 */
typedef struct
{
    uint32_t magic;
    uint32_t version;

    uint32_t header_size;
    uint32_t sector_size;

    uint32_t container_sectors;

    /*
     * Which physical file this image represents:
     *
     *   0 -> DOOMCUBE0
     *   1 -> DOOMCUBE1
     */
    uint32_t file_index;

    uint32_t flags;

    /*
     * CRC32 of the encoded metadata header with this field zeroed.
     */
    uint32_t header_crc32;

} gc_save_v3_container_header_t;


/*
 * Logical contents of superblock A or B.
 *
 * log_end_sector points one sector past the last committed record.
 *
 * A record written beyond log_end_sector is intentionally invisible
 * until a newer valid superblock commits it.
 */
typedef struct
{
    uint32_t magic;
    uint32_t version;

    uint32_t generation;

    uint32_t sector_size;
    uint32_t container_sectors;

    uint32_t log_start_sector;
    uint32_t log_end_sector;

    uint32_t flags;

    /*
     * CRC32 of the encoded superblock with this field zeroed.
     */
    uint32_t superblock_crc32;

} gc_save_v3_superblock_t;


/*
 * Every record begins at a sector boundary.
 *
 * The encoded header is followed immediately by the compressed payload.
 * Header + payload are padded to a whole number of CARD sectors.
 *
 * Old records remain valid until compaction. When multiple committed
 * records describe the same identity + slot, the highest generation
 * wins.
 */
typedef struct
{
    uint32_t magic;
    uint32_t version;

    uint32_t header_size;

    uint32_t record_type;
    uint32_t generation;

    /*
     * SAVE records:
     *
     *   0..5
     *
     * CONFIG records:
     *
     *   zero
     */
    uint32_t slot;

    /*
     * Unix timestamp used by DoomCube's save UI.
     */
    uint32_t timestamp;

    /*
     * Uncompressed Doom/config payload size.
     */
    uint32_t raw_size;

    /*
     * zlib/DEFLATE payload size stored after this header.
     */
    uint32_t compressed_size;

    /*
     * CRC32 of the uncompressed payload.
     */
    uint32_t raw_crc32;

    /*
     * CRC32 of the compressed payload.
     */
    uint32_t compressed_crc32;

    /*
     * Total sectors occupied by header + payload + padding.
     */
    uint32_t record_sectors;

    /*
     * SAVE records carry the launch identity.
     * CONFIG records leave this zeroed.
     */
    gc_save_v3_launch_identity_t identity;

    uint32_t flags;

    /*
     * CRC32 of the encoded record header with this field zeroed.
     */
    uint32_t header_crc32;

} gc_save_v3_record_header_t;


/*
 * Calculate the number of sectors required for one encoded record.
 *
 * encoded_header_size is supplied explicitly because the serializer,
 * rather than sizeof(gc_save_v3_record_header_t), defines the actual
 * persistent representation.
 */
static inline uint32_t GC_SaveV3RecordSectorCount(
    size_t encoded_header_size,
    size_t compressed_size,
    uint32_t sector_size)
{
    size_t total;

    if (sector_size == 0)
    {
        return 0;
    }

    if (compressed_size > SIZE_MAX - encoded_header_size)
    {
        return 0;
    }

    total =
        encoded_header_size
        + compressed_size;

    return (uint32_t)(
        (total + sector_size - 1)
        / sector_size
    );
}


bool GC_SaveV3EncodeContainerHeader(
    uint8_t *buffer,
    size_t buffer_size,
    const gc_save_v3_container_header_t *header
);

bool GC_SaveV3DecodeContainerHeader(
    gc_save_v3_container_header_t *header,
    const uint8_t *buffer,
    size_t buffer_size
);

bool GC_SaveV3EncodeSuperblock(
    uint8_t *buffer,
    size_t buffer_size,
    const gc_save_v3_superblock_t *superblock
);

bool GC_SaveV3DecodeSuperblock(
    gc_save_v3_superblock_t *superblock,
    const uint8_t *buffer,
    size_t buffer_size
);

bool GC_SaveV3EncodeRecordHeader(
    uint8_t *buffer,
    size_t buffer_size,
    const gc_save_v3_record_header_t *record
);

bool GC_SaveV3DecodeRecordHeader(
    gc_save_v3_record_header_t *record,
    const uint8_t *buffer,
    size_t buffer_size
);

/*
 * Pure in-memory codec regression:
 *
 * - container round trip
 * - superblock round trip
 * - CRC corruption rejection
 */
bool GC_SaveV3CodecSelfTest(void);


static inline bool GC_SaveV3FileIdentityEqual(
    const gc_save_v3_file_identity_t *a,
    const gc_save_v3_file_identity_t *b)
{
    if (!a || !b)
    {
        return false;
    }

    return
        a->size == b->size
        && a->crc32 == b->crc32;
}


static inline bool GC_SaveV3LaunchIdentityEqual(
    const gc_save_v3_launch_identity_t *a,
    const gc_save_v3_launch_identity_t *b)
{
    if (!a || !b)
    {
        return false;
    }

    if (!GC_SaveV3FileIdentityEqual(
            &a->iwad,
            &b->iwad))
    {
        return false;
    }

    if (a->has_pwad != b->has_pwad)
    {
        return false;
    }

    if (!a->has_pwad)
    {
        return true;
    }

    return GC_SaveV3FileIdentityEqual(
        &a->pwad,
        &b->pwad
    );
}


#endif

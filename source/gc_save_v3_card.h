#ifndef GC_SAVE_V3_CARD_H
#define GC_SAVE_V3_CARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ogc/card.h>

#include "gc_save_v3.h"


/*
 * Physical Memory Card helpers for DoomCube save format v3.
 *
 * All CARD I/O is performed as whole sectors. Caller supplies one
 * 32-byte-aligned sector-sized scratch buffer.
 */

bool GC_SaveV3CardReadSuperblock(
    card_file *file,
    void *scratch,
    size_t scratch_size,
    uint32_t sector_size,
    uint32_t container_sectors,
    uint32_t superblock_sector,
    gc_save_v3_superblock_t *superblock
);

bool GC_SaveV3CardWriteSuperblock(
    card_file *file,
    void *scratch,
    size_t scratch_size,
    uint32_t sector_size,
    uint32_t superblock_sector,
    const gc_save_v3_superblock_t *superblock
);


/*
 * Read one complete record through sector-sized CARD reads.
 *
 * read_limit_sector is one sector past the last record sector the caller
 * considers visible. For committed records this should normally be the
 * authoritative superblock's log_end_sector.
 *
 * The helper validates:
 *
 *   - encoded record-header CRC
 *   - record geometry
 *   - compressed-payload CRC
 *
 * compressed_output may be NULL when the caller only wants validation.
 */
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
    size_t *compressed_size
);


/*
 * Append and commit one record transactionally.
 *
 * Transaction:
 *
 *   1. read authoritative A/B state
 *   2. append record at authoritative log_end
 *   3. read it back and validate compressed CRC
 *   4. write the inactive superblock with generation + 1
 *
 * If record writing or verification fails, the old superblock remains
 * authoritative and the incomplete record is invisible.
 *
 * generation, compressed_size, compressed_crc32 and record_sectors are
 * filled by this function; callers provide the logical record metadata.
 */
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
    uint32_t *record_sector
);


/*
 * Latest committed SAVE record for one Doom slot.
 */
typedef struct
{
    bool present;

    uint32_t record_sector;

    gc_save_v3_record_header_t record;

} gc_save_v3_slot_index_t;


/*
 * Scan the authoritative committed log and build the six-slot view for
 * one launch identity.
 *
 * Records belonging to other IWAD/PWAD identities remain invisible.
 * When several committed records exist for one slot, the newest
 * generation wins.
 *
 * Every committed record encountered is structurally and CRC validated
 * through GC_SaveV3CardReadRecord().
 */
bool GC_SaveV3CardBuildSaveIndex(
    card_file *file,
    void *scratch,
    size_t scratch_size,
    uint32_t sector_size,
    uint32_t container_sectors,
    const gc_save_v3_launch_identity_t *identity,
    gc_save_v3_slot_index_t slots[GC_SAVE_V3_SLOT_COUNT],
    gc_save_v3_superblock_t *authoritative_superblock,
    uint32_t *authoritative_superblock_sector
);


/*
 * Read and validate both A/B superblocks and select the authoritative
 * committed generation.
 *
 * Rules:
 *
 *   - one valid, one invalid -> valid one wins
 *   - both valid            -> newest generation wins
 *   - equal + identical     -> A wins deterministically
 *   - equal + conflicting   -> reject as ambiguous/corrupt
 *   - neither valid         -> failure
 *
 * Generation comparison is wrap-aware for normal monotonic progression.
 */
bool GC_SaveV3CardReadAuthoritativeSuperblock(
    card_file *file,
    void *scratch,
    size_t scratch_size,
    uint32_t sector_size,
    uint32_t container_sectors,
    gc_save_v3_superblock_t *superblock,
    uint32_t *superblock_sector
);


#endif

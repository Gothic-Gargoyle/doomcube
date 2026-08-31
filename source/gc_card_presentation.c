#include "gc_card_presentation.h"

#include "gc_card_presentation_data.h"
#include "gc_debug.h"

#include <stdint.h>
#include <string.h>


/*
 * Sector-zero layout.
 *
 *   0x0000..0x001f   DoomCube v3 container header
 *   0x0020..0x003f   reserved
 *
 *   0x0040           CI8 banner, 96x32       3072 bytes
 *   0x0c40           banner RGB5A3 TLUT       512 bytes
 *   0x0e40           CI8 icon, 32x32         1024 bytes
 *   0x1240           icon RGB5A3 TLUT          512 bytes
 *   0x1440           two CARD comment lines     64 bytes
 *   0x1480           presentation end
 *
 * libogc requires icon_addr < CARD_READSIZE (512).  icon_addr is the
 * base from which libogc derives the banner and icon offsets.
 */
#define GC_CARD_PRESENTATION_BASE           64u

#define GC_CARD_BANNER_OFFSET               64u
#define GC_CARD_BANNER_TLUT_OFFSET        3136u
#define GC_CARD_ICON_OFFSET               3648u
#define GC_CARD_ICON_TLUT_OFFSET          4672u
#define GC_CARD_COMMENT_OFFSET            5184u
#define GC_CARD_PRESENTATION_END          5248u


static bool presentationStatusMatches(
    const card_stat *status
)
{
    unsigned int bannerFormat;
    unsigned int iconFormat;
    unsigned int iconSpeed;

    if (!status)
    {
        return false;
    }

    /*
     * Do not use CARD_GetIconSpeed() here.
     *
     * The libogc2 header currently installed on the development
     * system contains a broken mask expression in that convenience
     * macro.  Read the packed two-bit fields directly instead.
     */
    bannerFormat =
        (unsigned int)(
            status->banner_fmt &
            CARD_BANNER_MASK
        );

    iconFormat =
        (unsigned int)(
            status->icon_fmt &
            CARD_ICON_MASK
        );

    iconSpeed =
        (unsigned int)(
            status->icon_speed &
            CARD_SPEED_MASK
        );

    return
        bannerFormat ==
            CARD_BANNER_CI &&
        status->icon_addr ==
            GC_CARD_PRESENTATION_BASE &&
        iconFormat ==
            CARD_ICON_CI &&
        iconSpeed ==
            CARD_SPEED_SLOW &&
        status->comment_addr ==
            GC_CARD_COMMENT_OFFSET;
}


static bool presentationDerivedOffsetsMatch(
    const card_stat *status
)
{
    if (!status)
    {
        return false;
    }

    return
        status->offset_banner ==
            GC_CARD_BANNER_OFFSET &&
        status->offset_banner_tlut ==
            GC_CARD_BANNER_TLUT_OFFSET &&
        status->offset_icon[0] ==
            GC_CARD_ICON_OFFSET &&
        status->offset_icon_tlut[0] ==
            GC_CARD_ICON_TLUT_OFFSET;
}


bool GC_CardPresentationApply(
    card_file *file,
    s32 sectorSize,
    unsigned char *sectorBuffer
)
{
    card_stat status;

    s32 result;

    bool payloadMatches;
    bool statusMatches;
    bool changed = false;

    if (!file ||
        !sectorBuffer ||
        sectorSize <= 0)
    {
        DC_WARN(
            "DoomCube: CARD presentation rejected invalid arguments\n"
        );

        return false;
    }

    if ((uint32_t)sectorSize <
        GC_CARD_PRESENTATION_END)
    {
        DC_WARN(
            "DoomCube: CARD presentation does not fit sector: "
            "sector=%ld need=%u\n",
            (long)sectorSize,
            (unsigned int)GC_CARD_PRESENTATION_END
        );

        return false;
    }

    /*
     * Read sector zero first so that the existing transactional v3
     * header is never reconstructed or rewritten from assumptions.
     */
    result =
        CARD_Read(
            file,
            sectorBuffer,
            (u32)sectorSize,
            0
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD presentation sector-0 read failed: %ld\n",
            (long)result
        );

        return false;
    }

    payloadMatches =
        memcmp(
            sectorBuffer +
                GC_CARD_PRESENTATION_BASE,
            gc_card_presentation_data,
            GC_CARD_PRESENTATION_DATA_SIZE
        ) == 0;

    memset(
        &status,
        0,
        sizeof(status)
    );

    result =
        CARD_GetStatus(
            file->chn,
            file->filenum,
            &status
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD_GetStatus for presentation failed: %ld\n",
            (long)result
        );

        return false;
    }

    statusMatches =
        presentationStatusMatches(
            &status
        );

    if (!payloadMatches)
    {
        /*
         * Only bytes 64..5247 are replaced.
         *
         * The v3 container header in bytes 0..31 and the reserved
         * bytes 32..63 are preserved exactly as read from the card.
         */
        memcpy(
            sectorBuffer +
                GC_CARD_PRESENTATION_BASE,
            gc_card_presentation_data,
            GC_CARD_PRESENTATION_DATA_SIZE
        );

        result =
            CARD_Write(
                file,
                sectorBuffer,
                (u32)sectorSize,
                0
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: CARD presentation sector-0 write failed: %ld\n",
                (long)result
            );

            return false;
        }

        changed = true;
    }

    if (!statusMatches)
    {
        /*
         * libogc's icon_addr is the beginning of the complete
         * presentation image area.  It walks the CI banner, banner
         * palette, icon and icon palette from this base.
         *
         * Set the packed fields directly rather than using the
         * installed CARD_SetIconSpeed() macro.
         */
        status.banner_fmt =
            (u8)CARD_BANNER_CI;

        status.icon_addr =
            GC_CARD_PRESENTATION_BASE;

        status.icon_fmt =
            (u16)CARD_ICON_CI;

        status.icon_speed =
            (u16)CARD_SPEED_SLOW;

        status.comment_addr =
            GC_CARD_COMMENT_OFFSET;

        result =
            CARD_SetStatus(
                file->chn,
                file->filenum,
                &status
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: CARD_SetStatus for presentation failed: %ld\n",
                (long)result
            );

            return false;
        }

        changed = true;
    }

    /*
     * Verify the directory metadata and libogc's derived physical
     * offsets after the write.
     */
    memset(
        &status,
        0,
        sizeof(status)
    );

    result =
        CARD_GetStatus(
            file->chn,
            file->filenum,
            &status
        );

    if (result !=
        CARD_ERROR_READY)
    {
        DC_WARN(
            "DoomCube: CARD presentation verification status failed: %ld\n",
            (long)result
        );

        return false;
    }

    if (!presentationStatusMatches(
            &status))
    {
        DC_WARN(
            "DoomCube: CARD presentation status verification mismatch\n"
        );

        return false;
    }

    if (!presentationDerivedOffsetsMatch(
            &status))
    {
        DC_WARN(
            "DoomCube: CARD presentation derived offsets mismatch: "
            "banner=%u banner_tlut=%u icon=%u icon_tlut=%u\n",
            (unsigned int)status.offset_banner,
            (unsigned int)status.offset_banner_tlut,
            (unsigned int)status.offset_icon[0],
            (unsigned int)status.offset_icon_tlut[0]
        );

        return false;
    }

    if (changed)
    {
        /*
         * Verify the actual sector payload too.
         */
        result =
            CARD_Read(
                file,
                sectorBuffer,
                (u32)sectorSize,
                0
            );

        if (result !=
            CARD_ERROR_READY)
        {
            DC_WARN(
                "DoomCube: CARD presentation read-back failed: %ld\n",
                (long)result
            );

            return false;
        }

        if (memcmp(
                sectorBuffer +
                    GC_CARD_PRESENTATION_BASE,
                gc_card_presentation_data,
                GC_CARD_PRESENTATION_DATA_SIZE) != 0)
        {
            DC_WARN(
                "DoomCube: CARD presentation payload verification mismatch\n"
            );

            return false;
        }

        DC_INFO(
            "DoomCube: CARD presentation installed: "
            "banner=CI8 icon=CI8 comments=%u\n",
            (unsigned int)GC_CARD_COMMENT_OFFSET
        );
    }
    else
    {
        DC_DEBUG(
            "DoomCube: CARD presentation already current\n"
        );
    }

    return true;
}

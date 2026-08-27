/* ------------------------------------------------------------------------- */
/* DoomCube automated regression controller                                  */
/* ------------------------------------------------------------------------- */

#include "gc_debug.h"
#include "gc_regression.h"

#include <stddef.h>
#include <time.h>

#ifdef DOOMCUBE_REGRESSION

/*
 * Dolphin regression transport.
 *
 * The host launches the same ISO with:
 *
 *   EnableCustomRTC=True
 *   CustomRTCValue=GC_REGRESSION_RTC_BASE +
 *                  case_index * GC_REGRESSION_RTC_SLOT
 *
 * time(NULL) advances normally after boot, so each slot is deliberately
 * large and only its first minute is accepted.
 */
#define GC_REGRESSION_RTC_BASE       1704067200LL
#define GC_REGRESSION_RTC_SLOT       300LL
#define GC_REGRESSION_RTC_TOLERANCE  60LL

static const gc_regression_case_t gcRegressionCases[] =
{
    {
        "sigil-e5m6-secret",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        6,
        GC_REGRESSION_ACTION_SECRET_EXIT
    },
    {
        "sigil-e5m9-return",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        9,
        GC_REGRESSION_ACTION_EXIT
    },
    {
        "sigil2-e6m3-secret",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        3,
        GC_REGRESSION_ACTION_SECRET_EXIT
    },
    {
        "sigil2-e6m9-return",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        9,
        GC_REGRESSION_ACTION_EXIT
    },
    {
        "sigil-e5m8-finale",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        8,
        GC_REGRESSION_ACTION_EXIT
    },
    {
        "sigil2-e6m8-finale",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        8,
        GC_REGRESSION_ACTION_EXIT
    }
    ,
    {
        "save-doom1-e1m1",
        "dvd:/data/wad/doom1.wad",
        NULL,
        1,
        1,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-doom-e1m1",
        "dvd:/data/wad/doom.wad",
        NULL,
        1,
        1,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-doom2-map01",
        "dvd:/data/wad/doom2.wad",
        NULL,
        1,
        1,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-tnt-map01",
        "dvd:/data/wad/tnt.wad",
        NULL,
        1,
        1,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-plutonia-map01",
        "dvd:/data/wad/plutonia.wad",
        NULL,
        1,
        1,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil-e5m1",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        1,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil2-e6m1",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        1,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil-e5m2",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        2,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil-e5m3",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        3,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil-e5m4",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        4,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil-e5m5",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        5,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil-e5m6",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        6,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil-e5m7",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        7,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil-e5m8",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        8,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil-e5m9",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_V1_23.wad",
        5,
        9,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil2-e6m2",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        2,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil2-e6m3",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        3,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil2-e6m4",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        4,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil2-e6m5",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        5,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil2-e6m6",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        6,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil2-e6m7",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        7,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil2-e6m8",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        8,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "save-sigil2-e6m9",
        "dvd:/data/wad/doom.wad",
        "dvd:/data/pwad/SIGIL_II_V1_0.WAD",
        6,
        9,
        GC_REGRESSION_ACTION_SAVE_PROBE
    },
    {
        "load-doom-e1m1-persisted",
        "dvd:/data/wad/doom.wad",
        NULL,
        1,
        1,
        GC_REGRESSION_ACTION_LOAD_PROBE
    }

};

#define GC_REGRESSION_CASE_COUNT \
    ((int)(sizeof(gcRegressionCases) / sizeof(gcRegressionCases[0])))

static const gc_regression_case_t *gcRegressionCase;
static int gcRegressionCaseIndex = -1;

#endif


bool GC_RegressionInit(void)
{
#ifdef DOOMCUBE_REGRESSION
    time_t now;
    long long rtc;
    long long delta;
    long long remainder;
    long long index;

    now = time(NULL);
    rtc = (long long)now;

    DC_INFO(
        "DoomCube: REGRESSION RTC: %lld\n",
        rtc);

    delta =
        rtc - GC_REGRESSION_RTC_BASE;

    if (delta < 0)
    {
        DC_ERROR(
            "DoomCube: REGRESSION invalid RTC: %lld\n",
            rtc);

        return false;
    }

    index =
        delta / GC_REGRESSION_RTC_SLOT;

    remainder =
        delta % GC_REGRESSION_RTC_SLOT;

    if (remainder > GC_REGRESSION_RTC_TOLERANCE)
    {
        DC_ERROR(
            "DoomCube: REGRESSION RTC outside test slot: %lld\n",
            rtc);

        return false;
    }

    if (index < 0 ||
        index >= GC_REGRESSION_CASE_COUNT)
    {
        DC_ERROR(
            "DoomCube: REGRESSION invalid case index: %lld\n",
            index);

        return false;
    }

    gcRegressionCaseIndex =
        (int)index;

    gcRegressionCase =
        &gcRegressionCases[gcRegressionCaseIndex];

    DC_INFO(
        "DoomCube: REGRESSION CASE %d/%d: %s\n",
        gcRegressionCaseIndex,
        GC_REGRESSION_CASE_COUNT,
        gcRegressionCase->name);

    return true;
#else
    return false;
#endif
}


const gc_regression_case_t *GC_RegressionGetCase(void)
{
#ifdef DOOMCUBE_REGRESSION
    return gcRegressionCase;
#else
    return NULL;
#endif
}


int GC_RegressionGetCaseIndex(void)
{
#ifdef DOOMCUBE_REGRESSION
    return gcRegressionCaseIndex;
#else
    return -1;
#endif
}


int GC_RegressionGetCaseCount(void)
{
#ifdef DOOMCUBE_REGRESSION
    return GC_REGRESSION_CASE_COUNT;
#else
    return 0;
#endif
}

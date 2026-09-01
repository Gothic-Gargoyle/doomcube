#include "gc_rumble.h"

#include "gc_debug.h"
#include "i_system.h"

#include <carryhandle/ch_rumble.h>

#include <stdint.h>


#define GC_RUMBLE_TICKS_PER_SECOND 35u
#define GC_RUMBLE_ON_TIME_SCALE 2u


int gc_rumble_enabled = 1;


static bool gcRumbleInitialized;
static bool gcRumbleExitRegistered;
static bool gcRumbleDemoMuted;


static uint32_t ticksToMs(
    int ticks)
{
    if (ticks <= 0)
        return 0;

    return (
        ((uint32_t)ticks * 1000u)
        + (GC_RUMBLE_TICKS_PER_SECOND - 1u)
    ) / GC_RUMBLE_TICKS_PER_SECOND;
}


static uint32_t onTicksToMs(
    int ticks)
{
    return
        ticksToMs(ticks)
        * GC_RUMBLE_ON_TIME_SCALE;
}


static bool ensureRumble(void)
{
    if (gcRumbleInitialized)
        return true;

    if (!CH_RumbleInit())
    {
        DC_WARN(
            "DoomCube: CarryHandle rumble initialization failed\n");
        return false;
    }

    gcRumbleInitialized = true;

    if (!gcRumbleExitRegistered)
    {
        I_AtExit(
            GC_RumbleShutdown,
            false);

        gcRumbleExitRegistered = true;
    }

    DC_INFO(
        "DoomCube: rumble sequencer started "
        "(independent wall-clock worker)\n");

    return true;
}


void GC_RumblePulseTicks(
    int ticks)
{
    if (ticks <= 0 ||
        !gc_rumble_enabled ||
        gcRumbleDemoMuted ||
        !ensureRumble())
    {
        return;
    }

    CH_RumblePulse(
        0,
        onTicksToMs(ticks));
}


void GC_RumblePatternTicks(
    int onTicks,
    int offTicks,
    int pulses,
    bool hardStop)
{
    if (onTicks <= 0 ||
        pulses <= 0 ||
        !gc_rumble_enabled ||
        gcRumbleDemoMuted ||
        !ensureRumble())
    {
        return;
    }

    if (offTicks < 0)
        offTicks = 0;

    CH_RumblePattern(
        0,
        onTicksToMs(onTicks),
        ticksToMs(offTicks),
        (unsigned int)pulses,
        hardStop);
}


void GC_RumbleSetEnabled(
    bool enabled)
{
    gc_rumble_enabled =
        enabled ? 1 : 0;

    if (!gc_rumble_enabled)
        CH_RumbleStop(0, true);

    DC_INFO(
        "DoomCube: rumble enabled=%d\n",
        gc_rumble_enabled);
}


void GC_RumbleSetDemoMode(
    bool active)
{
    bool changed =
        gcRumbleDemoMuted != active;

    gcRumbleDemoMuted =
        active;

    if (active)
        CH_RumbleStop(0, true);

    if (changed)
    {
        DC_INFO(
            "DoomCube: rumble demo mute=%d\n",
            active ? 1 : 0);
    }
}


void GC_RumbleApplyConfig(void)
{
    gc_rumble_enabled =
        gc_rumble_enabled ? 1 : 0;

    if (!gc_rumble_enabled)
        CH_RumbleStop(0, true);

    DC_INFO(
        "DoomCube: rumble configuration loaded: enabled=%d\n",
        gc_rumble_enabled);
}


void GC_RumbleShutdown(void)
{
    if (!gcRumbleInitialized)
        return;

    CH_RumbleShutdown();

    gcRumbleInitialized =
        false;

    DC_INFO(
        "DoomCube: rumble sequencer stopped\n");
}

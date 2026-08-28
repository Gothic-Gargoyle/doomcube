#ifndef DOOMCUBE_GC_RUMBLE_H
#define DOOMCUBE_GC_RUMBLE_H

#include <stdbool.h>

/*
 * Master player preference.
 *
 * 0 = disabled
 * 1 = enabled
 *
 * This is stored in DoomCube's global configuration.
 */
extern int gc_rumble_enabled;


/*
 * Historical DoomCube rumble durations use 35 Hz-style units.
 *
 * These APIs convert those units to milliseconds before they enter
 * the independent wall-clock sequencer.
 */
void GC_RumblePulseTicks(
    int ticks);

void GC_RumblePatternTicks(
    int onTicks,
    int offTicks,
    int pulses,
    bool hardStop);


/*
 * Player preference / runtime gates.
 */
void GC_RumbleSetEnabled(
    bool enabled);

void GC_RumbleSetDemoMode(
    bool active);

void GC_RumbleApplyConfig(void);


/*
 * Stop the motor, terminate the worker, and release synchronization
 * resources. Safe to call more than once.
 */
void GC_RumbleShutdown(void);

#endif

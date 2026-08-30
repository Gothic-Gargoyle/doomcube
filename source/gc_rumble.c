#include "gc_rumble.h"

#include "gc_controls.h"
#include "gc_debug.h"

#include "i_system.h"

#include <SDL2/SDL.h>

#include <ogc/pad.h>

#include <stdint.h>


#define GC_RUMBLE_TICKS_PER_SECOND 35u

/*
 * GameCube exposes a binary motor command, not analogue amplitude.
 *
 * The motor is already fully powered during every ON phase, so the
 * requested "2x intensity" is represented as 2x ON-time instead.
 *
 * Explicit OFF gaps in patterns are deliberately NOT scaled.
 */
#define GC_RUMBLE_ON_TIME_SCALE 2u


int gc_rumble_enabled = 1;


static SDL_mutex *rumbleMutex;
static SDL_cond *rumbleCond;
static SDL_Thread *rumbleThread;

static bool rumbleExitRegistered;
static bool rumbleStopRequested;
static bool rumbleDemoMuted;

static bool rumbleEffectActive;
static bool rumbleOnPhase;
static bool rumbleMotorOn;
static bool rumbleHardStopAtEnd;

static uint32_t rumbleDeadlineMs;
static uint32_t rumbleOnMs;
static uint32_t rumbleOffMs;

static int rumblePulsesLeft;

static uint32_t rumbleRequestCount;
static uint32_t rumbleTransitionCount;


/* ------------------------------------------------------------------------- */
/* Timing                                                                     */
/* ------------------------------------------------------------------------- */


static uint32_t ticksToMs(
    int ticks)
{
    if (ticks <= 0)
        return 0;

    /*
     * Round upward so the conversion can never shorten one of the
     * historical DoomCube effects.
     */
    return (
        ((uint32_t)ticks * 1000u)
        + (GC_RUMBLE_TICKS_PER_SECOND - 1u)
    ) /
        GC_RUMBLE_TICKS_PER_SECOND;
}


static uint32_t onTicksToMs(
    int ticks)
{
    uint32_t milliseconds =
        ticksToMs(
            ticks
        );

    return
        milliseconds *
        GC_RUMBLE_ON_TIME_SCALE;
}


static bool deadlineReached(
    uint32_t now,
    uint32_t deadline)
{
    /*
     * Wrap-safe for SDL_GetTicks()' 32-bit counter.
     */
    return (int32_t)(now - deadline) >= 0;
}


static uint32_t timeUntil(
    uint32_t now,
    uint32_t deadline)
{
    int32_t remaining =
        (int32_t)(
            deadline -
            now
        );

    if (remaining <= 0)
        return 1;

    return (uint32_t)remaining;
}


/* ------------------------------------------------------------------------- */
/* Motor ownership                                                            */
/* ------------------------------------------------------------------------- */


static void setMotorLocked(
    bool on,
    bool hardStop)
{
    if (on)
    {
        if (rumbleMotorOn)
            return;

        GC_ControlsMotorCommand(
            PAD_MOTOR_RUMBLE
        );

        rumbleMotorOn =
            true;

        rumbleTransitionCount++;

        return;
    }

    if (!rumbleMotorOn)
        return;

    GC_ControlsMotorCommand(
        hardStop
            ? PAD_MOTOR_STOP_HARD
            : PAD_MOTOR_STOP
    );

    rumbleMotorOn =
        false;

    rumbleTransitionCount++;
}


static void cancelEffectLocked(
    bool hardStop)
{
    rumbleEffectActive =
        false;

    rumbleOnPhase =
        false;

    rumblePulsesLeft =
        0;

    setMotorLocked(
        false,
        hardStop
    );
}


/* ------------------------------------------------------------------------- */
/* Independent sequencer                                                      */
/* ------------------------------------------------------------------------- */


static void advanceEffectLocked(
    uint32_t now)
{
    if (!rumbleEffectActive)
        return;

    if (!deadlineReached(
            now,
            rumbleDeadlineMs))
    {
        return;
    }

    if (rumbleOnPhase)
    {
        rumblePulsesLeft--;

        if (rumblePulsesLeft <= 0)
        {
            cancelEffectLocked(
                rumbleHardStopAtEnd
            );

            return;
        }

        if (rumbleOffMs > 0)
        {
            /*
             * Ordinary STOP lets the eccentric motor coast between
             * pulses. STOP_HARD is reserved for actual cancellation.
             */
            setMotorLocked(
                false,
                false
            );

            rumbleOnPhase =
                false;

            rumbleDeadlineMs =
                now +
                rumbleOffMs;
        }
        else
        {
            /*
             * No OFF phase means one continuous physical vibration.
             */
            rumbleDeadlineMs =
                now +
                rumbleOnMs;
        }
    }
    else
    {
        setMotorLocked(
            true,
            false
        );

        rumbleOnPhase =
            true;

        rumbleDeadlineMs =
            now +
            rumbleOnMs;
    }
}


static int rumbleWorkerMain(
    void *unused)
{
    (void)unused;

    SDL_LockMutex(
        rumbleMutex
    );

    while (!rumbleStopRequested)
    {
        uint32_t now;
        uint32_t waitMs;

        if (!gc_rumble_enabled
            || rumbleDemoMuted)
        {
            cancelEffectLocked(
                true
            );

            SDL_CondWait(
                rumbleCond,
                rumbleMutex
            );

            continue;
        }

        if (!rumbleEffectActive)
        {
            SDL_CondWait(
                rumbleCond,
                rumbleMutex
            );

            continue;
        }

        now =
            SDL_GetTicks();

        advanceEffectLocked(
            now
        );

        if (!rumbleEffectActive)
            continue;

        waitMs =
            timeUntil(
                now,
                rumbleDeadlineMs
            );

        /*
         * Sleeps until either:
         *
         *   - the next motor transition is due, or
         *   - the game signals a new effect/toggle/demo transition.
         *
         * There is no render-loop polling here.
         */
        SDL_CondWaitTimeout(
            rumbleCond,
            rumbleMutex,
            waitMs
        );
    }

    cancelEffectLocked(
        true
    );

    SDL_UnlockMutex(
        rumbleMutex
    );

    return 0;
}


/* ------------------------------------------------------------------------- */
/* Worker lifecycle                                                           */
/* ------------------------------------------------------------------------- */


static bool ensureWorker(void)
{
    if (rumbleThread)
        return true;

    if (!GC_ControlsInitPadMutex())
    {
        DC_WARN(
            "DoomCube: rumble: failed to create PAD mutex: %s\n",
            SDL_GetError()
        );

        return false;
    }

    rumbleMutex =
        SDL_CreateMutex();

    if (!rumbleMutex)
    {
        DC_WARN(
            "DoomCube: rumble: failed to create state mutex: %s\n",
            SDL_GetError()
        );

        GC_ControlsShutdownPadMutex();

        return false;
    }

    rumbleCond =
        SDL_CreateCond();

    if (!rumbleCond)
    {
        DC_WARN(
            "DoomCube: rumble: failed to create condition variable: %s\n",
            SDL_GetError()
        );

        SDL_DestroyMutex(
            rumbleMutex
        );

        rumbleMutex =
            NULL;

        GC_ControlsShutdownPadMutex();

        return false;
    }

    rumbleStopRequested =
        false;

    rumbleThread =
        SDL_CreateThread(
            rumbleWorkerMain,
            "DoomCube rumble",
            NULL
        );

    if (!rumbleThread)
    {
        DC_WARN(
            "DoomCube: rumble: failed to create worker: %s\n",
            SDL_GetError()
        );

        SDL_DestroyCond(
            rumbleCond
        );

        SDL_DestroyMutex(
            rumbleMutex
        );

        rumbleCond =
            NULL;

        rumbleMutex =
            NULL;

        GC_ControlsShutdownPadMutex();

        return false;
    }

    /*
     * Normal Doom shutdown runs registered exit handlers before the SDL
     * process teardown. The platform main-loop return path also calls
     * GC_RumbleShutdown(), and the function is deliberately idempotent.
     */
    if (!rumbleExitRegistered)
    {
        I_AtExit(
            GC_RumbleShutdown,
            false
        );

        rumbleExitRegistered =
            true;
    }

    DC_INFO(
        "DoomCube: rumble sequencer started (independent wall-clock worker)\n"
    );

    return true;
}


/* ------------------------------------------------------------------------- */
/* Effect requests                                                            */
/* ------------------------------------------------------------------------- */


void GC_RumblePulseTicks(
    int ticks)
{
    uint32_t durationMs;
    uint32_t now;

    if (ticks <= 0)
        return;

    if (!gc_rumble_enabled
        || rumbleDemoMuted)
    {
        return;
    }

    if (!ensureWorker())
        return;

    durationMs =
        onTicksToMs(
            ticks
        );

    SDL_LockMutex(
        rumbleMutex
    );

    if (!gc_rumble_enabled
        || rumbleDemoMuted
        || rumbleStopRequested)
    {
        SDL_UnlockMutex(
            rumbleMutex
        );

        return;
    }

    now =
        SDL_GetTicks();

    /*
     * Preserve the old single-pulse merge behaviour:
     *
     * A shorter request never cuts an existing pulse short.
     * A longer request extends it from the current moment.
     */
    if (rumbleEffectActive
        && rumbleOnPhase
        && rumblePulsesLeft == 1
        && rumbleOffMs == 0)
    {
        int32_t remaining =
            (int32_t)(
                rumbleDeadlineMs -
                now
            );

        if (remaining < 0)
            remaining = 0;

        if (durationMs >
            (uint32_t)remaining)
        {
            rumbleDeadlineMs =
                now +
                durationMs;
        }

        setMotorLocked(
            true,
            false
        );

        SDL_CondSignal(
            rumbleCond
        );

        SDL_UnlockMutex(
            rumbleMutex
        );

        return;
    }

    rumbleOnMs =
        durationMs;

    rumbleOffMs =
        0;

    rumblePulsesLeft =
        1;

    rumbleHardStopAtEnd =
        true;

    rumbleEffectActive =
        true;

    rumbleOnPhase =
        true;

    rumbleDeadlineMs =
        now +
        rumbleOnMs;

    rumbleRequestCount++;

    /*
     * First edge happens immediately. Every later edge belongs entirely
     * to the worker.
     */
    setMotorLocked(
        true,
        false
    );

    SDL_CondSignal(
        rumbleCond
    );

    SDL_UnlockMutex(
        rumbleMutex
    );
}


void GC_RumblePatternTicks(
    int onTicks,
    int offTicks,
    int pulses,
    bool hardStop)
{
    uint32_t now;

    if (onTicks <= 0
        || pulses <= 0)
    {
        return;
    }

    if (!gc_rumble_enabled
        || rumbleDemoMuted)
    {
        return;
    }

    if (!ensureWorker())
        return;

    if (offTicks < 0)
        offTicks = 0;

    SDL_LockMutex(
        rumbleMutex
    );

    if (!gc_rumble_enabled
        || rumbleDemoMuted
        || rumbleStopRequested)
    {
        SDL_UnlockMutex(
            rumbleMutex
        );

        return;
    }

    rumbleOnMs =
        onTicksToMs(
            onTicks
        );

    rumbleOffMs =
        ticksToMs(
            offTicks
        );

    rumblePulsesLeft =
        pulses;

    rumbleHardStopAtEnd =
        hardStop;

    rumbleEffectActive =
        true;

    rumbleOnPhase =
        true;

    now =
        SDL_GetTicks();

    rumbleDeadlineMs =
        now +
        rumbleOnMs;

    rumbleRequestCount++;

    setMotorLocked(
        true,
        false
    );

    SDL_CondSignal(
        rumbleCond
    );

    SDL_UnlockMutex(
        rumbleMutex
    );
}


/* ------------------------------------------------------------------------- */
/* Master preference + demo gate                                              */
/* ------------------------------------------------------------------------- */


void GC_RumbleSetEnabled(
    bool enabled)
{
    int normalized =
        enabled
            ? 1
            : 0;

    if (rumbleMutex)
    {
        SDL_LockMutex(
            rumbleMutex
        );

        gc_rumble_enabled =
            normalized;

        if (!gc_rumble_enabled)
        {
            cancelEffectLocked(
                true
            );
        }

        SDL_CondSignal(
            rumbleCond
        );

        SDL_UnlockMutex(
            rumbleMutex
        );
    }
    else
    {
        gc_rumble_enabled =
            normalized;
    }

    DC_INFO(
        "DoomCube: rumble enabled=%d\n",
        gc_rumble_enabled
    );
}


void GC_RumbleSetDemoMode(
    bool active)
{
    bool changed;

    if (rumbleMutex)
    {
        SDL_LockMutex(
            rumbleMutex
        );

        changed =
            rumbleDemoMuted != active;

        rumbleDemoMuted =
            active;

        if (active)
        {
            cancelEffectLocked(
                true
            );
        }

        SDL_CondSignal(
            rumbleCond
        );

        SDL_UnlockMutex(
            rumbleMutex
        );
    }
    else
    {
        changed =
            rumbleDemoMuted != active;

        rumbleDemoMuted =
            active;
    }

    if (changed)
    {
        DC_INFO(
            "DoomCube: rumble demo mute=%d\n",
            active
                ? 1
                : 0
        );
    }
}


void GC_RumbleApplyConfig(void)
{
    gc_rumble_enabled =
        gc_rumble_enabled
            ? 1
            : 0;

    if (!gc_rumble_enabled
        && rumbleMutex)
    {
        SDL_LockMutex(
            rumbleMutex
        );

        cancelEffectLocked(
            true
        );

        SDL_CondSignal(
            rumbleCond
        );

        SDL_UnlockMutex(
            rumbleMutex
        );
    }

    DC_INFO(
        "DoomCube: rumble configuration loaded: enabled=%d\n",
        gc_rumble_enabled
    );
}


/* ------------------------------------------------------------------------- */
/* Shutdown                                                                   */
/* ------------------------------------------------------------------------- */


void GC_RumbleShutdown(void)
{
    SDL_Thread *thread;

    if (!rumbleThread)
        return;

    SDL_LockMutex(
        rumbleMutex
    );

    rumbleStopRequested =
        true;

    cancelEffectLocked(
        true
    );

    SDL_CondSignal(
        rumbleCond
    );

    SDL_UnlockMutex(
        rumbleMutex
    );

    thread =
        rumbleThread;

    SDL_WaitThread(
        thread,
        NULL
    );

    rumbleThread =
        NULL;

    DC_INFO(
        "DoomCube: rumble sequencer stopped: requests=%u transitions=%u\n",
        (unsigned int)rumbleRequestCount,
        (unsigned int)rumbleTransitionCount
    );

    SDL_DestroyCond(
        rumbleCond
    );

    SDL_DestroyMutex(
        rumbleMutex
    );

    rumbleCond =
        NULL;

    rumbleMutex =
        NULL;

    GC_ControlsShutdownPadMutex();
}

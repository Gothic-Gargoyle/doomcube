#include "gc_controls.h"

#include <gccore.h>
#include <ogc/system.h>

#include <stdlib.h>
#include "m_config.h"
#include <SDL2/SDL.h>

#define GC_STICK_DEADZONE       16
#define GC_CSTICK_DEADZONE      24
#define GC_MAP_CSTICK_DEADZONE  40
#define GC_TRIGGER_THRESHOLD    40

#define GC_TURN_SENSITIVITY_MIN      25
#define GC_TURN_SENSITIVITY_MAX     200
#define GC_TURN_SENSITIVITY_DEFAULT 100
#define GC_ANALOG_TURN_MAX          2048


/* ------------------------------------------------------------------------- */
/* Raw controller state                                                      */
/* ------------------------------------------------------------------------- */

static u16 gcHeld;

static s8 gcStickX;
static s8 gcStickY;

static s8 gcCStickX;
static s8 gcCStickY;

static u8 gcTriggerR;

static int gcTurnSensitivity =
    GC_TURN_SENSITIVITY_DEFAULT;


/* ------------------------------------------------------------------------- */
/* Bindings                                                                  */
/* ------------------------------------------------------------------------- */

static int gcBindings[GC_ACTION_COUNT] =
{
    [GC_ACTION_MOVE_UP]      = GC_INPUT_STICK_UP,
    [GC_ACTION_MOVE_DOWN]    = GC_INPUT_STICK_DOWN,
    [GC_ACTION_MOVE_LEFT]    = GC_INPUT_STICK_LEFT,
    [GC_ACTION_MOVE_RIGHT]   = GC_INPUT_STICK_RIGHT,

    [GC_ACTION_STRAFE_LEFT]  = GC_INPUT_CSTICK_LEFT,
    [GC_ACTION_STRAFE_RIGHT] = GC_INPUT_CSTICK_RIGHT,

    [GC_ACTION_FIRE]         = GC_INPUT_R,
    [GC_ACTION_USE]          = GC_INPUT_A,
    [GC_ACTION_RUN]          = GC_INPUT_L,

    [GC_ACTION_NEXT_WEAPON]  = GC_INPUT_X,
    [GC_ACTION_PREV_WEAPON]  = GC_INPUT_Y,

    [GC_ACTION_MENU_CONFIRM] = GC_INPUT_A,
    [GC_ACTION_MENU_BACK]    = GC_INPUT_B
};


/* ------------------------------------------------------------------------- */
/* Binding capture                                                           */
/* ------------------------------------------------------------------------- */

static bool gcCapturing;
static bool gcCaptureArmed;
static bool gcCaptureReleaseWait;

static gc_action_t gcCaptureAction;


/* ------------------------------------------------------------------------- */
/* Input helpers                                                             */
/* ------------------------------------------------------------------------- */

static bool GC_InputHeld(
    gc_input_t input)
{
    switch (input)
    {
        case GC_INPUT_A:
            return
                (gcHeld & PAD_BUTTON_A) != 0;

        case GC_INPUT_B:
            return
                (gcHeld & PAD_BUTTON_B) != 0;

        case GC_INPUT_X:
            return
                (gcHeld & PAD_BUTTON_X) != 0;

        case GC_INPUT_Y:
            return
                (gcHeld & PAD_BUTTON_Y) != 0;

        case GC_INPUT_L:
            return
                (gcHeld & PAD_TRIGGER_L) != 0;

        case GC_INPUT_R:
            return
                gcTriggerR > GC_TRIGGER_THRESHOLD;

        case GC_INPUT_DPAD_UP:
            return
                (gcHeld & PAD_BUTTON_UP) != 0;

        case GC_INPUT_DPAD_DOWN:
            return
                (gcHeld & PAD_BUTTON_DOWN) != 0;

        case GC_INPUT_DPAD_LEFT:
            return
                (gcHeld & PAD_BUTTON_LEFT) != 0;

        case GC_INPUT_DPAD_RIGHT:
            return
                (gcHeld & PAD_BUTTON_RIGHT) != 0;

        case GC_INPUT_STICK_UP:
            return
                gcStickY > GC_STICK_DEADZONE;

        case GC_INPUT_STICK_DOWN:
            return
                gcStickY < -GC_STICK_DEADZONE;

        case GC_INPUT_STICK_LEFT:
            return
                gcStickX < -GC_STICK_DEADZONE;

        case GC_INPUT_STICK_RIGHT:
            return
                gcStickX > GC_STICK_DEADZONE;

        case GC_INPUT_CSTICK_UP:
            return
                gcCStickY > GC_CSTICK_DEADZONE;

        case GC_INPUT_CSTICK_DOWN:
            return
                gcCStickY < -GC_CSTICK_DEADZONE;

        case GC_INPUT_CSTICK_LEFT:
            return
                gcCStickX < -GC_CSTICK_DEADZONE;

        case GC_INPUT_CSTICK_RIGHT:
            return
                gcCStickX > GC_CSTICK_DEADZONE;

        case GC_INPUT_NONE:
        case GC_INPUT_COUNT:
        default:
            return false;
    }
}


static gc_input_t GC_FirstInputHeld(void)
{
    gc_input_t input;

    for (input = GC_INPUT_A;
         input < GC_INPUT_COUNT;
         ++input)
    {
        if (GC_InputHeld(input))
            return input;
    }

    return GC_INPUT_NONE;
}


/* ------------------------------------------------------------------------- */
/* Poll                                                                      */
/* ------------------------------------------------------------------------- */

static SDL_mutex *gcPadMutex;


bool GC_ControlsInitPadMutex(void)
{
    if (gcPadMutex)
        return true;

    gcPadMutex =
        SDL_CreateMutex();

    return gcPadMutex != NULL;
}


void GC_ControlsShutdownPadMutex(void)
{
    if (!gcPadMutex)
        return;

    SDL_DestroyMutex(
        gcPadMutex
    );

    gcPadMutex =
        NULL;
}


void GC_ControlsMotorCommand(
    unsigned int command)
{
    if (gcPadMutex)
    {
        SDL_LockMutex(
            gcPadMutex
        );
    }

    PAD_ControlMotor(
        PAD_CHAN0,
        command
    );

    if (gcPadMutex)
    {
        SDL_UnlockMutex(
            gcPadMutex
        );
    }
}


void GC_ControlsPoll(void)
{
    if (gcPadMutex)
    {
        SDL_LockMutex(
            gcPadMutex
        );
    }

    PAD_ScanPads();

    if (gcPadMutex)
    {
        SDL_UnlockMutex(
            gcPadMutex
        );
    }

    gcHeld =
        PAD_ButtonsHeld(0);

    gcStickX =
        PAD_StickX(0);

    gcStickY =
        PAD_StickY(0);


    gcCStickX =
        PAD_SubStickX(0);

    gcCStickY =
        PAD_SubStickY(0);

    gcTriggerR =
        PAD_TriggerR(0);

/*
 * A binding was just assigned.
 *
 * Suppress normal controller input until every bindable
 * input has been released.
 */
if (gcCaptureReleaseWait)
{
    if (GC_FirstInputHeld() == GC_INPUT_NONE)
    {
        gcCaptureReleaseWait =
            false;
    }

    return;
}


/*
 * Controller binding capture.
 *
 * First wait until the button used to select the menu entry
 * has been released. Then accept the next physical input.
 */
if (gcCapturing)
{
    gc_input_t input =
        GC_FirstInputHeld();

    if (!gcCaptureArmed)
    {
        if (input == GC_INPUT_NONE)
        {
            gcCaptureArmed =
                true;
        }

        return;
    }

    if (input != GC_INPUT_NONE)
    {
        GC_ControlsSetBinding(
            gcCaptureAction,
            input);

        gcCapturing =
            false;

        gcCaptureArmed =
            false;

        /*
         * Do not let the newly assigned input also operate
         * the menu. Wait for it to be released first.
         */
        gcCaptureReleaseWait =
            true;

        return;
    }
}
}

/* ------------------------------------------------------------------------- */
/* Remappable actions                                                        */
/* ------------------------------------------------------------------------- */

bool GC_ControlHeld(
    gc_action_t action)
{
    if (gcCapturing ||
        gcCaptureReleaseWait)
    {
        return false;
    }

    if (action < 0 ||
        action >= GC_ACTION_COUNT)
    {
        return false;
    }

    return
        GC_InputHeld(
            gcBindings[action]);
}

bool GC_ControlHasAnalogAxis(
    gc_action_t negative_action,
    gc_action_t positive_action)
{
    gc_input_t negative;
    gc_input_t positive;

    if (negative_action < 0 ||
        negative_action >= GC_ACTION_COUNT ||
        positive_action < 0 ||
        positive_action >= GC_ACTION_COUNT)
    {
        return false;
    }

    negative = gcBindings[negative_action];
    positive = gcBindings[positive_action];

    if (negative == GC_INPUT_STICK_LEFT &&
        positive == GC_INPUT_STICK_RIGHT)
    {
        return true;
    }

    if (negative == GC_INPUT_STICK_DOWN &&
        positive == GC_INPUT_STICK_UP)
    {
        return true;
    }

    if (negative == GC_INPUT_CSTICK_LEFT &&
        positive == GC_INPUT_CSTICK_RIGHT)
    {
        return true;
    }

    if (negative == GC_INPUT_CSTICK_DOWN &&
        positive == GC_INPUT_CSTICK_UP)
    {
        return true;
    }

    return false;
}

int GC_ControlAnalogAxis(
    gc_action_t negative_action,
    gc_action_t positive_action)
{
    gc_input_t negative;
    gc_input_t positive;

    if (gcCapturing ||
        gcCaptureReleaseWait)
    {
        return 0;
    }

    if (!GC_ControlHasAnalogAxis(
            negative_action,
            positive_action))
    {
        return 0;
    }

    negative = gcBindings[negative_action];
    positive = gcBindings[positive_action];

    if (negative == GC_INPUT_STICK_LEFT &&
        positive == GC_INPUT_STICK_RIGHT)
    {
        return gcStickX;
    }

    if (negative == GC_INPUT_STICK_DOWN &&
        positive == GC_INPUT_STICK_UP)
    {
        return gcStickY;
    }

    if (negative == GC_INPUT_CSTICK_LEFT &&
        positive == GC_INPUT_CSTICK_RIGHT)
    {
        return gcCStickX;
    }

    if (negative == GC_INPUT_CSTICK_DOWN &&
        positive == GC_INPUT_CSTICK_UP)
    {
        return gcCStickY;
    }

    return 0;
}


/*
 * Return a proportional -127..127 movement value for a pair of
 * actions bound to opposite directions of one physical analogue axis.
 *
 * The physical deadzone is removed and the remaining range is
 * rescaled so that:
 *
 *     deadzone edge -> 0
 *     full deflection -> 127
 *
 * This is intended for forward/backward and strafing. Turning has
 * its own sensitivity-scaled function below.
 */
int GC_ControlsAnalogMovement(
    gc_action_t negative_action,
    gc_action_t positive_action)
{
    gc_input_t negative;
    gc_input_t positive;
    int value;
    int magnitude;
    int deadzone;
    int maximum;

    if (gcCapturing ||
        gcCaptureReleaseWait)
    {
        return 0;
    }

    /*
     * Z owns the controller movement inputs while the automap
     * layer is active.
     */
    if ((gcHeld & PAD_TRIGGER_Z) != 0)
    {
        return 0;
    }

    if (!GC_ControlHasAnalogAxis(
            negative_action,
            positive_action))
    {
        return 0;
    }

    negative =
        gcBindings[negative_action];

    positive =
        gcBindings[positive_action];

    value =
        GC_ControlAnalogAxis(
            negative_action,
            positive_action);

    if ((negative == GC_INPUT_STICK_LEFT &&
         positive == GC_INPUT_STICK_RIGHT) ||
        (negative == GC_INPUT_STICK_DOWN &&
         positive == GC_INPUT_STICK_UP))
    {
        deadzone = GC_STICK_DEADZONE;

        /*
         * Real GameCube main sticks do not normally reach the
         * signed 8-bit theoretical maximum of 127.
         *
         * Testing showed cardinal maxima around 97..112.
         * Treat 100 as full intended deflection so Doom can
         * reach full movement speed on real controllers.
         */
        maximum = 100;
    }
    else
    {
        deadzone = GC_CSTICK_DEADZONE;
        maximum = 127;
    }

    magnitude = abs(value);

    if (magnitude <= deadzone)
    {
        return 0;
    }

    /*
     * Clamp physical values beyond the nominal full-deflection
     * point, then remove the deadzone and rescale to 0..127.
     */
    if (magnitude > maximum)
    {
        magnitude = maximum;
    }

    magnitude =
        (magnitude - deadzone) * 127 /
        (maximum - deadzone);

    if (value < 0)
    {
        magnitude = -magnitude;
    }

    return magnitude;
}

int GC_ControlsAnalogTurn(void)
{
    int value;
    int magnitude;
    int deadzone;
    int scaled;

    if (gcCapturing ||
        gcCaptureReleaseWait)
    {
        return 0;
    }

    /*
     * Z owns the C-stick while the automap layer is active.
     */
    if ((gcHeld & PAD_TRIGGER_Z) != 0)
    {
        return 0;
    }

    if (!GC_ControlHasAnalogAxis(
            GC_ACTION_MOVE_LEFT,
            GC_ACTION_MOVE_RIGHT))
    {
        return 0;
    }

    value =
        GC_ControlAnalogAxis(
            GC_ACTION_MOVE_LEFT,
            GC_ACTION_MOVE_RIGHT);

    if (gcBindings[GC_ACTION_MOVE_LEFT] ==
            GC_INPUT_STICK_LEFT &&
        gcBindings[GC_ACTION_MOVE_RIGHT] ==
            GC_INPUT_STICK_RIGHT)
    {
        deadzone = GC_STICK_DEADZONE;
    }
    else
    {
        deadzone = GC_CSTICK_DEADZONE;
    }

    magnitude = abs(value);

    if (magnitude <= deadzone)
    {
        return 0;
    }

    /*
     * Remove the deadzone and rescale the remaining physical
     * range back to 0..127.
     */
    magnitude =
        (magnitude - deadzone) * 127 /
        (127 - deadzone);

    /*
     * At 100 percent sensitivity, full stick deflection gives
     * 2048 angleturn units per tic.
     */
    scaled =
        magnitude *
        GC_ANALOG_TURN_MAX *
        gcTurnSensitivity /
        (127 * 100);

    if (value < 0)
    {
        scaled = -scaled;
    }

    return scaled;
}


int GC_ControlsGetTurnSensitivity(void)
{
    return gcTurnSensitivity;
}


void GC_ControlsSetTurnSensitivity(
    int sensitivity)
{
    if (sensitivity < GC_TURN_SENSITIVITY_MIN)
    {
        sensitivity = GC_TURN_SENSITIVITY_MIN;
    }

    if (sensitivity > GC_TURN_SENSITIVITY_MAX)
    {
        sensitivity = GC_TURN_SENSITIVITY_MAX;
    }

    gcTurnSensitivity = sensitivity;
}


/* ------------------------------------------------------------------------- */
/* Fixed controls                                                            */
/* ------------------------------------------------------------------------- */

bool GC_MenuStartHeld(void)
{
    if (gcCaptureReleaseWait)
        return false;

    return
        (gcHeld & PAD_BUTTON_START) != 0;
}


bool GC_MenuMainStickUpHeld(void)
{
    if (gcCapturing ||
        gcCaptureReleaseWait)
    {
        return false;
    }

    return
        gcStickY > GC_STICK_DEADZONE;
}


bool GC_MenuMainStickDownHeld(void)
{
    if (gcCapturing ||
        gcCaptureReleaseWait)
    {
        return false;
    }

    return
        gcStickY < -GC_STICK_DEADZONE;
}


bool GC_MenuMainStickLeftHeld(void)
{
    if (gcCapturing ||
        gcCaptureReleaseWait)
    {
        return false;
    }

    return
        gcStickX < -GC_STICK_DEADZONE;
}


bool GC_MenuMainStickRightHeld(void)
{
    if (gcCapturing ||
        gcCaptureReleaseWait)
    {
        return false;
    }

    return
        gcStickX > GC_STICK_DEADZONE;
}


bool GC_MapModifierHeld(void)
{
    return
        (gcHeld & PAD_TRIGGER_Z) != 0;
}


bool GC_MapNorthHeld(void)
{
    return
        abs(gcCStickY) > GC_MAP_CSTICK_DEADZONE &&
        gcCStickY > 0;
}


bool GC_MapSouthHeld(void)
{
    return
        abs(gcCStickY) > GC_MAP_CSTICK_DEADZONE &&
        gcCStickY < 0;
}


bool GC_MapEastHeld(void)
{
    return
        abs(gcCStickX) > GC_MAP_CSTICK_DEADZONE &&
        gcCStickX > 0;
}


bool GC_MapWestHeld(void)
{
    return
        abs(gcCStickX) > GC_MAP_CSTICK_DEADZONE &&
        gcCStickX < 0;
}


bool GC_MapZoomInHeld(void)
{
    return
        (gcHeld & PAD_BUTTON_UP) != 0;
}


bool GC_MapZoomOutHeld(void)
{
    return
        (gcHeld & PAD_BUTTON_DOWN) != 0;
}


bool GC_MapMaxZoomHeld(void)
{
    return
        (gcHeld & PAD_BUTTON_LEFT) != 0;
}


bool GC_MapFollowHeld(void)
{
    return
        (gcHeld & PAD_BUTTON_RIGHT) != 0;
}


bool GC_MapMarkHeld(void)
{
    return
        (gcHeld & PAD_BUTTON_A) != 0;
}


bool GC_MapClearMarkHeld(void)
{
    return
        (gcHeld & PAD_BUTTON_B) != 0;
}


bool GC_MapGridHeld(void)
{
    return
        (gcHeld & PAD_BUTTON_X) != 0;
}


/* ------------------------------------------------------------------------- */
/* Binding access                                                            */
/* ------------------------------------------------------------------------- */

gc_input_t GC_ControlsGetBinding(
    gc_action_t action)
{
    if (action < 0 ||
        action >= GC_ACTION_COUNT)
    {
        return GC_INPUT_NONE;
    }

    return
        (gc_input_t)gcBindings[action];
}


void GC_ControlsSetBinding(
    gc_action_t action,
    gc_input_t input)
{
    if (action < 0 ||
        action >= GC_ACTION_COUNT)
    {
        return;
    }

    if (input < GC_INPUT_NONE ||
        input >= GC_INPUT_COUNT)
    {
        return;
    }

    gcBindings[action] =
        input;
}

void GC_ControlsBindConfig(void)
{
    M_BindVariable(
        "gc_move_up",
        &gcBindings[GC_ACTION_MOVE_UP]);

    M_BindVariable(
        "gc_move_down",
        &gcBindings[GC_ACTION_MOVE_DOWN]);

    M_BindVariable(
        "gc_move_left",
        &gcBindings[GC_ACTION_MOVE_LEFT]);

    M_BindVariable(
        "gc_move_right",
        &gcBindings[GC_ACTION_MOVE_RIGHT]);

    M_BindVariable(
        "gc_strafe_left",
        &gcBindings[GC_ACTION_STRAFE_LEFT]);

    M_BindVariable(
        "gc_strafe_right",
        &gcBindings[GC_ACTION_STRAFE_RIGHT]);

    M_BindVariable(
        "gc_fire",
        &gcBindings[GC_ACTION_FIRE]);

    M_BindVariable(
        "gc_use",
        &gcBindings[GC_ACTION_USE]);

    M_BindVariable(
        "gc_run",
        &gcBindings[GC_ACTION_RUN]);

    M_BindVariable(
        "gc_next_weapon",
        &gcBindings[GC_ACTION_NEXT_WEAPON]);

    M_BindVariable(
        "gc_prev_weapon",
        &gcBindings[GC_ACTION_PREV_WEAPON]);

    M_BindVariable(
        "gc_menu_confirm",
        &gcBindings[GC_ACTION_MENU_CONFIRM]);

    M_BindVariable(
        "gc_menu_back",
        &gcBindings[GC_ACTION_MENU_BACK]);

    M_BindVariable(
        "gc_turn_sensitivity",
        &gcTurnSensitivity);
}


void GC_ControlsResetDefaults(void)
{
    gcTurnSensitivity =
        GC_TURN_SENSITIVITY_DEFAULT;

    gcBindings[GC_ACTION_MOVE_UP] =
        GC_INPUT_STICK_UP;

    gcBindings[GC_ACTION_MOVE_DOWN] =
        GC_INPUT_STICK_DOWN;

    gcBindings[GC_ACTION_MOVE_LEFT] =
        GC_INPUT_STICK_LEFT;

    gcBindings[GC_ACTION_MOVE_RIGHT] =
        GC_INPUT_STICK_RIGHT;

    gcBindings[GC_ACTION_STRAFE_LEFT] =
        GC_INPUT_CSTICK_LEFT;

    gcBindings[GC_ACTION_STRAFE_RIGHT] =
        GC_INPUT_CSTICK_RIGHT;

    gcBindings[GC_ACTION_FIRE] =
        GC_INPUT_R;

    gcBindings[GC_ACTION_USE] =
        GC_INPUT_A;

    gcBindings[GC_ACTION_RUN] =
        GC_INPUT_L;

    gcBindings[GC_ACTION_NEXT_WEAPON] =
        GC_INPUT_X;

    gcBindings[GC_ACTION_PREV_WEAPON] =
        GC_INPUT_Y;

    gcBindings[GC_ACTION_MENU_CONFIRM] =
        GC_INPUT_A;

    gcBindings[GC_ACTION_MENU_BACK] =
        GC_INPUT_B;
}


const char *GC_ControlsInputName(
    gc_input_t input)
{
    switch (input)
    {
        case GC_INPUT_A:
            return "A";

        case GC_INPUT_B:
            return "B";

        case GC_INPUT_X:
            return "X";

        case GC_INPUT_Y:
            return "Y";

        case GC_INPUT_L:
            return "L";

        case GC_INPUT_R:
            return "R";

        case GC_INPUT_DPAD_UP:
            return "D-UP";

        case GC_INPUT_DPAD_DOWN:
            return "D-DOWN";

        case GC_INPUT_DPAD_LEFT:
            return "D-LEFT";

        case GC_INPUT_DPAD_RIGHT:
            return "D-RIGHT";

        case GC_INPUT_STICK_UP:
            return "STICK UP";

        case GC_INPUT_STICK_DOWN:
            return "STICK DOWN";

        case GC_INPUT_STICK_LEFT:
            return "STICK LEFT";

        case GC_INPUT_STICK_RIGHT:
            return "STICK RIGHT";

        case GC_INPUT_CSTICK_UP:
            return "C UP";

        case GC_INPUT_CSTICK_DOWN:
            return "C DOWN";

        case GC_INPUT_CSTICK_LEFT:
            return "C LEFT";

        case GC_INPUT_CSTICK_RIGHT:
            return "C RIGHT";

        case GC_INPUT_NONE:
        default:
            return "NONE";
    }
}


/* ------------------------------------------------------------------------- */
/* Capture API                                                               */
/* ------------------------------------------------------------------------- */

void GC_ControlsBeginCapture(
    gc_action_t action)
{
    if (action < 0 ||
        action >= GC_ACTION_COUNT)
    {
        return;
    }

    gcCaptureAction =
        action;

    gcCapturing =
        true;

    gcCaptureArmed =
        false;

    gcCaptureReleaseWait =
        false;
}


void GC_ControlsCancelCapture(void)
{
    gcCapturing =
        false;

    gcCaptureArmed =
        false;
}


bool GC_ControlsIsCapturing(void)
{
    return
        gcCapturing;
}


gc_action_t GC_ControlsCaptureAction(void)
{
    return
        gcCaptureAction;
}
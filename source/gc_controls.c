#include "gc_controls.h"

#include <gccore.h>

#include <stdlib.h>


#define GC_STICK_DEADZONE       24
#define GC_CSTICK_DEADZONE      24
#define GC_MAP_CSTICK_DEADZONE  40
#define GC_TRIGGER_THRESHOLD    40


/* ------------------------------------------------------------------------- */
/* Raw controller state                                                      */
/* ------------------------------------------------------------------------- */

static u16 gcHeld;

static s8 gcStickX;
static s8 gcStickY;

static s8 gcCStickX;
static s8 gcCStickY;

static u8 gcTriggerR;


/* ------------------------------------------------------------------------- */
/* Bindings                                                                  */
/* ------------------------------------------------------------------------- */

static gc_input_t gcBindings[GC_ACTION_COUNT] =
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

void GC_ControlsPoll(void)
{
    PAD_ScanPads();

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
     * Consume the input that completed the binding.
     * It must not also operate the menu this frame.
     */
    gcHeld = 0;
    gcStickX = 0;
    gcStickY = 0;
    gcCStickX = 0;
    gcCStickY = 0;
    gcTriggerR = 0;
}
    }
}


/* ------------------------------------------------------------------------- */
/* Remappable actions                                                        */
/* ------------------------------------------------------------------------- */

bool GC_ControlHeld(
    gc_action_t action)
{
    if (gcCapturing)
        return false;

    if (action < 0 ||
        action >= GC_ACTION_COUNT)
    {
        return false;
    }

    return
        GC_InputHeld(
            gcBindings[action]);
}


/* ------------------------------------------------------------------------- */
/* Fixed controls                                                            */
/* ------------------------------------------------------------------------- */

bool GC_MenuStartHeld(void)
{
    return
        (gcHeld & PAD_BUTTON_START) != 0;
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
        gcBindings[action];
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


void GC_ControlsResetDefaults(void)
{
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
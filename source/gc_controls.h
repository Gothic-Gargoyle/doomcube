#ifndef DOOMCUBE_GC_CONTROLS_H
#define DOOMCUBE_GC_CONTROLS_H

#include <stdbool.h>


typedef enum
{
    GC_INPUT_NONE = 0,

    GC_INPUT_A,
    GC_INPUT_B,
    GC_INPUT_X,
    GC_INPUT_Y,

    GC_INPUT_L,
    GC_INPUT_R,

    GC_INPUT_DPAD_UP,
    GC_INPUT_DPAD_DOWN,
    GC_INPUT_DPAD_LEFT,
    GC_INPUT_DPAD_RIGHT,

    GC_INPUT_STICK_UP,
    GC_INPUT_STICK_DOWN,
    GC_INPUT_STICK_LEFT,
    GC_INPUT_STICK_RIGHT,

    GC_INPUT_CSTICK_UP,
    GC_INPUT_CSTICK_DOWN,
    GC_INPUT_CSTICK_LEFT,
    GC_INPUT_CSTICK_RIGHT,

    GC_INPUT_COUNT
} gc_input_t;


typedef enum
{
    GC_ACTION_MOVE_UP = 0,
    GC_ACTION_MOVE_DOWN,
    GC_ACTION_MOVE_LEFT,
    GC_ACTION_MOVE_RIGHT,

    GC_ACTION_STRAFE_LEFT,
    GC_ACTION_STRAFE_RIGHT,

    GC_ACTION_FIRE,
    GC_ACTION_USE,
    GC_ACTION_RUN,

    GC_ACTION_NEXT_WEAPON,
    GC_ACTION_PREV_WEAPON,

    GC_ACTION_MENU_CONFIRM,
    GC_ACTION_MENU_BACK,

    GC_ACTION_COUNT
} gc_action_t;


/*
 * Poll controller 1 once for the current frame.
 */
void GC_ControlsPoll(void);


/*
 * Remappable gameplay/menu actions.
 */
bool GC_ControlHeld(
    gc_action_t action);

gc_input_t GC_ControlsGetBinding(
    gc_action_t action);

void GC_ControlsSetBinding(
    gc_action_t action,
    gc_input_t input);

void GC_ControlsResetDefaults(void);

const char *GC_ControlsInputName(
    gc_input_t input);


/*
 * Fixed GameCube controls.
 *
 * Start and the entire Z/automap layer are intentionally
 * not remappable.
 */
bool GC_MenuStartHeld(void);

bool GC_MapModifierHeld(void);

bool GC_MapNorthHeld(void);
bool GC_MapSouthHeld(void);
bool GC_MapEastHeld(void);
bool GC_MapWestHeld(void);

bool GC_MapZoomInHeld(void);
bool GC_MapZoomOutHeld(void);
bool GC_MapMaxZoomHeld(void);
bool GC_MapFollowHeld(void);

bool GC_MapMarkHeld(void);
bool GC_MapClearMarkHeld(void);
bool GC_MapGridHeld(void);


/*
 * Binding capture support for the controls menu.
 */
void GC_ControlsBeginCapture(
    gc_action_t action);

void GC_ControlsCancelCapture(void);

bool GC_ControlsIsCapturing(void);

gc_action_t GC_ControlsCaptureAction(void);

#endif
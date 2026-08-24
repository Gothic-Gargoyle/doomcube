#include "gc_controls.h"

#include <gccore.h>

#include <stdlib.h>
#include <string.h>

#define GC_STICK_DEADZONE       24
#define GC_CSTICK_DEADZONE      24
#define GC_MAP_CSTICK_DEADZONE  40
#define GC_TRIGGER_THRESHOLD    40

void GC_ControlsPoll(gc_control_state_t *state)
{
    u16 held;

    s8 stickX;
    s8 stickY;

    s8 cstickX;
    s8 cstickY;

    u8 triggerR;

    if (!state)
        return;

    memset(
        state,
        0,
        sizeof(*state));

    PAD_ScanPads();

    held = PAD_ButtonsHeld(0);

    stickX = PAD_StickX(0);
    stickY = PAD_StickY(0);

    cstickX = PAD_SubStickX(0);
    cstickY = PAD_SubStickY(0);

    triggerR = PAD_TriggerR(0);

    state->moveUp =
        stickY > GC_STICK_DEADZONE;

    state->moveDown =
        stickY < -GC_STICK_DEADZONE;

    state->moveLeft =
        stickX < -GC_STICK_DEADZONE;

    state->moveRight =
        stickX > GC_STICK_DEADZONE;

    state->mapModifier =
        (held & PAD_TRIGGER_Z) != 0;

    state->fire =
        triggerR > GC_TRIGGER_THRESHOLD;

    state->run =
        (held & PAD_TRIGGER_L) != 0;

    state->menuStart =
        (held & PAD_BUTTON_START) != 0;

    if (!state->mapModifier)
    {
        state->strafeLeft =
            cstickX < -GC_CSTICK_DEADZONE;

        state->strafeRight =
            cstickX > GC_CSTICK_DEADZONE;

        state->use =
            (held & PAD_BUTTON_A) != 0;

        state->menuConfirm =
            (held & PAD_BUTTON_A) != 0;

        state->menuBack =
            (held & PAD_BUTTON_B) != 0;

        state->nextWeapon =
            (held & PAD_BUTTON_X) != 0;

        state->prevWeapon =
            (held & PAD_BUTTON_Y) != 0;

        return;
    }

    if (abs(cstickX) > GC_MAP_CSTICK_DEADZONE)
    {
        state->mapEast =
            cstickX > 0;

        state->mapWest =
            cstickX < 0;
    }

    if (abs(cstickY) > GC_MAP_CSTICK_DEADZONE)
    {
        state->mapNorth =
            cstickY > 0;

        state->mapSouth =
            cstickY < 0;
    }

    state->mapZoomIn =
        (held & PAD_BUTTON_UP) != 0;

    state->mapZoomOut =
        (held & PAD_BUTTON_DOWN) != 0;

    state->mapMaxZoom =
        (held & PAD_BUTTON_LEFT) != 0;

    state->mapFollow =
        (held & PAD_BUTTON_RIGHT) != 0;

    state->mapMark =
        (held & PAD_BUTTON_A) != 0;

    state->mapClearMark =
        (held & PAD_BUTTON_B) != 0;

    state->mapGrid =
        (held & PAD_BUTTON_X) != 0;
}
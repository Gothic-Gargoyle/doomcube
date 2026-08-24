#ifndef DOOMCUBE_GC_CONTROLS_H
#define DOOMCUBE_GC_CONTROLS_H

#include <stdbool.h>

typedef struct
{
    bool moveUp;
    bool moveDown;
    bool moveLeft;
    bool moveRight;

    bool strafeLeft;
    bool strafeRight;

    bool fire;
    bool use;
    bool run;

    bool nextWeapon;
    bool prevWeapon;

    bool menuConfirm;
    bool menuBack;
    bool menuStart;

    bool mapModifier;

    bool mapNorth;
    bool mapSouth;
    bool mapEast;
    bool mapWest;

    bool mapZoomIn;
    bool mapZoomOut;
    bool mapMaxZoom;
    bool mapFollow;

    bool mapMark;
    bool mapClearMark;
    bool mapGrid;
} gc_control_state_t;

void GC_ControlsPoll(gc_control_state_t *state);

#endif
#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>

#include "doomkeys.h"
#include "doomgeneric.h"
#include "m_controls.h"

#include "gc_memcard.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ogcsys.h>
#include <gccore.h>

#include <iso9660.h>
#include <ogc/dvd.h>

#define KEYQUEUE_SIZE 64
#define STICK_DEADZONE 24
#define CSTICK_DEADZONE 24
#define MAP_CSTICK_DEADZONE 40
#define TRIGGER_THRESHOLD 40

#define GC_KEY_BASE 0xd0

#define GC_KEY_PREVWEAPON    (GC_KEY_BASE + 0)
#define GC_KEY_NEXTWEAPON    (GC_KEY_BASE + 1)
#define GC_KEY_MENU_ENTER    (GC_KEY_BASE + 2)
#define GC_KEY_MENU_BACK     (GC_KEY_BASE + 3)
#define GC_KEY_MENU_CONFIRM  (GC_KEY_BASE + 4)
#define GC_KEY_MENU_ABORT    (GC_KEY_BASE + 5)

#define GC_KEY_MAP_TOGGLE    (GC_KEY_BASE + 16)
#define GC_KEY_MAP_NORTH     (GC_KEY_BASE + 17)
#define GC_KEY_MAP_SOUTH     (GC_KEY_BASE + 18)
#define GC_KEY_MAP_EAST      (GC_KEY_BASE + 19)
#define GC_KEY_MAP_WEST      (GC_KEY_BASE + 20)
#define GC_KEY_MAP_ZOOMIN    (GC_KEY_BASE + 21)
#define GC_KEY_MAP_ZOOMOUT   (GC_KEY_BASE + 22)
#define GC_KEY_MAP_MAXZOOM   (GC_KEY_BASE + 23)
#define GC_KEY_MAP_FOLLOW    (GC_KEY_BASE + 24)
#define GC_KEY_MAP_GRID      (GC_KEY_BASE + 25)
#define GC_KEY_MAP_MARK      (GC_KEY_BASE + 26)
#define GC_KEY_MAP_CLEARMARK (GC_KEY_BASE + 27)

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

static unsigned short keyQueue[KEYQUEUE_SIZE];

static unsigned int keyWrite;
static unsigned int keyRead;

static int gcUp;
static int gcDown;
static int gcLeft;
static int gcRight;

static int gcFire;
static int gcUse;
static int gcRun;

static int gcEnter;
static int gcBack;
static int gcEscape;

static int gcZHeld;
static int gcZUsed;

static int gcMapNorth;
static int gcMapSouth;
static int gcMapEast;
static int gcMapWest;
static int gcMapZoomIn;
static int gcMapZoomOut;
static int gcMapMaxZoom;
static int gcMapFollow;
static int gcMapGrid;
static int gcMapMark;
static int gcMapClearMark;

static int gcConfirm;
static int gcAbort;

static int gcStrafeLeft;
static int gcStrafeRight;

static int gcPrevWeapon;
static int gcNextWeapon;

static int gcRumbleFrames;
static int gcRumbleOnFrames;
static int gcRumbleOffFrames;
static int gcRumblePulsesLeft;

static bool gcRumbleOn;
static bool gcRumbleHardStop;

static bool dvdMounted;

/* ------------------------------------------------------------------------- */
/* Key queue                                                                 */
/* ------------------------------------------------------------------------- */

static void queueKey(int pressed, unsigned char key)
{
    unsigned int next = (keyWrite + 1) % KEYQUEUE_SIZE;

    if (next == keyRead)
        keyRead = (keyRead + 1) % KEYQUEUE_SIZE;

    keyQueue[keyWrite] = ((pressed ? 1 : 0) << 8) | key;
    keyWrite = next;
}

static void setKeyState(int wanted, int *state, unsigned char key)
{
    wanted = !!wanted;

    if (*state == wanted)
        return;

    *state = wanted;
    queueKey(wanted, key);
}

/* ------------------------------------------------------------------------- */
/* GameCube controller                                                       */
/* ------------------------------------------------------------------------- */

static void handleGameCubeInput(void)
{
    PAD_ScanPads();

    u16 held = PAD_ButtonsHeld(0);

    s8 stickX = PAD_StickX(0);
    s8 stickY = PAD_StickY(0);

    s8 cstickX = PAD_SubStickX(0);
    s8 cstickY = PAD_SubStickY(0);

    u8 triggerR = PAD_TriggerR(0);

    int up = stickY > STICK_DEADZONE;

    int down = stickY < -STICK_DEADZONE;

    int left = stickX < -STICK_DEADZONE;

    int right = stickX > STICK_DEADZONE;

    int zHeld = !!(held & PAD_TRIGGER_Z);

    int cLeft = !zHeld && cstickX < -CSTICK_DEADZONE;
    int cRight = !zHeld && cstickX > CSTICK_DEADZONE;

    int fire = triggerR > TRIGGER_THRESHOLD;


/*
     * Z acts as an automap modifier.
     *
     * Tapping Z by itself toggles the automap.
     * Using another automap control while Z is held suppresses
     * the toggle when Z is released.
*/
    if (zHeld && !gcZHeld)
    {
        gcZHeld = 1;
        gcZUsed = 0;
    }

    if (zHeld)
    {
        int mapNorth = 0;
        int mapSouth = 0;
        int mapEast =  0;
        int mapWest =  0;

        if (abs(cstickX) > MAP_CSTICK_DEADZONE)
        {
            mapEast = cstickX > 0;
            mapWest = cstickX < 0;
        }

        if (abs(cstickY) > MAP_CSTICK_DEADZONE)
        {
            mapNorth = cstickY > 0;
            mapSouth = cstickY < 0;
        }

        int zoomIn = !!(held & PAD_BUTTON_UP);
        int zoomOut = !!(held & PAD_BUTTON_DOWN);
        int maxZoom = !!(held & PAD_BUTTON_LEFT);
        int follow = !!(held & PAD_BUTTON_RIGHT);

        int mark = !!(held & PAD_BUTTON_A);
        int clearMark = !!(held & PAD_BUTTON_B);
        int grid = !!(held & PAD_BUTTON_X);

        if (mapNorth || mapSouth || mapEast || mapWest ||
            zoomIn || zoomOut || maxZoom || follow ||
            mark || clearMark || grid)
        {
            gcZUsed = 1;
        }

        setKeyState(mapNorth, &gcMapNorth, GC_KEY_MAP_NORTH);
        setKeyState(mapSouth, &gcMapSouth, GC_KEY_MAP_SOUTH);
        setKeyState(mapEast, &gcMapEast, GC_KEY_MAP_EAST);
        setKeyState(mapWest, &gcMapWest, GC_KEY_MAP_WEST);

        setKeyState(zoomIn, &gcMapZoomIn, GC_KEY_MAP_ZOOMIN);
        setKeyState(zoomOut, &gcMapZoomOut, GC_KEY_MAP_ZOOMOUT);
        setKeyState(maxZoom, &gcMapMaxZoom, GC_KEY_MAP_MAXZOOM);
        setKeyState(follow, &gcMapFollow, GC_KEY_MAP_FOLLOW);

        setKeyState(grid, &gcMapGrid, GC_KEY_MAP_GRID);
        setKeyState(mark, &gcMapMark, GC_KEY_MAP_MARK);
        setKeyState(clearMark, &gcMapClearMark, GC_KEY_MAP_CLEARMARK);
    }
    else
    {
        setKeyState(0, &gcMapNorth, GC_KEY_MAP_NORTH);
        setKeyState(0, &gcMapSouth, GC_KEY_MAP_SOUTH);
        setKeyState(0, &gcMapEast, GC_KEY_MAP_EAST);
        setKeyState(0, &gcMapWest, GC_KEY_MAP_WEST);

        setKeyState(0, &gcMapZoomIn, GC_KEY_MAP_ZOOMIN);
        setKeyState(0, &gcMapZoomOut, GC_KEY_MAP_ZOOMOUT);
        setKeyState(0, &gcMapMaxZoom, GC_KEY_MAP_MAXZOOM);
        setKeyState(0, &gcMapFollow, GC_KEY_MAP_FOLLOW);

        setKeyState(0, &gcMapGrid, GC_KEY_MAP_GRID);
        setKeyState(0, &gcMapMark, GC_KEY_MAP_MARK);
        setKeyState(0, &gcMapClearMark, GC_KEY_MAP_CLEARMARK);

        if (gcZHeld)
        {
            if (!gcZUsed)
            {
                queueKey(1, GC_KEY_MAP_TOGGLE);
                queueKey(0, GC_KEY_MAP_TOGGLE);
            }

            gcZHeld = 0;
            gcZUsed = 0;
        }
    }

    setKeyState(
        up,
        &gcUp,
        KEY_UPARROW);

    setKeyState(
        down,
        &gcDown,
        KEY_DOWNARROW);

    setKeyState(
        left,
        &gcLeft,
        KEY_LEFTARROW);

    setKeyState(
        right,
        &gcRight,
        KEY_RIGHTARROW);

    setKeyState(
        cLeft,
        &gcStrafeLeft,
        KEY_STRAFE_L);

    setKeyState(
        cRight,
        &gcStrafeRight,
        KEY_STRAFE_R);

    setKeyState(
        fire,
        &gcFire,
        KEY_FIRE);

    setKeyState(
        !zHeld && (held & PAD_BUTTON_X),
        &gcNextWeapon,
        key_nextweapon);

    setKeyState(
        !zHeld && (held & PAD_BUTTON_Y),
        &gcPrevWeapon,
        key_prevweapon);


    setKeyState(
        held & PAD_TRIGGER_L,
        &gcRun,
        KEY_RSHIFT);

 /* Various things the a button does*/
    setKeyState(
        !zHeld && (held & PAD_BUTTON_A),
        &gcUse,
        KEY_USE);
    
    setKeyState(
        !zHeld && (held & PAD_BUTTON_A),
        &gcConfirm,
        GC_KEY_MENU_CONFIRM);

    setKeyState(
        !zHeld && (held & PAD_BUTTON_A),
        &gcEnter,
        GC_KEY_MENU_ENTER);

 /* Various things the b button does*/
    setKeyState(
        !zHeld && (held & PAD_BUTTON_B),
        &gcBack,
        GC_KEY_MENU_BACK);
    
    setKeyState(
        !zHeld && (held & PAD_BUTTON_B),
        &gcAbort,
        GC_KEY_MENU_ABORT);

    setKeyState(
        held & PAD_BUTTON_START,
        &gcEscape,
        KEY_ESCAPE);

    if (gcRumbleFrames > 0)
    {
        gcRumbleFrames--;

        if (gcRumbleFrames == 0)
        {
            if (gcRumbleOn)
            {
                gcRumblePulsesLeft--;

                if (gcRumblePulsesLeft <= 0)
                {
                    PAD_ControlMotor(
                        PAD_CHAN0,
                        gcRumbleHardStop
                            ? PAD_MOTOR_STOP_HARD
                            : PAD_MOTOR_STOP);

                    gcRumbleOn = false;
                }
                else if (gcRumbleOffFrames > 0)
                {
                    PAD_ControlMotor(
                        PAD_CHAN0,
                        PAD_MOTOR_STOP);

                    gcRumbleOn = false;
                    gcRumbleFrames = gcRumbleOffFrames;
                }
                else
                {
                    gcRumbleFrames = gcRumbleOnFrames;
                }
            }
            else
            {
                PAD_ControlMotor(
                    PAD_CHAN0,
                    PAD_MOTOR_RUMBLE);

                gcRumbleOn = true;
                gcRumbleFrames = gcRumbleOnFrames;
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* ISO9660                                                                   */
/* ------------------------------------------------------------------------- */

static bool mountIsoFilesystem(void)
{
    printf("DoomCube: mounting ISO9660...\n");

    if (!ISO9660_Mount("dvd", &__io_gcdvd))
    {
        printf("DoomCube: ISO9660_Mount FAILED\n");
        return false;
    }

    dvdMounted = true;

    printf("DoomCube: mounted dvd:/\n");

    return true;
}

/* ------------------------------------------------------------------------- */
/* DoomGeneric                                                               */
/* ------------------------------------------------------------------------- */

void DG_Init(void)
{
    PAD_Init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        printf(
            "SDL_Init failed: %s\n",
            SDL_GetError());

        exit(1);
    }

    window = SDL_CreateWindow(
        "DOOM",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        DOOMGENERIC_RESX,
        DOOMGENERIC_RESY,
        SDL_WINDOW_SHOWN);

    if (!window)
    {
        printf(
            "SDL_CreateWindow failed: %s\n",
            SDL_GetError());

        exit(1);
    }

    renderer = SDL_CreateRenderer(
        window,
        -1,
        0);

    if (!renderer)
    {
        printf(
            "SDL_CreateRenderer failed: %s\n",
            SDL_GetError());

        exit(1);
    }

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB888,
        SDL_TEXTUREACCESS_STREAMING,
        DOOMGENERIC_RESX,
        DOOMGENERIC_RESY);

    if (!texture)
    {
        printf(
            "SDL_CreateTexture failed: %s\n",
            SDL_GetError());

        exit(1);
    }

    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    if (!mountIsoFilesystem())
    {
        printf(
            "DoomCube: disc mount unavailable\n");

        exit(1);
    }

    if (!GC_MemoryCardInit())
    {
        SYS_Report(
            "DoomCube: Memory Card unavailable; continuing without saves\n");
    }
}

void DG_DrawFrame(void)
{
    SDL_UpdateTexture(
        texture,
        NULL,
        DG_ScreenBuffer,
        DOOMGENERIC_RESX * sizeof(uint32_t));

    SDL_RenderClear(renderer);

    SDL_RenderCopy(
        renderer,
        texture,
        NULL,
        NULL);

    SDL_RenderPresent(renderer);

    handleGameCubeInput();
}

void DG_SleepMs(uint32_t ms)
{
    SDL_Delay(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return SDL_GetTicks();
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    unsigned short data;

    if (keyRead == keyWrite)
        return 0;

    data = keyQueue[keyRead];

    keyRead =
        (keyRead + 1) % KEYQUEUE_SIZE;

    *pressed = data >> 8;
    *doomKey = data & 0xff;

    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    if (window)
    {
        SDL_SetWindowTitle(
            window,
            title);
    }
}
/* RUMBLE */
void DG_Rumble(int frames)
{
    if (frames <= 0)
        return;

    if (frames > gcRumbleFrames)
        gcRumbleFrames = frames;

    /*
     * Do not let a weaker single pulse shorten a longer
     * single pulse already in progress.
     */
    if (gcRumbleOn &&
        gcRumblePulsesLeft == 1 &&
        frames <= gcRumbleFrames)
    {
        return;
    }

    DG_RumblePattern(
        frames,
        0,
        1,
        true);
}

void DG_RumblePattern(
    int onFrames,
    int offFrames,
    int pulses,
    bool hardStop)
{
    if (onFrames <= 0 || pulses <= 0)
        return;

    if (offFrames < 0)
        offFrames = 0;

    gcRumbleOnFrames = onFrames;
    gcRumbleOffFrames = offFrames;
    gcRumblePulsesLeft = pulses;

    gcRumbleFrames = onFrames;

    gcRumbleOn = true;
    gcRumbleHardStop = hardStop;

    PAD_ControlMotor(
        PAD_CHAN0,
        PAD_MOTOR_RUMBLE);
}
// For damage done to player.
void DG_RumbleDamage(int damage)
{
    int frames;

    if (damage >= 100)
        frames = 60;
    else if (damage >= 50)
        frames = 20;
    else if (damage >= 25)
        frames = 12;
    else if (damage >= 10)
        frames = 9;
    else if (damage > 0)
        frames = 5;
    else
        return;

    DG_Rumble(frames);
}



/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char *doomArgv[] =
        {
            "doomcube",
            "-iwad",
            "dvd:/doom1.wad"};

    doomgeneric_Create(
        3,
        doomArgv);

    key_prevweapon = GC_KEY_PREVWEAPON;
    key_nextweapon = GC_KEY_NEXTWEAPON;
    key_menu_forward = GC_KEY_MENU_ENTER;
    key_menu_back = GC_KEY_MENU_BACK;
    key_menu_confirm = GC_KEY_MENU_CONFIRM;
    key_menu_abort = GC_KEY_MENU_ABORT;
    key_message_refresh = 0;

    key_map_toggle = GC_KEY_MAP_TOGGLE;
    key_map_north = GC_KEY_MAP_NORTH;
    key_map_south = GC_KEY_MAP_SOUTH;
    key_map_east = GC_KEY_MAP_EAST;
    key_map_west = GC_KEY_MAP_WEST;
    key_map_zoomin = GC_KEY_MAP_ZOOMIN;
    key_map_zoomout = GC_KEY_MAP_ZOOMOUT;
    key_map_maxzoom = GC_KEY_MAP_MAXZOOM;
    key_map_follow = GC_KEY_MAP_FOLLOW;
    key_map_grid = GC_KEY_MAP_GRID;
    key_map_mark = GC_KEY_MAP_MARK;
    key_map_clearmark = GC_KEY_MAP_CLEARMARK;

    while (SYS_MainLoop())
    {
        doomgeneric_Tick();
    }

    /*
     * Explicitly stop the controller motor when leaving the main loop.
     */
    PAD_ControlMotor(
        PAD_CHAN0,
        PAD_MOTOR_STOP);

    GC_MemoryCardShutdown();

    if (dvdMounted)
    {
        ISO9660_Unmount(
            "dvd");
    }

    return 0;
}
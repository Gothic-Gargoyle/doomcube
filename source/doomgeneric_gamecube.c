#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>

#include "gc_debug.h"

#include "doomkeys.h"
#include "doomgeneric.h"
#include "m_controls.h"

#include "gc_launcher.h"
#include "gc_memcard.h"
#include "m_menu.h"
#include "gc_controls.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ogcsys.h>
#include <gccore.h>

#include "gc_dvd_fst.h"
#include <ogc/dvd.h>

#define DOOMGENERIC_RESX 640
#define DOOMGENERIC_RESY 400

#define GC_OUTPUT_WIDTH  640
#define GC_OUTPUT_HEIGHT 480

#define KEYQUEUE_SIZE 64

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

static bool platformInitialized;
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
    int zHeld;

    GC_ControlsPoll();

    zHeld =
        GC_MapModifierHeld();


    /* ------------------------------------------------------------------ */
    /* Z tap / automap modifier                                           */
    /* ------------------------------------------------------------------ */

    if (zHeld &&
        !gcZHeld)
    {
        gcZHeld = 1;
        gcZUsed = 0;
    }


    if (zHeld)
    {
        int mapNorth =
            GC_MapNorthHeld();

        int mapSouth =
            GC_MapSouthHeld();

        int mapEast =
            GC_MapEastHeld();

        int mapWest =
            GC_MapWestHeld();

        int zoomIn =
            GC_MapZoomInHeld();

        int zoomOut =
            GC_MapZoomOutHeld();

        int maxZoom =
            GC_MapMaxZoomHeld();

        int follow =
            GC_MapFollowHeld();

        int mark =
            GC_MapMarkHeld();

        int clearMark =
            GC_MapClearMarkHeld();

        int grid =
            GC_MapGridHeld();


        if (mapNorth ||
            mapSouth ||
            mapEast ||
            mapWest ||
            zoomIn ||
            zoomOut ||
            maxZoom ||
            follow ||
            mark ||
            clearMark ||
            grid)
        {
            gcZUsed = 1;
        }


        setKeyState(
            mapNorth,
            &gcMapNorth,
            GC_KEY_MAP_NORTH);

        setKeyState(
            mapSouth,
            &gcMapSouth,
            GC_KEY_MAP_SOUTH);

        setKeyState(
            mapEast,
            &gcMapEast,
            GC_KEY_MAP_EAST);

        setKeyState(
            mapWest,
            &gcMapWest,
            GC_KEY_MAP_WEST);

        setKeyState(
            zoomIn,
            &gcMapZoomIn,
            GC_KEY_MAP_ZOOMIN);

        setKeyState(
            zoomOut,
            &gcMapZoomOut,
            GC_KEY_MAP_ZOOMOUT);

        setKeyState(
            maxZoom,
            &gcMapMaxZoom,
            GC_KEY_MAP_MAXZOOM);

        setKeyState(
            follow,
            &gcMapFollow,
            GC_KEY_MAP_FOLLOW);

        setKeyState(
            grid,
            &gcMapGrid,
            GC_KEY_MAP_GRID);

        setKeyState(
            mark,
            &gcMapMark,
            GC_KEY_MAP_MARK);

        setKeyState(
            clearMark,
            &gcMapClearMark,
            GC_KEY_MAP_CLEARMARK);
    }
    else
    {
        setKeyState(
            0,
            &gcMapNorth,
            GC_KEY_MAP_NORTH);

        setKeyState(
            0,
            &gcMapSouth,
            GC_KEY_MAP_SOUTH);

        setKeyState(
            0,
            &gcMapEast,
            GC_KEY_MAP_EAST);

        setKeyState(
            0,
            &gcMapWest,
            GC_KEY_MAP_WEST);

        setKeyState(
            0,
            &gcMapZoomIn,
            GC_KEY_MAP_ZOOMIN);

        setKeyState(
            0,
            &gcMapZoomOut,
            GC_KEY_MAP_ZOOMOUT);

        setKeyState(
            0,
            &gcMapMaxZoom,
            GC_KEY_MAP_MAXZOOM);

        setKeyState(
            0,
            &gcMapFollow,
            GC_KEY_MAP_FOLLOW);

        setKeyState(
            0,
            &gcMapGrid,
            GC_KEY_MAP_GRID);

        setKeyState(
            0,
            &gcMapMark,
            GC_KEY_MAP_MARK);

        setKeyState(
            0,
            &gcMapClearMark,
            GC_KEY_MAP_CLEARMARK);


        if (gcZHeld)
        {
            if (!gcZUsed)
            {
                queueKey(
                    1,
                    GC_KEY_MAP_TOGGLE);

                queueKey(
                    0,
                    GC_KEY_MAP_TOGGLE);
            }

            gcZHeld = 0;
            gcZUsed = 0;
        }
    }


    /* ------------------------------------------------------------------ */
    /* Normal remappable controls                                         */
    /* ------------------------------------------------------------------ */

    if (!zHeld)
    {
        /*
         * Analogue forward/backward movement is applied directly
         * to forwardmove by the GameCube-specific ticcmd hook.
         * Only emit Doom key events when this pair is not mapped
         * to a physical analogue axis.
         */
        /*
         * Gameplay uses the main stick as a true analogue axis.
         * Doom's menus still expect ordinary directional key events,
         * so restore digital stick directions while a menu is active.
         */
        /*
         * Menus always use the physical main stick.
         *
         * Gameplay remains remappable and analogue, but menu
         * navigation must never depend on gameplay bindings.
         */
        if (menuactive)
        {
            setKeyState(
                GC_MenuMainStickUpHeld(),
                &gcUp,
                KEY_UPARROW);

            setKeyState(
                GC_MenuMainStickDownHeld(),
                &gcDown,
                KEY_DOWNARROW);
        }
        else if (GC_ControlHasAnalogAxis(
                     GC_ACTION_MOVE_DOWN,
                     GC_ACTION_MOVE_UP))
        {
            setKeyState(0, &gcUp, KEY_UPARROW);
            setKeyState(0, &gcDown, KEY_DOWNARROW);
        }
        else
        {
            setKeyState(
                GC_ControlHeld(GC_ACTION_MOVE_UP),
                &gcUp,
                KEY_UPARROW);

            setKeyState(
                GC_ControlHeld(GC_ACTION_MOVE_DOWN),
                &gcDown,
                KEY_DOWNARROW);
        }

        /*
         * Analogue stick turning is applied directly to angleturn
         * by the GameCube-specific ticcmd hook.  Do not also emit
         * digital arrow keys when MOVE_LEFT / MOVE_RIGHT form an
         * analogue axis.
         */
        /*
         * Same rule horizontally: analogue turning in-game,
         * ordinary left/right navigation in Doom menus.
         */
        /*
         * Menus always use physical main-stick X for left/right.
         *
         * This deliberately ignores MOVE_LEFT / MOVE_RIGHT bindings,
         * so a C-stick gameplay binding cannot operate menu sliders.
         */
        if (menuactive)
        {
            setKeyState(
                GC_MenuMainStickLeftHeld(),
                &gcLeft,
                KEY_LEFTARROW);

            setKeyState(
                GC_MenuMainStickRightHeld(),
                &gcRight,
                KEY_RIGHTARROW);
        }
        else if (GC_ControlHasAnalogAxis(
                     GC_ACTION_MOVE_LEFT,
                     GC_ACTION_MOVE_RIGHT))
        {
            setKeyState(0, &gcLeft, KEY_LEFTARROW);
            setKeyState(0, &gcRight, KEY_RIGHTARROW);
        }
        else
        {
            setKeyState(
                GC_ControlHeld(GC_ACTION_MOVE_LEFT),
                &gcLeft,
                KEY_LEFTARROW);

            setKeyState(
                GC_ControlHeld(GC_ACTION_MOVE_RIGHT),
                &gcRight,
                KEY_RIGHTARROW);
        }

        /*
         * As above, an analogue strafe pair is consumed directly
         * by G_BuildTiccmd(). Digital/remapped bindings continue
         * to use Doom's normal key path.
         */
        if (GC_ControlHasAnalogAxis(
                GC_ACTION_STRAFE_LEFT,
                GC_ACTION_STRAFE_RIGHT))
        {
            setKeyState(0, &gcStrafeLeft, KEY_STRAFE_L);
            setKeyState(0, &gcStrafeRight, KEY_STRAFE_R);
        }
        else
        {
            setKeyState(
                GC_ControlHeld(GC_ACTION_STRAFE_LEFT),
                &gcStrafeLeft,
                KEY_STRAFE_L);

            setKeyState(
                GC_ControlHeld(GC_ACTION_STRAFE_RIGHT),
                &gcStrafeRight,
                KEY_STRAFE_R);
        }

        setKeyState(
            GC_ControlHeld(
                GC_ACTION_FIRE),
            &gcFire,
            KEY_FIRE);

        setKeyState(
            GC_ControlHeld(
                GC_ACTION_USE),
            &gcUse,
            KEY_USE);

        setKeyState(
            GC_ControlHeld(
                GC_ACTION_RUN),
            &gcRun,
            KEY_RSHIFT);

        setKeyState(
            GC_ControlHeld(
                GC_ACTION_NEXT_WEAPON),
            &gcNextWeapon,
            key_nextweapon);

        setKeyState(
            GC_ControlHeld(
                GC_ACTION_PREV_WEAPON),
            &gcPrevWeapon,
            key_prevweapon);

        setKeyState(
            GC_ControlHeld(
                GC_ACTION_MENU_CONFIRM),
            &gcConfirm,
            GC_KEY_MENU_CONFIRM);

        setKeyState(
            GC_ControlHeld(
                GC_ACTION_MENU_CONFIRM),
            &gcEnter,
            GC_KEY_MENU_ENTER);

        setKeyState(
            GC_ControlHeld(
                GC_ACTION_MENU_BACK),
            &gcBack,
            GC_KEY_MENU_BACK);

        setKeyState(
            GC_ControlHeld(
                GC_ACTION_MENU_BACK),
            &gcAbort,
            GC_KEY_MENU_ABORT);
    }
    else
    {
        /*
         * Z owns the normal controls while its automap layer is active.
         * Release anything Doom previously considered held.
         */

        setKeyState(0, &gcUp, KEY_UPARROW);
        setKeyState(0, &gcDown, KEY_DOWNARROW);
        setKeyState(0, &gcLeft, KEY_LEFTARROW);
        setKeyState(0, &gcRight, KEY_RIGHTARROW);

        setKeyState(0, &gcStrafeLeft, KEY_STRAFE_L);
        setKeyState(0, &gcStrafeRight, KEY_STRAFE_R);

        setKeyState(0, &gcFire, KEY_FIRE);
        setKeyState(0, &gcUse, KEY_USE);
        setKeyState(0, &gcRun, KEY_RSHIFT);

        setKeyState(0, &gcNextWeapon, key_nextweapon);
        setKeyState(0, &gcPrevWeapon, key_prevweapon);

        setKeyState(0, &gcConfirm, GC_KEY_MENU_CONFIRM);
        setKeyState(0, &gcEnter, GC_KEY_MENU_ENTER);

        setKeyState(0, &gcBack, GC_KEY_MENU_BACK);
        setKeyState(0, &gcAbort, GC_KEY_MENU_ABORT);
    }


    /* ------------------------------------------------------------------ */
    /* Start is fixed                                                     */
    /* ------------------------------------------------------------------ */

    setKeyState(
        GC_MenuStartHeld(),
        &gcEscape,
        KEY_ESCAPE);


    /* ------------------------------------------------------------------ */
    /* Rumble                                                             */
    /* ------------------------------------------------------------------ */

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

                    gcRumbleFrames =
                        gcRumbleOffFrames;
                }
                else
                {
                    gcRumbleFrames =
                        gcRumbleOnFrames;
                }
            }
            else
            {
                PAD_ControlMotor(
                    PAD_CHAN0,
                    PAD_MOTOR_RUMBLE);

                gcRumbleOn = true;

                gcRumbleFrames =
                    gcRumbleOnFrames;
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* ISO9660                                                                   */
/* ------------------------------------------------------------------------- */

static bool mountIsoFilesystem(void)
{
    DC_DEBUG(
        "DoomCube: mounting native GameCube FST...\n");

    if (!GC_DVDFST_Mount())
    {
        DC_WARN(
            "DoomCube: native FST mount FAILED\n");

        return false;
    }

    dvdMounted = true;

    DC_INFO(
        "DoomCube: mounted native dvd:/\n");

    return true;
}


/* ------------------------------------------------------------------------- */
/* DoomGeneric / GameCube platform                                           */
/* ------------------------------------------------------------------------- */

static bool GC_PlatformInit(void)
{
   
   if (platformInitialized)
   {
       
       return true;
   }

    PAD_Init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        DC_ERROR(
            "SDL_Init failed: %s\n",
            SDL_GetError());

        return false;
    }

    window = SDL_CreateWindow(
        "DoomCube",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        GC_OUTPUT_WIDTH,
        GC_OUTPUT_HEIGHT,
        SDL_WINDOW_SHOWN);

    if (!window)
    {
        DC_ERROR(
            "SDL_CreateWindow failed: %s\n",
            SDL_GetError());

        return false;
    }

    renderer = SDL_CreateRenderer(
        window,
        -1,
        0);

    if (!renderer)
    {
        DC_ERROR(
            "SDL_CreateRenderer failed: %s\n",
            SDL_GetError());

        return false;
    }

    {
        int outputWidth;
        int outputHeight;

        if (SDL_GetRendererOutputSize(
                renderer,
                &outputWidth,
                &outputHeight) == 0)
        {
            DC_DEBUG(
                "DoomCube: SDL renderer output = %dx%d\n",
                outputWidth,
                outputHeight);
        }
        else
        {
            DC_WARN(
                "DoomCube: SDL_GetRendererOutputSize failed: %s\n",
                SDL_GetError());
        }
    }
    

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB888,
        SDL_TEXTUREACCESS_STREAMING,
        DOOMGENERIC_RESX,
        DOOMGENERIC_RESY);

    if (!texture)
    {
        DC_ERROR(
            "SDL_CreateTexture failed: %s\n",
            SDL_GetError());

        return false;
    }

    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    if (!mountIsoFilesystem())
    {
        DC_ERROR(
            "DoomCube: disc mount unavailable\n");

        return false;
    }

   if (!GC_MemoryCardInit())
{
    DC_WARN(
        "DoomCube: Memory Card unavailable; continuing without saves\n");
}
platformInitialized = true;


return true;
}

void DG_Init(void)
{
    if (!GC_PlatformInit())
    {
        exit(1);
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


/* ------------------------------------------------------------------------- */
/* Rumble                                                                    */
/* ------------------------------------------------------------------------- */

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


/* For damage done to player. */
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

#define DOOMCUBE_STRINGIFY_INNER(x) #x
#define DOOMCUBE_STRINGIFY(x) DOOMCUBE_STRINGIFY_INNER(x)

int main(int argc, char **argv)
{
    gc_launch_selection_t selection;
    char *doomArgv[9] = { NULL };
    int doomArgc;


    (void)argc;
    (void)argv;

    if (!GC_PlatformInit())
    {
        return 1;
    }


    if (!GC_LauncherSelectGame(
            renderer,
            &selection))
    {
        return 1;
    }

    doomArgv[0] =
        "doomcube";

    doomArgv[1] =
        "-iwad";

    doomArgv[2] =
        (char *)selection.iwadPath;

    doomArgc = 3;

    if (selection.pwadPath != NULL)
    {
        doomArgv[3] =
            "-merge";

        doomArgv[4] =
            (char *)selection.pwadPath;

        doomArgc = 5;

        DC_INFO(
            "DoomCube: starting Doom engine with %s + %s\n",
            selection.iwadPath,
            selection.pwadPath);
    }
    else
    {
        DC_INFO(
            "DoomCube: starting Doom engine with %s\n",
            selection.iwadPath);
    }

#if defined(DOOMCUBE_TEST_WARP_EPISODE) && \
    defined(DOOMCUBE_TEST_WARP_MAP)
    doomArgv[doomArgc++] =
        "-warp";

    doomArgv[doomArgc++] =
        DOOMCUBE_STRINGIFY(DOOMCUBE_TEST_WARP_EPISODE);

    doomArgv[doomArgc++] =
        DOOMCUBE_STRINGIFY(DOOMCUBE_TEST_WARP_MAP);

    DC_INFO(
        "DoomCube: TEST WARP enabled: E%sM%s\n",
        doomArgv[doomArgc - 2],
        doomArgv[doomArgc - 1]);
#endif

    doomArgv[doomArgc] = NULL;

    doomgeneric_Create(
        doomArgc,
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

    DC_ERROR(
        "DoomCube: >>> SYS_MainLoop RETURNED FALSE <<<\n");

    /*
     * Explicitly stop the controller motor when leaving the main loop.
     */
    PAD_ControlMotor(
        PAD_CHAN0,
        PAD_MOTOR_STOP);

    GC_MemoryCardShutdown();

    if (dvdMounted)
    {
        GC_DVDFST_Unmount();
    }

    return 0;
}

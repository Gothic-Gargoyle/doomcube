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
#define TRIGGER_THRESHOLD 40

#define GC_KEY_PREVWEAPON 0xa4
#define GC_KEY_NEXTWEAPON 0xa5

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
static int gcEscape;
static int gcTab;

static int gcStrafeLeft;
static int gcStrafeRight;

static int gcPrevWeapon;
static int gcNextWeapon;

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

    u8 triggerR = PAD_TriggerR(0);

    int up =
        (held & PAD_BUTTON_UP) ||
        stickY > STICK_DEADZONE;

    int down =
        (held & PAD_BUTTON_DOWN) ||
        stickY < -STICK_DEADZONE;

    int left =
        (held & PAD_BUTTON_LEFT) ||
        stickX < -STICK_DEADZONE;

    int right =
        (held & PAD_BUTTON_RIGHT) ||
        stickX > STICK_DEADZONE;

    int cLeft =
        cstickX < -CSTICK_DEADZONE;

    int cRight =
        cstickX > CSTICK_DEADZONE;

    int fire =
        triggerR > TRIGGER_THRESHOLD;

    /*
     * Only update the motor when the fire state changes.
     *
     * gcFire still contains the previous frame's state here because
     * setKeyState() has not been called yet.
     */
    if (fire != gcFire)
    {
        PAD_ControlMotor(
            PAD_CHAN0,
            fire ? PAD_MOTOR_RUMBLE : PAD_MOTOR_STOP);
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
        held & PAD_BUTTON_X,
        &gcNextWeapon,
        key_nextweapon);

    setKeyState(
        held & PAD_BUTTON_Y,
        &gcPrevWeapon,
        key_prevweapon);

    setKeyState(
        held & PAD_BUTTON_A,
        &gcUse,
        KEY_USE);

    setKeyState(
        held & PAD_TRIGGER_L,
        &gcRun,
        KEY_RSHIFT);

    setKeyState(
        held & PAD_BUTTON_A,
        &gcEnter,
        KEY_ENTER);

    setKeyState(
        held & PAD_BUTTON_START,
        &gcEscape,
        KEY_ESCAPE);

    setKeyState(held & PAD_TRIGGER_Z, &gcTab, KEY_TAB);
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
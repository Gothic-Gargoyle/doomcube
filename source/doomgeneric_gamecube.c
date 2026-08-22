#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>

#include "doomkeys.h"
#include "doomgeneric.h"
#include "m_controls.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ogcsys.h>
#include <gccore.h>

#define KEYQUEUE_SIZE     64
#define STICK_DEADZONE    24
#define CSTICK_DEADZONE   24
#define TRIGGER_THRESHOLD 40

/*
 * These are arbitrary unused keycodes for the GameCube backend.
 * Doom's own key_prevweapon/key_nextweapon variables will point to them.
 */
#define GC_KEY_PREVWEAPON 0xa4
#define GC_KEY_NEXTWEAPON 0xa5

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

static unsigned short keyQueue[KEYQUEUE_SIZE];
static unsigned int keyWrite;
static unsigned int keyRead;

static int gcUp, gcDown, gcLeft, gcRight;
static int gcFire, gcUse, gcRun, gcEnter, gcEscape, gcTab;
static int gcStrafeLeft, gcStrafeRight;
static int gcPrevWeapon, gcNextWeapon;

static void queueKey(int pressed, unsigned char key)
{
    unsigned int next = (keyWrite + 1) % KEYQUEUE_SIZE;

    if (next == keyRead)
        keyRead = (keyRead + 1) % KEYQUEUE_SIZE;

    keyQueue[keyWrite] =
        ((pressed ? 1 : 0) << 8) | key;

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

    setKeyState(up,    &gcUp,    KEY_UPARROW);
    setKeyState(down,  &gcDown,  KEY_DOWNARROW);
    setKeyState(left,  &gcLeft,  KEY_LEFTARROW);
    setKeyState(right, &gcRight, KEY_RIGHTARROW);

    setKeyState(cLeft,  &gcStrafeLeft,  KEY_STRAFE_L);
    setKeyState(cRight, &gcStrafeRight, KEY_STRAFE_R);

    setKeyState(
        triggerR > TRIGGER_THRESHOLD,
        &gcFire,
        KEY_FIRE
    );

    setKeyState(
        held & PAD_BUTTON_X,
        &gcNextWeapon,
        GC_KEY_NEXTWEAPON
    );

    setKeyState(
        held & PAD_BUTTON_Y,
        &gcPrevWeapon,
        GC_KEY_PREVWEAPON
    );

    setKeyState(
        held & PAD_BUTTON_A,
        &gcUse,
        KEY_USE
    );

    setKeyState(
        held & PAD_BUTTON_B,
        &gcRun,
        KEY_RSHIFT
    );

    setKeyState(
        held & PAD_BUTTON_A,
        &gcEnter,
        KEY_ENTER
    );

    setKeyState(
        held & PAD_BUTTON_START,
        &gcEscape,
        KEY_ESCAPE
    );

    setKeyState(
        held & PAD_TRIGGER_Z,
        &gcTab,
        KEY_TAB
    );
}

void DG_Init(void)
{
    PAD_Init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    window = SDL_CreateWindow(
        "DOOM",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        DOOMGENERIC_RESX,
        DOOMGENERIC_RESY,
        SDL_WINDOW_SHOWN
    );

    if (!window)
    {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        exit(1);
    }

    renderer = SDL_CreateRenderer(window, -1, 0);

    if (!renderer)
    {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        exit(1);
    }

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB888,
        SDL_TEXTUREACCESS_STREAMING,
        DOOMGENERIC_RESX,
        DOOMGENERIC_RESY
    );

    if (!texture)
    {
        printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
}

void DG_DrawFrame(void)
{
    SDL_UpdateTexture(
        texture,
        NULL,
        DG_ScreenBuffer,
        DOOMGENERIC_RESX * sizeof(uint32_t)
    );

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
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
    if (keyRead == keyWrite)
        return 0;

    unsigned short data = keyQueue[keyRead];

    keyRead = (keyRead + 1) % KEYQUEUE_SIZE;

    *pressed = data >> 8;
    *doomKey = data & 0xff;

    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    if (window)
        SDL_SetWindowTitle(window, title);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char *doomArgv[] =
    {
        "doomcube",
        "-iwad",
        "doom1.wad"
    };

    doomgeneric_Create(3, doomArgv);

    /*
     * DoomGeneric defaults these to 0, meaning unbound.
     * Give the GameCube backend two private keycodes.
     */
    key_prevweapon = GC_KEY_PREVWEAPON;
    key_nextweapon = GC_KEY_NEXTWEAPON;

    while (SYS_MainLoop())
        doomgeneric_Tick();

    return 0;
}
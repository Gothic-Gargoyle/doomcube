#include <gccore.h>
#include <ogcsys.h>
#include <ogc/si.h>

#include <stdio.h>

#include "gba_link.h"
#include "doomcube_gba_mb_gba.h"

static void *xfb;
static GXRModeObj *rmode;

static void initConsole(void)
{
    VIDEO_Init();

    rmode =
        VIDEO_GetPreferredMode(NULL);

    xfb =
        MEM_K0_TO_K1(
            SYS_AllocateFramebuffer(rmode)
        );

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();

    VIDEO_WaitVSync();

    if (rmode->viTVMode & VI_NON_INTERLACE)
    {
        VIDEO_WaitVSync();
    }

    CON_InitEx(
        rmode,
        24,
        32,
        rmode->fbWidth - 32,
        rmode->xfbHeight - 48
    );

    VIDEO_ClearFrameBuffer(
        rmode,
        xfb,
        COLOR_BLACK
    );
}

int main(void)
{
    const int gba_channel = 1;

    initConsole();

    /*
     * libogc2 does not expose SI_Init().
     *
     * PAD_Init() performs the required SI subsystem
     * initialization, as used by existing GameCube
     * GBA-link implementations.
     *
     * We do not scan/read the pads afterwards while
     * the GBA multiboot transfer is running.
     */
    PAD_Init();

    printf("\n");
    printf("DoomCube GBA link test\n");
    printf("======================\n\n");

    printf(
        "Embedded payload: %u bytes\n",
        doomcube_gba_mb_gba_size
    );

    printf(
        "Forcing GBA test on GameCube Port 2\n"
    );

    printf(
        "SI channel: %d\n\n",
        gba_channel
    );

    printf(
        "Initial SI type:   %08x\n",
        (unsigned int)SI_GetType(gba_channel)
    );

    printf(
        "Initial SI status: %08x\n\n",
        (unsigned int)SI_GetStatus(gba_channel)
    );

    if (!GBA_LinkBoot(
            gba_channel,
            doomcube_gba_mb_gba,
            doomcube_gba_mb_gba_size))
    {
        printf("\n");
        printf("GBA boot failed.\n");

        while (SYS_MainLoop())
        {
            VIDEO_WaitVSync();
        }

        return 1;
    }

    printf("\n");
    printf("GBA boot successful.\n");
    printf("The GBA should now display Hello World.\n");

    while (SYS_MainLoop())
    {
        VIDEO_WaitVSync();
    }

    return 0;
}
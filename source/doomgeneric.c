#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"

#include <ogcsys.h>
#include <gccore.h>

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	 // save arguments
     myargc = argc;
     myargv = argv;

    SYS_Report("DoomCube: doomgeneric_Create: args set\n");

    M_FindResponseFile();
    SYS_Report("DoomCube: doomgeneric_Create: response file done\n");

    DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
    SYS_Report(
        "DoomCube: doomgeneric_Create: screen buffer = %p\n",
        DG_ScreenBuffer);

    DG_Init();
    SYS_Report("DoomCube: doomgeneric_Create: DG_Init done\n");

    D_DoomMain ();

    SYS_Report("DoomCube: doomgeneric_Create: D_DoomMain returned\n");
}


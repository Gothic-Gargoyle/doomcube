#include <gba.h>
#include <stdio.h>

int main(void)
{
    irqInit();
    irqEnable(IRQ_VBLANK);

    consoleDemoInit();

    iprintf("\x1b[8;8HHello World!");
    iprintf("\x1b[10;5HDoomCube GBA test");

    while (1)
    {
        VBlankIntrWait();
    }

    return 0;
}
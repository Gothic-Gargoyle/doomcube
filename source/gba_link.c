#include "gba_link.h"

#include <gccore.h>
#include <ogc/si.h>

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SI_TRANS_DELAY   50
#define GBA_MAX_ROM_SIZE 0x40000

static u8 *resbuf;
static u8 *cmdbuf;

static volatile u32 transval;
static volatile u32 resval;


/*
 * Match the behaviour of FIX94's known-working sender:
 *
 * The callback result is NOT treated as a success/error return.
 * Its purpose here is simply to signal that the asynchronous SI
 * transfer has completed.
 */
static void transcb(s32 chan, u32 ret)
{
    (void)chan;
    (void)ret;

    transval = 1;
}


static void typecb(s32 chan, u32 val)
{
    (void)chan;

    resval = val;
}


static void wait_for_transfer(void)
{
    while (transval == 0)
        usleep(350);
}


/* ------------------------------------------------------------------------- */
/* JoyBus commands                                                           */
/* ------------------------------------------------------------------------- */

static void gba_reset(int channel)
{
    cmdbuf[0] = 0xff;

    transval = 0;

    /*
     * IMPORTANT:
     *
     * Do not test SI_Transfer() as a boolean here.
     *
     * The reference implementation queues the transfer and waits for
     * the callback.
     */
    SI_Transfer(
        channel,
        cmdbuf,
        1,
        resbuf,
        3,
        transcb,
        SI_TRANS_DELAY
    );

    wait_for_transfer();
}


static void gba_status(int channel)
{
    cmdbuf[0] = 0x00;

    transval = 0;

    SI_Transfer(
        channel,
        cmdbuf,
        1,
        resbuf,
        3,
        transcb,
        SI_TRANS_DELAY
    );

    wait_for_transfer();
}


static u32 gba_recv(int channel)
{
    memset(resbuf, 0, 32);

    cmdbuf[0] = 0x14;

    transval = 0;

    SI_Transfer(
        channel,
        cmdbuf,
        1,
        resbuf,
        5,
        transcb,
        SI_TRANS_DELAY
    );

    wait_for_transfer();

    return *(volatile u32 *)resbuf;
}


static void gba_send(int channel, u32 msg)
{
    cmdbuf[0] = 0x15;

    cmdbuf[1] = (msg >> 0)  & 0xff;
    cmdbuf[2] = (msg >> 8)  & 0xff;
    cmdbuf[3] = (msg >> 16) & 0xff;
    cmdbuf[4] = (msg >> 24) & 0xff;

    transval = 0;

    resbuf[0] = 0;

    SI_Transfer(
        channel,
        cmdbuf,
        5,
        resbuf,
        1,
        transcb,
        SI_TRANS_DELAY
    );

    wait_for_transfer();
}


/* ------------------------------------------------------------------------- */
/* CRC                                                                       */
/* ------------------------------------------------------------------------- */

static unsigned int do_crc(
    u32 crc,
    u32 val)
{
    int i;

    for (i = 0; i < 0x20; ++i)
    {
        if ((crc ^ val) & 1)
        {
            crc >>= 1;
            crc ^= 0xa1c1;
        }
        else
        {
            crc >>= 1;
        }

        val >>= 1;
    }

    return crc;
}


/* ------------------------------------------------------------------------- */
/* Multiboot key                                                             */
/* ------------------------------------------------------------------------- */

static unsigned int calc_key(unsigned int size)
{
    unsigned int ret = 0;

    int res1;
    int res2;
    int res3;

    size =
        (size - 0x200) >> 3;

    res1 =
        (size & 0x3f80) << 1;

    res1 |=
        (size & 0x4000) << 2;

    res1 |=
        size & 0x7f;

    res1 |=
        0x380000;

    res2 = res1;

    res1 =
        res2 >> 0x10;

    res3 =
        res2 >> 8;

    res3 += res1;
    res3 += res2;

    res3 <<= 24;
    res3 |= res2;
    res3 |= 0x80808080;

    if ((res3 & 0x200) == 0)
    {
        ret |=
            (((res3 >> 0) & 0xff) ^ 0x4b)
            << 24;

        ret |=
            (((res3 >> 8) & 0xff) ^ 0x61)
            << 16;

        ret |=
            (((res3 >> 16) & 0xff) ^ 0x77)
            << 8;

        ret |=
            (((res3 >> 24) & 0xff) ^ 0x61);
    }
    else
    {
        ret |=
            (((res3 >> 0) & 0xff) ^ 0x73)
            << 24;

        ret |=
            (((res3 >> 8) & 0xff) ^ 0x65)
            << 16;

        ret |=
            (((res3 >> 16) & 0xff) ^ 0x64)
            << 8;

        ret |=
            (((res3 >> 24) & 0xff) ^ 0x6f);
    }

    return ret;
}


/* ------------------------------------------------------------------------- */
/* Detection                                                                 */
/* ------------------------------------------------------------------------- */

int GBA_LinkDetect(int channel)
{
    unsigned int timeout = 1000;

    resval = 0;

    SI_GetTypeAsync(
        channel,
        typecb
    );

    while (timeout--)
    {
        if (resval)
        {
            /*
             * These are transient/error responses used by
             * the original sender.
             */
            if (resval == 0x80 ||
                (resval & 8))
            {
                resval = 0;

                SI_GetTypeAsync(
                    channel,
                    typecb
                );
            }
            else
            {
                break;
            }
        }

        usleep(1000);
    }

    printf(
        "Port %d: SI type %08x, status %08x\n",
        channel + 1,
        (unsigned int)resval,
        (unsigned int)SI_GetStatus(channel)
    );

    return
        (resval & SI_GBA) != 0;
}


int GBA_LinkFind(void)
{
    int channel;

    printf("\nScanning all GameCube SI ports...\n\n");

    for (channel = 0;
         channel < 4;
         ++channel)
    {
        if (GBA_LinkDetect(channel))
        {
            printf(
                "\nGBA detected on port %d\n",
                channel + 1
            );

            return channel;
        }
    }

    printf("\nNo SI_GBA device detected.\n");

    return -1;
}


/* ------------------------------------------------------------------------- */
/* Multiboot                                                                 */
/* ------------------------------------------------------------------------- */

int GBA_LinkBoot(
    int channel,
    const void *rom,
    size_t rom_size)
{
    u8 *gba;

    unsigned int sendsize;
    unsigned int ourkey;
    unsigned int fcrc;

    u32 sessionkeyraw;
    u32 sessionkey;

    unsigned int i;

    if (channel < 0 ||
        channel > 3)
    {
        printf("GBA: invalid SI channel\n");
        return 0;
    }

    if (!rom)
    {
        printf("GBA: ROM pointer is NULL\n");
        return 0;
    }

    if (rom_size < 0x200)
    {
        printf(
            "GBA: ROM too small: %u bytes\n",
            (unsigned int)rom_size
        );

        return 0;
    }

    if (rom_size > GBA_MAX_ROM_SIZE)
    {
        printf(
            "GBA: ROM too large: %u bytes\n",
            (unsigned int)rom_size
        );

        return 0;
    }

    cmdbuf =
        memalign(32, 32);

    resbuf =
        memalign(32, 32);

    gba =
        memalign(
            32,
            GBA_MAX_ROM_SIZE
        );

    if (!cmdbuf ||
        !resbuf ||
        !gba)
    {
        printf("GBA: allocation failed\n");

        free(cmdbuf);
        free(resbuf);
        free(gba);

        return 0;
    }

    memset(
        cmdbuf,
        0,
        32
    );

    memset(
        resbuf,
        0,
        32
    );

    memset(
        gba,
        0,
        GBA_MAX_ROM_SIZE
    );

    memcpy(
        gba,
        rom,
        rom_size
    );


    /*
     * Same JoyBoot entry patch used by FIX94's sender.
     */
    if (*(u32 *)(gba + 0xe4)  == 0x0010a0e3 &&
        *(u32 *)(gba + 0xec)  == 0xc010a0e3 &&
        *(u32 *)(gba + 0x100) == 0xfcffff1a &&
        *(u32 *)(gba + 0x118) == 0x040050e3 &&
        *(u32 *)(gba + 0x11c) == 0xfbffff1a &&
        *(u32 *)(gba + 0x12c) == 0x020050e3 &&
        *(u32 *)(gba + 0x130) == 0xfbffff1a &&
        *(u32 *)(gba + 0x140) == 0xfeffff1a)
    {
        printf(
            "GBA: patching JoyBoot entry point\n"
        );

        *(u32 *)(gba + 0xe0) =
            0x170000ea;
    }


    printf(
        "GBA: waiting for BIOS on port %d...\n",
        channel + 1
    );

    resbuf[2] = 0;

    /*
     * This loop intentionally matches the reference sender:
     *
     * RESET
     * STATUS
     * check bit 0x10
     */
    while (!(resbuf[2] & 0x10))
    {
        gba_reset(channel);
        gba_status(channel);

        printf(
            "GBA status: %02x %02x %02x\r",
            resbuf[0],
            resbuf[1],
            resbuf[2]
        );

        VIDEO_WaitVSync();
    }

    printf(
        "\nGBA: BIOS ready\n"
    );


    sendsize =
        (((unsigned int)rom_size) + 7)
        & ~7;

    ourkey =
        calc_key(sendsize);


    sessionkeyraw =
        gba_recv(channel);

    sessionkey =
        __builtin_bswap32(
            sessionkeyraw ^
            0x7365646f
        );


    gba_send(
        channel,
        __builtin_bswap32(ourkey)
    );


    fcrc = 0x15a0;


    printf(
        "GBA: sending header...\n"
    );

    for (i = 0;
         i < 0xc0;
         i += 4)
    {
        gba_send(
            channel,
            __builtin_bswap32(
                *(volatile u32 *)(gba + i)
            )
        );
    }


    printf(
        "GBA: sending payload (%u bytes)...\n",
        sendsize
    );

    for (i = 0xc0;
         i < sendsize;
         i += 4)
    {
        u32 enc;

        enc =
            ((gba[i + 3] << 24) |
             (gba[i + 2] << 16) |
             (gba[i + 1] << 8) |
             (gba[i + 0]));

        fcrc =
            do_crc(
                fcrc,
                enc
            );

        sessionkey =
            (sessionkey *
             0x6177614b)
            + 1;

        enc ^=
            sessionkey;

        enc ^=
            ((~(
                i +
                (0x20 << 20)
            )) + 1);

        enc ^=
            0x20796220;

        gba_send(
            channel,
            enc
        );
    }


    fcrc |=
        sendsize << 16;


    sessionkey =
        (sessionkey *
         0x6177614b)
        + 1;


    fcrc ^=
        sessionkey;

    fcrc ^=
        ((~(
            i +
            (0x20 << 20)
        )) + 1);

    fcrc ^=
        0x20796220;


    printf(
        "GBA: sending CRC...\n"
    );

    gba_send(
        channel,
        fcrc
    );


    /*
     * BIOS CRC reply.
     */
    (void)gba_recv(channel);


    printf(
        "GBA: MULTIBOOT COMPLETE\n"
    );


    free(gba);
    free(cmdbuf);
    free(resbuf);

    gba = NULL;
    cmdbuf = NULL;
    resbuf = NULL;

    return 1;
}
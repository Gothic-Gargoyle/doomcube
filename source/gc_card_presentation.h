#ifndef DOOMCUBE_GC_CARD_PRESENTATION_H
#define DOOMCUBE_GC_CARD_PRESENTATION_H

#include <stdbool.h>

#include <ogc/card.h>


/*
 * Ensure that an already-valid DoomCube v3 CARD file contains the
 * GameCube Memory Card manager presentation payload and directory
 * metadata.
 *
 * sectorBuffer must:
 *
 *   - be at least sectorSize bytes
 *   - be 32-byte aligned for CARD_Read/CARD_Write
 *
 * The operation is idempotent.  If both the payload and CARD status
 * already match, no write is performed.
 *
 * Presentation failure is cosmetic and must never be interpreted as
 * save-format corruption by callers.
 */
bool GC_CardPresentationApply(
    card_file *file,
    s32 sectorSize,
    unsigned char *sectorBuffer
);

#endif

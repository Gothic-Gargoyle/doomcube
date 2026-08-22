#ifndef DOOMCUBE_GC_MEMCARD_H
#define DOOMCUBE_GC_MEMCARD_H

#include <stdbool.h>

bool GC_MemoryCardInit(void);
bool GC_MemoryCardCounterTest(void);
void GC_MemoryCardShutdown(void);

#endif
#ifndef GC_DEBUG_H
#define GC_DEBUG_H

#include <ogc/system.h>

/*
 * DoomCube logging
 *
 * DC_ERROR - errors that should always be visible
 * DC_WARN  - warnings that should always be visible
 * DC_INFO  - useful normal runtime information
 * DC_DEBUG - verbose development diagnostics
 * DC_TRACE - extremely verbose development diagnostics
 *
 * DEBUG is compiled in with DOOMCUBE_DEBUG.
 * TRACE is compiled in with DOOMCUBE_TRACE.
 */

#define DC_ERROR(...) SYS_Report(__VA_ARGS__)
#define DC_WARN(...)  SYS_Report(__VA_ARGS__)
#define DC_INFO(...)  SYS_Report(__VA_ARGS__)

#ifdef DOOMCUBE_DEBUG
#define DC_DEBUG(...) SYS_Report(__VA_ARGS__)
#else
#define DC_DEBUG(...) ((void)0)
#endif

#ifdef DOOMCUBE_TRACE
#define DC_TRACE(...) SYS_Report(__VA_ARGS__)
#else
#define DC_TRACE(...) ((void)0)
#endif

#endif /* GC_DEBUG_H */

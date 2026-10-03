/*
nvsoft.h -- software implementation of the NVIDIA NV1 accelerator, phase 1 only.

This is DEVELOPMENT SCAFFOLDING for nv1Quake.  It implements the subset of
NVLIB 1.50 that nv1Quake calls, in software, so the port can be built and seen
on a modern machine.  On real NV1 hardware this whole directory is dropped and
nvlib.lib is linked instead -- nv1Quake itself never includes this header
except through the NV1_SOFT guard.

Everything here is deliberately Win95-era ANSI C, same as the rest of the port.
*/

#ifndef NVSOFT_H
#define NVSOFT_H

/*
 * Hand the emulator the window it should present into.  On real hardware the
 * resource manager owns the display, so there is no equivalent call.
 */
void NVSOFT_SetWindow (void *hWnd);

/*
 * Pump the emulator's idea of the display.  Called from NVLIB_FlipDBPipe.
 */
void NVSOFT_Present (void);

/*
 * Current emulated framebuffer geometry, as established by NvRequestVideoMode.
 */
int  NVSOFT_GetWidth (void);
int  NVSOFT_GetHeight (void);

/*
 * Statistics, so we can see what the real card would have been asked to chew
 * through: patches submitted and texels streamed this frame.  On NV1 the texel
 * count is the number the CPU has to push across the bus, so it is the number
 * that actually matters.
 */
void NVSOFT_ResetStats (void);
long NVSOFT_GetPatchCount (void);
long NVSOFT_GetTexelCount (void);

#endif /* NVSOFT_H */

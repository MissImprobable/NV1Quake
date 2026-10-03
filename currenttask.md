# Current Task: nv1Quake

## Problem / feature
Port GLQuake (id's 1999 GPL release, `WinQuake/gl_*.c`) to the NVIDIA NV1 /
Diamond Edge 3D / SGS-Thomson STG2000 accelerator, using the NV1 SDK 1.50 in
`NV1/NV/SDK`. Result is called **nv1Quake**.

## Agreed design (confirmed by user)
Three phases, in this order:

1. **Phase 1 (current)** — Renderer written strictly against the real NVLIB 1.50
   API, PLUS a software implementation of that same API (`nvsoft`) that emulates
   NV1 semantics, so it builds and runs on the Windows 11 dev box and we can
   actually see results and iterate.
2. **Phase 2** — Period-correct Win95 build linking the real `nvlib.lib`,
   running on the user's real NV1 card + retro PC.
3. **Phase 3** — DOS build against `nvlibdos.lib` + `nvrm.lib` + DOS4GW.

**Hard constraint from the user: "it should run on Windows 95 so don't use any
modern compile methods."** This applies to ALL engine code from day one, not
just phase 2. `nvsoft` is dev scaffolding but is held to the same standard.

### Coding rules (Win95 / MSVC 2.0-4.x era)
- ANSI C (C89) only. Declarations at top of block. No C99, no `//` on its own
  where the surrounding id code wouldn't (id's own code uses `//`, so `//` is
  fine — MSVC accepted it).
- No `stdint.h`, `snprintf`, `stdbool.h`, compound literals, designated
  initialisers, variadic macros, `inline`, `long long`.
- ANSI charset only, never Unicode/`TCHAR`. Plain Win32 API of the Win95 era.
- No CMake / vcpkg / modern build tooling. `nmake` makefiles only.
- Link only against Win95-era libs: kernel32, user32, gdi32, winmm, wsock32,
  dinput (from `WinQuake/dxsdk`), and nvlib.
- Match the surrounding id Quake style (tabs, brace placement, naming).

### NV1 hardware facts established by reading the SDK (drive the design)
- **No Z-buffer.** `NV32.H` has no depth/zeta concept at all. Depth must be
  resolved by sorting -> back-to-front BSP traversal (painter's algorithm),
  not GLQuake's z-buffered approach.
- The native primitive is a **quadratic patch**: 9 screen-space control points
  (`NV_RENDER_TEXTURE_QUADRATIC`, class 36) laid out as
  `col0=(p0,p1,p2)` left edge, `row0=(p0,p3,p6)` top edge,
  `col2=(p6,p7,p8)` right edge, `row2=(p2,p5,p8)` bottom edge, `p4` centre.
- Each edge curve is an **interpolating (Lagrange) quadratic**, verified in
  `SBQTM.C:NVMath_CalcQcurve`: `P(0)=start, P(0.5)=middle, P(1)=end`.
- `SubdivideIn` = `HEIGHT_02_68` (bits 0-3) = number of strips, along edges
  0->2 and 6->8; `WIDTH_06_28` (bits 4-7) = texels per strip, along edges
  0->6 and 2->8. Each power is 2..8 (4..256).
- **The CPU resamples the texture**; texels are streamed 2-per-32-bit-write
  into `renderTextureQuadratic.Color[0]`. Cost per primitive is
  `2^HEIGHT * 2^WIDTH` texel writes, INDEPENDENT of on-screen size. Choosing
  subdivision powers from projected screen size is the whole performance story.
- `NVLIB_DrawQTexTri` is not a hardware triangle: it reorders 3 verts into a
  degenerate 4-corner patch (two corners coincide) and derives the 5 interior
  control points by perspective interpolation via the `nvldiv` LUT.
- `nvldiv[101][401]`, built by `NVLIB_InitDivLUT`, is `i/j` for `i` in 0..100
  and `j` in 1..400, with `nvldiv[0][j]` forced to `0.5f`.
  **=> `nvLib_3DV.w` must be an integer clamped to 1..100.**
- Control points are integer screen coords (`S016`). A `ControlPointOut12d4`
  S12.4 subpixel path also exists in the class, unused by NVLIB.
- Lighting is a per-vertex `beta` (S1.15), not a lightmap texture unit; there
  is no multitexture. Quake lightmaps must collapse to per-vertex beta.
- Textures: 16-bit, power-of-two, max 256.
- Target video mode: **640x400, 32K colours (RGB555), double-buffered** —
  documented in `NVVIDMOD.H` as the only double-buffered RGB mode that fits
  NV1's 1MB framebuffer.

### Submission-path decision (made while writing the emulator)
`NVLIB_DrawQTexTri` does its texture resampling **on the CPU, inside NVLIB**,
and explicitly does no bounds checking on texture coordinates ("No checking for
source texture boundaries vs points in texture space" -- QTMT.C).  Quake's world
surfaces routinely run past one texture repeat and rely on GL_REPEAT, so calling
DrawQTexTri directly would read off the end of every wall texture.

Since the resampling is CPU-side either way, nv1Quake does it itself:
  1. project and build the nine control points, perspective-correct via nvldiv
  2. resample the chosen mip into a 2^h x 2^w grid, masking u/v so power-of-two
     textures wrap the way GL_REPEAT would
  3. submit with `NVLIB_DrawQTexQuad` / `NVLIB_DrawBlendQTexQuad`, which take a
     ready-made texel grid plus the nine control points

This is identical work to what NVLIB would have done, uses only the streaming
primitive the chip actually has, and behaves the same against real nvlib.lib.
It also puts mip selection and subdivision choice -- the only two things that
control NV1 performance -- under our control.

### Source layout decision
`quakedef.h` includes `"glquake.h"` only under `#ifdef GLQUAKE`, and no other
id file includes it. So nv1Quake defines `GLQUAKE` and puts its OWN
`glquake.h` earlier on the include path, shadowing id's. This means
**zero modifications to any file under `WinQuake/`** — the id source stays
pristine and every shared module (common, host, sv_*, cl_*, net_*, snd_*, ...)
compiles straight out of `WinQuake/`.

```
NV1Quake/
  Makefile            nmake
  src/                the port (replaces gl_*.c)
    glquake.h         shadowing header; NV1 renderer private defs
    nv1_vid.c         video mode, DBPipe, window     (was gl_vidnt.c)
    nv1_draw.c        2D + texture upload to RGB555  (was gl_draw.c)
    nv1_rmain.c       view setup, transforms         (was gl_rmain.c)
    nv1_rsurf.c       world, back-to-front           (was gl_rsurf.c)
    nv1_mesh.c        alias models                   (was gl_mesh.c)
    nv1_warp.c        sky / water                    (was gl_warp.c)
    nv1_model.c       model load                     (was gl_model.c)
    nv1_screen.c, nv1_rlight.c, nv1_refrag.c, nv1_rmisc.c
  nvsoft/             PHASE 1 ONLY - software NVLIB 1.50
    nvsoft.c          patch rasteriser + NVLIB entry points
    nvsoft_win.c      window + DIB present (dev scaffolding)
```

## Files already read
- `NV1/NV/SDK/INC/NVLIB.H`, `NVUTYPES.H`, `NVVIDMOD.H`; `NV32.H` (classes,
  QTM/BTM subdivide bits, `NV_RENDER_TEXTURE_QUADRATIC`, `..._BILINEAR_BETA`)
- `NV1/NV/SDK/SRC/NVLIB/QTMT.C` (full), `QTMDIV.C`, `SBQTM.C` (CalcQcurve /
  CalcMidPtQcurve / SubdivideQSurf), `DBPIPE.H`
- `NV1/NV/SDK/README.TXT`, `SRC/NVLIB/README.TXT`
- `WinQuake/glquake.h` (head), `WinQuake/quakedef.h` (GLQUAKE switch),
  `WinQuake/gl_vidnt.c` (structure), `WinQuake/glqnotes.txt`,
  `WinQuake/WinQuake.dsp` (source list), `WinQuake/dxsdk` contents

## READ ISSUES.md FIRST
ISSUES.md is the handoff document: current state of all three builds, the one
blocking bug (DOS startup), and the ranked list of everything else with
concrete next steps.  Paused 2026-09-08 at the user request; resuming after
Thursday.

## STATUS: Phase 1 COMPLETE.  Phase 2 BUILDS AND LINKS (never run on hardware).

Two binaries:
  NV1Quake/nv1quake.exe   software NV1, runs on Windows 11   (build.bat)
  NV1Quake/nv1q95.exe     real NV1, Windows 95               (build-nv1.bat)

nv1q95.exe links the real nvlib.lib with Open Watcom 2.0 and is verified as an
i386 PE, GUI subsystem, subsystem version 4.0, importing NVAPI.DLL and
NVVIDMOD.DLL.  It has NEVER BEEN RUN -- the card arrives in a few weeks.

Release/ holds ready-to-run folders for both, each with id1/ and a README.

### Flagged for a future revision
The first-person weapon models still look wrong.  The alias fan bug (fans were
being clipped as convex polygons) was real and is fixed, but the view model is
still visibly off.  It is the hardest case here: a few units from the eye,
heavily near-plane clipped, spanning a large depth ratio that a quadratic patch
represents badly.  Look at nv1_zslab at very short range, the 4 unit near clip,
and how subdivision powers are chosen for extremely foreshortened triangles.
Everything else -- world, monsters, items, sky, water -- looks right.

Measured: demo1 at 640x400, 969 frames / 44.3 s / 21.9 fps in the emulator,
~1,100 patches and ~650,000 texels per frame.  The texel count is the figure
that matters for real hardware and is what Phase 2 will be tuned against.

See CHANGELOG.md for the full record of what was established about the
hardware, what was designed around it, and what was fixed during bring-up.

### Correction to the source-layout decision below
The include-shadowing plan does NOT work.  `quakedef.h` reaches the renderer
interface with a quoted `#include "glquake.h"`, and MSVC resolves a quoted
include relative to the directory of the includer (`WinQuake`) before any -I
path, so id's header always wins.  Resolved WITHOUT touching id's tree by
using id's `glquake.h` as-is, satisfying its `<GL/gl.h>` with a type-only shim
in `src/GL/` (angle-bracket includes DO honour -I order), and putting the NV1
interface in `src/nv1quake.h`, which every file in the port includes after
quakedef.h.

## Step-by-step plan (Phase 1) -- all done
1. `NV1Quake/` skeleton + build.bat + `src/nv1quake.h`.
2. `nvsoft`: the NV1 quadratic-patch rasteriser. Interpolating-quadratic
   surface eval, texel-grid forward mapping, RGB555 framebuffer, beta
   modulation, clip rect, DBPipe flip, GDI DIB present.
   Implement the NVLIB entry points nv1Quake actually calls, with the real
   signatures from `NVLIB.H`.
3. `nv1_vid.c` + `nv1_draw.c`: boot Quake to a working console and menu
   (proves pak loading, 2D path, input, sound, the whole shell).
4. `nv1_rmain.c` + `nv1_rsurf.c`: world rendering, back-to-front BSP,
   per-vertex beta lighting, screen-size-driven subdivision selection.
5. `nv1_mesh.c` + sprites + particles: entities, depth-sorted.
6. `nv1_warp.c`: sky and water.
7. `CHANGELOG.md`.  [done]

## Phase 2: real hardware -- BUILT, awaiting the card
When the hardware arrives:
  - run Release/NV1Quake/NV1QUAKE.EXE on the Win95 box
  - the NVIDIA Win95 driver / resource manager must be installed (NVAPI.DLL)
  - -vidmem 2 if the Edge 3D 2200 has its expansion modules fitted
  - expect to cut the texel budget hard; see the README cvar list
  - nv1_snd.c (NV1 audio) has never run; -nosound is the fallback

Original plan, kept for reference:
1. Drop `nvsoft/` from the build; link `NV1/NV/SDK/LIB/nvlib.lib` instead.
   Every NVLIB call the port makes already uses the real signatures.
2. Build with a period toolchain (MSVC 2.0-4.x).  The code is already ANSI C
   with no modern CRT or Win32 use; the three build workarounds in build.bat
   (WINDED, nv1_errno.h, nv1_fpu.c) all exist because of the MODERN compiler
   and should be revisited -- a period compiler does not predefine __i386__,
   so `WINDED` and `nv1_fpu.c` are probably unnecessary there, and the real
   sys_wina.s / mathlib asm can be assembled instead.
3. Sound: switch from snd_win.c to `src/nv1_snd.c`, which drives the NV1's own
   audio engine through NVLIB.  Written, never run.  Define NV1_HARDWARE.
4. Consider the Sega Saturn game port (`NVLIB_InitGamePort`) as an input
   device -- it is on the same chip.
5. Tune the texel budget.  650k texels/frame is far more than a period CPU can
   push over the bus; `nv1_subdiv 0.5` quarters it, and `nv1_maxsubdiv`,
   `nv1_worldsubdiv` and `nv1_zslab` all trade quality for texels.
6. `-vidmem 2` on an Edge 3D 2200 with the expansion modules fitted.

## Phase 3: DOS
Link `nvlibdos.lib` + `nvrm.lib`, Watcom C, DOS4GW, per the SDK's DOSDEMO
sample.  Needs a DOS sys_/vid_ layer; sys_dos.c and dos_v2.c exist in WinQuake.

## Open questions for the user
- 86Box: they offered to set up an environment.  I am not aware of NV1 /
  Diamond Edge 3D emulation existing in 86Box (the NVIDIA work there targets
  NV3 / Riva 128) -- worth them checking before spending time on it.  It is
  not needed for Phase 1, which runs natively; it would only help Phase 2 if
  the emulation were accurate enough to trust.

## What NOT to do
- Do NOT modify anything under `WinQuake/`, `QW/`, or `qw-qc/` — id's source
  stays pristine; shadow `glquake.h` instead.
- Do NOT modify anything under `NV1/` — that's the SDK as shipped.
- Do NOT use modern C, modern Win32, CMake, or any non-Win95-era toolchain
  feature anywhere, including in `nvsoft`.
- Do NOT add a git remote or push.
- Do NOT reintroduce a Z-buffer or assume multitexture; the point of the
  experiment is to hit the real NV1 constraints.
- Do NOT let `nvLib_3DV.w` leave the 1..100 integer range.

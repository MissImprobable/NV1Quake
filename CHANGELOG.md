# Changelog

## nv1Quake

A port of GLQuake (id Software's 1999 GPL release) to the NVIDIA NV1 /
Diamond Edge 3D / SGS-Thomson STG2000, built against the NVIDIA SDK 1.50 in
`NV1/`.

Three phases, in order:

1. **Phase 1 (current)** — the renderer written against the real NVLIB 1.50
   API, plus `nvsoft`, a software implementation of that API which emulates NV1
   semantics so the port can be built, run and iterated on a modern machine.
2. **Phase 2** — period-correct Win95 build linking the real `nvlib.lib`,
   running on actual hardware.
3. **Phase 3** — DOS build against `nvlibdos.lib` + `nvrm.lib` + DOS4GW.

All engine code is written to Win95 / MSVC-era constraints from the start:
ANSI C, no C99, no modern CRT or Win32, ANSI charset only, plain command-line
builds. `nvsoft` is development scaffolding but is held to the same standard.

---

### 2026-10-03 - DOS build: fixed "Corrupted data file" at startup

The DOS build (`build-dos.bat`) died in `COM_CheckRegistered` right after
loading the pak files. Cause: `NVRM.LIB`'s `osinit.c` defines its own `strcmp_`,
and the linker bound every `strcmp` in the program to it instead of Watcom's, so
`COM_FindFile` matched `gfx/pop.lmp` against the first pak entry. Found by
printing the pak directory from inside `COM_FindFile` (entries and
`sizeof(packfile_t)` were all correct; only the comparison was wrong).

Fix: `src/nv1_strcmp.c` supplies a correct `strcmp`, linked ahead of the
library. `-zp8` was also added to the DOS `CFLAGS` to match the Win95 build.
Under DOSBox the build now gets past the pak check; DOSBox has no NV1, so
nothing beyond that is verified. Nothing under `WinQuake/`, `QW/` or `NV1/` was
modified.

---

### 2026-09-07 (later) - Phase 2: links against the real NVLIB, targets Windows 95

`build-nv1.bat` produces **nv1q95.exe**: no emulator, no `nvsoft`, linked
against NVIDIA's shipped `nvlib.lib`. Verified as a 32-bit i386 PE, Windows
GUI subsystem, subsystem version **4.0**, statically linked runtime, importing
`NVAPI.DLL` and `NVVIDMOD.DLL` - the actual NVIDIA driver stack - and nothing
newer than Windows 95.

Toolchain is **Open Watcom 2.0**. A modern MSVC binary cannot run on Windows 95
at all, and Watcom is what the SDK itself expects for anything that is not
MSVC 2.0 (NVIDIA shipped Watcom flavours of the import libraries and built the
DOS library with Watcom 10.5). Every source file compiled with it, which was
the point of holding the whole port to period-appropriate ANSI C from day one.

#### Caught by linking against the real library

Reading `nvlib.lib`'s symbol table found API mistakes that could otherwise only
have surfaced on the hardware:

- **`NVLIB_DrawBlendQTexQuadA` does not exist.** I had assumed a combined
  "cut-out + beta-lit" entry point. NVLIB has cut-out *or* beta, never both.
  Since nv1Quake builds its own texel grids, lit cut-outs now fold the lighting
  into the texels during resampling and go out through the real
  `NVLIB_DrawQTexQuadA`.
- **`NVLIB_InitQTexTri`, `NVLIB_InitLitQTexTri`, `NVLIB_DestroyQTexTri` and
  `NVLIB_DestroyLitQTexTri` are declared in `nvlib.h` but are not in the
  shipped library.** No longer called. nv1Quake never needed them - it submits
  through the quad entry points, which is also the only way to get texture
  coordinate wrapping.

#### Mixing an MSVC library into a Watcom build

`nvlib.lib` was built with Microsoft C and carries references into the
Microsoft runtime, which cannot be fixed in source:

- `src/nv1_msvcrt.c` supplies the six Microsoft symbols: `_sprintf`,
  `_vsprintf` and `__mkdir` forwarded to Watcom's; `__fltused` and
  `__adjust_fdiv` as the markers they are; `__adj_fdiv_r` / `__adj_fdivr_m32`
  as stubs (unreachable while `__adjust_fdiv` is zero, true of every CPU except
  the recalled 60/66 MHz Pentiums); and `__ftol`, which is genuinely called.
  `__ftol` needs no assembly - its argument arrives on the x87 stack and its
  result in EDX:EAX, which Watcom's
  `#pragma aux ... parm [8087] value [edx eax]` describes exactly.
- The two SDK import libraries need **opposite flavours**, which is not a
  mistake: `NVVMWC.LIB` (Watcom) because `nvvidmod.h` declares
  `NvRequestVideoMode` with `#pragma aux (__stdcall) ... "*"` under
  `__WATCOMC__`, asking for an undecorated name; and `NVW32MS.LIB` (Microsoft)
  because the `NvOpen` / `NvClose` references come from inside `nvlib.lib` and
  want the decorated `_NvOpen@4`. NVIDIA shipped both flavours for this.
- Watcom's headers keep its own runtime on the register calling convention
  regardless of `-ecc`, so `clib3s.lib` / `math387s.lib` (the cdecl flavours)
  must be named explicitly.
- Watcom *does* define `_M_IX86`, so `id386` is 1 and Quake's assembly
  fallbacks compile out, same as the modern build. `WINDED` handles
  `mathlib.c` and `world.c`; `snd_mix.c` cannot use it because it includes
  `winquake.h`, where `VID_LockBuffer` is declared and `WINDED` turns it into a
  macro - so `src/nv1_sndc.c` supplies its two missing routines instead.
- `src/nv1_w32compat.h` casts the `int*` that `sys_win.c` passes to Win32 calls
  wanting `LPDWORD`. MSVC allowed it; Watcom is stricter, and correct.

Still nothing under `WinQuake/`, `QW/` or `NV1/` has been modified.

#### Renderer fixes

- **Alias model fans were drawn as polygons.** `gl_mesh.c` emits strips *and*
  fans; strips were emitted as individual triangles but a fan was handed to the
  polygon path whole. An alias fan is neither convex nor planar, so the
  Sutherland-Hodgman clipper mangled it. Models built mostly of strips
  survived; the view model did not, and rendered as a smooth grey cone that
  changed shape as the weapon animated. Both are emitted as individual
  triangles now. (The view weapon still does not look right - see below.)
- **Entities are drawn inside the world walk instead of after it.** Drawing
  every entity after the whole world means a door behind a column paints over
  the column, because there is no depth buffer to stop it. Quake's own software
  renderer fed brush models into the ordered traversal, and so does this now:
  each entity is filed under the leaf it sits in (bounding-box centre for brush
  models, whose origin is usually nowhere near the geometry) and drawn as the
  back-to-front walk reaches that leaf. Anything whose leaf is never visited is
  drawn afterwards as a fallback.
- **Cut-out transparency.** Sprites were opaque, putting a black box around
  every explosion. Alpha textures are now ARGB1555 with bit 15 as the alpha
  bit, which is how the chip does it, and mip generation averages only opaque
  texels so the key does not bleed. This also brought back the sky's second
  layer - the clouds are a cut-out, and mark transparency with palette index 0
  rather than 255, so it is remapped on load.
- The software build now defaults to a **640x400 window**: fullscreen
  mode-setting is meaningless for it (it blits a DIB) and asking a modern
  display for 640x400 fails with "specified video mode not available"; and
  640x400 rather than glquake's 640x480 because that is the NV1's actual mode,
  so the window matches the framebuffer instead of leaving a black band.

#### Release folders

`Release/NV1Quake-Emulated/` - the software build, runs on any modern Windows
PC by double-clicking, with `id1/` and a README explaining what the chip did.

`Release/NV1Quake/` - the hardware build as `NV1QUAKE.EXE`, with NVIDIA's
redistributable `NVVIDMOD.DLL`, `id1/`, and a README covering the driver
requirement, `-vidmem 2` for an expanded Edge 3D 2200, and which cvars to turn
down when the texel budget is too high.

#### Known issues / next revision

- **The view weapon models still look wrong.** The fan bug was real and is
  fixed, but the first-person weapon is still visibly off. It is the hardest
  case in the game for this renderer - it sits a few units from the eye, so it
  is heavily near-plane clipped and spans a large depth ratio, and a quadratic
  patch represents that badly. Suspects, in order: the depth-slab subdivision
  (`nv1_zslab`) not being aggressive enough at very short range; the near clip
  at 4 units cutting into the model; and subdivision powers chosen from screen
  extent being wrong for triangles this foreshortened. Flagged for a future
  pass; acceptable for a proof of concept.
- **The hardware build has never been run.** It links and the binary is
  correctly formed, but nothing has driven a real NV1 yet.
- `nv1_snd.c` likewise - written, links, never executed.
- No DOS build. That is Phase 3: `nvlibdos.lib` + `nvrm.lib` + DOS4GW and a DOS
  system layer. Watcom already being in place makes this much closer than it
  was, and the DOS library is Watcom-built so the MSVC runtime shim would not
  even be needed.

---

### 2026-09-07 — Phase 1: first working build

**nv1Quake renders Quake.** 640x400, 32768 colours, double buffered, entirely
through NV1 quadratic patches, with no depth buffer.

Measured on `demo1` at 640x400: **969 frames, 44.3 s, 21.9 fps** in the
software emulator, at roughly **1,100 patches and 650,000 texels per frame**.
The texel figure is the one that matters for real hardware — on NV1 the CPU
streams every texel to the chip, so it is the whole cost model.

#### What was established about the hardware (from the SDK, not assumed)

- **There is no depth buffer.** `NV32.H` has no depth or zeta concept at all.
  Ordering has to come from sorting.
- The native primitive is a **quadratic patch** with nine integer screen-space
  control points, and each edge is an **interpolating** quadratic — the middle
  control point lies *on* the curve, not off it as a Bezier's would. Verified
  against `SBQTM.C:NVMath_CalcQcurve`.
- `SubdivideIn` packs HEIGHT (strips) in bits 0-3 and WIDTH (texels per strip)
  in bits 4-7, each a power 2..8.
- **The chip does not sample textures.** The CPU resamples into a
  2^HEIGHT x 2^WIDTH grid and streams it, two 16-bit texels per 32-bit write.
  A primitive costs its texel count regardless of how large it lands on screen.
- `NVLIB_DrawQTexTri` does its resampling inside NVLIB and explicitly does no
  bounds checking on texture coordinates, so it cannot be used for Quake world
  surfaces, which rely on GL_REPEAT. nv1Quake resamples itself (masking u/v for
  wrap) and submits through `NVLIB_DrawQTexQuad`, which is identical work and
  behaves the same against real `nvlib.lib`.
- 640x400 at 32K colours is the only double-buffered RGB mode that fits the
  stock 1MB framebuffer.

#### Added

- `nvsoft/` — software NV1. Implements the NVLIB 1.50 subset the port uses,
  with the real signatures: DB pipe, clipping, background/rectangle/image
  (opaque and alpha), quadratic textured quads (plain, lit, cut-out), the
  division LUT, timers, and video mode selection. The patch rasteriser walks
  the surface with second-order forward differences in 16.16 fixed point and
  fills texel quads with incremental edge functions.
- `src/` — the renderer: video and mode setup, 2D and texture manager, the
  transform/clip/submission core, world surfaces, alias models, sprites,
  particles, sky and water.
- `src/nv1_snd.c` — Quake sound output through the NV1's own audio engine, for
  the hardware build. Compiled only under `NV1_HARDWARE`. **Untested**; written
  against the NVLIB audio API and NVLIB's own `AUDFILE.C` usage.
- Framebuffer memory budget: modes are offered only if two 16-bit buffers fit
  in the card's memory. `nv1_vidmem` (or `-vidmem <mb>`) says how much there
  is, so an Edge 3D 2200 with its expansion modules to 2MB gets 640x480 and
  800x600 as well as 640x400.
- NV1 tuning cvars: `nv1_subdiv`, `nv1_maxsubdiv`, `nv1_minsubdiv`,
  `nv1_zslab`, `nv1_worldsubdiv`, `nv1_skylayers`, `nv1_lightmap`,
  `nv1_clear`, `nv1_showtexels`, `nv1_maxtexture`, `nv1_vidmem`.
- `r_drawworld`, which id's `glquake.h` declares but no id file ever defined.

#### Renderer design decisions forced by the hardware

- **Painter's algorithm.** The BSP is walked far-child-first, which is an exact
  back-to-front ordering. Surface marking had to be split into its own pass:
  GLQuake marks surfaces in leaves during the same walk that draws them, which
  only works because it walks near-side-first. Reversing the walk without
  splitting the passes drew almost nothing.
- **Per-vertex lighting.** No multitexture and no lightmap unit, but there is a
  per-control-point beta. Lightmaps are sampled per vertex from the BSP data;
  none of GLQuake's lightmap block allocator or upload scheduling exists.
- **Geometry subdivision** (`nv1_worldsubdiv`, default 128 units). A quadratic
  patch interpolates linearly in texture space, so one patch over a whole wall
  both smears and bows; and with lighting at the corners, a large polygon has
  nothing to interpolate and renders flat.
- **Depth-slab subdivision** (`nv1_zslab`, default 4). Polygons spanning more
  than a 4:1 depth ratio are chopped along z, because the quadratic cannot
  represent that much perspective and overshoots.
- **Solid-rectangle particles.** A textured particle is a whole patch and costs
  its full texel count no matter how small it lands.
- Screenshots, envmap and the flashblend coronas are not implemented — each
  needs a framebuffer readback or an additive blend the chip does not have.

#### Fixed during bring-up

- Console background and character tiles were allocated on the hunk inside
  `Draw_Init`'s `Hunk_LowMark`/`Hunk_FreeToLowMark` pair. Safe in GLQuake
  because the pixels had already gone to OpenGL; here the pixels *are* the
  texture, so they were freed and later redrawn as whatever model and BSP data
  landed on top.
- Lightmap scale was `>> 8`; GLQuake uses `>> 7` clamped to 255. The whole
  world was half as bright.
- Alias model vertices are byte-packed and meaningless until scaled by
  `paliashdr->scale` / `scale_origin`, which GLQuake applies through the
  modelview matrix. Building our own transform and not applying them drew every
  model about ten times too large and offset — most visibly the view model,
  which became a smear across the top-left corner.
- Cut-out transparency: sprites were drawn opaque, putting a black box around
  every explosion. Now uses ARGB1555 with bit 15 as the alpha bit, which is how
  the chip does it. The sky's second (cloud) layer is drawn again as a result —
  it marks transparency with palette index 0 rather than 255, so it is remapped
  on load.
- `nv1_clear` now defaults to on. GLQuake can leave the colour buffer alone
  because the depth buffer plus full coverage guarantees every pixel is
  written each frame; with no depth buffer, anything the world fails to cover
  keeps what was there two frames ago and smears across the screen.
- Gap filling. Forward-mapping a texel grid leaves pinholes where a pixel
  centre falls between two quads — the reason `NV_QTM_GAP_FILLING` exists.
  Coverage now rounds outward.
- Clip buffer was 32 vertices while surfaces of up to 64 were being fed to it,
  silently truncating large polygons into holes in floors.

#### Build notes (nothing under `WinQuake/`, `QW/` or `NV1/` was modified)

- `quakedef.h` reaches the renderer interface through a quoted
  `#include "glquake.h"`, which MSVC resolves relative to `WinQuake` before any
  `-I` path, so id's header cannot be shadowed. It is used as-is; its `<GL/gl.h>`
  is satisfied by a type-only shim in `src/GL/` (angle-bracket includes *do*
  honour `-I` order) and the NV1 interface lives in `src/nv1quake.h`.
- `mathlib.c`, `world.c` and `snd_mix.c` are compiled with `WINDED`, which
  stops `quakedef.h` defining `__i386__` and so keeps the C equivalents of the
  `.s` files that we do not assemble.
- `net_wins.c` and `net_wipx.c` declare a local `int errno`, which a modern CRT
  turns into nonsense; they are force-included with `src/nv1_errno.h`.
- `src/nv1_fpu.c` supplies the four FPU control-word helpers that `sys_win.c`
  expects from `sys_wina.s`.

#### Known remaining work

- Water is opaque (`r_wateralpha` is ignored); the patch path has no per-texel
  alpha blending, only the 1-bit cut-out.
- Brush model (door, platform) surfaces are drawn in list order rather than
  sorted, so they can self-overlap incorrectly.
- Dynamic lights are per-vertex only; `gl_flashblend` coronas are gone.
- Phase 2 and 3 not started. `nv1_snd.c` is written but has never run.

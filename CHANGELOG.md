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

## v0.12

### New: clearer DOS start-up screen (2026-10-16)

**User-facing:** The DOS version now shows what it is doing as it starts. A title box is followed by one line per step: finding the NV1 card, checking how much memory is free, and setting aside memory for the game, each ending in a coloured DONE, WARN or FAILED. If memory is short, it says so on screen instead of failing later. The last line says LAUNCHING when everything is ready. If no NV1 card is found, the game now stops straight away with a clear "No NV1 detected" message instead of carrying on. If your card is not on the PCI bus, start the game with `-nopcicheck` to skip that check.

**Implementation:** New `src/nv1_boot.c/.h`, compiled into the DOS build only (`build-dos.bat`). Output is plain `printf` plus BIOS int 10h function 09h for colour, so it works in any text mode. The card is found by a PCI BIOS scan (INT 1Ah B101h/B102h for 12D2:0008/0009 and 104A:0008/0009, the same ids as the diagnostic log) and not through `NvRequestVideoMode`, because that call switches to graphics mode and would hide the result. A miss (or no PCI BIOS) is a FAILED line plus `Sys_Error` before the heap is allocated or the driver is touched; `-nopcicheck` downgrades it to a WARN for boards that are not on PCI. Checked under DOSBox 0.74 (no PCI BIOS there, so it takes the stop path, each line on its own row). Every step is also written to `NV1DIAG.LOG` when `-diag` is on. `NV1QUAKE_VERSION` was still 0.10 and is now 0.12, so the banner matches the release.

### Fixed: DOS version ran out of memory on a 16 MB machine (2026-10-16)

**User-facing:** On a computer with 16 MB of memory the DOS version could get most of the way into loading and then stop with an out-of-memory error. It now works out how much memory is free and keeps enough back for the game's textures. If memory is still too tight, it warns before starting, and the error message says what to try. Starting the game with `-mem` followed by a number still lets you choose the size yourself.

**Implementation:** The engine heap used to ask for 16 MB and shrink 1 MB at a time until `malloc` succeeded, which took everything free; textures are `malloc`'d separately (`NV1_UploadIndexed`, RGB555 mip chains) and then had nothing left. `Boot_PlanHeap` now sizes the heap from the DPMI largest free block (INT 31h 0500h): that value minus a 5 MB texture reserve, clamped to 6..16 MB. `-mem N` is honoured (minimum 4, clamped to what is free). If DPMI will not report, the old 16 MB request is used. "Tight" is flagged when under 4 MB is left for textures. `Boot_QueryRam` adds `_memmax()` (the largest free block the C library already holds) to the DPMI figure, because the `-diag` malloc probe leaves about 8 MB there and the reading otherwise dropped from 11.9 MB to 3.7 MB just by turning `-diag` on. `tests/boot_heap_test.c` covers the sizing arithmetic on the host. The texture out-of-memory errors in `nv1_draw.c` now say what to do.
The full fix for small machines is to keep textures as 8-bit indexed data and convert to RGB555 while feeding the chip, which would halve texture memory. Not done here; it costs CPU time per texel and needs its own design.

---

## v0.11

### Internal: one release script, with the changelog on the product page (2026-10-15)

**User-facing:** There is no change to the games. Behind the scenes, nv1Quake is now released the same way as the other Abnormality Software apps, and its web page now shows what changed in each version.

**Implementation:** `NV1Quake\installers\build-release.ps1` replaces `build-installers.ps1` + `publish-to-site.ps1` (archived in `installers\_old\`, which is gitignored along with every `*.ps1` here). One run rebuilds the three games, builds the three installers and the source zip, uploads the four files to R2, updates `apps\nv1quake.html` in place (file names, version, a size and SHA-256 span per download, and the `<!-- CHANGELOG-START/END -->` block), writes the public `apps\NV1Quake\CHANGELOG.md`, updates `versions.json` through `Update-VersionsEntry`, and commits (`--only`) and pushes the site repo. It uses `X:\repos\ReleaseTools\SiteRelease.psm1` like the other apps. Pre-flight, before any build: the newest `## vX.Y.Z` heading must equal `installers\VERSION` with nothing left under `## Unreleased`; the tracked tree must be clean, with no untracked files under `NV1Quake\` or `WinQuake\` (`git archive` would leave them out of the source zip); HEAD must be on origin (the installers' notice promises the source of exactly this commit); and the product page must still have its spans and markers. The public changelog is the file from the first version heading to `## Pre-release history`. `BUILD-INFO.json` and the dry-run publish are gone because one run now does both halves from one commit. The page is no longer generated from `site\nv1quake.html.in` (removed); the live page was edited once to add the spans and the changelog section. `lib\release.ps1` holds the helpers; `tests\run-tests.ps1` covers them.

### New: diagnostic log for troubleshooting (-diag) (2026-10-15)

**User-facing:** All three versions of nv1Quake (DOS, Windows 95 and the emulated Windows version) can now write a detailed troubleshooting log. Start the game with `-diag` to create a file called NV1DIAG.LOG in the game folder, or with `-diagv` for a much more detailed one. The log records the computer's memory and system details, whether the NV1 card can be found, and every step of start-up. It is written as the game runs, so even if the game crashes or the computer locks up, everything up to the last step is saved. If the log does not end with a line starting "EXIT", the game did not shut down cleanly, and the last line shows where it stopped. Nothing is written unless you ask for it.

**Implementation:** New `src/nv1_diag.c/.h`, compiled into all three builds with `NV1_DIAG` defined (`build-dos.bat`, `build-nv1.bat`, `build.bat`). `Diag_Early` scans raw argv for `-diag`/`-diagv` before `COM_InitArgv`. Each log line is fopen/append/fclose so it survives a crash or reset; capped at 512 KB carry-over and 4 MB per run. Startup report: build id, argv, DOS reported and true version (INT 21h 30h/3306h), Windows-present check, XMS, DPMI version and free memory (INT 31h 0400h/0500h), PCI scan for 12D2:0008/0009 and 104A:0008/0009 with command/BAR/IRQ (INT 1Ah B1h), BIOS video mode, environment, Win32 version/CPU/memory/desktop, self-tests (type sizes, struct strides, `strcmp`, float conversions, a malloc probe from 64 MB down). A Win32 unhandled-exception filter logs the code, EIP, registers, 24 stack dwords and an EBP caller chain (match addresses against the `.map`); `signal()` handlers log SIGSEGV/SIGILL/SIGFPE/SIGABRT then re-raise so DOS/4GW still prints its own crash dump. `atexit` records an exit that bypassed `Sys_Quit`/`Sys_Error`. Hooks are guarded by `#ifdef NV1_DIAG` and use the `DIAG_*` macros: `Sys_Error`/`Sys_Quit`/`Sys_Printf` (so all console text lands in the log) in `nv1_sysdos.c` and `sys_win.c`; heap request and result; a checkpoint with a memory line at each `Host_Init` stage plus `Hunk_Check`/`Z_CheckHeap`/`_heapchk` passes; `Memory_Init`, hunk-full and (verbose) every hunk allocation in `zone.c`; pak header, per-entry bounds validation against the file length, directory CRC, and (verbose) every `COM_FindFile`/`COM_LoadFile` in `common.c`; every `NvRequestVideoMode`/`NvGetVideoMode`/`NVLIB_*` init call, the first three flips, mouse reset and audio init. Changes to the `WinQuake/` files are additive and invisible to builds that do not define `NV1_DIAG`. The installer stages and `installers/output` still hold the old executables until the installers are rebuilt.

### Fixed: Windows 95 build crashed on start-up on real NV1 hardware (2026-10-15)

**User-facing:** The Windows 95 version of nv1Quake crashed with an "illegal operation" message as soon as it started loading the game files. It now starts normally.

**Implementation:** `nv1q95.exe` faulted at 0x407dd3 inside `COM_LoadPackFile`, whose `dpackfile_t info[MAX_FILES_IN_PACK]` local makes a 128 KB stack frame. The Open Watcom build uses `-s` (no stack probes) and the linker only committed 64 KB of the 1 MB stack; Win9x grows a stack one guard page at a time, so the first touch past the frame skipped the guard page and page-faulted. NT is more forgiving, which is why the emulated build was fine. `build-nv1.bat` now adds `commit stack=0x100000`, so the whole stack is committed at load. This also covers the other 4 KB+ frames (`CL_ParseServerInfo` 32 KB, `Host_Loadgame_f` 33 KB, `Con_CheckResize`, and others). Not yet confirmed on real hardware. The installer stage and `installers/output` still hold the old exe until the installers are rebuilt.

---

## v0.10

### New: Installers for all three builds, the Fragged logo, and a product page (2026-10-03)

**User-facing:** nv1Quake now comes with proper installers: one for the emulated build that runs on any modern Windows PC, one for Windows 95 and one for real-mode DOS, plus a zip of the source. They contain the engine only (you bring your own Quake game files), show the licence and credits before installing anything, and use the Fragged logo.

**Implementation:**


`NV1Quake\installers\` now builds three installers from fresh binaries, plus a
source zip and SHA-256 sums: an Inno Setup installer for the emulated build, a Windows 95 installer
and a real-mode DOS installer for the hardware builds. They are engine-only (no Quake game data),
install the GPL text and a notice naming the exact source commit, show that notice before writing
anything, and credit id Software, NVIDIA and the community (see `installers\README.md`).

- **Vendored the two Fragged retro installer engines** into `installers\retro\` and `installers\dos\`
  and added a licence/notice screen to each. The Win32 engine did not compile as shipped (its
  `extract_archive` call disagreed with its own header) - fixed in our copy. The DOS engine was
  hard-wired to Fragged/Duke (banner text, player name, resolution, invert mouse) - it now takes a
  title and can hide those fields, and an unattended `/INSTALLDIR=` install no longer waits for a key.
- **The Fragged logo** is the installer, uninstaller and shortcut icon, the Inno wizard art, the DOS
  splash, and is now embedded in `nv1quake.exe` and `nv1q95.exe` (`src\nv1quake.rc`, `src\nv1q95.rc`;
  the Win95 build uses the 16/32 px icon only, which is all Win95 can render).
- **Product page:** one NV1 Quake page with the three downloads and the source, a prominent
  "community experiment made with love" statement, and the honest status.
- **Bugs found along the way:** the legal-file generator mishandled relative paths; Inno's directory
  page rejects an empty value (aborting silent installs), so the optional "your Quake data" step is a
  custom page with a Browse button and a `/DATADIR=` option; the installer build initially failed to
  run the engine batch files.
- **Open licensing questions** (recorded in each installer's `NOTICE.TXT` and in `ISSUES.md`): the
  Win95/DOS builds statically link NVIDIA's libraries, and `DOS4GW.EXE` comes from Open Watcom; neither
  redistribution grant could be confirmed.
- Not tested on real hardware: neither the Win95 nor the DOS installer has run on real Windows 95 or
  real DOS (Win95 is checked for PE 4.0 and Win95-era imports; DOS runs under DOSBox).
- **After an independent review** the following were fixed: the Win95 uninstaller deleted the whole
  install folder (including the user's `id1` PAKs, saves and config, or other programs if a shared
  folder was chosen) - it now removes only the files in its own manifest, plus its shortcuts, and has a
  `/uninstall /silent` mode; the Inno uninstaller deleted `id1\` (saves, config.cfg) - it now removes only
  the PAKs the installer copied; a release now verifies that every installer and the source zip came
  from HEAD, a clean tree and a fresh rebuild, and that HEAD is on origin (the notice promises the
  exact-commit source archive).

---

### Fixed: DOS build "Corrupted data file" at startup (2026-10-03)

**User-facing:** The DOS build no longer stops at startup with a "Corrupted data file" error, so it now gets as far as loading the game data under DOSBox.

**Implementation:**

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

## Pre-release history

Work from before the first versioned release (v0.10, the installers). No version numbers were assigned at the time.

### New: Phase 2, links against the real NVLIB and targets Windows 95 (2026-09-07, later)

**User-facing:** There is now a Windows 95 build that talks to the real NVIDIA driver library instead of the emulator, along with several drawing fixes (the weapon model, doors and transparency) and a window size that matches the card's real 640x400 mode. It has not yet been run on actual NV1 hardware.

**Implementation:**

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

### New: Phase 1, first working build (2026-09-07)

**User-facing:** nv1Quake can now play Quake through a software emulation of the NVIDIA NV1 chip, at 640x400 with 32768 colours, running at about 22 frames per second on a demo.

**Implementation:**

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

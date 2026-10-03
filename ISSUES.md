# nv1Quake — open issues and ideas

Snapshot as of 2026-09-08. Written as a handoff: read this first when picking
the project back up.

`CHANGELOG.md` has the full design record and everything already solved.
`currenttask.md` has the phase plan. This file is only what is still wrong and
what to do about it.

---

## Where things stand

Three builds, all of which compile and link cleanly right now:

| Build | Command | Output | State |
|---|---|---|---|
| Software NV1 (Windows 11) | `build.bat` | `NV1Quake\nv1quake.exe` | **Works, playable** |
| Real NV1, Windows 95 | `build-nv1.bat` | `NV1Quake\nv1q95.exe` | Links; **never run** |
| Real NV1, DOS4GW | `build-dos.bat` | `NV1Quake\nv1qdos.exe` | Pak/startup bug fixed (#1); never run on hardware |

Ready-to-run folders are in `Release\`:
`NV1Quake-Emulated\` (Windows 11), `NV1Quake\` (Win95), `NV1Quake-DOS\` (DOS).
Each has `id1\` and a README.

Baseline performance (software build, `demo1`, 640×400): 969 frames, 44.3 s,
**21.9 fps**, ~1,100 patches and ~650,000 texels per frame.

---

## 1. DOS build: "Corrupted data file" at startup — FIXED 2026-10-03

**Root cause: `NVRM.LIB` ships its own `strcmp_`.** Its `osinit.c` module
defines a private `strcmp_`, and because that module is linked in for the
resource manager, every `strcmp` in the program bound to NVIDIA's version
instead of Watcom's. It does not behave like the standard function:
`COM_FindFile` matched `gfx/pop.lmp` against entry 0 of `pak1.pak`. The pak
directory itself was always correct (debug print: entry 55 = `gfx/pop.lmp` at
1896636, `sizeof(packfile_t)` = 72, `newfiles` sane).

**Fix:** `src\nv1_strcmp.c` defines a correct `strcmp`, added to the source list
in `build-dos.bat`. Object files beat library definitions, and the map confirms
`strcmp_` now comes from `nv1_strcmp.obj` (the linker prints a harmless W1027
"redefinition of strcmp_ ignored" for the NVRM one).

Ruled out on the way: `-zp8` (kept, harmless), `-od` (reverted to `-otexan`),
hunk memory, struct stride.

**State after the fix:** startup gets past the pak check and DOSBox then sits on
a black screen. DOSBox has no NV1, so a stall at hardware init is expected and
is not evidence of a further bug. **Nothing past the pak check has been
verified.** Other NVRM.LIB / NVLIBDOS.LIB modules may shadow more libc
functions; if something else misbehaves only on DOS, check the link map for
libc names (`*_`) owned by an NVIDIA module.

<details><summary>Original investigation notes (kept for reference)</summary>

**Symptom.** `NV1QUAKE.EXE` under DOSBox gets a long way in and then dies:

```
nv1Quake 0.10 (Quake 1.09) -- NVIDIA NV1 / Diamond Edge 3D
16 MB heap
Added packfile C:/id1/pak0.pak (339 files)
Added packfile C:/id1/pak1.pak (85 files)
PackFile: C:/id1/pak1.pak : gfx/pop.lmp
Sys_Error: Corrupted data file.
```

That is `COM_CheckRegistered` in `common.c` failing its `pop[]` comparison.

**What is already proven** (do not re-derive this):

- DOS4GW loads, the 16MB heap allocates, both PAK directories parse and give
  the *correct* file counts (339 / 85). So the DOS system layer's `fopen`,
  `fseek`, `fread` and the `LittleLong` function pointers all work.
- A standalone DOS test (`NV1Quake\tools\paktest.c`, same compiler flags)
  reads `gfx/pop.lmp` out of `pak1.pak` **perfectly**: finds it at offset
  1896636, length 256, checksum `sum=1290666`, matching what Python computes
  on the host. **Plain file I/O under DOS/DOSBox is not the problem.**
- Instrumenting my `Sys_FileSeek` / `Sys_FileRead` showed Quake asking for:
  `seek h=2 pos=12` then `read h=2 count=256 → sum=3626439`.
  It asked to seek to **12**, not 1896636.
- Offset 12 is `pak1.pak` entry **0** (`sound/misc/basekey.wav`). The correct
  entry is index **55**.
- Linker map confirms the stack is a full 1MB, so the 128KB
  `dpackfile_t info[MAX_FILES_IN_PACK]` array in `COM_LoadPackFile` is **not**
  overflowing it. That theory is dead.

**So:** the file layer is fine. Either `COM_FindFile`'s
`strcmp(pak->files[i].name, filename)` is matching entry 0 when it should match
55, or entry 55's `filepos` reads back as 12. Something is wrong with the
`packfile_t newfiles[]` array that `COM_LoadPackFile` builds in hunk memory.

**Next step, already set up.** Re-add the instrumentation and read the answer
directly. `tools\mkdbg-notes.txt` describes it; in short:

1. Copy `WinQuake\common.c` to `NV1Quake\src\nv1_commondbg.c` and add, right
   after the `Sys_Printf ("PackFile: ...")` line in `COM_FindFile`, a print of
   `i`, `pak->files[i].name`, `pak->files[i].filepos`, `pak->numfiles`,
   `sizeof(packfile_t)`, plus `pak->files[0]` and `pak->files[54]`.
2. In `build-dos.bat`, swap `common` for `nv1_commondbg` in the source lists.
3. Build, copy to `Release\NV1Quake-DOS\NV1QUAKE.EXE`, run under DOSBox with
   `tools\dosbox.conf`.

That single print distinguishes the two hypotheses immediately.

**Leading hypotheses, in order:**

1. **`Hunk_AllocName` returning bad memory in the DOS build.** `newfiles` comes
   from the hunk. If the hunk base or size is wrong, the array could alias
   something. Worth printing `hunk_base`/`hunk_size` and `newfiles` and
   sanity-checking that `newfiles` lands inside the heap `malloc` returned.
   Note the DOS build passes `parms.membase = malloc(16MB)` — check
   `Memory_Init` is getting a properly aligned pointer.
2. **`sizeof(packfile_t)` differing between translation units.** `MAX_QPATH` is
   64 so it should be 72 bytes everywhere, but if some file sees different
   packing the stride would be wrong and index 55 would read garbage. Printing
   `sizeof(packfile_t)` from inside `common.c` and comparing to what
   `nv1_sysdos.c` thinks would settle it. (The DOS build uses no `-zp` flag;
   the Win95 build uses `-zp8`. **Consider adding `-zp8` to `build-dos.bat`
   for consistency** — this is a cheap thing to try first and could be the
   whole bug.)
3. Watcom `-otexan` inlining `strcmp` wrongly. Cheap test: rebuild with `-od`
   and see if the fault disappears. If it does, bisect the optimisation flags.

**Try #2's cheap version first** (add `-zp8` to the DOS `CFLAGS`, rebuild,
run) — it is one line and would explain the symptom exactly.

</details>

---

## 2. First-person weapon models still look wrong

Everything else — world, monsters, items, sky, water — looks right. The view
model does not.

A real bug was already fixed here: alias model *fans* were being handed to the
polygon path whole, and an alias fan is neither convex nor planar, so the
Sutherland-Hodgman clipper mangled it. Both fans and strips are now emitted as
individual triangles. That fixed the "smooth grey cone", but the weapon is
still visibly off.

Why it is the hardest case for this renderer: the view model sits a few units
from the eye, so it is heavily near-plane clipped *and* spans a large
far/near depth ratio, and a quadratic patch represents that badly.

Suspects, in order:

1. `nv1_zslab` (default 4) is not aggressive enough at very short range. Try
   forcing a much smaller ratio for the view model specifically.
2. `NV1_NEARCLIP` is 4.0 world units and is probably cutting into the model.
   glquake gets away with the same near plane because GL rasterises
   perspective-correctly; we do not.
3. Subdivision powers are chosen from *screen extent* (`NV1_SubdivPower`),
   which is wrong for triangles this foreshortened — a triangle can be large on
   screen while spanning almost no texture.

Idea worth trying: render the view model with its own, much larger near clip
and a compressed depth range, the way glquake squashes `glDepthRange` for the
weapon. We have no depth buffer, but we could scale the model's view-space z
into a narrow band before projection, which would remove the huge depth ratio
entirely.

---

## 3. Textures warp while moving — mostly authentic, partly tunable

This is the NV1's defining artifact and is **not** a bug. The chip walks
texture space *linearly* across a patch while the patch's screen shape is
quadratic, so only the nine control points are perspective-correct and the
interior swims. It is why NV1 games looked the way they did.

It can be reduced, at the cost of texels:

- `nv1_worldsubdiv 64` (default 128) — smaller patches, less perspective error
- `nv1_zslab 2` (default 4) — tighter depth slabs

An avenue not yet explored: the subdivision powers are recomputed every frame
from screen extent, so a surface's texel grid resolution *jumps* between e.g.
32 and 64 as you move, which adds shimmer on top of the authentic warp.
**Adding hysteresis to `NV1_SubdivPower` so the power only changes when the
extent moves well past a threshold would likely calm it noticeably** and cost
nothing.

---

## 4. Neither hardware build has ever run

`nv1q95.exe` links against the real `nvlib.lib` and is verified as an i386 PE,
Windows GUI subsystem, subsystem version 4.0, statically linked runtime,
importing `NVAPI.DLL` and `NVVIDMOD.DLL`. But nothing has driven an actual NV1.

When the card arrives:

- Install the NVIDIA/Diamond Win95 driver and resource manager first
  (`NVAPI.DLL` comes from it; `NVVIDMOD.DLL` is shipped in `Release\NV1Quake\`).
- `-vidmem 2` if the Edge 3D 2200 has its expansion modules fitted.
- Expect the texel budget to be far too high for a period CPU. Start with
  `nv1_subdiv 0.5` (quarters the traffic) and `r_speeds 1`.
- Start with `-nosound` — see #5.

---

## 5. Sound

- **Win95:** `src\nv1_snd.c` drives the NV1's own audio engine through NVLIB
  (the Edge 3D is a multimedia card — wavetable synth and Saturn game port on
  the same chip). It compiles and links. **It has never executed.** If it
  misbehaves, `-nosound` is the fallback.
- **DOS:** there is no sound at all. The DOS build links `snd_null.c`. Bringing
  `nv1_snd.c` up on DOS needs the DOS resource manager's audio path; the NVLIB
  audio API is the same, so it is mostly a matter of guarding the Win32-only
  bits (it currently includes `winquake.h` and defines the DirectSound globals
  that `snd_dma.c`/`snd_mix.c` reference).

---

## 6. Smaller renderer issues

- **Water is opaque.** `r_wateralpha` is ignored. The patch path's transparency
  is a 1-bit cut-out (ARGB1555 bit 15), not a blend, so there is no cheap way to
  do translucent water. Would need a software blend during resampling — which
  is possible, since we build the texel grid ourselves, but costs a read of the
  framebuffer per texel.
- **Brush model self-overlap.** Entities are now inserted into the BSP
  back-to-front walk by leaf (this fixed doors drawing over columns), but a
  brush model's *own* surfaces are still drawn in list order, so a door can
  overlap itself incorrectly. Sorting a bmodel's surfaces by distance would fix
  it.
- **Screenshots and `envmap`** are not implemented — both need a framebuffer
  readback (`NVLIB_GetSnapshot`), which needs a pinned region registered at
  startup.
- **`gl_flashblend` coronas** are gone; the chip has no additive blend.
- **Dynamic lights are per-vertex only**, evaluated at the polygon corners.
  With `nv1_worldsubdiv 128` that is coarse for a muzzle flash on a big wall.

---

## 7. Ideas / nice-to-haves

- **Texture cache for the resampler.** Right now every patch resamples its
  texture into the grid from scratch, every frame. NVLIB itself offers
  `NVLIB_DrawCachedQTexTri` + `NVLIB_WarpTextureTriToRect` for exactly this.
  For static world surfaces whose subdivision has not changed, the grid could
  be cached and re-streamed. On real hardware this is potentially the single
  biggest win available, because it attacks the texel budget directly.
- **Hysteresis on subdivision selection** — see #3.
- **`ControlPointOut12d4`.** The hardware class has a S12.4 *subpixel* control
  point path that NVLIB never uses (it only writes the integer
  `ControlPointOut`). Using it would reduce the cracks between adjacent patches
  and probably some of the swim. It would mean bypassing NVLIB and writing the
  object registers directly, which is a bigger change but is well documented in
  `NV1\NV\SDK\INC\NV32.H`.
- **Sega Saturn game port.** `NVLIB_InitGamePort` — it is on the same chip. A
  Saturn pad driving Quake would be a very on-brand touch.
- **`-vidmem 2` modes.** 640×480 and 800×600 are unlocked with the expansion
  modules but have never been exercised.

---

## 8. Installers and licensing

Installers are generated by `NV1Quake\installers\build-installers.ps1` (see its README). Open items:

- **NVIDIA's libraries.** `nv1q95.exe` / `nv1qdos.exe` statically link `nvlib.lib`, `nvlibdos.lib` and
  `nvrm.lib` into GPL-derived code. The SDK ships no licence text, so redistribution inside a GPL
  program is unverified. Worth asking NVIDIA, or checking the SDK CD's own paperwork.
- **`DOS4GW.EXE`** is bundled from Open Watcom; no redistribution grant was found locally.
- **The "Quake" name** is used for a free, unofficial, non-commercial port (per id's LICINFO.TXT).
- **Win95 and DOS installers have never run on real Windows 95 / real DOS.**
- The DOS installer silently truncates an unattended `/INSTALLDIR=` path longer than 63 characters
  (inherited 64-byte buffer); interactive installs cap the field at 40 characters.
- **Product art** is a placeholder (the Fragged logo): replace `installers\art\nv1quake-poster.png`.
- **Publishing** (`publish-to-site.ps1 -Publish`) has not been run; it is a separate, explicit step.

---

## Notes for whoever resumes

- **Nothing under `WinQuake\`, `QW\`, `qw-qc\` or `NV1\` has been modified**,
  and it should stay that way. All the tricks that make that possible are
  documented in `CHANGELOG.md` (the `src\GL\` type shim, `nv1_errno.h`,
  `nv1_w32compat.h`, `nv1_compat.h`, the `WINDED` trick, `nv1_fpu.c`,
  `nv1_sndc.c`, `nv1_msvcrt.c`).
- Toolchains: **MSVC (VS 18)** for the software build, **Open Watcom 2.0**
  (`C:\WATCOM`) for both hardware builds. DOSBox 0.74-3 is installed and
  `tools\dosbox.conf` is set up to mount `Release\NV1Quake-DOS` and run it.
- Nothing is committed to git yet.

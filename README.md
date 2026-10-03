# nv1Quake

**Quake on the NVIDIA NV1.**

This is a fork of [id Software's Quake GPL source release](https://github.com/id-Software/Quake)
that ports GLQuake to the NVIDIA NV1 (Diamond Edge 3D / SGS-Thomson STG2000) - the
1995 accelerator that drew *quadratic patches* instead of triangles, had no depth
buffer, and streamed every texel from the CPU.

id's original source is still here, untouched (`WinQuake/`, `QW/`, `qw-qc/`). All
of the work lives in [`NV1Quake/`](NV1Quake/).

> **The NVIDIA NV1 SDK (1.50) is required to compile this.** It is NVIDIA's, it is
> not included in this repository, and none of the builds will compile without it.
> You also need Quake's game data to run the result. See [Building](#building).

## What we did

- **A renderer written against the real NVLIB 1.50 API.** No depth buffer, so the
  BSP is walked back-to-front (painter's algorithm); lightmaps collapse to
  per-vertex beta; world, models, sprites, particles, sky and water are all drawn
  as quadratic patches with the CPU resampling textures into the grid the chip
  expects.
- **`nvsoft`, a software NV1.** A software implementation of the same NVLIB
  entry points that emulates NV1 semantics, so the port can be built, run and
  iterated on a modern PC (about 22 fps on `demo1` at 640x400).
- **A Windows 95 build** (`build-nv1.bat`) linking NVIDIA's real `nvlib.lib` with
  Open Watcom, including the shims needed to mix NVIDIA's Microsoft-built library
  into a Watcom link.
- **A DOS build** (`build-dos.bat`) against `nvlibdos.lib` + `nvrm.lib` + DOS4GW.
- **Zero changes to id's code.** Everything id wrote under `WinQuake/`, `QW/` and
  `qw-qc/` is exactly as released; the port compiles it as-is and adapts around
  it. The only things removed from id's release are prebuilt binaries and IDE
  leftovers.
- All of it is period-correct ANSI C with no modern CRT or Win32, because the
  target is Windows 95 and DOS.

## Status

| Build | Output | State |
|---|---|---|
| Software NV1 (Windows 11) | `nv1quake.exe` | Works, playable |
| Real NV1, Windows 95 | `nv1q95.exe` | Links; **never run on hardware** |
| Real NV1, DOS | `nv1qdos.exe` | Gets through startup under DOSBox; **never run on hardware** |

Known problems: the first-person weapon models still look wrong, textures swim
(partly authentic NV1 behaviour), water is opaque, and the hardware audio path
has never executed. [`ISSUES.md`](ISSUES.md) is the full list with next steps and
[`CHANGELOG.md`](CHANGELOG.md) is the design record: what the hardware is, what
was designed around it, and what went wrong along the way.

## Building

You supply two things this repo does not contain:

1. **The NVIDIA NV1 SDK 1.50 - required to compile, every build.** Unpack it as
   `NV1/` in the repo root (so that `NV1/NV/SDK/INC` and `NV1/NV/SDK/LIB` exist).
   Even the software-emulated build includes NVIDIA's headers, and the hardware
   builds link NVIDIA's `nvlib.lib`, `nvlibdos.lib` and `nvrm.lib`. It is
   NVIDIA's and is not redistributed here.
2. **Quake's game data**, `id1/PAK0.PAK` (and `PAK1.PAK` for the registered game).
   The shareware data is freely redistributable; the registered data is not.

Then, from `NV1Quake/`:

    build.bat        software NV1, MSVC
    build-nv1.bat    real NV1, Windows 95, Open Watcom 2.0
    build-dos.bat    real NV1, DOS4GW, Open Watcom 2.0

See [`NV1Quake/README.md`](NV1Quake/README.md) for layout and run options.

## Licence

Quake's source is released under the GNU General Public License (see
[`gnu.txt`](gnu.txt)), and this port, being derived from it, is too. The NVIDIA SDK
and the game data are not part of this repository and carry their own terms.

## Thanks

Huge thanks to:

- **everyone at id Software**, for Quake, and for releasing the source so that
  things like this can exist;
- **NVIDIA**, for the NV1 and for shipping an SDK with enough source in it to work
  out how the chip really behaves;
- **the Quake community**, who have kept this game alive and understood for
  nearly thirty years;
- **the retro gaming community**, for preserving hardware, drivers and
  knowledge that would otherwise be lost;
- and [**Carcenomy**](https://www.twitch.tv/carcenomy), for buying an NV1 - which
  is what inspired these builds in the first place.

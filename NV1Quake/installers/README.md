# nv1Quake installers

Three installers, built by one script:

| Variant | Installs | Technology | Output |
|---|---|---|---|
| `emulated` | `nv1quake.exe`, the software-NV1 build for Windows 10/11 | Inno Setup | `NV1Quake-<ver>-Emulated-Setup.exe` |
| `win95` | `nv1q95.exe` + `NVVIDMOD.DLL`, real NV1 on Windows 95 | vendored Fragged Win32 retro installer (CRT-free, i486, PE 4.0) | `NV1Quake-<ver>-Win95-Setup.exe` |
| `dos` | `nv1qdos.exe` + `DOS4GW.EXE`, real NV1 under DOS | vendored Fragged real-mode DOS installer (Open Watcom) | `NV1Quake-<ver>-DOS-Setup.exe` |

Every installer ships **no Quake game data**, installs `COPYING.TXT` (id's `gnu.txt`), `NOTICE.TXT`
and `README.TXT`, creates an empty `id1\` folder, and shows the licence/notice before writing
anything. Each is also published with a source zip (`git archive` of the built commit) and
`SHA256SUMS.txt`.

## Regenerating

From `NV1Quake\installers\`:

    .\build-installers.ps1 -Rebuild               # rebuild all three games, then all three installers
    .\build-installers.ps1 -Variant dos           # only the DOS installer, from the existing nv1qdos.exe
    .\build-installers.ps1 -Rebuild -UpdateRelease  # also refresh the ..\Release\ folders

Results land in `output\`; staging is in `stage\`. Both are git-ignored.

**Build from a clean, committed tree.** `NOTICE.TXT` and the source zip name the commit that was
checked out, and `git archive` only contains committed files. The script warns if tracked files have
uncommitted changes.

### Tools it expects

| Tool | Default path | Parameter |
|---|---|---|
| NVIDIA NV1 SDK 1.50 | `<repo>\NV1\` | (fixed) |
| Inno Setup 7 | `C:\Program Files\Inno Setup 7\ISCC.exe` | `-Inno` |
| MinGW-w64 32-bit | `C:\msys64\mingw32\bin` | `-MinGW` |
| Open Watcom 2.0 | `C:\WATCOM` | `-Watcom` |
| MSVC (for `build.bat`) | VS 18 Community | (in `build.bat`) |

## Tests

    pwsh -NoProfile -File tests\run-tests.ps1

Covers the library: version, templates, the "no game data" guard, legal file generation, staging,
the DOS notice header, the source zip, the publish plan and the `versions.json` editor.

## Where things are

    art\     Fragged logo assets used for icons, the Inno wizard and the product page poster
    legal\   COPYING.TXT, NOTICE.TXT.in, README.TXT.in, id1 placeholder (placeholders are @@KEY@@)
    inno\    NV1Quake-Emulated.iss
    retro\   vendored Win32 retro installer engine (+ VENDORED.txt: where it came from, what changed)
    dos\     vendored DOS installer engine (+ VENDORED.txt)
    site\    product page template
    lib\     PowerShell library (common, stage, build-retro, build-dos, publish)

To change the legal wording, edit `legal\*.in`. To replace the placeholder art, replace the files in
`art\` keeping their names (`nv1quake-poster.png` is the product page poster).

## Publishing

    .\publish-to-site.ps1             # dry run: prints what would be uploaded and changed
    .\publish-to-site.ps1 -Publish    # uploads to R2, writes the page, adds versions.json, pushes

It uploads to `abnormalitysoftware-downloads/NV1Quake/`, writes `apps\nv1quake.html` and the poster
asset, adds an `nv1quake` entry to `versions.json` (text edit, so nothing else in that file changes),
and commits and pushes **only those files** in the site repo. It refuses to run if those files
already have uncommitted changes.

## Licensing notes

The installers follow id Software's licence documents (`readme.txt`, `WinQuake\data\LICINFO.TXT`):
the GPL, source available to everyone, game data not redistributed, no commercial use, no id
branding. These are engineering notes, not legal advice. Three points are **unverified** and are
stated in each installer's `NOTICE.TXT`:

- The Win95 and DOS builds statically link NVIDIA's `nvlib.lib` / `nvlibdos.lib` / `nvrm.lib`. The SDK
  has no licence text, so whether that may be redistributed inside a GPL-derived program is unknown.
  (`NVVIDMOD.DLL`, by contrast, is in the SDK's `REDIST` folder and NVIDIA's docs say it must ship
  with the application.)
- `DOS4GW.EXE` comes from Open Watcom; no redistribution grant was found in its local documentation.
- The name "Quake" is used for a free, unofficial, non-commercial port.

## Not tested on real hardware

Neither the Win95 nor the DOS installer has been run on real Windows 95 or real DOS. The Win95
installer is checked for PE subsystem 4.0 and Win95-era imports; the DOS installer runs under
DOSBox. The DOS installer is small (under 1 MB), well under the size at which Fragged's DOS
installers hit trouble on real hardware, but that is still unproven here.

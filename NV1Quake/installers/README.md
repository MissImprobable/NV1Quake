# nv1Quake installers

Three installers:

| Variant | Installs | Technology | Output |
|---|---|---|---|
| `emulated` | `nv1quake.exe`, the software-NV1 build for Windows 10/11 | Inno Setup | `NV1Quake-<ver>-Emulated-Setup.exe` |
| `win95` | `nv1q95.exe` + `NVVIDMOD.DLL`, real NV1 on Windows 95 | vendored Fragged Win32 retro installer (CRT-free, i486, PE 4.0) | `NV1Quake-<ver>-Win95-Setup.exe` |
| `dos` | `nv1qdos.exe` + `DOS4GW.EXE`, real NV1 under DOS | vendored Fragged real-mode DOS installer (Open Watcom) | `NV1Quake-<ver>-DOS-Setup.exe` |

Every installer ships **no Quake game data**, installs `COPYING.TXT` (id's `gnu.txt`), `NOTICE.TXT`
and `README.TXT`, creates an empty `id1\` folder, and shows the licence/notice before writing
anything. Each is also published with a source zip (`git archive` of the built commit) and
`SHA256SUMS.txt`. The notice in each installer promises "a source archive of exactly this commit",
and the GPL requires the source to be available, so a release is always built from a clean, committed
tree whose commit is on `origin`.

The release version is the single line in `VERSION`.

## Per-build versions

Emulated, Win95 and DOS each have their own version, taken from `CHANGELOG.md`: it is the newest `## vX.Y.Z`
section that has an entry whose `**Applies to:**` line names that build (or `All`). `VERSION` is the release
version and names the source zip. `build-release.ps1` prints a plan such as
`Emulated 0.11 (unchanged, skipped) / Win95 0.11 (unchanged, skipped) / DOS 0.12 (build)`, builds and uploads only
the changed builds, leaves the others' page links, sizes and SHA-256 alone, and stops if nothing changed.
`-RebuildAll` rebuilds and re-uploads everything under the versions the changelog gives. The product page shows
each download's version and labels each changelog entry with its build.

## Where things are

    art\     Fragged logo assets used for icons, the Inno wizard and the product page poster
    legal\   COPYING.TXT, NOTICE.TXT.in, README.TXT.in, id1 placeholder (placeholders are @@KEY@@)
    inno\    NV1Quake-Emulated.iss
    retro\   vendored Win32 retro installer engine (+ VENDORED.txt: where it came from, what changed)
    dos\     vendored DOS installer engine (+ VENDORED.txt)

To change the legal wording, edit `legal\*.in`. The installer icons and wizard art are in `art\`
(replace them keeping their names). The product page poster is Abigail's own art, kept with the
product page and not copied into this repo.

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

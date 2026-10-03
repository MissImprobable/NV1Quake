@echo off
rem ===========================================================================
rem nv1Quake -- PHASE 2 build: real NV1 hardware, Win32 / Windows 95.
rem
rem Toolchain is Open Watcom, for three reasons:
rem
rem   1. It targets Windows 95 properly.  A modern MSVC binary will not run
rem      there at all -- wrong CRT, wrong imports, wrong subsystem version.
rem   2. It does NOT define _M_IX86, so quakedef.h leaves __i386__ undefined
rem      and id386 comes out 0.  Every C equivalent of Quake's .s files is
rem      then compiled automatically, and none of the workarounds the modern
rem      build needs (WINDED, nv1_fpu.c) apply here.  sys_win.c even supplies
rem      its own FPU control-word stubs under #ifndef _M_IX86.
rem   3. NVIDIA shipped Watcom flavours of the SDK import libraries and built
rem      the DOS library with Watcom 10.5, so it is the toolchain the SDK
rem      itself expects for anything that is not MSVC 2.0.
rem
rem Calling convention is forced to cdecl (-ecc): nvlib.lib was built with
rem MSVC and is cdecl throughout, while Watcom would otherwise default to its
rem own register convention and nothing would link.
rem
rem This links the REAL nvlib.lib.  nvsoft/ is not built and NV1_SOFT is not
rem defined -- there is no emulator in this binary, it drives the chip.
rem ===========================================================================
setlocal EnableDelayedExpansion

if "%WATCOM%"=="" set WATCOM=C:\WATCOM
set PATH=%WATCOM%\binnt;%WATCOM%\binw;%PATH%
set INCLUDE=%WATCOM%\h;%WATCOM%\h\nt
set LIB=%WATCOM%\lib386;%WATCOM%\lib386\nt

set SDK=..\NV1\NV\SDK
set OUT=objnv1
if not exist %OUT% mkdir %OUT%

rem -bt=nt   target Win32          -ecc     cdecl, to match nvlib.lib
rem -otexan  optimise for speed    -zp8     8 byte packing, as MSVC used
rem -zq      quiet                 -s       no stack overflow checks
set INC=-i=src -i=..\WinQuake -i=%SDK%\INC -i=..\WinQuake\dxsdk\SDK\INC
set DEF=-DWIN32 -D_WINDOWS -DGLQUAKE -DNV1_HARDWARE
set CFLAGS=-bt=nt -zq -ecc -otexan -zp8 -s -w1 %DEF% %INC%

echo compiling the NV1 renderer...
for %%f in (nv1_vid nv1_draw nv1_rmain nv1_rsurf nv1_warp nv1_model nv1_mesh
            nv1_rmisc nv1_rlight nv1_refrag nv1_screen nv1_part nv1_snd
            nv1_fpu nv1_sndc nv1_msvcrt) do (
  wcc386 %CFLAGS% -fo=%OUT%\ src\%%f.c
  if errorlevel 1 goto fail
)

echo compiling the shared engine...
for %%f in (chase cd_win cl_demo cl_input cl_main cl_parse cl_tent cmd common
            conproc console crc cvar host host_cmd in_win keys menu
            net_dgrm net_loop net_main net_vcr net_win
            pr_cmds pr_edict pr_exec sbar snd_dma snd_mem snd_mix sv_main
            sv_move sv_phys sv_user view wad zone) do (
  wcc386 %CFLAGS% -fo=%OUT%\ ..\WinQuake\%%f.c
  if errorlevel 1 goto fail
)

rem Watcom DOES define _M_IX86, so quakedef.h sets id386 and compiles out the
rem C equivalents of the .s files.  Same fix as the modern build: WINDED stops
rem quakedef.h defining __i386__.  Only these three, because WINDED also turns
rem VID_LockBuffer into a macro, and snd_mix.c -- the third file that needs
rem this -- includes winquake.h where VID_LockBuffer is declared, so it is
rem compiled normally and src/nv1_sndc.c supplies its two missing routines.
for %%f in (mathlib world) do (
  wcc386 %CFLAGS% -DWINDED -fo=%OUT%\ ..\WinQuake\%%f.c
  if errorlevel 1 goto fail
)

rem sys_win.c passes int* to Win32 calls that want LPDWORD; see the header.
wcc386 %CFLAGS% -fi=nv1_w32compat.h -fo=%OUT%\ ..\WinQuake\sys_win.c
if errorlevel 1 goto fail

rem net_wins.c and net_wipx.c declare a local `int errno`, which was fine in
rem 1996 and is nonsense to any CRT where errno is a macro.  Same fix as the
rem modern build: force-include a header that undefines it, for these two only.
for %%f in (net_wins net_wipx) do (
  wcc386 %CFLAGS% -fi=nv1_errno.h -fo=%OUT%\ ..\WinQuake\%%f.c
  if errorlevel 1 goto fail
)

echo linking against the real NVLIB...

rem Build the linker directive file.
rem
rem NVVMWC.LIB and NVW32WC.LIB are the WATCOM flavours of the SDK import
rem libraries, and they are the right ones here: nvvidmod.h declares
rem NvRequestVideoMode with `#pragma aux (__stdcall) ... "*"` under __WATCOMC__,
rem which asks for an UNDECORATED name, while the MS flavours export the
rem decorated _NvRequestVideoMode@16.  NVIDIA shipped both for this reason.
rem nt_win, not nt: Quake's entry point is WinMain and it is a GUI app.
rem Plain `system nt` produces a console-subsystem binary.
> nv1q95.lnk echo system nt_win
>> nv1q95.lnk echo name nv1q95.exe
>> nv1q95.lnk echo option quiet
>> nv1q95.lnk echo option map=nv1q95.map
>> nv1q95.lnk echo option stack=0x100000
>> nv1q95.lnk echo libpath %WATCOM%\lib386
>> nv1q95.lnk echo libpath %WATCOM%\lib386\nt
for %%f in (%OUT%\*.obj) do >> nv1q95.lnk echo file %%f
>> nv1q95.lnk echo library %SDK%\LIB\NVLIB.LIB
>> nv1q95.lnk echo library %SDK%\LIB\NVVMWC.LIB
>> nv1q95.lnk echo library %SDK%\LIB\NVW32MS.LIB
>> nv1q95.lnk echo library ..\WinQuake\dxsdk\SDK\LIB\DXGUID.LIB
>> nv1q95.lnk echo library winmm.lib
>> nv1q95.lnk echo library wsock32.lib
rem clib3s / math387s are the STACK (cdecl) flavours of the Watcom runtime.
rem `system nt` pulls in the register-convention ones by default, which do not
rem carry the _sprintf style names our -ecc objects and nvlib.lib both want.
>> nv1q95.lnk echo library clib3s.lib
>> nv1q95.lnk echo library math387s.lib

wlink @nv1q95.lnk
if errorlevel 1 goto fail

echo.
echo built nv1q95.exe -- NV1 hardware build, for the retro box.

echo (build.bat still builds nv1quake.exe, the software-NV1 build for here.)
goto :eof

:fail
echo.
echo BUILD FAILED
exit /b 1

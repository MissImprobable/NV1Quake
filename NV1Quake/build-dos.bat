@echo off
rem ===========================================================================
rem nv1Quake -- PHASE 3 build: DOS4GW, real NV1 hardware.
rem
rem Toolchain is forced here, it is not a preference: NVIDIA built
rem nvlibdos.lib with Watcom 10.5 and it is an OMF library, so DJGPP -- which
rem is what id built DOS Quake with -- cannot link it at all.  Watcom it is,
rem and that is why this build carries its own system and input layers rather
rem than using id's sys_dos.c (DJGPP headers: dpmi.h, sys/nearptr.h, dir.h)
rem and dosasm.s (GAS syntax).
rem
rem Flags follow the SDK's own DOSDEMO makefile: /bt=dos /l=dos4g /mf /5r,
rem i.e. flat model and the Pentium REGISTER calling convention.  Note the
rem difference from the Windows build, which forces cdecl with -ecc to match
rem the Microsoft-built nvlib.lib.  Here the library is Watcom's own, so the
rem native convention is the correct one -- and no MSVC runtime shim is needed.
rem
rem quakedef.h only defines __i386__ when _WIN32 is set, so on DOS id386 comes
rem out 0 by itself and every C equivalent of Quake's .s files is compiled.
rem No WINDED, no nv1_fpu.c, no nv1_sndc.c.
rem ===========================================================================
setlocal

if "%WATCOM%"=="" set WATCOM=C:\WATCOM
set PATH=%WATCOM%\binnt;%WATCOM%\binw;%PATH%
set INCLUDE=%WATCOM%\h
set LIB=%WATCOM%\lib386;%WATCOM%\lib386\dos

set SDK=..\NV1\NV\SDK
set OUT=objdos
if not exist %OUT% mkdir %OUT%

set INC=-i=src -i=..\WinQuake -i=%SDK%\INC
set DEF=-DDOS -DGLQUAKE -DNV1_HARDWARE
set CFLAGS=-bt=dos -mf -5r -zq -zp8 -otexan -s -w1 %DEF% %INC%

echo compiling the NV1 renderer...
for %%f in (nv1_viddos nv1_sysdos nv1_indos nv1_draw nv1_rmain nv1_rsurf
            nv1_warp nv1_model nv1_mesh nv1_rmisc nv1_rlight nv1_refrag
            nv1_screen nv1_part nv1_strcmp) do (
  wcc386 %CFLAGS% -fo=%OUT%\ src\%%f.c
  if errorlevel 1 goto fail
)

echo compiling the shared engine...
rem Null drivers for sound, CD and networking.  The NV1's own audio engine is
rem wired up in src/nv1_snd.c for the Windows build; bringing it up on DOS
rem needs the DOS resource manager's audio path and is the next step, not this
rem one.
for %%f in (chase cl_demo cl_input cl_main cl_parse cl_tent cmd common console
            crc cvar host host_cmd keys mathlib menu pr_cmds pr_edict pr_exec
            sbar snd_null cd_null net_none net_main net_vcr net_loop sv_main
            sv_move sv_phys sv_user view wad world zone) do (
  wcc386 %CFLAGS% -fo=%OUT%\ ..\WinQuake\%%f.c
  if errorlevel 1 goto fail
)

echo linking against the DOS NVLIB...

> nv1qdos.lnk echo system dos4g
>> nv1qdos.lnk echo name nv1qdos.exe
>> nv1qdos.lnk echo option quiet
>> nv1qdos.lnk echo option map=nv1qdos.map
>> nv1qdos.lnk echo option stack=0x100000
>> nv1qdos.lnk echo libpath %WATCOM%\lib386
>> nv1qdos.lnk echo libpath %WATCOM%\lib386\dos
for %%f in (%OUT%\*.obj) do >> nv1qdos.lnk echo file %%f
>> nv1qdos.lnk echo library %SDK%\LIB\NVLIBDOS.LIB
>> nv1qdos.lnk echo library %SDK%\LIB\NVRM.LIB

wlink @nv1qdos.lnk
if errorlevel 1 goto fail

echo.
echo built nv1qdos.exe -- needs DOS4GW.EXE beside it
goto :eof

:fail
echo.
echo BUILD FAILED
exit /b 1

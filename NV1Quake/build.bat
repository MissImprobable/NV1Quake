@echo off
rem ---------------------------------------------------------------------------
rem nv1Quake build, phase 1 (software NV1).
rem
rem Deliberately plain: straight command lines, 32 bit x86, C only, no modern
rem toolchain features, so the same lines port to a period compiler with only
rem the paths changed.
rem ---------------------------------------------------------------------------
setlocal
if "%VCINSTALLDIR%"=="" call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul

set OUT=obj
if not exist %OUT% mkdir %OUT%

set INC=/I"src" /I"nvsoft" /I"..\WinQuake" /I"..\NV1\NV\SDK\INC" /I"..\WinQuake\dxsdk\SDK\INC"
set DEF=/DWIN32 /D_WINDOWS /DGLQUAKE /DNV1_SOFT /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_WARNINGS /FInv1_compat.h
set CFLAGS=/nologo /c /TC /W3 /O2 /Oi /GS- /fp:fast %DEF% %INC% /Fo%OUT%\

set NV1SRC=src\nv1_vid.c src\nv1_draw.c src\nv1_rmain.c src\nv1_rsurf.c src\nv1_warp.c src\nv1_model.c src\nv1_mesh.c src\nv1_rmisc.c src\nv1_rlight.c src\nv1_refrag.c src\nv1_screen.c src\nv1_part.c src\nv1_fpu.c

set SOFTSRC=nvsoft\nvsoft.c

set QSRC=..\WinQuake\chase.c ..\WinQuake\cd_win.c ..\WinQuake\cl_demo.c ..\WinQuake\cl_input.c ..\WinQuake\cl_main.c ..\WinQuake\cl_parse.c ..\WinQuake\cl_tent.c ..\WinQuake\cmd.c ..\WinQuake\common.c ..\WinQuake\conproc.c ..\WinQuake\console.c ..\WinQuake\crc.c ..\WinQuake\cvar.c ..\WinQuake\host.c ..\WinQuake\host_cmd.c ..\WinQuake\in_win.c ..\WinQuake\keys.c ..\WinQuake\menu.c ..\WinQuake\net_dgrm.c ..\WinQuake\net_loop.c ..\WinQuake\net_main.c ..\WinQuake\net_vcr.c ..\WinQuake\net_win.c ..\WinQuake\pr_cmds.c ..\WinQuake\pr_edict.c ..\WinQuake\pr_exec.c ..\WinQuake\sbar.c ..\WinQuake\snd_dma.c ..\WinQuake\snd_mem.c ..\WinQuake\snd_win.c ..\WinQuake\sv_main.c ..\WinQuake\sv_move.c ..\WinQuake\sv_phys.c ..\WinQuake\sv_user.c ..\WinQuake\sys_win.c ..\WinQuake\view.c ..\WinQuake\wad.c ..\WinQuake\zone.c

cl %CFLAGS% %NV1SRC% %SOFTSRC% %QSRC%
if errorlevel 1 goto fail

rem ---------------------------------------------------------------------------
rem mathlib.c, world.c and snd_mix.c keep the C equivalents of Quake's hand
rem written assembly behind #if !id386.  quakedef.h sets id386 from __i386__,
rem which it defines itself whenever _M_IX86 is set, so on a 32 bit build those
rem C functions vanish and BoxOnPlaneSide, SV_HullPointContents,
rem Snd_WriteLinearBlastStereo16 and SND_PaintChannelFrom8 go undefined.  We do
rem not assemble the .s files -- they are GAS syntax for the DOS and Linux
rem builds -- so the C paths are the ones we want.
rem
rem WINDED is the switch that stops quakedef.h defining __i386__ at all.  It
rem also turns VID_LockBuffer/VID_UnlockBuffer into empty macros, which is why
rem only these three files get it: none of them call either.
rem ---------------------------------------------------------------------------
cl %CFLAGS% /DWINDED ..\WinQuake\mathlib.c ..\WinQuake\world.c ..\WinQuake\snd_mix.c
if errorlevel 1 goto fail

rem net_wins.c and net_wipx.c shadow errno with a local; see src\nv1_errno.h
cl %CFLAGS% /FInv1_errno.h ..\WinQuake\net_wins.c ..\WinQuake\net_wipx.c
if errorlevel 1 goto fail

rc /nologo /fo %OUT%\nv1quake.res src\nv1quake.rc
if errorlevel 1 goto fail

link /nologo /OUT:nv1quake.exe /SUBSYSTEM:WINDOWS %OUT%\*.obj %OUT%\nv1quake.res kernel32.lib user32.lib gdi32.lib winmm.lib wsock32.lib comctl32.lib "..\WinQuake\dxsdk\SDK\LIB\dxguid.lib"
if errorlevel 1 goto fail

echo.
echo built nv1quake.exe
goto :eof

:fail
echo.
echo BUILD FAILED
exit /b 1

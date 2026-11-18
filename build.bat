@echo off
rem Builds bin\koora.exe with MSVC. Debug by default; pass "release" for an optimized build.
setlocal enabledelayedexpansion
pushd "%~dp0"

rem Find MSVC if cl is not already on the PATH (not run from a Native Tools prompt).
rem No paths inside ( ) blocks: the ")" in "Program Files (x86)" would end the block.
where cl >nul 2>nul
if not errorlevel 1 goto have_msvc

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_vswhere

set "VSPATH="
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" goto no_msvc

rem vcvars64.bat runs vswhere.exe by name, so it must be on the PATH.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

:have_msvc
if not exist bin mkdir bin
if not exist obj mkdir obj

set COMMON=/nologo /std:c++20 /W4 /WX /EHsc /utf-8 /Isrc /external:I third-party /external:W0 /Fo:obj\ /Fd:obj\
set FLAGS=%COMMON% /Zi /Od
if "%1"=="release" set FLAGS=%COMMON% /O2 /DNDEBUG

set SOURCES=
for /r src %%f in (*.cpp) do set SOURCES=!SOURCES! "%%f"

cl %FLAGS% %SOURCES% /Fe:bin\koora.exe /link /INCREMENTAL:NO
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%

:no_vswhere
echo build: vswhere.exe not found. Install Visual Studio or the Build Tools.
popd
exit /b 1

:no_msvc
echo build: no Visual Studio install with the C++ tools found.
popd
exit /b 1

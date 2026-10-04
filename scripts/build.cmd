@echo off
rem Builds a Windows preset: scripts\build.cmd <preset> [target ...]
rem   scripts\build.cmd windows-msvc
rem   scripts\build.cmd windows-msvc-release daw_app
rem
rem Why this script and not a bare "cmake --build" (S22): the compiler on this
rem machine speaks French, and its /showIncludes lines start with
rem "Remarque : inclusion du fichier :", whose two spaces are non-breaking.
rem CMake records that prefix in UTF-8 at configure time; Ninja compares it
rem byte for byte with what cl writes, and cl writes in the console's code
rem page: 0xFF under 850, the default of cmd and PowerShell. The lines then do
rem not match, Ninja records no header for the files it compiles, and a later
rem change to a header does not rebuild them: the stale object of S20 and S21.
rem Under code page 65001 cl writes the same bytes CMake recorded.
setlocal
rem Before any shift: shift moves %0 too.
set ROOT=%~dp0..
if "%~1"=="" (
    echo usage: scripts\build.cmd ^<preset^> [target ...]
    exit /b 2
)
set PRESET=%~1
shift

chcp 65001 >nul

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSROOT=%%i
if not defined VSROOT (
    echo build.cmd: no Visual Studio with the C++ tools found
    exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cd /d "%ROOT%"
set TARGETS=
:targets
if "%~1"=="" goto build
set TARGETS=%TARGETS% %~1
shift
goto targets

:build
if "%TARGETS%"=="" (
    cmake --build --preset %PRESET%
) else (
    cmake --build --preset %PRESET% --target %TARGETS%
)

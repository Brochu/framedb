@echo off
setlocal

REM Build entry point. Sets up the MSVC environment itself if it isn't already present,
REM so this works from a plain shell, a Developer PowerShell, a VS Code task, or CI --
REM the build must not depend on which shell happened to launch it.

set "TARGETS=%*"
if "%TARGETS%"=="" set "TARGETS=all"

REM Already in a developer shell? Then skip vcvars; it costs a second or two.
where msbuild >nul 2>&1
if not errorlevel 1 goto :build

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [compile] vswhere.exe not found -- is Visual Studio installed?
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo [compile] No Visual Studio install with the C++ x64 toolset was found.
    exit /b 1
)

REM vcvars64.bat calls vswhere without a full path, so it warns if the installer directory
REM isn't on PATH. Harmless, but noisy on every build.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"

REM x64 specifically: the x86 toolset would produce objects that fail to link
REM against the x64 renderdoc.lib.
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [compile] Failed to initialise the MSVC environment.
    exit /b 1
)

:build
msbuild -nologo config.xml -t:%TARGETS%
exit /b %errorlevel%

@echo off
rem Builds the given targets (default: engine-sim-server engine-sim-app) in the folder "build" (or %BUILD_DIR%).
rem Needs: Visual Studio with the C++ workload (CMake and Ninja come with it), vcpkg with sdl2, sdl2-image,
rem boost-filesystem (triplet x64-windows-static-md), WinFlexBison (winget install WinFlexBison.win_flex_bison),
rem and the environment variable VCPKG_ROOT. Run it from the project folder.
setlocal
if not defined VCPKG_ROOT (
    echo Set the environment variable VCPKG_ROOT to your vcpkg folder.
    exit /b 1
)
set "OWN_VCPKG=%VCPKG_ROOT%"
if not defined BUILD_DIR set "BUILD_DIR=build"
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
set "VSDIR="
for /f "usebackq delims=" %%i in (`vswhere -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio not found
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
rem vcvars points VCPKG_ROOT to the vcpkg inside Visual Studio, which does not have our packages
set "VCPKG_ROOT=%OWN_VCPKG%"
set "CM=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%CM%\CMake\bin;%CM%\Ninja;%PATH%"
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    cmake -S . -B "%BUILD_DIR%" -G Ninja -C tools\windows-build\init.cmake || exit /b 1
)
set "TARGETS=%*"
if "%TARGETS%"=="" set "TARGETS=engine-sim-server engine-sim-app"
cmake --build "%BUILD_DIR%" --target %TARGETS%

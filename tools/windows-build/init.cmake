# Initial CMake cache for Windows (MSVC + vcpkg, static runtime-friendly triplet).
# Usage: cmake -S . -B build -G Ninja -C tools/windows-build/init.cmake      (VCPKG_ROOT must point to your vcpkg)
# vcpkg packages: sdl2 sdl2-image boost-filesystem  with triplet x64-windows-static-md
if (NOT DEFINED ENV{VCPKG_ROOT})
    message(FATAL_ERROR "Set the environment variable VCPKG_ROOT to your vcpkg folder (e.g. C:/Users/you/vcpkg).")
endif ()

set(V "$ENV{VCPKG_ROOT}/installed/x64-windows-static-md")
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
set(VCPKG_TARGET_TRIPLET x64-windows-static-md CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL CACHE STRING "")
set(SDL2_LIBRARY_TEMP "${V}/lib/SDL2-static.lib;winmm.lib;imm32.lib;version.lib;setupapi.lib;ole32.lib;oleaut32.lib;gdi32.lib;user32.lib;advapi32.lib;shell32.lib;cfgmgr32.lib" CACHE STRING "")
set(SDL2_IMAGE_LIBRARY "${V}/lib/SDL2_image-static.lib;${V}/lib/libpng16.lib;${V}/lib/zs.lib" CACHE STRING "")

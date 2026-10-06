# Native Nintendo Switch Homebrew cross toolchain (devkitA64 + libnx).
if(SWITCH_TOOLCHAIN_INCLUDED)
    return()
endif()
set(SWITCH_TOOLCHAIN_INCLUDED YES)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(NINTENDO_SWITCH YES)

set(_dkp_candidates "")
if(DEFINED DEVKITPRO)
    list(APPEND _dkp_candidates "${DEVKITPRO}")
endif()
if(DEFINED ENV{DEVKITPRO})
    list(APPEND _dkp_candidates "$ENV{DEVKITPRO}")
endif()
list(APPEND _dkp_candidates "/opt/devkitpro" "C:/devkitPro")

set(_dkp_found "")
foreach(_candidate IN LISTS _dkp_candidates)
    if(IS_DIRECTORY "${_candidate}/devkitA64" AND IS_DIRECTORY "${_candidate}/libnx")
        set(_dkp_found "${_candidate}")
        break()
    endif()
endforeach()
if(NOT _dkp_found)
    message(FATAL_ERROR "devkitPro Switch SDK not found. Install switch-dev or pass -DDEVKITPRO=<path>.")
endif()

set(DEVKITPRO "${_dkp_found}" CACHE PATH "devkitPro root")
set(DEVKITA64 "${DEVKITPRO}/devkitA64" CACHE PATH "devkitA64 root")
set(LIBNX "${DEVKITPRO}/libnx" CACHE PATH "libnx root")

set(CMAKE_C_COMPILER "${DEVKITA64}/bin/aarch64-none-elf-gcc" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${DEVKITA64}/bin/aarch64-none-elf-g++" CACHE FILEPATH "")
set(CMAKE_ASM_COMPILER "${DEVKITA64}/bin/aarch64-none-elf-gcc" CACHE FILEPATH "")
set(CMAKE_AR "${DEVKITA64}/bin/aarch64-none-elf-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${DEVKITA64}/bin/aarch64-none-elf-ranlib" CACHE FILEPATH "")

if(NOT EXISTS "${CMAKE_CXX_COMPILER}")
    message(FATAL_ERROR "aarch64-none-elf-g++ not found at '${CMAKE_CXX_COMPILER}'.")
endif()

set(CMAKE_FIND_ROOT_PATH "${DEVKITA64}" "${LIBNX}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# CMake reloads the toolchain inside compiler ABI probes. Preserve the selected
# SDK root there as well instead of relying on the host environment.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES DEVKITPRO)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

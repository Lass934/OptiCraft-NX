cmake_minimum_required(VERSION 3.21)

project(OptiCraftNX
    VERSION 0.1.0
    DESCRIPTION "OptiCraft NX - Minecraft 1.2.5 homebrew for Nintendo Switch"
    LANGUAGES C CXX
)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SourceSelection.cmake)

# StrictMathCompat reproduces the operation ordering of Java StrictMath/fdlibm.
# In particular on Broadway/PowerPC, contracting a multiply+add into FMA can
# change the last bit. Keep contraction disabled for this TU only so the rest
# of the renderer/world code keeps its normal optimization profile.
file(GLOB FDLIBM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/java/fdlibm/*.c"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/java/StrictMathCompat.cpp"
        ${FDLIBM_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off"
    )
elseif(MSVC)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/java/StrictMathCompat.cpp"
        ${FDLIBM_SOURCES}
        PROPERTIES COMPILE_OPTIONS "/fp:strict"
    )
endif()

# Nintendo Switch homebrew build (devkitA64/libnx -> .nro). The switch-*
# presets select cmake/switch_toolchain.cmake, which defines NINTENDO_SWITCH.
if(NOT (NINTENDO_SWITCH OR (DEFINED PLATFORM AND PLATFORM STREQUAL "SWITCH")))
    message(FATAL_ERROR "OptiCraft NX only builds for Nintendo Switch. "
        "Configure with a switch-* preset (cmake --preset switch-release).")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/switch.cmake)

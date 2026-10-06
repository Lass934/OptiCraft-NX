# Nintendo Switch (devkitA64/libnx) build. SWITCH_BRINGUP keeps the small
# diagnostic executable; OFF builds the game with Switch-native backends.
cmake_minimum_required(VERSION 3.21)
include(${CMAKE_SOURCE_DIR}/cmake/SourceSelection.cmake)
option(SWITCH_BRINGUP "Build only the native Switch hardware diagnostic" OFF)
option(SWITCH_ENABLE_SOUND "Enable Switch audio" ON)
set(MC_LOG_LEVEL "0" CACHE STRING "Unified diagnostic verbosity")
set(SWITCH_DATA_ROOT "${CMAKE_SOURCE_DIR}/data" CACHE PATH
    "Directory containing the assets/ and resources/ runtime data trees")

if(SWITCH_BRINGUP)
    set(SWITCH_SOURCES "${CMAKE_SOURCE_DIR}/src/switch/tools/SwitchBringup.cpp")
    set(SWITCH_ARTIFACT_NAME "OptiCraft-bringup")
    message(STATUS "Switch build: BRINGUP diagnostics")
else()
    mcbeta_collect_platform_sources(SWITCH_SOURCES switch)
    # These three translation units provide symbols that are mandatory at the
    # final link boundary. Keep them explicit rather than relying only on the
    # recursive platform glob: stale/conflict-resolved CMake source lists can
    # otherwise produce an almost-complete build that fails only at the final
    # ELF link with missing main, Resource::getResource, and renderer symbols.
    set(SWITCH_REQUIRED_RUNTIME_SOURCES
        "${CMAKE_SOURCE_DIR}/src/switch/main_switch.cpp"
        "${CMAKE_SOURCE_DIR}/src/switch/java/Resource_switch.cpp"
        "${CMAKE_SOURCE_DIR}/src/switch/render/SwitchLegacyRenderer.cpp")
    foreach(_switch_required_source IN LISTS SWITCH_REQUIRED_RUNTIME_SOURCES)
        if(NOT EXISTS "${_switch_required_source}")
            message(FATAL_ERROR "Required Switch runtime source is missing: ${_switch_required_source}")
        endif()
    endforeach()
    list(APPEND SWITCH_SOURCES ${SWITCH_REQUIRED_RUNTIME_SOURCES})
    list(REMOVE_DUPLICATES SWITCH_SOURCES)
    set(SWITCH_MINIZIP_SOURCES
        "${CMAKE_SOURCE_DIR}/external/zlib/contrib/minizip/ioapi.c"
        "${CMAKE_SOURCE_DIR}/external/zlib/contrib/minizip/unzip.c")
    list(APPEND SWITCH_SOURCES ${SWITCH_MINIZIP_SOURCES})
    if(SWITCH_ENABLE_SOUND)
        # SwitchAudio decodes OGG with stb_vorbis, as the Wii backend does.
        list(APPEND SWITCH_SOURCES "${CMAKE_SOURCE_DIR}/src/pc/external/stb_vorbis.cpp")
    else()
        mcbeta_exclude_sources(SWITCH_SOURCES "[/\\]switch[/\\]audio[/\\]")
    endif()
    set_source_files_properties(${SWITCH_MINIZIP_SOURCES} PROPERTIES COMPILE_DEFINITIONS USE_FILE32API)
    mcbeta_exclude_sources(SWITCH_SOURCES "[/\\]switch[/\\]tools[/\\]SwitchBringup\\.cpp$")
    mcbeta_exclude_remote_stats_sources(SWITCH_SOURCES)
    mcbeta_select_platform_backends(SWITCH_SOURCES SWITCH SWITCH SWITCH)
    set(SWITCH_ARTIFACT_NAME "OptiCraft")
    message(STATUS "Switch build: FULL game")
endif()

add_executable(OptiCraft ${SWITCH_SOURCES})
set_target_properties(OptiCraft PROPERTIES SUFFIX ".elf" CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/switch")
target_compile_definitions(OptiCraft PRIVATE SWITCH_PLATFORM=1 NO_NETWORK
    MC_LOG_LEVEL=${MC_LOG_LEVEL} $<$<NOT:$<BOOL:${SWITCH_ENABLE_SOUND}>>:NO_SOUND>)
target_compile_options(OptiCraft PRIVATE -march=armv8-a+crc -mtp=soft -fPIE
    -ffunction-sections -fdata-sections -fno-math-errno -fno-trapping-math)
target_include_directories(OptiCraft PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/pc"
    "${CMAKE_SOURCE_DIR}/external/stb" "${LIBNX}/include")
target_link_directories(OptiCraft PRIVATE "${LIBNX}/lib")

# The diagnostic deliberately depends only on libnx. zlib is a Switch portlib
# used by the game's region/minizip code; requiring it for bring-up prevents the
# hardware test from linking on an otherwise complete switch-dev install.
set(_SWITCH_LIBS nx m)
if(NOT SWITCH_BRINGUP)
    set(SWITCH_PORTLIBS "${DEVKITPRO}/portlibs/switch")
    find_path(SWITCH_OPENGL_INCLUDE_DIR EGL/egl.h
        HINTS "${SWITCH_PORTLIBS}/include"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(SWITCH_ZLIB NAMES z HINTS "${SWITCH_PORTLIBS}/lib"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(SWITCH_GLAD NAMES glad HINTS "${SWITCH_PORTLIBS}/lib"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(SWITCH_EGL NAMES EGL HINTS "${SWITCH_PORTLIBS}/lib"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(SWITCH_GLAPI NAMES glapi HINTS "${SWITCH_PORTLIBS}/lib"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(SWITCH_DRM_NOUVEAU NAMES drm_nouveau HINTS "${SWITCH_PORTLIBS}/lib"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(NOT SWITCH_ZLIB)
        message(FATAL_ERROR
            "Switch full game requires switch-zlib. Install it with: dkp-pacman -S switch-zlib")
    endif()
    if(NOT SWITCH_OPENGL_INCLUDE_DIR OR NOT SWITCH_GLAD OR NOT SWITCH_EGL OR
       NOT SWITCH_GLAPI OR NOT SWITCH_DRM_NOUVEAU)
        message(FATAL_ERROR
            "Switch full game requires the Switch OpenGL portlibs. Install them with: "
            "dkp-pacman -S switch-mesa switch-glad")
    endif()
    target_include_directories(OptiCraft PRIVATE "${SWITCH_OPENGL_INCLUDE_DIR}")
    target_link_directories(OptiCraft PRIVATE "${SWITCH_PORTLIBS}/lib")
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/src/switch/render/SwitchGraphicsContext.cpp"
        PROPERTIES COMPILE_DEFINITIONS __SWITCH__)
    list(PREPEND _SWITCH_LIBS
        "${SWITCH_GLAD}" "${SWITCH_EGL}" "${SWITCH_GLAPI}" "${SWITCH_DRM_NOUVEAU}")
    list(APPEND _SWITCH_LIBS z)
endif()
target_link_libraries(OptiCraft PRIVATE ${_SWITCH_LIBS})
target_link_options(OptiCraft PRIVATE "-specs=${LIBNX}/switch.specs" -march=armv8-a+crc -mtp=soft -fPIE
    "-Wl,-Map,${CMAKE_BINARY_DIR}/OptiCraft.map" -Wl,--gc-sections)

find_program(SWITCH_NACPTOOL NAMES nacptool HINTS "${DEVKITPRO}/tools/bin")
find_program(SWITCH_ELF2NRO NAMES elf2nro HINTS "${DEVKITPRO}/tools/bin")
if(NOT SWITCH_NACPTOOL OR NOT SWITCH_ELF2NRO)
    message(FATAL_ERROR
        "elf2nro and nacptool are required. Install devkitPro's switch-tools package.")
endif()

if(SWITCH_BRINGUP)
    set(_switch_default_title "OptiCraft NX Bring-up")
else()
    set(_switch_default_title "OptiCraft NX")
endif()
set(SWITCH_TITLE "${_switch_default_title}" CACHE STRING "NRO application title")
set(SWITCH_AUTHOR "Lass934" CACHE STRING "NRO author")
set(SWITCH_VERSION "0.1.0" CACHE STRING "NRO version")
set(SWITCH_ICON "" CACHE FILEPATH "Optional 256x256 JPEG icon embedded in the NRO")

set(SWITCH_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/switch")
set(SWITCH_NACP "${CMAKE_CURRENT_BINARY_DIR}/${SWITCH_ARTIFACT_NAME}.nacp")
set(SWITCH_NRO "${SWITCH_OUTPUT_DIR}/${SWITCH_ARTIFACT_NAME}.nro")
add_custom_command(OUTPUT "${SWITCH_NACP}" COMMAND "${SWITCH_NACPTOOL}" --create
    "${SWITCH_TITLE}" "${SWITCH_AUTHOR}" "${SWITCH_VERSION}" "${SWITCH_NACP}" VERBATIM)
add_custom_target(switch-nacp DEPENDS "${SWITCH_NACP}")
add_dependencies(OptiCraft switch-nacp)
set(_switch_elf2nro_arguments "$<TARGET_FILE:OptiCraft>" "${SWITCH_NRO}" "--nacp=${SWITCH_NACP}")
if(SWITCH_ICON)
    if(NOT EXISTS "${SWITCH_ICON}")
        message(FATAL_ERROR "SWITCH_ICON does not exist: ${SWITCH_ICON}")
    endif()
    list(APPEND _switch_elf2nro_arguments "--icon=${SWITCH_ICON}")
endif()
add_custom_command(TARGET OptiCraft POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SWITCH_OUTPUT_DIR}"
    COMMAND "${SWITCH_ELF2NRO}" ${_switch_elf2nro_arguments}
    COMMENT "elf2nro: ${SWITCH_NRO}" COMMAND_EXPAND_LISTS VERBATIM)

find_program(SWITCH_NXLINK NAMES nxlink HINTS "${DEVKITPRO}/tools/bin")
if(SWITCH_NXLINK)
    add_custom_target(switch-nxlink
        COMMAND "${SWITCH_NXLINK}" "${SWITCH_NRO}"
        DEPENDS OptiCraft
        COMMENT "Sending ${SWITCH_ARTIFACT_NAME}.nro with nxlink"
        USES_TERMINAL VERBATIM)
endif()
add_custom_target(switch-data
    COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_ROOT=${SWITCH_DATA_ROOT}"
            "-DOUTPUT_ROOT=${SWITCH_OUTPUT_DIR}/data"
            -P "${CMAKE_SOURCE_DIR}/cmake/StageSwitchData.cmake"
    COMMENT "Staging Switch runtime data" VERBATIM)

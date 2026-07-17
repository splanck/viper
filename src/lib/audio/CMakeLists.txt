# ZannaAUD - Cross-Platform Audio Library
# Part of the Zanna project

# Can be built standalone or via add_subdirectory
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Standalone build
    cmake_minimum_required(VERSION 3.10)
    project(ZannaAUD VERSION 1.0.0 LANGUAGES C)
    set(VAUD_STANDALONE TRUE)

    # C11 standard (Zanna sets this globally)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    # Warning flags (Zanna sets these globally)
    if (MSVC)
        add_compile_options(/W4)
    else ()
        add_compile_options(-Wall -Wextra -Wpedantic)
    endif ()

    # Options for standalone build
    option(VAUD_BUILD_TESTS "Build ZannaAUD tests" ON)
    option(VAUD_BUILD_EXAMPLES "Build ZannaAUD examples" ON)
else ()
    # Integrated into Zanna build
    set(VAUD_STANDALONE FALSE)

    # Use Zanna's test option
    set(VAUD_BUILD_TESTS ${ZANNA_BUILD_TESTING})
    set(VAUD_BUILD_EXAMPLES ${BUILD_EXAMPLES})
endif ()

include(GNUInstallDirs)

# Core source files (platform-independent)
set(VAUD_CORE_SOURCES
        src/vaud.c
        src/vaud_wav.c
        src/vaud_mixer.c
)

set(ZANNAAUD_AVAILABLE OFF CACHE INTERNAL "Whether ZannaAUD is available in this build" FORCE)
set(ZANNAAUD_UNAVAILABLE_REASON "audio support disabled" CACHE INTERNAL
        "Reason ZannaAUD is unavailable" FORCE)
set(ZANNAAUD_BACKEND "disabled" CACHE INTERNAL "Selected ZannaAUD backend" FORCE)

if (NOT DEFINED ZANNA_AUDIO_MODE)
    message(FATAL_ERROR "ZANNA_AUDIO_MODE must be defined before adding src/lib/audio")
endif ()

if (ZANNA_AUDIO_MODE STREQUAL "OFF")
    message(STATUS "ZannaAUD: disabled by ZANNA_AUDIO_MODE=OFF")
    set(ZANNAAUD_UNAVAILABLE_REASON "disabled by ZANNA_AUDIO_MODE=OFF" CACHE INTERNAL
            "Reason ZannaAUD is unavailable" FORCE)
    return()
endif ()

# Platform detection and source selection
if (APPLE)
    message(STATUS "ZannaAUD: Building for macOS (AudioQueue backend)")
    set(VAUD_PLATFORM_SOURCES src/vaud_platform_macos.m)
    set(VAUD_PLATFORM_LIBS "-framework AudioToolbox")
    set(ZANNAAUD_BACKEND "AudioQueue" CACHE INTERNAL "Selected ZannaAUD backend" FORCE)
    enable_language(OBJC)
elseif (UNIX)
    # Check for ALSA on Linux
    find_package(ALSA QUIET)
    find_package(Threads REQUIRED)
    if (NOT ALSA_FOUND)
        set(ZANNAAUD_UNAVAILABLE_REASON
                "ALSA not found; install libasound2-dev/alsa-lib-devel or set ZANNA_AUDIO_MODE=OFF"
                CACHE INTERNAL "Reason ZannaAUD is unavailable" FORCE)
        if (ZANNA_AUDIO_MODE STREQUAL "REQUIRE")
            message(FATAL_ERROR
                    "ZannaAUD: ALSA not found and ZANNA_AUDIO_MODE=REQUIRE.\n"
                    "Install libasound2-dev (Debian/Ubuntu) or alsa-lib-devel (Fedora/RHEL),\n"
                    "or reconfigure with -DZANNA_AUDIO_MODE=OFF.")
        endif ()
        message(STATUS "ZannaAUD: disabled (${ZANNAAUD_UNAVAILABLE_REASON})")
        return()
    endif ()
    message(STATUS "ZannaAUD: Building for Linux (ALSA backend)")
    set(VAUD_PLATFORM_SOURCES src/vaud_platform_linux.c)
    set(VAUD_PLATFORM_LIBS ${ALSA_LIBRARIES} Threads::Threads)
    set(ZANNAAUD_BACKEND "ALSA" CACHE INTERNAL "Selected ZannaAUD backend" FORCE)
    include_directories(${ALSA_INCLUDE_DIRS})
elseif (WIN32)
    message(STATUS "ZannaAUD: Building for Windows (WASAPI backend)")
    set(VAUD_PLATFORM_SOURCES src/vaud_platform_win32.c)
    set(VAUD_PLATFORM_LIBS ole32)
    set(ZANNAAUD_BACKEND "WASAPI" CACHE INTERNAL "Selected ZannaAUD backend" FORCE)
else ()
    message(FATAL_ERROR "ZannaAUD: Unsupported platform")
endif ()

# Static library target
add_library(zannaaud STATIC
        ${VAUD_CORE_SOURCES}
        ${VAUD_PLATFORM_SOURCES}
)

# Public include directory
target_include_directories(zannaaud
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# MSVC: force synchronous PDB writes and UTF-8 source encoding
if (MSVC)
    target_compile_options(zannaaud PRIVATE /utf-8 /FS)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
        CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    target_compile_options(zannaaud PRIVATE -mno-outline-atomics)
endif ()

# POSIX feature test macros for Linux (nanosleep, pthread APIs, etc.)
if (NOT WIN32 AND NOT APPLE)
    target_compile_definitions(zannaaud PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
endif ()

# Link platform-specific libraries
target_link_libraries(zannaaud PRIVATE ${VAUD_PLATFORM_LIBS})

# Set output name
set_target_properties(zannaaud PROPERTIES
        OUTPUT_NAME "zannaaud"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

set(ZANNAAUD_AVAILABLE ON CACHE INTERNAL "Whether ZannaAUD is available in this build" FORCE)
set(ZANNAAUD_UNAVAILABLE_REASON "" CACHE INTERNAL "Reason ZannaAUD is unavailable" FORCE)

# Subdirectories (only if options enabled)
if (VAUD_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (VAUD_BUILD_EXAMPLES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples)
    add_subdirectory(examples)
endif ()

# Install rules (only for standalone builds)
if (VAUD_STANDALONE)
    # Install library
    install(TARGETS zannaaud
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif ()

install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
)

# Summary
message(STATUS "ZannaAUD Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "  Build Tests: ${VAUD_BUILD_TESTS}")
message(STATUS "  Build Examples: ${VAUD_BUILD_EXAMPLES}")
message(STATUS "  Standalone Build: ${VAUD_STANDALONE}")

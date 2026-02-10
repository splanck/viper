# ViperAUD - Cross-Platform Audio Library
# Part of the Viper project

# Can be built standalone or via add_subdirectory
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Standalone build
    cmake_minimum_required(VERSION 3.10)
    project(ViperAUD VERSION 1.0.0 LANGUAGES C)
    set(VAUD_STANDALONE TRUE)

    # C11 standard (Viper sets this globally)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    # Warning flags (Viper sets these globally)
    if (MSVC)
        add_compile_options(/W4)
    else ()
        add_compile_options(-Wall -Wextra -Wpedantic)
    endif ()

    # Options for standalone build
    option(VAUD_BUILD_TESTS "Build ViperAUD tests" ON)
    option(VAUD_BUILD_EXAMPLES "Build ViperAUD examples" ON)
else ()
    # Integrated into Viper build
    set(VAUD_STANDALONE FALSE)

    # Use Viper's test option
    set(VAUD_BUILD_TESTS ${VIPER_BUILD_TESTING})
    set(VAUD_BUILD_EXAMPLES ${BUILD_EXAMPLES})
endif ()

# Core source files (platform-independent)
set(VAUD_CORE_SOURCES
        src/vaud.c
        src/vaud_wav.c
        src/vaud_mixer.c
)

# Platform detection and source selection
if (APPLE)
    message(STATUS "ViperAUD: Building for macOS (AudioQueue backend)")
    set(VAUD_PLATFORM_SOURCES src/vaud_platform_macos.m)
    set(VAUD_PLATFORM_LIBS "-framework AudioToolbox")
    enable_language(OBJC)
elseif (UNIX)
    # Check for ALSA on Linux
    find_package(ALSA QUIET)
    if (NOT ALSA_FOUND)
        message(STATUS "ViperAUD: ALSA not found - skipping audio library (install libasound2-dev to enable)")
        return()
    endif ()
    message(STATUS "ViperAUD: Building for Linux (ALSA backend)")
    set(VAUD_PLATFORM_SOURCES src/vaud_platform_linux.c)
    set(VAUD_PLATFORM_LIBS ${ALSA_LIBRARIES})
    include_directories(${ALSA_INCLUDE_DIRS})
elseif (WIN32)
    message(STATUS "ViperAUD: Building for Windows (WASAPI backend)")
    set(VAUD_PLATFORM_SOURCES src/vaud_platform_win32.c)
    set(VAUD_PLATFORM_LIBS ole32)
else ()
    message(FATAL_ERROR "ViperAUD: Unsupported platform")
endif ()

# Static library target
add_library(viperaud STATIC
        ${VAUD_CORE_SOURCES}
        ${VAUD_PLATFORM_SOURCES}
)

# Public include directory
target_include_directories(viperaud
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# MSVC: force synchronous PDB writes and UTF-8 source encoding
if (MSVC)
    target_compile_options(viperaud PRIVATE /utf-8 /FS)
endif ()

# Link platform-specific libraries
target_link_libraries(viperaud PRIVATE ${VAUD_PLATFORM_LIBS})

# Set output name
set_target_properties(viperaud PROPERTIES
        OUTPUT_NAME "viperaud"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

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
    install(TARGETS viperaud
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
    )

    # Install public headers
    install(DIRECTORY include/
            DESTINATION include
            FILES_MATCHING PATTERN "*.h"
    )
endif ()

# Summary
message(STATUS "ViperAUD Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "  Build Tests: ${VAUD_BUILD_TESTS}")
message(STATUS "  Build Examples: ${VAUD_BUILD_EXAMPLES}")
message(STATUS "  Standalone Build: ${VAUD_STANDALONE}")

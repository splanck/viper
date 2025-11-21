# ViperGFX - Cross-Platform Software 2D Graphics Library
# Part of the Viper project

# Can be built standalone or via add_subdirectory
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Standalone build
    cmake_minimum_required(VERSION 3.10)
    project(ViperGFX VERSION 1.0.0 LANGUAGES C)
    set(VGFX_STANDALONE TRUE)

    # C99 standard (Viper sets this globally)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    # Warning flags (Viper sets these globally)
    if(MSVC)
        add_compile_options(/W4)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic)
    endif()

    # Options for standalone build
    option(VGFX_BUILD_TESTS "Build ViperGFX tests" ON)
    option(VGFX_BUILD_EXAMPLES "Build ViperGFX examples" ON)
else()
    # Integrated into Viper build
    set(VGFX_STANDALONE FALSE)

    # Use Viper's test option
    set(VGFX_BUILD_TESTS ${VIPER_BUILD_TESTING})
    set(VGFX_BUILD_EXAMPLES ${BUILD_EXAMPLES})
endif()

# Core source files (platform-independent)
set(VGFX_CORE_SOURCES
    src/vgfx.c
    src/vgfx_draw.c
)

# Platform detection and source selection
if(APPLE)
    message(STATUS "ViperGFX: Building for macOS (Cocoa backend)")
    set(VGFX_PLATFORM_SOURCES src/vgfx_platform_macos.m)
    set(VGFX_PLATFORM_LIBS "-framework Cocoa")
    enable_language(OBJC)
elseif(UNIX)
    message(STATUS "ViperGFX: Building for Linux (X11 backend)")
    set(VGFX_PLATFORM_SOURCES src/vgfx_platform_linux.c)
    find_package(X11 REQUIRED)
    set(VGFX_PLATFORM_LIBS ${X11_LIBRARIES})
    include_directories(${X11_INCLUDE_DIR})
elseif(WIN32)
    message(STATUS "ViperGFX: Building for Windows (Win32 backend)")
    set(VGFX_PLATFORM_SOURCES src/vgfx_platform_win32.c)
    set(VGFX_PLATFORM_LIBS user32 gdi32)
else()
    message(FATAL_ERROR "ViperGFX: Unsupported platform")
endif()

# Static library target
add_library(vipergfx STATIC
    ${VGFX_CORE_SOURCES}
    ${VGFX_PLATFORM_SOURCES}
)

# Public include directory
target_include_directories(vipergfx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link platform-specific libraries
target_link_libraries(vipergfx PRIVATE ${VGFX_PLATFORM_LIBS})

# Set output name
set_target_properties(vipergfx PROPERTIES
    OUTPUT_NAME "vipergfx"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Subdirectories (only if options enabled)
if(VGFX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(VGFX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install rules (only for standalone builds)
if(VGFX_STANDALONE)
    # Install library
    install(TARGETS vipergfx
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
    )

    # Install public headers
    install(DIRECTORY include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
    )
endif()

# Summary
message(STATUS "ViperGFX Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "  Build Tests: ${VGFX_BUILD_TESTS}")
message(STATUS "  Build Examples: ${VGFX_BUILD_EXAMPLES}")
message(STATUS "  Standalone Build: ${VGFX_STANDALONE}")

# ZannaGFX Test Suite
# Builds tests using mock platform backend for deterministic testing

cmake_minimum_required(VERSION 3.15)

# ============================================================================
# Mock Platform Library
# ============================================================================
# This library is identical to zannagfx but uses the mock backend instead
# of real OS windowing systems. This allows tests to run without display.

add_library(zannagfx_mock STATIC
        # Core implementation (same as main library)
        ../src/vgfx.c
        ../src/vgfx_draw.c

        # Mock platform backend (instead of macos/linux/win32)
        ../src/vgfx_platform_mock.c
)

target_include_directories(zannagfx_mock PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_features(zannagfx_mock PUBLIC c_std_11)

# POSIX feature test macros for Linux (posix_memalign, etc.)
if (NOT WIN32 AND NOT APPLE)
    target_compile_definitions(zannagfx_mock PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
endif ()

# Compiler warnings
if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(zannagfx_mock PRIVATE
            -Wall -Wextra -Wpedantic
            -Wno-unused-parameter
    )
endif ()

# ============================================================================
# Test Helper Function
# ============================================================================

function(add_vgfx_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE zannagfx_mock)
    target_include_directories(${test_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Add to CTest
    add_test(NAME ${test_name} COMMAND ${test_name})

    # Set test properties for better output
    set_tests_properties(${test_name} PROPERTIES
            TIMEOUT 10
            LABELS "unit"
    )
endfunction()

# ============================================================================
# Test Executables
# ============================================================================

add_vgfx_test(test_window test_window.c)
add_vgfx_test(test_pixels test_pixels.c)
add_vgfx_test(test_drawing test_drawing.c)
if (NOT WIN32)
    # Skip on Windows: aligned memory allocation/free mismatch in mock platform
    add_vgfx_test(test_input test_input.c)
endif ()

# ============================================================================
# Test Runner Target
# ============================================================================

# Custom target to run all tests
if (WIN32)
    add_custom_target(run_zannagfx_tests
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            DEPENDS test_window test_pixels test_drawing
            COMMENT "Running all ZannaGFX tests..."
    )
else ()
    add_custom_target(run_zannagfx_tests
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            DEPENDS test_window test_pixels test_drawing test_input
            COMMENT "Running all ZannaGFX tests..."
    )
endif ()

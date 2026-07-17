#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/tools/HeadlessGraphicsSmoke.cmake
# Purpose: Configure, build, and execute the standalone Linux headless graphics tests.
#
# Key invariants: The smoke build performs no X11 discovery and remains isolated in the build tree.
#
# Ownership/Lifetime: CTest owns and replaces TEST_WORK_DIR.
#
# Links: docs/linux-platform.md, docs/adr/0106-linux-graphics-backend-selection.md
#
#===----------------------------------------------------------------------===#

if (NOT DEFINED CMAKE_BIN OR NOT DEFINED ZANNA_SOURCE_DIR OR NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "Headless graphics smoke requires CMAKE_BIN, ZANNA_SOURCE_DIR, and TEST_WORK_DIR")
endif ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
execute_process(
        COMMAND "${CMAKE_BIN}"
                -S "${ZANNA_SOURCE_DIR}/src/lib/graphics"
                -B "${TEST_WORK_DIR}"
                -DZANNA_GRAPHICS_MODE=REQUIRE
                -DZANNA_GRAPHICS_BACKEND=HEADLESS
                -DVGFX_BUILD_TESTS=ON
                -DVGFX_BUILD_EXAMPLES=OFF
        RESULT_VARIABLE configure_result)
if (NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Headless graphics configure failed: ${configure_result}")
endif ()

execute_process(
        COMMAND "${CMAKE_BIN}" --build "${TEST_WORK_DIR}" --target test_window test_pixels test_input
        RESULT_VARIABLE build_result)
if (NOT build_result EQUAL 0)
    message(FATAL_ERROR "Headless graphics build failed: ${build_result}")
endif ()

execute_process(
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${TEST_WORK_DIR}"
                -R "^(test_window|test_pixels|test_input)$" --output-on-failure
        RESULT_VARIABLE test_result)
if (NOT test_result EQUAL 0)
    message(FATAL_ERROR "Headless graphics tests failed: ${test_result}")
endif ()

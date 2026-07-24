#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/cmake/CheckLinuxCTestLabels.cmake
# Purpose: Reject host-Linux CTests that omit the requires_linux capability label.
#
# Key invariants:
#   - Linux backend names and backend-specific labels imply requires_linux.
#   - Cross-target ABI tests remain host-independent and are explicitly exempt.
#
# Ownership/Lifetime:
#   - Reads CTest's filtered inventory and creates no persistent files.
#
# Links: docs/internals/testing.md
#
#===----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED ZANNA_CTEST_COMMAND OR NOT DEFINED ZANNA_BUILD_DIR)
    message(FATAL_ERROR "ZANNA_CTEST_COMMAND and ZANNA_BUILD_DIR are required")
endif ()

execute_process(
        COMMAND "${ZANNA_CTEST_COMMAND}" --test-dir "${ZANNA_BUILD_DIR}" -N
                -L "^(wayland|x11|alsa|atspi)$" -LE "^requires_linux$"
        RESULT_VARIABLE label_result
        OUTPUT_VARIABLE label_violations
        ERROR_VARIABLE label_error)
if (NOT label_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect Linux backend labels: ${label_error}")
endif ()

execute_process(
        COMMAND "${ZANNA_CTEST_COMMAND}" --test-dir "${ZANNA_BUILD_DIR}" -N
                -R "(linux|wayland|x11|alsa|atspi)" -E "^test_aarch64_linux_abi$"
                -LE "^requires_linux$"
        RESULT_VARIABLE name_result
        OUTPUT_VARIABLE name_violations
        ERROR_VARIABLE name_error)
if (NOT name_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect Linux backend test names: ${name_error}")
endif ()

if (label_violations MATCHES "Test +#[0-9]+:" OR
    name_violations MATCHES "Test +#[0-9]+:")
    message(FATAL_ERROR
            "Linux-specific CTests missing requires_linux:\n"
            "${label_violations}${name_violations}")
endif ()
message(STATUS "Linux CTest label policy: clean")

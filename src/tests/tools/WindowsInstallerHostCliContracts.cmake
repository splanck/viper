#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/tools/WindowsInstallerHostCliContracts.cmake
# Purpose: Verify fail-closed Windows installer command-line parsing.
# Key invariants:
#   - Duplicate, empty, ambiguous, and malformed options return ERROR_INVALID_PARAMETER.
#   - Tests stop at argument parsing and never mutate installation state.
# Ownership/Lifetime: The test creates no files and owns no persistent state.
# Links: src/tools/windows_installer/WindowsInstallerHost.cpp
#
#===----------------------------------------------------------------------===#

if (NOT DEFINED HOST OR NOT EXISTS "${HOST}")
    message(FATAL_ERROR "Windows installer host is missing: ${HOST}")
endif ()

function(expect_invalid label)
    execute_process(
        COMMAND "${HOST}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 15)
    if (NOT result EQUAL 87)
        message(FATAL_ERROR
            "${label}: expected exit 87, got ${result}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif ()
endfunction()

expect_invalid("duplicate operation" /selftest /self-test /quiet)
expect_invalid("help with lifecycle operation" /help /selftest /quiet)
expect_invalid("duplicate UI level" /selftest /quiet /silent)
expect_invalid("duplicate integration" /selftest /addToPath /path /quiet)
expect_invalid("duplicate scope" /selftest /scope=user /scope=currentuser /quiet)
expect_invalid("duplicate preset" /selftest /type=typical /preset=recommended /quiet)
expect_invalid("duplicate simple flag" /selftest /noRestart /noRestart /quiet)
expect_invalid("empty install directory" /selftest /installDir= /quiet)
expect_invalid("empty log path" /selftest /log= /quiet)
expect_invalid("empty output path" /inspect /output= /quiet)
expect_invalid("empty component list" /selftest /components= /quiet)
expect_invalid("signed handoff identifier" /uninstall-worker /handoff-parent=+1 /quiet)
expect_invalid("spaced handoff identifier" /uninstall-worker "/handoff-parent= 1" /quiet)

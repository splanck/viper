# SPDX-License-Identifier: GPL-3.0-only
# File: tests/e2e/differential_vm_native.cmake
# Purpose: Real VM<->native determinism gate. Runs one IL program under the VM
#          (`viper -run`) and the host-native backend (`viper codegen <arch>
#          -run-native`) and asserts identical stdout AND exit code. Unlike the
#          self-checking native probes, this byte-diffs the two outputs.
# Links: docs/internals/architecture.md (determinism Core Principle)

if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED IL_FILE)
    message(FATAL_ERROR "IL_FILE not set")
endif ()
if (NOT DEFINED ARCH)
    message(FATAL_ERROR "ARCH not set")
endif ()

execute_process(
    COMMAND ${ILC} -run ${IL_FILE}
    OUTPUT_VARIABLE vm_out
    RESULT_VARIABLE vm_exit)

# ERROR_QUIET drops the linker's "dead-strip: removed N sections" diagnostic so
# only the program's own stdout is compared.
execute_process(
    COMMAND ${ILC} codegen ${ARCH} ${IL_FILE} -run-native
    OUTPUT_VARIABLE nat_out
    RESULT_VARIABLE nat_exit
    ERROR_QUIET)

if (NOT vm_out STREQUAL nat_out)
    message(FATAL_ERROR
        "VM/native stdout mismatch for ${IL_FILE}:\n  VM:     [${vm_out}]\n  native: [${nat_out}]")
endif ()
if (NOT vm_exit EQUAL nat_exit)
    message(FATAL_ERROR
        "VM/native exit-code mismatch for ${IL_FILE}: VM=${vm_exit} native=${nat_exit}")
endif ()

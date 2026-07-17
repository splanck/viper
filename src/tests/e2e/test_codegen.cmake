#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/e2e/test_codegen.cmake
# Purpose: Smoke-test x64 assembly syntax, linking, and execution.
#
# Key invariants: The configured Clang executable is used without relying on
#                 the CTest process PATH.
#
# Ownership/Lifetime: Temporary assembly and executables stay in the test's
#                     build-tree working directory.
#
# Links: src/tests/e2e/CMakeLists.txt
#
#===----------------------------------------------------------------------===#

if (NOT DEFINED CLANG_BIN OR "${CLANG_BIN}" STREQUAL "")
    message(FATAL_ERROR "CLANG_BIN must name the configured Clang executable")
endif ()

separate_arguments(ASM_FLAGS_LIST NATIVE_COMMAND "${ASM_FLAGS}")
# Force non-PIE output so the test binary can run on minimal environments.
set(LD_FLAGS "${LD_FLAGS} -no-pie")
separate_arguments(LD_FLAGS_LIST NATIVE_COMMAND "${LD_FLAGS}")

# .note.GNU-stack marks stack non-executable on Linux; omit on Windows
if (WIN32)
    file(WRITE out.s ".text\n.globl main\nmain:\n  mov $0, %eax\n  ret\n")
else ()
    file(WRITE out.s ".text\n.globl main\nmain:\n  mov $0, %eax\n  ret\n.section .note.GNU-stack,\"\",@progbits\n")
endif ()

if (MODE STREQUAL "syntax")
    execute_process(
            COMMAND "${CLANG_BIN}" ${ASM_FLAGS_LIST} -x assembler -fsyntax-only out.s
            RESULT_VARIABLE r)
    if (NOT r EQUAL 0)
        message(FATAL_ERROR "syntax check failed")
    endif ()
elseif (MODE STREQUAL "assemble_link")
    execute_process(COMMAND "${CLANG_BIN}" ${ASM_FLAGS_LIST} -c out.s -o out.o
            RESULT_VARIABLE r1
            OUTPUT_VARIABLE asm_out
            ERROR_VARIABLE asm_err)
    if (NOT r1 EQUAL 0)
        message(FATAL_ERROR "assembly failed: ${asm_out} ${asm_err}")
    endif ()
    execute_process(COMMAND "${CLANG_BIN}" ${LD_FLAGS_LIST} out.o -o a.out
            RESULT_VARIABLE r2
            OUTPUT_VARIABLE ld_out
            ERROR_VARIABLE ld_err)
    if (NOT r2 EQUAL 0)
        message(FATAL_ERROR "link failed: ${ld_out} ${ld_err}")
    endif ()
elseif (MODE STREQUAL "run")
    execute_process(COMMAND "${CLANG_BIN}" ${ASM_FLAGS_LIST} -c out.s -o out.o
            RESULT_VARIABLE r1
            OUTPUT_VARIABLE asm_out
            ERROR_VARIABLE asm_err)
    if (NOT r1 EQUAL 0)
        message(FATAL_ERROR "assembly failed: ${asm_out} ${asm_err}")
    endif ()
    execute_process(COMMAND "${CLANG_BIN}" ${LD_FLAGS_LIST} out.o -o a.out
            RESULT_VARIABLE r2
            OUTPUT_VARIABLE ld_out
            ERROR_VARIABLE ld_err)
    if (NOT r2 EQUAL 0)
        message(FATAL_ERROR "link failed: ${ld_out} ${ld_err}")
    endif ()
    execute_process(
            COMMAND ./a.out
            RESULT_VARIABLE run_result
            OUTPUT_VARIABLE run_output
            ERROR_VARIABLE run_error)
    if (NOT run_result EQUAL 0)
        message(FATAL_ERROR "run failed: ${run_output} ${run_error}")
    endif ()
else ()
    message(FATAL_ERROR "unknown MODE ${MODE}")
endif ()

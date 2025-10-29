## SPDX-License-Identifier: MIT
## File: tests/e2e/test_vm_strings.cmake
## Purpose: Ensure VM string operations behave as expected.
## Key invariants: Execution output contains specific substrings.
## Ownership/Lifetime: Invoked by CTest.
## Links: docs/codemap.md

# Use a unique filename to avoid collisions when tests run in parallel.
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/vm_strings.out.txt")
execute_process(COMMAND ${ILC} -run ${SRC_DIR}/examples/il/ex5_strings.il
        OUTPUT_FILE ${OUT_FILE} RESULT_VARIABLE r)
if (NOT r EQUAL 0)
    message(FATAL_ERROR "ex5_strings execution failed")
endif ()
file(READ ${OUT_FILE} S)
string(REGEX MATCH "JOHN DOE" _m1 "${S}")
if (NOT _m1)
    message(FATAL_ERROR "missing JOHN DOE")
endif ()
string(REGEX MATCH "\n8\n" _m2 "${S}")
if (NOT _m2)
    message(FATAL_ERROR "missing 8")
endif ()
string(REGEX MATCH "\nJ\n" _m3 "${S}")
if (NOT _m3)
    message(FATAL_ERROR "missing J")
endif ()
string(REGEX MATCH "1\n$" _m4 "${S}")
if (NOT _m4)
    message(FATAL_ERROR "missing final 1")
endif ()

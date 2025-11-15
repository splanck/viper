## SPDX-License-Identifier: MIT
## File: tests/e2e/test_vm_examples.cmake
## Purpose: Validate VM with simple IL examples.
## Key invariants: Example programs produce expected outputs.
## Ownership/Lifetime: Invoked by CTest.
## Links: docs/examples.md

execute_process(COMMAND ${ILC} -run ${SRC_DIR}/examples/il/ex1_hello_cond.il
        OUTPUT_FILE hello.txt RESULT_VARIABLE r1)
if (NOT r1 EQUAL 0)
    message(FATAL_ERROR "hello_cond execution failed")
endif ()
file(READ hello.txt H1)
string(REGEX MATCH "HELLO" _m1 "${H1}")
if (NOT _m1)
    message(FATAL_ERROR "missing HELLO")
endif ()
string(REGEX MATCH "READY" _m2 "${H1}")
if (NOT _m2)
    message(FATAL_ERROR "missing READY")
endif ()
string(REGEX MATCHALL "[0-9]+" nums1 "${H1}")
list(LENGTH nums1 n1)
if (NOT n1 EQUAL 2)
    message(FATAL_ERROR "expected two integers in hello output")
endif ()

execute_process(COMMAND ${ILC} -run ${SRC_DIR}/examples/il/ex2_sum_1_to_10.il
        OUTPUT_FILE sum.txt RESULT_VARIABLE r2)
if (NOT r2 EQUAL 0)
    message(FATAL_ERROR "sum_1_to_10 execution failed")
endif ()
file(READ sum.txt S1)
string(REGEX MATCH "SUM 1..10" _s1 "${S1}")
if (NOT _s1)
    message(FATAL_ERROR "missing header")
endif ()
string(REGEX MATCH "55" _s2 "${S1}")
if (NOT _s2)
    message(FATAL_ERROR "missing sum 55")
endif ()
string(REGEX MATCH "DONE" _s3 "${S1}")
if (NOT _s3)
    message(FATAL_ERROR "missing DONE")
endif ()

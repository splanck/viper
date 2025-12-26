## File: tests/e2e/test_vm_rec_fact.cmake
## Purpose: Verify VM handles recursive calls via factorial computation.
## Key invariants: Execution output matches golden file.
## Ownership/Lifetime: Invoked by CTest.
## Links: docs/dev/vm.md

if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED SRC_DIR)
    message(FATAL_ERROR "SRC_DIR not set")
endif ()
set(IL_FILE "${SRC_DIR}/src/tests/il/e2e/rec_fact.il")
set(GOLDEN "${SRC_DIR}/src/tests/golden/e2e/rec_fact.out")
# Use a unique filename to avoid collisions when tests run in parallel.
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/vm_rec_fact.out.txt")
execute_process(COMMAND ${ILC} -run ${IL_FILE} OUTPUT_FILE ${OUT_FILE} RESULT_VARIABLE r)
if (NOT r EQUAL 0)
    message(FATAL_ERROR "rec_fact execution failed")
endif ()
file(READ ${OUT_FILE} OUT)
file(READ ${GOLDEN} EXP)
if (NOT OUT STREQUAL EXP)
    message(FATAL_ERROR "stdout mismatch")
endif ()


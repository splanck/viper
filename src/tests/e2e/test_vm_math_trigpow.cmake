## File: tests/e2e/test_vm_math_trigpow.cmake
## Purpose: Ensure VM trig and power math ops produce expected results.
## Key invariants: Execution output matches golden file.
## Ownership/Lifetime: Invoked by CTest.
## Links: docs/codemap.md

if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED SRC_DIR)
    message(FATAL_ERROR "SRC_DIR not set")
endif ()
set(IL_FILE "${SRC_DIR}/tests/il/e2e/math_trigpow.il")
set(GOLDEN "${SRC_DIR}/tests/il/e2e/math_trigpow.out")
# Use a unique filename to avoid collisions when tests run in parallel.
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/vm_math_trigpow.out.txt")
execute_process(COMMAND ${ILC} -run ${IL_FILE} OUTPUT_FILE ${OUT_FILE} RESULT_VARIABLE r)
if (NOT r EQUAL 0)
    message(FATAL_ERROR "math_trigpow execution failed")
endif ()
file(READ ${OUT_FILE} OUT)
string(REPLACE "\\n" "\n" OUT "${OUT}")
file(READ ${GOLDEN} EXP)
string(REPLACE "\\n" "\n" EXP "${EXP}")
if (NOT OUT STREQUAL EXP)
    message(FATAL_ERROR "stdout mismatch")
endif ()

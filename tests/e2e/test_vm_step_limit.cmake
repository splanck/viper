## File: tests/e2e/test_vm_step_limit.cmake
## Purpose: Verify VM aborts when exceeding configured step limit.
## Key invariants: Exceeding the limit yields non-zero exit and message.
## Ownership/Lifetime: Invoked by CTest.
## Links: docs/codemap.md

# Use unique filenames to avoid collisions when tests run in parallel.
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/vm_step_limit.out.txt")
set(ERR_FILE "${CMAKE_CURRENT_BINARY_DIR}/vm_step_limit.err.txt")
execute_process(COMMAND ${ILC} -run ${SRC_DIR}/tests/data/loop.il --max-steps 5
        OUTPUT_FILE ${OUT_FILE} ERROR_FILE ${ERR_FILE} RESULT_VARIABLE r)
if (r EQUAL 0)
    message(FATAL_ERROR "expected non-zero exit")
endif ()
file(READ ${ERR_FILE} E)
string(FIND "${E}" "VM: step limit exceeded (5); aborting." pos)
if (pos EQUAL -1)
    message(FATAL_ERROR "missing step limit message")
endif ()

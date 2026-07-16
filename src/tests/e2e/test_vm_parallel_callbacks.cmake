## SPDX-License-Identifier: GPL-3.0-only
## File: tests/e2e/test_vm_parallel_callbacks.cmake
## Purpose: Verify parallel callback execution on every interpreted VM backend.
## Key invariants: Parallel callbacks execute and VM teardown does not crash.
## Ownership/Lifetime: Invoked by CTest.
## Links: docs/internals/codemap.md

# Regression: Parallel.For + Pool.Submit + DefaultPool callbacks must run on EVERY interpreted
# backend (tree-walking VM, bytecode VM switch dispatch, bytecode VM threaded dispatch) and tear
# down cleanly. Two prior bugs are guarded here:
#   * Tree-walker: the shared default thread pool's workers stayed bound to the VM's per-run
#     runtime context, which was freed at teardown while the pool (a process-lifetime singleton)
#     lived on, so the workers unbound a freed context during the process-exit finalizer sweep —
#     underflowing the context bind_count and aborting (SIGABRT).
#   * Bytecode VM: callback values are tagged module-function indices, not native code. Falling
#     through to the native parallel path dispatched that tag onto a pool thread, which called it
#     as a function pointer (jump to 0x8000...0000) — SIGSEGV.
# A non-zero exit catches either crash; the output checks confirm the callbacks executed.

# Each entry is a "label|flag" pair (empty flag = default tree-walker).
set(_modes "tree-walker|" "bytecode|--bytecode" "bytecode-threaded|--bc-threaded")

foreach (_mode IN LISTS _modes)
    string(REGEX MATCH "^([^|]*)\\|(.*)$" _matched "${_mode}")
    if (NOT _matched)
        message(FATAL_ERROR "invalid vm_parallel_callbacks mode tuple: '${_mode}'")
    endif ()
    set(_label "${CMAKE_MATCH_1}")
    set(_flag "${CMAKE_MATCH_2}")

    set(_cmd ${ILC} -run ${SRC_DIR}/src/tests/data/parallel_callbacks.il)
    if (_flag)
        list(APPEND _cmd ${_flag})
    endif ()

    execute_process(
        COMMAND ${_cmd}
        RESULT_VARIABLE r
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)

    if (NOT r EQUAL 0)
        message(FATAL_ERROR "[${_label}] expected clean exit (0) but got '${r}'\nstdout:\n${out}\nstderr:\n${err}")
    endif ()

    # Parallel.For(0,3,@worker) prints "w" three times (the only 'w' in the output).
    string(REGEX MATCHALL "w" _w_matches "${out}")
    list(LENGTH _w_matches _w_count)
    if (NOT _w_count EQUAL 3)
        message(FATAL_ERROR "[${_label}] expected 3 Parallel.For iterations but got ${_w_count}\nstdout:\n${out}")
    endif ()

    # Pool.Submit must have run @task, and @main must have reached its tail.
    if (NOT out MATCHES "task")
        message(FATAL_ERROR "[${_label}] expected Pool.Submit task to run ('task' missing)\nstdout:\n${out}")
    endif ()
    # ForEach runs per element on every backend (VDOC-126): two "fe" lines.
    string(REGEX MATCHALL "fe\n" _e_matches "${out}")
    list(LENGTH _e_matches _e_count)
    if (NOT _e_count EQUAL 2)
        message(FATAL_ERROR "[${_label}] expected 2 ForEach iterations but got ${_e_count}\nstdout:\n${out}")
    endif ()
    # Map returns a 2-element seq and Reduce computes 1 + 2 = 3.
    if (NOT out MATCHES "2\n3\n")
        message(FATAL_ERROR "[${_label}] expected Map count 2 and Reduce total 3\nstdout:\n${out}")
    endif ()

    # Submitting to a shut-down pool must be rejected on every backend (VDOC-125).
    if (NOT out MATCHES "rejected")
        message(FATAL_ERROR "[${_label}] expected shut-down pool submission to be rejected\nstdout:\n${out}")
    endif ()
    if (NOT out MATCHES "done")
        message(FATAL_ERROR "[${_label}] expected clean completion ('done' missing)\nstdout:\n${out}")
    endif ()
endforeach ()

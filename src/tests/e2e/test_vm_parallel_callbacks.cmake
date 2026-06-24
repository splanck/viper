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
    string(REPLACE "|" ";" _parts "${_mode}")
    list(GET _parts 0 _label)
    list(GET _parts 1 _flag)

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
    if (NOT out MATCHES "done")
        message(FATAL_ERROR "[${_label}] expected clean completion ('done' missing)\nstdout:\n${out}")
    endif ()
endforeach ()

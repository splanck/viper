# File: tests/golden/il_opt/check_opt.cmake
# Purpose: Run the IL optimizer and compare its output to a golden file.
# Key invariants: Uses a unique output file per test to prevent cross-test
# clobbering during parallel runs. Emits helpful diffs and artifacts on failure.
# Ownership/Lifetime: Invoked by CTest for IL optimizer golden tests.
# Links: docs/codemap.md

if (NOT DEFINED ILC)
    message(FATAL_ERROR "ILC not set")
endif ()
if (NOT DEFINED IL_FILE)
    message(FATAL_ERROR "IL_FILE not set")
endif ()
if (NOT DEFINED GOLDEN)
    message(FATAL_ERROR "GOLDEN not set")
endif ()
if (NOT DEFINED PASSES)
    set(PASSES "constfold,peephole")
endif ()
get_filename_component(test_name ${IL_FILE} NAME_WE)
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${test_name}.out.il")
execute_process(
        COMMAND ${ILC} il-opt ${IL_FILE} -o ${OUT_FILE} --passes ${PASSES}
        RESULT_VARIABLE res)
if (NOT res EQUAL 0)
    message(FATAL_ERROR "il-opt failed")
endif ()
execute_process(
        COMMAND diff -u ${GOLDEN} ${OUT_FILE}
        OUTPUT_VARIABLE diff
        RESULT_VARIABLE diff_res)
if (NOT diff_res EQUAL 0)
    set(art_dir "${CMAKE_CURRENT_BINARY_DIR}/_artifacts/il_opt_${test_name}")
    file(MAKE_DIRECTORY ${art_dir})
    configure_file(${GOLDEN} ${art_dir}/golden.il COPYONLY)
    configure_file(${OUT_FILE} ${art_dir}/out.il COPYONLY)
    file(WRITE ${art_dir}/diff.txt "${diff}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo
            "Expected (first 50 lines):")
    execute_process(COMMAND sh -c "cat -n ${GOLDEN} | head -n 50")
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo
            "Got (first 50 lines):")
    execute_process(COMMAND sh -c "cat -n ${OUT_FILE} | head -n 50")
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo "Diff:\n${diff}")
    message(FATAL_ERROR "IL mismatch")
endif ()

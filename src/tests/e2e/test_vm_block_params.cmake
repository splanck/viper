execute_process(COMMAND ${ILC} -run ${SRC_DIR}/src/tests/data/block_params_sum.il
        RESULT_VARIABLE r)
if (NOT r EQUAL 55)
    message(FATAL_ERROR "expected 55")
endif ()

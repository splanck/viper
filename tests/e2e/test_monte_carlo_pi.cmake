execute_process(COMMAND ${ILC} front basic -run ${SRC_DIR}/examples/basic/monte_carlo_pi.bas
                OUTPUT_FILE out.txt RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "execution failed")
endif()
execute_process(COMMAND ${FLOAT_OUT} out.txt ${SRC_DIR}/tests/e2e/monte_carlo_pi.expect
                RESULT_VARIABLE r2)
if(NOT r2 EQUAL 0)
  message(FATAL_ERROR "float mismatch")
endif()

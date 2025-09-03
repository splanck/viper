execute_process(COMMAND ${ILC} front basic -run ${SRC_DIR}/examples/basic/random_repro.bas
                OUTPUT_FILE out.txt RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "execution failed")
endif()
file(READ out.txt R)
if(NOT R STREQUAL "0.345001\n0.752709\n0.795745\n")
  message(FATAL_ERROR "unexpected output: ${R}")
endif()

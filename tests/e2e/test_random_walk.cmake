execute_process(COMMAND ${ILC} front basic -run ${SRC_DIR}/examples/basic/random_walk.bas
                OUTPUT_FILE out.txt RESULT_VARIABLE r)
if(NOT r EQUAL 0)
  message(FATAL_ERROR "execution failed")
endif()
file(READ out.txt O)
if(NOT O STREQUAL "4\n")
  message(FATAL_ERROR "unexpected output: ${O}")
endif()

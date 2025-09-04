if(NOT DEFINED ILC)
  message(FATAL_ERROR "ILC not set")
endif()
if(NOT DEFINED SRC_DIR)
  message(FATAL_ERROR "SRC_DIR not set")
endif()
set(GOLDEN "${SRC_DIR}/tests/goldens/break_src_exact.out")
execute_process(COMMAND ${ILC} -run tests/e2e/BreakSrcExact.bas --break-src tests/e2e/BreakSrcExact.bas:2
                WORKING_DIRECTORY ${SRC_DIR}
                ERROR_FILE ${CMAKE_CURRENT_BINARY_DIR}/out.txt
                RESULT_VARIABLE r)
if(NOT r EQUAL 10)
  message(FATAL_ERROR "execution did not break")
endif()
file(READ ${CMAKE_CURRENT_BINARY_DIR}/out.txt OUT)
file(READ ${GOLDEN} EXP)
if(NOT OUT STREQUAL EXP)
  message(FATAL_ERROR "break output mismatch")
endif()

if(NOT DEFINED ILC)
  message(FATAL_ERROR "ILC not set")
endif()
if(NOT DEFINED IL_FILE)
  message(FATAL_ERROR "IL_FILE not set")
endif()
if(NOT DEFINED GOLDEN)
  message(FATAL_ERROR "GOLDEN not set")
endif()
if(NOT DEFINED PASSES)
  set(PASSES "constfold,peephole")
endif()
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/out.il")
execute_process(
  COMMAND ${ILC} il-opt ${IL_FILE} -o ${OUT_FILE} --passes ${PASSES}
  RESULT_VARIABLE res)
if(NOT res EQUAL 0)
  message(FATAL_ERROR "il-opt failed")
endif()
execute_process(
  COMMAND diff -u ${GOLDEN} ${OUT_FILE}
  OUTPUT_VARIABLE diff
  RESULT_VARIABLE diff_res)
if(NOT diff_res EQUAL 0)
  get_filename_component(test_name ${IL_FILE} NAME_WE)
  set(art_dir "${CMAKE_CURRENT_BINARY_DIR}/_artifacts/il_opt_${test_name}")
  file(MAKE_DIRECTORY ${art_dir})
  configure_file(${GOLDEN} ${art_dir}/golden.il COPYONLY)
  configure_file(${OUT_FILE} ${art_dir}/out.il COPYONLY)
  file(WRITE ${art_dir}/diff.txt "${diff}")
  execute_process(COMMAND ${CMAKE_COMMAND} -E echo
                  "Expected (first 50 lines):")
  execute_process(COMMAND sh -c "nl -ba ${GOLDEN} | head -n 50")
  execute_process(COMMAND ${CMAKE_COMMAND} -E echo
                  "Got (first 50 lines):")
  execute_process(COMMAND sh -c "nl -ba ${OUT_FILE} | head -n 50")
  execute_process(COMMAND ${CMAKE_COMMAND} -E echo "Diff:\n${diff}")
  message(FATAL_ERROR "IL mismatch")
endif()

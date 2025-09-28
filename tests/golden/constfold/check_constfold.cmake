# File: tests/golden/constfold/check_constfold.cmake
# Purpose: Run constfold pass and verify optimized IL plus runtime behaviour.
# Key invariants: Optimized IL matches golden file and preserves stdout/stderr
# and exit status, including traps. Creates per-test artifact directories on
# mismatch to ease debugging.
# Ownership/Lifetime: Invoked by CTest for constfold golden tests.
# Links: docs/codemap.md

if(NOT DEFINED ILC)
  message(FATAL_ERROR "ILC not set")
endif()
if(NOT DEFINED IL_FILE)
  message(FATAL_ERROR "IL_FILE not set")
endif()
if(NOT DEFINED GOLDEN)
  message(FATAL_ERROR "GOLDEN not set")
endif()
get_filename_component(test_name ${IL_FILE} NAME_WE)
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${test_name}.opt.il")
execute_process(
  COMMAND ${ILC} -run ${IL_FILE}
  RESULT_VARIABLE before_res
  OUTPUT_VARIABLE before_out
  ERROR_VARIABLE before_err)
execute_process(
  COMMAND ${ILC} il-opt ${IL_FILE} -o ${OUT_FILE} --passes constfold
  RESULT_VARIABLE opt_res)
if(NOT opt_res EQUAL 0)
  message(FATAL_ERROR "il-opt failed")
endif()
execute_process(
  COMMAND ${ILC} -run ${OUT_FILE}
  RESULT_VARIABLE after_res
  OUTPUT_VARIABLE after_out
  ERROR_VARIABLE after_err)
if(NOT before_res EQUAL after_res)
  message(FATAL_ERROR "exit status changed")
endif()
if(NOT before_out STREQUAL after_out)
  message(FATAL_ERROR "stdout changed")
endif()
if(NOT before_err STREQUAL after_err)
  message(FATAL_ERROR "stderr changed")
endif()
if(DEFINED EXPECT_TRAP)
  if(before_res EQUAL 0)
    message(FATAL_ERROR "expected non-zero exit")
  endif()
  string(FIND "${after_err}" "${EXPECT_TRAP}" trap_pos)
  if(trap_pos EQUAL -1)
    message(FATAL_ERROR "expected trap substring '${EXPECT_TRAP}' not found")
  endif()
else()
  if(NOT before_res EQUAL 0)
    message(FATAL_ERROR "unexpected non-zero exit")
  endif()
endif()
execute_process(
  COMMAND diff -u ${GOLDEN} ${OUT_FILE}
  OUTPUT_VARIABLE diff
  RESULT_VARIABLE diff_res)
if(NOT diff_res EQUAL 0)
  set(art_dir "${CMAKE_CURRENT_BINARY_DIR}/_artifacts/constfold_${test_name}")
  file(MAKE_DIRECTORY ${art_dir})
  configure_file(${GOLDEN} ${art_dir}/golden.il COPYONLY)
  configure_file(${OUT_FILE} ${art_dir}/out.il COPYONLY)
  file(WRITE ${art_dir}/diff.txt "${diff}")
  execute_process(COMMAND ${CMAKE_COMMAND} -E echo "Diff:\n${diff}")
  message(FATAL_ERROR "IL mismatch")
endif()

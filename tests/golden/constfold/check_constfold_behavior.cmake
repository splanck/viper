# File: tests/golden/constfold/check_constfold_behavior.cmake
# Purpose: Run constfold and ensure optimized IL matches the golden output and
#          preserves observable behavior (including traps).
# Key invariants: Uses unique filenames per test and compares both stdout and
#                 stderr before/after folding. When EXPECT_TRAP is true, the
#                 script requires matching trap diagnostics.
# Ownership/Lifetime: Invoked by CTest for constfold-specific golden tests.
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
if(NOT DEFINED EXPECT_TRAP)
  set(EXPECT_TRAP 0)
endif()
if(EXPECT_TRAP)
  if(NOT DEFINED EXPECT)
    message(FATAL_ERROR "EXPECT not set for trap case")
  endif()
endif()
if(NOT DEFINED PASSES)
  set(PASSES "constfold")
endif()

get_filename_component(test_name ${IL_FILE} NAME_WE)
set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${test_name}.opt.il")

execute_process(
  COMMAND ${ILC} il-opt ${IL_FILE} -o ${OUT_FILE} --passes ${PASSES}
  RESULT_VARIABLE opt_res)
if(NOT opt_res EQUAL 0)
  message(FATAL_ERROR "il-opt failed")
endif()

execute_process(
  COMMAND diff -u ${GOLDEN} ${OUT_FILE}
  RESULT_VARIABLE diff_res
  OUTPUT_VARIABLE diff_out)
if(NOT diff_res EQUAL 0)
  set(art_dir "${CMAKE_CURRENT_BINARY_DIR}/_artifacts/constfold_${test_name}")
  file(MAKE_DIRECTORY ${art_dir})
  configure_file(${GOLDEN} ${art_dir}/golden.il COPYONLY)
  configure_file(${OUT_FILE} ${art_dir}/out.il COPYONLY)
  file(WRITE ${art_dir}/diff.txt "${diff_out}")
  message(FATAL_ERROR "IL mismatch")
endif()

execute_process(
  COMMAND ${ILC} -run ${IL_FILE}
  RESULT_VARIABLE before_res
  OUTPUT_VARIABLE before_out
  ERROR_VARIABLE before_err)

execute_process(
  COMMAND ${ILC} -run ${OUT_FILE}
  RESULT_VARIABLE after_res
  OUTPUT_VARIABLE after_out
  ERROR_VARIABLE after_err)

if(EXPECT_TRAP)
  if(before_res EQUAL 0)
    message(FATAL_ERROR "expected trap before constfold")
  endif()
  if(after_res EQUAL 0)
    message(FATAL_ERROR "expected trap after constfold")
  endif()
  if(NOT before_err MATCHES "${EXPECT}")
    message(FATAL_ERROR "expected trap message not found before constfold")
  endif()
  if(NOT after_err MATCHES "${EXPECT}")
    message(FATAL_ERROR "expected trap message not found after constfold")
  endif()
  if(NOT before_res EQUAL after_res)
    message(FATAL_ERROR "trap status changed after constfold")
  endif()
  if(NOT before_err STREQUAL after_err)
    message(FATAL_ERROR "trap diagnostics differ after constfold")
  endif()
  if(NOT before_out STREQUAL after_out)
    message(FATAL_ERROR "stdout changed for trapping program")
  endif()
else()
  if(NOT before_res EQUAL 0)
    message(FATAL_ERROR "run before constfold failed")
  endif()
  if(NOT after_res EQUAL 0)
    message(FATAL_ERROR "run after constfold failed")
  endif()
  if(NOT before_out STREQUAL after_out)
    message(FATAL_ERROR "stdout mismatch after constfold")
  endif()
  if(NOT before_err STREQUAL after_err)
    message(FATAL_ERROR "stderr mismatch after constfold")
  endif()
endif()

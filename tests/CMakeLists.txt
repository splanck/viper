add_executable(test_support unit/test_support.cpp)
target_link_libraries(test_support PRIVATE support)
add_test(NAME test_support COMMAND test_support)

add_executable(test_il_serialize unit/test_il_serialize.cpp)
target_link_libraries(test_il_serialize PRIVATE il_core il_build il_io support)
target_compile_definitions(test_il_serialize PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME test_il_serialize COMMAND test_il_serialize)

add_executable(test_il_roundtrip unit/test_il_roundtrip.cpp)
target_link_libraries(test_il_roundtrip PRIVATE il_core il_io il_verify support)
target_compile_definitions(test_il_roundtrip PRIVATE EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/docs/examples")
add_test(NAME test_il_roundtrip COMMAND test_il_roundtrip)

add_test(NAME il_verify_ex1 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/docs/examples/il/ex1_hello_cond.il)
add_test(NAME il_verify_ex2 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/docs/examples/il/ex2_sum_1_to_10.il)
add_test(NAME il_verify_ex3 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/docs/examples/il/ex3_table_5x5.il)
add_test(NAME il_verify_ex4 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/docs/examples/il/ex4_factorial.il)
add_test(NAME il_verify_ex5 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/docs/examples/il/ex5_strings.il)
add_test(NAME il_verify_ex6 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/docs/examples/il/ex6_heap_array_avg.il)

add_test(NAME vm_examples COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_vm_examples.cmake)

add_executable(test_rt_string unit/test_rt_string.cpp)
target_link_libraries(test_rt_string PRIVATE rt)
add_test(NAME test_rt_string COMMAND test_rt_string)

add_executable(test_rt_string_invalid unit/test_rt_string_invalid.cpp)
target_link_libraries(test_rt_string_invalid PRIVATE rt)
add_test(NAME test_rt_string_invalid COMMAND test_rt_string_invalid)
set_tests_properties(test_rt_string_invalid PROPERTIES WILL_FAIL TRUE)

add_test(NAME vm_strings_example COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_vm_strings.cmake)

add_executable(test_basic_lexer unit/test_basic_lexer.cpp)
target_link_libraries(test_basic_lexer PRIVATE basic_frontend support)
add_test(NAME test_basic_lexer COMMAND test_basic_lexer)

set(BASIC_AST ${CMAKE_BINARY_DIR}/src/tools/basic-ast/basic-ast)
add_test(NAME basic_ast_ex1 COMMAND ${CMAKE_COMMAND}
  -DBASIC_AST=${BASIC_AST}
  -DFILE=${CMAKE_SOURCE_DIR}/docs/examples/basic/ex1_hello_cond.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/ex1_hello_cond.ast
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/check_ast.cmake)
add_test(NAME basic_ast_ex2 COMMAND ${CMAKE_COMMAND}
  -DBASIC_AST=${BASIC_AST}
  -DFILE=${CMAKE_SOURCE_DIR}/docs/examples/basic/ex2_sum_1_to_10.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/ex2_sum_1_to_10.ast
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/check_ast.cmake)

set(IL_VERIFY ${CMAKE_BINARY_DIR}/src/tools/il-verify/il-verify)
add_test(NAME il_verify_invalid_bad_types
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/bad_types.il
          -DEXPECT=operand\ type\ mismatch
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)
add_test(NAME il_verify_invalid_missing_term
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/missing_terminator.il
          -DEXPECT=missing\ terminator
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)
add_test(NAME il_verify_invalid_wrong_call_arity
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/wrong_call_arity.il
          -DEXPECT=call\ arg\ count
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)
add_test(NAME il_verify_invalid_unknown_symbol
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/unknown_symbol.il
          -DEXPECT=unknown\ callee
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)
add_test(NAME il_verify_invalid_bad_load_store
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/bad_load_store.il
          -DEXPECT=pointer\ type\ mismatch
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)

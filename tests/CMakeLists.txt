# SPDX-License-Identifier: MIT
# File: tests/CMakeLists.txt
# Purpose: Configure test targets and optional docs comment lint.

option(IL_ENABLE_DOCS_LINT "Run header/Doxygen docs check (scripts/check_comments.py)" OFF)

if(IL_ENABLE_DOCS_LINT)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  execute_process(COMMAND git rev-parse --is-inside-work-tree
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  RESULT_VARIABLE GIT_OK
                  OUTPUT_QUIET ERROR_QUIET)
  if(GIT_OK EQUAL 0)
    add_test(NAME docs_comment_check
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/check_comments.py)
    set_tests_properties(docs_comment_check PROPERTIES LABELS Docs)
  else()
    message(STATUS "Skipping docs_comment_check (not a Git working tree)")
  endif()
endif()

add_executable(test_support unit/test_support.cpp)
target_link_libraries(test_support PRIVATE support)
add_test(NAME test_support COMMAND test_support)

add_executable(test_il_serialize unit/test_il_serialize.cpp)
target_link_libraries(test_il_serialize PRIVATE il_core il_build il_io support)
target_compile_definitions(test_il_serialize PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME test_il_serialize COMMAND test_il_serialize)

add_executable(test_il_malformed_cbr unit/test_il_malformed_cbr.cpp)
target_link_libraries(test_il_malformed_cbr PRIVATE il_core il_io support)
add_test(NAME test_il_malformed_cbr COMMAND test_il_malformed_cbr)

add_executable(test_il_roundtrip unit/test_il_roundtrip.cpp)
target_link_libraries(test_il_roundtrip PRIVATE il_core il_io il_verify support)
target_compile_definitions(test_il_roundtrip PRIVATE EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples" ROUNDTRIP_DIR="${CMAKE_SOURCE_DIR}/tests/il/roundtrip")
add_test(NAME test_il_roundtrip COMMAND test_il_roundtrip)

add_executable(test_il_parse_negative unit/test_il_parse_negative.cpp)
target_link_libraries(test_il_parse_negative PRIVATE il_core il_io support)
target_compile_definitions(test_il_parse_negative PRIVATE BAD_DIR="${CMAKE_SOURCE_DIR}/tests/il/parse")
add_test(NAME test_il_parse_negative COMMAND test_il_parse_negative)

add_executable(test_il_parse_comment unit/test_il_parse_comment.cpp)
target_link_libraries(test_il_parse_comment PRIVATE il_core il_io support)
add_test(NAME test_il_parse_comment COMMAND test_il_parse_comment)

add_executable(test_il_comments unit/test_il_comments.cpp)
target_link_libraries(test_il_comments PRIVATE il_core il_io support)
add_test(NAME test_il_comments COMMAND test_il_comments)

add_executable(test_basic_lexer_high_bit unit/test_basic_lexer_high_bit.cpp)
target_link_libraries(test_basic_lexer_high_bit PRIVATE fe_basic support)
add_test(NAME test_basic_lexer_high_bit COMMAND test_basic_lexer_high_bit)

add_executable(test_il_utils il/UtilsTests.cpp)
target_link_libraries(test_il_utils PRIVATE IL)
add_test(NAME test_il_utils COMMAND test_il_utils)

add_executable(test_tools_break_parsing tools/BreakParsingTests.cpp)
target_include_directories(test_tools_break_parsing PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_tools_break_parsing COMMAND test_tools_break_parsing)

add_executable(test_vm_trace_il vm/TraceILTests.cpp)
add_test(NAME test_vm_trace_il COMMAND test_vm_trace_il $<TARGET_FILE:ilc> ${CMAKE_SOURCE_DIR}/examples/il/trace_min.il ${CMAKE_SOURCE_DIR}/tests/vm/trace_min.trace)
add_executable(test_vm_break_label vm/BreakLabelTests.cpp)
add_test(NAME test_vm_break_label COMMAND test_vm_break_label $<TARGET_FILE:ilc> ${CMAKE_SOURCE_DIR}/examples/il/break_label.il)
add_executable(test_vm_debug_script vm/DebugScriptTests.cpp)
add_test(NAME test_vm_debug_script COMMAND test_vm_debug_script $<TARGET_FILE:ilc> ${CMAKE_SOURCE_DIR}/examples/il/debug_script.il ${CMAKE_SOURCE_DIR}/examples/il/debug_script.txt)
add_executable(test_vm_watch vm/WatchTests.cpp)
add_test(NAME test_vm_watch COMMAND test_vm_watch $<TARGET_FILE:ilc> ${CMAKE_SOURCE_DIR}/tests/vm/WatchTests.il)

add_executable(test_vm_summary vm/SummaryTests.cpp)
add_test(NAME test_vm_summary COMMAND test_vm_summary $<TARGET_FILE:ilc> ${CMAKE_SOURCE_DIR}/examples/il/summary.il)


add_executable(test_analysis_cfg analysis/CFGTests.cpp)
target_link_libraries(test_analysis_cfg PRIVATE Analysis il_build)
add_test(NAME test_analysis_cfg COMMAND test_analysis_cfg)

add_executable(test_analysis_graph_order analysis/GraphOrderTests.cpp)
target_link_libraries(test_analysis_graph_order PRIVATE Analysis il_build)
add_test(NAME test_analysis_graph_order COMMAND test_analysis_graph_order)

add_executable(test_analysis_dominators analysis/DominatorsTests.cpp)
target_link_libraries(test_analysis_dominators PRIVATE Analysis il_build)
add_test(NAME test_analysis_dominators COMMAND test_analysis_dominators)

add_executable(test_analysis_acyclic analysis/AcyclicTests.cpp)
target_link_libraries(test_analysis_acyclic PRIVATE Analysis il_build)
add_test(NAME test_analysis_acyclic COMMAND test_analysis_acyclic)

add_test(NAME il_verify_ex1 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex1_hello_cond.il)
add_test(NAME il_verify_ex2 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex2_sum_1_to_10.il)
add_test(NAME il_verify_ex3 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex3_table_5x5.il)
add_test(NAME il_verify_ex4 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex4_factorial.il)
add_test(NAME il_verify_ex5 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex5_strings.il)
add_test(NAME il_verify_ex6 COMMAND $<TARGET_FILE:il-verify> ${CMAKE_SOURCE_DIR}/examples/il/ex6_heap_array_avg.il)

add_test(NAME vm_examples COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_vm_examples.cmake)
set_tests_properties(vm_examples PROPERTIES LABELS VM)

add_executable(test_rt_string unit/test_rt_string.cpp)
target_link_libraries(test_rt_string PRIVATE rt)
add_test(NAME test_rt_string COMMAND test_rt_string)

add_executable(test_rt_string_invalid unit/test_rt_string_invalid.cpp)
target_link_libraries(test_rt_string_invalid PRIVATE rt)
add_test(NAME test_rt_string_invalid COMMAND test_rt_string_invalid)
set_tests_properties(test_rt_string_invalid PROPERTIES WILL_FAIL TRUE)

add_executable(test_rt_conv unit/test_rt_conv.cpp)
target_link_libraries(test_rt_conv PRIVATE rt)
add_test(NAME test_rt_conv COMMAND test_rt_conv)

add_executable(test_rt_val_str runtime/RTValStrTests.cpp)
target_link_libraries(test_rt_val_str PRIVATE rt)
add_test(NAME test_rt_val_str COMMAND test_rt_val_str)

add_executable(test_rt_math_core runtime/RTMathCoreTests.cpp)
target_link_libraries(test_rt_math_core PRIVATE rt)
add_test(NAME test_rt_math_core COMMAND test_rt_math_core)

add_executable(test_rt_random runtime/RTRandomTests.cpp)
target_link_libraries(test_rt_random PRIVATE rt)
add_test(NAME test_rt_random COMMAND test_rt_random)

add_executable(test_rt_instr runtime/RTInstrTests.cpp)
target_link_libraries(test_rt_instr PRIVATE rt)
add_test(NAME test_rt_instr COMMAND test_rt_instr)

add_executable(test_rt_trim_case runtime/RTTrimCaseTests.cpp)
target_link_libraries(test_rt_trim_case PRIVATE rt)
add_test(NAME test_rt_trim_case COMMAND test_rt_trim_case)

add_executable(test_rt_chr_asc runtime/RTChrAscTests.cpp)
target_link_libraries(test_rt_chr_asc PRIVATE rt)
add_test(NAME test_rt_chr_asc COMMAND test_rt_chr_asc)

add_executable(test_rt_chr_invalid runtime/RTChrInvalidTests.cpp)
target_link_libraries(test_rt_chr_invalid PRIVATE rt)
add_test(NAME test_rt_chr_invalid COMMAND test_rt_chr_invalid)

add_executable(test_rt_string_ranges runtime/RTStringRangeTests.cpp)
target_link_libraries(test_rt_string_ranges PRIVATE rt)
add_test(NAME test_rt_string_ranges COMMAND test_rt_string_ranges)

add_executable(test_rt_alloc runtime/RTAllocTests.cpp)
target_link_libraries(test_rt_alloc PRIVATE rt)
add_test(NAME test_rt_alloc COMMAND test_rt_alloc)

add_executable(test_rt_input_line runtime/RTInputLineTests.cpp)
target_link_libraries(test_rt_input_line PRIVATE rt)
add_test(NAME test_rt_input_line COMMAND test_rt_input_line)

add_executable(float_out e2e/support/FloatOut.cpp)

add_test(NAME vm_strings_example COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_vm_strings.cmake)
set_tests_properties(vm_strings_example PROPERTIES LABELS VM)

add_test(NAME vm_step_limit COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_vm_step_limit.cmake)
set_tests_properties(vm_step_limit PROPERTIES LABELS VM)

add_test(NAME vm_block_params COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_vm_block_params.cmake)
set_tests_properties(vm_block_params PROPERTIES LABELS VM)

add_test(NAME vm_math_core COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_vm_math_core.cmake)
set_tests_properties(vm_math_core PROPERTIES LABELS VM)

add_test(NAME vm_math_trigpow COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_vm_math_trigpow.cmake)
set_tests_properties(vm_math_trigpow PROPERTIES LABELS VM)

add_test(NAME vm_rec_fact COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_vm_rec_fact.cmake)
set_tests_properties(vm_rec_fact PROPERTIES LABELS VM)

add_test(NAME vm_trace_src COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/trace_src.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/e2e/TraceSrcTests.bas.out
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_trace_src.cmake)
set_tests_properties(vm_trace_src PROPERTIES LABELS VM)

add_test(NAME vm_break_src_exact COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_FILE=tests/e2e/BreakSrcExact.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/goldens/break_src_exact.out
  -DLINE=1
  -DROOT=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_break_src_exact.cmake)
set_tests_properties(vm_break_src_exact PROPERTIES LABELS VM DISABLED TRUE)

add_test(NAME vm_break_src_coalesce COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_FILE=tests/e2e/BreakSrcCoalesce.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/goldens/break_src_coalesce.out
  -DLINE=1
  -DROOT=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_break_src_exact.cmake)
set_tests_properties(vm_break_src_coalesce PROPERTIES LABELS VM)

add_test(NAME front_basic_example COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_front_basic.cmake)

add_test(NAME basic_math_phase1 COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/e2e/basic_math_phase1.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/e2e/basic_math_phase1.bas.out
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_basic_math_phase1.cmake)

add_test(NAME basic_math_phase2 COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/e2e/basic_math_phase2.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/e2e/basic_math_phase2.bas.out
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_basic_math_phase1.cmake)

add_test(NAME basic_abs_mixed COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/e2e/basic_abs_mixed.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/e2e/basic_abs_mixed.bas.out
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_basic_math_phase1.cmake)

add_test(NAME basic_factorial COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/e2e/factorial.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/e2e/goldens/factorial.out
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_basic_math_phase1.cmake)
set_tests_properties(basic_factorial PROPERTIES WILL_FAIL TRUE)
add_test(NAME basic_fibonacci COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/e2e/fibonacci.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/e2e/goldens/fibonacci.out
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_basic_math_phase1.cmake)
set_tests_properties(basic_fibonacci PROPERTIES WILL_FAIL TRUE)
add_test(NAME basic_random_repro COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_basic_random_repro.cmake)

add_test(NAME monte_carlo_pi COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DFLOAT_OUT=$<TARGET_FILE:float_out>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_monte_carlo_pi.cmake)

add_test(NAME random_walk COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DSRC_DIR=${CMAKE_SOURCE_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_random_walk.cmake)

add_test(NAME il_opt_equiv COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DIL_FILE=${CMAKE_SOURCE_DIR}/tests/golden/il_opt/e2e.in.il
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_il_opt_equiv.cmake)
add_test(NAME constfold_equiv COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DIL_FILE=${CMAKE_SOURCE_DIR}/tests/il/e2e/math_constfold.il
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_constfold_equiv.cmake)

add_test(NAME mem2reg_basic COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/data/mem2reg.bas
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_mem2reg_basic.cmake)

add_test(NAME ilc_mem2reg_stats COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DIL_FILE=${CMAKE_SOURCE_DIR}/tests/golden/il_opt/mem2reg_simple.in.il
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_mem2reg_stats.cmake)

add_test(NAME ilc_mem2reg_stats_multi COMMAND ${CMAKE_COMMAND}
  -DILC=$<TARGET_FILE:ilc>
  -DIL_FILE=${CMAKE_SOURCE_DIR}/tests/golden/il_opt/mem2reg_multi.in.il
  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_mem2reg_stats_multi.cmake)

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

add_test(NAME il_verify_invalid_wrong_extern_sig
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/wrong_extern_sig.il
          -DEXPECT=extern\ @rt_print_str\ signature\ mismatch
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)
add_test(NAME il_verify_invalid_wrong_extern_call
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/wrong_extern_call.il
          -DEXPECT=call\ arg\ type\ mismatch
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)

add_test(NAME il_verify_invalid_wrong_block_arg_arity
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/wrong_block_arg_arity.il
          -DEXPECT=bad\ arg\ count
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)

add_test(NAME il_verify_invalid_wrong_block_arg_type
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/wrong_block_arg_type.il
          -DEXPECT=arg\ type\ mismatch
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)

add_test(NAME il_verify_invalid_duplicate_param
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/duplicate_param.il
          -DEXPECT=duplicate\ param
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)

add_test(NAME il_verify_invalid_param_use_before_entry
  COMMAND ${CMAKE_COMMAND}
          -DIL_VERIFY=${IL_VERIFY}
          -DFILE=${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/param_use_before_entry.il
          -DEXPECT=use\ before\ def
          -P ${CMAKE_SOURCE_DIR}/tests/golden/invalid_il/check_invalid.cmake)

add_executable(test_basic_lexer unit/test_basic_lexer.cpp)
target_link_libraries(test_basic_lexer PRIVATE fe_basic support)
add_test(NAME test_basic_lexer COMMAND test_basic_lexer)

add_executable(test_basic_loc unit/test_basic_loc.cpp)
target_link_libraries(test_basic_loc PRIVATE fe_basic il_build il_core support)
add_test(NAME test_basic_loc COMMAND test_basic_loc)

add_executable(test_basic_expr_loc unit/test_basic_expr_loc.cpp)
target_link_libraries(test_basic_expr_loc PRIVATE fe_basic support)
add_test(NAME test_basic_expr_loc COMMAND test_basic_expr_loc)

add_executable(test_basic_semantic unit/test_basic_semantic.cpp)
target_link_libraries(test_basic_semantic PRIVATE fe_basic support)
add_test(NAME test_basic_semantic COMMAND test_basic_semantic)

add_executable(test_basic_intrinsic_semantics unit/test_basic_intrinsic_semantics.cpp)
target_link_libraries(test_basic_intrinsic_semantics PRIVATE fe_basic support)
add_test(NAME test_basic_intrinsic_semantics COMMAND test_basic_intrinsic_semantics)

add_executable(test_basic_diagnostic unit/test_basic_diagnostic.cpp)
target_link_libraries(test_basic_diagnostic PRIVATE fe_basic support)
add_test(NAME test_basic_diagnostic COMMAND test_basic_diagnostic)

add_executable(test_basic_constfold unit/test_basic_constfold.cpp)
target_link_libraries(test_basic_constfold PRIVATE fe_basic support)
add_test(NAME test_basic_constfold COMMAND test_basic_constfold)

add_executable(test_basic_parse_array_var unit/test_basic_parse_array_var.cpp)
target_link_libraries(test_basic_parse_array_var PRIVATE fe_basic support)
add_test(NAME test_basic_parse_array_var COMMAND test_basic_parse_array_var)

add_executable(test_basic_intrinsics unit/test_basic_intrinsics.cpp)
target_link_libraries(test_basic_intrinsics PRIVATE fe_basic support)
add_test(NAME test_basic_intrinsics COMMAND test_basic_intrinsics)

add_executable(test_vm_trap_loc unit/test_vm_trap_loc.cpp)
target_link_libraries(test_vm_trap_loc PRIVATE il_build il_vm support)
add_test(NAME test_vm_trap_loc COMMAND test_vm_trap_loc)

add_executable(test_vm_rt_trap_loc unit/test_vm_rt_trap_loc.cpp)
target_link_libraries(test_vm_rt_trap_loc PRIVATE il_build il_vm support)
add_test(NAME test_vm_rt_trap_loc COMMAND test_vm_rt_trap_loc)

add_executable(test_vm_alloca_negative unit/test_vm_alloca_negative.cpp)
target_link_libraries(test_vm_alloca_negative PRIVATE il_build il_vm support)
add_test(NAME test_vm_alloca_negative COMMAND test_vm_alloca_negative)

add_executable(test_vm_normalize_path unit/test_vm_normalize_path.cpp)
target_link_libraries(test_vm_normalize_path PRIVATE VMTrace)
add_test(NAME test_vm_normalize_path COMMAND test_vm_normalize_path)

add_executable(test_path_normalize unit/PathNormalizeTests.cpp)
target_link_libraries(test_path_normalize PRIVATE VMTrace)
add_test(NAME test_path_normalize COMMAND test_path_normalize)


set(BASIC_AST_DUMP $<TARGET_FILE:basic-ast-dump>)
add_test(NAME basic_ast_ex1 COMMAND ${CMAKE_COMMAND}
  -DBASIC_AST_DUMP=${BASIC_AST_DUMP}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex1_hello_cond.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/ex1_hello_cond.ast
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/check_ast.cmake)
add_test(NAME basic_ast_ex2 COMMAND ${CMAKE_COMMAND}
  -DBASIC_AST_DUMP=${BASIC_AST_DUMP}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex2_sum_1_to_10.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/ex2_sum_1_to_10.ast
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/check_ast.cmake)

add_test(NAME basic_ast_divmod_precedence COMMAND ${CMAKE_COMMAND}
  -DBASIC_AST_DUMP=${BASIC_AST_DUMP}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/divmod_precedence.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/divmod_precedence.ast
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/check_ast.cmake)

add_test(NAME basic_ast_float_literals COMMAND ${CMAKE_COMMAND}
  -DBASIC_AST_DUMP=${BASIC_AST_DUMP}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/float_literals.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/float_literals.ast
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/check_ast.cmake)

add_test(NAME basic_ast_functions_subs_parsing COMMAND ${CMAKE_COMMAND}
  -DBASIC_AST_DUMP=${BASIC_AST_DUMP}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/functions_subs_parsing.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/functions_subs_parsing.ast
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/check_ast.cmake)

add_test(NAME basic_ast_funcs_then_code COMMAND ${CMAKE_COMMAND}
  -DBASIC_AST_DUMP=${BASIC_AST_DUMP}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/parse/FuncsThenCode.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/basic/parse/FuncsThenCode.ast
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/check_ast.cmake)

add_test(NAME basic_ast_intrinsic_calls COMMAND ${CMAKE_COMMAND}
  -DBASIC_AST_DUMP=${BASIC_AST_DUMP}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/parse/IntrinsicCalls.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/basic/parse/IntrinsicCalls.ast
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_ast/check_ast.cmake)

set(BASIC_LEX_DUMP $<TARGET_FILE:basic-lex-dump>)
add_test(NAME basic_lex_comments COMMAND ${CMAKE_COMMAND}
  -DBASIC_LEX_DUMP=${BASIC_LEX_DUMP}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/lex/Comments.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/basic/lex/Comments.tokens
  -P ${CMAKE_SOURCE_DIR}/tests/basic/lex/check_tokens.cmake)

set(BASIC_ILC $<TARGET_FILE:ilc>)
add_test(NAME basic_to_il_ex1 COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex1_hello_cond.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/ex1_hello_cond.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_ex2 COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex2_sum_1_to_10.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/ex2_sum_1_to_10.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_ex3 COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex3_for_table.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/ex3_for_table.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_ex4 COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex4_if_elseif.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/ex4_if_elseif.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_ex_elseif COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_elseif.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/ex_elseif.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_ex6 COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex6_array_sum.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/ex6_array_sum.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_not COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_not.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/not.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_print_commas COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_print_commas.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/print_commas.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_print_semicolons COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_print_semicolons.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/print_semicolons.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_print_newline_control COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_print_newline_control.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/print_newline_control.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_colon COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_colon.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/ex_colon.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_loc COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/loc_add.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/loc_add.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_strings COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/strings.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/strings.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_conversions COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/conversions.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/conversions.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_float_ops COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/float_ops.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/float_ops.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_math_phase1 COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/math_phase1.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/math_phase1.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_math_phase2 COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/math_phase2.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/math_phase2.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_abs_mixed COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/abs_mixed.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/abs_mixed.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_bounds_checks COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/bounds_check.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/bounds_check.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il_bounds.cmake)
add_test(NAME basic_to_il_randomize COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/randomize.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/randomize.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_rnd COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/rnd.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/rnd.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_lower_funcdef_only COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/lower_funcdef_only.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/lower_funcdef_only.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_calls_lowering COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/calls_lowering.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/calls_lowering.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_instr COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/instr.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/instr.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_len_mid COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/goldens/LenMid.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/basic/goldens/LenMid.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_instr_dispatch COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/goldens/Instr.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/basic/goldens/Instr.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_labels_proc_loops COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/goldens/labels_proc_loops.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/basic/goldens/labels_proc_loops.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

add_test(NAME basic_to_il_lower_program_with_proc COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/goldens/LowerProgramWithProc.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/basic/goldens/LowerProgramWithProc.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)
add_test(NAME basic_to_il_return_nest COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/goldens/ReturnNest.bas
  -DGOLDEN=${CMAKE_SOURCE_DIR}/tests/basic/goldens/ReturnNest.il
  -P ${CMAKE_SOURCE_DIR}/tests/golden/basic_to_il/check_il.cmake)

# BASIC string intrinsic tests
add_test(NAME basic_strings COMMAND ${CMAKE_SOURCE_DIR}/scripts/test_strings.sh)
set_tests_properties(basic_strings PROPERTIES LABELS BasicStrings)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic/semantics/proc_registration_ok.err EXPECT_PROC_REG)
string(STRIP "${EXPECT_PROC_REG}" EXPECT_PROC_REG)
add_test(NAME basic_semantics_proc_registration COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic/semantics/proc_registration_ok.bas
  "-DEXPECT=${EXPECT_PROC_REG}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/golden/basic_errors/check_error.cmake)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic/semantics/missing_return.err EXPECT_MISSING_RET)
string(STRIP "${EXPECT_MISSING_RET}" EXPECT_MISSING_RET)
add_test(NAME basic_semantics_missing_return COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic/semantics/missing_return.bas
  "-DEXPECT=${EXPECT_MISSING_RET}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/golden/basic_errors/check_error.cmake)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/basic/semantics/sub_in_expr.err EXPECT_SUB_IN_EXPR)
string(STRIP "${EXPECT_SUB_IN_EXPR}" EXPECT_SUB_IN_EXPR)
add_test(NAME basic_semantics_sub_in_expr COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic/semantics/sub_in_expr.bas
  "-DEXPECT=${EXPECT_SUB_IN_EXPR}"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/golden/basic_errors/check_error.cmake)

add_test(NAME basic_semantics_funcs_then_code COMMAND ${CMAKE_COMMAND}
  -DILC=${BASIC_ILC}
  -DBAS_FILE=${CMAKE_CURRENT_SOURCE_DIR}/basic/semantics/FuncsThenCode.ok.bas
  -P ${CMAKE_CURRENT_SOURCE_DIR}/basic/semantics/check_ok.cmake)

add_subdirectory(golden)

set(CODEGEN_E2E ${CMAKE_CURRENT_SOURCE_DIR}/e2e/test_codegen.cmake)
# Toolchain smoke tests (require Clang)
if(IL_ENABLE_X64_ASM_SYNTAX_CHECK AND CLANG_FOUND)
  add_test(NAME codegen_syntax_only COMMAND ${CMAKE_COMMAND}
    "-DASM_FLAGS=${IL_X64_ASM_FLAGS}"
    "-DLD_FLAGS=${IL_X64_LD_FLAGS}"
    -DMODE=syntax
    -P ${CODEGEN_E2E})
  set_tests_properties(codegen_syntax_only PROPERTIES LABELS CodegenSyntaxOnly)
else()
  message(STATUS "Skipping codegen_* tests (CLANG not found or flags OFF)")
endif()
if(IL_ENABLE_X64_ASM_ASSEMBLE_LINK AND CLANG_FOUND)
  add_test(NAME codegen_assemble_link COMMAND ${CMAKE_COMMAND}
    "-DASM_FLAGS=${IL_X64_ASM_FLAGS}"
    "-DLD_FLAGS=${IL_X64_LD_FLAGS}"
    -DMODE=assemble_link
    -P ${CODEGEN_E2E})
  set_tests_properties(codegen_assemble_link PROPERTIES LABELS CodegenAssembleLink)
else()
  message(STATUS "Skipping codegen_* tests (CLANG not found or flags OFF)")
endif()
if(IL_ENABLE_X64_NATIVE_RUN AND CLANG_FOUND)
  add_test(NAME codegen_native_run COMMAND ${CMAKE_COMMAND}
    "-DASM_FLAGS=${IL_X64_ASM_FLAGS}"
    "-DLD_FLAGS=${IL_X64_LD_FLAGS}"
    -DMODE=run
    -P ${CODEGEN_E2E})
  set_tests_properties(codegen_native_run PROPERTIES LABELS NativeX64Run)
else()
  message(STATUS "Skipping codegen_* tests (CLANG not found or flags OFF)")
endif()

# SPDX-License-Identifier: MIT
# File: tests/basic/CMakeLists.txt
# Purpose: BASIC frontend unit and golden tests.

set(VIPER_BASIC_LIBS fe_basic)
set(VIPER_BASIC_LIBS ${VIPER_BASIC_LIBS} PARENT_SCOPE)

function(viper_add_basic_early_tests)
    if (TARGET test_basic_lexer_high_bit)
        return()
    endif ()

    viper_add_test(test_basic_lexer_high_bit ${VIPER_TESTS_DIR}/unit/test_basic_lexer_high_bit.cpp)
    target_link_libraries(test_basic_lexer_high_bit PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_lexer_high_bit test_basic_lexer_high_bit)
endfunction()

function(viper_add_basic_main_tests)
    if (TARGET test_basic_lexer)
        return()
    endif ()

    set(_VIPER_BASIC_UNIT_DIR ${VIPER_TESTS_DIR}/unit)

    viper_add_test(test_basic_lexer ${_VIPER_BASIC_UNIT_DIR}/test_basic_lexer.cpp)
    target_link_libraries(test_basic_lexer PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_lexer test_basic_lexer)

    viper_add_test(test_basic_loc ${_VIPER_BASIC_UNIT_DIR}/test_basic_loc.cpp)
    target_link_libraries(test_basic_loc PRIVATE fe_basic il_build viper_il_full)
    viper_add_ctest(test_basic_loc test_basic_loc)

    viper_add_test(test_basic_expr_loc ${_VIPER_BASIC_UNIT_DIR}/test_basic_expr_loc.cpp)
    target_link_libraries(test_basic_expr_loc PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_expr_loc test_basic_expr_loc)

    viper_add_test(test_basic_semantic ${_VIPER_BASIC_UNIT_DIR}/test_basic_semantic.cpp)
    target_link_libraries(test_basic_semantic PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_semantic test_basic_semantic)

    viper_add_test(test_basic_lowerer_collect ${_VIPER_BASIC_UNIT_DIR}/test_basic_lowerer_collect.cpp)
    target_link_libraries(test_basic_lowerer_collect PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_lowerer_collect test_basic_lowerer_collect)

    viper_add_test(test_basic_lowerer_conversions ${_VIPER_BASIC_UNIT_DIR}/test_basic_lowerer_conversions.cpp)
    target_link_libraries(test_basic_lowerer_conversions PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_lowerer_conversions test_basic_lowerer_conversions)

    viper_add_test(test_basic_emit_common ${_VIPER_BASIC_UNIT_DIR}/test_basic_emit_common.cpp)
    target_link_libraries(test_basic_emit_common PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_emit_common test_basic_emit_common)

    viper_add_test(test_basic_lowerer_runtime_helpers ${_VIPER_BASIC_UNIT_DIR}/test_basic_lowerer_runtime_helpers.cpp)
    target_link_libraries(test_basic_lowerer_runtime_helpers PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_lowerer_runtime_helpers test_basic_lowerer_runtime_helpers)

    viper_add_test(test_basic_lowerer_string_assignment ${_VIPER_BASIC_UNIT_DIR}/test_basic_lowerer_string_assignment.cpp)
    target_link_libraries(test_basic_lowerer_string_assignment PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_lowerer_string_assignment test_basic_lowerer_string_assignment)

    viper_add_test(test_basic_lowerer_string_escapes ${_VIPER_BASIC_UNIT_DIR}/test_basic_lowerer_string_escapes.cpp)
    target_link_libraries(test_basic_lowerer_string_escapes PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_lowerer_string_escapes test_basic_lowerer_string_escapes)

    viper_add_test(test_basic_lowerer_select_case ${_VIPER_BASIC_UNIT_DIR}/test_basic_lowerer_select_case.cpp)
    target_link_libraries(test_basic_lowerer_select_case PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_lowerer_select_case test_basic_lowerer_select_case)

    viper_add_test(test_basic_semantic_components ${_VIPER_BASIC_UNIT_DIR}/test_basic_semantic_components.cpp)
    target_link_libraries(test_basic_semantic_components PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_semantic_components test_basic_semantic_components)

    viper_add_test(test_basic_semantic_arrays ${_VIPER_BASIC_UNIT_DIR}/test_basic_semantic_arrays.cpp)
    target_link_libraries(test_basic_semantic_arrays PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_semantic_arrays test_basic_semantic_arrays)

    viper_add_test(
            test_basic_semantic_proc_bool_array
            ${_VIPER_BASIC_UNIT_DIR}/test_basic_semantic_proc_bool_array.cpp)
    target_link_libraries(test_basic_semantic_proc_bool_array PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_basic_semantic_proc_bool_array test_basic_semantic_proc_bool_array)

    viper_add_test(test_basic_intrinsic_semantics ${_VIPER_BASIC_UNIT_DIR}/test_basic_intrinsic_semantics.cpp)
    target_link_libraries(test_basic_intrinsic_semantics PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_intrinsic_semantics test_basic_intrinsic_semantics)

    viper_add_test(test_basic_diagnostic ${_VIPER_BASIC_UNIT_DIR}/test_basic_diagnostic.cpp)
    target_link_libraries(test_basic_diagnostic PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_diagnostic test_basic_diagnostic)

    viper_add_test(test_basic_constfold ${_VIPER_BASIC_UNIT_DIR}/test_basic_constfold.cpp)
    target_link_libraries(test_basic_constfold PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_constfold test_basic_constfold)

    viper_add_test(test_basic_compiler ${_VIPER_BASIC_UNIT_DIR}/test_basic_compiler.cpp)
    target_link_libraries(test_basic_compiler PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_compiler test_basic_compiler)

    viper_add_test(test_basic_function_as_type ${_VIPER_BASIC_UNIT_DIR}/test_basic_function_as_type.cpp)
    target_link_libraries(test_basic_function_as_type PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_function_as_type test_basic_function_as_type)

    viper_add_test(test_basic_ast_mutation ${_VIPER_BASIC_UNIT_DIR}/test_basic_ast_mutation.cpp)
    target_link_libraries(test_basic_ast_mutation PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_ast_mutation test_basic_ast_mutation)

    viper_add_test(test_basic_parse_array_var ${_VIPER_BASIC_UNIT_DIR}/test_basic_parse_array_var.cpp)
    target_link_libraries(test_basic_parse_array_var PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_parse_array_var test_basic_parse_array_var)

    viper_add_test(test_basic_parse_loops ${_VIPER_BASIC_UNIT_DIR}/test_basic_parse_loops.cpp)
    target_link_libraries(test_basic_parse_loops PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_parse_loops test_basic_parse_loops)

    viper_add_test(test_basic_parse_do ${_VIPER_BASIC_UNIT_DIR}/test_basic_parse_do.cpp)
    target_link_libraries(test_basic_parse_do PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_parse_do test_basic_parse_do)

    viper_add_test(test_basic_parse_oop ${_VIPER_BASIC_UNIT_DIR}/test_basic_parse_oop.cpp)
    target_link_libraries(test_basic_parse_oop PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_parse_oop test_basic_parse_oop)

    viper_add_test(test_frontends_basic_statement_context ${VIPER_TESTS_DIR}/frontends/basic/ParserStatementContextTests.cpp)
    target_link_libraries(test_frontends_basic_statement_context PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_statement_context test_frontends_basic_statement_context)

    viper_add_test(test_frontends_basic_parse_on_error ${VIPER_TESTS_DIR}/frontends/basic/ParseOnErrorTests.cpp)
    target_link_libraries(test_frontends_basic_parse_on_error PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_parse_on_error test_frontends_basic_parse_on_error)

    viper_add_test(test_frontends_basic_parse_file_io ${VIPER_TESTS_DIR}/frontends/basic/ParseFileIoTests.cpp)
    target_link_libraries(test_frontends_basic_parse_file_io PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_parse_file_io test_frontends_basic_parse_file_io)

    viper_add_test(test_frontends_basic_parse_call_stmt ${VIPER_TESTS_DIR}/frontends/basic/ParseCallStatementTests.cpp)
    target_link_libraries(test_frontends_basic_parse_call_stmt PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_parse_call_stmt test_frontends_basic_parse_call_stmt)

    viper_add_test(
            test_frontends_basic_parse_method_call_stmt
            ${VIPER_TESTS_DIR}/frontends/basic/ParseMethodCallStatementTests.cpp)
    target_link_libraries(test_frontends_basic_parse_method_call_stmt PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_parse_method_call_stmt
            test_frontends_basic_parse_method_call_stmt)

    viper_add_test(
            test_frontends_basic_lowerer_select_case
            ${VIPER_TESTS_DIR}/frontends/basic/LowererSelectCaseTests.cpp)
    target_link_libraries(test_frontends_basic_lowerer_select_case PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_lowerer_select_case test_frontends_basic_lowerer_select_case)

    viper_add_test(
            test_frontends_basic_lowerer_oop_default_ctor
            ${VIPER_TESTS_DIR}/frontends/basic/LowererOopConstructorTests.cpp)
    target_link_libraries(test_frontends_basic_lowerer_oop_default_ctor PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_lowerer_oop_default_ctor
            test_frontends_basic_lowerer_oop_default_ctor)

    viper_add_test(test_frontends_basic_parse_goto ${VIPER_TESTS_DIR}/frontends/basic/ParseGotoTests.cpp)
    target_link_libraries(test_frontends_basic_parse_goto PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_parse_goto test_frontends_basic_parse_goto)

    viper_add_test(test_frontends_basic_parse_gosub ${VIPER_TESTS_DIR}/frontends/basic/ParseGosubTests.cpp)
    target_link_libraries(test_frontends_basic_parse_gosub PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_parse_gosub test_frontends_basic_parse_gosub)

    viper_add_test(
            test_frontends_basic_parse_type_alias
            ${VIPER_TESTS_DIR}/frontends/basic/ParseTypeAliasTests.cpp)
    target_link_libraries(test_frontends_basic_parse_type_alias PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_parse_type_alias test_frontends_basic_parse_type_alias)

    viper_add_test(test_frontends_basic_parse_select_case ${VIPER_TESTS_DIR}/frontends/basic/ParseSelectCaseTests.cpp)
    target_link_libraries(test_frontends_basic_parse_select_case PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_parse_select_case test_frontends_basic_parse_select_case)

    viper_add_test(
            test_frontends_basic_parse_select_case_positive
            ${VIPER_TESTS_DIR}/frontends/basic/ParseSelectCasePositiveTests.cpp)
    target_link_libraries(test_frontends_basic_parse_select_case_positive PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_parse_select_case_positive
            test_frontends_basic_parse_select_case_positive)

    viper_add_test(
            test_frontends_basic_subroutines_gosub_inline_case
            ${VIPER_TESTS_DIR}/frontends/basic/SubroutinesGosubInlineCaseTests.cpp)
    target_link_libraries(
            test_frontends_basic_subroutines_gosub_inline_case PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_subroutines_gosub_inline_case
            test_frontends_basic_subroutines_gosub_inline_case)

    viper_add_test(
            test_frontends_basic_parse_input_extra_args
            ${VIPER_TESTS_DIR}/frontends/basic/ParserInputExtraArgsTests.cpp)
    target_link_libraries(test_frontends_basic_parse_input_extra_args PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_parse_input_extra_args
            test_frontends_basic_parse_input_extra_args)

    viper_add_test(
            test_frontends_basic_parse_unknown_stmt
            ${VIPER_TESTS_DIR}/frontends/basic/ParserUnknownStatementTests.cpp)
    target_link_libraries(test_frontends_basic_parse_unknown_stmt PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_parse_unknown_stmt test_frontends_basic_parse_unknown_stmt)

    # ADDFILE directive tests
    viper_add_test(
            test_frontends_basic_addfile
            ${VIPER_TESTS_DIR}/frontends/basic/AddFileTests.cpp)
    target_link_libraries(test_frontends_basic_addfile PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_addfile test_frontends_basic_addfile)

    viper_add_test(
            test_frontends_basic_line_label_diagnostics
            ${VIPER_TESTS_DIR}/frontends/basic/ParserLineLabelDiagnosticsTests.cpp)
    target_link_libraries(test_frontends_basic_line_label_diagnostics PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_line_label_diagnostics test_frontends_basic_line_label_diagnostics)

    viper_add_test(test_frontends_basic_lexer_file_io ${VIPER_TESTS_DIR}/frontends/basic/LexerFileIoTests.cpp)
    target_link_libraries(test_frontends_basic_lexer_file_io PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_lexer_file_io test_frontends_basic_lexer_file_io)

    viper_add_test(
            test_frontends_basic_lexer_select_case
            ${VIPER_TESTS_DIR}/frontends/basic/LexerSelectCaseTests.cpp)
    target_link_libraries(test_frontends_basic_lexer_select_case PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_lexer_select_case test_frontends_basic_lexer_select_case)

    viper_add_test(test_frontends_basic_semantic_exprs ${VIPER_TESTS_DIR}/frontends/basic/SemanticAnalyzerBinaryExprTests.cpp)
    target_link_libraries(test_frontends_basic_semantic_exprs PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_semantic_exprs test_frontends_basic_semantic_exprs)

    viper_add_test(
            test_frontends_basic_semantic_var_expr
            ${VIPER_TESTS_DIR}/frontends/basic/SemanticAnalyzerVarExprTests.cpp)
    target_link_libraries(test_frontends_basic_semantic_var_expr PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_semantic_var_expr test_frontends_basic_semantic_var_expr)

    viper_add_test(test_frontends_basic_semantic_calls ${VIPER_TESTS_DIR}/frontends/basic/SemanticAnalyzerCallTests.cpp)
    target_link_libraries(test_frontends_basic_semantic_calls PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_semantic_calls test_frontends_basic_semantic_calls)

    # Case-insensitive namespace/proc resolution tests
    if (NOT TARGET test_frontends_basic_semantic_case_insensitive)
        viper_add_test(test_frontends_basic_semantic_case_insensitive ${VIPER_TESTS_DIR}/frontends/basic/SemanticAnalyzerCaseInsensitiveTests.cpp)
        target_link_libraries(test_frontends_basic_semantic_case_insensitive PRIVATE ${VIPER_BASIC_LIBS})
        viper_add_ctest(test_frontends_basic_semantic_case_insensitive test_frontends_basic_semantic_case_insensitive)
    endif ()

    viper_add_test(
            test_frontends_basic_builtin_registry
            ${VIPER_TESTS_DIR}/frontends/basic/BuiltinRegistryTests.cpp)
    target_link_libraries(test_frontends_basic_builtin_registry PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_builtin_registry test_frontends_basic_builtin_registry)

    viper_add_test(test_frontends_basic_semantic_on_error ${VIPER_TESTS_DIR}/frontends/basic/SemanticAnalyzerOnErrorTests.cpp)
    target_link_libraries(test_frontends_basic_semantic_on_error PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_semantic_on_error test_frontends_basic_semantic_on_error)

    viper_add_test(
            test_frontends_basic_semantic_call_stmt
            ${VIPER_TESTS_DIR}/frontends/basic/SemanticAnalyzerCallStatementTests.cpp)
    target_link_libraries(test_frontends_basic_semantic_call_stmt PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_semantic_call_stmt test_frontends_basic_semantic_call_stmt)

    viper_add_test(
            test_frontends_basic_semantic_select_case
            ${VIPER_TESTS_DIR}/frontends/basic/SemanticAnalyzerSelectCaseTests.cpp)
    target_link_libraries(test_frontends_basic_semantic_select_case PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(
            test_frontends_basic_semantic_select_case test_frontends_basic_semantic_select_case)

    viper_add_test(test_frontends_basic_semantic_gosub ${VIPER_TESTS_DIR}/frontends/basic/SemanticAnalyzerGosubTests.cpp)
    target_link_libraries(test_frontends_basic_semantic_gosub PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_semantic_gosub test_frontends_basic_semantic_gosub)

    viper_add_test(test_frontends_basic_semantic_file_io ${VIPER_TESTS_DIR}/frontends/basic/SemanticsFileIoTests.cpp)
    target_link_libraries(test_frontends_basic_semantic_file_io PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_semantic_file_io test_frontends_basic_semantic_file_io)

    viper_add_test(test_frontends_basic_type_rules ${VIPER_TESTS_DIR}/frontends/basic/TypeRulesTests.cpp)
    target_link_libraries(test_frontends_basic_type_rules PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_frontends_basic_type_rules test_frontends_basic_type_rules)

    viper_add_test(test_basic_intrinsics ${_VIPER_BASIC_UNIT_DIR}/test_basic_intrinsics.cpp)
    target_link_libraries(test_basic_intrinsics PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_intrinsics test_basic_intrinsics)

    viper_add_test(test_basic_ast_printer ${_VIPER_BASIC_UNIT_DIR}/test_basic_ast_printer.cpp)
    target_link_libraries(test_basic_ast_printer PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_ast_printer test_basic_ast_printer)

    viper_add_test(test_basic_ast_select_case ${_VIPER_BASIC_UNIT_DIR}/test_basic_ast_select_case.cpp)
    target_link_libraries(test_basic_ast_select_case PRIVATE ${VIPER_BASIC_LIBS})
    viper_add_ctest(test_basic_ast_select_case test_basic_ast_select_case)
endfunction()

function(viper_add_basic_ast_golden_tests)
    if (TEST basic_ast_ex1)
        return()
    endif ()

    set(_VIPER_BASIC_AST_DIR ${VIPER_TESTS_DIR}/golden/basic_ast)
    set(_VIPER_BASIC_PARSE_DIR ${VIPER_TESTS_DIR}/basic/parse)
    set(_VIPER_BASIC_AST_CHECK ${_VIPER_BASIC_AST_DIR}/check_ast.cmake)
    set(_VIPER_BASIC_AST_DUMP $<TARGET_FILE:basic-ast-dump>)

    viper_add_ctest(basic_ast_ex1
            ${CMAKE_COMMAND}
            -DBASIC_AST_DUMP=${_VIPER_BASIC_AST_DUMP}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex1_hello_cond.bas
            -DGOLDEN=${_VIPER_BASIC_AST_DIR}/ex1_hello_cond.ast
            -P ${_VIPER_BASIC_AST_CHECK})

    viper_add_ctest(basic_ast_ex2
            ${CMAKE_COMMAND}
            -DBASIC_AST_DUMP=${_VIPER_BASIC_AST_DUMP}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex2_sum_1_to_10.bas
            -DGOLDEN=${_VIPER_BASIC_AST_DIR}/ex2_sum_1_to_10.ast
            -P ${_VIPER_BASIC_AST_CHECK})

    viper_add_ctest(basic_ast_divmod_precedence
            ${CMAKE_COMMAND}
            -DBASIC_AST_DUMP=${_VIPER_BASIC_AST_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_AST_DIR}/divmod_precedence.bas
            -DGOLDEN=${_VIPER_BASIC_AST_DIR}/divmod_precedence.ast
            -P ${_VIPER_BASIC_AST_CHECK})

    viper_add_ctest(basic_ast_float_literals
            ${CMAKE_COMMAND}
            -DBASIC_AST_DUMP=${_VIPER_BASIC_AST_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_AST_DIR}/float_literals.bas
            -DGOLDEN=${_VIPER_BASIC_AST_DIR}/float_literals.ast
            -P ${_VIPER_BASIC_AST_CHECK})

    viper_add_ctest(basic_ast_functions_subs_parsing
            ${CMAKE_COMMAND}
            -DBASIC_AST_DUMP=${_VIPER_BASIC_AST_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_AST_DIR}/functions_subs_parsing.bas
            -DGOLDEN=${_VIPER_BASIC_AST_DIR}/functions_subs_parsing.ast
            -P ${_VIPER_BASIC_AST_CHECK})

    viper_add_ctest(basic_ast_funcs_then_code
            ${CMAKE_COMMAND}
            -DBASIC_AST_DUMP=${_VIPER_BASIC_AST_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_PARSE_DIR}/FuncsThenCode.bas
            -DGOLDEN=${_VIPER_BASIC_PARSE_DIR}/FuncsThenCode.ast
            -P ${_VIPER_BASIC_AST_CHECK})

    viper_add_ctest(basic_ast_intrinsic_calls
            ${CMAKE_COMMAND}
            -DBASIC_AST_DUMP=${_VIPER_BASIC_AST_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_PARSE_DIR}/IntrinsicCalls.bas
            -DGOLDEN=${_VIPER_BASIC_PARSE_DIR}/IntrinsicCalls.ast
            -P ${_VIPER_BASIC_AST_CHECK})

    viper_add_ctest(basic_ast_boolean_exprs
            ${CMAKE_COMMAND}
            -DBASIC_AST_DUMP=${_VIPER_BASIC_AST_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_PARSE_DIR}/test_boolean_exprs.bas
            -DGOLDEN=${_VIPER_BASIC_PARSE_DIR}/test_boolean_exprs.ast
            -P ${_VIPER_BASIC_AST_CHECK})

    viper_add_ctest(basic_ast_pretty_boolean
            ${CMAKE_COMMAND}
            -DBASIC_AST_DUMP=${_VIPER_BASIC_AST_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_PARSE_DIR}/test_pretty_boolean.bas
            -DGOLDEN=${_VIPER_BASIC_PARSE_DIR}/test_pretty_boolean.ast
            -P ${_VIPER_BASIC_AST_CHECK})

    viper_add_ctest(basic_ast_future_keywords
            ${CMAKE_COMMAND}
            -DBASIC_AST_DUMP=${_VIPER_BASIC_AST_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_PARSE_DIR}/test_future_keywords.bas
            -DGOLDEN=${_VIPER_BASIC_PARSE_DIR}/test_future_keywords.ast
            -P ${_VIPER_BASIC_AST_CHECK})
endfunction()

function(viper_add_basic_lex_golden_tests)
    if (TEST basic_lex_comments)
        return()
    endif ()

    set(_VIPER_BASIC_LEX_DIR ${VIPER_TESTS_DIR}/basic/lex)
    set(_VIPER_BASIC_LEX_DUMP $<TARGET_FILE:basic-lex-dump>)

    viper_add_ctest(basic_lex_comments
            ${CMAKE_COMMAND}
            -DBASIC_LEX_DUMP=${_VIPER_BASIC_LEX_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_LEX_DIR}/Comments.bas
            -DGOLDEN=${_VIPER_BASIC_LEX_DIR}/Comments.tokens
            -P ${_VIPER_BASIC_LEX_DIR}/check_tokens.cmake)

    viper_add_ctest(basic_lex_boolean_tokens
            ${CMAKE_COMMAND}
            -DBASIC_LEX_DUMP=${_VIPER_BASIC_LEX_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_LEX_DIR}/test_boolean_tokens.bas
            -DGOLDEN=${_VIPER_BASIC_LEX_DIR}/test_boolean_tokens.expect
            -P ${_VIPER_BASIC_LEX_DIR}/check_tokens.cmake)

    viper_add_ctest(basic_lex_select_case
            ${CMAKE_COMMAND}
            -DBASIC_LEX_DUMP=${_VIPER_BASIC_LEX_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_LEX_DIR}/select_case.bas
            -DGOLDEN=${_VIPER_BASIC_LEX_DIR}/select_case.tokens
            -P ${_VIPER_BASIC_LEX_DIR}/check_tokens.cmake)

    viper_add_ctest(basic_lex_ident_with_underscore
            ${CMAKE_COMMAND}
            -DBASIC_LEX_DUMP=${_VIPER_BASIC_LEX_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_LEX_DIR}/ident_with_underscore.bas
            -DGOLDEN=${_VIPER_BASIC_LEX_DIR}/ident_with_underscore.expect
            -P ${_VIPER_BASIC_LEX_DIR}/check_tokens.cmake)

    viper_add_ctest(basic_lex_ident_invalid_suffix
            ${CMAKE_COMMAND}
            -DBASIC_LEX_DUMP=${_VIPER_BASIC_LEX_DUMP}
            -DBAS_FILE=${_VIPER_BASIC_LEX_DIR}/ident_invalid_suffix.bas
            -DGOLDEN=${_VIPER_BASIC_LEX_DIR}/ident_invalid_suffix.expect
            -P ${_VIPER_BASIC_LEX_DIR}/check_tokens.cmake)
endfunction()

function(viper_add_basic_to_il_golden_tests)
    if (TEST basic_to_il_ex1)
        return()
    endif ()

    set(_VIPER_BASIC_ILC $<TARGET_FILE:ilc>)
    set(_VIPER_BASIC_TO_IL_DIR ${VIPER_TESTS_DIR}/golden/basic_to_il)
    set(_VIPER_BASIC_TO_IL_CHECK ${_VIPER_BASIC_TO_IL_DIR}/check_il.cmake)
    set(_VIPER_BASIC_TO_IL_CHECK_BOUNDS ${_VIPER_BASIC_TO_IL_DIR}/check_il_bounds.cmake)
    set(_VIPER_GOLDEN_SRC_DIR ${VIPER_TESTS_DIR}/golden/src)
    set(_VIPER_GOLDEN_IL_DIR ${VIPER_TESTS_DIR}/golden/il)
    set(_VIPER_BASIC_GOLDENS_DIR ${VIPER_TESTS_DIR}/golden/basic_lowering)
    set(_VIPER_EH_LOWERING_DIR ${VIPER_TESTS_DIR}/golden/eh_lowering)

    viper_add_ctest(basic_to_il_ex1
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex1_hello_cond.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/ex1_hello_cond.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_ex2
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex2_sum_1_to_10.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/ex2_sum_1_to_10.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_ex3
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex3_for_table.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/ex3_for_table.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_ex4
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex4_if_elseif.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/ex4_if_elseif.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_ex_elseif
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_elseif.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/ex_elseif.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_ex6
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex6_array_sum.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/ex6_array_sum.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_array_dim_redim
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/array_dim_redim.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/array_dim_redim.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_array_dim_runtime
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/array_dim_runtime.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/array_dim_runtime.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_ubound
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/ubound.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/ubound.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_not
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_not.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/not.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_print_commas
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_print_commas.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/print_commas.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_print_semicolons
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_print_semicolons.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/print_semicolons.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_print_newline_control
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_print_newline_control.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/print_newline_control.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_colon
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/ex_colon.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/ex_colon.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_loc
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/loc_add.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/loc_add.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_strings
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/strings.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/strings.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    # Ensure STRING arrays lower to the rt_arr_str_* family only (no i32 helpers)
    viper_add_ctest(basic_to_il_string_arrays_contains
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/string_array_contains.bas
            "-DMUST_HAVE=rt_arr_str_alloc;rt_arr_str_len;rt_arr_str_put;rt_arr_str_get"
            "-DMUST_NOT_HAVE=rt_arr_i32_"
            -P ${_VIPER_BASIC_TO_IL_DIR}/check_il_contains.cmake)

    viper_add_ctest(basic_to_il_string_cmp
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/string_cmp.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/string_cmp.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_boolean_literals
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_SRC_DIR}/boolean_literals.bas
            -DGOLDEN=${_VIPER_GOLDEN_IL_DIR}/boolean_literals.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_boolean_dim_and_assign
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_SRC_DIR}/boolean_dim_and_assign.bas
            -DGOLDEN=${_VIPER_GOLDEN_IL_DIR}/boolean_dim_and_assign.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_boolean_branch_join_skeleton
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_SRC_DIR}/boolean_branch_join_skeleton.bas
            -DGOLDEN=${_VIPER_GOLDEN_IL_DIR}/boolean_branch_join_skeleton.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_boolean_not
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_SRC_DIR}/boolean_not.bas
            -DGOLDEN=${_VIPER_GOLDEN_IL_DIR}/boolean_not.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_boolean_and
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_SRC_DIR}/boolean_and.bas
            -DGOLDEN=${_VIPER_GOLDEN_IL_DIR}/boolean_and.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_boolean_or
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_SRC_DIR}/boolean_or.bas
            -DGOLDEN=${_VIPER_GOLDEN_IL_DIR}/boolean_or.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_boolean_andalso
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_SRC_DIR}/boolean_andalso.bas
            -DGOLDEN=${_VIPER_GOLDEN_IL_DIR}/boolean_andalso.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_boolean_orelse
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_GOLDEN_SRC_DIR}/boolean_orelse.bas
            -DGOLDEN=${_VIPER_GOLDEN_IL_DIR}/boolean_orelse.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_conversions
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/conversions.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/conversions.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_float_ops
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/float_ops.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/float_ops.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_math_phase1
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/math_phase1.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/math_phase1.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_math_phase2
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/math_phase2.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/math_phase2.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_abs_mixed
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/abs_mixed.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/abs_mixed.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_bounds_checks
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/bounds_check.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/bounds_check.il
            -P ${_VIPER_BASIC_TO_IL_CHECK_BOUNDS})

    viper_add_ctest(basic_to_il_randomize
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/randomize.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/randomize.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_rnd
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/rnd.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/rnd.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_lower_funcdef_only
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/lower_funcdef_only.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/lower_funcdef_only.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_calls_lowering
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/calls_lowering.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/calls_lowering.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_instr
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_TO_IL_DIR}/instr.bas
            -DGOLDEN=${_VIPER_BASIC_TO_IL_DIR}/instr.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_len_mid
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_GOLDENS_DIR}/LenMid.bas
            -DGOLDEN=${_VIPER_BASIC_GOLDENS_DIR}/LenMid.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_instr_dispatch
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_GOLDENS_DIR}/Instr.bas
            -DGOLDEN=${_VIPER_BASIC_GOLDENS_DIR}/Instr.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_labels_proc_loops
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_GOLDENS_DIR}/labels_proc_loops.bas
            -DGOLDEN=${_VIPER_BASIC_GOLDENS_DIR}/labels_proc_loops.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_suffix_free_vars
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_GOLDENS_DIR}/SuffixFreeVars.bas
            -DGOLDEN=${_VIPER_BASIC_GOLDENS_DIR}/SuffixFreeVars.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_lower_program_with_proc
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_GOLDENS_DIR}/LowerProgramWithProc.bas
            -DGOLDEN=${_VIPER_BASIC_GOLDENS_DIR}/LowerProgramWithProc.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_on_error_push_pop
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_EH_LOWERING_DIR}/on_error_push_pop.bas
            -DGOLDEN=${_VIPER_EH_LOWERING_DIR}/on_error_push_pop.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_resume_forms
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_EH_LOWERING_DIR}/resume_forms.bas
            -DGOLDEN=${_VIPER_EH_LOWERING_DIR}/resume_forms.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})

    viper_add_ctest(basic_to_il_return_nest
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_GOLDENS_DIR}/ReturnNest.bas
            -DGOLDEN=${_VIPER_BASIC_GOLDENS_DIR}/ReturnNest.il
            -P ${_VIPER_BASIC_TO_IL_CHECK})
endfunction()

function(viper_add_basic_strings_test)
    if (TEST basic_strings)
        return()
    endif ()

    viper_add_ctest(basic_strings ${CMAKE_SOURCE_DIR}/scripts/test_strings.sh)
    set_tests_properties(basic_strings PROPERTIES LABELS BasicStrings)
endfunction()

function(viper_add_basic_semantics_golden_tests)
    if (TEST basic_semantics_proc_registration)
        return()
    endif ()

    set(_VIPER_BASIC_ILC $<TARGET_FILE:ilc>)
    set(_VIPER_BASIC_SEMANTICS_DIR ${VIPER_TESTS_DIR}/basic/semantics)
    set(_VIPER_BASIC_NEGATIVES_DIR ${VIPER_TESTS_DIR}/basic/negatives)
    set(_VIPER_GOLDEN_ERRORS_DIR ${VIPER_TESTS_DIR}/golden/basic_errors)

    file(READ ${_VIPER_BASIC_SEMANTICS_DIR}/proc_registration_ok.err EXPECT_PROC_REG)
    string(STRIP "${EXPECT_PROC_REG}" EXPECT_PROC_REG)
    viper_add_ctest(basic_semantics_proc_registration
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/proc_registration_ok.bas
            "-DEXPECT=${EXPECT_PROC_REG}"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)

    file(READ ${_VIPER_BASIC_SEMANTICS_DIR}/missing_return.err EXPECT_MISSING_RET)
    string(STRIP "${EXPECT_MISSING_RET}" EXPECT_MISSING_RET)
    viper_add_ctest(basic_semantics_missing_return
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/missing_return.bas
            "-DEXPECT=${EXPECT_MISSING_RET}"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)

    file(READ ${_VIPER_BASIC_SEMANTICS_DIR}/sub_in_expr.err EXPECT_SUB_IN_EXPR)
    string(STRIP "${EXPECT_SUB_IN_EXPR}" EXPECT_SUB_IN_EXPR)
    viper_add_ctest(basic_semantics_sub_in_expr
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/sub_in_expr.bas
            "-DEXPECT=${EXPECT_SUB_IN_EXPR}"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)

    viper_add_ctest(basic_semantics_funcs_then_code
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/FuncsThenCode.ok.bas
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_ok.cmake)

    viper_add_ctest(basic_semantics_boolean_type_ok
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_boolean_type_ok.bas
            -DDIAG_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_boolean_type_ok.diag
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_diag.cmake)

    viper_add_ctest(basic_semantics_condition_boolean_ok
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_condition_boolean_ok.bas
            -DDIAG_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_condition_boolean_ok.diag
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_diag.cmake)

    viper_add_ctest(basic_semantics_condition_nonboolean_errors
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_condition_nonboolean_errors.bas
            -DDIAG_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_condition_nonboolean_errors.diag
            -DEXPECT_STATUS=1
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_diag.cmake)

    viper_add_ctest(basic_semantics_color_locate_type_errors
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_color_locate_type_errors.bas
            -DDIAG_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_color_locate_type_errors.diag
            -DEXPECT_STATUS=1
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_diag.cmake)

    viper_add_ctest(basic_negatives_if_int_condition
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_NEGATIVES_DIR}/test_if_int_condition.bas
            -DDIAG_FILE=${_VIPER_BASIC_NEGATIVES_DIR}/test_if_int_condition.diag
            -DEXPECT_STATUS=0
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_diag.cmake)

    viper_add_ctest(basic_negatives_while_float_condition
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_NEGATIVES_DIR}/test_while_float_condition.bas
            -DDIAG_FILE=${_VIPER_BASIC_NEGATIVES_DIR}/test_while_float_condition.diag
            -DEXPECT_STATUS=1
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_diag.cmake)

    viper_add_ctest(basic_semantics_boolean_constfold
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_boolean_constfold.bas
            -DEXPECT_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_boolean_constfold.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_semantics_ident_with_underscore
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/ident_with_underscore.bas
            -DEXPECT_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/ident_with_underscore.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_semantics_unary_numeric
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_unary_numeric.bas
            -DEXPECT_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/test_unary_numeric.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_semantics_arith_mix
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/arith_mix.bas
            -DEXPECT_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/arith_mix.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_semantics_divide_by_zero
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/divide_by_zero.bas
            -DDIAG_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/divide_by_zero.diag
            -DEXPECT_STATUS=1
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_diag.cmake)

    set(_VIPER_UNKNOWN_STMT_REGEX "unknown statement 'FROB'")
    viper_add_ctest(basic_semantics_unknown_statement
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/unknown_statement.bas
            "-DEXPECT=${_VIPER_UNKNOWN_STMT_REGEX}"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)

    viper_add_ctest(basic_semantics_gosub_ok
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/gosub_ok.bas
            -DEXPECT_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/gosub_ok.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_semantics_gosub_empty_return
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/gosub_empty_return.bas
            "-DEXPECT=gosub: empty return stack"
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_run_error.cmake)

    viper_add_ctest(basic_semantics_input_single_diag
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/input_single_diag.bas
            -DSTDIN_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/input_single_diag.stdin
            "-DEXPECT=INPUT: expected 2 values, got 1"
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_run_error.cmake)

    viper_add_ctest(basic_semantics_input_multi_ok
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/input_single_diag.bas
            -DSTDIN_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/input_multi_ok.stdin
            -DEXPECT_FILE=${_VIPER_BASIC_SEMANTICS_DIR}/input_multi_ok.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)
endfunction()

function(viper_add_basic_proc_tests)
    if (TEST basic_proc_forward_calls)
        return()
    endif ()

    set(_VIPER_BASIC_ILC $<TARGET_FILE:ilc>)
    set(_VIPER_BASIC_PROCS_DIR ${VIPER_TESTS_DIR}/basic/procs)
    set(_VIPER_BASIC_SEMANTICS_DIR ${VIPER_TESTS_DIR}/basic/semantics)

    viper_add_ctest(basic_proc_forward_calls
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_PROCS_DIR}/forward_calls.bas
            -DEXPECT_FILE=${_VIPER_BASIC_PROCS_DIR}/forward_calls.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)
endfunction()

function(viper_add_basic_call_tests)
    if (TEST basic_call_stmt_sub)
        return()
    endif ()

    set(_VIPER_BASIC_ILC $<TARGET_FILE:ilc>)
    set(_VIPER_BASIC_CALLS_DIR ${VIPER_TESTS_DIR}/basic/calls)
    set(_VIPER_BASIC_SEMANTICS_DIR ${VIPER_TESTS_DIR}/basic/semantics)
    set(_VIPER_GOLDEN_ERRORS_DIR ${VIPER_TESTS_DIR}/golden/basic_errors)

    viper_add_ctest(basic_call_stmt_sub
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_CALLS_DIR}/call_stmt_sub.bas
            -DEXPECT_FILE=${_VIPER_BASIC_CALLS_DIR}/call_stmt_sub.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_call_stmt_sub_with_builtin
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_CALLS_DIR}/call_stmt_sub_with_builtin.bas
            -DEXPECT_FILE=${_VIPER_BASIC_CALLS_DIR}/call_stmt_sub_with_builtin.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    file(READ ${_VIPER_BASIC_CALLS_DIR}/bare_call_error.err EXPECT_BARE_CALL_ERROR)
    string(STRIP "${EXPECT_BARE_CALL_ERROR}" EXPECT_BARE_CALL_ERROR)

    viper_add_ctest(basic_call_bare_call_error
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_CALLS_DIR}/bare_call_error.bas
            "-DEXPECT=${EXPECT_BARE_CALL_ERROR}"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)
endfunction()

function(viper_add_basic_integration_tests)
    if (TEST basic_boolean_andalso_short)
        return()
    endif ()

    set(_VIPER_BASIC_ILC $<TARGET_FILE:ilc>)
    set(_VIPER_BASIC_INTEGRATION_DIR ${VIPER_TESTS_DIR}/integration)
    set(_VIPER_BASIC_SEMANTICS_DIR ${VIPER_TESTS_DIR}/basic/semantics)
    set(_VIPER_GOLDEN_ERRORS_DIR ${VIPER_TESTS_DIR}/golden/basic_errors)

    viper_add_ctest(basic_boolean_andalso_short
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_INTEGRATION_DIR}/basic_boolean_andalso_short.bas
            -DEXPECT_FILE=${_VIPER_BASIC_INTEGRATION_DIR}/basic_boolean_andalso_short.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_boolean_truth
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_INTEGRATION_DIR}/basic_boolean_truth.bas
            -DEXPECT_FILE=${_VIPER_BASIC_INTEGRATION_DIR}/basic_boolean_truth.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_boolean_shortcircuit
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_INTEGRATION_DIR}/basic_shortcircuit.bas
            -DEXPECT_FILE=${_VIPER_BASIC_INTEGRATION_DIR}/basic_shortcircuit.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_gosub_label_only
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/gosub_label_only.bas
            -DEXPECT_FILE=${CMAKE_SOURCE_DIR}/tests/basic/gosub_label_only.out
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    # Regression: module-level globals visible to SUB/FUNCTION (BUG-030)
    viper_add_ctest(basic_globals_sub_mod
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/semantics/globals_sub_mod.bas
            -DEXPECT_FILE=${CMAKE_SOURCE_DIR}/tests/basic/semantics/globals_sub_mod.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_regress_fileio_write_ok
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/regress_fileio_write_ok.bas
            -DEXPECT_FILE=${CMAKE_SOURCE_DIR}/tests/basic/regress_fileio_write_ok.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_regress_me_keyword
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/regress_me_keyword.bas
            -DEXPECT_FILE=${CMAKE_SOURCE_DIR}/tests/basic/regress_me_keyword.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    viper_add_ctest(basic_select_case_relop
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/select_case_relop.bas
            -DEXPECT_FILE=${CMAKE_SOURCE_DIR}/tests/basic/select_case_relop.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    file(READ ${CMAKE_SOURCE_DIR}/tests/basic/select_case_mixed_types.err EXPECT_SELECT_CASE_MIXED_TYPES)
    string(STRIP "${EXPECT_SELECT_CASE_MIXED_TYPES}" EXPECT_SELECT_CASE_MIXED_TYPES)

    viper_add_ctest(basic_select_case_mixed_types
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/select_case_mixed_types.bas
            "-DEXPECT=${EXPECT_SELECT_CASE_MIXED_TYPES}"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)

    file(READ ${CMAKE_SOURCE_DIR}/tests/basic/select_case_double_else.err EXPECT_SELECT_CASE_DOUBLE_ELSE)
    string(STRIP "${EXPECT_SELECT_CASE_DOUBLE_ELSE}" EXPECT_SELECT_CASE_DOUBLE_ELSE)

    viper_add_ctest(basic_select_case_double_else
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/tests/basic/select_case_double_else.bas
            "-DEXPECT=${EXPECT_SELECT_CASE_DOUBLE_ELSE}"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)
endfunction()

function(viper_add_basic_namespace_tests)
    if (TEST basic_ns_t01_flatten)
        return()
    endif ()

    set(_VIPER_BASIC_ILC $<TARGET_FILE:ilc>)
    set(_VIPER_BASIC_NS_DIR ${VIPER_TESTS_DIR}/basic/namespaces)
    set(_VIPER_BASIC_SEMANTICS_DIR ${VIPER_TESTS_DIR}/basic/semantics)
    set(_VIPER_GOLDEN_ERRORS_DIR ${VIPER_TESTS_DIR}/golden/basic_errors)

    # t01: Flattened namespace call
    viper_add_ctest(basic_ns_t01_flatten
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_NS_DIR}/t01_flatten.bas
            -DEXPECT_FILE=${_VIPER_BASIC_NS_DIR}/t01_flatten.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    # t02: Nested namespace call
    viper_add_ctest(basic_ns_t02_nested
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_NS_DIR}/t02_nested.bas
            -DEXPECT_FILE=${_VIPER_BASIC_NS_DIR}/t02_nested.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    # t03: Parent-walk resolution within nested namespace
    viper_add_ctest(basic_ns_t03_parent_walk
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_NS_DIR}/t03_parent_walk.bas
            -DEXPECT_FILE=${_VIPER_BASIC_NS_DIR}/t03_parent_walk.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    # t04_1: Cross-file defines only (no output)
    viper_add_ctest(basic_ns_t04_cross_file_1
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_NS_DIR}/t04_cross_file_1.bas
            -DEXPECT_FILE=${_VIPER_BASIC_NS_DIR}/t04_cross_file_1.expect
            -P ${_VIPER_BASIC_SEMANTICS_DIR}/check_expect.cmake)

    # t04_2: Cross-file usage without imports should fail with unknown procedure 'lib.ping'
    viper_add_ctest(basic_ns_t04_cross_file_2_fail
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_NS_DIR}/t04_cross_file_2.bas
            "-DEXPECT=unknown procedure 'lib\\.ping'"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)

    # t05: Unqualified unknown with attempted candidates listed
    viper_add_ctest(basic_ns_t05_unknown_parent_walk
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_VIPER_BASIC_NS_DIR}/t05_unknown_parent_walk.bas
            "-DEXPECT=unknown procedure 'f' \\(tried: a\\.b\\.f, a\\.f, f\\)"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)

    # Phase 2 tests ----------------------------------------------------------
    set(_P2_DIR ${VIPER_TESTS_DIR}/basic/namespaces_phase2)
    set(_SEM_DIR ${VIPER_TESTS_DIR}/basic/semantics)

    viper_add_ctest(basic_ns2_t01_import_proc
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_P2_DIR}/t01_using_import_proc.bas
            -DEXPECT_FILE=${_P2_DIR}/t01_using_import_proc.expect
            -P ${_SEM_DIR}/check_expect.cmake)

    viper_add_ctest(basic_ns2_t02_alias_qualified_call
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_P2_DIR}/t02_using_alias_qualified_call.bas
            -DEXPECT_FILE=${_P2_DIR}/t02_using_alias_qualified_call.expect
            -P ${_SEM_DIR}/check_expect.cmake)

    viper_add_ctest(basic_ns2_t03_shadowing_local_vs_import
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_P2_DIR}/t03_shadowing_local_vs_import.bas
            -DEXPECT_FILE=${_P2_DIR}/t03_shadowing_local_vs_import.expect
            -P ${_SEM_DIR}/check_expect.cmake)

    viper_add_ctest(basic_ns2_t04_ambiguity_imports
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_P2_DIR}/t04_ambiguous_imports.bas
            "-DEXPECT=B2009"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)

    viper_add_ctest(basic_ns2_t05_types_with_imports
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_P2_DIR}/t05_types_with_imports.bas
            -DEXPECT_FILE=${_P2_DIR}/t05_types_with_imports.expect
            -P ${_SEM_DIR}/check_expect.cmake)

    viper_add_ctest(basic_ns2_t06_alias_does_not_open
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_P2_DIR}/t06_alias_does_not_open.bas
            "-DEXPECT=B1006"
            -P ${_VIPER_GOLDEN_ERRORS_DIR}/check_error.cmake)

    viper_add_ctest(basic_ns2_t07_case_insensitive
            ${CMAKE_COMMAND}
            -DILC=${_VIPER_BASIC_ILC}
            -DBAS_FILE=${_P2_DIR}/t07_case_insensitive.bas
            -DEXPECT_FILE=${_P2_DIR}/t07_case_insensitive.expect
            -P ${_SEM_DIR}/check_expect.cmake)
endfunction()

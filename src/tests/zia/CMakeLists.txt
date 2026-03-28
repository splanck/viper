# SPDX-License-Identifier: GPL-3.0-only
# File: tests/zia/CMakeLists.txt
# Purpose: Zia frontend unit tests.

set(VIPER_ZIA_LIBS fe_zia)
set(VIPER_ZIA_LIBS ${VIPER_ZIA_LIBS} PARENT_SCOPE)

function(viper_add_zia_tests)
    if (TARGET test_zia_basic)
        return()
    endif ()

    set(_VIPER_ZIA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zia)

    # Basic compilation tests
    viper_add_test(test_zia_basic ${_VIPER_ZIA_DIR}/test_zia_basic.cpp)
    target_link_libraries(test_zia_basic PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_basic test_zia_basic)

    # Control flow tests (if, while, for)
    viper_add_test(test_zia_control_flow ${_VIPER_ZIA_DIR}/test_zia_control_flow.cpp)
    target_link_libraries(test_zia_control_flow PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_control_flow test_zia_control_flow)

    # Type tests (value types, entity types)
    viper_add_test(test_zia_types ${_VIPER_ZIA_DIR}/test_zia_types.cpp)
    target_link_libraries(test_zia_types PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_types test_zia_types)

    # Enum tests (declarations, type checking, lowering)
    viper_add_test(test_zia_enums ${_VIPER_ZIA_DIR}/test_zia_enums.cpp)
    target_link_libraries(test_zia_enums PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_enums test_zia_enums)

    # Async surface tests
    viper_add_test(test_zia_async ${_VIPER_ZIA_DIR}/test_zia_async.cpp)
    target_link_libraries(test_zia_async PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_async test_zia_async)

    # String instance-method dispatch tests
    viper_add_test(test_zia_string_instance_methods
            ${_VIPER_ZIA_DIR}/test_zia_string_instance_methods.cpp)
    target_link_libraries(test_zia_string_instance_methods PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_string_instance_methods test_zia_string_instance_methods)

    # Destructor bind-resolution tests
    viper_add_test(test_zia_destructor_bindings
            ${_VIPER_ZIA_DIR}/test_zia_destructor_bindings.cpp)
    target_link_libraries(test_zia_destructor_bindings PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_destructor_bindings test_zia_destructor_bindings)

    # Expression tests (arithmetic, operators)
    viper_add_test(test_zia_expressions ${_VIPER_ZIA_DIR}/test_zia_expressions.cpp)
    target_link_libraries(test_zia_expressions PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_expressions test_zia_expressions)

    # Collection tests (Map, List)
    viper_add_test(test_zia_collections ${_VIPER_ZIA_DIR}/test_zia_collections.cpp)
    target_link_libraries(test_zia_collections PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_collections test_zia_collections)

    # Optional type tests
    viper_add_test(test_zia_optional ${_VIPER_ZIA_DIR}/test_zia_optional.cpp)
    target_link_libraries(test_zia_optional PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_optional test_zia_optional)

    # Visibility tests
    viper_add_test(test_zia_visibility ${_VIPER_ZIA_DIR}/test_zia_visibility.cpp)
    target_link_libraries(test_zia_visibility PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_visibility test_zia_visibility)

    # Match expression tests
    viper_add_test(test_zia_match ${_VIPER_ZIA_DIR}/test_zia_match.cpp)
    target_link_libraries(test_zia_match PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_match test_zia_match)

    # Lambda tests
    viper_add_test(test_zia_lambda ${_VIPER_ZIA_DIR}/test_zia_lambda.cpp)
    target_link_libraries(test_zia_lambda PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_lambda test_zia_lambda)

    # Bind resolution tests
    viper_add_test(test_zia_binds ${_VIPER_ZIA_DIR}/test_zia_binds.cpp)
    target_link_libraries(test_zia_binds PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_binds test_zia_binds)

    # Bug fix tests (Bugs #38-44)
    viper_add_test(test_zia_bugfixes ${_VIPER_ZIA_DIR}/test_zia_bugfixes.cpp)
    target_link_libraries(test_zia_bugfixes PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_bugfixes test_zia_bugfixes)

    # Literal expression tests
    viper_add_test(test_zia_literals ${_VIPER_ZIA_DIR}/test_zia_literals.cpp)
    target_link_libraries(test_zia_literals PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_literals test_zia_literals)

    # Operator tests
    viper_add_test(test_zia_operators ${_VIPER_ZIA_DIR}/test_zia_operators.cpp)
    target_link_libraries(test_zia_operators PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_operators test_zia_operators)

    # Class/OOP tests
    viper_add_test(test_zia_classes ${_VIPER_ZIA_DIR}/test_zia_classes.cpp)
    target_link_libraries(test_zia_classes PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_classes test_zia_classes)

    # Function tests
    viper_add_test(test_zia_functions ${_VIPER_ZIA_DIR}/test_zia_functions.cpp)
    target_link_libraries(test_zia_functions PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_functions test_zia_functions)

    # Statement tests
    viper_add_test(test_zia_statements ${_VIPER_ZIA_DIR}/test_zia_statements.cpp)
    target_link_libraries(test_zia_statements PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_statements test_zia_statements)

    # Struct type tests
    viper_add_test(test_zia_struct_types ${_VIPER_ZIA_DIR}/test_zia_struct_types.cpp)
    target_link_libraries(test_zia_struct_types PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_struct_types test_zia_struct_types)

    # Namespace tests
    viper_add_test(test_zia_namespaces ${_VIPER_ZIA_DIR}/test_zia_namespaces.cpp)
    target_link_libraries(test_zia_namespaces PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_namespaces test_zia_namespaces)

    # Bug #022 fix: List methods (remove, insert, find)
    viper_add_test(test_zia_list_methods ${_VIPER_ZIA_DIR}/test_zia_list_methods.cpp)
    target_link_libraries(test_zia_list_methods PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_list_methods test_zia_list_methods)

    # Bug #023 fix: Optional field access in helper functions
    viper_add_test(test_zia_optional_field ${_VIPER_ZIA_DIR}/test_zia_optional_field.cpp)
    target_link_libraries(test_zia_optional_field PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_optional_field test_zia_optional_field)

    # List.removeAt() and type checking for List.remove()
    viper_add_test(test_zia_list_remove_at ${_VIPER_ZIA_DIR}/test_zia_list_remove_at.cpp)
    target_link_libraries(test_zia_list_remove_at PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_list_remove_at test_zia_list_remove_at)

    # Bug #003 fix: Optional type narrowing after null check
    viper_add_test(test_zia_optional_narrowing ${_VIPER_ZIA_DIR}/test_zia_optional_narrowing.cpp)
    target_link_libraries(test_zia_optional_narrowing PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_optional_narrowing test_zia_optional_narrowing)

    # Runtime 'new' keyword fixes (A-028, A-029, A-031, A-032, A-033, A-042, A-050)
    viper_add_test(test_zia_rt_new ${_VIPER_ZIA_DIR}/test_zia_rt_new.cpp)
    target_link_libraries(test_zia_rt_new PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_rt_new test_zia_rt_new)

    # Bind standalone function import fixes (A-002, A-003, A-004, A-005)
    viper_add_test(test_zia_bind_fns ${_VIPER_ZIA_DIR}/test_zia_bind_fns.cpp)
    target_link_libraries(test_zia_bind_fns PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_bind_fns test_zia_bind_fns)

    # Static call fixes (A-014, A-019, A-034, A-043, A-052)
    viper_add_test(test_zia_static_calls ${_VIPER_ZIA_DIR}/test_zia_static_calls.cpp)
    target_link_libraries(test_zia_static_calls PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_static_calls test_zia_static_calls)

    # Phase 1 completion API tests (parseAndAnalyze, getGlobalSymbols, etc.)
    viper_add_test(test_zia_completion ${_VIPER_ZIA_DIR}/test_zia_completion.cpp)
    target_link_libraries(test_zia_completion PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_completion test_zia_completion)

    # Phase 2 completion engine tests (CompletionEngine, context extraction, providers)
    viper_add_test(test_zia_completion_engine ${_VIPER_ZIA_DIR}/test_zia_completion_engine.cpp)
    target_link_libraries(test_zia_completion_engine PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_completion_engine test_zia_completion_engine)

    # Recursion depth limit tests
    viper_add_test(test_zia_depth_limits ${_VIPER_ZIA_DIR}/test_zia_depth_limits.cpp)
    target_link_libraries(test_zia_depth_limits PRIVATE ${VIPER_ZIA_LIBS})
    if (WIN32)
        target_link_options(test_zia_depth_limits PRIVATE /STACK:8388608)
    endif ()
    viper_add_ctest(test_zia_depth_limits test_zia_depth_limits)

    # Type system soundness tests
    viper_add_test(test_zia_type_soundness ${_VIPER_ZIA_DIR}/test_zia_type_soundness.cpp)
    target_link_libraries(test_zia_type_soundness PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_type_soundness test_zia_type_soundness)

    # Warning infrastructure tests (W001-W016, policy, suppression)
    viper_add_test(test_zia_warnings ${_VIPER_ZIA_DIR}/test_zia_warnings.cpp)
    target_link_libraries(test_zia_warnings PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_warnings test_zia_warnings)

    # Parser/lexer negative tests (malformed input, error recovery)
    viper_add_test(test_zia_parser_errors ${_VIPER_ZIA_DIR}/test_zia_parser_errors.cpp)
    target_link_libraries(test_zia_parser_errors PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_parser_errors test_zia_parser_errors)

    # Compound assignment operator tests (+=, -=, *=, /=, %=)
    viper_add_test(test_zia_compound_assign ${_VIPER_ZIA_DIR}/test_zia_compound_assign.cpp)
    target_link_libraries(test_zia_compound_assign PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_compound_assign test_zia_compound_assign)

    # `is` type check + set literal lowering tests
    viper_add_test(test_zia_is_and_sets ${_VIPER_ZIA_DIR}/test_zia_is_and_sets.cpp)
    target_link_libraries(test_zia_is_and_sets PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_is_and_sets test_zia_is_and_sets)

    # Default parameter value tests
    viper_add_test(test_zia_defaults ${_VIPER_ZIA_DIR}/test_zia_defaults.cpp)
    target_link_libraries(test_zia_defaults PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_defaults test_zia_defaults)

    # Property (get/set) and static member tests
    viper_add_test(test_zia_properties_static ${_VIPER_ZIA_DIR}/test_zia_properties_static.cpp)
    target_link_libraries(test_zia_properties_static PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_properties_static test_zia_properties_static)

    # Destructor (deinit) tests
    viper_add_test(test_zia_destructors ${_VIPER_ZIA_DIR}/test_zia_destructors.cpp)
    target_link_libraries(test_zia_destructors PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_destructors test_zia_destructors)

    # Try/catch/finally tests
    viper_add_test(test_zia_try_catch ${_VIPER_ZIA_DIR}/test_zia_try_catch.cpp)
    target_link_libraries(test_zia_try_catch PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_try_catch test_zia_try_catch)

    # Interface dynamic dispatch tests
    viper_add_test(test_zia_iface_dispatch ${_VIPER_ZIA_DIR}/test_zia_iface_dispatch.cpp)
    target_link_libraries(test_zia_iface_dispatch PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_iface_dispatch test_zia_iface_dispatch)

    # BUG-ADV-001: Class string field loads must emit rt_str_retain_maybe
    viper_add_test(test_zia_class_string_field ${_VIPER_ZIA_DIR}/test_zia_class_string_field.cpp)
    target_link_libraries(test_zia_class_string_field PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_class_string_field test_zia_class_string_field)

    # REPL adapter tests (input classifier, expression auto-print, var persistence, .type)
    viper_add_test(test_zia_repl ${_VIPER_ZIA_DIR}/test_zia_repl.cpp)
    target_link_libraries(test_zia_repl PRIVATE viper_repl)
    viper_add_ctest(test_zia_repl test_zia_repl)

    # BASIC REPL adapter tests (classifier, adapter, completion)
    viper_add_test(test_basic_repl ${_VIPER_ZIA_DIR}/test_basic_repl.cpp)
    target_link_libraries(test_basic_repl PRIVATE viper_repl)
    viper_add_ctest(test_basic_repl test_basic_repl)

    # Guard-clause null narrowing (Phase 1 Item 7)
    viper_add_test(test_zia_guard_clause_narrowing ${_VIPER_ZIA_DIR}/test_zia_guard_clause_narrowing.cpp)
    target_link_libraries(test_zia_guard_clause_narrowing PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_guard_clause_narrowing test_zia_guard_clause_narrowing)

    # Match exhaustiveness checking (Phase 2 Item 10)
    viper_add_test(test_zia_match_exhaustiveness ${_VIPER_ZIA_DIR}/test_zia_match_exhaustiveness.cpp)
    target_link_libraries(test_zia_match_exhaustiveness PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_match_exhaustiveness test_zia_match_exhaustiveness)

    # Lexer edge-case unit tests (T4: direct Lexer testing)
    viper_add_test(test_zia_lexer ${_VIPER_ZIA_DIR}/test_zia_lexer.cpp)
    target_link_libraries(test_zia_lexer PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_lexer test_zia_lexer)

    # Parser unit tests (Phase 3A: expression precedence, method chains, error recovery)
    set(_VIPER_ZIA_UNIT_DIR ${VIPER_TESTS_DIR}/unit/frontends)
    viper_add_test(test_zia_parser_unit ${_VIPER_ZIA_UNIT_DIR}/test_zia_parser.cpp)
    target_link_libraries(test_zia_parser_unit PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_parser_unit test_zia_parser_unit)

    # Sema unit tests (Phase 3B: type inference, method resolution, error diagnostics)
    viper_add_test(test_zia_sema_unit ${_VIPER_ZIA_UNIT_DIR}/test_zia_sema.cpp)
    target_link_libraries(test_zia_sema_unit PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_sema_unit test_zia_sema_unit)

    # Lowerer unit tests (Phase 3C: IL generation, verifier integration)
    viper_add_test(test_zia_lowerer_unit ${_VIPER_ZIA_UNIT_DIR}/test_zia_lowerer.cpp)
    target_link_libraries(test_zia_lowerer_unit PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_lowerer_unit test_zia_lowerer_unit)
endfunction()

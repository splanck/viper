# SPDX-License-Identifier: GPL-3.0-only
# File: tests/zia/CMakeLists.txt
# Purpose: Zia frontend unit tests.

set(ZANNA_ZIA_LIBS fe_zia)
set(ZANNA_ZIA_LIBS ${ZANNA_ZIA_LIBS} PARENT_SCOPE)

function(zanna_add_zia_tests)
    if (TARGET test_zia_basic)
        return()
    endif ()

    set(_ZANNA_ZIA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zia)

    # Basic compilation tests
    zanna_add_test(test_zia_basic ${_ZANNA_ZIA_DIR}/test_zia_basic.cpp)
    target_link_libraries(test_zia_basic PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_basic test_zia_basic)

    # Control flow tests (if, while, for)
    zanna_add_test(test_zia_control_flow ${_ZANNA_ZIA_DIR}/test_zia_control_flow.cpp)
    target_link_libraries(test_zia_control_flow PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_control_flow test_zia_control_flow)

    # Type tests (value types, entity types)
    zanna_add_test(test_zia_types ${_ZANNA_ZIA_DIR}/test_zia_types.cpp)
    target_link_libraries(test_zia_types PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_types test_zia_types)

    # Enum tests (declarations, type checking, lowering)
    zanna_add_test(test_zia_enums ${_ZANNA_ZIA_DIR}/test_zia_enums.cpp)
    target_link_libraries(test_zia_enums PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_enums test_zia_enums)

    # Async surface tests
    zanna_add_test(test_zia_async ${_ZANNA_ZIA_DIR}/test_zia_async.cpp)
    target_link_libraries(test_zia_async PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_async test_zia_async)

    # String instance-method dispatch tests
    zanna_add_test(test_zia_string_instance_methods
            ${_ZANNA_ZIA_DIR}/test_zia_string_instance_methods.cpp)
    target_link_libraries(test_zia_string_instance_methods PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_string_instance_methods test_zia_string_instance_methods)

    # Destructor bind-resolution tests
    zanna_add_test(test_zia_destructor_bindings
            ${_ZANNA_ZIA_DIR}/test_zia_destructor_bindings.cpp)
    target_link_libraries(test_zia_destructor_bindings PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_destructor_bindings test_zia_destructor_bindings)

    # Expression tests (arithmetic, operators)
    zanna_add_test(test_zia_expressions ${_ZANNA_ZIA_DIR}/test_zia_expressions.cpp)
    target_link_libraries(test_zia_expressions PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_expressions test_zia_expressions)

    # Collection tests (Map, List)
    zanna_add_test(test_zia_collections ${_ZANNA_ZIA_DIR}/test_zia_collections.cpp)
    target_link_libraries(test_zia_collections PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_collections test_zia_collections)

    # Optional type tests
    zanna_add_test(test_zia_optional ${_ZANNA_ZIA_DIR}/test_zia_optional.cpp)
    target_link_libraries(test_zia_optional PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_optional test_zia_optional)

    # Visibility tests
    zanna_add_test(test_zia_visibility ${_ZANNA_ZIA_DIR}/test_zia_visibility.cpp)
    target_link_libraries(test_zia_visibility PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_visibility test_zia_visibility)

    # Match expression tests
    zanna_add_test(test_zia_match ${_ZANNA_ZIA_DIR}/test_zia_match.cpp)
    target_link_libraries(test_zia_match PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_match test_zia_match)

    # Lambda tests
    zanna_add_test(test_zia_lambda ${_ZANNA_ZIA_DIR}/test_zia_lambda.cpp)
    target_link_libraries(test_zia_lambda PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_lambda test_zia_lambda)

    # Bind resolution tests
    zanna_add_test(test_zia_binds ${_ZANNA_ZIA_DIR}/test_zia_binds.cpp)
    target_link_libraries(test_zia_binds PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_binds test_zia_binds)

    # Bug fix tests (Bugs #38-44)
    zanna_add_test(test_zia_bugfixes ${_ZANNA_ZIA_DIR}/test_zia_bugfixes.cpp)
    target_link_libraries(test_zia_bugfixes PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_bugfixes test_zia_bugfixes)

    # Literal expression tests
    zanna_add_test(test_zia_literals ${_ZANNA_ZIA_DIR}/test_zia_literals.cpp)
    target_link_libraries(test_zia_literals PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_literals test_zia_literals)

    # Operator tests
    zanna_add_test(test_zia_operators ${_ZANNA_ZIA_DIR}/test_zia_operators.cpp)
    target_link_libraries(test_zia_operators PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_operators test_zia_operators)

    # Class/OOP tests
    zanna_add_test(test_zia_classes ${_ZANNA_ZIA_DIR}/test_zia_classes.cpp)
    target_link_libraries(test_zia_classes PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_classes test_zia_classes)

    # Function tests
    zanna_add_test(test_zia_functions ${_ZANNA_ZIA_DIR}/test_zia_functions.cpp)
    target_link_libraries(test_zia_functions PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_functions test_zia_functions)

    # Constrained generic tests
    zanna_add_test(test_zia_constrained_generics
            ${_ZANNA_ZIA_DIR}/test_zia_constrained_generics.cpp)
    target_link_libraries(test_zia_constrained_generics PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_constrained_generics test_zia_constrained_generics)

    # Statement tests
    zanna_add_test(test_zia_statements ${_ZANNA_ZIA_DIR}/test_zia_statements.cpp)
    target_link_libraries(test_zia_statements PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_statements test_zia_statements)

    # Struct type tests
    zanna_add_test(test_zia_struct_types ${_ZANNA_ZIA_DIR}/test_zia_struct_types.cpp)
    target_link_libraries(test_zia_struct_types PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_struct_types test_zia_struct_types)

    # Namespace tests
    zanna_add_test(test_zia_namespaces ${_ZANNA_ZIA_DIR}/test_zia_namespaces.cpp)
    target_link_libraries(test_zia_namespaces PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_namespaces test_zia_namespaces)

    # Bug #022 fix: List methods (remove, insert, find)
    zanna_add_test(test_zia_list_methods ${_ZANNA_ZIA_DIR}/test_zia_list_methods.cpp)
    target_link_libraries(test_zia_list_methods PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_list_methods test_zia_list_methods)

    # Bug #023 fix: Optional field access in helper functions
    zanna_add_test(test_zia_optional_field ${_ZANNA_ZIA_DIR}/test_zia_optional_field.cpp)
    target_link_libraries(test_zia_optional_field PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_optional_field test_zia_optional_field)

    # List.removeAt() and type checking for List.remove()
    zanna_add_test(test_zia_list_remove_at ${_ZANNA_ZIA_DIR}/test_zia_list_remove_at.cpp)
    target_link_libraries(test_zia_list_remove_at PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_list_remove_at test_zia_list_remove_at)

    # Bug #003 fix: Optional type narrowing after null check
    zanna_add_test(test_zia_optional_narrowing ${_ZANNA_ZIA_DIR}/test_zia_optional_narrowing.cpp)
    target_link_libraries(test_zia_optional_narrowing PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_optional_narrowing test_zia_optional_narrowing)

    # Runtime 'new' keyword fixes (A-028, A-029, A-031, A-032, A-033, A-042, A-050)
    zanna_add_test(test_zia_rt_new ${_ZANNA_ZIA_DIR}/test_zia_rt_new.cpp)
    target_link_libraries(test_zia_rt_new PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_rt_new test_zia_rt_new)

    # Bind standalone function import fixes (A-002, A-003, A-004, A-005)
    zanna_add_test(test_zia_bind_fns ${_ZANNA_ZIA_DIR}/test_zia_bind_fns.cpp)
    target_link_libraries(test_zia_bind_fns PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_bind_fns test_zia_bind_fns)

    zanna_add_test(test_zia_http_server ${_ZANNA_ZIA_DIR}/test_zia_http_server.cpp)
    target_link_libraries(test_zia_http_server PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_http_server test_zia_http_server)

    # Static call fixes (A-014, A-019, A-034, A-043, A-052)
    zanna_add_test(test_zia_static_calls ${_ZANNA_ZIA_DIR}/test_zia_static_calls.cpp)
    target_link_libraries(test_zia_static_calls PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_static_calls test_zia_static_calls)

    # Phase 1 completion API tests (parseAndAnalyze, getGlobalSymbols, etc.)
    zanna_add_test(test_zia_completion ${_ZANNA_ZIA_DIR}/test_zia_completion.cpp)
    target_link_libraries(test_zia_completion PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_completion test_zia_completion)

    # Project language index runtime bridge tests (definition/reference/rename)
    zanna_add_test(test_zia_project_index ${_ZANNA_ZIA_DIR}/test_zia_project_index.cpp)
    target_link_libraries(test_zia_project_index PRIVATE zia_editor_services zanna_runtime)
    zanna_add_ctest(test_zia_project_index test_zia_project_index)

    # Phase 2 completion engine tests (CompletionEngine, context extraction, providers)
    zanna_add_test(test_zia_completion_engine ${_ZANNA_ZIA_DIR}/test_zia_completion_engine.cpp)
    target_link_libraries(test_zia_completion_engine PRIVATE zia_editor_services)
    zanna_add_ctest(test_zia_completion_engine test_zia_completion_engine)

    # Document mirror delta validation tests (VDOC-109)
    zanna_add_test(test_zia_doc_mirror ${_ZANNA_ZIA_DIR}/test_zia_doc_mirror.cpp)
    target_link_libraries(test_zia_doc_mirror PRIVATE zia_editor_services)
    zanna_add_ctest(test_zia_doc_mirror test_zia_doc_mirror)

    # Recursion depth limit tests
    zanna_add_test(test_zia_depth_limits ${_ZANNA_ZIA_DIR}/test_zia_depth_limits.cpp)
    target_link_libraries(test_zia_depth_limits PRIVATE ${ZANNA_ZIA_LIBS})
    if (WIN32)
        target_link_options(test_zia_depth_limits PRIVATE /STACK:8388608)
    endif ()
    zanna_add_ctest(test_zia_depth_limits test_zia_depth_limits)

    # Type system soundness tests
    zanna_add_test(test_zia_type_soundness ${_ZANNA_ZIA_DIR}/test_zia_type_soundness.cpp)
    target_link_libraries(test_zia_type_soundness PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_type_soundness test_zia_type_soundness)

    # Safe pointer surface tests (Ptr is not source-visible; Any/function bridges are safe)
    zanna_add_test(test_zia_pointer_safety ${_ZANNA_ZIA_DIR}/test_zia_pointer_safety.cpp)
    target_link_libraries(test_zia_pointer_safety PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_pointer_safety test_zia_pointer_safety)

    # Warning infrastructure tests (W001-W016, policy, suppression)
    zanna_add_test(test_zia_warnings ${_ZANNA_ZIA_DIR}/test_zia_warnings.cpp)
    target_link_libraries(test_zia_warnings PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_warnings test_zia_warnings)

    # Parser/lexer negative tests (malformed input, error recovery)
    zanna_add_test(test_zia_parser_errors ${_ZANNA_ZIA_DIR}/test_zia_parser_errors.cpp)
    target_link_libraries(test_zia_parser_errors PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_parser_errors test_zia_parser_errors)

    # Documentation drift checks for canonical Zia docs and runtime guides
    zanna_add_test(test_zia_docs_consistency ${_ZANNA_ZIA_DIR}/test_zia_docs_consistency.cpp)
    target_link_libraries(test_zia_docs_consistency PRIVATE ${ZANNA_ZIA_LIBS})
    target_compile_definitions(test_zia_docs_consistency PRIVATE ZANNA_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    zanna_add_ctest(test_zia_docs_consistency test_zia_docs_consistency)

    # Compound assignment operator tests (+=, -=, *=, /=, %=)
    zanna_add_test(test_zia_compound_assign ${_ZANNA_ZIA_DIR}/test_zia_compound_assign.cpp)
    target_link_libraries(test_zia_compound_assign PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_compound_assign test_zia_compound_assign)

    # `final` keyword enforcement tests
    zanna_add_test(test_zia_final_enforcement ${_ZANNA_ZIA_DIR}/test_zia_final_enforcement.cpp)
    target_link_libraries(test_zia_final_enforcement PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_final_enforcement test_zia_final_enforcement)

    # `is` type check + set literal lowering tests
    zanna_add_test(test_zia_is_and_sets ${_ZANNA_ZIA_DIR}/test_zia_is_and_sets.cpp)
    target_link_libraries(test_zia_is_and_sets PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_is_and_sets test_zia_is_and_sets)

    # Language hardening regression tests for audited semantics gaps
    zanna_add_test(test_zia_language_hardening ${_ZANNA_ZIA_DIR}/test_zia_language_hardening.cpp)
    target_link_libraries(test_zia_language_hardening PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_language_hardening test_zia_language_hardening)

    # Default parameter value tests
    zanna_add_test(test_zia_defaults ${_ZANNA_ZIA_DIR}/test_zia_defaults.cpp)
    target_link_libraries(test_zia_defaults PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_defaults test_zia_defaults)

    # Property (get/set) and static member tests
    zanna_add_test(test_zia_properties_static ${_ZANNA_ZIA_DIR}/test_zia_properties_static.cpp)
    target_link_libraries(test_zia_properties_static PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_properties_static test_zia_properties_static)

    # Destructor (deinit) tests
    zanna_add_test(test_zia_destructors ${_ZANNA_ZIA_DIR}/test_zia_destructors.cpp)
    target_link_libraries(test_zia_destructors PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_destructors test_zia_destructors)

    # Try/catch/finally tests
    zanna_add_test(test_zia_try_catch ${_ZANNA_ZIA_DIR}/test_zia_try_catch.cpp)
    target_link_libraries(test_zia_try_catch PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_try_catch test_zia_try_catch)

    # Interface dynamic dispatch tests
    zanna_add_test(test_zia_iface_dispatch ${_ZANNA_ZIA_DIR}/test_zia_iface_dispatch.cpp)
    target_link_libraries(test_zia_iface_dispatch PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_iface_dispatch test_zia_iface_dispatch)

    # BUG-ADV-001: Class string field loads must emit rt_str_retain_maybe
    zanna_add_test(test_zia_class_string_field ${_ZANNA_ZIA_DIR}/test_zia_class_string_field.cpp)
    target_link_libraries(test_zia_class_string_field PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_class_string_field test_zia_class_string_field)

    # REPL adapter tests (input classifier, expression auto-print, var persistence, .type)
    zanna_add_test(test_zia_repl ${_ZANNA_ZIA_DIR}/test_zia_repl.cpp)
    target_link_libraries(test_zia_repl PRIVATE zanna_repl)
    zanna_add_ctest(test_zia_repl test_zia_repl)

    # BASIC REPL adapter tests (classifier, adapter, completion)
    zanna_add_test(test_basic_repl ${_ZANNA_ZIA_DIR}/test_basic_repl.cpp)
    target_link_libraries(test_basic_repl PRIVATE zanna_repl)
    zanna_add_ctest(test_basic_repl test_basic_repl)

    # Guard-clause null narrowing (Phase 1 Item 7)
    zanna_add_test(test_zia_guard_clause_narrowing ${_ZANNA_ZIA_DIR}/test_zia_guard_clause_narrowing.cpp)
    target_link_libraries(test_zia_guard_clause_narrowing PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_guard_clause_narrowing test_zia_guard_clause_narrowing)

    # Match exhaustiveness checking (Phase 2 Item 10)
    zanna_add_test(test_zia_match_exhaustiveness ${_ZANNA_ZIA_DIR}/test_zia_match_exhaustiveness.cpp)
    target_link_libraries(test_zia_match_exhaustiveness PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_match_exhaustiveness test_zia_match_exhaustiveness)

    # Lexer edge-case unit tests (T4: direct Lexer testing)
    zanna_add_test(test_zia_lexer ${_ZANNA_ZIA_DIR}/test_zia_lexer.cpp)
    target_link_libraries(test_zia_lexer PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_lexer test_zia_lexer)

    # Parser unit tests (Phase 3A: expression precedence, method chains, error recovery)
    set(_ZANNA_ZIA_UNIT_DIR ${ZANNA_TESTS_DIR}/unit/frontends)
    zanna_add_test(test_zia_parser_unit ${_ZANNA_ZIA_UNIT_DIR}/test_zia_parser.cpp)
    target_link_libraries(test_zia_parser_unit PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_parser_unit test_zia_parser_unit)

    # Sema unit tests (Phase 3B: type inference, method resolution, error diagnostics)
    zanna_add_test(test_zia_sema_unit ${_ZANNA_ZIA_UNIT_DIR}/test_zia_sema.cpp)
    target_link_libraries(test_zia_sema_unit PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_sema_unit test_zia_sema_unit)

    # Lowerer unit tests (Phase 3C: IL generation, verifier integration)
    zanna_add_test(test_zia_lowerer_unit ${_ZANNA_ZIA_UNIT_DIR}/test_zia_lowerer.cpp)
    target_link_libraries(test_zia_lowerer_unit PRIVATE ${ZANNA_ZIA_LIBS})
    zanna_add_ctest(test_zia_lowerer_unit test_zia_lowerer_unit)
endfunction()

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

    # Entity/OOP tests
    viper_add_test(test_zia_entities ${_VIPER_ZIA_DIR}/test_zia_entities.cpp)
    target_link_libraries(test_zia_entities PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_entities test_zia_entities)

    # Function tests
    viper_add_test(test_zia_functions ${_VIPER_ZIA_DIR}/test_zia_functions.cpp)
    target_link_libraries(test_zia_functions PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_functions test_zia_functions)

    # Statement tests
    viper_add_test(test_zia_statements ${_VIPER_ZIA_DIR}/test_zia_statements.cpp)
    target_link_libraries(test_zia_statements PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_statements test_zia_statements)

    # Value type tests
    viper_add_test(test_zia_value_types ${_VIPER_ZIA_DIR}/test_zia_value_types.cpp)
    target_link_libraries(test_zia_value_types PRIVATE ${VIPER_ZIA_LIBS})
    viper_add_ctest(test_zia_value_types test_zia_value_types)

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
endfunction()

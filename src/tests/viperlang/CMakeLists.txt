# SPDX-License-Identifier: GPL-3.0-only
# File: tests/viperlang/CMakeLists.txt
# Purpose: ViperLang frontend unit tests.

set(VIPER_VIPERLANG_LIBS fe_viperlang)
set(VIPER_VIPERLANG_LIBS ${VIPER_VIPERLANG_LIBS} PARENT_SCOPE)

function(viper_add_viperlang_tests)
    if (TARGET test_viperlang_basic)
        return()
    endif ()

    set(_VIPER_VIPERLANG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/viperlang)

    # Basic compilation tests
    viper_add_test(test_viperlang_basic ${_VIPER_VIPERLANG_DIR}/test_viperlang_basic.cpp)
    target_link_libraries(test_viperlang_basic PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_basic test_viperlang_basic)

    # Control flow tests (if, while, for)
    viper_add_test(test_viperlang_control_flow ${_VIPER_VIPERLANG_DIR}/test_viperlang_control_flow.cpp)
    target_link_libraries(test_viperlang_control_flow PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_control_flow test_viperlang_control_flow)

    # Type tests (value types, entity types)
    viper_add_test(test_viperlang_types ${_VIPER_VIPERLANG_DIR}/test_viperlang_types.cpp)
    target_link_libraries(test_viperlang_types PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_types test_viperlang_types)

    # Expression tests (arithmetic, operators)
    viper_add_test(test_viperlang_expressions ${_VIPER_VIPERLANG_DIR}/test_viperlang_expressions.cpp)
    target_link_libraries(test_viperlang_expressions PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_expressions test_viperlang_expressions)

    # Collection tests (Map, List)
    viper_add_test(test_viperlang_collections ${_VIPER_VIPERLANG_DIR}/test_viperlang_collections.cpp)
    target_link_libraries(test_viperlang_collections PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_collections test_viperlang_collections)

    # Optional type tests
    viper_add_test(test_viperlang_optional ${_VIPER_VIPERLANG_DIR}/test_viperlang_optional.cpp)
    target_link_libraries(test_viperlang_optional PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_optional test_viperlang_optional)

    # Visibility tests
    viper_add_test(test_viperlang_visibility ${_VIPER_VIPERLANG_DIR}/test_viperlang_visibility.cpp)
    target_link_libraries(test_viperlang_visibility PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_visibility test_viperlang_visibility)

    # Match expression tests
    viper_add_test(test_viperlang_match ${_VIPER_VIPERLANG_DIR}/test_viperlang_match.cpp)
    target_link_libraries(test_viperlang_match PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_match test_viperlang_match)

    # Lambda tests
    viper_add_test(test_viperlang_lambda ${_VIPER_VIPERLANG_DIR}/test_viperlang_lambda.cpp)
    target_link_libraries(test_viperlang_lambda PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_lambda test_viperlang_lambda)

    # Import resolution tests
    viper_add_test(test_viperlang_imports ${_VIPER_VIPERLANG_DIR}/test_viperlang_imports.cpp)
    target_link_libraries(test_viperlang_imports PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_imports test_viperlang_imports)

    # Bug fix tests (Bugs #38-44)
    viper_add_test(test_viperlang_bugfixes ${_VIPER_VIPERLANG_DIR}/test_viperlang_bugfixes.cpp)
    target_link_libraries(test_viperlang_bugfixes PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_bugfixes test_viperlang_bugfixes)

    # Literal expression tests
    viper_add_test(test_viperlang_literals ${_VIPER_VIPERLANG_DIR}/test_viperlang_literals.cpp)
    target_link_libraries(test_viperlang_literals PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_literals test_viperlang_literals)

    # Operator tests
    viper_add_test(test_viperlang_operators ${_VIPER_VIPERLANG_DIR}/test_viperlang_operators.cpp)
    target_link_libraries(test_viperlang_operators PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_operators test_viperlang_operators)

    # Entity/OOP tests
    viper_add_test(test_viperlang_entities ${_VIPER_VIPERLANG_DIR}/test_viperlang_entities.cpp)
    target_link_libraries(test_viperlang_entities PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_entities test_viperlang_entities)

    # Function tests
    viper_add_test(test_viperlang_functions ${_VIPER_VIPERLANG_DIR}/test_viperlang_functions.cpp)
    target_link_libraries(test_viperlang_functions PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_functions test_viperlang_functions)

    # Statement tests
    viper_add_test(test_viperlang_statements ${_VIPER_VIPERLANG_DIR}/test_viperlang_statements.cpp)
    target_link_libraries(test_viperlang_statements PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_statements test_viperlang_statements)

    # Value type tests
    viper_add_test(test_viperlang_value_types ${_VIPER_VIPERLANG_DIR}/test_viperlang_value_types.cpp)
    target_link_libraries(test_viperlang_value_types PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_value_types test_viperlang_value_types)

    # Namespace tests
    viper_add_test(test_viperlang_namespaces ${_VIPER_VIPERLANG_DIR}/test_viperlang_namespaces.cpp)
    target_link_libraries(test_viperlang_namespaces PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_namespaces test_viperlang_namespaces)

    # Bug #022 fix: List methods (remove, insert, find)
    viper_add_test(test_viperlang_list_methods ${_VIPER_VIPERLANG_DIR}/test_viperlang_list_methods.cpp)
    target_link_libraries(test_viperlang_list_methods PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_list_methods test_viperlang_list_methods)

    # Bug #023 fix: Optional field access in helper functions
    viper_add_test(test_viperlang_optional_field ${_VIPER_VIPERLANG_DIR}/test_viperlang_optional_field.cpp)
    target_link_libraries(test_viperlang_optional_field PRIVATE ${VIPER_VIPERLANG_LIBS})
    viper_add_ctest(test_viperlang_optional_field test_viperlang_optional_field)
endfunction()

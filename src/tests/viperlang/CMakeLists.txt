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
endfunction()

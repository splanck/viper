# SPDX-License-Identifier: GPL-3.0-only
# File: tests/tools/CMakeLists.txt
# Purpose: CLI and tool-specific smoke tests.

set(VIPER_TOOLS_LIBS viper_il_full il_vm il_api il_transform il_tools_common)
set(VIPER_TOOLS_LIBS ${VIPER_TOOLS_LIBS} PARENT_SCOPE)

function(viper_add_tools_tests)
    if (TARGET test_tools_break_parsing)
        return()
    endif ()

    set(_VIPER_TOOLS_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

    viper_add_test(test_tools_break_parsing
            ${_VIPER_TOOLS_DIR}/BreakParsingTests.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
    target_include_directories(test_tools_break_parsing PRIVATE ${CMAKE_SOURCE_DIR}/src)
    viper_add_ctest(test_tools_break_parsing test_tools_break_parsing)

    viper_add_test(test_tools_module_loader
            ${_VIPER_TOOLS_DIR}/ModuleLoaderTests.cpp)
    target_include_directories(test_tools_module_loader PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_tools_module_loader PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_tools_module_loader test_tools_module_loader)

    viper_add_test(test_tools_basic_common
            ${_VIPER_TOOLS_DIR}/BasicCommonTests.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/basic/common.cpp)
    target_include_directories(test_tools_basic_common PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(test_tools_basic_common PRIVATE VIPER_BASIC_TOOL_USAGE="Usage: basic-tool <file.bas>\\n")
    target_link_libraries(test_tools_basic_common PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_tools_basic_common test_tools_basic_common)

    viper_add_test(test_cli_run_missing_main
            ${VIPER_TESTS_DIR}/unit/test_cli_run_missing_main.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
    target_include_directories(test_cli_run_missing_main PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_run_missing_main PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_run_missing_main test_cli_run_missing_main)

    viper_add_test(test_cli_run_invalid_break_line
            ${VIPER_TESTS_DIR}/unit/test_cli_run_invalid_break_line.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
    target_include_directories(test_cli_run_invalid_break_line PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_run_invalid_break_line PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_run_invalid_break_line test_cli_run_invalid_break_line)

    viper_add_test(test_cli_run_break_src_whitespace
            ${VIPER_TESTS_DIR}/unit/test_cli_run_break_src_whitespace.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
    target_include_directories(test_cli_run_break_src_whitespace PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_run_break_src_whitespace PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_run_break_src_whitespace test_cli_run_break_src_whitespace)

    viper_add_test(test_cli_run_break_src_line_padding
            ${VIPER_TESTS_DIR}/unit/test_cli_run_break_src_line_padding.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
    target_include_directories(test_cli_run_break_src_line_padding PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_run_break_src_line_padding PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_run_break_src_line_padding test_cli_run_break_src_line_padding)

    viper_add_test(test_cli_run_break_src_plain
            ${VIPER_TESTS_DIR}/unit/test_cli_run_break_src_plain.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
    target_include_directories(test_cli_run_break_src_plain PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_run_break_src_plain PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_run_break_src_plain test_cli_run_break_src_plain)

    viper_add_test(test_cli_front_basic_verify_diag
            ${VIPER_TESTS_DIR}/unit/test_cli_front_basic_verify_diag.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp)
    target_include_directories(test_cli_front_basic_verify_diag PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_front_basic_verify_diag PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_front_basic_verify_diag test_cli_front_basic_verify_diag)

    viper_add_test(test_cli_front_basic_reject_break
            ${_VIPER_TOOLS_DIR}/FrontBasicRejectBreakFlagTests.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp)
    target_include_directories(test_cli_front_basic_reject_break PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_front_basic_reject_break PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_front_basic_reject_break test_cli_front_basic_reject_break)

    viper_add_test(test_cli_run_invalid_max_steps
            ${VIPER_TESTS_DIR}/unit/test_cli_run_invalid_max_steps.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
    target_include_directories(test_cli_run_invalid_max_steps PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_run_invalid_max_steps PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_run_invalid_max_steps test_cli_run_invalid_max_steps)

    viper_add_test(test_cli_run_return_overflow
            ${VIPER_TESTS_DIR}/unit/test_cli_run_return_overflow.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
    target_include_directories(test_cli_run_return_overflow PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_run_return_overflow PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_run_return_overflow test_cli_run_return_overflow)

    viper_add_test(test_cli_run_bounds_checks_unsupported
            ${VIPER_TESTS_DIR}/unit/test_cli_run_bounds_checks_unsupported.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
    target_include_directories(test_cli_run_bounds_checks_unsupported PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_run_bounds_checks_unsupported PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_run_bounds_checks_unsupported test_cli_run_bounds_checks_unsupported)

    viper_add_test(test_cli_run_source_manager_overflow
            ${VIPER_TESTS_DIR}/unit/test_cli_run_source_manager_overflow.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
    target_include_directories(test_cli_run_source_manager_overflow PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_run_source_manager_overflow PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_run_source_manager_overflow test_cli_run_source_manager_overflow)

    viper_add_test(test_cli_il_verify_source_manager_overflow
            ${VIPER_TESTS_DIR}/unit/test_cli_il_verify_source_manager_overflow.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/il-verify/il-verify.cpp)
    target_include_directories(test_cli_il_verify_source_manager_overflow PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(test_cli_il_verify_source_manager_overflow PRIVATE VIPER_IL_VERIFY_SKIP_MAIN)
    target_link_libraries(test_cli_il_verify_source_manager_overflow PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_cli_il_verify_source_manager_overflow test_cli_il_verify_source_manager_overflow)

    viper_add_test(test_tools_il_verify_overflow_diag_once
            ${_VIPER_TOOLS_DIR}/IlVerifyOverflowDiagOnceTests.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/il-verify/il-verify.cpp)
    target_include_directories(test_tools_il_verify_overflow_diag_once PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(test_tools_il_verify_overflow_diag_once PRIVATE VIPER_IL_VERIFY_SKIP_MAIN)
    target_link_libraries(test_tools_il_verify_overflow_diag_once PRIVATE ${VIPER_TOOLS_LIBS})
    viper_add_ctest(test_tools_il_verify_overflow_diag_once test_tools_il_verify_overflow_diag_once)

    viper_add_test(test_cli_il_opt_passes
            ${VIPER_TESTS_DIR}/unit/test_il_opt_passes.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
            ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_il_opt.cpp)
    target_include_directories(test_cli_il_opt_passes PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_il_opt_passes PRIVATE ${VIPER_TOOLS_LIBS} il_transform)
    viper_add_ctest(test_cli_il_opt_passes test_cli_il_opt_passes)
endfunction()

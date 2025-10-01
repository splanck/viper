# SPDX-License-Identifier: MIT
# File: tests/tools/CMakeLists.txt
# Purpose: CLI and tool-specific smoke tests.

set(VIPER_TOOLS_LIBS il_vm il_api support il_tools_common)
set(VIPER_TOOLS_LIBS ${VIPER_TOOLS_LIBS} PARENT_SCOPE)

function(viper_add_tools_tests)
  if(TARGET test_tools_break_parsing)
    return()
  endif()

  set(_VIPER_TOOLS_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

  viper_add_test_exe(test_tools_break_parsing
    ${_VIPER_TOOLS_DIR}/BreakParsingTests.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
  target_include_directories(test_tools_break_parsing PRIVATE ${CMAKE_SOURCE_DIR}/src)
  viper_add_ctest(test_tools_break_parsing test_tools_break_parsing)

  viper_add_test_exe(test_tools_module_loader
    ${_VIPER_TOOLS_DIR}/ModuleLoaderTests.cpp)
  target_include_directories(test_tools_module_loader PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(test_tools_module_loader PRIVATE ${VIPER_TOOLS_LIBS})
  viper_add_ctest(test_tools_module_loader test_tools_module_loader)

  viper_add_test_exe(test_cli_run_missing_main
    ${VIPER_TESTS_DIR}/unit/test_cli_run_missing_main.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
  target_include_directories(test_cli_run_missing_main PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(test_cli_run_missing_main PRIVATE ${VIPER_TOOLS_LIBS})
  viper_add_ctest(test_cli_run_missing_main test_cli_run_missing_main)

  viper_add_test_exe(test_cli_run_invalid_break_line
    ${VIPER_TESTS_DIR}/unit/test_cli_run_invalid_break_line.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ilc/cli.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ilc/cmd_run_il.cpp
    ${CMAKE_SOURCE_DIR}/src/tools/ilc/break_spec.cpp)
  target_include_directories(test_cli_run_invalid_break_line PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(test_cli_run_invalid_break_line PRIVATE ${VIPER_TOOLS_LIBS})
  viper_add_ctest(test_cli_run_invalid_break_line test_cli_run_invalid_break_line)
endfunction()

# SPDX-License-Identifier: GPL-3.0-only
# File: tests/cmake/TestHelpers.cmake
# Purpose: Helper functions to keep the test CMake files concise.

# Helper function to assign labels based on test name patterns
function(_viper_assign_test_label name)
    if (name MATCHES "^test_basic_" OR name MATCHES "^test_frontends_basic_" OR name MATCHES "^test_lowerer_" OR name MATCHES "^test_type_" OR name MATCHES "^test_builtin_" OR name MATCHES "^test_lowering_")
        set_tests_properties(${name} PROPERTIES LABELS basic)
    elseif (name MATCHES "^test_il_" OR name MATCHES "^il_" OR name MATCHES "^test_analysis_" OR name MATCHES "^test_irbuilder_")
        set_tests_properties(${name} PROPERTIES LABELS il)
    elseif (name MATCHES "^test_vm_" OR name MATCHES "^vm_")
        set_tests_properties(${name} PROPERTIES LABELS vm)
    elseif (name MATCHES "^test_rt_" OR name MATCHES "^test_runtime_" OR name MATCHES "^runtime_" OR name MATCHES "^test_stringbuilder_" OR name MATCHES "^test_console_" OR name MATCHES "^test_convert_" OR name MATCHES "^test_catalog_")
        set_tests_properties(${name} PROPERTIES LABELS runtime)
    elseif (name MATCHES "^test_codegen_" OR name MATCHES "^test_emit_" OR name MATCHES "^test_target_" OR name MATCHES "^test_aarch64_" OR name MATCHES "^test_arm64_" OR name MATCHES "^codegen_")
        set_tests_properties(${name} PROPERTIES LABELS codegen)
    elseif (name MATCHES "^test_oop_" OR name MATCHES "^oop_" OR name MATCHES "^unit_basic_oop" OR name MATCHES "^test_method_" OR name MATCHES "^test_property_")
        set_tests_properties(${name} PROPERTIES LABELS oop)
    elseif (name MATCHES "^test_viperlang_" OR name MATCHES "^viperlang_")
        set_tests_properties(${name} PROPERTIES LABELS viperlang)
    elseif (name MATCHES "^test_namespace_" OR name MATCHES "^test_using_" OR name MATCHES "^test_ns_")
        set_tests_properties(${name} PROPERTIES LABELS namespace)
    elseif (name MATCHES "^basic_error_" OR name MATCHES "^basic_ast_" OR name MATCHES "^basic_lex_" OR name MATCHES "^basic_to_il_" OR name MATCHES "^basic_semantics_" OR name MATCHES "^basic_proc_" OR name MATCHES "^basic_call_" OR name MATCHES "^basic_ns" OR name MATCHES "^basic_negatives_" OR name MATCHES "^basic_regress_" OR name MATCHES "^basic_select_" OR name MATCHES "^basic_boolean_" OR name MATCHES "^basic_gosub_" OR name MATCHES "^basic_globals_" OR name MATCHES "^basic_print_" OR name MATCHES "^basic_namespace_" OR name MATCHES "^basic_trycatch_" OR name MATCHES "^basic_fileio_" OR name MATCHES "^basic_viper_" OR name MATCHES "^basic_int_" OR name MATCHES "^basic_shared_" OR name MATCHES "^basic_not_" OR name MATCHES "^basic_runtime_ns_" OR name MATCHES "^basic_using_" OR name MATCHES "^basic_arrays_" OR name MATCHES "^example_basic_" OR name MATCHES "^errors_map_" OR name MATCHES "^eh_runtime_" OR name MATCHES "^runtime_classes_" OR name MATCHES "^oop_runtime_" OR name MATCHES "^numerics_il_" OR name MATCHES "^il_quickstart_" OR name MATCHES "^il_viper_ns_" OR name MATCHES "^il_legacy_" OR name MATCHES "^basic_select_case_")
        set_tests_properties(${name} PROPERTIES LABELS golden)
    elseif (name MATCHES "^basic_oop_" OR name MATCHES "^basic_numerics_" OR name MATCHES "^basic_args_" OR name MATCHES "^basic_bug" OR name MATCHES "^basic_array" OR name MATCHES "^basic_do_" OR name MATCHES "^basic_for_" OR name MATCHES "^basic_const_" OR name MATCHES "^basic_math_" OR name MATCHES "^basic_abs_" OR name MATCHES "^basic_factorial$" OR name MATCHES "^basic_fibonacci$" OR name MATCHES "^basic_random_" OR name MATCHES "^front_basic_" OR name MATCHES "^monte_carlo_" OR name MATCHES "^random_walk$" OR name MATCHES "^mem2reg_" OR name MATCHES "^ilc_mem2reg_")
        set_tests_properties(${name} PROPERTIES LABELS e2e)
    elseif (name MATCHES "^il_opt_" OR name MATCHES "^constfold_" OR name MATCHES "^simplifycfg_")
        set_tests_properties(${name} PROPERTIES LABELS ilopt)
    elseif (name MATCHES "^smoke_" OR name MATCHES "^basic_sum_" OR name MATCHES "^basic_repros$")
        set_tests_properties(${name} PROPERTIES LABELS smoke)
    elseif (name MATCHES "^tui_")
        set_tests_properties(${name} PROPERTIES LABELS tui)
    elseif (name MATCHES "^perf_")
        set_tests_properties(${name} PROPERTIES LABELS perf)
    elseif (name MATCHES "^test_cli_" OR name MATCHES "^NoAssertFalseGuard$" OR name MATCHES "^test_tools_")
        set_tests_properties(${name} PROPERTIES LABELS tools)
    elseif (name MATCHES "^test_support$" OR name MATCHES "^test_run_" OR name MATCHES "^test_expected_" OR name MATCHES "^test_ident_" OR name MATCHES "^test_path_" OR name MATCHES "^test_integer_" OR name MATCHES "^test_window$" OR name MATCHES "^test_pixels$" OR name MATCHES "^test_drawing$" OR name MATCHES "^test_input$")
        set_tests_properties(${name} PROPERTIES LABELS unit)
    endif ()
endfunction()

function(viper_add_test target)
    set(options NO_CTEST)
    set(oneValueArgs TEST_NAME)
    set(multiValueArgs SRCS)
    cmake_parse_arguments(VT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    if (VT_SRCS)
        set(_viper_sources ${VT_SRCS})
    else ()
        set(_viper_sources ${VT_UNPARSED_ARGUMENTS})
    endif ()
    if (NOT _viper_sources)
        message(FATAL_ERROR "viper_add_test requires at least one source")
    endif ()
    add_executable(${target} ${_viper_sources})
    target_link_libraries(${target} PRIVATE viper_testing)
    if (VT_NO_CTEST)
        return()
    endif ()
    if (VT_TEST_NAME)
        set(_viper_test_name ${VT_TEST_NAME})
    else ()
        set(_viper_test_name ${target})
    endif ()
    add_test(NAME ${_viper_test_name} COMMAND ${target})
    _viper_assign_test_label(${_viper_test_name})
endfunction()

function(viper_add_ctest name)
    if (ARGC EQUAL 2 AND "${name}" STREQUAL "${ARGV1}")
        return()
    endif ()
    add_test(NAME ${name} COMMAND ${ARGN})
    _viper_assign_test_label(${name})
endfunction()

# Ensure no new golden directories sneak into the tree. Golden tests must rely on
# the shared suites under tests/golden/ rather than ad-hoc "goldens" folders.
function(viper_assert_no_goldens_directories)
    file(
            GLOB_RECURSE _viper_goldens
            LIST_DIRECTORIES true
            RELATIVE "${CMAKE_SOURCE_DIR}/src/tests"
            "${CMAKE_SOURCE_DIR}/src/tests/*")

    list(FILTER _viper_goldens INCLUDE REGEX "/goldens$")

    if (_viper_goldens)
        list(TRANSFORM _viper_goldens PREPEND "${CMAKE_SOURCE_DIR}/src/tests/")
        list(JOIN _viper_goldens "\n  " _viper_goldens_pretty)
        message(
                FATAL_ERROR
                "Golden directories named 'goldens' are prohibited. Remove or rename:\n  ${_viper_goldens_pretty}")
    endif ()
endfunction()

viper_assert_no_goldens_directories()

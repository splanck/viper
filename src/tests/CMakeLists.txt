# SPDX-License-Identifier: GPL-3.0-only
# File: tests/CMakeLists.txt
# Purpose: Configure test suites.

cmake_minimum_required(VERSION 3.20)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/TestHelpers.cmake)

## Docs lint disabled (no external scripting dependencies)

set(VIPER_TESTS_DIR ${CMAKE_CURRENT_LIST_DIR})

file(GLOB VIPER_TEST_SCRIPT_FILES
        LIST_DIRECTORIES FALSE
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*.sh")
foreach (_viper_test_script IN LISTS VIPER_TEST_SCRIPT_FILES)
    file(
            CHMOD "${_viper_test_script}"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
endforeach ()

find_package(Threads REQUIRED)

add_library(viper_testing INTERFACE)
target_link_libraries(viper_testing INTERFACE viper_common_opts viper::il_full)

add_library(viper_test_common STATIC
        ${CMAKE_CURRENT_LIST_DIR}/common/CodegenFixture.cpp
        ${CMAKE_CURRENT_LIST_DIR}/common/ILGenerator.cpp
        ${CMAKE_CURRENT_LIST_DIR}/common/TestIRBuilder.cpp
        ${CMAKE_CURRENT_LIST_DIR}/common/VmFixture.cpp)
# Limit include paths to avoid clashing with the C++ <version> header on
# case-insensitive filesystems (root contains a VERSION file).
# Expose only tests dir, public headers, generated headers, and src as needed.
target_include_directories(viper_test_common PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(viper_test_common PUBLIC viper_testing il_vm il_build)

## docs_comment_check removed to avoid Python dependency

add_subdirectory(unit)
add_subdirectory(il)
add_subdirectory(vm)
add_subdirectory(perf)
add_subdirectory(tools)
add_subdirectory(basic)
add_subdirectory(zia)
add_subdirectory(golden)
add_subdirectory(e2e)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/codegen/x86_64)

# ARM64/AArch64 tests - skip on Windows (x86_64)
if (NOT WIN32)

    # ── helper: register one codegen unit test ──────────────────────────
    macro(_aarch64_add_test _name _src _libs)
        add_executable(${_name} ${_src})
        target_link_libraries(${_name} PRIVATE viper_testing ${_libs})
        viper_add_ctest(${_name} $<TARGET_FILE:${_name}>)
    endmacro()

    # helper variant for tests that use viper_test_common instead of viper_testing
    macro(_aarch64_add_test_common _name _src _libs)
        add_executable(${_name} ${_src})
        target_link_libraries(${_name} PRIVATE viper_test_common ${_libs})
        viper_add_ctest(${_name} $<TARGET_FILE:${_name}>)
    endmacro()

    # ── Group 1: il_codegen_aarch64 only ────────────────────────────────
    set(_AARCH64_CODEGEN_ONLY_TESTS
            test_target_aarch64
            test_emit_aarch64_minimal
            test_emit_aarch64_mir_minimal
            test_emit_aarch64_frameplan
            test_emit_aarch64_mir_bitwise
            test_emit_aarch64_mir_branches
            test_emit_aarch64_mir_frameplan
            test_codegen_arm64_peephole
            test_aarch64_rodata_pool
            test_codegen_review_fixes
    )
    foreach (_test IN LISTS _AARCH64_CODEGEN_ONLY_TESTS)
        _aarch64_add_test(${_test}
                ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/${_test}.cpp
                "il_codegen_aarch64")
    endforeach ()

    # ── Group 2: il_codegen_aarch64 + viper_cmd_arm64 ──────────────────
    set(_AARCH64_CMD_TESTS
            test_codegen_arm64_cli
            test_codegen_arm64_add_params
            test_codegen_arm64_ret_param
            test_codegen_arm64_add_imm
            test_codegen_arm64_bitwise
            test_codegen_arm64_params_wide
            test_codegen_arm64_ovf
            test_codegen_arm64_shift_imm
            test_codegen_arm64_stack_locals
            test_codegen_arm64_stack_locals_cli
            test_codegen_arm64_gep_load_store
            test_codegen_arm64_array_access
            test_codegen_arm64_const_str_addr
            test_codegen_arm64_mir_dump
            test_codegen_arm64_ra_many_temps
            test_codegen_arm64_icmp
            test_codegen_arm64_large_imm
            test_codegen_arm64_icmp_imm
            test_codegen_arm64_switch
            test_codegen_arm64_trap_eh
            test_codegen_arm64_run_native
            test_codegen_arm64_bugfix
            test_codegen_arm64_cbr
            test_codegen_arm64_call
            test_codegen_arm64_call_args
            test_codegen_arm64_call_mid_block
            test_codegen_arm64_cf_if_else_phi
            test_codegen_arm64_select
            test_codegen_arm64_cf_loop_phi
            test_codegen_arm64_call_temp_args
            test_codegen_arm64_print_str
            test_codegen_arm64_dead_strip
            test_codegen_arm64_modvar_addr
            test_codegen_arm64_emit_and_link
            test_codegen_arm64_arr_obj_len
            test_codegen_arm64_obj_field_gep
            test_codegen_arm64_arr_obj_put_get
            test_codegen_arm64_call_stack_args
            test_codegen_arm64_callee_stack_params
            test_codegen_arm64_call_convention
            test_codegen_arm64_fp
            test_codegen_arm64_indirect_call
            test_codegen_arm64_idxchk
            test_codegen_arm64_division
            test_codegen_arm64_exception_handling
            test_codegen_arm64_string_store_refcount
            test_codegen_arm64_sub_mul
            test_codegen_arm64_shift_reg
            test_codegen_arm64_fp_cmp_all
            test_codegen_arm64_gaddr_constnull
            test_codegen_arm64_spill_fpr
            test_codegen_arm64_cross_block_phi_spill
            test_codegen_arm64_unsigned_cmp
            test_codegen_arm64_peephole_comprehensive
            test_codegen_arm64_abi_mixed_args
            test_codegen_arm64_rodata_literals
            test_codegen_arm64_callee_saved
    )
    foreach (_test IN LISTS _AARCH64_CMD_TESTS)
        _aarch64_add_test(${_test}
                ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/${_test}.cpp
                "il_codegen_aarch64;viper_cmd_arm64")
    endforeach ()

    # Temporarily disabled: flaky on some environments while AArch64 cast tests
    # also covered by VM and IL suites.
    # _aarch64_add_test(test_codegen_arm64_casts
    #     ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_casts.cpp
    #     "il_codegen_aarch64;viper_cmd_arm64")

    # CLI run-native should return user exit code
    # Note: run-native exit code coverage exists in other tests; keep CI stable.
    # _aarch64_add_test(test_codegen_arm64_run_ret42
    #     ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_arm64_run_ret42.cpp
    #     "il_codegen_aarch64;viper_cmd_arm64")

    # ── Group 3: special link patterns ──────────────────────────────────

    # Environment.IsNative should distinguish VM vs native paths
    _aarch64_add_test_common(test_codegen_env_is_native
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_codegen_env_is_native.cpp
            "il_codegen_aarch64;viper_cmd_arm64")

    # Property-based differential testing: VM vs ARM64 native
    # This test must run alone (RUN_SERIAL) to avoid resource contention with other
    # codegen tests that use the same assembler/linker toolchain.
    _aarch64_add_test_common(test_diff_vm_native_property
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_diff_vm_native_property.cpp
            "il_codegen_aarch64;viper_cmd_arm64")
    set_tests_properties(test_diff_vm_native_property PROPERTIES RUN_SERIAL TRUE TIMEOUT 120 LABELS "slow")

    # x86-64 runtime map (lives here because it requires non-Windows host)
    _aarch64_add_test_common(test_emit_x86_runtime_map
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/codegen/test_emit_x86_runtime_map.cpp
            "viper_codegen_x86_64")

    # Optimizer pipeline differential testing (VM baseline vs O0/O1/O2)
    _aarch64_add_test_common(test_il_opt_equivalence
            ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_il_opt_equivalence.cpp
            "il_transform;viper::il_verify")

    # ── ARM64 end-to-end demo tests ────────────────────────────────────
    set(_AARCH64_E2E_TESTS
            test_arm64_demos
            test_arm64_demos_run
    )
    foreach (_test IN LISTS _AARCH64_E2E_TESTS)
        _aarch64_add_test(${_test}
                ${CMAKE_CURRENT_SOURCE_DIR}/e2e/${_test}.cpp
                "viper_common")
    endforeach ()

endif () # NOT WIN32 - ARM64 tests

viper_add_ctest(NoAssertFalseGuard
        ${CMAKE_COMMAND}
        -DVIPER_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/src/tests/tools/NoAssertFalseTest.cmake)

add_test(NAME smoke_term_basic
        COMMAND $<TARGET_FILE:viper> front basic -run ${CMAKE_CURRENT_SOURCE_DIR}/smoke/term_basic.bas)

# Shell script tests - skip on Windows
if (NOT WIN32)
    add_test(NAME basic_sum_no_linenos
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_sum_no_linenos.sh
            $<TARGET_FILE:viper>)
    set_tests_properties(basic_sum_no_linenos
            PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif ()

# ── BASIC OOP unit tests ────────────────────────────────────────────────
viper_add_test(test_basic_oop_parsing
        SRCS ${CMAKE_CURRENT_SOURCE_DIR}/unit/BasicOOP_Parsing.cpp
        TEST_NAME unit_basic_oop_parsing
        LIBS fe_basic)

viper_add_test(test_basic_oop_lowering
        SRCS ${CMAKE_CURRENT_SOURCE_DIR}/unit/BasicOOP_Lowering.cpp
        TEST_NAME unit_basic_oop_lowering
        LIBS fe_basic)

# NOTE: The following tests were previously compiled but never registered with
# ctest due to a viper_add_ctest(name name) no-op pattern. They have pre-existing
# failures that need investigation. NO_CTEST preserves the old behavior.
viper_add_test(test_basic_oop_string_array_field NO_CTEST
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_oop_string_array_field.cpp
        LIBS fe_basic)

viper_add_test(test_basic_oop_object_array_field NO_CTEST
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_oop_object_array_field.cpp
        LIBS fe_basic)

viper_add_test(test_basic_global_string_array_sub_store
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_global_string_array_sub_store.cpp
        LIBS fe_basic)

viper_add_test(test_basic_select_case_object_assign
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_select_case_object_assign.cpp
        LIBS fe_basic)

viper_add_test(test_basic_input_typing NO_CTEST
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_input_typing.cpp
        LIBS fe_basic)

viper_add_test(test_basic_nested_member_method_call NO_CTEST
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_nested_member_method_call.cpp
        LIBS fe_basic)

viper_add_test(test_basic_class_return
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_class_return.cpp
        LIBS fe_basic)

viper_add_test(test_basic_lower_builtin_dispatch
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_lower_builtin_dispatch.cpp
        LIBS fe_basic)

# ── Runtime class catalog and binding tests ─────────────────────────────
viper_add_test(test_runtime_class_catalog NO_CTEST
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime_classes/TestRuntimeClassCatalog.cpp
        LIBS fe_basic)

viper_add_test(test_runtime_property_binding
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime_classes/TestPropertyBinding.cpp
        LIBS fe_basic)

viper_add_test(test_runtime_method_binding
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime_classes/TestMethodBinding.cpp
        LIBS fe_basic)

viper_add_test(test_runtime_catalog_targets_resolve NO_CTEST
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime_classes/TestCatalogTargetsResolve.cpp
        LIBS fe_basic)

viper_add_test(test_frontends_basic_runtime_property_diag
        ${CMAKE_CURRENT_SOURCE_DIR}/frontends/basic/RuntimePropertyDiagTests.cpp
        LIBS fe_basic)

viper_add_test(test_basic_semantic_new_ctor
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_basic_semantic_new_ctor.cpp
        LIBS fe_basic)

viper_add_test(test_run_process_quotes
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_run_process_quotes.cpp
        LIBS viper_common)

add_test(NAME smoke_basic_oop
        COMMAND $<TARGET_FILE:viper> front basic -run ${CMAKE_CURRENT_SOURCE_DIR}/smoke/basic_oop.bas)

add_test(NAME smoke_basic_point_ctor
        COMMAND $<TARGET_FILE:viper> front basic -run ${CMAKE_CURRENT_SOURCE_DIR}/smoke/basic_point_ctor.bas)

viper_add_unit_support_tests()
viper_add_il_core_tests()
viper_add_basic_early_tests()
viper_add_il_utils_test()
viper_add_tools_tests()
viper_add_vm_debug_tests()
viper_add_il_analysis_tests()
viper_add_il_verify_examples()
viper_add_e2e_vm_examples_smoke()
viper_add_runtime_tests()
viper_add_e2e_vm_suite()
viper_add_il_invalid_tests()
viper_add_basic_main_tests()
viper_add_zia_tests()
viper_add_vm_unit_tests()
viper_add_il_liveness_tests()
viper_add_vm_path_tests()
viper_add_basic_ast_golden_tests()
viper_add_basic_lex_golden_tests()
viper_add_basic_to_il_golden_tests()
viper_add_basic_strings_test()
viper_add_basic_semantics_golden_tests()
viper_add_basic_proc_tests()
viper_add_basic_call_tests()
viper_add_basic_integration_tests()
viper_add_basic_namespace_tests()
viper_add_e2e_codegen_tests()
viper_add_golden_suite_tests()

# ── Milestone D unit tests (OOP) ────────────────────────────────────────
# NOTE: Several OOP tests were previously compiled but never registered with
# ctest due to a viper_add_ctest(name name) no-op pattern. They have pre-existing
# failures. NO_CTEST preserves the old behavior until they are fixed.
viper_add_test(test_oop_static_members NO_CTEST
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestStaticMembers.cpp
        LIBS fe_basic)

viper_add_test(test_oop_properties NO_CTEST
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestProperties.cpp
        LIBS fe_basic)

viper_add_test(test_oop_overloads
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestOverloads.cpp
        LIBS fe_basic)

viper_add_test(test_oop_inheritance
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestInheritance.cpp
        LIBS fe_basic viper_test_common)

viper_add_test(test_oop_destructor_dispose NO_CTEST
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestDestructorDispose.cpp
        LIBS fe_basic)

viper_add_test(test_oop_destructor_chain NO_CTEST
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestDestructorChain.cpp
        LIBS fe_basic viper_test_common)

viper_add_test(test_oop_dispose
        ${CMAKE_CURRENT_SOURCE_DIR}/unit/oop/TestDispose.cpp
        LIBS fe_basic viper_test_common)

# Shell script tests - skip on Windows
if (NOT WIN32)
    add_test(NAME basic_repros
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_basic_repros.sh
            $<TARGET_FILE:viper>)
    set_tests_properties(basic_repros
            PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            LABELS smoke)
endif ()

# =============================================================================
# Test Labels Configuration
# =============================================================================
# Labels are applied by pattern-matching test names. Tests defined in
# subdirectories via viper_add_ctest get their labels set here.
#
# Use `ctest -L <label>` to run tests with a specific label.
# Use `ctest --print-labels` to see all available labels.
#
# The label summary appears at the end of `ctest` output.

# Get all test names defined in this scope
get_property(ALL_TESTS DIRECTORY PROPERTY TESTS)

foreach (TEST_NAME IN LISTS ALL_TESTS)
    # Skip if test already has a label
    get_test_property(${TEST_NAME} LABELS EXISTING_LABEL)
    if (EXISTING_LABEL)
        continue()
    endif ()

    # Assign labels based on test name patterns
    if (TEST_NAME MATCHES "^test_basic_" OR TEST_NAME MATCHES "^test_frontends_basic_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS basic)
    elseif (TEST_NAME MATCHES "^test_il_" OR TEST_NAME MATCHES "^il_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS il)
    elseif (TEST_NAME MATCHES "^test_vm_" OR TEST_NAME MATCHES "^vm_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS vm)
    elseif (TEST_NAME MATCHES "^test_rt_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS runtime)
    elseif (TEST_NAME MATCHES "^test_codegen_" OR TEST_NAME MATCHES "^test_emit_" OR TEST_NAME MATCHES "^test_target_" OR TEST_NAME MATCHES "^test_aarch64_" OR TEST_NAME MATCHES "^test_arm64_" OR TEST_NAME MATCHES "^codegen_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS codegen)
    elseif (TEST_NAME MATCHES "^test_oop_" OR TEST_NAME MATCHES "^oop_" OR TEST_NAME MATCHES "^unit_basic_oop")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS oop)
    elseif (TEST_NAME MATCHES "^test_runtime_" OR TEST_NAME MATCHES "^runtime_" OR TEST_NAME MATCHES "^test_stringbuilder_" OR TEST_NAME MATCHES "^test_console_" OR TEST_NAME MATCHES "^test_convert_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS runtime)
    elseif (TEST_NAME MATCHES "^test_namespace_" OR TEST_NAME MATCHES "^test_using_" OR TEST_NAME MATCHES "^test_ns_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS namespace)
    elseif (TEST_NAME MATCHES "^test_lowerer_" OR TEST_NAME MATCHES "^test_type_" OR TEST_NAME MATCHES "^test_builtin_" OR TEST_NAME MATCHES "^test_lowering_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS basic)
    elseif (TEST_NAME MATCHES "^basic_error_" OR TEST_NAME MATCHES "^basic_ast_" OR TEST_NAME MATCHES "^basic_lex_" OR TEST_NAME MATCHES "^basic_to_il_" OR TEST_NAME MATCHES "^basic_semantics_" OR TEST_NAME MATCHES "^basic_proc_" OR TEST_NAME MATCHES "^basic_call_" OR TEST_NAME MATCHES "^basic_ns" OR TEST_NAME MATCHES "^basic_negatives_" OR TEST_NAME MATCHES "^basic_regress_" OR TEST_NAME MATCHES "^basic_select_" OR TEST_NAME MATCHES "^basic_boolean_" OR TEST_NAME MATCHES "^basic_gosub_" OR TEST_NAME MATCHES "^basic_globals_" OR TEST_NAME MATCHES "^basic_print_" OR TEST_NAME MATCHES "^basic_namespace_" OR TEST_NAME MATCHES "^basic_trycatch_" OR TEST_NAME MATCHES "^basic_fileio_" OR TEST_NAME MATCHES "^basic_viper_" OR TEST_NAME MATCHES "^basic_int_" OR TEST_NAME MATCHES "^basic_shared_" OR TEST_NAME MATCHES "^basic_not_" OR TEST_NAME MATCHES "^basic_runtime_ns_" OR TEST_NAME MATCHES "^basic_using_" OR TEST_NAME MATCHES "^basic_arrays_" OR TEST_NAME MATCHES "^example_basic_" OR TEST_NAME MATCHES "^errors_map_" OR TEST_NAME MATCHES "^eh_runtime_" OR TEST_NAME MATCHES "^runtime_classes_" OR TEST_NAME MATCHES "^oop_runtime_" OR TEST_NAME MATCHES "^numerics_il_" OR TEST_NAME MATCHES "^il_quickstart_" OR TEST_NAME MATCHES "^il_viper_ns_" OR TEST_NAME MATCHES "^il_legacy_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS golden)
    elseif (TEST_NAME MATCHES "^basic_oop_" OR TEST_NAME MATCHES "^basic_numerics_" OR TEST_NAME MATCHES "^basic_args_" OR TEST_NAME MATCHES "^basic_bug" OR TEST_NAME MATCHES "^basic_array" OR TEST_NAME MATCHES "^basic_do_" OR TEST_NAME MATCHES "^basic_for_" OR TEST_NAME MATCHES "^basic_const_" OR TEST_NAME MATCHES "^basic_math_" OR TEST_NAME MATCHES "^basic_abs_" OR TEST_NAME MATCHES "^basic_factorial$" OR TEST_NAME MATCHES "^basic_fibonacci$" OR TEST_NAME MATCHES "^basic_random_" OR TEST_NAME MATCHES "^front_basic_" OR TEST_NAME MATCHES "^monte_carlo_" OR TEST_NAME MATCHES "^random_walk$" OR TEST_NAME MATCHES "^mem2reg_" OR TEST_NAME MATCHES "^ilc_mem2reg_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS e2e)
    elseif (TEST_NAME MATCHES "^il_opt_" OR TEST_NAME MATCHES "^constfold_" OR TEST_NAME MATCHES "^simplifycfg_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS ilopt)
    elseif (TEST_NAME MATCHES "^smoke_" OR TEST_NAME MATCHES "^basic_sum_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS smoke)
    elseif (TEST_NAME MATCHES "^tui_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS tui)
    elseif (TEST_NAME MATCHES "^perf_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS perf)
    elseif (TEST_NAME MATCHES "^test_cli_" OR TEST_NAME MATCHES "^NoAssertFalseGuard$" OR TEST_NAME MATCHES "^test_tools_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS tools)
    elseif (TEST_NAME MATCHES "^test_analysis_" OR TEST_NAME MATCHES "^test_irbuilder_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS il)
    elseif (TEST_NAME MATCHES "^test_support$" OR TEST_NAME MATCHES "^test_run_" OR TEST_NAME MATCHES "^test_expected_" OR TEST_NAME MATCHES "^test_ident_" OR TEST_NAME MATCHES "^test_path_" OR TEST_NAME MATCHES "^test_integer_" OR TEST_NAME MATCHES "^test_catalog_" OR TEST_NAME MATCHES "^test_window$" OR TEST_NAME MATCHES "^test_pixels$" OR TEST_NAME MATCHES "^test_drawing$" OR TEST_NAME MATCHES "^test_input$")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS unit)
    elseif (TEST_NAME MATCHES "^test_method_" OR TEST_NAME MATCHES "^test_property_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS oop)
    elseif (TEST_NAME MATCHES "^test_vm_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS vm)
    elseif (TEST_NAME MATCHES "^tui_test_" OR TEST_NAME MATCHES "^tui_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS tui)
    elseif (TEST_NAME MATCHES "^test_runtime_classes_" OR TEST_NAME MATCHES "^test_stringbuilder_")
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS runtime)
    endif ()
endforeach ()

# Additional labels for tests added via add_test() rather than viper_add_ctest()
# These tests are defined at file scope and picked up by get_property(DIRECTORY)
if (TEST perf_vm_dispatch_bench)
    set_tests_properties(perf_vm_dispatch_bench perf_vm_switch_bench PROPERTIES LABELS perf)
endif ()
if (TEST basic_select_case_string)
    set_tests_properties(basic_select_case_string PROPERTIES LABELS golden)
endif ()

# =============================================================================
# Demo compilation tests (ARM64 only)
# These tests verify that the full compilation pipeline works for each demo.
# They compile through: source -> IL -> ARM64 assembly -> object -> executable
# =============================================================================
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(DEMO_TEST_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/test_demo_compile.sh")
    set(DEMOS_DIR "${CMAKE_SOURCE_DIR}/demos")

    # BASIC demos
    set(BASIC_DEMOS
            "chess:${DEMOS_DIR}/basic/chess/chess.bas"
            "vtris:${DEMOS_DIR}/basic/vtris/vtris.bas"
            "frogger:${DEMOS_DIR}/basic/frogger/frogger.bas"
            "centipede:${DEMOS_DIR}/basic/centipede/centipede.bas"
            "pacman:${DEMOS_DIR}/basic/pacman/pacman.bas"
    )

    foreach (demo IN LISTS BASIC_DEMOS)
        string(REPLACE ":" ";" demo_parts "${demo}")
        list(GET demo_parts 0 demo_name)
        list(GET demo_parts 1 demo_source)
        if (EXISTS "${demo_source}")
            add_test(NAME demo_basic_${demo_name}
                    COMMAND "${DEMO_TEST_SCRIPT}" "basic" "${demo_source}"
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
            set_tests_properties(demo_basic_${demo_name} PROPERTIES
                    LABELS "demos;ARM64;NativeLink"
                    TIMEOUT 60)
        endif ()
    endforeach ()

    # Zia demos
    set(ZIA_DEMOS
            "paint:${DEMOS_DIR}/zia/paint/main.zia"
    )

    foreach (demo IN LISTS ZIA_DEMOS)
        string(REPLACE ":" ";" demo_parts "${demo}")
        list(GET demo_parts 0 demo_name)
        list(GET demo_parts 1 demo_source)
        if (EXISTS "${demo_source}")
            add_test(NAME demo_zia_${demo_name}
                    COMMAND "${DEMO_TEST_SCRIPT}" "zia" "${demo_source}"
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
            set_tests_properties(demo_zia_${demo_name} PROPERTIES
                    LABELS "demos;ARM64;NativeLink"
                    TIMEOUT 60)
        endif ()
    endforeach ()
endif ()

# =============================================================================
# Zia Runtime Tests
# These tests verify the Viper runtime API functions work correctly via Zia.
# Each test outputs "RESULT: ok" on success.
# =============================================================================
set(ZIA_RUNTIME_TESTS_DIR "${CMAKE_SOURCE_DIR}/tests/runtime")
set(ZIA_RUNTIME_TESTS
        test_math
        test_string
        test_bits
        test_list
        test_map
        test_collections
        test_bytes
        test_datetime
        test_random
        test_crypto
        test_graphics
        test_terminal
        test_environment
        test_diagnostics
        test_file
        test_gui
        test_bugfix_text
        test_bugfix_crypto
        test_bugfix_string
        test_bugfix_boolyn
        test_bugfix_html
        test_bugfix_bool_returns
        test_bugfix_game_heap
        test_bugfix_ring
        test_bugfix_saynum
        test_bugfix_random_new
        test_bugfix_zia_new
        test_sprite_flip
        test_png_io
        test_collision_layers
        test_tilemap_collision
        test_iterator
        test_audio_api
        test_network_api
        test_threading_api
        test_concmap
        test_spritesheet
        test_physics2d_full
        test_json_stream
        test_action_api
        test_parallel_api
        test_chord_api
        test_quaternion
        test_spline
        test_serialize_unified
        test_weakref_gc
        test_async_promise
)

foreach (test_name IN LISTS ZIA_RUNTIME_TESTS)
    set(test_file "${ZIA_RUNTIME_TESTS_DIR}/${test_name}.zia")
    if (EXISTS "${test_file}")
        add_test(NAME zia_runtime_${test_name}
                COMMAND $<TARGET_FILE:zia> "${test_file}"
                WORKING_DIRECTORY "${ZIA_RUNTIME_TESTS_DIR}")
        set_tests_properties(zia_runtime_${test_name} PROPERTIES
                PASS_REGULAR_EXPRESSION "RESULT: ok"
                LABELS "runtime;zia"
                TIMEOUT 30)
    endif ()
endforeach ()

# =============================================================================
# Zia Language Feature Tests
# These tests verify Zia language features work correctly.
# Each test outputs "RESULT: ok" on success.
# =============================================================================
set(ZIA_LANG_TESTS_DIR "${CMAKE_SOURCE_DIR}/tests/zia_runtime")
# Note: Some tests excluded due to environment requirements:
# - 04_environment, 17_time_env: require command-line arguments
# - 07_terminal_input: requires interactive input
# - 09_math_rnd: requires specific Viper namespace bindings
set(ZIA_LANG_TESTS
        01_terminal_math_random
        02_strings
        03_collections
        05_optionals_match
        06_control_flow
        08_string_length
        10_for_break_continue
        11_recursion_factorial
        12_list_map_stats
        13_optional_chain
        14_entity_methods
        15_generics_annotations
        16_string_building
        18_numeric_algorithms
        19_lambdas
        20_guard_error_handling
        21_inheritance_deep
        22_string_interpolation
        23_pattern_matching_advanced
        24_generics_advanced
        25_interfaces
        26_modules_namespaces
        27_type_coercion
        28_numeric_edge_cases
        29_control_flow_advanced
        30_value_types
)

foreach (test_name IN LISTS ZIA_LANG_TESTS)
    set(test_file "${ZIA_LANG_TESTS_DIR}/${test_name}.zia")
    if (EXISTS "${test_file}")
        add_test(NAME zia_lang_${test_name}
                COMMAND $<TARGET_FILE:zia> "${test_file}"
                WORKING_DIRECTORY "${ZIA_LANG_TESTS_DIR}")
        set_tests_properties(zia_lang_${test_name} PROPERTIES
                PASS_REGULAR_EXPRESSION "RESULT: ok"
                LABELS "zia"
                TIMEOUT 30)
    endif ()
endforeach ()

# =============================================================================
# Zia Namespace Bind Tests
# These tests verify the bind Viper.* namespace import feature.
# Each test outputs "RESULT: ok" on success.
# =============================================================================
set(ZIA_BIND_NAMESPACE_TESTS_DIR "${CMAKE_SOURCE_DIR}/tests/zia/bind_namespace")
set(ZIA_BIND_NAMESPACE_TESTS
        test_basic
        test_alias
        test_selective
        test_new
)

foreach (test_name IN LISTS ZIA_BIND_NAMESPACE_TESTS)
    set(test_file "${ZIA_BIND_NAMESPACE_TESTS_DIR}/${test_name}.zia")
    if (EXISTS "${test_file}")
        add_test(NAME zia_bind_namespace_${test_name}
                COMMAND $<TARGET_FILE:zia> "${test_file}"
                WORKING_DIRECTORY "${ZIA_BIND_NAMESPACE_TESTS_DIR}")
        set_tests_properties(zia_bind_namespace_${test_name} PROPERTIES
                PASS_REGULAR_EXPRESSION "RESULT: ok"
                LABELS "zia;namespace"
                TIMEOUT 30)
    endif ()
endforeach ()

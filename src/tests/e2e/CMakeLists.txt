# SPDX-License-Identifier: GPL-3.0-only
# File: tests/e2e/CMakeLists.txt
# Purpose: End-to-end CLI and VM integration tests.

function(viper_add_e2e_vm_examples_smoke)
    if (TEST vm_examples)
        return()
    endif ()

    set(_VIPER_E2E_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
    viper_add_ctest(vm_examples
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_vm_examples.cmake)
    viper_set_test_labels(vm_examples vm)
endfunction()

function(viper_add_e2e_vm_suite)
    if (TEST vm_strings_example)
        return()
    endif ()

    set(_VIPER_E2E_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

    if (NOT TARGET float_out)
        viper_add_test(float_out ${_VIPER_E2E_DIR}/support/FloatOut.cpp NO_CTEST)
    endif ()

    viper_add_ctest(vm_strings_example
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_vm_strings.cmake)
    viper_set_test_labels(vm_strings_example vm)

    viper_add_ctest(vm_step_limit
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_vm_step_limit.cmake)
    viper_set_test_labels(vm_step_limit vm)

    viper_add_ctest(vm_block_params
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_vm_block_params.cmake)
    viper_set_test_labels(vm_block_params vm)

    viper_add_ctest(vm_math_core
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_vm_math_core.cmake)
    viper_set_test_labels(vm_math_core vm)

    viper_add_ctest(vm_math_trigpow
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_vm_math_trigpow.cmake)
    viper_set_test_labels(vm_math_trigpow vm)

    viper_add_ctest(vm_rec_fact
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_vm_rec_fact.cmake)
    viper_set_test_labels(vm_rec_fact vm)

    viper_add_ctest(vm_trace_src
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/basic/trace_src.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/TraceSrcTests.bas.out
            -P ${_VIPER_E2E_DIR}/test_trace_src.cmake)
    viper_set_test_labels(vm_trace_src vm)

    viper_add_ctest(vm_break_src_exact
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_FILE=src/tests/e2e/BreakSrcExact.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/golden/break_src_exact.out
            -DLINE=1
            -DROOT=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_break_src_exact.cmake)
    viper_set_test_labels(vm_break_src_exact vm)

    viper_add_ctest(vm_break_src_coalesce
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_FILE=src/tests/e2e/BreakSrcCoalesce.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/golden/break_src_coalesce.out
            -DLINE=1
            -DROOT=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_break_src_exact.cmake)
    viper_set_test_labels(vm_break_src_coalesce vm)

    viper_add_ctest(front_basic_example
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_front_basic.cmake)

    viper_add_ctest(basic_math_phase1
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_math_phase1.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_math_phase1.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)

    viper_add_ctest(basic_math_phase2
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_math_phase2.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_math_phase2.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)

    viper_add_ctest(basic_abs_mixed
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_abs_mixed.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_abs_mixed.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)

    viper_add_ctest(basic_numerics_addmul
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_addmul.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_addmul.bas.out
            -DEXPECT_SUBSTR=Overflow
            -P ${_VIPER_E2E_DIR}/test_basic_numeric_overflow.cmake)
    viper_set_test_labels(basic_numerics_addmul vm)

    viper_add_ctest(basic_numerics_pow
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_pow.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_pow.bas.out
            -DEXPECT_SUBSTR=domain\ error
            -P ${_VIPER_E2E_DIR}/test_basic_numeric_overflow.cmake)
    viper_set_test_labels(basic_numerics_pow vm)

    viper_add_ctest(basic_numerics_conversions
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_conversions.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_conversions.bas.out
            -DEXPECT_SUBSTR=InvalidCast
            -P ${_VIPER_E2E_DIR}/test_basic_numeric_overflow.cmake)
    viper_set_test_labels(basic_numerics_conversions vm)

    viper_add_ctest(basic_numerics_val
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_val.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_val.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_numerics_val vm)

    viper_add_ctest(basic_numerics_literal_suffixes
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_literal_suffixes.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_literal_suffixes.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_numerics_literal_suffixes vm)

    viper_add_ctest(basic_select_case_run
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/select_case_run.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/select_case_run.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_select_case_run vm)

    viper_add_ctest(basic_array_sum
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_array_sum.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_array_sum.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_array_sum.cmake)
    viper_set_test_labels(basic_array_sum vm)

    viper_add_ctest(basic_do_exit
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/do_exit.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/do_exit.bas.out
            -P ${_VIPER_E2E_DIR}/test_do_exit.cmake)
    viper_set_test_labels(basic_do_exit vm)

    viper_add_ctest(basic_for_step_neg
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/for_step_neg.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/for_step_neg.bas.out
            -P ${_VIPER_E2E_DIR}/test_do_exit.cmake)
    viper_set_test_labels(basic_for_step_neg vm)

    viper_add_ctest(basic_oop_field_access
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_oop_field.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_oop_field.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_oop_field_access vm)

    viper_add_ctest(basic_oop_string_property
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_oop_string_property.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_oop_string_property.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_oop_string_property vm)

    viper_add_ctest(basic_print_field_string
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_print_field_string.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_print_field_string.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_print_field_string vm)

    # Arrays as class fields must allocate and be accessible
    viper_add_ctest(basic_oop_array_field
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_oop_array_field.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_oop_array_field.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_oop_array_field vm)


    viper_add_ctest(basic_const_string
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_const_string.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_const_string.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_const_string vm)

    viper_add_ctest(basic_oop_function_return
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_oop_function_return.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_oop_function_return.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_oop_function_return vm)

    viper_add_ctest(basic_oop_multi_obj_return
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_oop_multi_obj_return.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_oop_multi_obj_return.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_oop_multi_obj_return vm)

    # Command-line arguments: no args
    viper_add_ctest(basic_args_none
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_args_none.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_args_none.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_args_run.cmake)
    viper_set_test_labels(basic_args_none vm)

    # Command-line arguments: some args
    # Pass two program arguments: "foo" and "bar baz". Semicolons are CMake's
    # list separators, so escape it (\;) to keep both elements inside the single
    # -DARGS=... argument. Spaces do not need escaping here.
    viper_add_ctest(basic_args_some
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_args_some.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_args_some.bas.out
            "-DARGS=foo\;bar baz"
            -P ${_VIPER_E2E_DIR}/test_basic_args_run.cmake)
    viper_set_test_labels(basic_args_some vm)

    # BUG-053: Access module-level array inside FUNCTION
    viper_add_ctest(basic_bug053_global_array_in_proc
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/bug053_global_array_in_proc.bas
            -P ${_VIPER_E2E_DIR}/test_basic_compile_only.cmake)
    viper_set_test_labels(basic_bug053_global_array_in_proc vm)

    # BUG-052: ON ERROR GOTO no-op should not produce empty block
    viper_add_ctest(basic_bug052_on_error_empty_block
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/bug052_on_error_empty_block.bas
            -P ${_VIPER_E2E_DIR}/test_basic_compile_only.cmake)
    viper_set_test_labels(basic_bug052_on_error_empty_block vm)

    viper_add_ctest(basic_oop_local_string
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_oop_local_string.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_oop_local_string.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_oop_local_string vm)

    viper_add_ctest(basic_oop_method_return
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_oop_method_return.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_oop_method_return.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_oop_method_return vm)

    viper_add_ctest(basic_gosub_return
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/gosub_return.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/gosub_return.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(basic_gosub_return vm)

    viper_add_ctest(basic_array_oob
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic_array_oob.bas
            "-DEXPECT_SUBSTR=rt_arr_i32: index 4 out of bounds (len=4)"
            -P ${_VIPER_E2E_DIR}/test_basic_array_oob.cmake)
    viper_set_test_labels(basic_array_oob vm)

    viper_add_ctest(basic_factorial
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/factorial.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/golden/e2e/factorial.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)

    viper_add_ctest(basic_fibonacci
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/fibonacci.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/golden/e2e/fibonacci.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)

    viper_add_ctest(basic_random_repro
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_basic_random_repro.cmake)

    viper_add_ctest(basic_numerics_all
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_all.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/basic/numerics_all.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)

    viper_add_ctest(monte_carlo_pi
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DFLOAT_OUT=$<TARGET_FILE:float_out>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_monte_carlo_pi.cmake)

    viper_add_ctest(random_walk
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${_VIPER_E2E_DIR}/test_random_walk.cmake)

    viper_add_ctest(il_opt_equiv
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DIL_FILE=${CMAKE_SOURCE_DIR}/src/tests/golden/il_opt/e2e.in.il
            -P ${_VIPER_E2E_DIR}/test_il_opt_equiv.cmake)

    viper_add_ctest(simplifycfg_smoke
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DIL_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/simplifycfg_smoke.il
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/simplifycfg_smoke.out
            -P ${_VIPER_E2E_DIR}/test_simplifycfg_smoke.cmake)
    viper_set_test_labels(simplifycfg_smoke ilopt smoke)

    viper_add_ctest(constfold_equiv
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DIL_FILE=${CMAKE_SOURCE_DIR}/src/tests/il/e2e/math_constfold.il
            -P ${_VIPER_E2E_DIR}/test_constfold_equiv.cmake)

    viper_add_ctest(mem2reg_basic
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/data/mem2reg.bas
            -P ${_VIPER_E2E_DIR}/test_mem2reg_basic.cmake)

    viper_add_ctest(ilc_mem2reg_stats
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DIL_FILE=${CMAKE_SOURCE_DIR}/src/tests/golden/il_opt/mem2reg_simple.in.il
            -P ${_VIPER_E2E_DIR}/test_mem2reg_stats.cmake)

    viper_add_ctest(ilc_mem2reg_stats_multi
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DIL_FILE=${CMAKE_SOURCE_DIR}/src/tests/golden/il_opt/mem2reg_multi.in.il
            -P ${_VIPER_E2E_DIR}/test_mem2reg_stats_multi.cmake)

    # TRY/FINALLY support tests (requires standard VM for runtime exception handling)
    viper_add_ctest(e2e_try_finally
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_try_finally.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_try_finally.bas.out
            -DDEBUG_VM=1
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(e2e_try_finally e2e vm)

    # Constructor arguments tests (multiple types: INTEGER, DOUBLE, STRING)
    viper_add_ctest(e2e_ctor_args
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_ctor_args.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_ctor_args.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(e2e_ctor_args e2e vm)

    # Graphics API basic tests (no window required)
    viper_add_ctest(e2e_graphics_basic
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_graphics_basic.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_graphics_basic.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(e2e_graphics_basic e2e vm)

    # Graphics window tests (requires display - skip in headless CI)
    viper_add_ctest(e2e_graphics_window
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_graphics_window.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_graphics_window.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(e2e_graphics_window e2e vm requires_display)

    # DateTime API tests
    viper_add_ctest(e2e_datetime
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_datetime.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_datetime.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(e2e_datetime e2e)

    # Map API tests
    viper_add_ctest(e2e_map
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_map.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_map.bas.out
            -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
    viper_set_test_labels(e2e_map e2e)

    # USING statement tests (resource cleanup)
    # Skip on Windows: USING statement with DESTROY causes crash (to be investigated)
    if (NOT WIN32)
        viper_add_ctest(e2e_using
                ${CMAKE_COMMAND}
                -DILC=$<TARGET_FILE:viper>
                -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_using.bas
                -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/test_using.bas.out
                -P ${_VIPER_E2E_DIR}/test_basic_math_phase1.cmake)
        viper_set_test_labels(e2e_using e2e)
    endif ()
endfunction()

# OOP runtime e2e: List + String + Object (avoids reserved keyword collisions)
viper_add_ctest(oop_collections_string_report
        ${CMAKE_COMMAND}
        -DILC=$<TARGET_FILE:viper>
        -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/oop/collections_string_report.bas
        -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/oop/collections_string_report.bas.out
        -P ${CMAKE_SOURCE_DIR}/src/tests/e2e/test_basic_math_phase1.cmake)
viper_set_test_labels(oop_collections_string_report e2e vm oop)

function(viper_add_e2e_codegen_tests)
    if (DEFINED VIPER_E2E_CODEGEN_DONE)
        return()
    endif ()

    set(_VIPER_E2E_DIR ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
    set(CODEGEN_E2E ${_VIPER_E2E_DIR}/test_codegen.cmake)

    if (IL_ENABLE_X64_ASM_SYNTAX_CHECK AND CLANG_FOUND)
        viper_add_ctest(codegen_syntax_only
                ${CMAKE_COMMAND}
                "-DASM_FLAGS=${IL_X64_ASM_FLAGS}"
                "-DLD_FLAGS=${IL_X64_LD_FLAGS}"
                -DMODE=syntax
                -P ${CODEGEN_E2E})
        viper_set_test_labels(codegen_syntax_only codegen syntax_only)
    else ()
        message(STATUS "Skipping codegen_* tests (CLANG not found or flags OFF)")
    endif ()

    if (IL_ENABLE_X64_ASM_ASSEMBLE_LINK AND CLANG_FOUND)
        viper_add_ctest(codegen_assemble_link
                ${CMAKE_COMMAND}
                "-DASM_FLAGS=${IL_X64_ASM_FLAGS}"
                "-DLD_FLAGS=${IL_X64_LD_FLAGS}"
                -DMODE=assemble_link
                -P ${CODEGEN_E2E})
        viper_set_test_labels(codegen_assemble_link codegen assemble_link)
    else ()
        message(STATUS "Skipping codegen_* tests (CLANG not found or flags OFF)")
    endif ()

    if (IL_ENABLE_X64_NATIVE_RUN AND CLANG_FOUND)
        viper_add_ctest(codegen_native_run
                ${CMAKE_COMMAND}
                "-DASM_FLAGS=${IL_X64_ASM_FLAGS}"
                "-DLD_FLAGS=${IL_X64_LD_FLAGS}"
                -DMODE=run
                -P ${CODEGEN_E2E})
        viper_set_test_labels(codegen_native_run codegen native_run)
    else ()
        message(STATUS "Skipping codegen_* tests (CLANG not found or flags OFF)")
    endif ()

    set(VIPER_E2E_CODEGEN_DONE TRUE PARENT_SCOPE)
endfunction()

# Namespace multi-file E2E test
if (NOT TARGET test_namespace_e2e)
    viper_add_test(test_namespace_e2e ${VIPER_TESTS_DIR}/e2e/test_namespace_e2e.cpp)
    target_link_libraries(test_namespace_e2e PRIVATE fe_basic viper_support)
    viper_add_ctest(test_namespace_e2e test_namespace_e2e)
endif ()

# ARM64 native linking tests - only on macOS ARM64
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" AND APPLE)
    set(_VIPER_E2E_DIR ${CMAKE_CURRENT_LIST_DIR})

    # Chess demo - compile, assemble, link
    viper_add_ctest(arm64_native_chess
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DRUNTIME_LIB=${CMAKE_BINARY_DIR}/src/runtime/libviper_runtime.a
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/games/chess-basic/chess.bas
            -DDEMO_NAME=chess
            -P ${_VIPER_E2E_DIR}/test_arm64_native_link.cmake)
    viper_set_test_labels(arm64_native_chess arm64 native_link)

    # Vtris demo - compile, assemble, link
    viper_add_ctest(arm64_native_vtris
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DRUNTIME_LIB=${CMAKE_BINARY_DIR}/src/runtime/libviper_runtime.a
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/games/vtris/vtris.bas
            -DDEMO_NAME=vtris
            -P ${_VIPER_E2E_DIR}/test_arm64_native_link.cmake)
    viper_set_test_labels(arm64_native_vtris arm64 native_link)

    # Frogger demo - compile, assemble, link
    viper_add_ctest(arm64_native_frogger
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DRUNTIME_LIB=${CMAKE_BINARY_DIR}/src/runtime/libviper_runtime.a
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/games/frogger-basic/frogger.bas
            -DDEMO_NAME=frogger
            -P ${_VIPER_E2E_DIR}/test_arm64_native_link.cmake)
    viper_set_test_labels(arm64_native_frogger arm64 native_link)

    # Centipede demo - compile, assemble, link
    viper_add_ctest(arm64_native_centipede
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DRUNTIME_LIB=${CMAKE_BINARY_DIR}/src/runtime/libviper_runtime.a
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/examples/games/centipede-basic/centipede.bas
            -DDEMO_NAME=centipede
            -P ${_VIPER_E2E_DIR}/test_arm64_native_link.cmake)
    viper_set_test_labels(arm64_native_centipede arm64 native_link)

    # Comprehensive tests - ARM64 native execution
    viper_add_ctest(arm64_comprehensive_data_structures
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DRUNTIME_LIB=${CMAKE_BINARY_DIR}/src/runtime/libviper_runtime.a
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/data_structures.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/data_structures.bas.out
            -P ${_VIPER_E2E_DIR}/test_arm64_comprehensive.cmake)
    viper_set_test_labels(arm64_comprehensive_data_structures arm64 native_link comprehensive)

    viper_add_ctest(arm64_comprehensive_oop_features
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DRUNTIME_LIB=${CMAKE_BINARY_DIR}/src/runtime/libviper_runtime.a
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/oop_features.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/oop_features.bas.out
            -P ${_VIPER_E2E_DIR}/test_arm64_comprehensive.cmake)
    viper_set_test_labels(arm64_comprehensive_oop_features arm64 native_link comprehensive)

    viper_add_ctest(arm64_comprehensive_control_flow_strings
            ${CMAKE_COMMAND}
            -DILC=$<TARGET_FILE:viper>
            -DRUNTIME_LIB=${CMAKE_BINARY_DIR}/src/runtime/libviper_runtime.a
            -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/control_flow_strings.bas
            -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/control_flow_strings.bas.out
            -P ${_VIPER_E2E_DIR}/test_arm64_comprehensive.cmake)
    viper_set_test_labels(arm64_comprehensive_control_flow_strings arm64 native_link comprehensive)
endif ()

# Comprehensive tests - VM execution (cross-platform)
viper_add_ctest(vm_comprehensive_data_structures
        ${CMAKE_COMMAND}
        -DILC=$<TARGET_FILE:viper>
        -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/data_structures.bas
        -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/data_structures.bas.out
        -P ${CMAKE_SOURCE_DIR}/src/tests/e2e/test_basic_math_phase1.cmake)
viper_set_test_labels(vm_comprehensive_data_structures vm comprehensive)

viper_add_ctest(vm_comprehensive_oop_features
        ${CMAKE_COMMAND}
        -DILC=$<TARGET_FILE:viper>
        -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/oop_features.bas
        -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/oop_features.bas.out
        -P ${CMAKE_SOURCE_DIR}/src/tests/e2e/test_basic_math_phase1.cmake)
viper_set_test_labels(vm_comprehensive_oop_features vm comprehensive)

viper_add_ctest(vm_comprehensive_control_flow_strings
        ${CMAKE_COMMAND}
        -DILC=$<TARGET_FILE:viper>
        -DBAS_FILE=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/control_flow_strings.bas
        -DGOLDEN=${CMAKE_SOURCE_DIR}/src/tests/e2e/comprehensive/control_flow_strings.bas.out
        -P ${CMAKE_SOURCE_DIR}/src/tests/e2e/test_basic_math_phase1.cmake)
viper_set_test_labels(vm_comprehensive_control_flow_strings vm comprehensive)

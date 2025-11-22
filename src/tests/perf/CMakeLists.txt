# SPDX-License-Identifier: GPL-3.0-only
# File: tests/perf/CMakeLists.txt
# Purpose: Performance-oriented regression checks.

viper_add_test(
        test_perf_vm_switch
        ${CMAKE_CURRENT_LIST_DIR}/vm_switch_bench.cpp
        TEST_NAME perf_vm_switch_bench)
target_link_libraries(test_perf_vm_switch PRIVATE il_build il_vm viper_support)

viper_add_test(
        test_perf_vm_dispatch
        ${CMAKE_CURRENT_LIST_DIR}/vm_dispatch_bench.cpp
        TEST_NAME perf_vm_dispatch_bench)
target_link_libraries(test_perf_vm_dispatch PRIVATE il_build il_vm viper_support)

# Runtime classes microbench (not registered with CTest by default)
viper_add_test(
        bench_runtime_classes_string_length
        ${CMAKE_CURRENT_LIST_DIR}/runtime_classes/bench_string_length.cpp
        NO_CTEST)
target_link_libraries(bench_runtime_classes_string_length PRIVATE fe_basic il_vm viper_support)

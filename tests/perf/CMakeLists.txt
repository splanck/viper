# SPDX-License-Identifier: MIT
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

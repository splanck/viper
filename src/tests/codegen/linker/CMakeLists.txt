# SPDX-License-Identifier: GPL-3.0-only
# File: tests/codegen/linker/CMakeLists.txt
# Purpose: Build unit tests for the native linker subsystem
#          (RelocApplier, SectionMerger, DeadStripPass).

set(_linker_tests
        reloc_applier
        section_merger
        dead_strip
        string_dedup
        symbol_resolver
        reloc_edge_cases
        elf_exe_writer
        linker_integration
        icf
        branch_trampoline
        debug_sections)

foreach (_case IN LISTS _linker_tests)
    set(_target test_linker_${_case})
    set(_source ${VIPER_TESTS_DIR}/codegen/linker/test_${_case}.cpp)

    add_executable(${_target} ${_source})
    if (WIN32)
        target_sources(${_target} PRIVATE
                ${VIPER_TESTS_DIR}/common/WinDialogSuppress.c)
    endif ()
    target_link_libraries(${_target} PRIVATE viper_testing viper_codegen_common)
    target_include_directories(${_target} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ${_target} COMMAND ${_target})
endforeach ()

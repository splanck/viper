# SPDX-License-Identifier: GPL-3.0-only
# File: tests/codegen/objfile/CMakeLists.txt
# Purpose: Build unit tests for the object file infrastructure
#          (StringTable, SymbolTable, CodeSection).

set(_objfile_tests
        string_table
        symbol_table
        code_section
        elf_writer
        macho_writer
        debug_line
        pe_writer)

foreach (_case IN LISTS _objfile_tests)
    set(_target test_objfile_${_case})
    set(_source ${VIPER_TESTS_DIR}/codegen/objfile/test_${_case}.cpp)

    add_executable(${_target} ${_source})
    if (WIN32)
        target_sources(${_target} PRIVATE
                ${VIPER_TESTS_DIR}/common/WinDialogSuppress.c)
    endif ()
    target_link_libraries(${_target} PRIVATE viper_testing viper_codegen_common)
    target_include_directories(${_target} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    viper_add_ctest(${_target} $<TARGET_FILE:${_target}>)
endforeach ()

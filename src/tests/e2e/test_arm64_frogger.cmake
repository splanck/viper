# End-to-end test for Frogger on ARM64
# Only run on macOS ARM64 or when ARM64_E2E_TESTS is set

if (NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    if (NOT DEFINED ENV{ARM64_E2E_TESTS})
        message(STATUS "Skipping ARM64 Frogger test (not on macOS ARM64 and ARM64_E2E_TESTS not set)")
        return()
    endif ()
endif ()

# Find required tool. The old standalone `ilc` binary was folded into the
# `zanna codegen` subcommand, so this test must use the checked-in CLI.
find_program(ZANNA_TOOL zanna PATHS ${CMAKE_BINARY_DIR}/src/tools/zanna NO_DEFAULT_PATH)

if (NOT ZANNA_TOOL)
    message(WARNING "Skipping Frogger ARM64 test: zanna tool not found")
    return()
endif ()

# Set up test paths
set(FROGGER_BAS "${CMAKE_SOURCE_DIR}/examples/games/frogger-basic/frogger.bas")
set(FROGGER_IL "${CMAKE_BINARY_DIR}/test_frogger.il")
set(FROGGER_ASM "${CMAKE_BINARY_DIR}/test_frogger.s")
set(FROGGER_EXE "${CMAKE_BINARY_DIR}/test_frogger_arm64")

# Test: Compile BASIC to IL
add_test(NAME arm64_frogger_compile_il
        COMMAND ${ZANNA_TOOL} front basic -emit-il ${FROGGER_BAS}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(arm64_frogger_compile_il PROPERTIES
        PASS_REGULAR_EXPRESSION "func @main")

add_test(NAME arm64_frogger_write_il
        COMMAND ${ZANNA_TOOL} build ${FROGGER_BAS} -o ${FROGGER_IL}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(arm64_frogger_write_il PROPERTIES DEPENDS arm64_frogger_compile_il)

# Test: Compile IL to ARM64 assembly
add_test(NAME arm64_frogger_compile_asm
        COMMAND ${ZANNA_TOOL} codegen arm64 ${FROGGER_IL} -S ${FROGGER_ASM}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(arm64_frogger_compile_asm PROPERTIES DEPENDS arm64_frogger_write_il)

# Test: Link to executable through the ARM64 codegen driver.
add_test(NAME arm64_frogger_link_exe
        COMMAND ${ZANNA_TOOL} codegen arm64 ${FROGGER_IL} -o ${FROGGER_EXE}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(arm64_frogger_link_exe PROPERTIES DEPENDS arm64_frogger_compile_asm)

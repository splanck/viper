# SPDX-License-Identifier: MIT
# File: tests/cmake/TestHelpers.cmake
# Purpose: Helper functions to keep the test CMake files concise.

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
endfunction()

function(viper_add_ctest name)
    if (ARGC EQUAL 2 AND "${name}" STREQUAL "${ARGV1}")
        return()
    endif ()
    add_test(NAME ${name} COMMAND ${ARGN})
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

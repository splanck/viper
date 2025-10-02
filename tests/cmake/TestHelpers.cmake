# SPDX-License-Identifier: MIT
# File: tests/cmake/TestHelpers.cmake
# Purpose: Helper functions to keep the test CMake files concise.

function(viper_add_test_exe target)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs SRCS)
  cmake_parse_arguments(VT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  if(VT_SRCS)
    set(_viper_sources ${VT_SRCS})
  else()
    set(_viper_sources ${VT_UNPARSED_ARGUMENTS})
  endif()
  if(NOT _viper_sources)
    message(FATAL_ERROR "viper_add_test_exe requires at least one source")
  endif()
  add_executable(${target} ${_viper_sources})
endfunction()

function(viper_add_ctest name)
  add_test(NAME ${name} COMMAND ${ARGN})
endfunction()

# Ensure no new golden directories sneak into the tree. Golden tests must rely on
# the shared suites under tests/golden/ rather than ad-hoc "goldens" folders.
function(viper_assert_no_goldens_directories)
  file(
    GLOB_RECURSE _viper_goldens
    LIST_DIRECTORIES true
    RELATIVE "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_SOURCE_DIR}/tests/*")

  list(FILTER _viper_goldens INCLUDE REGEX "/goldens$")

  if(_viper_goldens)
    list(TRANSFORM _viper_goldens PREPEND "${CMAKE_SOURCE_DIR}/tests/")
    list(JOIN _viper_goldens "\n  " _viper_goldens_pretty)
    message(
      FATAL_ERROR
      "Golden directories named 'goldens' are prohibited. Remove or rename:\n  ${_viper_goldens_pretty}")
  endif()
endfunction()

viper_assert_no_goldens_directories()

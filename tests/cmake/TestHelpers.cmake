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

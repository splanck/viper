# File: cmake/DistClean.cmake
# Purpose: Scrub CMake-generated files in this build tree.
# Key invariants: Removes common CMake cache and generator files while
# preserving the build directory itself.
# Ownership/Lifetime: Invoked via the 'distclean' custom target or directly
# with `cmake -P` from inside a build tree.
# Links: docs/build/cleaning.md

# DistClean.cmake — scrub CMake-generated files in this build tree.
message(STATUS "DistClean in: ${CMAKE_BINARY_DIR}")
set(_paths
  "${CMAKE_BINARY_DIR}/CMakeCache.txt"
  "${CMAKE_BINARY_DIR}/CMakeFiles"
  "${CMAKE_BINARY_DIR}/cmake_install.cmake"
  "${CMAKE_BINARY_DIR}/install_manifest.txt"
  "${CMAKE_BINARY_DIR}/Testing"
  "${CMAKE_BINARY_DIR}/Makefile"
  "${CMAKE_BINARY_DIR}/build.ninja"
  "${CMAKE_BINARY_DIR}/rules.ninja"
  "${CMAKE_BINARY_DIR}/.ninja_deps"
  "${CMAKE_BINARY_DIR}/.ninja_log"
)
foreach(p IN LISTS _paths)
  if (EXISTS "${p}" OR IS_SYMLINK "${p}")
    file(REMOVE_RECURSE "${p}")
  endif()
endforeach()
message(STATUS "DistClean done.")

#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/tools/ZannaStudioMacStagingTests.cmake
# Purpose: Verify the staged macOS Zanna Studio launcher/payload contract.
# Key invariants:
#   - The stable `zannastudio` command dispatches to a sibling `Zanna Studio`.
#   - Direct and launcher version invocations have identical observable output.
# Ownership/Lifetime: Owns and removes TEST_WORK_DIR; build outputs are read-only.
# Cross-platform touchpoints: Registered only by the Apple test branch.
# Links: docs/adr/0149-macos-zanna-studio-application-identity.md
#
#===----------------------------------------------------------------------===#

foreach(_required IN ITEMS ZANNASTUDIO_LAUNCHER ZANNASTUDIO_NATIVE TEST_WORK_DIR)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()
foreach(_required IN ITEMS ZANNASTUDIO_LAUNCHER ZANNASTUDIO_NATIVE)
  if(NOT EXISTS "${${_required}}")
    message(FATAL_ERROR "Required Zanna Studio output is missing: ${${_required}}")
  endif()
endforeach()

get_filename_component(_native_name "${ZANNASTUDIO_NATIVE}" NAME)
if(NOT _native_name STREQUAL "Zanna Studio")
  message(FATAL_ERROR
          "macOS native payload must be named 'Zanna Studio', got '${_native_name}'")
endif()

file(READ "${ZANNASTUDIO_LAUNCHER}" _launcher_text LIMIT 4096)
if(NOT _launcher_text MATCHES "exec.*Zanna Studio")
  message(FATAL_ERROR "zannastudio launcher does not dispatch to the authored payload")
endif()

execute_process(
  COMMAND "${ZANNASTUDIO_NATIVE}" --version
  RESULT_VARIABLE _native_rv
  OUTPUT_VARIABLE _native_out
  ERROR_VARIABLE _native_err
  TIMEOUT 10)
if(NOT _native_rv EQUAL 0)
  message(FATAL_ERROR
          "native Zanna Studio --version failed\nstdout:\n${_native_out}\nstderr:\n${_native_err}")
endif()

execute_process(
  COMMAND "${ZANNASTUDIO_LAUNCHER}" --version
  RESULT_VARIABLE _launcher_rv
  OUTPUT_VARIABLE _launcher_out
  ERROR_VARIABLE _launcher_err
  TIMEOUT 10)
if(NOT _launcher_rv EQUAL 0)
  message(FATAL_ERROR
          "zannastudio launcher --version failed\nstdout:\n${_launcher_out}\nstderr:\n${_launcher_err}")
endif()

if(NOT "${_launcher_out}" STREQUAL "${_native_out}" OR
   NOT "${_launcher_err}" STREQUAL "${_native_err}")
  message(FATAL_ERROR "zannastudio launcher did not preserve native output")
endif()
if(NOT _launcher_out MATCHES "^Zanna Studio ")
  message(FATAL_ERROR "Zanna Studio version output does not use the authored product name")
endif()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")
set(_launcher_symlink "${TEST_WORK_DIR}/zannastudio")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E create_symlink
          "${ZANNASTUDIO_LAUNCHER}" "${_launcher_symlink}"
  RESULT_VARIABLE _symlink_rv)
if(NOT _symlink_rv EQUAL 0)
  message(FATAL_ERROR "failed to create the installed-command symlink probe")
endif()

execute_process(
  COMMAND "${_launcher_symlink}" --version
  RESULT_VARIABLE _symlink_launch_rv
  OUTPUT_VARIABLE _symlink_launch_out
  ERROR_VARIABLE _symlink_launch_err
  TIMEOUT 10)
file(REMOVE_RECURSE "${TEST_WORK_DIR}")
if(NOT _symlink_launch_rv EQUAL 0)
  message(FATAL_ERROR
          "symlinked zannastudio launcher failed\n"
          "stdout:\n${_symlink_launch_out}\nstderr:\n${_symlink_launch_err}")
endif()
if(NOT "${_symlink_launch_out}" STREQUAL "${_native_out}" OR
   NOT "${_symlink_launch_err}" STREQUAL "${_native_err}")
  message(FATAL_ERROR "symlinked zannastudio launcher did not preserve native output")
endif()

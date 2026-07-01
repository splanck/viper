#===----------------------------------------------------------------------===//
#
# Part of the Viper project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//
#
# File: cmake/WriteViperIDEBuildInfo.cmake
# Purpose: Write ViperIDE standalone build metadata next to the native binary.
#
#===----------------------------------------------------------------------===//

if(NOT DEFINED OUTPUT_FILE OR "${OUTPUT_FILE}" STREQUAL "")
  message(FATAL_ERROR "OUTPUT_FILE is required")
endif()
if(NOT DEFINED ROOT_DIR OR "${ROOT_DIR}" STREQUAL "")
  message(FATAL_ERROR "ROOT_DIR is required")
endif()
if(NOT DEFINED VIPER_PATH)
  set(VIPER_PATH "")
endif()

get_filename_component(_output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")
set(_info_path "${_output_dir}/viperide.buildinfo")

string(TIMESTAMP _timestamp "%Y-%m-%dT%H:%M:%SZ" UTC)
set(_revision "unknown")
find_program(_git_executable NAMES git)
if(_git_executable)
  execute_process(
    COMMAND "${_git_executable}" -C "${ROOT_DIR}" rev-parse --short HEAD
    RESULT_VARIABLE _git_rev_rv
    OUTPUT_VARIABLE _git_rev
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(_git_rev_rv EQUAL 0 AND NOT "${_git_rev}" STREQUAL "")
    set(_revision "${_git_rev}")
  endif()

  execute_process(
    COMMAND "${_git_executable}" -C "${ROOT_DIR}" diff --quiet --ignore-submodules --
    RESULT_VARIABLE _git_dirty_rv
    OUTPUT_QUIET
    ERROR_QUIET)
  if(_git_dirty_rv EQUAL 1)
    string(APPEND _revision " dirty")
  endif()
endif()

file(WRITE "${_info_path}"
  "Build: ${_timestamp}\n"
  "Source: ${_revision}\n"
  "Output: ${OUTPUT_FILE}\n"
  "Viper: ${VIPER_PATH}\n")

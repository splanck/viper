#===----------------------------------------------------------------------===//
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//
#
# File: cmake/WriteZannaStudioBuildInfo.cmake
# Purpose: Write Zanna Studio standalone build metadata next to the native binary.
#
#===----------------------------------------------------------------------===//

if(NOT DEFINED OUTPUT_FILE OR "${OUTPUT_FILE}" STREQUAL "")
  message(FATAL_ERROR "OUTPUT_FILE is required")
endif()
if(NOT DEFINED ROOT_DIR OR "${ROOT_DIR}" STREQUAL "")
  message(FATAL_ERROR "ROOT_DIR is required")
endif()
if(NOT DEFINED ZANNA_PATH)
  set(ZANNA_PATH "")
endif()
if(NOT DEFINED ZANNA_VERSION OR "${ZANNA_VERSION}" STREQUAL "")
  file(READ "${ROOT_DIR}/src/buildmeta/VERSION" ZANNA_VERSION)
  string(STRIP "${ZANNA_VERSION}" ZANNA_VERSION)
endif()
if("${ZANNA_VERSION}" STREQUAL "")
  set(ZANNA_VERSION "unknown")
endif()

get_filename_component(_output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")
set(_info_path "${_output_dir}/zannastudio.buildinfo")

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
  "Zanna Studio ${ZANNA_VERSION}\n"
  "Build: ${_timestamp}\n"
  "Source: ${_revision}\n"
  "Output: ${OUTPUT_FILE}\n"
  "Zanna: ${ZANNA_PATH}\n")

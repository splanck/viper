#===----------------------------------------------------------------------===//
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//
#
# File: cmake/WriteZannaStudioBuildInfo.cmake
# Purpose: Write Zanna Studio standalone build metadata next to the native binary.
# Key invariants:
#   - Schema-1 metadata binds the exact binary size, SHA-256, and architecture.
#   - The complete metadata file is staged before it replaces an older record.
# Ownership/Lifetime: The output directory owns the published buildinfo file.
# Links: CMakeLists.txt, scripts/build_ide_win.ps1, WindowsPackageBuilder.cpp
#
#===----------------------------------------------------------------------===//

if(NOT DEFINED OUTPUT_FILE OR "${OUTPUT_FILE}" STREQUAL "")
  message(FATAL_ERROR "OUTPUT_FILE is required")
endif()
if(NOT DEFINED ROOT_DIR OR "${ROOT_DIR}" STREQUAL "")
  message(FATAL_ERROR "ROOT_DIR is required")
endif()
if(NOT DEFINED ZANNA_ARCH OR
   (NOT "${ZANNA_ARCH}" STREQUAL "x64" AND NOT "${ZANNA_ARCH}" STREQUAL "arm64"))
  message(FATAL_ERROR "ZANNA_ARCH must be x64 or arm64")
endif()
set(_binary_input "${OUTPUT_FILE}")
if(DEFINED STAGED_OUTPUT_FILE AND NOT "${STAGED_OUTPUT_FILE}" STREQUAL "")
  set(_binary_input "${STAGED_OUTPUT_FILE}")
endif()
if(NOT EXISTS "${_binary_input}" OR IS_DIRECTORY "${_binary_input}")
  message(FATAL_ERROR "Zanna Studio binary input is not a regular file: ${_binary_input}")
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
    COMMAND "${_git_executable}" -C "${ROOT_DIR}" status --porcelain --untracked-files=normal
    RESULT_VARIABLE _git_dirty_rv
    OUTPUT_VARIABLE _git_dirty_output
    ERROR_QUIET)
  if(NOT _git_dirty_rv EQUAL 0 OR NOT "${_git_dirty_output}" STREQUAL "")
    string(APPEND _revision " dirty")
  endif()
endif()

foreach(_single_line IN ITEMS "${OUTPUT_FILE}" "${ZANNA_PATH}" "${_revision}")
  if(_single_line MATCHES "[\r\n]")
    message(FATAL_ERROR "Zanna Studio metadata fields must not contain line breaks")
  endif()
endforeach()
file(SIZE "${_binary_input}" _output_size)
if(_output_size LESS 1)
  message(FATAL_ERROR "Zanna Studio binary input is empty: ${_binary_input}")
endif()
file(SHA256 "${_binary_input}" _output_sha256)
string(TOLOWER "${_output_sha256}" _output_sha256)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef _metadata_token)
set(_temporary_info
    "${_output_dir}/.zannastudio-buildinfo-${_metadata_token}.tmp")
file(WRITE "${_temporary_info}"
  "Zanna Studio ${ZANNA_VERSION}\n"
  "Schema: 1\n"
  "Build: ${_timestamp}\n"
  "Source: ${_revision}\n"
  "Architecture: ${ZANNA_ARCH}\n"
  "Size: ${_output_size}\n"
  "SHA256: ${_output_sha256}\n"
  "Output: ${OUTPUT_FILE}\n"
  "Zanna: ${ZANNA_PATH}\n")
if(DEFINED STAGED_OUTPUT_FILE AND NOT "${STAGED_OUTPUT_FILE}" STREQUAL "")
  file(RENAME "${STAGED_OUTPUT_FILE}" "${OUTPUT_FILE}")
endif()
file(RENAME "${_temporary_info}" "${_info_path}")

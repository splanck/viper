# Update VIPER_IL_VERSION_STR in the generated version header without Python.
# Usage:
#   cmake -DIL_VERSION_FILE=path/to/src/buildmeta/IL_VERSION \
#         -DHEADER_FILE=path/to/build/generated/viper/version.hpp \
#         -P cmake/BumpIlVersion.cmake

if(NOT DEFINED IL_VERSION_FILE)
  message(FATAL_ERROR "BumpIlVersion.cmake: IL_VERSION_FILE is not set")
endif()
if(NOT DEFINED HEADER_FILE)
  message(FATAL_ERROR "BumpIlVersion.cmake: HEADER_FILE is not set")
endif()

if(NOT EXISTS "${IL_VERSION_FILE}")
  message(FATAL_ERROR "BumpIlVersion.cmake: IL_VERSION_FILE not found: ${IL_VERSION_FILE}")
endif()

if(NOT EXISTS "${HEADER_FILE}")
  message(WARNING "BumpIlVersion.cmake: HEADER_FILE not found: ${HEADER_FILE}; nothing to update")
  return()
endif()

file(READ "${IL_VERSION_FILE}" _il_raw)
string(STRIP "${_il_raw}" _il_ver)

file(READ "${HEADER_FILE}" _hdr)

set(_pattern "#define[ \t]+VIPER_IL_VERSION_STR[ \t]+\"[^\"]*\"")
set(_replacement "#define VIPER_IL_VERSION_STR \"${_il_ver}\"")
string(REGEX REPLACE "${_pattern}" "${_replacement}" _new_hdr "${_hdr}")

if(NOT _new_hdr STREQUAL _hdr)
  file(WRITE "${HEADER_FILE}" "${_new_hdr}")
  message(STATUS "Updated VIPER_IL_VERSION_STR to ${_il_ver}")
else()
  message(STATUS "VIPER_IL_VERSION_STR already up-to-date (${_il_ver})")
endif()


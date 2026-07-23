#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: cmake/StageZannaStudioMac.cmake
# Purpose: Stage the macOS Zanna Studio payload and compatibility launcher.
# Key invariants:
#   - NATIVE_BINARY has the leaf name `Zanna Studio`.
#   - LAUNCHER remains the stable `zannastudio` command-line entry point.
#   - Optional compatibility outputs contain the same native payload and launcher.
# Ownership/Lifetime: The caller owns all generated output paths.
# Cross-platform touchpoints: Invoked only by Apple build branches.
# Links: cmake/ZannaStudioMacLauncher.sh,
#        docs/adr/0149-macos-zanna-studio-application-identity.md
#
#===----------------------------------------------------------------------===#

foreach(_required IN ITEMS NATIVE_BINARY LAUNCHER LAUNCHER_TEMPLATE)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

if(NOT EXISTS "${NATIVE_BINARY}")
  message(FATAL_ERROR "Zanna Studio native payload does not exist: ${NATIVE_BINARY}")
endif()
if(NOT EXISTS "${LAUNCHER_TEMPLATE}")
  message(FATAL_ERROR "Zanna Studio launcher template does not exist: ${LAUNCHER_TEMPLATE}")
endif()
get_filename_component(_native_name "${NATIVE_BINARY}" NAME)
if(NOT _native_name STREQUAL "Zanna Studio")
  message(FATAL_ERROR
          "Zanna Studio native payload must have the leaf name 'Zanna Studio'")
endif()
if("${NATIVE_BINARY}" STREQUAL "${LAUNCHER}")
  message(FATAL_ERROR "Zanna Studio native payload and launcher paths must differ")
endif()

get_filename_component(_launcher_dir "${LAUNCHER}" DIRECTORY)
file(MAKE_DIRECTORY "${_launcher_dir}")
file(REMOVE "${LAUNCHER}")
configure_file("${LAUNCHER_TEMPLATE}" "${LAUNCHER}" COPYONLY)
file(CHMOD "${LAUNCHER}"
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                 GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

set(_has_compat_native FALSE)
set(_has_compat_launcher FALSE)
if(DEFINED COMPAT_NATIVE_BINARY AND NOT "${COMPAT_NATIVE_BINARY}" STREQUAL "")
  set(_has_compat_native TRUE)
endif()
if(DEFINED COMPAT_LAUNCHER AND NOT "${COMPAT_LAUNCHER}" STREQUAL "")
  set(_has_compat_launcher TRUE)
endif()
if((_has_compat_native AND NOT _has_compat_launcher) OR
   (_has_compat_launcher AND NOT _has_compat_native))
  message(FATAL_ERROR
          "COMPAT_NATIVE_BINARY and COMPAT_LAUNCHER must be provided together")
endif()

if(_has_compat_native)
  get_filename_component(_compat_native_name "${COMPAT_NATIVE_BINARY}" NAME)
  if(NOT _compat_native_name STREQUAL "Zanna Studio")
    message(FATAL_ERROR
            "Zanna Studio compatibility payload must have the leaf name 'Zanna Studio'")
  endif()
  if("${COMPAT_NATIVE_BINARY}" STREQUAL "${COMPAT_LAUNCHER}")
    message(FATAL_ERROR "Compatibility native payload and launcher paths must differ")
  endif()
  get_filename_component(_compat_dir "${COMPAT_NATIVE_BINARY}" DIRECTORY)
  file(MAKE_DIRECTORY "${_compat_dir}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${NATIVE_BINARY}" "${COMPAT_NATIVE_BINARY}"
    RESULT_VARIABLE _copy_rv)
  if(NOT _copy_rv EQUAL 0)
    message(FATAL_ERROR "Failed to stage the Zanna Studio compatibility payload")
  endif()
  file(REMOVE "${COMPAT_LAUNCHER}")
  configure_file("${LAUNCHER_TEMPLATE}" "${COMPAT_LAUNCHER}" COPYONLY)
  file(CHMOD "${COMPAT_LAUNCHER}"
       PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                   GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

cmake_minimum_required(VERSION 3.20)
# ---- Begin: Viper version wiring (SSOT + snapshots) ----
# Read SSOT files from repo root
file(READ "${CMAKE_SOURCE_DIR}/src/buildmeta/VERSION"     VIPER_VERSION_STR)
string(STRIP "${VIPER_VERSION_STR}"         VIPER_VERSION_STR)
file(READ "${CMAKE_SOURCE_DIR}/src/buildmeta/IL_VERSION"  VIPER_IL_VERSION_STR)
string(STRIP "${VIPER_IL_VERSION_STR}"      VIPER_IL_VERSION_STR)

# Extract numeric MAJOR.MINOR.PATCH for CMake's project() VERSION
string(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\1" VIPER_VERSION_MAJOR "${VIPER_VERSION_STR}")
string(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\2" VIPER_VERSION_MINOR "${VIPER_VERSION_STR}")
string(REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\3" VIPER_VERSION_PATCH "${VIPER_VERSION_STR}")
set(VIPER_SEMVER_NUM "${VIPER_VERSION_MAJOR}.${VIPER_VERSION_MINOR}.${VIPER_VERSION_PATCH}")

# Compute snapshot string from git (empty if unavailable)
find_package(Git QUIET)
set(VIPER_SNAPSHOT_STR "")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --match "v[0-9]*" --long --dirty
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE _git_r
    OUTPUT_VARIABLE  _git_out
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(_git_r EQUAL 0)
    string(REGEX REPLACE "^v" "" VIPER_SNAPSHOT_STR "${_git_out}")  # drop leading 'v'
  endif()
endif()
# ---- End: Viper version wiring ----
project(Viper LANGUAGES CXX VERSION ${VIPER_SEMVER_NUM})
enable_language(C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Runtime namespace transition flag: dual-publish legacy rt_* aliases alongside
# canonical Viper.* names. Default ON for a smoother migration window.
option(VIPER_RUNTIME_NS_DUAL "Publish legacy rt_* runtime aliases alongside Viper.*" ON)

# Compute numeric define for headers and C++ sources (1/0)
if(VIPER_RUNTIME_NS_DUAL)
  set(VIPER_RUNTIME_NS_DUAL_DEFINE 1)
else()
  set(VIPER_RUNTIME_NS_DUAL_DEFINE 0)
endif()

# Emit the generated version header
configure_file(
  "${CMAKE_SOURCE_DIR}/include/viper/version.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/viper/version.hpp"
  @ONLY
)
install(FILES "${CMAKE_BINARY_DIR}/generated/viper/version.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper)

# No external scripting dependencies required (Python removed).

option(VIPER_INSTALL_TUI "Install TUI apps/libraries" OFF)
option(VIPER_BUILD_TESTING "Enable CTest-based tests" ON)
option(VIPER_VM_THREADED "Enable threaded VM dispatch" OFF)
option(VIPER_VM_TAILCALL "Enable tailcall" ON)
option(VIPER_VM_OPCOUNTS "Enable op counters" ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to strict builds: treat warnings as errors unless opted out.
option(IL_WARN_AS_ERROR "Treat warnings as errors" ON)
option(IL_USE_LLD "Use lld linker if available" ON)
option(IL_USE_LIBCXX "Use libc++ on Clang (Linux)" OFF)
option(IL_SANITIZE_ADDRESS "Enable address sanitizer" OFF)
option(IL_SANITIZE_UNDEFINED "Enable undefined behavior sanitizer" OFF)

set(IL_COMPILE_FLAGS -Wall -Wextra -Wpedantic)
set(IL_LINK_FLAGS)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND IL_COMPILE_FLAGS -Wno-unused-parameter -Wno-unused-private-field -Wno-mismatched-tags)
  if(IL_SANITIZE_ADDRESS)
    list(APPEND IL_COMPILE_FLAGS -fsanitize=address)
    list(APPEND IL_LINK_FLAGS -fsanitize=address)
  endif()
  if(IL_SANITIZE_UNDEFINED)
    list(APPEND IL_COMPILE_FLAGS -fsanitize=undefined)
    list(APPEND IL_LINK_FLAGS -fsanitize=undefined)
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Keep parity with Clang: suppress benign unused parameter warnings under -Werror
  list(APPEND IL_COMPILE_FLAGS -Wno-unused-parameter)
endif()

if(IL_WARN_AS_ERROR)
  list(APPEND IL_COMPILE_FLAGS -Werror)
endif()

# Test code often has variables/functions/types used only in assertions (compiled out in release builds)
if(VIPER_BUILD_TESTING)
  list(APPEND IL_COMPILE_FLAGS -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-lambda-capture -Wno-unused-local-typedef)
endif()

if(IL_USE_LLD)
  include(CheckLinkerFlag)
  check_linker_flag(CXX "-fuse-ld=lld" IL_HAS_LLD)
  if(IL_HAS_LLD)
    list(APPEND IL_LINK_FLAGS -fuse-ld=lld)
  endif()
endif()

add_compile_definitions(
  VIPER_ENABLE_OOP=1
  VIPER_VM_TAILCALL=$<BOOL:${VIPER_VM_TAILCALL}>
  VIPER_VM_OPCOUNTS=$<BOOL:${VIPER_VM_OPCOUNTS}>
  VIPER_RUNTIME_NS_DUAL=$<BOOL:${VIPER_RUNTIME_NS_DUAL}>
)

if(IL_USE_LIBCXX AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT APPLE)
  list(APPEND IL_COMPILE_FLAGS -stdlib=libc++)
  list(APPEND IL_LINK_FLAGS -stdlib=libc++)
endif()

add_library(viper_common_opts INTERFACE)
# Generated headers live here (configured below)
target_include_directories(viper_common_opts INTERFACE "${CMAKE_BINARY_DIR}/generated")
target_compile_features(viper_common_opts INTERFACE cxx_std_20)
if(IL_COMPILE_FLAGS)
  target_compile_options(viper_common_opts INTERFACE ${IL_COMPILE_FLAGS})
endif()

set(_viper_link_opts ${IL_LINK_FLAGS})
list(FILTER _viper_link_opts EXCLUDE REGEX "^$")
if(_viper_link_opts)
  target_link_options(viper_common_opts INTERFACE ${_viper_link_opts})
endif()

if(APPLE)
  target_link_options(viper_common_opts INTERFACE -Wl,-no_warn_duplicate_libraries)
endif()

function(viper_assert_no_directory_link_libraries dir label)
  if("${dir}" STREQUAL ".")
    get_property(_dir_link_libs DIRECTORY PROPERTY LINK_LIBRARIES)
  else()
    get_property(_dir_link_libs DIRECTORY "${dir}" PROPERTY LINK_LIBRARIES)
  endif()
  if(_dir_link_libs)
    message(FATAL_ERROR
      "Directory-scope link_libraries() detected in ${label}. "
      "Please link libraries on their owning targets instead. "
      "LINK_LIBRARIES=${_dir_link_libs}")
  endif()
endfunction()

function(viper_assert_no_archive_link_paths)
  get_property(_viper_all_targets GLOBAL PROPERTY TARGETS)
  if(NOT _viper_all_targets)
    return()
  endif()

  set(_viper_archive_offenders)
  foreach(_viper_target IN LISTS _viper_all_targets)
    foreach(_viper_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(_viper_links "${_viper_target}" "${_viper_prop}")
      if(NOT _viper_links)
        continue()
      endif()

      foreach(_viper_entry IN LISTS _viper_links)
        if(NOT _viper_entry)
          continue()
        endif()

        if(_viper_entry STREQUAL "optimized" OR _viper_entry STREQUAL "debug" OR _viper_entry STREQUAL "general")
          continue()
        endif()

        if(_viper_entry MATCHES "^\\$<")
          continue()
        endif()

        string(REPLACE "\\" "/" _viper_normalized "${_viper_entry}")
        if(_viper_normalized MATCHES "/[^/]+\\.a$")
          list(APPEND _viper_archive_offenders "${_viper_target} -> ${_viper_entry}")
        endif()
      endforeach()
    endforeach()
  endforeach()

  if(_viper_archive_offenders)
    list(REMOVE_DUPLICATES _viper_archive_offenders)
    list(JOIN _viper_archive_offenders "\n  " _viper_archive_message)
    message(FATAL_ERROR
      "Targets linked against archive file paths:\n  ${_viper_archive_message}\n"
      "Link against the producing CMake target or wrap the archive with add_library(IMPORTED).")
  endif()
endfunction()

file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
     src/*.cpp src/*.hpp
     src/runtime/*.cpp src/runtime/*.hpp
     src/runtime/*.c src/runtime/*.h)

add_custom_target(format
  COMMAND clang-format -i ${ALL_CXX_SOURCE_FILES}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Convenience cleanup: clean build tree and remove source-generated artifacts.
add_custom_target(clean-all
  COMMAND ${CMAKE_COMMAND} --build . --target clean
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/clean-source.cmake
  COMMENT "Clean build tree and remove source-generated files")

# CI-safe helper: normalize IL headers to current IL_VERSION
add_custom_target(bump_il
  COMMAND "${CMAKE_SOURCE_DIR}/scripts/rel/bump_il_version.py"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Normalize IL headers to IL_VERSION"
)

# Stronger cleanup that also removes built binaries and CMake cache/files.
add_custom_target(clean-dist
  COMMAND ${CMAKE_COMMAND} --build . --target clean
  COMMAND ${CMAKE_COMMAND} -E rm -rf
          "${CMAKE_BINARY_DIR}/bin"
          "${CMAKE_BINARY_DIR}/lib"
          "${CMAKE_BINARY_DIR}/src"
          "${CMAKE_BINARY_DIR}/tui"
          "${CMAKE_BINARY_DIR}/generated"
          "${CMAKE_BINARY_DIR}/compile_commands.json"
  COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_BINARY_DIR}/CMakeCache.txt"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/CMakeFiles"
  COMMENT "Remove all build artifacts (including binaries) and CMake cache/files"
)

option(IL_ENABLE_X64_ASM_SYNTAX_CHECK "Check generated x86_64 asm syntax" ON)
option(IL_ENABLE_X64_ASM_ASSEMBLE_LINK "Assemble+link x86_64 asm" ON)
option(IL_ENABLE_X64_NATIVE_RUN "Run x86_64 binaries" ON)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
  set(IL_ENABLE_X64_NATIVE_RUN OFF CACHE BOOL "" FORCE)
endif()

if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
  set(IL_ENABLE_X64_ASM_ASSEMBLE_LINK OFF CACHE BOOL "" FORCE)
endif()

set(IL_X64_ASM_FLAGS "" CACHE STRING "Extra flags for assembling x86_64")
set(IL_X64_LD_FLAGS  "" CACHE STRING "Extra flags for linking x86_64")
if(APPLE)
  set(IL_X64_ASM_FLAGS "-arch x86_64 -mmacosx-version-min=12.0" CACHE STRING "" FORCE)
  set(IL_X64_LD_FLAGS  "-arch x86_64 -mmacosx-version-min=12.0" CACHE STRING "" FORCE)
endif()

find_program(CLANG_EXECUTABLE NAMES clang clang-17 clang-16 clang-15)
set(CLANG_FOUND OFF)
if(CLANG_EXECUTABLE)
  set(CLANG_FOUND ON)
endif()
message(STATUS "CLANG_FOUND=${CLANG_FOUND} (${CLANG_EXECUTABLE})")

message(STATUS "IL_ENABLE_X64_ASM_SYNTAX_CHECK=${IL_ENABLE_X64_ASM_SYNTAX_CHECK}")
message(STATUS "IL_ENABLE_X64_ASM_ASSEMBLE_LINK=${IL_ENABLE_X64_ASM_ASSEMBLE_LINK}")
message(STATUS "IL_ENABLE_X64_NATIVE_RUN=${IL_ENABLE_X64_NATIVE_RUN}")
message(STATUS "IL_X64_ASM_FLAGS=${IL_X64_ASM_FLAGS}")
message(STATUS "IL_X64_LD_FLAGS=${IL_X64_LD_FLAGS}")

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Warn-as-error: ${IL_WARN_AS_ERROR}")
message(STATUS "Linker: lld=${IL_USE_LLD}")
message(STATUS "VIPER_VM_THREADED=${VIPER_VM_THREADED}")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(STATUS "Stdlib (Clang): libc++=${IL_USE_LIBCXX}")
endif()

if(VIPER_BUILD_TESTING)
  enable_testing()
  add_test(
    NAME basic_select_case_string
    COMMAND ${CMAKE_SOURCE_DIR}/src/tests/integration/basic/select_case_string.sh
            ${CMAKE_BINARY_DIR}
  )
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/src/runtime)
viper_assert_no_directory_link_libraries("src/runtime" "src/runtime")

add_subdirectory(src)
viper_assert_no_directory_link_libraries("src" "src")

# ---- ViperTUI subproject ----
option(BUILD_TUI "Build ViperTUI subproject" ON)
if(VIPER_INSTALL_TUI AND NOT BUILD_TUI)
  message(WARNING "VIPER_INSTALL_TUI is ON but BUILD_TUI is OFF; no TUI targets will be installed.")
endif()
if(BUILD_TUI)
  add_subdirectory(src/tui)
  viper_assert_no_directory_link_libraries("src/tui" "src/tui")
endif()

if(VIPER_BUILD_TESTING)
  add_subdirectory(src/tests)
  viper_assert_no_directory_link_libraries("src/tests" "src/tests")
endif()

# ---- Examples ----
option(BUILD_EXAMPLES "Build C++ examples" ON)
if(BUILD_EXAMPLES)
  add_executable(example_vm_stepping examples/stepping/stepping_example.cpp)
  target_link_libraries(example_vm_stepping PRIVATE viper_common_opts il_build il_vm viper_support)
  target_include_directories(example_vm_stepping PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)

  add_executable(example_vm_register_times2 examples/externs/register_times2.cpp)
  target_link_libraries(example_vm_register_times2 PRIVATE viper_common_opts il_build il_vm viper_support)
  target_include_directories(example_vm_register_times2 PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)

  add_executable(example_vm_smoke_combined examples/smoke/combined.cpp)
  target_link_libraries(example_vm_smoke_combined PRIVATE viper_common_opts il_build il_vm viper_support)
  target_include_directories(example_vm_smoke_combined PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
endif()

# Man pages (section 1)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/docs/man/
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
        FILES_MATCHING PATTERN "*.1")

# Uninstall convenience target (uses install_manifest.txt)
if(NOT TARGET uninstall)
  configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
    ${CMAKE_BINARY_DIR}/cmake_uninstall.cmake
    @ONLY)
  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake_uninstall.cmake
    COMMENT "Uninstall installed files (from install_manifest.txt)")
endif()

# ---- Packaging (CPack) ----
set(CPACK_PACKAGE_NAME "Viper")
set(CPACK_PACKAGE_VENDOR "Viper Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Viper IL compiler toolchain and VM")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_CONTACT "viper@example.com")

if(APPLE)
  # Build a macOS installer package (.pkg) suitable for CLI tools
  set(CPACK_GENERATOR "productbuild")
  set(CPACK_PACKAGE_FILE_NAME "Viper-${PROJECT_VERSION}-macos")
else()
  if(UNIX)
    # Build both DEB and RPM if possible; users can choose
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Viper Project")
    set(CPACK_PACKAGE_FILE_NAME "viper-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  endif()
endif()

include(CPack)

viper_assert_no_archive_link_paths()

add_custom_target(distclean
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/cmake/DistClean.cmake"
  COMMENT "Scrub CMake-generated files in this build tree"
)

set(VIPER_PUBLIC_LIB_TARGETS
  viper_support
  viper_runtime
  viper_pass
  viper_il_core
  il_runtime
  il_build
  viper_il_io
  viper_il_verify
  viper_il_full
  il_analysis
  il_transform
  il_utils
  il_vm
  fe_basic
  il_api
)

install(TARGETS ${VIPER_PUBLIC_LIB_TARGETS}
  EXPORT ViperTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/viper
)

set(VIPER_CLI_TARGETS ilc il-verify il-dis)
install(TARGETS ${VIPER_CLI_TARGETS}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

export(EXPORT ViperTargets
  NAMESPACE viper::
  FILE "${CMAKE_CURRENT_BINARY_DIR}/ViperTargets.cmake"
)

install(EXPORT ViperTargets
  NAMESPACE viper::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Viper
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/ViperConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_LIST_DIR}/cmake/ViperConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/ViperConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Viper
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/ViperConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/ViperConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Viper
)

set(CPACK_PACKAGE_NAME "Viper")
set(CPACK_PACKAGE_VENDOR "Viper Project")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)

# Silence benign duplicate-library warnings on macOS
option(VIPER_SUPPRESS_DUPLIB_WARN "Suppress ld64 duplicate library warnings" ON)
if(APPLE AND VIPER_SUPPRESS_DUPLIB_WARN)
  add_link_options(-Wl,-no_warn_duplicate_libraries)
endif()

viper_assert_no_directory_link_libraries("." "root project")

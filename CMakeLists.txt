cmake_minimum_required(VERSION 3.16)
project(ilc LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IL_WARN_AS_ERROR "Treat warnings as errors" OFF)
option(IL_USE_LLD "Use lld linker if available" ON)
option(IL_USE_LIBCXX "Use libc++ on Clang (Linux)" OFF)
option(IL_SANITIZE_ADDRESS "Enable address sanitizer" OFF)
option(IL_SANITIZE_UNDEFINED "Enable undefined behavior sanitizer" OFF)

set(IL_COMPILE_FLAGS -Wall -Wextra -Wpedantic)
set(IL_LINK_FLAGS "")

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
  # GCC-specific flags can be added here if needed
endif()

if(IL_WARN_AS_ERROR)
  list(APPEND IL_COMPILE_FLAGS -Werror)
endif()

if(IL_USE_LLD)
  include(CheckLinkerFlag)
  check_linker_flag(CXX "-fuse-ld=lld" IL_HAS_LLD)
  if(IL_HAS_LLD)
    list(APPEND IL_LINK_FLAGS -fuse-ld=lld)
  endif()
endif()

if(IL_USE_LIBCXX AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT APPLE)
  list(APPEND IL_COMPILE_FLAGS -stdlib=libc++)
  list(APPEND IL_LINK_FLAGS -stdlib=libc++)
endif()

add_compile_options(${IL_COMPILE_FLAGS})
add_link_options(${IL_LINK_FLAGS})

file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
     src/*.cpp src/*.hpp
     runtime/*.cpp runtime/*.hpp)

add_custom_target(format
  COMMAND clang-format -i ${ALL_CXX_SOURCE_FILES}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
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

message(STATUS "IL_ENABLE_X64_ASM_SYNTAX_CHECK=${IL_ENABLE_X64_ASM_SYNTAX_CHECK}")
message(STATUS "IL_ENABLE_X64_ASM_ASSEMBLE_LINK=${IL_ENABLE_X64_ASM_ASSEMBLE_LINK}")
message(STATUS "IL_ENABLE_X64_NATIVE_RUN=${IL_ENABLE_X64_NATIVE_RUN}")
message(STATUS "IL_X64_ASM_FLAGS=${IL_X64_ASM_FLAGS}")
message(STATUS "IL_X64_LD_FLAGS=${IL_X64_LD_FLAGS}")

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Warn-as-error: ${IL_WARN_AS_ERROR}")
message(STATUS "Linker: lld=${IL_USE_LLD}")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(STATUS "Stdlib (Clang): libc++=${IL_USE_LIBCXX}")
endif()

enable_testing()

add_subdirectory(runtime)
add_subdirectory(src)
add_subdirectory(lib/IL)
add_subdirectory(lib/Analysis)
add_subdirectory(tests)

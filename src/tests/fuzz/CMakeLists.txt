# SPDX-License-Identifier: GPL-3.0-only
# File: tests/fuzz/CMakeLists.txt
# Purpose: Build libFuzzer harnesses for Zia lexer and parser.
#          Gated on VIPER_ENABLE_FUZZ=ON; not registered as ctest targets.

if (NOT VIPER_ENABLE_FUZZ)
    return()
endif ()

# ── Zia Lexer Fuzzer ─────────────────────────────────────────────────────────
add_executable(fuzz_zia_lexer fuzz_zia_lexer.cpp)
target_include_directories(fuzz_zia_lexer PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fuzz_zia_lexer PRIVATE fe_zia)
target_compile_options(fuzz_zia_lexer PRIVATE -fsanitize=fuzzer)
target_link_options(fuzz_zia_lexer PRIVATE -fsanitize=fuzzer)

# ── Zia Parser Fuzzer ────────────────────────────────────────────────────────
add_executable(fuzz_zia_parser fuzz_zia_parser.cpp)
target_include_directories(fuzz_zia_parser PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fuzz_zia_parser PRIVATE fe_zia)
target_compile_options(fuzz_zia_parser PRIVATE -fsanitize=fuzzer)
target_link_options(fuzz_zia_parser PRIVATE -fsanitize=fuzzer)

# ── BASIC Lexer Fuzzer ──────────────────────────────────────────────────────
add_executable(fuzz_basic_lexer fuzz_basic_lexer.cpp)
target_include_directories(fuzz_basic_lexer PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fuzz_basic_lexer PRIVATE fe_basic)
target_compile_options(fuzz_basic_lexer PRIVATE -fsanitize=fuzzer)
target_link_options(fuzz_basic_lexer PRIVATE -fsanitize=fuzzer)

# ── BASIC Parser Fuzzer ─────────────────────────────────────────────────────
add_executable(fuzz_basic_parser fuzz_basic_parser.cpp)
target_include_directories(fuzz_basic_parser PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fuzz_basic_parser PRIVATE fe_basic)
target_compile_options(fuzz_basic_parser PRIVATE -fsanitize=fuzzer)
target_link_options(fuzz_basic_parser PRIVATE -fsanitize=fuzzer)

# ── IL Parser Fuzzer ────────────────────────────────────────────────────────
add_executable(fuzz_il_parser fuzz_il_parser.cpp)
target_include_directories(fuzz_il_parser PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fuzz_il_parser PRIVATE viper::il_full)
target_compile_options(fuzz_il_parser PRIVATE -fsanitize=fuzzer)
target_link_options(fuzz_il_parser PRIVATE -fsanitize=fuzzer)

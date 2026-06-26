//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_basic_completion_stub.c
// Purpose: Weak-symbol stubs for the Viper BASIC IDE language-service bridge.
//          The real implementations live in
//          src/frontends/basic/rt_basic_completion.cpp (part of fe_basic). When
//          fe_basic is linked the linker prefers those strong symbols; binaries
//          that omit the frontend fall back to these stubs, which return empty,
//          protocol-shaped payloads — never false editor warnings.
// Key invariants:
//   - Stubs use __attribute__((weak)) on Clang/GCC; on MSVC the macro is empty
//     (MSVC builds always link fe_basic, so the stubs are never emitted there).
//   - Diagnostics/completion return an empty owned Seq (a missing analyzer is
//     not a source diagnostic).
//   - If fe_basic is linked, none of these run; the strong symbols win.
// Links: src/frontends/basic/rt_basic_completion.cpp (strong overrides),
//        src/runtime/graphics/common/rt_basic_completion.h
//
//===----------------------------------------------------------------------===//

#include "rt_string.h"

#include "rt_map.h"
#include "rt_seq.h"

#include <stdint.h>
#include <string.h>

#ifndef _MSC_VER
#define RT_WEAK __attribute__((weak))
#else
#define RT_WEAK
#endif

RT_WEAK void *rt_basic_toolchain_check_for_file(rt_string source, rt_string file_path) {
    (void)source;
    (void)file_path;
    return rt_seq_new_owned();
}

RT_WEAK void *rt_basic_completion_items_for_file(rt_string source,
                                                 rt_string file_path,
                                                 int64_t line,
                                                 int64_t col) {
    (void)source;
    (void)file_path;
    (void)line;
    (void)col;
    return rt_seq_new_owned();
}

RT_WEAK rt_string rt_basic_completion_symbols_for_file(rt_string source, rt_string file_path) {
    (void)source;
    (void)file_path;
    return rt_string_from_bytes("", 0);
}

RT_WEAK void *rt_basic_completion_hover_info_for_file(rt_string source,
                                                      rt_string file_path,
                                                      int64_t line,
                                                      int64_t col) {
    (void)source;
    (void)file_path;
    (void)line;
    (void)col;
    void *map = rt_map_new();
    rt_string key = rt_string_from_bytes("available", strlen("available"));
    rt_map_set_bool(map, key, 0);
    rt_string_unref(key);
    return map;
}

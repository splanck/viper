//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/common/rt_basic_completion.h
// Purpose: C ABI for the Viper BASIC IDE language-service bridge (completion,
//          diagnostics, hover, symbols), mirroring the Zia bridge. The strong
//          implementations live in src/frontends/basic/rt_basic_completion.cpp
//          (part of fe_basic); weak stubs in src/runtime/core/
//          rt_basic_completion_stub.c keep viper_runtime frontend-free. Symbols
//          resolve at final link when the binary links both fe_basic and
//          viper_runtime.
// Key invariants:
//   - All functions are side-effect-free queries over a source string + path.
//   - Returned runtime objects (Seq/Map) and rt_string are owned by the caller.
//   - Result shapes match the Zia bridge so the IDE controllers consume both.
// Links: src/frontends/basic/rt_basic_completion.cpp,
//        src/runtime/graphics/common/rt_zia_completion.h,
//        docs/adr/0013-basic-language-service-runtime-bridge.md
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_BASIC_COMPLETION_H
#define VIPER_RT_BASIC_COMPLETION_H

#include "runtime/core/rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Diagnostics for BASIC @p source (virtual @p file_path). Returns an
///        owned Seq of Maps {file,line,column,endLine,endColumn,severity,
///        severityName,code,message,stage,...} — identical to the Zia toolchain.
void *rt_basic_toolchain_check_for_file(rt_string source, rt_string file_path);

/// @brief Completion items for BASIC @p source at 1-based (@p line,@p col).
///        Returns an owned Seq of Maps {label,insertText,kind,kindName,detail,
///        documentation,source,cursorOffset,replacement*}.
void *rt_basic_completion_items_for_file(rt_string source,
                                         rt_string file_path,
                                         int64_t line,
                                         int64_t col);

/// @brief BASIC document symbols for @p source as a tab-delimited string, one
///        "name\tkind\ttype\tline" record per line.
rt_string rt_basic_completion_symbols_for_file(rt_string source, rt_string file_path);

/// @brief Hover info for the identifier at 1-based (@p line, 0-based @p col) in
///        @p source. Returns an owned Map {available,title,type,display,source,...}.
void *rt_basic_completion_hover_info_for_file(rt_string source,
                                              rt_string file_path,
                                              int64_t line,
                                              int64_t col);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_BASIC_COMPLETION_H

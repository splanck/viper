//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_zia_highlight_stub.c
// Purpose: Weak-symbol stub for the Zia syntax-highlight keyword bridge. The
//          real implementation lives in src/frontends/zia/rt_zia_highlight.cpp
//          (part of fe_zia) and consults the authoritative kKeywordTable.
//          When a test binary or other consumer links viper_runtime without
//          fe_zia, the linker falls back to this stub which conservatively
//          reports "not a keyword" for every identifier.
//
// Key invariants:
//   - The stub uses __attribute__((weak)) on Clang/GCC (macOS, Linux); on
//     MSVC the macro expands to nothing because production Windows builds
//     always link fe_zia. (Mirrors the pattern in rt_zia_completion_stub.c.)
//   - rt_zia_is_keyword stub returns 0 unconditionally. The highlighter
//     gracefully degrades to "no keyword coloring" rather than failing to
//     link or crashing.
//   - If fe_zia is linked, this stub is overridden; the strong symbol in
//     rt_zia_highlight.cpp takes precedence.
//
// Ownership/Lifetime:
//   - No allocation, no state. Pure function.
//
// Links: src/frontends/zia/rt_zia_highlight.cpp (strong-symbol override),
//        src/runtime/graphics/rt_gui_codeeditor.c (consumer)
//
//===----------------------------------------------------------------------===//

#include <stdint.h>

#ifndef _MSC_VER
#define RT_WEAK __attribute__((weak))
#else
// MSVC: production Windows builds always link fe_zia, so no weak fallback
// is needed. A duplicate-symbol error here is louder than a silent stub.
#define RT_WEAK
#endif

/// @brief Weak stub: report no identifier as a keyword.
/// Overridden by rt_zia_highlight.cpp when fe_zia is linked.
RT_WEAK int rt_zia_is_keyword(const char *name, int64_t len) {
    (void)name;
    (void)len;
    return 0;
}

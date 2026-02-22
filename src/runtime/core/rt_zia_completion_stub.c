//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_zia_completion_stub.c
// Purpose: Weak-symbol stubs for the Zia completion bridge.
//
// The real implementations live in src/frontends/zia/rt_zia_completion.cpp
// (part of fe_zia).  When the executable links fe_zia, the linker prefers the
// strong symbols there; unit-test binaries that omit fe_zia fall back to these
// stubs, which return empty results rather than causing a link error.
//
// Weak symbols are supported by Clang and GCC (macOS, Linux).  On MSVC the
// compiler-specific __declspec(selectany) only applies to data; function-level
// weak linking is not needed in practice because Windows builds always link
// the full fe_zia library.
//
//===----------------------------------------------------------------------===//

#include "rt_string.h"

#ifndef _MSC_VER
#define RT_WEAK __attribute__((weak))
#else
// MSVC: accept a duplicate-symbol link error rather than silently stub out;
// production Windows builds always link fe_zia so the issue never arises.
#define RT_WEAK
#endif

/// @brief Weak stub: returns an empty string.
/// Overridden by rt_zia_completion.cpp when fe_zia is linked.
RT_WEAK rt_string rt_zia_complete(rt_string source, int64_t line, int64_t col)
{
    (void)source;
    (void)line;
    (void)col;
    return rt_str_empty();
}

/// @brief Weak stub: no-op.
RT_WEAK void rt_zia_completion_clear_cache(void)
{
}

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: common/Mangle.hpp
// Purpose: Provide unified mangling for linkable symbols from qualified names.
// Key invariants: Linkage mangling is ASCII-only, stable, and case-insensitive.
// Ownership/Lifetime: Header-only declarations; implementation in Mangle.cpp.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

namespace viper::common
{

/// @brief Mangle a qualified name into a safe ASCII linker symbol.
/// @details Converts to lowercase, replaces dots with underscores, and prefixes
///          with '@' to avoid collisions with user names. Only [a-z0-9_] remain.
///          Unsupported characters are replaced with '_'.
/// @param qualified Qualified name like "A.B.F" or "Klass.__ctor".
/// @return Mangled link symbol like "@a_b_f" or "@klass___ctor".
std::string MangleLink(std::string_view qualified);

/// @brief Best-effort demangle of a link symbol back to dotted form.
/// @details Strips leading '@' when present and replaces underscores with dots.
///          Lowercase is preserved.
/// @param symbol Link symbol like "@a_b_f".
/// @return Dotted form like "a.b.f".
std::string DemangleLink(std::string_view symbol);

} // namespace viper::common

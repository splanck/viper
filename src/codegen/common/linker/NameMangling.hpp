//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/NameMangling.hpp
// Purpose: Shared Mach-O/Darwin symbol name mangling utilities.
//          Centralizes the underscore-prefix convention so that all linker
//          and object file writer code uses a single mangling point.
// Key invariants:
//   - Local labels (starting with 'L' or '.') are NOT mangled
//   - Empty names are returned unchanged
//   - machoMangle() is idempotent only for non-mangled inputs
// Links: MachOWriter.cpp, MachOExeWriter.cpp, SymbolResolver.cpp,
//        NativeLinker.cpp, DeadStripPass.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <unordered_map>

namespace viper::codegen::linker {

/// Apply the Darwin/Mach-O underscore convention to a symbol name.
/// Local labels (starting with 'L' or '.') and empty names are returned as-is.
inline std::string machoMangle(const std::string &name) {
    if (name.empty())
        return name;
    if (name[0] == 'L' || name[0] == '.')
        return name; // Local label — no mangling.
    return "_" + name;
}

/// Look up a symbol name in a map, falling back to the underscore-prefixed
/// version if the plain name is not found (Mach-O convention).
/// Returns the iterator to the found entry, or map.end() if neither exists.
template <typename V>
typename std::unordered_map<std::string, V>::const_iterator findWithMachoFallback(
    const std::unordered_map<std::string, V> &map, const std::string &name) {
    auto it = map.find(name);
    if (it != map.end())
        return it;
    return map.find("_" + name);
}

} // namespace viper::codegen::linker

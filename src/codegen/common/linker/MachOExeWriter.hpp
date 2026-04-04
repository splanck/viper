//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/MachOExeWriter.hpp
// Purpose: Write Mach-O executables from linked sections.
// Key invariants:
//   - MH_EXECUTE with LC_MAIN (no LC_UNIXTHREAD)
//   - __PAGEZERO first segment, then __TEXT, __DATA, __LINKEDIT
//   - Page alignment: 16KB for arm64, 4KB for x86_64
//   - Ad-hoc code signing for arm64 (required macOS Ventura+)
// Ownership/Lifetime:
//   - Stateless writer utility
// Links: codegen/common/linker/LinkTypes.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::linker {

/// Dynamic library import for Mach-O linking.
struct DylibImport {
    std::string path; ///< Dylib path (e.g., "/usr/lib/libSystem.B.dylib").
};

/// Write a Mach-O executable.
/// @param path         Output file path.
/// @param layout       The link layout with merged sections.
/// @param arch         Target architecture.
/// @param dylibs       Dynamic libraries to link against.
/// @param dynSyms      Symbols imported from dynamic libraries.
/// @param symOrdinals  Map from symbol name to 1-based dylib ordinal (for MH_TWOLEVEL).
///                     Ordinal 0 means flat lookup (BIND_SPECIAL_DYLIB_FLAT_LOOKUP).
///                     Symbols not in the map default to ordinal 1 (libSystem).
/// @param err          Error output.
/// @return true on success.
bool writeMachOExe(const std::string &path,
                   const LinkLayout &layout,
                   LinkArch arch,
                   const std::vector<DylibImport> &dylibs,
                   const std::unordered_set<std::string> &dynSyms,
                   const std::unordered_map<std::string, uint32_t> &symOrdinals,
                   std::size_t stackSize,
                   std::ostream &err);

inline bool writeMachOExe(const std::string &path,
                          const LinkLayout &layout,
                          LinkArch arch,
                          const std::vector<DylibImport> &dylibs,
                          const std::unordered_set<std::string> &dynSyms,
                          const std::unordered_map<std::string, uint32_t> &symOrdinals,
                          std::ostream &err) {
    return writeMachOExe(path, layout, arch, dylibs, dynSyms, symOrdinals, 0, err);
}

} // namespace viper::codegen::linker

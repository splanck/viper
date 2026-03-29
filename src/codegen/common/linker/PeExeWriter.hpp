//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/PeExeWriter.hpp
// Purpose: Write PE executables from linked sections.
// Key invariants:
//   - DOS stub + PE signature + COFF header + Optional Header (PE32+)
//   - Preserves SectionMerger-assigned section RVAs for linked code/data
//   - Emits a PE import directory when DLL imports are provided
//   - Emits an x64 startup shim that terminates through ExitProcess when needed
// Ownership/Lifetime:
//   - Stateless writer utility
// Links: codegen/common/linker/LinkTypes.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"

#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::linker {

/// DLL import for PE linking.
struct DllImport {
    std::string dllName; ///< DLL name (e.g., "kernel32.dll").
    std::vector<std::string> functions; ///< Linker-visible symbol names.
    std::unordered_map<std::string, std::string>
        importNames; ///< Optional PE import-name overrides keyed by symbol name.
};

/// Write a PE executable.
bool writePeExe(const std::string &path,
                const LinkLayout &layout,
                LinkArch arch,
                const std::vector<DllImport> &imports,
                const std::unordered_map<std::string, uint32_t> &slotRvas,
                bool emitStartupStub,
                std::ostream &err);

inline bool writePeExe(const std::string &path,
                       const LinkLayout &layout,
                       LinkArch arch,
                       const std::vector<DllImport> &imports,
                       std::ostream &err) {
    static const std::unordered_map<std::string, uint32_t> kNoSlots;
    return writePeExe(path, layout, arch, imports, kNoSlots, true, err);
}

} // namespace viper::codegen::linker

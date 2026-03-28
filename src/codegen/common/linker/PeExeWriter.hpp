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
//   - Sections: .text, .rdata, .data
//   - Import directory in .rdata for DLL imports
// Ownership/Lifetime:
//   - Stateless writer utility
// Links: codegen/common/linker/LinkTypes.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace viper::codegen::linker {

/// DLL import for PE linking.
struct DllImport {
    std::string dllName;                ///< DLL name (e.g., "kernel32.dll").
    std::vector<std::string> functions; ///< Imported function names.
};

/// Write a PE executable.
bool writePeExe(const std::string &path,
                const LinkLayout &layout,
                LinkArch arch,
                const std::vector<DllImport> &imports,
                std::ostream &err);

} // namespace viper::codegen::linker

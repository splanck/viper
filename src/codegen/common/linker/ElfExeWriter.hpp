//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ElfExeWriter.hpp
// Purpose: Write ELF executables from linked sections.
// Key invariants:
//   - ET_EXEC with static base address (non-PIE for simplicity)
//   - PT_LOAD segments page-aligned
//   - PT_GNU_STACK with non-executable flags
// Ownership/Lifetime:
//   - Stateless writer utility
// Links: codegen/common/linker/LinkTypes.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"

#include <ostream>
#include <string>

namespace viper::codegen::linker {

/// Write an ELF executable.
/// @param path    Output file path.
/// @param layout  The link layout with merged sections.
/// @param arch    Target architecture.
/// @param err     Error output.
/// @return true on success.
bool writeElfExe(const std::string &path,
                 const LinkLayout &layout,
                 LinkArch arch,
                 std::ostream &err);

} // namespace viper::codegen::linker

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/RelocApplier.hpp
// Purpose: Apply relocations to merged section data, patching machine code
//          with resolved symbol addresses.
// Key invariants:
//   - x86_64 PCRel: S + A - P (32-bit), addend typically -4
//   - AArch64 CALL26: ((S + A - P) >> 2) masked to 26 bits
//   - AArch64 ADRP: Page(S+A) - Page(P), split into immhi/immlo
//   - Range checking for branches (±128MB for B/BL, ±1MB for B.cond)
// Ownership/Lifetime:
//   - Stateless utility; modifies OutputSection data in-place
// Links: codegen/common/linker/LinkTypes.hpp
//        codegen/common/linker/ObjFileReader.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"

#include <ostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::linker
{

/// Apply all relocations across merged sections.
/// @param objects      All object files.
/// @param layout       The link layout with merged sections and resolved symbols.
/// @param dynamicSyms  Symbols expected from shared libraries (generate stubs).
/// @param platform     Target platform.
/// @param arch         Target architecture.
/// @param err          Error output.
/// @return true on success.
bool applyRelocations(const std::vector<ObjFile> &objects, LinkLayout &layout,
                      const std::unordered_set<std::string> &dynamicSyms, LinkPlatform platform,
                      LinkArch arch, std::ostream &err);

} // namespace viper::codegen::linker

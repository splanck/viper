//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ICF.hpp
// Purpose: Identical Code Folding (ICF) pass for the native linker.
//          Identifies per-function .text sections with identical bytes AND
//          identical relocation signatures, redirects duplicates to a single
//          canonical copy.
// Key invariants:
//   - Only per-function .text.* sections with one Global symbol at offset 0
//   - Identity = (section bytes, sorted relocation signatures)
//   - Address-taken functions (referenced by Abs64/Abs32 from data) are excluded
//   - Non-canonical sections have both data AND relocs cleared
// Links: codegen/common/linker/StringDedup.hpp
//        codegen/common/linker/LinkTypes.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::linker {

struct ObjFile;
struct GlobalSymEntry;

/// Fold identical .text sections across object files.
/// Scans per-function text sections, groups by content identity (bytes + reloc
/// signatures), and redirects all duplicates to a single canonical copy.
///
/// @param allObjects  All object files (modified in place: sections cleared).
/// @param globalSyms  Global symbol table (entries redirected to canonical).
/// @return Number of sections folded (eliminated).
size_t foldIdenticalCode(std::vector<ObjFile> &allObjects,
                         std::unordered_map<std::string, GlobalSymEntry> &globalSyms);

} // namespace viper::codegen::linker

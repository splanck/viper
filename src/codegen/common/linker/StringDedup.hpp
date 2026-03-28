//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/StringDedup.hpp
// Purpose: Cross-module string deduplication pass for the native linker.
//          Identifies identical NUL-terminated rodata strings across object
//          files and promotes them to shared global symbols so all relocations
//          resolve to a single canonical copy.
// Key invariants:
//   - Only LOCAL symbols in non-executable, non-writable, allocatable sections
//   - Content must be NUL-terminated within section bounds
//   - Strings with identical content but different lengths are NOT merged
//   - Non-canonical copies remain as dead bytes (no byte removal)
// Links: codegen/common/linker/ObjFileReader.hpp
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

/// Deduplicate identical string literals across object files.
/// Scans LOCAL symbols in rodata sections, groups by NUL-terminated content,
/// and promotes all duplicates to share a single synthetic global symbol name.
///
/// @param allObjects  All object files (modified in place: symbol names/bindings updated).
/// @param globalSyms  Global symbol table (new entries added for canonical strings).
/// @return Number of duplicate strings eliminated.
size_t deduplicateStrings(std::vector<ObjFile> &allObjects,
                          std::unordered_map<std::string, GlobalSymEntry> &globalSyms);

} // namespace viper::codegen::linker

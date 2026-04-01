//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/SymbolResolver.hpp
// Purpose: Global symbol table construction and iterative archive resolution.
// Key invariants:
//   - Strong definitions override weak; multiple strong = error
//   - Archives searched iteratively until no new definitions found
//   - Dynamic library symbols left as undefined after resolution
// Ownership/Lifetime:
//   - SymbolResolver builds the global symbol table from parsed ObjFiles
// Links: codegen/common/linker/LinkTypes.hpp
//        codegen/common/linker/ObjFileReader.hpp
//        codegen/common/linker/ArchiveReader.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/ArchiveReader.hpp"
#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"

#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::linker {

/// Resolve symbols across object files and archives.
/// @param objects       Initial object files (user's compiled code).
/// @param archives      Archives to search for missing symbols (in order).
/// @param globalSyms    Output: resolved global symbol table.
/// @param allObjects    Output: all object files including those extracted from archives.
/// @param dynamicSyms   Output: symbols expected from shared libraries.
/// @param err           Error output stream.
/// @return true on success (all symbols resolved or marked dynamic).
bool resolveSymbols(const std::vector<ObjFile> &initialObjects,
                    std::vector<Archive> &archives,
                    std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                    std::vector<ObjFile> &allObjects,
                    std::unordered_set<std::string> &dynamicSyms,
                    std::ostream &err,
                    LinkPlatform platform = detectLinkPlatform());

} // namespace viper::codegen::linker

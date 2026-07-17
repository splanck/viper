//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

namespace zanna::codegen::linker {

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

/// Resolve symbols using non-owning immutable archive pointers.
/// @details This overload lets persistent build processes retain parsed archives
///          in shared immutable storage without copying their raw byte buffers.
///          Every pointer must remain valid for the duration of the call.
/// @param objects Initial object files supplied directly by the caller.
/// @param archives Ordered, non-null parsed archives searched for definitions.
/// @param globalSyms Receives the resolved global symbol table.
/// @param allObjects Receives direct objects plus extracted archive members.
/// @param dynamicSyms Receives unresolved symbols accepted as loader imports.
/// @param err Diagnostic stream for malformed archives and resolution failures.
/// @param platform Target platform governing symbol spelling and import policy.
/// @return True when every reference resolves statically or as a permitted import.
bool resolveSymbols(const std::vector<ObjFile> &objects,
                    const std::vector<const Archive *> &archives,
                    std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                    std::vector<ObjFile> &allObjects,
                    std::unordered_set<std::string> &dynamicSyms,
                    std::ostream &err,
                    LinkPlatform platform = detectLinkPlatform());

} // namespace zanna::codegen::linker

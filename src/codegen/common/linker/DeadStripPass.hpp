//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/DeadStripPass.hpp
// Purpose: Dead code/data stripping pass for the native linker.
//          Performs mark-and-sweep GC over input sections to remove
//          unreferenced functions and data from the output.
// Key invariants:
//   - Roots: entry point section, ObjC metadata, TLS, init/fini, and sections
//     explicitly retained (SHF_GNU_RETAIN / S_ATTR_NO_DEAD_STRIP / N_NO_DEAD_STRIP).
//     Synthetic linker-generated objects are always kept.
//   - Liveness flows through relocations (section A references symbol in section B)
//   - Every non-synthetic object (user and archive) is eligible for stripping;
//     only sections reachable from a root survive.
// Links: codegen/common/linker/ObjFileReader.hpp
//        codegen/common/linker/LinkTypes.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::linker {

struct ObjFile;
struct GlobalSymEntry;

/// Perform dead section stripping on the linked object files.
/// Sections not reachable from root sections are cleared (data emptied,
/// relocations removed) so they contribute zero bytes to the output.
///
/// @param allObjects       All object files (user + archive extracts + stubs).
/// @param userObjCount     Number of leading objects that are user-provided
///                         (not from archives). These are always live.
/// @param globalSyms       Global symbol table (used to find entry point).
/// @param entrySymbol      Entry point symbol name (e.g., "main").
/// @param preserveDebugSections Keep non-alloc debug sections rooted.
/// @param err              Diagnostic output.
void deadStrip(std::vector<ObjFile> &allObjects,
               size_t userObjCount,
               const std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
               const std::string &entrySymbol,
               LinkPlatform platform,
               bool preserveDebugSections,
               std::ostream &err);

inline void deadStrip(std::vector<ObjFile> &allObjects,
                      size_t userObjCount,
                      const std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                      const std::string &entrySymbol,
                      LinkPlatform platform,
                      std::ostream &err) {
    deadStrip(allObjects, userObjCount, globalSyms, entrySymbol, platform, false, err);
}

inline void deadStrip(std::vector<ObjFile> &allObjects,
                      size_t userObjCount,
                      const std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                      const std::string &entrySymbol,
                      std::ostream &err) {
    deadStrip(allObjects, userObjCount, globalSyms, entrySymbol, detectLinkPlatform(), false, err);
}

} // namespace viper::codegen::linker

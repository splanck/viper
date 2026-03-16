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
//   - Roots: user .o sections, entry point section, ObjC metadata, TLS, init/fini
//   - Liveness flows through relocations (section A references symbol in section B)
//   - Only archive-extracted objects are eligible for stripping
// Links: codegen/common/linker/ObjFileReader.hpp
//        codegen/common/linker/LinkTypes.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::linker
{

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
/// @param err              Diagnostic output.
void deadStrip(std::vector<ObjFile> &allObjects,
               size_t userObjCount,
               const std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
               const std::string &entrySymbol,
               std::ostream &err);

} // namespace viper::codegen::linker

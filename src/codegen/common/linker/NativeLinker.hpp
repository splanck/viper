//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/NativeLinker.hpp
// Purpose: Top-level native linker orchestrator. Ties together archive reading,
//          object file parsing, symbol resolution, section merging, relocation
//          application, and executable output.
// Key invariants:
//   - Zero external tool dependencies
//   - Replaces the `cc` system linker invocation
//   - Supports ELF (Linux), Mach-O (macOS), PE (Windows)
// Ownership/Lifetime:
//   - Stateless entry point; each call is independent
// Links: codegen/common/linker/LinkTypes.hpp
//        codegen/common/LinkerSupport.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace viper::codegen::linker {

/// Options for the native linker.
struct NativeLinkerOptions {
    std::string objPath;                   ///< Path to the user's compiled .o file.
    std::string exePath;                   ///< Output executable path.
    std::vector<std::string> archivePaths; ///< Runtime archive .a paths (in dependency order).
    LinkPlatform platform = detectLinkPlatform();
    LinkArch arch = detectLinkArch();
    std::string entrySymbol = "main";       ///< Entry point symbol name.
    std::vector<std::string> extraObjPaths; ///< Additional .o files to link (e.g. asset blob).
    std::size_t stackSize = 0; ///< Requested stack size in bytes; 0 uses format defaults.
};

/// Run the native linker.
/// @param opts  Linker options.
/// @param out   Standard output stream.
/// @param err   Error output stream.
/// @return 0 on success, non-zero on failure.
int nativeLink(const NativeLinkerOptions &opts, std::ostream &out, std::ostream &err);

} // namespace viper::codegen::linker

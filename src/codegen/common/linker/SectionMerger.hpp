//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/SectionMerger.hpp
// Purpose: Merge input sections from multiple object files into output sections
//          and assign virtual addresses with proper page alignment.
// Key invariants:
//   - macOS arm64: 16KB page alignment (0x4000) — dyld rejects wrong alignment
//   - All other platforms: 4KB (0x1000)
//   - __TEXT contains both code and rodata on Mach-O
//   - Base addresses: macOS=0x100000000, Linux non-PIE=0x400000, Windows=0x140000000
// Ownership/Lifetime:
//   - SectionMerger is stateless; builds LinkLayout from inputs
// Links: codegen/common/linker/LinkTypes.hpp
//        codegen/common/linker/ObjFileReader.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"

#include <ostream>
#include <vector>

namespace viper::codegen::linker {

/// Merge sections from all object files and compute virtual address layout.
/// @param objects     All object files (including archive extracts).
/// @param platform    Target platform.
/// @param arch        Target architecture.
/// @param layout      Output: merged sections with virtual addresses.
/// @param err         Error output.
/// @return true on success.
bool mergeSections(const std::vector<ObjFile> &objects,
                   LinkPlatform platform,
                   LinkArch arch,
                   LinkLayout &layout,
                   std::ostream &err);

} // namespace viper::codegen::linker

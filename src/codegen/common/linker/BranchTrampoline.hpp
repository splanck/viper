//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/BranchTrampoline.hpp
// Purpose: AArch64 branch trampoline insertion for the native linker.
//          Detects out-of-range B/BL (Branch26) relocations after section
//          merging and inserts ADRP+ADD+BR x16 trampolines at the end of
//          the .text segment.
// Key invariants:
//   - Only activates on AArch64 (no-op on x86-64)
//   - Only handles Branch26 (B/BL); B.cond (CondBr19) is left as-is
//   - Trampoline relocations (Page21+PageOff12) are applied inline
//   - Multiple branches to the same out-of-range target share one trampoline
//   - VA re-assignment for sections after .text after trampoline insertion
// Links: codegen/common/linker/RelocApplier.cpp
//        codegen/common/linker/RelocClassify.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"

#include <ostream>
#include <vector>

namespace viper::codegen::linker
{

/// Insert branch trampolines for out-of-range AArch64 B/BL instructions.
/// Runs between mergeSections() and applyRelocations() in the link pipeline.
///
/// @param objects    All object files (relocs may be rewritten).
/// @param layout     Merged link layout (text section data extended, VAs re-assigned).
/// @param arch       Target architecture (no-op if not AArch64).
/// @param platform   Target platform (for base address / page size).
/// @param err        Error output stream.
/// @return true on success, false on unrecoverable error.
bool insertBranchTrampolines(std::vector<ObjFile> &objects,
                             LinkLayout &layout,
                             LinkArch arch,
                             LinkPlatform platform,
                             std::ostream &err);

} // namespace viper::codegen::linker

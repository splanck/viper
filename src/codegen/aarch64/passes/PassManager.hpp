//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/passes/PassManager.hpp
// Purpose: AArch64 pass manager types — delegates to common PassManager template.
// Key invariants: Passes run sequentially, short-circuiting on failure while preserving
//                 prior pass results. Each pass receives the shared AArch64Module state.
// Ownership/Lifetime: PassManager owns registered passes via unique_ptr and operates on
//                     a caller-owned AArch64Module instance passed by reference.
// Links: docs/codemap.md, src/codegen/aarch64/
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/RodataPool.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/common/Diagnostics.hpp"
#include "codegen/common/PassManager.hpp"
#include "codegen/common/objfile/CodeSection.hpp"
#include "il/core/Module.hpp"

#include <optional>
#include <string>
#include <vector>

namespace viper::codegen::aarch64::passes {

/// @brief Mutable state threaded through the AArch64 code-generation passes.
///
/// Each pass transforms a portion of this struct:
///   - LoweringPass  : populates mir and rodataPool from ilMod
///   - RegAllocPass  : assigns physical registers in mir
///   - PeepholePass  : applies peephole optimisations to mir
///   - EmitPass      : produces assembly text in assembly
struct AArch64Module {
    const il::core::Module *ilMod = nullptr; ///< Non-owning pointer to the IL module.
    const TargetInfo *ti = nullptr;          ///< Non-owning pointer to the target info.
    std::string debugSourcePath{}; ///< Source path used for DWARF line table file entries.
    std::vector<MFunction> mir;    ///< MIR functions, populated by LoweringPass.
    RodataPool rodataPool;         ///< Rodata pool, populated by LoweringPass.
    std::string assembly;          ///< Final assembly text, populated by EmitPass.

    // Binary emission output (populated by BinaryEmitPass).
    std::optional<objfile::CodeSection> binaryText;       ///< Machine code bytes + relocations.
    std::optional<objfile::CodeSection> binaryRodata;     ///< Read-only data section.
    std::vector<objfile::CodeSection> binaryTextSections; ///< Per-function text sections.
    std::vector<uint8_t> debugLineData;                   ///< Pre-encoded DWARF .debug_line bytes.
};

// Backward-compatible aliases — consumers use these names unchanged.
using Diagnostics = viper::codegen::common::Diagnostics;
using Pass = viper::codegen::common::Pass<AArch64Module>;
using PassManager = viper::codegen::common::PassManager<AArch64Module>;

} // namespace viper::codegen::aarch64::passes

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/passes/PassManager.hpp
// Purpose: x86_64 pass manager types — delegates to common PassManager template.
// Key invariants: Passes run sequentially, short-circuiting on failure while preserving
//                 prior pass results. Each pass receives the shared Module state.
// Ownership/Lifetime: PassManager owns registered passes via unique_ptr and operates on
//                     a caller-owned Module instance passed by reference.
// Links: docs/codemap.md, src/codegen/x86_64/CodegenPipeline.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/Diagnostics.hpp"
#include "codegen/common/PassManager.hpp"
#include "codegen/common/objfile/CodeSection.hpp"
#include "codegen/x86_64/Backend.hpp"
#include "il/core/Module.hpp"

#include <optional>
#include <vector>

namespace viper::codegen::x64::passes {

/// \brief Mutable state threaded through the code-generation passes.
struct Module {
    il::core::Module il;                ///< Original IL module loaded from disk.
    std::optional<ILModule> lowered;    ///< Adapter module produced by lowering.
    const TargetInfo *target = nullptr; ///< Target descriptor for lowering/allocation.
    CodegenOptions options{};           ///< Backend configuration for later passes.
    AsmEmitter::RoDataPool roData{};    ///< Module-level literal pool built during MIR lowering.
    std::vector<MFunction> mir{};       ///< Per-function MIR after legalization.
    std::vector<FrameInfo> frames{};    ///< Per-function frame summaries aligned with mir.
    bool legalised = false;             ///< Flag toggled once legalisation completes.
    bool registersAllocated = false;    ///< Flag toggled once register allocation runs.
    std::optional<CodegenResult> codegenResult; ///< Backend assembly emission artefacts.

    /// Binary emission artefacts (populated by BinaryEmitPass instead of EmitPass).
    std::optional<objfile::CodeSection> binaryText;       ///< Machine code bytes + relocations.
    std::optional<objfile::CodeSection> binaryRodata;     ///< Read-only data section.
    std::vector<objfile::CodeSection> binaryTextSections; ///< Per-function text sections.
    std::vector<uint8_t> debugLineData;                   ///< Pre-encoded DWARF .debug_line bytes.
};

// Backward-compatible aliases — consumers continue to use these names unchanged.
using Diagnostics = viper::codegen::common::Diagnostics;
using Pass = viper::codegen::common::Pass<Module>;
using PassManager = viper::codegen::common::PassManager<Module>;

} // namespace viper::codegen::x64::passes

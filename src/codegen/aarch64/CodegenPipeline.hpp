//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/CodegenPipeline.hpp
// Purpose: Entry point for the modular AArch64 code-generation pipeline.
//          Wires together all AArch64 passes via the PassManager and
//          replaces the monolithic per-function loop in the CLI driver.
//
// Key invariants:
//   - Passes run in order: Lowering → RegAlloc → Scheduler → BlockLayout →
//     Peephole → Emit.
//   - The AArch64Module struct is threaded through all passes.
//   - MIR dump callbacks fire between passes when enabled.
//
// Ownership/Lifetime:
//   - The pipeline operates on a caller-owned AArch64Module reference.
//   - The pipeline does not own the IL module or TargetInfo.
//
// Links: codegen/aarch64/passes/PassManager.hpp (types),
//        tools/viper/cmd_codegen_arm64.cpp (caller)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "codegen/aarch64/passes/PassManager.hpp"

#include <string>

namespace viper::codegen::aarch64
{

/// Options controlling optional MIR dumps and diagnostics.
struct PipelineOptions
{
    bool dumpMirBeforeRA = false;
    bool dumpMirAfterRA = false;
};

/// @brief Run the full AArch64 code-generation pipeline.
/// @param module  Mutable module state; ilMod and ti must be set.
/// @param opts    Pipeline options (MIR dump flags, etc.).
/// @return true on success, false on error (diagnostics printed to stderr).
bool runCodegenPipeline(passes::AArch64Module &module, const PipelineOptions &opts);

} // namespace viper::codegen::aarch64

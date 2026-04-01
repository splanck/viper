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

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace viper::codegen::aarch64 {

struct PipelineResult {
    int exit_code{0};
    std::string stdout_text{};
    std::string stderr_text{};
};

/// Options controlling optional MIR dumps and diagnostics.
struct PipelineOptions {
    bool dumpMirBeforeRA = false;
    bool dumpMirAfterRA = false;
    bool emitAssemblyText = true;
    bool useBinaryEmit = false; ///< When true, also run BinaryEmitPass after EmitPass.
};

class CodegenPipeline {
  public:
    enum class AssemblerMode {
        System,
        Native,
    };

    enum class LinkMode {
        System,
        Native,
    };

    struct Options {
        std::string input_il_path{};
        std::string output_obj_path{};
        std::string output_asm_path{};
        bool emit_asm = false;
        bool run_native = false;
        bool dump_mir_before_ra = false;
        bool dump_mir_after_ra = false;
        int optimize = 0;
        AssemblerMode assembler_mode = AssemblerMode::Native;
        LinkMode link_mode = LinkMode::Native;
        std::string asset_blob_path{}; ///< Path to VPA asset blob for .rodata embedding.
        std::vector<std::string> extra_objects{}; ///< Extra .o files to link.
    };

    explicit CodegenPipeline(Options opts);

    [[nodiscard]] PipelineResult run();

  private:
    Options opts_;
};

/// @brief Run the full AArch64 code-generation pipeline.
/// @param module  Mutable module state; ilMod and ti must be set.
/// @param opts    Pipeline options (MIR dump flags, etc.).
/// @param diagOut Stream receiving MIR dumps and diagnostics.
/// @return true on success, false on error.
bool runCodegenPipeline(passes::AArch64Module &module,
                        const PipelineOptions &opts,
                        std::ostream &diagOut);

} // namespace viper::codegen::aarch64

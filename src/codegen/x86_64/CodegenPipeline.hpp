//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/CodegenPipeline.hpp
// Purpose: Declare a reusable pipeline that lowers IL modules to native code.
// Key invariants: Passes execute sequentially with early exit on failure; assembly
//                 is written before linking when emit_asm is set; run() returns
//                 a zero exit_code only when all stages succeed; optimize level
//                 controls which optimisation passes are run (0=none, 1=peephole).
// Ownership/Lifetime: Callers retain ownership of file paths and do not transfer
//                     resource management; pipeline state is local to each run().
// Links: docs/codemap.md, src/codegen/x86_64/Backend.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

#include <string>

struct PipelineResult {
    int exit_code;           ///< Final exit status of the pipeline; zero indicates success.
    std::string stdout_text; ///< Aggregated standard output emitted by subprocesses.
    std::string stderr_text; ///< Aggregated diagnostics and error messages.
};

namespace viper::codegen::x64 {

/// \brief High-level orchestrator for the x86-64 code-generation flow.
class CodegenPipeline {
  public:
    /// \brief Assembler mode selection.
    enum class AssemblerMode {
        System, ///< Use the host assembler/compiler driver.
        Native, ///< Use the native binary encoder + object writer.
    };

    /// \brief Linker mode selection.
    enum class LinkMode {
        System, ///< Use the host toolchain linker.
        Native, ///< Use the built-in Viper native linker.
    };

    /// \brief User-configurable options controlling pipeline behaviour.
    struct Options {
        std::string input_il_path;   ///< IL module to load and compile.
        std::string output_obj_path; ///< Destination path for executable/object output.
        std::string output_asm_path; ///< Optional assembly output path when emit_asm is true.
        bool emit_asm = false;       ///< Emit assembly text to disk for inspection.
        int optimize = 1;            ///< Optimization level: 0 = none, 1 = O1, 2 = O2.
        bool run_native = false;     ///< Execute the produced binary after linking when true.
        std::size_t stack_size = 0;  ///< Stack size in bytes; 0 means use system default.
        AssemblerMode assembler_mode = AssemblerMode::Native;
        LinkMode link_mode = LinkMode::Native;
        std::string asset_blob_path; ///< Path to VPA asset blob for .rodata embedding (optional).
    };

    /// \brief Construct a pipeline configured with @p opts.
    explicit CodegenPipeline(Options opts);

    /// \brief Execute the end-to-end code-generation workflow.
    PipelineResult run();

  private:
    Options opts_;
};

} // namespace viper::codegen::x64

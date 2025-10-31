// File: src/codegen/x86_64/CodegenPipeline.hpp
// Purpose: Declare a reusable pipeline that lowers IL modules to native code via the x86-64
// backend. Key invariants: The pipeline sequences parsing, verification, adaptation, and emission
// in a
//                 deterministic order while reporting diagnostics through an aggregated result.
// Ownership/Lifetime: Callers retain ownership of file paths and do not transfer resource
//                     ownership to the pipeline; results are returned by value.
// Links: docs/codemap.md, src/codegen/x86_64/Backend.hpp

#pragma once

#include "codegen/x86_64/passes/PassManager.hpp"

#include <string>

struct PipelineResult
{
    int exit_code;           ///< Final exit status of the pipeline; zero indicates success.
    std::string stdout_text; ///< Aggregated standard output emitted by subprocesses.
    std::string stderr_text; ///< Aggregated diagnostics and error messages.
};

namespace viper::codegen::x64
{

/// \brief High-level orchestrator for the x86-64 code-generation flow.
class CodegenPipeline
{
  public:
    /// \brief User-configurable options controlling pipeline behaviour.
    struct Options
    {
        std::string input_il_path;   ///< IL module to load and compile.
        std::string output_obj_path; ///< Destination path for executable/object output; when set
                                     ///< without @ref run_native the pipeline emits an object file.
        std::string output_asm_path; ///< Optional assembly output path when emit_asm is true.
        bool emit_asm = false;       ///< Emit assembly text to disk for inspection.
        bool optimize = false;       ///< Placeholder for future optimisation control.
        bool run_native = false;     ///< Execute the produced binary after linking when true.
    };

    /// \brief Construct a pipeline configured with @p opts.
    explicit CodegenPipeline(Options opts);

    /// \brief Execute the end-to-end code-generation workflow.
    /// @return Aggregated result including exit code and captured output streams.
    PipelineResult run();

  private:
    Options opts_;
};

} // namespace viper::codegen::x64

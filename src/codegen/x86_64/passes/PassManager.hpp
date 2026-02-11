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
#include "codegen/x86_64/Backend.hpp"
#include "il/core/Module.hpp"

#include <optional>

namespace viper::codegen::x64::passes
{

/// \brief Mutable state threaded through the code-generation passes.
struct Module
{
    il::core::Module il;                        ///< Original IL module loaded from disk.
    std::optional<ILModule> lowered;            ///< Adapter module produced by lowering.
    bool legalised = false;                     ///< Flag toggled once legalisation completes.
    bool registersAllocated = false;            ///< Flag toggled once register allocation runs.
    std::optional<CodegenResult> codegenResult; ///< Backend assembly emission artefacts.
};

// Backward-compatible aliases — consumers continue to use these names unchanged.
using Diagnostics = viper::codegen::common::Diagnostics;
using Pass = viper::codegen::common::Pass<Module>;
using PassManager = viper::codegen::common::PassManager<Module>;

} // namespace viper::codegen::x64::passes

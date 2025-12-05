//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Backend.hpp
// Purpose: Declare the high-level orchestration entry points for x86-64 codegen.
// Key invariants: emitModuleToAssembly processes all functions in module order;
//                 errors field is empty on success; Phase A only supports AT&T
//                 syntax (atandtSyntax must be true).
// Ownership/Lifetime: Callers retain ownership of IL modules; generated assembly is
//                     returned by value in CodegenResult.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "LowerILToMIR.hpp"

#include <string>

namespace viper::codegen::x64
{

/// \brief Options controlling backend emission behaviour.
struct CodegenOptions
{
    bool atandtSyntax{true}; ///< Emit AT&T syntax when true; Phase A only supports this form.
    int optimizeLevel{1};    ///< Optimization level: 0 = none, 1 = standard (peephole), 2+ reserved.
};

/// \brief Aggregated result of a backend emission request.
struct CodegenResult
{
    std::string asmText{}; ///< Complete assembly text for the requested module/function.
    std::string errors{};  ///< Phase A diagnostics; empty when emission succeeds.
};

/// \brief Lower a single IL function to assembly text.
[[nodiscard]] CodegenResult emitFunctionToAssembly(const ILFunction &func,
                                                   const CodegenOptions &opt);

/// \brief Lower an IL module to assembly text using the Phase A backend pipeline.
[[nodiscard]] CodegenResult emitModuleToAssembly(const ILModule &mod, const CodegenOptions &opt);

} // namespace viper::codegen::x64

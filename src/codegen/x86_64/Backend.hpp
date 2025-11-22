//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/Backend.hpp
// Purpose: Declare the high-level orchestration entry points for the x86-64 
// Key invariants: To be documented.
// Ownership/Lifetime: Callers retain ownership of IL modules; generated assembly is
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

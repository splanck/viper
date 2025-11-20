// src/codegen/x86_64/Backend.hpp
// SPDX-License-Identifier: GPL-3.0-only
//
// Purpose: Declare the high-level orchestration entry points for the x86-64
//          backend, enabling Phase A clients to lower IL modules into assembly
//          text through a single facade.
// Invariants: The facade sequences the Phase A pipeline components in a
//             deterministic order and returns aggregated assembly and diagnostic
//             text.
// Ownership: Callers retain ownership of IL modules; generated assembly is
//            returned by value in the result structure.
// Notes: Uses the temporary ILModule adapter defined by LowerILToMIR.hpp until
//        the full IL integration lands.

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

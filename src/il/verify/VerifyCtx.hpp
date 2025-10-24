// File: src/il/verify/VerifyCtx.hpp
// Purpose: Defines a shared context structure for verifier routines operating on IL instructions.
// Key invariants: Context members reference live verifier state scoped to the current instruction.
// Ownership/Lifetime: Non-owning references valid for the duration of a single verification step.
// Links: docs/il-guide.md#reference
#pragma once

#include "il/core/fwd.hpp"

#include <string>
#include <unordered_map>

namespace il::core
{
struct Extern;
}

namespace il::verify
{

class DiagSink;
class TypeInference;

/// @brief Bundles shared verifier state when validating a single instruction.
struct VerifyCtx
{
    DiagSink &diags;      ///< Diagnostic sink used for warnings and errors.
    TypeInference &types; ///< Type inference table tracking temporaries.
    const std::unordered_map<std::string, const il::core::Extern *>
        &externs; ///< Known extern signatures.
    const std::unordered_map<std::string, const il::core::Function *>
        &functions;                    ///< Known function definitions.
    const il::core::Function &fn;      ///< Function that owns the instruction.
    const il::core::BasicBlock &block; ///< Basic block containing the instruction.
    const il::core::Instr &instr;      ///< Instruction under validation.
};

} // namespace il::verify

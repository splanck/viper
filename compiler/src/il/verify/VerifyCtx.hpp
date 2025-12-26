//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the VerifyCtx structure, which bundles all contextual state
// needed during instruction verification into a single parameter. This reduces
// parameter passing overhead and provides a consistent interface for verification
// helpers.
//
// Instruction verification requires access to multiple pieces of context: the
// enclosing function and basic block (for diagnostic formatting), the instruction
// being verified, type environment (for operand type resolution), extern/function
// symbol tables (for call validation), and diagnostic sink (for error reporting).
// Rather than passing these as individual parameters to every verification function,
// VerifyCtx aggregates them into a single context object.
//
// Key Responsibilities:
// - Bundle verification state into a single copyable structure
// - Provide const access to function, block, and instruction being verified
// - Reference type environment for operand type queries
// - Reference symbol tables for extern and function lookup during call validation
// - Reference diagnostic sink for error and warning reporting
//
// Design Notes:
// VerifyCtx is a simple aggregate struct holding only references - it owns no data.
// This makes it trivial to construct and copy, enabling easy context passing through
// the verification call stack. The struct is designed to be constructed once per
// instruction verification and passed by const reference to all helpers. The
// references remain valid only for the duration of instruction verification.
//
//===----------------------------------------------------------------------===//

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

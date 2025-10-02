// File: src/il/verify/BranchVerifier.hpp
// Purpose: Declare helpers that validate branch and return terminators during verification.
// Key invariants: Branch labels/arguments and return values match their targets and function
// signatures. Ownership/Lifetime: Stateless helpers operating on caller-provided verifier context.
// Links: docs/il-guide.md#reference
#pragma once

#include "support/diag_expected.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace il::core
{
struct BasicBlock;
struct Function;
struct Instr;
} // namespace il::core

namespace il::verify
{
class TypeInference;

il::support::Expected<void> verifyBr_E(
    const il::core::Function &fn,
    const il::core::BasicBlock &bb,
    const il::core::Instr &instr,
    const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
    TypeInference &types);

il::support::Expected<void> verifyCBr_E(
    const il::core::Function &fn,
    const il::core::BasicBlock &bb,
    const il::core::Instr &instr,
    const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
    TypeInference &types);

il::support::Expected<void> verifySwitchI32_E(
    const il::core::Function &fn,
    const il::core::BasicBlock &bb,
    const il::core::Instr &instr,
    const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
    TypeInference &types);

il::support::Expected<void> verifyRet_E(const il::core::Function &fn,
                                        const il::core::BasicBlock &bb,
                                        const il::core::Instr &instr,
                                        TypeInference &types);

} // namespace il::verify

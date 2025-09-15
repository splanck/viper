// File: src/il/verify/BlockVerifier.hpp
// Purpose: Declares verifier for basic blocks and instructions.
// Key invariants: None.
// Ownership/Lifetime: Verifier does not own functions or blocks.
// Links: docs/il-spec.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include <ostream>
#include <string>
#include <unordered_map>

namespace il::verify
{

/// @brief Validates basic blocks and their instructions.
class BlockVerifier
{
  public:
    /// @brief Validate a basic block's instructions and terminator.
    /// @param fn Enclosing function.
    /// @param bb Block under inspection.
    /// @param blockMap Map of labels to blocks for branch targets.
    /// @param externs Extern signatures for call checking.
    /// @param funcs Function map for call checking.
    /// @param temps Map of temporary ids to their inferred types.
    /// @param err Stream receiving diagnostic messages.
    /// @return True if the block is well-formed; false otherwise.
    bool verify(const il::core::Function &fn,
                const il::core::BasicBlock &bb,
                const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
                const std::unordered_map<std::string, const il::core::Extern *> &externs,
                const std::unordered_map<std::string, const il::core::Function *> &funcs,
                std::unordered_map<unsigned, il::core::Type> &temps,
                std::ostream &err);
};

} // namespace il::verify

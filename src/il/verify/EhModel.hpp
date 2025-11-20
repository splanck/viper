//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/il/verify/EhModel.hpp
//
// Purpose:
//   Declare the canonical exception-handling (EH) model used by verifier
//   components. The model captures the layout of basic blocks, handler entry
//   points, and successor relationships required to analyse EH invariants.
//
// Key invariants:
//   * The model borrows IR nodes from the owning function without taking
//     ownership.
//   * Successor queries are resolved through a deterministic label map built
//     during construction.
//
// Ownership/Lifetime:
//   The EhModel references IL structures owned by the caller. The caller must
//   guarantee the function outlives the model.
//
// Links:
//   docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace il::verify
{

/// @brief Canonical representation of a function's exception-handling graph.
class EhModel
{
  public:
    /// @brief Build the EH model for @p function.
    /// @param function Function whose EH structure will be analysed.
    explicit EhModel(const il::core::Function &function);

    /// @brief Access the function used to construct the model.
    /// @return Reference to the underlying function.
    [[nodiscard]] const il::core::Function &function() const noexcept
    {
        return *fn;
    }

    /// @brief Retrieve the entry block for the function.
    /// @return Pointer to the entry block or nullptr when no blocks exist.
    [[nodiscard]] const il::core::BasicBlock *entry() const noexcept
    {
        return entryBlock;
    }

    /// @brief Determine whether the function contains EH-relevant opcodes.
    /// @return True when at least one EH opcode is present.
    [[nodiscard]] bool hasEhInstructions() const noexcept
    {
        return hasEh;
    }

    /// @brief Resolve a block label to its definition.
    /// @param label Basic-block label to resolve.
    /// @return Pointer to the block or nullptr when missing.
    [[nodiscard]] const il::core::BasicBlock *findBlock(const std::string &label) const;

    /// @brief Enumerate successors for a terminator instruction.
    /// @param terminator Terminator whose successors are requested.
    /// @return Vector of successor block pointers (may be empty).
    [[nodiscard]] std::vector<const il::core::BasicBlock *> gatherSuccessors(
        const il::core::Instr &terminator) const;

    /// @brief Locate the first terminator instruction in a basic block.
    /// @param block Block whose terminator should be identified.
    /// @return Pointer to the terminator or nullptr when absent.
    [[nodiscard]] const il::core::Instr *findTerminator(const il::core::BasicBlock &block) const;

    /// @brief Access the internal label-to-block table.
    /// @return Reference to the label map.
    [[nodiscard]] const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap()
        const noexcept
    {
        return blocks;
    }

  private:
    const il::core::Function *fn = nullptr;
    const il::core::BasicBlock *entryBlock = nullptr;
    std::unordered_map<std::string, const il::core::BasicBlock *> blocks;
    bool hasEh = false;
};

} // namespace il::verify

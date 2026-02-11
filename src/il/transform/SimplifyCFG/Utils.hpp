//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/SimplifyCFG/Utils.hpp
// Purpose: Shared helpers for SimplifyCFG transformations -- terminator lookup,
//          value comparison, value substitution, block-index lookup,
//          reachability worklist helpers, side-effect classification, and
//          EH-sensitive block identification.
// Key invariants: Operates on IL CFG structures without mutating ownership.
// Ownership/Lifetime: Stateless free functions that inspect/mutate
//          caller-owned IR structures.
// Links: docs/codemap.md, il/core/BasicBlock.hpp, il/core/Function.hpp,
//        il/core/Instr.hpp, il/core/OpcodeInfo.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"
#include "il/core/Value.hpp"

#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(__has_include)
#if __has_include("llvm_like/ADT/BitVector.hpp")
#include "llvm_like/ADT/BitVector.hpp"
#define VIPER_HAVE_LLVM_LIKE_BITVECTOR 1
#endif
#endif

#ifndef VIPER_HAVE_LLVM_LIKE_BITVECTOR
namespace llvm_like
{

/// @brief Minimal BitVector drop-in used when llvm_like is unavailable.
class BitVector
{
  public:
    BitVector() = default;

    explicit BitVector(size_t count, bool value = false) : bits_(count, value) {}

    void resize(size_t count, bool value = false)
    {
        const size_t currentSize = bits_.size();
        if (count <= currentSize)
        {
            bits_.resize(count);
            return;
        }

        bits_.resize(count, value);
    }

    bool test(size_t index) const
    {
        return bits_.at(index);
    }

    void set(size_t index)
    {
        bits_.at(index) = true;
    }

    size_t size() const
    {
        return bits_.size();
    }

  private:
    std::vector<bool> bits_;
};

} // namespace llvm_like

#endif // VIPER_HAVE_LLVM_LIKE_BITVECTOR

namespace il::transform::simplify_cfg
{

using BitVector = llvm_like::BitVector;

/// @brief Locate the terminator instruction in a mutable basic block.
/// @param block Block whose last instruction is inspected.
/// @return Pointer to the terminator, or nullptr if the block is empty.
il::core::Instr *findTerminator(il::core::BasicBlock &block);

/// @brief Locate the terminator instruction in a const basic block.
/// @param block Block whose last instruction is inspected.
/// @return Pointer to the terminator, or nullptr if the block is empty.
const il::core::Instr *findTerminator(const il::core::BasicBlock &block);

/// \brief Compare IL values structurally (ignoring SSA id differences where safe).
bool valuesEqual(const il::core::Value &lhs, const il::core::Value &rhs);

/// \brief Compare two vectors of IL values element-wise using valuesEqual.
bool valueVectorsEqual(const std::vector<il::core::Value> &lhs,
                       const std::vector<il::core::Value> &rhs);

/// \brief Substitute temps according to @p mapping; non-temps are returned unchanged.
il::core::Value substituteValue(const il::core::Value &value,
                                const std::unordered_map<unsigned, il::core::Value> &mapping);

/// \brief Resolve a basic block index from a label->index map.
size_t lookupBlockIndex(const std::unordered_map<std::string, size_t> &labelToIndex,
                        const std::string &label);

/// \brief Mark @p successor reachable and push it onto the worklist when newly discovered.
void enqueueSuccessor(BitVector &reachable, std::deque<size_t> &worklist, size_t successor);

/// \brief Read a debug flag from the environment to enable verbose logging.
bool readDebugFlagFromEnv();

/// \brief Identify whether an instruction has observable side effects.
bool hasSideEffects(const il::core::Instr &instr);

/// \brief Check whether a block label denotes an entry block.
bool isEntryLabel(const std::string &label);

/// \brief Classify resume-family opcodes used by EH.
bool isResumeOpcode(il::core::Opcode op);

/// \brief Classify EH structural opcodes that constrain CFG transforms.
bool isEhStructuralOpcode(il::core::Opcode op);

/// \brief Identify blocks that participate in EH and must not be simplified.
bool isEHSensitiveBlock(const il::core::BasicBlock &block);

} // namespace il::transform::simplify_cfg

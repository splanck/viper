// File: src/il/transform/SimplifyCFG/Utils.hpp
// License: MIT (see LICENSE for details).
// Purpose: Shared helpers for SimplifyCFG transformations.
// Key invariants: Operates on IL CFG structures without mutating ownership.
// Ownership/Lifetime: Functions inspect/mutate caller-owned IR structures.
// Links: docs/codemap.md
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

il::core::Instr *findTerminator(il::core::BasicBlock &block);
const il::core::Instr *findTerminator(const il::core::BasicBlock &block);

bool valuesEqual(const il::core::Value &lhs, const il::core::Value &rhs);
bool valueVectorsEqual(const std::vector<il::core::Value> &lhs,
                       const std::vector<il::core::Value> &rhs);
il::core::Value substituteValue(const il::core::Value &value,
                                const std::unordered_map<unsigned, il::core::Value> &mapping);

size_t lookupBlockIndex(const std::unordered_map<std::string, size_t> &labelToIndex,
                        const std::string &label);
void enqueueSuccessor(BitVector &reachable,
                      std::deque<size_t> &worklist,
                      size_t successor);

bool readDebugFlagFromEnv();
bool hasSideEffects(const il::core::Instr &instr);
bool isEntryLabel(const std::string &label);
bool isResumeOpcode(il::core::Opcode op);
bool isEhStructuralOpcode(il::core::Opcode op);
bool isEHSensitiveBlock(const il::core::BasicBlock &block);

} // namespace il::transform::simplify_cfg


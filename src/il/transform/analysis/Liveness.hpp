//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares liveness analysis infrastructure for IL functions, computing
// live-in and live-out sets for SSA temporaries at each basic block. Liveness
// information is essential for register allocation, dead code elimination, and
// understanding variable lifetimes.
//
// Liveness analysis determines which SSA temporaries hold values that may be
// used along some path from a given program point. The analysis computes live-in
// sets (temporaries that must be available at block entry) and live-out sets
// (temporaries that must be preserved at block exit). These sets enable optimizations
// to identify unused values and guide resource allocation.
//
// Algorithm:
// - CFG construction: Build successor and predecessor relationships for all blocks
// - Use-def analysis: Identify which temporaries each instruction uses and defines
// - Backward dataflow: Propagate liveness information backward through the CFG
//   using iterative fixpoint computation
// - Bitset representation: Use dense bitsets indexed by SSA temporary IDs for
//   efficient set operations during fixpoint iteration
//
// The analysis uses block pointers for adjacency information but stores liveness
// as temporary ID bitsets, enabling efficient queries and compact representation.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/core/fwd.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace il::transform
{

/// @brief Cached control-flow information for a function.
struct CFGInfo
{
    std::unordered_map<const core::BasicBlock *, std::vector<const core::BasicBlock *>> successors;
    std::unordered_map<const core::BasicBlock *, std::vector<const core::BasicBlock *>>
        predecessors;
};

/// @brief Cached liveness sets (live-in/live-out) for each block.
class LivenessInfo
{
  public:
    /// @brief Lightweight view over the live value bitset for a block edge.
    class SetView
    {
      public:
        SetView() = default;

        /// @brief Query whether the set contains @p valueId.
        /// @param valueId SSA temporary identifier.
        /// @return True when live; false otherwise.
        bool contains(unsigned valueId) const;

        template <typename Fn> void forEach(Fn &&fn) const
        {
            if (!bits_)
                return;
            for (unsigned id = 0; id < bits_->size(); ++id)
            {
                if ((*bits_)[id])
                    fn(id);
            }
        }

        /// @brief Check whether the set is empty.
        bool empty() const;

        /// @brief Access the underlying bitset representation.
        /// @return Reference to a vector<bool> view; may reference an empty sentinel.
        const std::vector<bool> &bits() const;

      private:
        explicit SetView(const std::vector<bool> *bits);

        const std::vector<bool> *bits_ = nullptr;

        friend class LivenessInfo;
    };

    /// @brief Live-in set for @p block.
    SetView liveIn(const core::BasicBlock &block) const;
    /// @overload
    SetView liveIn(const core::BasicBlock *block) const;

    /// @brief Live-out set for @p block.
    SetView liveOut(const core::BasicBlock &block) const;
    /// @overload
    SetView liveOut(const core::BasicBlock *block) const;

    /// @brief Total number of SSA value IDs tracked by the analysis.
    std::size_t valueCount() const;

  private:
    using BitSet = std::vector<bool>;

    std::size_t valueCount_{0};
    std::unordered_map<const core::BasicBlock *, BitSet> liveInBits_;
    std::unordered_map<const core::BasicBlock *, BitSet> liveOutBits_;

    friend LivenessInfo computeLiveness(core::Module &module, core::Function &fn);
    friend LivenessInfo computeLiveness(core::Module &module,
                                        core::Function &fn,
                                        const CFGInfo &cfg);
};

/// @brief Build CFG adjacency information for a function.
CFGInfo buildCFG(core::Module &module, core::Function &fn);

/// @brief Compute liveness sets for @p fn by building a CFG internally.
LivenessInfo computeLiveness(core::Module &module, core::Function &fn);

/// @brief Compute liveness sets for @p fn using a precomputed CFG.
LivenessInfo computeLiveness(core::Module &module, core::Function &fn, const CFGInfo &cfg);

} // namespace il::transform

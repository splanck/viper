// File: src/il/transform/analysis/Liveness.hpp
// Purpose: Declare CFG adjacency summaries and liveness analysis helpers.
// Key invariants: Liveness bitsets track dense SSA identifiers sized per function.
// Ownership/Lifetime: Returned summaries borrow basic block pointers owned by the module.
// Links: docs/codemap.md
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

        bool empty() const;

        const std::vector<bool> &bits() const;

      private:
        explicit SetView(const std::vector<bool> *bits);

        const std::vector<bool> *bits_ = nullptr;

        friend class LivenessInfo;
    };

    SetView liveIn(const core::BasicBlock &block) const;
    SetView liveIn(const core::BasicBlock *block) const;

    SetView liveOut(const core::BasicBlock &block) const;
    SetView liveOut(const core::BasicBlock *block) const;

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

CFGInfo buildCFG(core::Module &module, core::Function &fn);
LivenessInfo computeLiveness(core::Module &module, core::Function &fn);
LivenessInfo computeLiveness(core::Module &module, core::Function &fn, const CFGInfo &cfg);

} // namespace il::transform

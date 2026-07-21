//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/LoopContext.hpp
// Purpose: Loop context management for break/continue support in frontends.
//
// This provides a unified abstraction for tracking loop contexts during
// lowering, enabling break and continue statements to find their targets.
//
// Key Invariants:
//   - Loop contexts are pushed when entering a loop and popped when exiting
//   - The stack maintains proper nesting of loops
//   - Break targets the exit block, continue targets the update/test block
// Ownership/Lifetime: Owns a stack of block indices; no IR pointers are stored.
// Links: src/frontends/common/BlockManager.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

namespace il::frontends::common {

/// @brief Context for a single loop during lowering.
/// @details Tracks the block indices that break and continue should target.
struct LoopContext {
    std::optional<std::size_t> breakBlockIdx;    ///< Target block for break statements.
    std::optional<std::size_t> continueBlockIdx; ///< Target block for continue statements.

    /// @brief Optional update block for FOR-style loops.
    /// @details When set, FOR loops use this for continue; otherwise
    ///          continueBlockIdx is used directly.
    std::optional<std::size_t> updateBlockIdx;

    /// @brief Optional test block for FOR/WHILE loops.
    std::optional<std::size_t> testBlockIdx;
};

/// @brief Manages a stack of loop contexts for nested loop support.
class LoopContextStack {
  public:
    /// @brief Push a new loop context onto the stack.
    /// @param ctx The loop context to push.
    void push(LoopContext ctx) {
        stack_.push_back(ctx);
    }

    /// @brief Push a simple loop context with just break and continue targets.
    /// @param breakIdx Target block index for break.
    /// @param continueIdx Target block index for continue.
    void push(std::size_t breakIdx, std::size_t continueIdx) {
        stack_.push_back({breakIdx, continueIdx, std::nullopt, std::nullopt});
    }

    /// @brief Pop the current loop context.
    void pop() {
        if (stack_.empty())
            throw std::logic_error("cannot pop an empty loop context stack");
        stack_.pop_back();
    }

    /// @brief Get the current (innermost) loop context.
    /// @return Reference to the current context.
    /// @pre !empty()
    [[nodiscard]] LoopContext &current() {
        requireCurrent();
        return stack_.back();
    }

    /// @brief Get the current (innermost) loop context (const).
    /// @return Const reference to the current context.
    /// @pre !empty()
    [[nodiscard]] const LoopContext &current() const {
        requireCurrent();
        return stack_.back();
    }

    /// @brief Check if there is an active loop context.
    [[nodiscard]] bool empty() const noexcept {
        return stack_.empty();
    }

    /// @brief Get the number of nested loops.
    [[nodiscard]] std::size_t depth() const noexcept {
        return stack_.size();
    }

    /// @brief Get the break target for the current loop.
    /// @pre !empty()
    [[nodiscard]] std::size_t breakTarget() const {
        requireCurrent();
        const auto &target = stack_.back().breakBlockIdx;
        if (!target)
            throw std::logic_error("loop context has no break target");
        return *target;
    }

    /// @brief Get the continue target for the current loop.
    /// @details Returns the update block if set, otherwise the continue block.
    /// @pre !empty()
    [[nodiscard]] std::size_t continueTarget() const {
        requireCurrent();
        const auto &ctx = stack_.back();
        if (ctx.updateBlockIdx)
            return *ctx.updateBlockIdx;
        if (!ctx.continueBlockIdx)
            throw std::logic_error("loop context has no continue target");
        return *ctx.continueBlockIdx;
    }

    /// @brief Clear all loop contexts.
    void clear() {
        stack_.clear();
    }

  private:
    void requireCurrent() const {
        if (stack_.empty())
            throw std::logic_error("loop context operation requires an active loop");
    }

    std::vector<LoopContext> stack_;
};

} // namespace il::frontends::common

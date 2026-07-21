//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/BlockManager.hpp
// Purpose: Basic block creation and management for language frontends.
//
// This provides a unified abstraction for creating and tracking basic blocks
// during lowering. Language frontends need deterministic block naming and
// insertion point management.
//
// Key Invariants:
//   - Block names are deterministic (based on counter)
//   - Current block is always valid when set
//   - Block indices are stable within a function
// Ownership/Lifetime: Holds non-owning pointers into the bound function's
//                     stable block storage; the function must outlive it.
// Links: src/il/build/IRBuilder.hpp, src/il/core/Function.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>

namespace il::frontends::common {

/// @brief Manages basic block creation, naming, and insertion point tracking.
/// @details Provides deterministic block naming and tracks the current block
///          for instruction emission. Used by all language frontends.
class BlockManager {
  public:
    using Function = il::core::Function;
    using BasicBlock = il::core::BasicBlock;

    /// @brief Default constructor creates an unbound manager.
    BlockManager() = default;

    /// @brief Construct with an IR builder and function.
    /// @param builder The IR builder for block creation.
    /// @param func The function to manage blocks for.
    BlockManager(il::build::IRBuilder *builder, Function *func)
        : builder_(builder), currentFunc_(func) {}

    /// @brief Bind to a new function (resets block counter).
    /// @param builder The IR builder.
    /// @param func The function to manage.
    void bind(il::build::IRBuilder *builder, Function *func) {
        builder_ = builder;
        currentFunc_ = func;
        currentBlockIdx_ = 0;
        blockCounter_ = 0;
    }

    /// @brief Reset for a new function without changing the builder.
    /// @param func The new function to manage.
    void reset(Function *func) {
        currentFunc_ = func;
        currentBlockIdx_ = 0;
        blockCounter_ = 0;
    }

    // =========================================================================
    // Block Creation
    // =========================================================================

    /// @brief Create a new basic block with a unique name.
    /// @param base Base name for the block (e.g., "if_then", "loop_body").
    /// @return Index of the created block within the function.
    [[nodiscard]] std::size_t createBlock(const std::string &base) {
        requireBound();
        std::ostringstream oss;
        oss << base << "_" << blockCounter_++;
        builder_->createBlock(*currentFunc_, oss.str());
        return currentFunc_->blocks.size() - 1;
    }

    /// @brief Create a block with an exact name (no counter suffix).
    /// @param name Exact name for the block.
    /// @return Index of the created block.
    [[nodiscard]] std::size_t createBlockExact(const std::string &name) {
        requireBound();
        for (const auto &existing : currentFunc_->blocks) {
            if (existing.label == name)
                throw std::invalid_argument("duplicate basic block label: " + name);
        }
        builder_->createBlock(*currentFunc_, name);
        return currentFunc_->blocks.size() - 1;
    }

    // =========================================================================
    // Block Navigation
    // =========================================================================

    /// @brief Set the current block for instruction emission.
    /// @param blockIdx Index of the block to make current.
    void setBlock(std::size_t blockIdx) {
        requireBound();
        if (blockIdx >= currentFunc_->blocks.size())
            throw std::out_of_range("basic block index out of range");
        currentBlockIdx_ = blockIdx;
        builder_->setInsertPoint(currentFunc_->blocks[blockIdx]);
    }

    /// @brief Get the current block.
    /// @return Pointer to the current basic block.
    [[nodiscard]] BasicBlock *currentBlock() {
        requireCurrentBlock();
        return &currentFunc_->blocks[currentBlockIdx_];
    }

    /// @brief Get the current block (const).
    [[nodiscard]] const BasicBlock *currentBlock() const {
        requireCurrentBlock();
        return &currentFunc_->blocks[currentBlockIdx_];
    }

    /// @brief Get a block by index.
    /// @param idx Index of the block.
    /// @return Reference to the block.
    [[nodiscard]] BasicBlock &getBlock(std::size_t idx) {
        requireBound();
        return currentFunc_->blocks.at(idx);
    }

    /// @brief Get the current block index.
    [[nodiscard]] std::size_t currentBlockIndex() const noexcept {
        return currentBlockIdx_;
    }

    /// @brief Get the label for a block by index.
    /// @param idx Index of the block.
    /// @return The block's label.
    [[nodiscard]] const std::string &getBlockLabel(std::size_t idx) const {
        requireBound();
        return currentFunc_->blocks.at(idx).label;
    }

    // =========================================================================
    // State Queries
    // =========================================================================

    /// @brief Check if the current block is terminated.
    [[nodiscard]] bool isTerminated() const {
        requireCurrentBlock();
        return currentFunc_->blocks[currentBlockIdx_].terminated;
    }

    /// @brief Get the number of blocks in the current function.
    [[nodiscard]] std::size_t blockCount() const {
        requireBound();
        return currentFunc_->blocks.size();
    }

    /// @brief Get the current function.
    [[nodiscard]] Function *function() {
        return currentFunc_;
    }

    /// @brief Get the current function (const).
    [[nodiscard]] const Function *function() const {
        return currentFunc_;
    }

    /// @brief Get the next block counter value (for external naming).
    [[nodiscard]] unsigned nextBlockId() const noexcept {
        return blockCounter_;
    }

    /// @brief Restore the next block counter value (for saved/restore contexts).
    /// @param nextId Next suffix value to use when creating new blocks.
    void setNextBlockId(unsigned nextId) noexcept {
        blockCounter_ = nextId;
    }

  private:
    void requireBound() const {
        if (!builder_ || !currentFunc_)
            throw std::logic_error("BlockManager operation requires a bound builder and function");
    }

    void requireCurrentBlock() const {
        requireBound();
        if (currentBlockIdx_ >= currentFunc_->blocks.size())
            throw std::logic_error("BlockManager has no current basic block");
    }

    il::build::IRBuilder *builder_{nullptr};
    Function *currentFunc_{nullptr};
    std::size_t currentBlockIdx_{0};
    unsigned blockCounter_{0};
};

} // namespace il::frontends::common

// File: src/frontends/basic/LowererContext.hpp
// License: GPL-3.0-only. See LICENSE in the project root for full license
//          information.
// Purpose: Defines helper context structures embedded in Lowerer for
//          per-procedure state, deterministic block naming, and loop tracking.
// Key invariants: Context state is reset between procedures and block labels
//                 remain deterministic within a procedure.
// Ownership/Lifetime: Owned by Lowerer; references to IL objects are borrowed
//                     for the duration of lowering.
// Links: docs/codemap.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Value.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// All context structures are in the il::frontends::basic namespace
namespace il::frontends::basic
{

using Function = il::core::Function;
using BasicBlock = il::core::BasicBlock;
using Value = il::core::Value;

struct BlockNamer
{
    std::string proc;                                          ///< procedure name
    unsigned ifCounter{0};                                     ///< sequential IF identifiers
    unsigned loopCounter{0};                                   ///< WHILE/FOR/call_cont identifiers
    std::unordered_map<std::string, unsigned> genericCounters; ///< other shapes

    explicit BlockNamer(std::string p) : proc(std::move(p)) {}

    [[nodiscard]] std::string entry() const
    {
        return "entry_" + proc;
    }

    [[nodiscard]] std::string ret() const
    {
        return "ret_" + proc;
    }

    [[nodiscard]] std::string line(int line) const
    {
        return "L" + std::to_string(line) + "_" + proc;
    }

    unsigned nextIf()
    {
        return ifCounter++;
    }

    [[nodiscard]] std::string ifTest(unsigned id) const
    {
        return "if_test_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string ifThen(unsigned id) const
    {
        return "if_then_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string ifElse(unsigned id) const
    {
        return "if_else_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string ifEnd(unsigned id) const
    {
        return "if_end_" + std::to_string(id) + "_" + proc;
    }

    unsigned nextWhile()
    {
        return loopCounter++;
    }

    [[nodiscard]] std::string whileHead(unsigned id) const
    {
        return "while_head_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string whileBody(unsigned id) const
    {
        return "while_body_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string whileEnd(unsigned id) const
    {
        return "while_end_" + std::to_string(id) + "_" + proc;
    }

    unsigned nextDo()
    {
        return loopCounter++;
    }

    [[nodiscard]] std::string doHead(unsigned id) const
    {
        return "do_head_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string doBody(unsigned id) const
    {
        return "do_body_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string doEnd(unsigned id) const
    {
        return "do_end_" + std::to_string(id) + "_" + proc;
    }

    unsigned nextFor()
    {
        return loopCounter++;
    }

    /// @brief Allocate next sequential ID for a call continuation.
    unsigned nextCall()
    {
        return loopCounter++;
    }

    [[nodiscard]] std::string forHead(unsigned id) const
    {
        return "for_head_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string forBody(unsigned id) const
    {
        return "for_body_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string forInc(unsigned id) const
    {
        return "for_inc_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string forEnd(unsigned id) const
    {
        return "for_end_" + std::to_string(id) + "_" + proc;
    }

    /// @brief Build label for a synthetic call continuation block.
    [[nodiscard]] std::string callCont(unsigned id) const
    {
        return "call_cont_" + std::to_string(id) + "_" + proc;
    }

    [[nodiscard]] std::string generic(const std::string &hint)
    {
        auto &n = genericCounters[hint];
        return hint + "_" + std::to_string(n++) + "_" + proc;
    }

    [[nodiscard]] std::string tag(const std::string &base) const
    {
        return base + "_" + proc;
    }
};

struct ForBlocks
{
    size_t headIdx{0};
    size_t headPosIdx{0};
    size_t headNegIdx{0};
    size_t bodyIdx{0};
    size_t incIdx{0};
    size_t doneIdx{0};
};

struct ProcedureContext
{
    struct BlockNameState
    {
        void reset() noexcept
        {
            lineBlocks_.clear();
            namer_.reset();
        }

        [[nodiscard]] std::unordered_map<int, size_t> &lineBlocks() noexcept
        {
            return lineBlocks_;
        }

        [[nodiscard]] const std::unordered_map<int, size_t> &lineBlocks() const noexcept
        {
            return lineBlocks_;
        }

        [[nodiscard]] BlockNamer *namer() noexcept
        {
            return namer_.get();
        }

        [[nodiscard]] const BlockNamer *namer() const noexcept
        {
            return namer_.get();
        }

        void setNamer(std::unique_ptr<BlockNamer> namer) noexcept
        {
            namer_ = std::move(namer);
        }

        void resetNamer() noexcept
        {
            namer_.reset();
        }

      private:
        std::unordered_map<int, size_t> lineBlocks_;
        std::unique_ptr<BlockNamer> namer_;
    };

    struct LoopState
    {
        void reset() noexcept
        {
            function_ = nullptr;
            exitTargetIdx_.clear();
            exitTaken_.clear();
        }

        void setFunction(Function *function) noexcept
        {
            function_ = function;
            exitTargetIdx_.clear();
            exitTaken_.clear();
        }

        void push(BasicBlock *exitBlock)
        {
            if (function_)
            {
                auto base = &function_->blocks[0];
                exitTargetIdx_.push_back(static_cast<size_t>(exitBlock - base));
            }
            else
            {
                exitTargetIdx_.push_back(0);
            }
            exitTaken_.push_back(false);
        }

        void pop()
        {
            if (exitTargetIdx_.empty())
                return;
            exitTargetIdx_.pop_back();
            exitTaken_.pop_back();
        }

        [[nodiscard]] BasicBlock *current() const
        {
            if (exitTargetIdx_.empty() || !function_)
                return nullptr;
            size_t idx = exitTargetIdx_.back();
            if (idx >= function_->blocks.size())
                return nullptr;
            return &function_->blocks[idx];
        }

        void markTaken()
        {
            if (!exitTaken_.empty())
                exitTaken_.back() = true;
        }

        void refresh(BasicBlock *exitBlock)
        {
            if (exitTargetIdx_.empty() || !function_)
                return;
            auto base = &function_->blocks[0];
            exitTargetIdx_.back() = static_cast<size_t>(exitBlock - base);
        }

        [[nodiscard]] bool taken() const
        {
            return !exitTaken_.empty() && exitTaken_.back();
        }

      private:
        Function *function_{nullptr};
        std::vector<size_t> exitTargetIdx_;
        std::vector<bool> exitTaken_;
    };

    struct ErrorHandlerState
    {
        void reset() noexcept
        {
            active_ = false;
            activeIndex_.reset();
            activeLine_.reset();
            blocks_.clear();
            handlerTargets_.clear();
        }

        [[nodiscard]] bool active() const noexcept
        {
            return active_;
        }

        void setActive(bool active) noexcept
        {
            active_ = active;
        }

        [[nodiscard]] std::optional<size_t> activeIndex() const noexcept
        {
            return activeIndex_;
        }

        void setActiveIndex(std::optional<size_t> index) noexcept
        {
            activeIndex_ = index;
        }

        [[nodiscard]] std::optional<int> activeLine() const noexcept
        {
            return activeLine_;
        }

        void setActiveLine(std::optional<int> line) noexcept
        {
            activeLine_ = line;
        }

        [[nodiscard]] std::unordered_map<int, size_t> &blocks() noexcept
        {
            return blocks_;
        }

        [[nodiscard]] const std::unordered_map<int, size_t> &blocks() const noexcept
        {
            return blocks_;
        }

        [[nodiscard]] std::unordered_map<size_t, int> &handlerTargets() noexcept
        {
            return handlerTargets_;
        }

        [[nodiscard]] const std::unordered_map<size_t, int> &handlerTargets() const noexcept
        {
            return handlerTargets_;
        }

      private:
        bool active_{false};
        std::optional<size_t> activeIndex_{};
        std::optional<int> activeLine_{};
        std::unordered_map<int, size_t> blocks_;
        std::unordered_map<size_t, int> handlerTargets_;
    };

    struct GosubState
    {
        void reset() noexcept
        {
            hasPrologue_ = false;
            spSlot_ = Value{};
            stackSlot_ = Value{};
            continuationBlocks_.clear();
            stmtToIndex_.clear();
        }

        void clearContinuations() noexcept
        {
            continuationBlocks_.clear();
            stmtToIndex_.clear();
        }

        void setPrologue(Value spSlot, Value stackSlot) noexcept
        {
            hasPrologue_ = true;
            spSlot_ = spSlot;
            stackSlot_ = stackSlot;
        }

        [[nodiscard]] bool hasPrologue() const noexcept
        {
            return hasPrologue_;
        }

        [[nodiscard]] Value spSlot() const noexcept
        {
            return spSlot_;
        }

        [[nodiscard]] Value stackSlot() const noexcept
        {
            return stackSlot_;
        }

        unsigned registerContinuation(const GosubStmt *stmt, size_t blockIdx)
        {
            unsigned idx = static_cast<unsigned>(continuationBlocks_.size());
            continuationBlocks_.push_back(blockIdx);
            stmtToIndex_[stmt] = idx;
            return idx;
        }

        [[nodiscard]] std::optional<unsigned> indexFor(const GosubStmt *stmt) const noexcept
        {
            auto it = stmtToIndex_.find(stmt);
            if (it == stmtToIndex_.end())
                return std::nullopt;
            return it->second;
        }

        [[nodiscard]] size_t blockIndexFor(unsigned idx) const
        {
            return continuationBlocks_.at(idx);
        }

        [[nodiscard]] const std::vector<size_t> &continuations() const noexcept
        {
            return continuationBlocks_;
        }

      private:
        bool hasPrologue_{false};
        Value spSlot_{};
        Value stackSlot_{};
        std::vector<size_t> continuationBlocks_{};
        std::unordered_map<const GosubStmt *, unsigned> stmtToIndex_{};
    };

    void reset() noexcept
    {
        function_ = nullptr;
        current_ = nullptr;
        exitIndex_ = 0;
        nextTemp_ = 0;
        boundsCheckId_ = 0;
        blockNames_.reset();
        loopState_.reset();
        errorHandlers_.reset();
        gosub_.reset();
    }

    [[nodiscard]] Function *function() const noexcept
    {
        return function_;
    }

    void setFunction(Function *function) noexcept
    {
        function_ = function;
        loopState_.setFunction(function);
    }

    [[nodiscard]] BasicBlock *current() const noexcept
    {
        return current_;
    }

    void setCurrent(BasicBlock *block) noexcept
    {
        current_ = block;
    }

    [[nodiscard]] size_t currentIndex() const noexcept
    {
        auto *f = function();
        return static_cast<size_t>(current_ - &f->blocks.front());
    }

    void setCurrentByIndex(size_t idx) noexcept
    {
        auto *f = function();
        setCurrent(&f->blocks[idx]);
    }

    [[nodiscard]] size_t exitIndex() const noexcept
    {
        return exitIndex_;
    }

    void setExitIndex(size_t index) noexcept
    {
        exitIndex_ = index;
    }

    [[nodiscard]] unsigned nextTemp() const noexcept
    {
        return nextTemp_;
    }

    void setNextTemp(unsigned next) noexcept
    {
        nextTemp_ = next;
    }

    [[nodiscard]] unsigned boundsCheckId() const noexcept
    {
        return boundsCheckId_;
    }

    void setBoundsCheckId(unsigned id) noexcept
    {
        boundsCheckId_ = id;
    }

    unsigned consumeBoundsCheckId() noexcept
    {
        return boundsCheckId_++;
    }

    [[nodiscard]] LoopState &loopState() noexcept
    {
        return loopState_;
    }

    [[nodiscard]] const LoopState &loopState() const noexcept
    {
        return loopState_;
    }

    [[nodiscard]] BlockNameState &blockNames() noexcept
    {
        return blockNames_;
    }

    [[nodiscard]] const BlockNameState &blockNames() const noexcept
    {
        return blockNames_;
    }

    [[nodiscard]] ErrorHandlerState &errorHandlers() noexcept
    {
        return errorHandlers_;
    }

    [[nodiscard]] const ErrorHandlerState &errorHandlers() const noexcept
    {
        return errorHandlers_;
    }

    [[nodiscard]] GosubState &gosub() noexcept
    {
        return gosub_;
    }

    [[nodiscard]] const GosubState &gosub() const noexcept
    {
        return gosub_;
    }

  private:
    Function *function_{nullptr};
    BasicBlock *current_{nullptr};
    size_t exitIndex_{0};
    unsigned nextTemp_{0};
    unsigned boundsCheckId_{0};
    BlockNameState blockNames_{};
    LoopState loopState_{};
    ErrorHandlerState errorHandlers_{};
    GosubState gosub_{};
};

} // namespace il::frontends::basic

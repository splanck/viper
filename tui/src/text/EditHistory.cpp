//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `EditHistory` container used by the terminal UI text editor to
// group user edits into undo/redo transactions.  Operations are intentionally
// stored by value so replay remains deterministic even when the live buffer is
// mutated elsewhere.  The implementation favours readability over micro
// optimisations; edits are typically tiny and the history depth shallow.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Implementation of grouped undo/redo support for the TUI text editor.
/// @details The edit history records insert/erase operations inside transactions
///          so related changes (for example, typing a word) can be undone in a
///          single step.  Transactions are pushed onto undo/redo stacks and
///          replayed via callbacks provided by the owning text buffer.

#include "tui/text/EditHistory.hpp"

#include <utility>

namespace viper::tui::text
{
/// @brief Begin a new transaction, grouping subsequent edits together.
/// @details When a transaction is already open the call is ignored, allowing
///          nested begin/end pairs from higher-level code to collapse into a
///          single logical unit.  Starting a transaction clears any partially
///          recorded operations so the caller can append fresh edits.
void EditHistory::beginTxn()
{
    if (in_txn_)
    {
        return;
    }
    in_txn_ = true;
    current_.clear();
}

/// @brief Finalise the current transaction and push it onto the undo stack.
/// @details When the transaction is empty the function simply clears the scratch
///          buffer; otherwise the recorded operations are moved into the undo
///          stack and the redo stack is invalidated because the edit history now
///          diverges from the previously undone timeline.
void EditHistory::endTxn()
{
    if (!in_txn_)
    {
        return;
    }
    in_txn_ = false;
    if (!current_.empty())
    {
        undo_stack_.push_back(std::move(current_));
        current_ = Txn{};
        redo_stack_.clear();
    }
    else
    {
        current_.clear();
    }
}

/// @brief Record an insertion operation at @p pos within the current transaction.
/// @details Inserts are stored by value so undo/redo callbacks receive the exact
///          text that was originally added, regardless of subsequent buffer
///          mutations.  The helper delegates to @ref append to handle transaction
///          grouping.
void EditHistory::recordInsert(std::size_t pos, std::string text)
{
    append(Op{OpType::Insert, pos, std::move(text)});
}

/// @brief Record an erase operation that removed @p text at offset @p pos.
/// @details Erase operations capture the deleted text so undo can faithfully
///          restore it.  Like insertions, they are funnelled through @ref append
///          to honour the active transaction and reset the redo stack.
void EditHistory::recordErase(std::size_t pos, std::string text)
{
    append(Op{OpType::Erase, pos, std::move(text)});
}

/// @brief Undo the most recent transaction and replay its inverse operations.
/// @details Pops the last transaction off the undo stack, replays the contained
///          operations in reverse order via the provided callback, and pushes the
///          transaction onto the redo stack.  Returns @c false when no undo state
///          is available.
bool EditHistory::undo(const Replay &replay)
{
    if (undo_stack_.empty())
    {
        return false;
    }

    Txn txn = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    for (auto it = txn.rbegin(); it != txn.rend(); ++it)
    {
        replay(*it);
    }
    redo_stack_.push_back(std::move(txn));
    return true;
}

/// @brief Reapply the most recently undone transaction.
/// @details Transactions are popped from the redo stack, replayed in forward
///          order through @p replay, and appended back to the undo stack so the
///          history returns to its pre-undo state.  Returns @c false when redo is
///          not possible.
bool EditHistory::redo(const Replay &replay)
{
    if (redo_stack_.empty())
    {
        return false;
    }

    Txn txn = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    for (const auto &op : txn)
    {
        replay(op);
    }
    undo_stack_.push_back(std::move(txn));
    return true;
}

/// @brief Discard all undo/redo history and reset the transaction state.
/// @details Clears both stacks, removes any in-flight transaction data, and marks
///          the history as not inside a transaction.  Useful when the owning text
///          buffer loads new content.
void EditHistory::clear()
{
    undo_stack_.clear();
    redo_stack_.clear();
    current_.clear();
    in_txn_ = false;
}

/// @brief Append an operation to the history, respecting the active transaction.
/// @details Operations with empty payloads are ignored to avoid generating
///          redundant history entries.  When inside a transaction the operation is
///          appended to the scratch buffer; otherwise a single-operation
///          transaction is pushed onto the undo stack.  Any new edit invalidates
///          the redo stack so redo cannot cross divergent histories.
void EditHistory::append(Op op)
{
    if (op.text.empty())
    {
        return;
    }

    redo_stack_.clear();
    if (in_txn_)
    {
        current_.push_back(std::move(op));
        return;
    }

    Txn txn;
    txn.push_back(std::move(op));
    undo_stack_.push_back(std::move(txn));
}
} // namespace viper::tui::text

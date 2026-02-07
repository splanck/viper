//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the EditHistory class, which implements transactional
// undo/redo for Viper's TUI text editor. Edit operations (insertions and
// erasures) are grouped into transactions that can be atomically undone
// or redone.
//
// The history maintains two stacks: an undo stack of committed transactions
// and a redo stack of undone transactions. Each transaction is a vector of
// individual operations (Op) that are replayed in reverse order for undo
// and forward order for redo.
//
// Transactions are demarcated by beginTxn()/endTxn() calls. All edit
// operations recorded between these calls are grouped together. Recording
// a new operation clears the redo stack (forking the history).
//
// Key invariants:
//   - Recording any edit clears the redo stack.
//   - Undo replays operations in reverse within a transaction.
//   - Redo replays operations in forward order.
//   - The current transaction is not on the undo stack until endTxn().
//
// Ownership: EditHistory owns all transaction data by value. Op structs
// own their text strings. Replay callbacks are borrowed during undo/redo.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace viper::tui::text
{
/// @brief Transactional undo/redo system for text editing operations.
/// @details Groups insert and erase operations into transactions that can be
///          atomically undone and redone. Maintains twin stacks (undo/redo)
///          of committed transactions. Recording any new edit forks the history
///          by clearing the redo stack.
class EditHistory
{
  public:
    /// @brief Discriminator for the kind of edit operation stored in a transaction.
    enum class OpType
    {
        Insert,
        Erase
    };

    /// @brief Single edit operation payload storing the type, byte position, and affected text.
    /// @details Represents either an insertion or an erasure at a specific byte position.
    ///          The text field contains the inserted or erased text for replay.
    struct Op
    {
        OpType type{};
        std::size_t pos{};
        std::string text{};
    };

    /// @brief A transaction is a group of related edit operations that are undone/redone atomically.
    using Txn = std::vector<Op>;

    /// @brief Callback signature invoked for each operation during undo/redo replay.
    /// @details The callback receives each Op in the transaction and should apply
    ///          the inverse operation (for undo) or the forward operation (for redo)
    ///          to the underlying text buffer.
    using Replay = std::function<void(const Op &)>;

    /// @brief Begin a new transaction, grouping subsequent edit recordings.
    /// @details Must be paired with endTxn(). Nested transactions are not supported;
    ///          calling beginTxn() while already in a transaction has no additional effect.
    void beginTxn();

    /// @brief Commit the current transaction to the undo stack.
    /// @details If the current transaction contains operations, it is pushed onto the
    ///          undo stack. If it is empty, nothing is recorded. The redo stack is not
    ///          affected.
    void endTxn();

    /// @brief Record an insertion operation in the current transaction.
    /// @details Appends an Insert op to the current transaction and clears the redo
    ///          stack, forking the edit history at this point.
    /// @param pos Byte offset where text was inserted.
    /// @param text The text that was inserted.
    void recordInsert(std::size_t pos, std::string text);

    /// @brief Record an erasure operation in the current transaction.
    /// @details Appends an Erase op to the current transaction and clears the redo stack.
    /// @param pos Byte offset where text was erased.
    /// @param text The text that was erased (preserved for undo replay).
    void recordErase(std::size_t pos, std::string text);

    /// @brief Undo the last committed transaction by replaying operations in reverse.
    /// @details Pops the top transaction from the undo stack and invokes the replay
    ///          callback for each operation in reverse order. The transaction is moved
    ///          to the redo stack.
    /// @param replay Callback invoked for each reversed operation.
    /// @return True if a transaction was undone; false if the undo stack was empty.
    [[nodiscard]] bool undo(const Replay &replay);

    /// @brief Redo the last undone transaction by replaying operations forward.
    /// @details Pops the top transaction from the redo stack and invokes the replay
    ///          callback for each operation in forward order. The transaction is moved
    ///          back to the undo stack.
    /// @param replay Callback invoked for each forward operation.
    /// @return True if a transaction was redone; false if the redo stack was empty.
    [[nodiscard]] bool redo(const Replay &replay);

    /// @brief Reset both stacks and cancel any active transaction.
    /// @details Clears all undo and redo history and discards any in-progress transaction.
    void clear();

  private:
    void append(Op op);

    std::vector<Txn> undo_stack_{};
    std::vector<Txn> redo_stack_{};
    Txn current_{};
    bool in_txn_{};
};
} // namespace viper::tui::text

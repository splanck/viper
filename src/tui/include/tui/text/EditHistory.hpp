// tui/include/tui/text/EditHistory.hpp
// @brief Tracks grouped edit operations supporting undo/redo replay.
// @invariant Transactions replay in recorded order with preserved payloads.
// @ownership EditHistory owns stored operation payload strings.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace viper::tui::text
{
/// @brief Stores edit transactions for undo/redo sequencing.
class EditHistory
{
  public:
    /// @brief Operation kind stored per edit.
    enum class OpType
    {
        Insert,
        Erase
    };

    /// @brief Single edit operation payload.
    struct Op
    {
        OpType type{};
        std::size_t pos{};
        std::string text{};
    };

    /// @brief Grouped edits forming a transaction.
    using Txn = std::vector<Op>;

    /// @brief Callback executed for each operation during replay.
    using Replay = std::function<void(const Op &)>;

    /// @brief Begin aggregating operations into a transaction.
    void beginTxn();

    /// @brief Commit current transaction when non-empty.
    void endTxn();

    /// @brief Record an insert operation (clears redo stack).
    void recordInsert(std::size_t pos, std::string text);

    /// @brief Record an erase operation (clears redo stack).
    void recordErase(std::size_t pos, std::string text);

    /// @brief Undo last transaction via replay callback.
    [[nodiscard]] bool undo(const Replay &replay);

    /// @brief Redo last undone transaction via replay callback.
    [[nodiscard]] bool redo(const Replay &replay);

    /// @brief Reset stacks and cancel active transaction.
    void clear();

  private:
    void append(Op op);

    std::vector<Txn> undo_stack_{};
    std::vector<Txn> redo_stack_{};
    Txn current_{};
    bool in_txn_{};
};
} // namespace viper::tui::text

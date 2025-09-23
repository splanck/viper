// tui/src/text/EditHistory.cpp
// @brief EditHistory implementation managing grouped undo/redo operations.
// @invariant Replay order mirrors stack semantics with preserved payload text.
// @ownership Stores operation payload copies for deterministic playback.

#include "tui/text/EditHistory.hpp"

#include <utility>

namespace viper::tui::text
{
void EditHistory::beginTxn()
{
    if (in_txn_)
    {
        return;
    }
    in_txn_ = true;
    current_.clear();
}

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

void EditHistory::recordInsert(std::size_t pos, std::string text)
{
    append(Op{OpType::Insert, pos, std::move(text)});
}

void EditHistory::recordErase(std::size_t pos, std::string text)
{
    append(Op{OpType::Erase, pos, std::move(text)});
}

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

void EditHistory::clear()
{
    undo_stack_.clear();
    redo_stack_.clear();
    current_.clear();
    in_txn_ = false;
}

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

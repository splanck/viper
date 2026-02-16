//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

/// @file lru_list.hpp
/// @brief Intrusive doubly-linked LRU list operations.
///
/// @details
/// Provides reusable remove/add-to-head/touch operations for any node type
/// that has `lru_prev` and `lru_next` pointer members. Used by BlockCache
/// and InodeCache to avoid duplicating identical LRU management logic.
///
/// Nodes are expected to have:
///   T *lru_prev;
///   T *lru_next;

namespace lib {

/// @brief Remove a node from an intrusive LRU doubly-linked list.
/// @param node   The node to remove.
/// @param head   Reference to the list head pointer.
/// @param tail   Reference to the list tail pointer.
template <typename T>
inline void lru_remove(T *node, T *&head, T *&tail) {
    if (node->lru_prev) {
        node->lru_prev->lru_next = node->lru_next;
    } else {
        head = node->lru_next;
    }

    if (node->lru_next) {
        node->lru_next->lru_prev = node->lru_prev;
    } else {
        tail = node->lru_prev;
    }

    node->lru_prev = nullptr;
    node->lru_next = nullptr;
}

/// @brief Insert a node at the head (most-recently-used end) of the LRU list.
/// @param node   The node to insert.
/// @param head   Reference to the list head pointer.
/// @param tail   Reference to the list tail pointer.
template <typename T>
inline void lru_add_head(T *node, T *&head, T *&tail) {
    node->lru_prev = nullptr;
    node->lru_next = head;

    if (head) {
        head->lru_prev = node;
    }
    head = node;

    if (!tail) {
        tail = node;
    }
}

/// @brief Move a node to the head of the LRU list (touch / mark as recently used).
/// @param node   The node to promote.
/// @param head   Reference to the list head pointer.
/// @param tail   Reference to the list tail pointer.
template <typename T>
inline void lru_touch(T *node, T *&head, T *&tail) {
    if (node == head) {
        return; // Already at head
    }

    lru_remove(node, head, tail);
    lru_add_head(node, head, tail);
}

} // namespace lib

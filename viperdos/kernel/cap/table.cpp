//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "table.hpp"
#include "../console/serial.hpp"
#include "../mm/kheap.hpp"

/**
 * @file table.cpp
 * @brief Capability table implementation.
 *
 * @details
 * The capability table uses an array of entries allocated from the kernel heap.
 * A singly-linked free list is stored in the `Entry::object` field while a slot
 * is unused (Kind::Invalid). This avoids additional metadata allocations.
 *
 * Stale handle detection is implemented by an 8-bit generation counter stored
 * in each entry and encoded into the public handle. When a slot is removed, the
 * generation is incremented so older handles can no longer resolve.
 */
namespace cap {

/** @copydoc cap::Table::init */
bool Table::init(usize capacity) {
    entries_ = static_cast<Entry *>(kheap::kzalloc(capacity * sizeof(Entry)));
    if (!entries_) {
        serial::puts("[cap] ERROR: Failed to allocate capability table\n");
        return false;
    }

    capacity_ = capacity;
    count_ = 0;

    // Build free list using object pointer as next index
    // Use 0xFFFFFFFF to mark end of free list
    for (usize i = 0; i < capacity - 1; i++) {
        entries_[i].object = reinterpret_cast<void *>(i + 1);
        entries_[i].kind = Kind::Invalid;
        entries_[i].generation = 0;
        entries_[i].parent_index = NO_PARENT;
    }
    entries_[capacity - 1].object = reinterpret_cast<void *>(0xFFFFFFFFUL);
    entries_[capacity - 1].kind = Kind::Invalid;
    entries_[capacity - 1].generation = 0;
    entries_[capacity - 1].parent_index = NO_PARENT;
    free_head_ = 0;

    serial::puts("[cap] Created capability table with ");
    serial::put_dec(capacity);
    serial::puts(" slots\n");

    return true;
}

/** @copydoc cap::Table::destroy */
void Table::destroy() {
    if (entries_) {
        kheap::kfree(entries_);
        entries_ = nullptr;
    }
    capacity_ = 0;
    count_ = 0;
    free_head_ = 0;
}

/** @copydoc cap::Table::insert */
Handle Table::insert(void *object, Kind kind, Rights rights) {
    SpinlockGuard guard(lock_);

    if (free_head_ == 0xFFFFFFFF) {
        serial::puts("[cap] ERROR: Capability table full\n");
        return HANDLE_INVALID;
    }

    u32 index = free_head_;
    Entry &e = entries_[index];

    // Advance free list
    free_head_ = static_cast<u32>(reinterpret_cast<uintptr>(e.object));

    // Fill entry
    e.object = object;
    e.kind = kind;
    e.rights = static_cast<u32>(rights);
    e.parent_index = NO_PARENT; // Root capability (not derived)
    // Generation already set from previous use (or 0 initially)

    count_++;

    return make_handle(index, e.generation);
}

/** @copydoc cap::Table::insert_bounded */
Handle Table::insert_bounded(void *object, Kind kind, Rights rights, u32 bounding_set) {
    // Mask requested rights by the process's capability bounding set
    Rights bounded_rights = static_cast<Rights>(static_cast<u32>(rights) & bounding_set);
    return insert(object, kind, bounded_rights);
}

// Internal unlocked get - caller must hold lock_
static Entry *get_unlocked(Entry *entries, usize capacity, Handle h) {
    if (h == HANDLE_INVALID)
        return nullptr;

    u32 index = handle_index(h);
    u8 gen = handle_gen(h);

    if (index >= capacity)
        return nullptr;

    Entry &e = entries[index];
    if (e.kind == Kind::Invalid)
        return nullptr;
    if (e.generation != gen)
        return nullptr;

    return &e;
}

/** @copydoc cap::Table::get */
Entry *Table::get(Handle h) {
    SpinlockGuard guard(lock_);
    return get_unlocked(entries_, capacity_, h);
}

/** @copydoc cap::Table::get_checked */
Entry *Table::get_checked(Handle h, Kind expected_kind) {
    SpinlockGuard guard(lock_);
    Entry *e = get_unlocked(entries_, capacity_, h);
    if (!e)
        return nullptr;
    if (e->kind != expected_kind)
        return nullptr;
    return e;
}

/** @copydoc cap::Table::get_with_rights */
Entry *Table::get_with_rights(Handle h, Kind kind, Rights required) {
    SpinlockGuard guard(lock_);
    Entry *e = get_unlocked(entries_, capacity_, h);
    if (!e)
        return nullptr;
    if (e->kind != kind)
        return nullptr;
    if (!has_rights(e->rights, required)) {
        return nullptr;
    }
    return e;
}

/** @copydoc cap::Table::remove */
void Table::remove(Handle h) {
    SpinlockGuard guard(lock_);

    if (h == HANDLE_INVALID)
        return;

    u32 index = handle_index(h);
    if (index >= capacity_)
        return;

    Entry &e = entries_[index];
    if (e.kind == Kind::Invalid)
        return;

    // Increment generation for next use (detects use-after-free)
    e.generation++;
    e.kind = Kind::Invalid;
    e.rights = 0;

    // Add to free list
    e.object = reinterpret_cast<void *>(static_cast<uintptr>(free_head_));
    free_head_ = index;

    count_--;
}

/** @copydoc cap::Table::derive */
Handle Table::derive(Handle h, Rights new_rights) {
    SpinlockGuard guard(lock_);

    Entry *e = get_unlocked(entries_, capacity_, h);
    if (!e) {
        return HANDLE_INVALID;
    }

    // Must have DERIVE right on original handle
    if (!has_rights(e->rights, CAP_DERIVE)) {
        return HANDLE_INVALID;
    }

    // New rights cannot exceed original rights
    u32 allowed = e->rights & static_cast<u32>(new_rights);

    // Get parent's index for tracking derivation chain
    u32 parent_idx = handle_index(h);

    // Allocate a new slot
    if (free_head_ == 0xFFFFFFFF) {
        serial::puts("[cap] ERROR: Capability table full\n");
        return HANDLE_INVALID;
    }

    u32 index = free_head_;
    Entry &new_entry = entries_[index];

    // Advance free list
    free_head_ = static_cast<u32>(reinterpret_cast<uintptr>(new_entry.object));

    // Fill entry with parent tracking
    new_entry.object = e->object;
    new_entry.kind = e->kind;
    new_entry.rights = allowed;
    new_entry.parent_index = parent_idx; // Track derivation chain

    count_++;

    return make_handle(index, new_entry.generation);
}

// Internal recursive revoke helper - caller must hold lock_
static u32 revoke_unlocked(Entry *entries, usize capacity, u32 &free_head, usize &count, Handle h) {
    if (h == HANDLE_INVALID)
        return 0;

    u32 index = handle_index(h);
    u8 gen = handle_gen(h);

    if (index >= capacity)
        return 0;

    Entry &e = entries[index];
    if (e.kind == Kind::Invalid)
        return 0;
    if (e.generation != gen)
        return 0;

    // Recursively revoke all children first
    u32 revoked = 0;
    for (usize i = 0; i < capacity; i++) {
        if (entries[i].kind != Kind::Invalid && entries[i].parent_index == index) {
            // This entry was derived from the handle we're revoking
            Handle child_handle = make_handle(static_cast<u32>(i), entries[i].generation);
            revoked += revoke_unlocked(entries, capacity, free_head, count, child_handle);
        }
    }

    // Now remove the original entry
    e.generation++;
    e.kind = Kind::Invalid;
    e.rights = 0;
    e.parent_index = NO_PARENT;

    // Add to free list
    e.object = reinterpret_cast<void *>(static_cast<uintptr>(free_head));
    free_head = index;

    count--;
    revoked++;

    return revoked;
}

/** @copydoc cap::Table::revoke */
u32 Table::revoke(Handle h) {
    SpinlockGuard guard(lock_);
    return revoke_unlocked(entries_, capacity_, free_head_, count_, h);
}

/** @copydoc cap::Table::entry_at */
Entry *Table::entry_at(usize index) {
    SpinlockGuard guard(lock_);

    if (index >= capacity_)
        return nullptr;
    return &entries_[index];
}

/** @copydoc cap::Table::generation_at */
u8 Table::generation_at(usize index) const {
    SpinlockGuard guard(lock_);

    if (index >= capacity_)
        return 0;
    return entries_[index].generation;
}

} // namespace cap

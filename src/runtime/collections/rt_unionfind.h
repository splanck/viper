//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_unionfind.h
// Purpose: Disjoint-set (Union-Find) data structure for efficient set merging and connectivity
// queries, using path compression and union by rank for near-constant-time operations.
//
// Key invariants:
//   - Elements are identified by integers in [0, n-1] set at creation.
//   - Uses path compression and union by rank for amortized O(alpha(n)) operations.
//   - rt_unionfind_connected returns 1 if two elements share a root.
//   - The number of sets starts at n and decreases by 1 per successful union.
//
// Ownership/Lifetime:
//   - UnionFind objects are GC-managed opaque pointers.
//   - Callers should not free unionfind objects directly.
//
// Links: src/runtime/collections/rt_unionfind.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new Union-Find with n elements (0..n-1).
    /// @param n Number of elements.
    /// @return Union-Find object.
    void *rt_unionfind_new(int64_t n);

    /// @brief Find the representative of the set containing x.
    /// @param uf Union-Find object.
    /// @param x Element index (0-based).
    /// @return Representative element of x's set.
    int64_t rt_unionfind_find(void *uf, int64_t x);

    /// @brief Merge the sets containing x and y.
    /// @param uf Union-Find object.
    /// @param x First element index.
    /// @param y Second element index.
    /// @return 1 if sets were merged, 0 if already in same set.
    int64_t rt_unionfind_union(void *uf, int64_t x, int64_t y);

    /// @brief Check if x and y are in the same set.
    /// @param uf Union-Find object.
    /// @param x First element index.
    /// @param y Second element index.
    /// @return true if connected, false otherwise.
    bool rt_unionfind_connected(void *uf, int64_t x, int64_t y);

    /// @brief Get the number of disjoint sets.
    /// @param uf Union-Find object.
    /// @return Number of sets.
    int64_t rt_unionfind_count(void *uf);

    /// @brief Get the size of the set containing x.
    /// @param uf Union-Find object.
    /// @param x Element index.
    /// @return Size of x's set.
    int64_t rt_unionfind_set_size(void *uf, int64_t x);

    /// @brief Reset all elements to individual sets.
    /// @param uf Union-Find object.
    void rt_unionfind_reset(void *uf);

#ifdef __cplusplus
}
#endif

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_unionfind.h
// Purpose: Disjoint set / Union-Find data structure.
// Key invariants: Uses path compression and union by rank.
// Ownership/Lifetime: UnionFind objects are GC-managed.
//
//===----------------------------------------------------------------------===//

#pragma once

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
    /// @return 1 if connected, 0 otherwise.
    int64_t rt_unionfind_connected(void *uf, int64_t x, int64_t y);

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

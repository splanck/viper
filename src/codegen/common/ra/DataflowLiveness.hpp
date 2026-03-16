//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/codegen/common/ra/DataflowLiveness.hpp
// Purpose: Generic backward dataflow liveness solver shared between all
//          backend register allocators. Computes liveIn/liveOut sets given
//          precomputed gen/kill sets and a successor relation. The algorithm
//          is the textbook fixed-point iteration:
//
//            liveOut[B] = union of liveIn[S] for all successors S of B
//            liveIn[B]  = gen[B] union (liveOut[B] - kill[B])
//
//          Callers provide the CFG (successors per block) and gen/kill sets;
//          the solver returns liveIn and liveOut vectors.
// Key invariants:
//   - Iteration bounded by maxIterations to prevent infinite loops
//   - Reverse block order for faster convergence
//   - Result sets are monotonically growing (lattice property)
// Ownership/Lifetime:
//   - Value semantics. Result struct owns its data.
// Links: src/codegen/x86_64/ra/Liveness.cpp,
//        src/codegen/aarch64/ra/Liveness.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace viper::codegen::ra
{

/// @brief Result of backward dataflow liveness analysis.
/// @tparam VregId Type used to identify virtual registers (typically uint16_t).
template <typename VregId = uint16_t> struct DataflowResult
{
    std::vector<std::unordered_set<VregId>> liveIn;
    std::vector<std::unordered_set<VregId>> liveOut;
};

/// @brief Solve backward dataflow equations to compute liveIn/liveOut sets.
///
/// @details Iterates the standard backward dataflow equations in reverse block
///          order until a fixed point is reached or @p maxIterations is
///          exceeded. The algorithm is architecture-agnostic — callers provide
///          pre-built gen/kill sets and successor relations from their MIR.
///
/// @tparam VregId Type used to identify virtual registers.
/// @param succs      Per-block successor indices.
/// @param gen        Per-block upward-exposed uses (used before defined in B).
/// @param kill       Per-block definitions.
/// @param maxIter    Safety bound on fixed-point iterations.
/// @return Computed liveIn and liveOut sets for each block.
template <typename VregId = uint16_t>
DataflowResult<VregId> solveBackwardDataflow(const std::vector<std::vector<std::size_t>> &succs,
                                             const std::vector<std::unordered_set<VregId>> &gen,
                                             const std::vector<std::unordered_set<VregId>> &kill,
                                             std::size_t maxIter = 1000)
{
    const std::size_t n = succs.size();
    assert(gen.size() == n && kill.size() == n);

    DataflowResult<VregId> result;
    result.liveIn.assign(n, {});
    result.liveOut.assign(n, {});

    bool changed = true;
    std::size_t iteration = 0;

    while (changed)
    {
        if (++iteration > maxIter)
        {
            assert(false && "Liveness dataflow did not converge");
            break;
        }
        changed = false;

        // Process blocks in reverse order for faster convergence on
        // forward-flowing programs.
        for (std::size_t i = n; i-- > 0;)
        {
            // liveOut[i] = union of liveIn[s] for all successors s
            std::unordered_set<VregId> newOut;
            for (std::size_t s : succs[i])
                for (VregId v : result.liveIn[s])
                    newOut.insert(v);

            // liveIn[i] = gen[i] union (liveOut[i] - kill[i])
            std::unordered_set<VregId> newIn = gen[i];
            for (VregId v : newOut)
                if (kill[i].count(v) == 0)
                    newIn.insert(v);

            if (newOut != result.liveOut[i] || newIn != result.liveIn[i])
            {
                result.liveOut[i] = std::move(newOut);
                result.liveIn[i] = std::move(newIn);
                changed = true;
            }
        }
    }

    return result;
}

/// @brief Build predecessor relations from a successor vector.
///
/// @details Constructs the reverse edge set so callers don't have to duplicate
///          this trivial logic. Both backends need predecessors for cross-block
///          register persistence.
///
/// @param succs Per-block successor indices.
/// @return Per-block predecessor indices.
inline std::vector<std::vector<std::size_t>> buildPredecessors(
    const std::vector<std::vector<std::size_t>> &succs)
{
    std::vector<std::vector<std::size_t>> preds(succs.size());
    for (std::size_t i = 0; i < succs.size(); ++i)
        for (std::size_t s : succs[i])
            preds[s].push_back(i);
    return preds;
}

} // namespace viper::codegen::ra

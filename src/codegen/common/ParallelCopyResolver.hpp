//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/ParallelCopyResolver.hpp
// Purpose: Target-independent parallel copy resolution via topological sort.
// Key invariants: Acyclic copies are emitted first via topological ordering;
//                 cycles are broken using a temporary spill via movVRegToTemp;
//                 output preserves the semantics of the parallel assignment.
// Ownership/Lifetime: Header-only utility with no global state.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

namespace viper::codegen::common
{

/// @brief A single parallel copy assignment from source to destination.
/// @tparam RegClassT The backend-specific register class enum type.
template <typename RegClassT>
struct CopyPair
{
    std::uint16_t srcV; ///< Source virtual register number.
    std::uint16_t dstV; ///< Destination virtual register number.
    RegClassT cls;      ///< Register class (GPR, FP, etc.) for this copy.
};

/// @brief Interface for emitting resolved copy instructions.
/// @tparam RegClassT The backend-specific register class enum type.
template <typename RegClassT>
struct CopyEmitter
{
    virtual void movVRegToVReg(RegClassT cls, std::uint16_t src, std::uint16_t dst) = 0;
    virtual void movVRegToTemp(RegClassT cls, std::uint16_t src) = 0;
    virtual void movTempToVReg(RegClassT cls, std::uint16_t dst) = 0;
    virtual ~CopyEmitter() = default;
};

namespace detail
{

template <typename RegClassT>
inline std::uint32_t findMaxVirtualRegister(const std::vector<CopyPair<RegClassT>> &pairs)
{
    std::uint32_t maxValue = 0U;
    const auto total = static_cast<std::uint32_t>(pairs.size());
    for (std::uint32_t index = 0U; index < total; ++index)
    {
        const auto &pair = pairs[index];
        if (pair.srcV > maxValue)
        {
            maxValue = pair.srcV;
        }
        if (pair.dstV > maxValue)
        {
            maxValue = pair.dstV;
        }
    }
    return maxValue;
}

template <typename RegClassT>
inline void resolveClassCopies(std::vector<CopyPair<RegClassT>> pairs,
                               CopyEmitter<RegClassT> &emitter)
{
    using Pair = CopyPair<RegClassT>;

    // Phase 0: Filter out self-copies (src == dst) as they require no action.
    std::vector<Pair> workList;
    workList.reserve(pairs.size());
    const auto inputCount = static_cast<std::uint32_t>(pairs.size());
    for (std::uint32_t index = 0U; index < inputCount; ++index)
    {
        const auto &pair = pairs[index];
        if (pair.srcV != pair.dstV)
        {
            workList.push_back(pair);
        }
    }

    if (workList.empty())
    {
        return;
    }

    // Build dependency graph.
    const std::uint32_t maxReg = findMaxVirtualRegister<RegClassT>(workList);
    std::vector<std::vector<std::uint32_t>> bySrc(maxReg + 1U);
    std::vector<std::uint32_t> indegree(maxReg + 1U, 0U);
    const auto total = static_cast<std::uint32_t>(workList.size());
    for (std::uint32_t index = 0U; index < total; ++index)
    {
        const auto &pair = workList[index];
        ++indegree[pair.dstV];
        bySrc[pair.srcV].push_back(index);
    }

    std::vector<std::uint32_t> ready;
    ready.reserve(total);
    std::vector<char> processed(total, 0);

    for (std::uint32_t index = 0U; index < total; ++index)
    {
        if (indegree[workList[index].srcV] == 0U)
        {
            ready.push_back(index);
        }
    }

    // Phase 1: Topological sort — emit acyclic copies in dependency order.
    while (!ready.empty())
    {
        const std::uint32_t index = ready.back();
        ready.pop_back();
        if (processed[index] != 0)
        {
            continue;
        }

        const auto &pair = workList[index];
        emitter.movVRegToVReg(pair.cls, pair.srcV, pair.dstV);
        processed[index] = 1;

        const std::uint32_t dst = pair.dstV;
        if (indegree[dst] > 0U)
        {
            --indegree[dst];
            if (indegree[dst] == 0U)
            {
                const auto &dependents = bySrc[dst];
                const auto dependentCount = static_cast<std::uint32_t>(dependents.size());
                for (std::uint32_t dep = 0U; dep < dependentCount; ++dep)
                {
                    const std::uint32_t dependentIndex = dependents[dep];
                    if (processed[dependentIndex] == 0)
                    {
                        ready.push_back(dependentIndex);
                    }
                }
            }
        }
    }

    // Phase 2: Cycle breaking.
    for (std::uint32_t index = 0U; index < total; ++index)
    {
        if (processed[index] != 0)
        {
            continue;
        }

        const Pair &startPair = workList[index];
        emitter.movVRegToTemp(startPair.cls, startPair.srcV);
        processed[index] = 1;

        const std::uint16_t startSrc = startPair.srcV;
        const std::uint16_t startDst = startPair.dstV;
        std::uint16_t current = startDst;

        while (current != startSrc)
        {
            const auto &candidates = bySrc[current];
            const auto candidateCount = static_cast<std::uint32_t>(candidates.size());
            std::uint32_t nextIndex = total;
            for (std::uint32_t c = 0U; c < candidateCount; ++c)
            {
                const std::uint32_t candidate = candidates[c];
                if (processed[candidate] == 0)
                {
                    nextIndex = candidate;
                    break;
                }
            }

            if (nextIndex == total)
            {
                break;
            }

            const Pair &chainPair = workList[nextIndex];
            emitter.movVRegToVReg(chainPair.cls, chainPair.srcV, chainPair.dstV);
            processed[nextIndex] = 1;
            current = chainPair.dstV;
        }

        emitter.movTempToVReg(startPair.cls, startDst);
    }
}

} // namespace detail

/// @brief Materialises a sequence of moves from parallel copy assignments.
/// @tparam RegClassT The backend-specific register class enum type.
template <typename RegClassT>
inline void resolveParallelCopies(std::vector<CopyPair<RegClassT>> pairs,
                                  CopyEmitter<RegClassT> &E)
{
    const auto total = static_cast<std::uint32_t>(pairs.size());
    if (total == 0U)
    {
        return;
    }

    std::vector<RegClassT> classes;
    classes.reserve(total);
    for (std::uint32_t index = 0U; index < total; ++index)
    {
        const RegClassT cls = pairs[index].cls;
        bool seen = false;
        const auto seenCount = static_cast<std::uint32_t>(classes.size());
        for (std::uint32_t c = 0U; c < seenCount; ++c)
        {
            if (classes[c] == cls)
            {
                seen = true;
                break;
            }
        }
        if (!seen)
        {
            classes.push_back(cls);
        }
    }

    const auto classCount = static_cast<std::uint32_t>(classes.size());
    for (std::uint32_t c = 0U; c < classCount; ++c)
    {
        const RegClassT cls = classes[c];
        std::vector<CopyPair<RegClassT>> perClass;
        for (std::uint32_t index = 0U; index < total; ++index)
        {
            if (pairs[index].cls == cls)
            {
                perClass.push_back(pairs[index]);
            }
        }

        detail::resolveClassCopies<RegClassT>(perClass, E);
    }
}

} // namespace viper::codegen::common

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/ParallelCopyResolver.hpp
// Purpose: Expand parallel register copy assignments into sequential moves.
// Key invariants: Acyclic copies are emitted first via topological ordering;
//                 cycles are broken using a temporary spill via movVRegToTemp;
//                 output preserves the semantics of the parallel assignment.
// Ownership/Lifetime: Header-only utility with no global state. All temporaries are scoped
//                     to the resolution call; caller owns the CopyEmitter implementation.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

namespace viper::codegen::x64
{

/// @brief Register class enumeration (forward declared).
enum class RegClass : int;

/// @brief A single parallel copy assignment from source to destination.
/// @details Represents one copy operation in a parallel assignment set.
///          Multiple CopyPairs form a parallel copy that must execute atomically.
struct CopyPair
{
    std::uint16_t srcV; ///< Source virtual register number.
    std::uint16_t dstV; ///< Destination virtual register number.
    RegClass cls;       ///< Register class (GPR, FP, etc.) for this copy.
};

/// @brief Interface for emitting resolved copy instructions.
/// @details Implementations translate abstract copy operations into target-specific
///          machine instructions. The resolver calls these methods in the order
///          needed to correctly materialize parallel copies.
struct CopyEmitter
{
    /// @brief Emit a register-to-register move.
    /// @param cls Register class of the operands.
    /// @param src Source virtual register.
    /// @param dst Destination virtual register.
    virtual void movVRegToVReg(RegClass cls, std::uint16_t src, std::uint16_t dst) = 0;

    /// @brief Spill a register to a temporary location (for cycle breaking).
    /// @param cls Register class of the operand.
    /// @param src Source virtual register to save.
    virtual void movVRegToTemp(RegClass cls, std::uint16_t src) = 0;

    /// @brief Restore from temporary to a register (for cycle breaking).
    /// @param cls Register class of the operand.
    /// @param dst Destination virtual register to restore into.
    virtual void movTempToVReg(RegClass cls, std::uint16_t dst) = 0;

    virtual ~CopyEmitter() = default;
};

namespace detail
{

/// @brief Find the maximum virtual register number in a set of copy pairs.
/// @details Used to size internal data structures for the resolution algorithm.
/// @param pairs Copy pairs to scan.
/// @return The maximum virtual register number found, or 0 if empty.
inline std::uint32_t findMaxVirtualRegister(const std::vector<CopyPair> &pairs)
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

/// @brief Resolve parallel copies for a single register class using topological sort.
/// @details The algorithm works in two phases:
///   Phase 1 (Topological Sort): Process acyclic copies by tracking in-degrees.
///     - A copy is "ready" when its source register has no pending writes (in-degree 0).
///     - After emitting a copy, decrement the in-degree of its destination, potentially
///       making dependent copies ready.
///   Phase 2 (Cycle Breaking): Any remaining unprocessed copies form cycles.
///     - Break each cycle by spilling one source to a temporary, emitting the chain,
///       then restoring from the temporary to complete the cycle.
/// @param pairs Copy pairs to resolve (all must be same register class).
/// @param emitter Interface for emitting move instructions.
inline void resolveClassCopies(std::vector<CopyPair> pairs, CopyEmitter &emitter)
{
    // Phase 0: Filter out self-copies (src == dst) as they require no action.
    std::vector<CopyPair> workList;
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

    // Build dependency graph:
    // - bySrc[r] = indices of copies that read from register r
    // - indegree[r] = count of copies that write to register r
    const std::uint32_t maxReg = findMaxVirtualRegister(workList);
    std::vector<std::vector<std::uint32_t>> bySrc(maxReg + 1U);
    std::vector<std::uint32_t> indegree(maxReg + 1U, 0U);
    const auto total = static_cast<std::uint32_t>(workList.size());
    for (std::uint32_t index = 0U; index < total; ++index)
    {
        const auto &pair = workList[index];
        ++indegree[pair.dstV];
        bySrc[pair.srcV].push_back(index);
    }

    // Initialize ready queue with copies whose source has no pending writes.
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

    // Phase 1: Topological sort - emit acyclic copies in dependency order.
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

    // Phase 2: Cycle breaking - remaining unprocessed copies form cycles.
    // For each cycle: spill one source to temp, emit chain, restore from temp.
    for (std::uint32_t index = 0U; index < total; ++index)
    {
        if (processed[index] != 0)
        {
            continue;
        }

        // Start of a cycle: save the source to a temporary register.
        const CopyPair &startPair = workList[index];
        emitter.movVRegToTemp(startPair.cls, startPair.srcV);
        processed[index] = 1;

        // Walk the cycle chain: emit copies until we reach the start source.
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

            const CopyPair &chainPair = workList[nextIndex];
            emitter.movVRegToVReg(chainPair.cls, chainPair.srcV, chainPair.dstV);
            processed[nextIndex] = 1;
            current = chainPair.dstV;
        }

        // Complete the cycle: restore from temp to the original destination.
        emitter.movTempToVReg(startPair.cls, startDst);
    }
}

} // namespace detail

/// \brief Materialises a sequence of moves from a set of parallel copy assignments.
/// \param pairs Parallel copy assignments, grouped by virtual register class.
/// \param E Interface used to emit the required move instructions.
/// \details The algorithm performs a simple in-degree analysis to schedule
///          acyclic moves first and falls back to breaking cycles with a temporary
///          virtual register spill via CopyEmitter::movVRegToTemp().
/// \note    Example usage:
/// \code{.cpp}
/// class DebugEmitter final : public CopyEmitter {
///  public:
///   void movVRegToVReg(RegClass, std::uint16_t src, std::uint16_t dst) override {
///     std::cout << "mov v" << src << " -> v" << dst << '\n';
///   }
///   void movVRegToTemp(RegClass, std::uint16_t src) override {
///     std::cout << "spill v" << src << " -> temp\n";
///   }
///   void movTempToVReg(RegClass, std::uint16_t dst) override {
///     std::cout << "restore temp -> v" << dst << '\n';
///   }
/// };
///
/// std::vector<CopyPair> copies = {
///   {0, 1, RegClass::GPR},
///   {1, 2, RegClass::GPR},
///   {2, 0, RegClass::GPR},
/// };
/// DebugEmitter emitter;
/// resolveParallelCopies(copies, emitter);
/// \endcode
/// Produces:
/// \code{.text}
/// spill v0 -> temp
/// mov v1 -> v2
/// mov v2 -> v0
/// restore temp -> v1
/// \endcode
inline void resolveParallelCopies(std::vector<CopyPair> pairs, CopyEmitter &E)
{
    const auto total = static_cast<std::uint32_t>(pairs.size());
    if (total == 0U)
    {
        return;
    }

    std::vector<RegClass> classes;
    classes.reserve(total);
    for (std::uint32_t index = 0U; index < total; ++index)
    {
        const RegClass cls = pairs[index].cls;
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
        const RegClass cls = classes[c];
        std::vector<CopyPair> perClass;
        for (std::uint32_t index = 0U; index < total; ++index)
        {
            if (pairs[index].cls == cls)
            {
                perClass.push_back(pairs[index]);
            }
        }

        detail::resolveClassCopies(perClass, E);
    }
}

} // namespace viper::codegen::x64

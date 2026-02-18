//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/analysis/MemorySSA.hpp
// Purpose: Lightweight memory SSA analysis providing def-use chains for
//          memory-touching operations. Each store, load, and call is assigned
//          a MemoryAccess node; stores produce MemoryDefs and loads produce
//          MemoryUses. MemoryPhis are inserted at control-flow join points.
//          The primary consumer is DSE, which uses MemorySSA to identify
//          dead stores with greater precision than a conservative BFS:
//
//            - Stores to non-escaping allocas are never read or modified by
//              external calls; MemorySSA encodes this by not treating calls
//              as def/use events for non-escaping locations.
//            - Dead-store detection becomes O(uses) rather than O(blocks × stores).
//
// Key invariants:
//   - MemoryAccess IDs are dense, starting at 1 (0 = LiveOnEntry sentinel).
//   - MemoryPhi nodes are inserted at every block with multiple predecessors
//     that could observe different reaching defs.
//   - A store is dead iff its MemoryDef has no reachable MemoryUse consumers.
// Ownership/Lifetime: MemorySSA holds raw Block* pointers into the Function;
//          both must outlive the MemorySSA object.
// Links: il/analysis/BasicAA.hpp, il/analysis/Dominators.hpp,
//        il/analysis/CFG.hpp, il/transform/DSE.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace il::core
{
struct Function;
struct BasicBlock;
using Block = BasicBlock;
struct Instr;
} // namespace il::core

namespace viper::analysis
{
class BasicAA;
} // namespace viper::analysis

namespace viper::analysis
{

/// @brief Identifies the role of a memory access in the MemorySSA graph.
enum class MemAccessKind
{
    LiveOnEntry, ///< Synthetic root access representing pre-function memory state.
    Def,         ///< A store (or modifying call) that defines a new memory version.
    Use,         ///< A load (or reading call) that consumes a memory version.
    Phi,         ///< A join-point merge of multiple incoming memory versions.
};

/// @brief A single node in the MemorySSA def-use graph.
///
/// MemoryAccess nodes are owned and indexed inside @ref MemorySSA.  Consumers
/// hold IDs (uint32_t) and look up nodes through the owning analysis.
struct MemoryAccess
{
    MemAccessKind kind;
    uint32_t id;                    ///< Dense ID; 0 is reserved for LiveOnEntry.
    il::core::Block *block;         ///< Containing block; nullptr for LiveOnEntry.
    int instrIdx;                   ///< Index into block->instructions; -1 for Phi/LiveOnEntry.
    uint32_t definingAccess;        ///< ID of the reaching Def/Phi for this Use or Def.
    std::vector<uint32_t> incoming; ///< For Phi: one definingAccess per predecessor (by order).
    std::vector<uint32_t> users;    ///< IDs of accesses that read this Def or Phi.
};

/// @brief Result of the MemorySSA analysis for one function.
///
/// Provides def-use chain queries and a convenience dead-store predicate used
/// by the DSE pass.  The analysis is built once per function and cached by the
/// @ref AnalysisManager.
///
/// Usage:
/// @code
///   MemorySSA mssa = computeMemorySSA(module, fn, aa);
///   if (mssa.isDeadStore(&block, instrIdx)) { /* eliminate store */ }
/// @endcode
class MemorySSA
{
  public:
    MemorySSA() = default;
    MemorySSA(const MemorySSA &) = default;
    MemorySSA &operator=(const MemorySSA &) = default;
    MemorySSA(MemorySSA &&) = default;
    MemorySSA &operator=(MemorySSA &&) = default;

    /// @brief Return true if the store at @p block[instrIdx] is provably dead.
    ///
    /// A store is dead when no reachable load on any path from the store to
    /// a function exit reads from the stored address before another store
    /// overwrites it.  Calls to external functions do not count as reads for
    /// non-escaping allocas.
    [[nodiscard]] bool isDeadStore(const il::core::Block *block, size_t instrIdx) const;

    /// @brief Return the MemoryAccess assigned to a given instruction, if any.
    [[nodiscard]] const MemoryAccess *accessFor(const il::core::Block *block,
                                                size_t instrIdx) const;

    /// @brief Access the full node table (for diagnostics/testing).
    [[nodiscard]] const std::vector<MemoryAccess> &accesses() const
    {
        return accesses_;
    }

  private:
    friend MemorySSA computeMemorySSA(il::core::Function &F, viper::analysis::BasicAA &AA);

    /// All MemoryAccess nodes; index 0 = LiveOnEntry placeholder.
    std::vector<MemoryAccess> accesses_;

    /// (block, instrIdx) → MemoryAccess index in accesses_.
    std::unordered_map<const il::core::Block *, std::unordered_map<size_t, uint32_t>>
        instrToAccess_;

    /// Set of MemoryAccess IDs that represent dead stores.
    std::unordered_set<uint32_t> deadStoreIds_;
};

/// @brief Build the MemorySSA analysis for function @p F.
///
/// @details
/// Construction proceeds in three phases:
/// 1. **Identify non-escaping allocas**: only allocas whose address does not
///    flow to a call or get stored elsewhere are eligible for precise tracking.
///    Stores to escaping allocas conservatively use the global LiveOnEntry def.
/// 2. **RPO scan**: In reverse-post-order, assign MemoryDef to every store and
///    MemoryUse to every load, linking each Use to its reaching Def.  Calls are
///    modelled as global Defs/Uses unless the access target is a non-escaping
///    alloca, in which case the call is transparent.
/// 3. **Dead-store detection**: After all def-use links are built, any MemoryDef
///    with no transitive MemoryUse consumers is a dead store.
///
/// @param F Function to analyse.
/// @param AA Alias analysis for memory disambiguation.
/// @return MemorySSA result with dead-store predicates populated.
MemorySSA computeMemorySSA(il::core::Function &F, viper::analysis::BasicAA &AA);

} // namespace viper::analysis

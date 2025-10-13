// File: src/il/transform/SimplifyCFG.hpp
// Purpose: Control-flow graph simplification pass scaffold for IL functions.
// Key invariants: Mutates functions in place while preserving observable behavior.
// Ownership/Lifetime: Pass operates on caller-owned functions; no heap ownership.
// Links: docs/codemap.md
#pragma once

#include "il/core/Module.hpp"
#include "il/core/Function.hpp"
#include "il/transform/PassManager.hpp"

namespace il::transform
{

/// \brief Simplify IL control-flow graphs by folding and pruning trivial shapes.
///
/// \details The pass focuses on canonicalising branching and block structure so
/// subsequent optimisations can operate on a predictable CFG. The scaffold keeps
/// statistics about the transformations performed once they are implemented.
struct SimplifyCFG
{
    /// \brief Aggregated statistics from a pass invocation.
    struct Stats
    {
        size_t cbrToBr = 0;            ///< Number of conditional branches simplified.
        size_t emptyBlocksRemoved = 0; ///< Count of empty blocks eliminated.
        size_t predsMerged = 0;        ///< Predecessor edge merges performed.
        size_t paramsShrunk = 0;       ///< Block parameter reductions.
        size_t blocksMerged = 0;       ///< Adjacent block merges.
        size_t unreachableRemoved = 0; ///< Unreachable block removals.
    };

    /// \brief Create a CFG simplifier.
    /// \param aggressive Enable more aggressive canonicalisations when true.
    explicit SimplifyCFG(bool aggressive = true) : aggressive(aggressive) {}

    /// \brief Provide the module containing functions processed by this pass.
    /// \param module Pointer to the parent module; may be null when unavailable.
    void setModule(const il::core::Module *module) { module_ = module; }

    /// \brief Run the simplification pass on a single function.
    /// \param F Function mutated in place.
    /// \param outStats Optional pointer populated with pass statistics.
    /// \return True if the pass modified the function.
    bool run(il::core::Function &F, Stats *outStats = nullptr);

  private:
    bool aggressive; ///< Controls heuristic aggressiveness.
    const il::core::Module *module_ = nullptr; ///< Parent module used for verification.
};

} // namespace il::transform

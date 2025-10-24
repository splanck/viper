// File: src/il/transform/SimplifyCFG.hpp
// Purpose: Control-flow graph simplification pass scaffold for IL functions.
// Key invariants: Mutates functions in place while preserving observable behavior.
// Ownership/Lifetime: Pass operates on caller-owned functions; no heap ownership.
// Links: docs/codemap.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"

#include <string_view>

namespace il::transform
{

class AnalysisManager;

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
        size_t switchToBr = 0;         ///< Switches rewritten to unconditional branches.
    };

    /// \brief Per-run context shared across helper routines.
    struct SimplifyCFGPassContext
    {
        SimplifyCFGPassContext(il::core::Function &function,
                               const il::core::Module *module,
                               Stats &stats);

        il::core::Function &function;   ///< Function currently being simplified.
        const il::core::Module *module; ///< Parent module, may be null.
        Stats &stats;                   ///< Mutable statistics for the run.

        bool isDebugLoggingEnabled() const;
        void logDebug(std::string_view message) const;
        bool isEHSensitive(const il::core::BasicBlock &block) const;

      private:
        bool debugLoggingEnabled_ = false; ///< Cached debug logging flag.
    };

    /// \brief Create a CFG simplifier.
    /// \param aggressive Enable more aggressive canonicalisations when true.
    explicit SimplifyCFG(bool aggressive = true) : aggressive(aggressive) {}

    /// \brief Provide the module containing functions processed by this pass.
    /// \param module Pointer to the parent module; may be null when unavailable.
    void setModule(const il::core::Module *module)
    {
        module_ = module;
    }

    /// \brief Provide the active analysis manager so the pass can invalidate caches.
    /// \param manager Pointer to the analysis manager driving the pipeline; may be null.
    void setAnalysisManager(AnalysisManager *manager)
    {
        analysisManager_ = manager;
    }

    /// \brief Run the simplification pass on a single function.
    /// \param F Function mutated in place.
    /// \param outStats Optional pointer populated with pass statistics.
    /// \return True if the pass modified the function.
    bool run(il::core::Function &F, Stats *outStats = nullptr);

  private:
    bool aggressive;                             ///< Controls heuristic aggressiveness.
    const il::core::Module *module_ = nullptr;   ///< Parent module used for verification.
    AnalysisManager *analysisManager_ = nullptr; ///< Analysis manager invalidated on change.
};

} // namespace il::transform

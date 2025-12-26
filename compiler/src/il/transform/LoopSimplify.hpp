//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Loop Simplification canonicalization pass for IL functions.
// Loop simplification transforms loops into a canonical form that simplifies
// subsequent loop optimizations by ensuring loops have predictable structure.
//
// Many loop optimizations (like LICM, loop unrolling, vectorization) rely on loops
// having specific structural properties: a single entry edge (preheader), a single
// backedge (latch), and dedicated exit blocks. Natural loops in arbitrary control
// flow graphs may violate these properties. The LoopSimplify pass canonicalizes
// loops by inserting preheader and latch blocks where needed, enabling downstream
// optimizations to make simplifying assumptions about loop structure.
//
// Key Responsibilities:
// - Ensure each loop has a unique preheader block (single entry to loop header)
// - Ensure each loop has a dedicated latch block (single backedge to header)
// - Insert exit blocks for loops with multiple exit edges
// - Maintain SSA form through proper block parameter threading
// - Preserve program semantics (transformations are structural, not semantic)
//
// Design Notes:
// The pass performs purely structural transformations - it doesn't change what
// the program computes, only how the control flow is organized. All transformations
// maintain SSA form by properly updating block parameters and branch arguments.
// The implementation is conservative and only modifies loops that violate canonical
// form. It integrates with the pass manager to invalidate affected analyses.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/transform/PassRegistry.hpp"

namespace il::transform
{

class LoopSimplify : public FunctionPass
{
  public:
    /// \brief Identifier used when registering the pass.
    std::string_view id() const override;

    /// \brief Run the loop simplifier over @p function using @p analysis for queries.
    PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) override;
};

} // namespace il::transform

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/ISel.hpp
// Purpose: Declare the instruction selection helpers that canonicalise Machine IR.
// Key invariants: Transformations preserve instruction ordering while rewriting
//                 pseudo-ops into concrete x86-64 encodings; i1 values are
//                 materialised via SETcc + MOVZX idioms; compare+branch folds.
// Ownership/Lifetime: The selector mutates Machine IR in-place; no dynamic resources are
//                     allocated beyond temporary worklists.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MachineIR.hpp"
#include "TargetX64.hpp"

namespace viper::codegen::x64
{

/// \brief Canonicalises lowered Machine IR into concrete x86-64 forms.
/// \details The instruction selector fixes operand modes for integer and
///          floating point arithmetic, resolves compare+branch sequences, and
///          materialises i1 values using byte set + zero-extend idioms.
class ISel
{
  public:
    explicit ISel(const TargetInfo &target) noexcept;

    /// \brief Lower arithmetic instructions to concrete encodings.
    void lowerArithmetic(MFunction &func) const;

    /// \brief Lower compare operations and conditional branches to x86-64 forms.
    void lowerCompareAndBranch(MFunction &func) const;

    /// \brief Lower select-like idioms to canonical register sequences.
    void lowerSelect(MFunction &func) const;

  private:
    const TargetInfo *target_{nullptr};

    // Scans blocks and folds LEA bases into mem operands when the temp has a single use.
    void foldLeaIntoMem(MFunction &func) const;

    // Folds SHL+ADD into SIB addressing modes in memory operands.
    void foldSibAddressing(MFunction &func) const;
};

} // namespace viper::codegen::x64

// src/codegen/x86_64/ISel.hpp
// SPDX-License-Identifier: GPL-3.0-only
//
// Purpose: Declare the instruction selection helpers that canonicalise the
//          Machine IR produced by the IL lowering bridge into concrete x86-64
//          instructions for Phase A.
// Invariants: Transformations operate locally within a machine function,
//             preserving operand ordering while upgrading pseudo encodings to
//             legal instruction forms (e.g. immediate compares, zero-extended
//             i1 materialisation).
// Ownership: The selector mutates Machine IR in-place; no dynamic resources are
//            owned beyond references to the target description supplied at
//            construction time.
// Notes: Depends solely on MachineIR.hpp and TargetX64.hpp.

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
};

} // namespace viper::codegen::x64

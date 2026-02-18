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

    /// @brief Fold LEA base addresses into memory operands.
    /// @details Scans all blocks looking for LEA instructions whose result
    /// temporary has exactly one use as a memory operand base. When found,
    /// the LEA's offset is folded directly into the memory operand's
    /// displacement, eliminating the intermediate register.
    /// @param func The machine function to optimize.
    void foldLeaIntoMem(MFunction &func) const;

    /// @brief Fold shift-add patterns into SIB addressing modes.
    /// @details Identifies patterns where an index register is shifted left
    /// and added to a base register, converting them into x86's scaled-index-base
    /// addressing mode (e.g., [base + index*scale + disp]).
    /// @param func The machine function to optimize.
    void foldSibAddressing(MFunction &func) const;

    /// @brief Replace IMULrr-by-small-constant with LEA strength reduction.
    /// @details Detects patterns where a MOVri loads a constant 3, 5, or 9
    /// into a virtual register used exactly once as the second operand of an
    /// IMULrr. Replaces the multiply with a LEA using SIB addressing:
    ///   x*3 → lea [x + x*2], x*5 → lea [x + x*4], x*9 → lea [x + x*8].
    /// The supplying MOVri is erased when the constant register becomes dead.
    /// IMULOvfrr (overflow-checked) is intentionally left untouched.
    /// @param func The machine function to optimize.
    void lowerMulToLea(MFunction &func) const;
};

} // namespace viper::codegen::x64

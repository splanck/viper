//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/Reassociate.cpp
// Purpose: Implements algebraic reassociation of commutative+associative ops.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements operand canonicalization for commutative+associative ops.
/// @details For each eligible binary instruction, operands are sorted into a
///          canonical order: temporaries by descending ID, then constants. This
///          simple normalization enables EarlyCSE and GVN to recognize more
///          equivalent expressions without building full expression trees.

#include "il/transform/Reassociate.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Value.hpp"

namespace il::transform {

using namespace il::core;

namespace {

/// @brief Check if an opcode is commutative and associative for integers.
/// @details Only includes operations where reassociation is provably correct:
///          plain integer arithmetic (no overflow checks) and bitwise ops.
bool isReassociable(Opcode op) {
    switch (op) {
        case Opcode::Add:
        case Opcode::Mul:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
            return true;
        default:
            return false;
    }
}

/// @brief Compute a canonical rank for operand sorting.
/// @details Constants get the lowest rank (sorted last in descending order)
///          so that `a + 1` and `1 + a` both become `a + 1` (temp first).
///          Among temporaries, higher IDs rank higher for a stable sort.
int operandRank(const Value &v) {
    switch (v.kind) {
        case Value::Kind::Temp:
            return static_cast<int>(v.id) + 1000; // High rank → sorted first
        case Value::Kind::ConstInt:
            return 0; // Constants sort last
        case Value::Kind::ConstFloat:
            return 1;
        case Value::Kind::ConstStr:
        case Value::Kind::GlobalAddr:
            return 2;
        case Value::Kind::NullPtr:
            return -1;
    }
    return 0;
}

/// @brief Canonicalize operand order for a single instruction.
/// @return True if the operand order was changed.
bool canonicalizeOperands(Instr &I) {
    if (I.operands.size() != 2)
        return false;
    if (!isReassociable(I.op))
        return false;

    int rank0 = operandRank(I.operands[0]);
    int rank1 = operandRank(I.operands[1]);

    // Sort by descending rank: higher-ranked operand first.
    // This puts temporaries before constants.
    if (rank0 < rank1) {
        std::swap(I.operands[0], I.operands[1]);
        return true;
    }
    return false;
}

} // namespace

void reassociate(Module &M) {
    for (auto &F : M.functions) {
        for (auto &B : F.blocks) {
            for (auto &I : B.instructions) {
                canonicalizeOperands(I);
            }
        }
    }
}

} // namespace il::transform

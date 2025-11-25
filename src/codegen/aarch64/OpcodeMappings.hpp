//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/OpcodeMappings.hpp
// Purpose: Declarative mappings between IL opcodes and AArch64 MIR opcodes
// Key invariants: Tables are immutable and used for pattern-based lowering
// Ownership/Lifetime: Static data, no dynamic allocation
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/aarch64/MachineIR.hpp"
#include "il/core/Opcode.hpp"

namespace viper::codegen::aarch64
{

// Binary arithmetic operations mapping
struct BinaryOpMapping
{
    il::core::Opcode ilOp;
    MOpcode mirOp;
    bool supportsImmediate;
    MOpcode immOp;
};

// Comparison operations mapping
struct CompareMapping
{
    il::core::Opcode ilOp;
    const char *condition;
};

// Unary operations mapping
struct UnaryOpMapping
{
    il::core::Opcode ilOp;
    MOpcode mirOp;
};

// Binary arithmetic mappings for integer operations
constexpr BinaryOpMapping kBinaryIntOps[] = {
    {il::core::Opcode::Add, MOpcode::AddRRR, true, MOpcode::AddRI},
    {il::core::Opcode::IAddOvf, MOpcode::AddRRR, true, MOpcode::AddRI},
    {il::core::Opcode::Sub, MOpcode::SubRRR, true, MOpcode::SubRI},
    {il::core::Opcode::ISubOvf, MOpcode::SubRRR, true, MOpcode::SubRI},
    {il::core::Opcode::Mul, MOpcode::MulRRR, false, MOpcode::MulRRR},
    {il::core::Opcode::IMulOvf, MOpcode::MulRRR, false, MOpcode::MulRRR},
    {il::core::Opcode::And, MOpcode::AndRRR, false, MOpcode::AndRRR},
    {il::core::Opcode::Or, MOpcode::OrrRRR, false, MOpcode::OrrRRR},
    {il::core::Opcode::Xor, MOpcode::EorRRR, false, MOpcode::EorRRR},
    {il::core::Opcode::Shl, MOpcode::LslRI, true, MOpcode::LslRI},
    {il::core::Opcode::LShr, MOpcode::LsrRI, true, MOpcode::LsrRI},
    {il::core::Opcode::AShr, MOpcode::AsrRI, true, MOpcode::AsrRI},
};

// Binary arithmetic mappings for floating-point operations
constexpr BinaryOpMapping kBinaryFpOps[] = {
    {il::core::Opcode::FAdd, MOpcode::FAddRRR, false, MOpcode::FAddRRR},
    {il::core::Opcode::FSub, MOpcode::FSubRRR, false, MOpcode::FSubRRR},
    {il::core::Opcode::FMul, MOpcode::FMulRRR, false, MOpcode::FMulRRR},
    {il::core::Opcode::FDiv, MOpcode::FDivRRR, false, MOpcode::FDivRRR},
};

// Comparison mappings
constexpr CompareMapping kCompareOps[] = {
    {il::core::Opcode::ICmpEq, "eq"},
    {il::core::Opcode::ICmpNe, "ne"},
    {il::core::Opcode::SCmpLT, "lt"},
    {il::core::Opcode::SCmpLE, "le"},
    {il::core::Opcode::SCmpGT, "gt"},
    {il::core::Opcode::SCmpGE, "ge"},
    {il::core::Opcode::UCmpLT, "lo"},
    {il::core::Opcode::UCmpLE, "ls"},
    {il::core::Opcode::UCmpGT, "hi"},
    {il::core::Opcode::UCmpGE, "hs"},
};

// Helper to lookup binary operation mapping
inline const BinaryOpMapping *lookupBinaryOp(il::core::Opcode op)
{
    for (const auto &mapping : kBinaryIntOps)
    {
        if (mapping.ilOp == op)
        {
            return &mapping;
        }
    }
    for (const auto &mapping : kBinaryFpOps)
    {
        if (mapping.ilOp == op)
        {
            return &mapping;
        }
    }
    return nullptr;
}

// Helper to lookup comparison condition
inline const char *lookupCondition(il::core::Opcode op)
{
    for (const auto &mapping : kCompareOps)
    {
        if (mapping.ilOp == op)
        {
            return mapping.condition;
        }
    }
    return nullptr;
}

// Check if opcode is a comparison
inline bool isCompareOp(il::core::Opcode op)
{
    return lookupCondition(op) != nullptr;
}

// Check if opcode is a floating-point operation
inline bool isFloatingPointOp(il::core::Opcode op)
{
    switch (op)
    {
        case il::core::Opcode::FAdd:
        case il::core::Opcode::FSub:
        case il::core::Opcode::FMul:
        case il::core::Opcode::FDiv:
            return true;
        default:
            return false;
    }
}

} // namespace viper::codegen::aarch64
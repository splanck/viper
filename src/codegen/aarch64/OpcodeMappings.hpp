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

/// @brief AArch64-specific opcode mapping tables and lookup functions.
///
/// This namespace contains the declarative mappings between IL (intermediate
/// language) opcodes and AArch64 MIR (machine IR) opcodes. These mappings drive
/// the instruction selection phase of code generation, allowing the lowering
/// pass to translate IL operations into equivalent AArch64 machine instructions.
///
/// The mappings are organized by operation category:
/// - Binary integer operations (add, sub, mul, bitwise)
/// - Binary floating-point operations (fadd, fsub, fmul, fdiv)
/// - Comparison operations (icmp_eq, scmp_lt, etc.)
///
/// @see MachineIR.hpp for the MOpcode definitions
/// @see il/core/Opcode.hpp for IL opcode definitions
namespace viper::codegen::aarch64
{

/// @brief Mapping entry for binary arithmetic IL operations to AArch64 instructions.
///
/// Each entry specifies how an IL binary operation should be lowered to AArch64
/// machine instructions. Some operations support immediate operands (e.g., ADD
/// can use ADD immediate form when one operand is a small constant), which
/// enables more efficient code generation.
///
/// @par Immediate Support:
/// When `supportsImmediate` is true and the right operand is a constant that
/// fits in the instruction's immediate field, the code generator can emit the
/// `immOp` form instead of loading the constant into a register first.
///
/// @par Example:
/// For IL `%r = add %a, 5`:
/// - If supportsImmediate=true, emit: `ADD Xr, Xa, #5`
/// - If supportsImmediate=false, emit: `MOV Xtmp, #5; ADD Xr, Xa, Xtmp`
struct BinaryOpMapping
{
    il::core::Opcode ilOp;  ///< The IL opcode this mapping applies to.
    MOpcode mirOp;          ///< The register-register-register MIR opcode (e.g., ADD Xd, Xn, Xm).
    bool supportsImmediate; ///< True if this operation has an immediate variant.
    MOpcode immOp;          ///< The register-immediate MIR opcode (e.g., ADD Xd, Xn, #imm).
};

/// @brief Mapping entry for IL comparison operations to AArch64 condition codes.
///
/// AArch64 comparisons work in two steps: a CMP instruction sets the NZCV flags,
/// then a conditional instruction (CSEL, B.cond, etc.) uses a condition code to
/// check those flags. This mapping specifies which condition code corresponds
/// to each IL comparison opcode.
///
/// @par Condition Codes:
/// - "eq" / "ne" - Equal / Not equal (Z flag)
/// - "lt" / "le" / "gt" / "ge" - Signed comparisons
/// - "lo" / "ls" / "hi" / "hs" - Unsigned comparisons (lower/higher)
///
/// @par Example:
/// For IL `%r = scmp_lt %a, %b` (signed less than):
/// - Emit: `CMP Xa, Xb; CSET Xr, lt`
struct CompareMapping
{
    il::core::Opcode ilOp; ///< The IL comparison opcode.
    const char *condition; ///< The AArch64 condition code string (e.g., "eq", "lt").
};

/// @brief Mapping entry for unary IL operations to AArch64 instructions.
///
/// Maps single-operand IL operations (like negation, bitwise NOT) to their
/// corresponding AArch64 machine instructions.
struct UnaryOpMapping
{
    il::core::Opcode ilOp; ///< The IL unary opcode.
    MOpcode mirOp;         ///< The MIR opcode for this unary operation.
};

/// @brief Mapping table for integer binary arithmetic operations.
///
/// Contains all supported integer operations: addition, subtraction,
/// multiplication, bitwise AND/OR/XOR, and shift operations. Operations
/// with overflow checking (IAddOvf, ISubOvf, IMulOvf) map to the same
/// machine instructions as their non-checking counterparts; overflow
/// detection is handled separately.
///
/// @note This table is provided for documentation and iteration. For actual
///       lookups, use lookupBinaryOp() which provides O(1) access via switch.
constexpr BinaryOpMapping kBinaryIntOps[] = {
    {il::core::Opcode::Add, MOpcode::AddRRR, true, MOpcode::AddRI},
    {il::core::Opcode::IAddOvf, MOpcode::AddRRR, true, MOpcode::AddRI},
    {il::core::Opcode::Sub, MOpcode::SubRRR, true, MOpcode::SubRI},
    {il::core::Opcode::ISubOvf, MOpcode::SubRRR, true, MOpcode::SubRI},
    {il::core::Opcode::Mul, MOpcode::MulRRR, false, MOpcode::MulRRR},
    {il::core::Opcode::IMulOvf, MOpcode::MulRRR, false, MOpcode::MulRRR},
    {il::core::Opcode::And, MOpcode::AndRRR, true, MOpcode::AndRI},
    {il::core::Opcode::Or, MOpcode::OrrRRR, true, MOpcode::OrrRI},
    {il::core::Opcode::Xor, MOpcode::EorRRR, true, MOpcode::EorRI},
    {il::core::Opcode::Shl, MOpcode::LslRI, true, MOpcode::LslRI},
    {il::core::Opcode::LShr, MOpcode::LsrRI, true, MOpcode::LsrRI},
    {il::core::Opcode::AShr, MOpcode::AsrRI, true, MOpcode::AsrRI},
};

/// @brief Mapping table for floating-point binary operations.
///
/// Contains mappings for double-precision floating-point arithmetic.
/// Unlike integer operations, FP operations on AArch64 do not have
/// immediate variants - all operands must be in registers.
constexpr BinaryOpMapping kBinaryFpOps[] = {
    {il::core::Opcode::FAdd, MOpcode::FAddRRR, false, MOpcode::FAddRRR},
    {il::core::Opcode::FSub, MOpcode::FSubRRR, false, MOpcode::FSubRRR},
    {il::core::Opcode::FMul, MOpcode::FMulRRR, false, MOpcode::FMulRRR},
    {il::core::Opcode::FDiv, MOpcode::FDivRRR, false, MOpcode::FDivRRR},
};

/// @brief Mapping table for comparison operations to AArch64 condition codes.
///
/// Maps IL comparison opcodes to the AArch64 condition code suffix used
/// with conditional instructions (CSEL, CSET, B.cond). Includes both
/// signed comparisons (lt, le, gt, ge) and unsigned (lo, ls, hi, hs).
///
/// @note "lo"/"ls"/"hi"/"hs" are aliases for "cc"/"ls"/"hi"/"cs" but are
///       preferred for unsigned comparisons as they're more readable.
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

/// @brief Looks up the binary operation mapping for an IL opcode in O(1) time.
///
/// Uses a switch statement for constant-time lookup of binary operation
/// mappings. Returns a pointer to a static BinaryOpMapping struct, or nullptr
/// if the opcode is not a supported binary operation.
///
/// @par Supported Operations:
/// - Integer: Add, Sub, Mul (and overflow variants), And, Or, Xor, Shl, LShr, AShr
/// - Floating-point: FAdd, FSub, FMul, FDiv
///
/// @param op The IL opcode to look up.
/// @return Pointer to the mapping struct, or nullptr if not a binary op.
///
/// @note This function returns pointers to static storage, so the returned
///       pointer remains valid for the lifetime of the program.
inline const BinaryOpMapping *lookupBinaryOp(il::core::Opcode op)
{
    using Opc = il::core::Opcode;
    switch (op)
    {
        // Integer operations
        case Opc::Add:
        case Opc::IAddOvf:
        {
            static constexpr BinaryOpMapping m{Opc::Add, MOpcode::AddRRR, true, MOpcode::AddRI};
            return &m;
        }
        case Opc::Sub:
        case Opc::ISubOvf:
        {
            static constexpr BinaryOpMapping m{Opc::Sub, MOpcode::SubRRR, true, MOpcode::SubRI};
            return &m;
        }
        case Opc::Mul:
        case Opc::IMulOvf:
        {
            static constexpr BinaryOpMapping m{Opc::Mul, MOpcode::MulRRR, false, MOpcode::MulRRR};
            return &m;
        }
        case Opc::And:
        {
            static constexpr BinaryOpMapping m{Opc::And, MOpcode::AndRRR, true, MOpcode::AndRI};
            return &m;
        }
        case Opc::Or:
        {
            static constexpr BinaryOpMapping m{Opc::Or, MOpcode::OrrRRR, true, MOpcode::OrrRI};
            return &m;
        }
        case Opc::Xor:
        {
            static constexpr BinaryOpMapping m{Opc::Xor, MOpcode::EorRRR, true, MOpcode::EorRI};
            return &m;
        }
        case Opc::Shl:
        {
            static constexpr BinaryOpMapping m{Opc::Shl, MOpcode::LslvRRR, true, MOpcode::LslRI};
            return &m;
        }
        case Opc::LShr:
        {
            static constexpr BinaryOpMapping m{Opc::LShr, MOpcode::LsrvRRR, true, MOpcode::LsrRI};
            return &m;
        }
        case Opc::AShr:
        {
            static constexpr BinaryOpMapping m{Opc::AShr, MOpcode::AsrvRRR, true, MOpcode::AsrRI};
            return &m;
        }
        // Floating-point operations
        case Opc::FAdd:
        {
            static constexpr BinaryOpMapping m{
                Opc::FAdd, MOpcode::FAddRRR, false, MOpcode::FAddRRR};
            return &m;
        }
        case Opc::FSub:
        {
            static constexpr BinaryOpMapping m{
                Opc::FSub, MOpcode::FSubRRR, false, MOpcode::FSubRRR};
            return &m;
        }
        case Opc::FMul:
        {
            static constexpr BinaryOpMapping m{
                Opc::FMul, MOpcode::FMulRRR, false, MOpcode::FMulRRR};
            return &m;
        }
        case Opc::FDiv:
        {
            static constexpr BinaryOpMapping m{
                Opc::FDiv, MOpcode::FDivRRR, false, MOpcode::FDivRRR};
            return &m;
        }
        default:
            return nullptr;
    }
}

/// @brief Looks up the AArch64 condition code for an IL comparison opcode.
///
/// Returns the condition code string (e.g., "eq", "lt", "hi") that should be
/// used with conditional instructions after a CMP. Uses a switch statement
/// for O(1) lookup.
///
/// @par Condition Codes:
/// | IL Opcode | AArch64 | Meaning                    |
/// |-----------|---------|----------------------------|
/// | ICmpEq    | "eq"    | Equal (Z=1)               |
/// | ICmpNe    | "ne"    | Not equal (Z=0)           |
/// | SCmpLT    | "lt"    | Signed less than          |
/// | SCmpLE    | "le"    | Signed less or equal      |
/// | SCmpGT    | "gt"    | Signed greater than       |
/// | SCmpGE    | "ge"    | Signed greater or equal   |
/// | UCmpLT    | "lo"    | Unsigned lower (carry=0)  |
/// | UCmpLE    | "ls"    | Unsigned lower or same    |
/// | UCmpGT    | "hi"    | Unsigned higher           |
/// | UCmpGE    | "hs"    | Unsigned higher or same   |
///
/// @param op The IL comparison opcode.
/// @return The condition code string, or nullptr if not a comparison opcode.
inline const char *lookupCondition(il::core::Opcode op)
{
    using Opc = il::core::Opcode;
    switch (op)
    {
        case Opc::ICmpEq:
            return "eq";
        case Opc::ICmpNe:
            return "ne";
        case Opc::SCmpLT:
            return "lt";
        case Opc::SCmpLE:
            return "le";
        case Opc::SCmpGT:
            return "gt";
        case Opc::SCmpGE:
            return "ge";
        case Opc::UCmpLT:
            return "lo";
        case Opc::UCmpLE:
            return "ls";
        case Opc::UCmpGT:
            return "hi";
        case Opc::UCmpGE:
            return "hs";
        default:
            return nullptr;
    }
}

/// @brief Tests whether an IL opcode is a comparison operation.
///
/// Returns true for all integer comparison opcodes (ICmpEq, ICmpNe, SCmpLT,
/// SCmpLE, SCmpGT, SCmpGE, UCmpLT, UCmpLE, UCmpGT, UCmpGE). These opcodes
/// require special handling in code generation: they lower to CMP + CSET
/// sequences rather than simple arithmetic instructions.
///
/// @param op The IL opcode to test.
/// @return True if op is a comparison opcode, false otherwise.
inline bool isCompareOp(il::core::Opcode op)
{
    return lookupCondition(op) != nullptr;
}

/// @brief Tests whether an IL opcode is a floating-point arithmetic operation.
///
/// Returns true for FAdd, FSub, FMul, FDiv. These opcodes require different
/// register classes (D0-D31 vector registers vs X0-X30 integer registers) and
/// different instruction encodings than integer operations.
///
/// @param op The IL opcode to test.
/// @return True if op is a floating-point arithmetic opcode, false otherwise.
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
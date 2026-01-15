//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/x86_64/LoweringRuleTable.hpp
// Purpose: Describe declarative lowering rules for x86-64 emission.
// Key invariants: Rule table entries are immutable and indexed by opcode prefix;
//                 operand patterns must align with IL operand encodings; emit
//                 callbacks may only append to the MIRBuilder, never remove.
// Ownership/Lifetime: Shared across lowering translation units via inline constexpr data.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

/// @brief x86-64 code generation components for the Viper compiler.
///
/// This namespace contains all x86-64 specific code generation infrastructure,
/// including instruction lowering, register allocation, and assembly emission.
/// The code generator follows the System V AMD64 ABI for Unix-like systems.
namespace viper::codegen::x64
{

struct ILInstr;
class MIRBuilder;

/// @brief Instruction lowering functions and rule tables for x86-64 code generation.
///
/// This namespace contains the table-driven instruction selection mechanism that
/// transforms IL (Intermediate Language) instructions into x86-64 MIR (Machine IR).
/// The lowering process uses a declarative rule table where each entry specifies:
/// - The IL opcode pattern to match (exact or prefix-based)
/// - Required operand shapes (arity and kind constraints)
/// - The emit callback function that generates the x86-64 MIR
///
/// ## Lowering Pipeline Overview
///
/// ```
/// IL Instruction -> lookupRuleSpec() -> RuleSpec -> emit callback -> MIR Instructions
/// ```
///
/// The lowering pass iterates over IL instructions, looks up matching rules, and
/// invokes the corresponding emit callback to append MIR instructions to the builder.
///
/// ## Emit Callback Contract
///
/// All emit functions must:
/// 1. Read the IL instruction operands without modification
/// 2. Append zero or more MIR instructions to the builder
/// 3. Never remove existing MIR instructions from the builder
/// 4. Handle all valid operand combinations for the matched opcode
///
/// @see kLoweringRuleTable for the complete set of lowering rules
/// @see MIRBuilder for the MIR construction interface
namespace lowering
{

/// @brief Emits x86-64 MIR for IL `add` instruction (integer addition).
///
/// Generates an ADD instruction for two integer operands. The x86-64 ADD instruction
/// modifies flags, so subsequent flag-dependent operations may use the result without
/// an explicit comparison. Handles both 32-bit and 64-bit integer operands based on
/// the IL instruction's type.
///
/// @param instr The IL add instruction with two value operands.
/// @param builder The MIR builder to append instructions to.
void emitAdd(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `sub` instruction (integer subtraction).
///
/// Generates a SUB instruction. Like ADD, this sets CPU flags that can be used
/// by subsequent conditional operations. The destination is the first operand
/// minus the second operand.
///
/// @param instr The IL sub instruction with two value operands.
/// @param builder The MIR builder to append instructions to.
void emitSub(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `mul` instruction (integer multiplication).
///
/// Generates an IMUL instruction for signed multiplication. The x86-64 IMUL
/// has three forms: one-operand (implicitly uses RAX), two-operand, and
/// three-operand with immediate. This function typically uses the two-operand
/// form for register-register multiplication.
///
/// @param instr The IL mul instruction with two value operands.
/// @param builder The MIR builder to append instructions to.
void emitMul(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `fdiv` instruction (floating-point division).
///
/// Generates a DIVSD (divide scalar double) instruction for 64-bit floating-point
/// division, or DIVSS for 32-bit. Uses SSE/AVX registers (XMM0-XMM15).
///
/// @param instr The IL fdiv instruction with two floating-point operands.
/// @param builder The MIR builder to append instructions to.
void emitFDiv(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `and` instruction (bitwise AND).
///
/// Generates an AND instruction that computes the bitwise AND of two operands.
/// Sets the ZF flag if the result is zero, which can be used for conditional branching.
///
/// @param instr The IL and instruction with two value operands.
/// @param builder The MIR builder to append instructions to.
void emitAnd(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `or` instruction (bitwise OR).
///
/// Generates an OR instruction that computes the bitwise inclusive OR of two operands.
///
/// @param instr The IL or instruction with two value operands.
/// @param builder The MIR builder to append instructions to.
void emitOr(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `xor` instruction (bitwise exclusive OR).
///
/// Generates a XOR instruction. A common idiom is XOR reg, reg to zero a register
/// (shorter encoding than MOV reg, 0), but this function handles the general case.
///
/// @param instr The IL xor instruction with two value operands.
/// @param builder The MIR builder to append instructions to.
void emitXor(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `icmp_*` family (integer comparisons).
///
/// Handles all integer comparison variants: icmp_eq, icmp_ne, scmp_lt, scmp_le,
/// scmp_gt, scmp_ge (signed), ucmp_lt, ucmp_le, ucmp_gt, ucmp_ge (unsigned).
/// Generates a CMP instruction followed by a SETcc to materialize the boolean result.
///
/// @param instr The IL comparison instruction with opcode prefix "icmp_" or "scmp_"/"ucmp_".
/// @param builder The MIR builder to append instructions to.
void emitICmp(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `fcmp_*` family (floating-point comparisons).
///
/// Handles floating-point comparisons using UCOMISD/UCOMISS instructions. These
/// set the EFLAGS differently than integer comparisons (unordered results set PF).
/// The comparison predicate is encoded in the opcode suffix.
///
/// @param instr The IL fcmp instruction with opcode prefix "fcmp_".
/// @param builder The MIR builder to append instructions to.
void emitFCmp(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL division family (div, sdiv, udiv, srem, urem, rem).
///
/// Handles all division and remainder operations. The x86-64 DIV/IDIV instructions
/// are complex: they implicitly use RDX:RAX as the dividend and produce both quotient
/// (in RAX) and remainder (in RDX). This function generates the setup (sign extension
/// via CDQ/CQO for signed, zero extension for unsigned) and extracts the correct result.
///
/// @param instr The IL division instruction (div, sdiv, udiv, rem, srem, urem).
/// @param builder The MIR builder to append instructions to.
void emitDivFamily(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `shl` instruction (shift left).
///
/// Generates a SHL instruction. The shift amount can be an immediate (0-63) or
/// in the CL register. This function may need to move the shift amount to CL
/// if it's in another register.
///
/// @param instr The IL shl instruction with value and shift amount operands.
/// @param builder The MIR builder to append instructions to.
void emitShiftLeft(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `lshr` instruction (logical shift right).
///
/// Generates a SHR instruction for unsigned (logical) right shift. Zeros are
/// shifted in from the left, regardless of the sign bit.
///
/// @param instr The IL lshr instruction with value and shift amount operands.
/// @param builder The MIR builder to append instructions to.
void emitShiftLshr(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `ashr` instruction (arithmetic shift right).
///
/// Generates a SAR instruction for signed (arithmetic) right shift. The sign bit
/// is replicated into the vacated high-order bits, preserving the sign of the value.
///
/// @param instr The IL ashr instruction with value and shift amount operands.
/// @param builder The MIR builder to append instructions to.
void emitShiftAshr(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for explicit CMP instruction.
///
/// Generates a CMP instruction without materializing a boolean result. Used when
/// the comparison result is consumed directly by a conditional branch rather than
/// stored in a register.
///
/// @param instr The IL cmp instruction with two value operands.
/// @param builder The MIR builder to append instructions to.
void emitCmpExplicit(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `select` instruction (conditional select).
///
/// Generates a CMOV (conditional move) instruction that selects between two values
/// based on a condition. Equivalent to the C ternary operator: `cond ? true_val : false_val`.
///
/// @param instr The IL select instruction with condition and two value operands.
/// @param builder The MIR builder to append instructions to.
void emitSelect(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `br` instruction (unconditional branch).
///
/// Generates a JMP instruction to the target basic block label.
///
/// @param instr The IL br instruction with a label operand.
/// @param builder The MIR builder to append instructions to.
void emitBranch(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `cbr` instruction (conditional branch).
///
/// Generates a TEST and Jcc (conditional jump) sequence. The condition value is
/// tested against zero, and control transfers to either the true or false target
/// based on the result.
///
/// @param instr The IL cbr instruction with condition, true label, and false label.
/// @param builder The MIR builder to append instructions to.
void emitCondBranch(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `ret` instruction (function return).
///
/// Generates the function epilogue and RET instruction. If the function returns a
/// value, it must be in RAX (integer) or XMM0 (floating-point) per the SysV ABI.
///
/// @param instr The IL ret instruction with optional return value operand.
/// @param builder The MIR builder to append instructions to.
void emitReturn(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `call` instruction (direct function call).
///
/// Generates a CALL instruction to a named function. Arguments are passed per the
/// SysV AMD64 ABI: first six integer/pointer args in RDI, RSI, RDX, RCX, R8, R9;
/// first eight floating-point args in XMM0-XMM7; remaining args on the stack.
///
/// @param instr The IL call instruction with function label and argument operands.
/// @param builder The MIR builder to append instructions to.
void emitCall(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `call.indirect` instruction (indirect function call).
///
/// Generates a CALL instruction through a function pointer. The first operand is
/// the address to call (in a register), followed by the arguments.
///
/// @param instr The IL call.indirect instruction with address and argument operands.
/// @param builder The MIR builder to append instructions to.
void emitCallIndirect(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `load` instruction (memory load).
///
/// Generates a MOV instruction to load a value from memory. The address operand
/// can be a register (base pointer) with an optional immediate offset. Supports
/// 8, 16, 32, and 64-bit loads with appropriate zero or sign extension.
///
/// @param instr The IL load instruction with address and optional offset operands.
/// @param builder The MIR builder to append instructions to.
void emitLoadAuto(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `store` instruction (memory store).
///
/// Generates a MOV instruction to store a value to memory. The operands are the
/// destination address, the value to store, and an optional immediate offset.
///
/// @param instr The IL store instruction with address, value, and optional offset.
/// @param builder The MIR builder to append instructions to.
void emitStore(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL type conversion instructions (zext, sext, trunc).
///
/// Handles zero extension (MOVZX), sign extension (MOVSX/MOVSXD), and truncation.
/// For truncation, this may be a no-op at the register level since x86-64 registers
/// can be accessed at different widths (AL, AX, EAX, RAX).
///
/// @param instr The IL conversion instruction (zext, sext, or trunc).
/// @param builder The MIR builder to append instructions to.
void emitZSTrunc(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `sitofp` instruction (signed int to floating-point).
///
/// Generates a CVTSI2SD (convert signed integer to scalar double) or CVTSI2SS
/// instruction to convert a signed integer to floating-point representation.
///
/// @param instr The IL sitofp instruction with integer operand.
/// @param builder The MIR builder to append instructions to.
void emitSIToFP(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `fptosi` instruction (floating-point to signed int).
///
/// Generates a CVTTSD2SI (convert with truncation scalar double to signed integer)
/// instruction. Uses truncation toward zero (not rounding to nearest).
///
/// @param instr The IL fptosi instruction with floating-point operand.
/// @param builder The MIR builder to append instructions to.
void emitFPToSI(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `eh.push` instruction (push exception handler).
///
/// Pushes an exception handler onto the runtime exception handler stack. The operand
/// is the label of the handler block to jump to when an exception is raised.
///
/// @param instr The IL eh.push instruction with handler label operand.
/// @param builder The MIR builder to append instructions to.
void emitEhPush(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `eh.pop` instruction (pop exception handler).
///
/// Pops the topmost exception handler from the runtime exception handler stack.
/// Called when leaving a protected region normally (without an exception).
///
/// @param instr The IL eh.pop instruction (no operands).
/// @param builder The MIR builder to append instructions to.
void emitEhPop(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `eh.entry` instruction (exception handler entry).
///
/// Marks the entry point of an exception handler block. Generates code to retrieve
/// the current exception object from the runtime's exception state.
///
/// @param instr The IL eh.entry instruction (no operands).
/// @param builder The MIR builder to append instructions to.
void emitEhEntry(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `trap` instruction (raise runtime error).
///
/// Generates code to raise a runtime trap/exception. May call a runtime function
/// to report the error and abort execution, or trigger a CPU trap instruction
/// depending on the trap kind specified.
///
/// @param instr The IL trap instruction with optional trap code operand.
/// @param builder The MIR builder to append instructions to.
void emitTrap(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `const_str` instruction (string constant).
///
/// Generates code to load the address of a string constant. The string data is
/// emitted to a read-only data section, and this instruction produces a pointer
/// to that data.
///
/// @param instr The IL const_str instruction with string index operand.
/// @param builder The MIR builder to append instructions to.
void emitConstStr(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `alloca` instruction (stack allocation).
///
/// Reserves space on the stack for local variables. Unlike C's alloca, this
/// typically happens at function entry with a fixed size, adjusting RSP once
/// rather than dynamically.
///
/// @param instr The IL alloca instruction with size immediate operand.
/// @param builder The MIR builder to append instructions to.
void emitAlloca(const ILInstr &instr, MIRBuilder &builder);

/// @brief Emits x86-64 MIR for IL `gep` instruction (get element pointer).
///
/// Generates address arithmetic to compute a pointer to an element within an
/// aggregate type (array or struct). Computes base + index * element_size.
///
/// @param instr The IL gep instruction with base pointer and index operands.
/// @param builder The MIR builder to append instructions to.
void emitGEP(const ILInstr &instr, MIRBuilder &builder);

/// @brief Bitflags that modify how a lowering rule matches IL instructions.
///
/// Rule flags customize the matching behavior for instruction selection. Currently
/// the only flag is `Prefix`, which enables prefix-based opcode matching for opcodes
/// that share a common handler (e.g., all `icmp_*` variants use one emit function).
///
/// ## Flag Combinations
///
/// Flags can be combined using the bitwise OR operator:
/// ```cpp
/// RuleFlags combined = RuleFlags::Prefix | RuleFlags::SomeOtherFlag;
/// ```
///
/// @see RuleSpec::flags for usage in rule definitions
enum class RuleFlags : std::uint8_t
{
    /// @brief No special matching behavior; opcode must match exactly.
    None = 0,

    /// @brief The rule's opcode string is a prefix, not an exact match.
    ///
    /// When this flag is set, a rule with opcode "icmp_" will match instructions
    /// with opcodes like "icmp_eq", "icmp_ne", "icmp_lt", etc. This allows a
    /// single rule to handle a family of related opcodes that share the same
    /// emit logic, with the emit function examining the full opcode to determine
    /// the specific variant.
    Prefix = 1U << 0,
};

/// @brief Combines two RuleFlags values using bitwise OR.
/// @param lhs The left-hand flag value.
/// @param rhs The right-hand flag value.
/// @return The combined flags.
constexpr RuleFlags operator|(RuleFlags lhs, RuleFlags rhs) noexcept
{
    return static_cast<RuleFlags>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

/// @brief Computes the intersection of two RuleFlags values using bitwise AND.
/// @param lhs The left-hand flag value.
/// @param rhs The right-hand flag value.
/// @return The flags present in both operands.
constexpr RuleFlags operator&(RuleFlags lhs, RuleFlags rhs) noexcept
{
    return static_cast<RuleFlags>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

/// @brief Tests whether a specific flag is set in a flags value.
/// @param flags The flags value to test.
/// @param flag The specific flag to check for.
/// @return True if the flag is set, false otherwise.
constexpr bool hasFlag(RuleFlags flags, RuleFlags flag) noexcept
{
    return (flags & flag) != RuleFlags::None;
}

/// @brief Specifies what kind of operand is expected at a given position.
///
/// When matching IL instructions to lowering rules, each operand position can have
/// a constraint on what kind of operand is allowed. This helps ensure rules are
/// only applied to instructions with compatible operand types.
///
/// ## Operand Kind Hierarchy
///
/// The IL has three fundamental operand kinds:
/// - **Value**: A virtual register reference (e.g., `%0`, `%result`)
/// - **Label**: A basic block label (e.g., `@entry`, `@loop_header`)
/// - **Immediate**: A literal constant (e.g., `42`, `3.14`)
///
/// The `Any` pattern matches all three kinds, while the specific patterns
/// require an exact match.
///
/// @see OperandShape for combining patterns into instruction-level constraints
enum class OperandKindPattern : std::uint8_t
{
    /// @brief Matches any operand kind (value, label, or immediate).
    /// Use this for operands where the emit function handles all cases.
    Any,

    /// @brief Matches only value operands (virtual register references).
    /// Used for operands that must be in registers for the target instruction.
    Value,

    /// @brief Matches only label operands (basic block references).
    /// Used for branch targets and call destinations (for direct calls).
    Label,

    /// @brief Matches only immediate operands (literal constants).
    /// Used for constants that can be encoded directly in the instruction.
    Immediate,
};

/// @brief Describes the expected shape of an IL instruction's operand list.
///
/// An operand shape specifies constraints on both the number of operands (arity)
/// and the kind of each operand (value, label, immediate). The lowering rule
/// matcher uses this information to filter candidate rules before invoking the
/// emit callback.
///
/// ## Arity Constraints
///
/// The `minArity` and `maxArity` fields define the acceptable range of operand
/// counts. For example:
/// - `{1, 1}`: Exactly one operand (unary operation)
/// - `{2, 2}`: Exactly two operands (binary operation)
/// - `{0, 1}`: Zero or one operand (optional result like `ret`)
/// - `{1, 255}`: One or more operands (variadic like `call`)
///
/// ## Kind Patterns
///
/// The `kinds` array specifies the expected kind for up to 4 operands. The
/// `kindCount` field indicates how many entries in `kinds` are meaningful.
/// Operands beyond `kindCount` are not checked (implicitly `Any`).
///
/// @par Example: Binary Value Operation
/// ```cpp
/// OperandShape{2U, 2U, 2U,
///     {OperandKindPattern::Value, OperandKindPattern::Value,
///      OperandKindPattern::Any, OperandKindPattern::Any}}
/// ```
/// This matches instructions with exactly 2 operands, both of which must be values.
///
/// @see RuleSpec for the complete rule definition structure
struct OperandShape
{
    /// @brief Minimum number of operands required for a match.
    std::uint8_t minArity{0};

    /// @brief Maximum number of operands allowed for a match.
    /// Use `std::numeric_limits<std::uint8_t>::max()` for variadic operations.
    std::uint8_t maxArity{std::numeric_limits<std::uint8_t>::max()};

    /// @brief Number of entries in `kinds` that should be checked.
    /// Set to 0 if operand kinds don't matter, only arity.
    std::uint8_t kindCount{0};

    /// @brief Expected operand kind for positions 0-3.
    /// Only the first `kindCount` entries are checked during matching.
    std::array<OperandKindPattern, 4> kinds{OperandKindPattern::Any,
                                            OperandKindPattern::Any,
                                            OperandKindPattern::Any,
                                            OperandKindPattern::Any};
};

/// @brief Complete specification of a lowering rule for instruction selection.
///
/// A RuleSpec binds together all the information needed to match an IL instruction
/// and emit the corresponding x86-64 MIR. The lowering pass iterates through the
/// rule table, finds matching rules, and invokes their emit callbacks.
///
/// ## Matching Process
///
/// A rule matches an IL instruction if:
/// 1. The opcode matches (exact or prefix, depending on flags)
/// 2. The operand count is within [minArity, maxArity]
/// 3. Each operand's kind matches the corresponding pattern (if kindCount > 0)
///
/// ## Example Rule
///
/// ```cpp
/// RuleSpec{"add",
///          OperandShape{2U, 2U, 2U,
///              {OperandKindPattern::Value, OperandKindPattern::Value,
///               OperandKindPattern::Any, OperandKindPattern::Any}},
///          RuleFlags::None,
///          &emitAdd,
///          "add"}
/// ```
///
/// This rule:
/// - Matches IL opcode "add" exactly (no Prefix flag)
/// - Requires exactly 2 operands (minArity=2, maxArity=2)
/// - Both operands must be values (register references)
/// - Invokes `emitAdd()` to generate the MIR
///
/// @see kLoweringRuleTable for the complete set of rules
/// @see matchesRuleSpec() for the matching implementation
struct RuleSpec
{
    /// @brief The IL opcode string to match.
    /// If RuleFlags::Prefix is set, this is a prefix (e.g., "icmp_" matches "icmp_eq").
    /// Otherwise, this must match the instruction's opcode exactly.
    std::string_view opcode{};

    /// @brief Constraints on the instruction's operand list (arity and kinds).
    OperandShape operands{};

    /// @brief Flags that modify matching behavior (e.g., prefix matching).
    RuleFlags flags{RuleFlags::None};

    /// @brief The emit callback that generates MIR for matched instructions.
    /// This function reads the IL instruction and appends MIR to the builder.
    /// Must never be nullptr for valid rules.
    void (*emit)(const ILInstr &, MIRBuilder &) = nullptr;

    /// @brief Human-readable name for diagnostics and debugging.
    /// Typically the same as `opcode` but without the trailing underscore for prefix rules.
    const char *name{nullptr};
};

/// @brief Master table of all x86-64 instruction lowering rules.
///
/// This compile-time constant array contains the complete set of rules for
/// transforming IL instructions into x86-64 MIR. The lowering pass searches
/// this table linearly (via lookupRuleSpec) to find matching rules for each
/// IL instruction.
///
/// ## Table Organization
///
/// Rules are grouped by category for clarity:
/// 1. **Arithmetic** (add, sub, mul, fdiv): Basic integer and FP operations
/// 2. **Bitwise** (and, or, xor): Logical operations
/// 3. **Comparison** (icmp_*, fcmp_*, cmp): All comparison operations
/// 4. **Division** (div, sdiv, udiv, rem, srem, urem): Complex division/remainder
/// 5. **Shifts** (shl, lshr, ashr): Bit shift operations
/// 6. **Control Flow** (select, br, cbr, ret): Branching and returns
/// 7. **Calls** (call, call.indirect): Function invocations
/// 8. **Memory** (load, store, alloca, gep): Memory operations
/// 9. **Conversions** (zext, sext, trunc, sitofp, fptosi): Type casts
/// 10. **Exception Handling** (eh.push, eh.pop, eh.entry): EH support
/// 11. **Miscellaneous** (trap, const_str): Special operations
///
/// ## Adding New Rules
///
/// To add support for a new IL opcode:
/// 1. Implement the emit function (e.g., `emitNewOp`)
/// 2. Add the function declaration at the top of this file
/// 3. Add a RuleSpec entry to this table with appropriate operand shape
/// 4. Update the table size (currently 39)
///
/// @see lookupRuleSpec() to find a rule for an instruction
/// @see matchesRuleSpec() for the rule matching implementation
inline constexpr auto kLoweringRuleTable = std::array<RuleSpec, 39>{
    // === Arithmetic Operations ===
    RuleSpec{"add",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitAdd,
             "add"},
    RuleSpec{"sub",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitSub,
             "sub"},
    RuleSpec{"mul",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitMul,
             "mul"},
    RuleSpec{"fdiv",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitFDiv,
             "fdiv"},
    RuleSpec{"and",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitAnd,
             "and"},
    RuleSpec{"or",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitOr,
             "or"},
    RuleSpec{"xor",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitXor,
             "xor"},
    RuleSpec{"icmp_",
             OperandShape{2U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Immediate,
                           OperandKindPattern::Any}},
             RuleFlags::Prefix,
             &emitICmp,
             "icmp"},
    RuleSpec{"fcmp_",
             OperandShape{2U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Immediate,
                           OperandKindPattern::Any}},
             RuleFlags::Prefix,
             &emitFCmp,
             "fcmp"},
    RuleSpec{"div",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "div"},
    RuleSpec{"sdiv",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "sdiv"},
    RuleSpec{"srem",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "srem"},
    RuleSpec{"udiv",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "udiv"},
    RuleSpec{"urem",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "urem"},
    RuleSpec{"rem",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitDivFamily,
             "rem"},
    RuleSpec{"shl",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitShiftLeft,
             "shl"},
    RuleSpec{"lshr",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitShiftLshr,
             "lshr"},
    RuleSpec{"ashr",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitShiftAshr,
             "ashr"},
    RuleSpec{"cmp",
             OperandShape{2U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Immediate,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitCmpExplicit,
             "cmp"},
    RuleSpec{"select",
             OperandShape{3U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitSelect,
             "select"},
    RuleSpec{"br",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Label,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitBranch,
             "br"},
    RuleSpec{"cbr",
             OperandShape{3U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Label,
                           OperandKindPattern::Label,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitCondBranch,
             "cbr"},
    RuleSpec{"ret",
             OperandShape{0U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitReturn,
             "ret"},
    RuleSpec{"call",
             OperandShape{1U,
                          std::numeric_limits<std::uint8_t>::max(),
                          1U,
                          {OperandKindPattern::Label,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitCall,
             "call"},
    RuleSpec{"call.indirect",
             OperandShape{1U,
                          std::numeric_limits<std::uint8_t>::max(),
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitCallIndirect,
             "call.indirect"},
    RuleSpec{"load",
             OperandShape{1U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Immediate,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitLoadAuto,
             "load"},
    RuleSpec{"store",
             OperandShape{2U,
                          3U,
                          3U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Immediate,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitStore,
             "store"},
    RuleSpec{"zext",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitZSTrunc,
             "zext"},
    RuleSpec{"sext",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitZSTrunc,
             "sext"},
    RuleSpec{"trunc",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitZSTrunc,
             "trunc"},
    RuleSpec{"sitofp",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitSIToFP,
             "sitofp"},
    RuleSpec{"fptosi",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitFPToSI,
             "fptosi"},
    RuleSpec{"eh.push",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Label,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitEhPush,
             "eh.push"},
    RuleSpec{"eh.pop", OperandShape{0U, 0U, 0U, {}}, RuleFlags::None, &emitEhPop, "eh.pop"},
    RuleSpec{"eh.entry", OperandShape{0U, 0U, 0U, {}}, RuleFlags::None, &emitEhEntry, "eh.entry"},
    RuleSpec{"trap",
             OperandShape{0U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitTrap,
             "trap"},
    RuleSpec{"const_str",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitConstStr,
             "const_str"},
    RuleSpec{"alloca",
             OperandShape{1U,
                          1U,
                          1U,
                          {OperandKindPattern::Immediate,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitAlloca,
             "alloca"},
    RuleSpec{"gep",
             OperandShape{2U,
                          2U,
                          2U,
                          {OperandKindPattern::Value,
                           OperandKindPattern::Value,
                           OperandKindPattern::Any,
                           OperandKindPattern::Any}},
             RuleFlags::None,
             &emitGEP,
             "gep"},
};

} // namespace lowering

/// @brief Tests whether a lowering rule matches an IL instruction.
///
/// Performs the full matching algorithm to determine if a RuleSpec can handle
/// a given IL instruction. The matching process checks:
///
/// 1. **Opcode Match**: The instruction's opcode must match the rule's opcode.
///    If RuleFlags::Prefix is set, the rule's opcode is treated as a prefix
///    (e.g., rule "icmp_" matches instruction "icmp_eq").
///
/// 2. **Arity Check**: The instruction's operand count must be within the
///    rule's [minArity, maxArity] range (inclusive).
///
/// 3. **Kind Check**: For each operand position up to `kindCount`, the operand's
///    kind must match the pattern. OperandKindPattern::Any matches anything.
///
/// @param spec The lowering rule specification to test.
/// @param instr The IL instruction to match against.
/// @return True if the rule can handle this instruction, false otherwise.
///
/// @par Example Usage
/// ```cpp
/// for (const auto& rule : kLoweringRuleTable) {
///     if (matchesRuleSpec(rule, instr)) {
///         rule.emit(instr, builder);
///         break;
///     }
/// }
/// ```
///
/// @see lookupRuleSpec() for a convenience wrapper that returns the first match
/// @see RuleSpec for details on the matching criteria
bool matchesRuleSpec(const lowering::RuleSpec &spec, const ILInstr &instr);

/// @brief Finds the first lowering rule that matches an IL instruction.
///
/// Searches the kLoweringRuleTable linearly and returns a pointer to the first
/// rule that matches the instruction (as determined by matchesRuleSpec). This
/// is the primary entry point for instruction selection during lowering.
///
/// ## Performance Note
///
/// The current implementation uses linear search through the rule table. For
/// the current table size (39 rules), this is efficient enough. If the table
/// grows significantly, consider switching to a hash map indexed by opcode
/// or a trie structure for prefix matching.
///
/// ## No Match Handling
///
/// If no rule matches, this function returns nullptr. The caller should handle
/// this case, typically by reporting an error (unknown IL instruction) or
/// falling back to a generic lowering strategy.
///
/// @param instr The IL instruction to find a rule for.
/// @return Pointer to the matching RuleSpec, or nullptr if no rule matches.
///
/// @par Example Usage
/// ```cpp
/// void lowerInstruction(const ILInstr& instr, MIRBuilder& builder) {
///     const auto* rule = lookupRuleSpec(instr);
///     if (!rule) {
///         reportError("Unknown IL instruction: " + instr.opcode);
///         return;
///     }
///     rule->emit(instr, builder);
/// }
/// ```
///
/// @see kLoweringRuleTable for the complete set of rules
/// @see matchesRuleSpec() for the matching algorithm details
const lowering::RuleSpec *lookupRuleSpec(const ILInstr &instr);

} // namespace viper::codegen::x64

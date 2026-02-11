//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/bytecode/Bytecode.hpp
// Purpose: Bytecode instruction format, opcode definitions, and runtime value type.
// Key invariants: All instructions are 32-bit fixed-width (or 64-bit extended).
//                 Opcodes are grouped by functional category for cache-friendly dispatch.
//                 BCSlot is exactly 8 bytes (static_assert enforced).
// Ownership: Part of the bytecode subsystem; no external dependencies.
// Lifetime: Constants and inline helpers are header-only; opcodeName() is defined
//           in the corresponding .cpp translation unit.
// Links: docs/architecture.md, BytecodeCompiler.hpp, BytecodeVM.hpp
//
//===----------------------------------------------------------------------===//
//
// This file defines the compact bytecode format used by the Viper bytecode VM.
// Bytecode is compiled from IL at module load time and interpreted for fast
// execution compared to direct IL interpretation.
//
// Instruction Encoding:
// - 32-bit fixed-width primary format: [opcode:8][arg0:8][arg1:8][arg2:8]
// - 64-bit extended format for large operands
//
// Stack Model:
// - Stack-based evaluation with local variable slots
// - Parameters mapped to first N locals
// - Operand stack grows upward from locals

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace viper::bytecode
{

/// @brief Magic number for bytecode modules: "VBC\x01".
/// @details Stored in little-endian byte order at the beginning of every
///          serialized bytecode module to identify the file format.
constexpr uint32_t kBytecodeModuleMagic = 0x01434256;

/// @brief Current bytecode format version.
/// @details Incremented whenever the instruction encoding, constant pool
///          layout, or module structure changes in an incompatible way.
constexpr uint32_t kBytecodeVersion = 1;

/// @brief Maximum call stack depth before a StackOverflow trap is raised.
constexpr uint32_t kMaxCallDepth = 4096;

/// @brief Maximum operand stack size (in BCSlot entries) per call frame.
constexpr uint32_t kMaxStackSize = 1024;

/// @brief Bytecode opcodes for the Viper bytecode VM.
/// @details Opcodes are organized by functional category and assigned to
///          contiguous ranges so that the interpreter's dispatch table
///          benefits from instruction-cache locality.
///
///          Encoding categories:
///          - 0x00-0x0F  Stack operations
///          - 0x10-0x1F  Local variable operations
///          - 0x20-0x2F  Constant loading
///          - 0x30-0x4F  Integer arithmetic
///          - 0x50-0x5F  Float arithmetic
///          - 0x60-0x6F  Bitwise operations
///          - 0x70-0x7F  Integer comparisons
///          - 0x80-0x8F  Float comparisons
///          - 0x90-0x9F  Type conversions
///          - 0xA0-0xAF  Memory operations
///          - 0xB0-0xBF  Control flow
///          - 0xC0-0xCF  Exception handling
///          - 0xD0-0xDF  Debug operations
///          - 0xE0-0xEF  String operations
enum class BCOpcode : uint8_t
{
    // Stack Operations (0x00-0x0F)
    NOP = 0x00,  ///< No operation.
    DUP = 0x01,  ///< Duplicate top-of-stack (TOS).
    DUP2 = 0x02, ///< Duplicate the top two stack entries.
    POP = 0x03,  ///< Discard TOS.
    POP2 = 0x04, ///< Discard the top two stack entries.
    SWAP = 0x05, ///< Swap the top two stack entries.
    ROT3 = 0x06, ///< Rotate the top three stack entries (a b c -> c a b).

    // Local Variable Operations (0x10-0x1F)
    LOAD_LOCAL = 0x10,    ///< Push locals[arg0] onto the operand stack.
    STORE_LOCAL = 0x11,   ///< Pop TOS and store to locals[arg0].
    LOAD_LOCAL_W = 0x12,  ///< Wide local load using a 16-bit index.
    STORE_LOCAL_W = 0x13, ///< Wide local store using a 16-bit index.
    INC_LOCAL = 0x14,     ///< Increment locals[arg0] in-place (locals[arg0]++).
    DEC_LOCAL = 0x15,     ///< Decrement locals[arg0] in-place (locals[arg0]--).

    // Constant Loading (0x20-0x2F)
    LOAD_I8 = 0x20,      ///< Push a signed 8-bit immediate value.
    LOAD_I16 = 0x21,     ///< Push a signed 16-bit immediate value.
    LOAD_I32 = 0x22,     ///< Push a signed 32-bit value (extended format).
    LOAD_I64 = 0x23,     ///< Push an i64 from the constant pool at index [arg0:arg1].
    LOAD_F64 = 0x24,     ///< Push an f64 from the constant pool at index [arg0:arg1].
    LOAD_STR = 0x25,     ///< Push a string from the constant pool at index [arg0:arg1].
    LOAD_NULL = 0x26,    ///< Push a null pointer value.
    LOAD_ZERO = 0x27,    ///< Push i64 zero (fast-path constant).
    LOAD_ONE = 0x28,     ///< Push i64 one (fast-path constant).
    LOAD_GLOBAL = 0x29,  ///< Push the value of global[arg0:arg1].
    STORE_GLOBAL = 0x2A, ///< Pop TOS and store to global[arg0:arg1].

    // Integer Arithmetic (0x30-0x4F)
    ADD_I64 = 0x30,      ///< Integer addition: a + b.
    SUB_I64 = 0x31,      ///< Integer subtraction: a - b.
    MUL_I64 = 0x32,      ///< Integer multiplication: a * b.
    SDIV_I64 = 0x33,     ///< Signed integer division: a / b.
    UDIV_I64 = 0x34,     ///< Unsigned integer division: a / b.
    SREM_I64 = 0x35,     ///< Signed integer remainder: a % b.
    UREM_I64 = 0x36,     ///< Unsigned integer remainder: a % b.
    NEG_I64 = 0x37,      ///< Integer negation: -a.
    ADD_I64_OVF = 0x38,  ///< Integer addition with overflow trap.
    SUB_I64_OVF = 0x39,  ///< Integer subtraction with overflow trap.
    MUL_I64_OVF = 0x3A,  ///< Integer multiplication with overflow trap.
    SDIV_I64_CHK = 0x3B, ///< Signed division with zero-divisor trap.
    UDIV_I64_CHK = 0x3C, ///< Unsigned division with zero-divisor trap.
    SREM_I64_CHK = 0x3D, ///< Signed remainder with zero-divisor trap.
    UREM_I64_CHK = 0x3E, ///< Unsigned remainder with zero-divisor trap.
    IDX_CHK = 0x3F,      ///< Bounds check: traps unless lo <= idx < hi.

    // Float Arithmetic (0x50-0x5F)
    ADD_F64 = 0x50, ///< Float addition: a + b.
    SUB_F64 = 0x51, ///< Float subtraction: a - b.
    MUL_F64 = 0x52, ///< Float multiplication: a * b.
    DIV_F64 = 0x53, ///< Float division: a / b.
    NEG_F64 = 0x54, ///< Float negation: -a.

    // Bitwise Operations (0x60-0x6F)
    AND_I64 = 0x60,  ///< Bitwise AND: a & b.
    OR_I64 = 0x61,   ///< Bitwise OR: a | b.
    XOR_I64 = 0x62,  ///< Bitwise XOR: a ^ b.
    NOT_I64 = 0x63,  ///< Bitwise NOT: ~a.
    SHL_I64 = 0x64,  ///< Left shift: a << b.
    LSHR_I64 = 0x65, ///< Logical right shift: a >>> b.
    ASHR_I64 = 0x66, ///< Arithmetic right shift: a >> b.

    // Integer Comparisons (0x70-0x7F)
    CMP_EQ_I64 = 0x70,  ///< Integer equality: a == b.
    CMP_NE_I64 = 0x71,  ///< Integer inequality: a != b.
    CMP_SLT_I64 = 0x72, ///< Signed less-than: a < b.
    CMP_SLE_I64 = 0x73, ///< Signed less-or-equal: a <= b.
    CMP_SGT_I64 = 0x74, ///< Signed greater-than: a > b.
    CMP_SGE_I64 = 0x75, ///< Signed greater-or-equal: a >= b.
    CMP_ULT_I64 = 0x76, ///< Unsigned less-than: a < b.
    CMP_ULE_I64 = 0x77, ///< Unsigned less-or-equal: a <= b.
    CMP_UGT_I64 = 0x78, ///< Unsigned greater-than: a > b.
    CMP_UGE_I64 = 0x79, ///< Unsigned greater-or-equal: a >= b.

    // Float Comparisons (0x80-0x8F)
    CMP_EQ_F64 = 0x80, ///< Float equality: a == b.
    CMP_NE_F64 = 0x81, ///< Float inequality: a != b.
    CMP_LT_F64 = 0x82, ///< Float less-than: a < b.
    CMP_LE_F64 = 0x83, ///< Float less-or-equal: a <= b.
    CMP_GT_F64 = 0x84, ///< Float greater-than: a > b.
    CMP_GE_F64 = 0x85, ///< Float greater-or-equal: a >= b.

    // Type Conversions (0x90-0x9F)
    I64_TO_F64 = 0x90,     ///< Convert signed i64 to f64.
    U64_TO_F64 = 0x91,     ///< Convert unsigned i64 to f64.
    F64_TO_I64 = 0x92,     ///< Convert f64 to signed i64 (truncation).
    F64_TO_I64_CHK = 0x93, ///< Convert f64 to i64 with range-check trap.
    F64_TO_U64_CHK = 0x94, ///< Convert f64 to u64 with range-check trap.
    I64_NARROW_CHK = 0x95, ///< Signed narrow with overflow-check trap.
    U64_NARROW_CHK = 0x96, ///< Unsigned narrow with overflow-check trap.
    BOOL_TO_I64 = 0x97,    ///< Convert boolean (0/1) to i64.
    I64_TO_BOOL = 0x98,    ///< Convert i64 to boolean (nonzero -> 1, zero -> 0).

    // Memory Operations (0xA0-0xAF)
    ALLOCA = 0xA0,        ///< Allocate n bytes on the alloca stack.
    GEP = 0xA1,           ///< Get element pointer: ptr + offset.
    LOAD_I8_MEM = 0xA2,   ///< Load 8-bit signed value from memory.
    LOAD_I16_MEM = 0xA3,  ///< Load 16-bit signed value from memory.
    LOAD_I32_MEM = 0xA4,  ///< Load 32-bit signed value from memory.
    LOAD_I64_MEM = 0xA5,  ///< Load 64-bit value from memory.
    LOAD_F64_MEM = 0xA6,  ///< Load f64 value from memory.
    LOAD_PTR_MEM = 0xA7,  ///< Load pointer value from memory.
    LOAD_STR_MEM = 0xA8,  ///< Load string handle from memory.
    STORE_I8_MEM = 0xA9,  ///< Store 8-bit value to memory.
    STORE_I16_MEM = 0xAA, ///< Store 16-bit value to memory.
    STORE_I32_MEM = 0xAB, ///< Store 32-bit value to memory.
    STORE_I64_MEM = 0xAC, ///< Store 64-bit value to memory.
    STORE_F64_MEM = 0xAD, ///< Store f64 value to memory.
    STORE_PTR_MEM = 0xAE, ///< Store pointer value to memory.
    STORE_STR_MEM = 0xAF, ///< Store string handle to memory.

    // Control Flow (0xB0-0xBF)
    JUMP = 0xB0,          ///< Unconditional jump (16-bit signed offset).
    JUMP_IF_TRUE = 0xB1,  ///< Conditional jump if TOS != 0 (16-bit offset).
    JUMP_IF_FALSE = 0xB2, ///< Conditional jump if TOS == 0 (16-bit offset).
    JUMP_LONG = 0xB3,     ///< Extended unconditional jump (24-bit offset).
    SWITCH = 0xB4,        ///< Table-driven switch dispatch.
    CALL = 0xB5,          ///< Call bytecode function by index [arg0:arg1].
    CALL_NATIVE = 0xB6,   ///< Call a native/runtime function.
    CALL_INDIRECT = 0xB7, ///< Indirect call through a function pointer.
    RETURN = 0xB8,        ///< Return TOS from the current function.
    RETURN_VOID = 0xB9,   ///< Return void from the current function.
    TAIL_CALL = 0xBA,     ///< Tail-call optimization: reuse current frame.

    // Exception Handling (0xC0-0xCF)
    EH_PUSH = 0xC0,       ///< Register an exception handler at a given offset.
    EH_POP = 0xC1,        ///< Unregister the most recently pushed handler.
    EH_ENTRY = 0xC2,      ///< Marker for handler entry point.
    TRAP = 0xC3,          ///< Raise a trap with a specified kind.
    TRAP_FROM_ERR = 0xC4, ///< Raise a trap from an error value.
    MAKE_ERROR = 0xC5,    ///< Create an error value on the stack.
    ERR_GET_KIND = 0xC6,  ///< Extract the trap kind from an error value.
    ERR_GET_CODE = 0xC7,  ///< Extract the error code from an error value.
    ERR_GET_IP = 0xC8,    ///< Extract the faulting instruction pointer.
    ERR_GET_LINE = 0xC9,  ///< Extract the source line number from an error.
    RESUME_SAME = 0xCA,   ///< Resume execution at the faulting instruction.
    RESUME_NEXT = 0xCB,   ///< Resume execution at the instruction after the fault.
    RESUME_LABEL = 0xCC,  ///< Resume execution at a labelled target.

    // Debug Operations (0xD0-0xDF)
    LINE = 0xD0,       ///< Source line marker for debug info.
    BREAKPOINT = 0xD1, ///< Debug breakpoint trap.
    WATCH_VAR = 0xD2,  ///< Variable watch trigger for the debugger.

    // String Operations (0xE0-0xEF)
    STR_RETAIN = 0xE0,  ///< Increment the reference count of a string handle.
    STR_RELEASE = 0xE1, ///< Decrement the reference count of a string handle.

    // Sentinel value
    OPCODE_COUNT = 0xFF ///< Sentinel / total opcode count marker.
};

/// @brief Get the human-readable name for an opcode.
/// @details Used for disassembly output, debug logging, and diagnostic messages.
/// @param op The bytecode opcode to look up.
/// @return A NUL-terminated string naming the opcode (e.g., "ADD_I64").
///         Returns "UNKNOWN" for unrecognized opcode values.
const char *opcodeName(BCOpcode op);

/// @brief Check whether an opcode is a basic-block terminator.
/// @details Terminators are instructions that transfer control flow out of the
///          current basic block (jumps, returns, traps, resumes). The compiler
///          and verifier use this to identify block boundaries.
/// @param op The opcode to test.
/// @return True if the opcode ends a basic block; false otherwise.
inline constexpr bool isTerminator(BCOpcode op)
{
    return op == BCOpcode::JUMP || op == BCOpcode::JUMP_IF_TRUE || op == BCOpcode::JUMP_IF_FALSE ||
           op == BCOpcode::JUMP_LONG || op == BCOpcode::SWITCH || op == BCOpcode::RETURN ||
           op == BCOpcode::RETURN_VOID || op == BCOpcode::TAIL_CALL || op == BCOpcode::TRAP ||
           op == BCOpcode::TRAP_FROM_ERR || op == BCOpcode::RESUME_SAME ||
           op == BCOpcode::RESUME_NEXT || op == BCOpcode::RESUME_LABEL;
}

/// @brief Check whether an opcode can raise a trap (exception).
/// @details Instructions that may trap include checked arithmetic, checked
///          conversions, memory allocation, function calls, and explicit traps.
///          The compiler uses this to determine which instructions require
///          exception handler coverage.
/// @param op The opcode to test.
/// @return True if the opcode may raise a trap; false otherwise.
inline constexpr bool canTrap(BCOpcode op)
{
    switch (op)
    {
        case BCOpcode::ADD_I64_OVF:
        case BCOpcode::SUB_I64_OVF:
        case BCOpcode::MUL_I64_OVF:
        case BCOpcode::SDIV_I64_CHK:
        case BCOpcode::UDIV_I64_CHK:
        case BCOpcode::SREM_I64_CHK:
        case BCOpcode::UREM_I64_CHK:
        case BCOpcode::IDX_CHK:
        case BCOpcode::F64_TO_I64_CHK:
        case BCOpcode::F64_TO_U64_CHK:
        case BCOpcode::I64_NARROW_CHK:
        case BCOpcode::U64_NARROW_CHK:
        case BCOpcode::ALLOCA:
        case BCOpcode::CALL:
        case BCOpcode::CALL_NATIVE:
        case BCOpcode::CALL_INDIRECT:
        case BCOpcode::TRAP:
        case BCOpcode::TRAP_FROM_ERR:
            return true;
        default:
            return false;
    }
}

//==============================================================================
// Instruction Encoding Helpers
//==============================================================================

/// @brief Encode a 32-bit instruction containing only an opcode (no arguments).
/// @details Produces the encoding [opcode:8][0:24].
/// @param op The opcode to encode.
/// @return The 32-bit encoded instruction word.
inline constexpr uint32_t encodeOp(BCOpcode op)
{
    return static_cast<uint32_t>(op);
}

/// @brief Encode a 32-bit instruction with an opcode and one unsigned 8-bit argument.
/// @details Produces the encoding [opcode:8][arg0:8][0:16].
/// @param op   The opcode to encode.
/// @param arg0 The 8-bit unsigned argument (e.g., local slot index).
/// @return The 32-bit encoded instruction word.
inline constexpr uint32_t encodeOp8(BCOpcode op, uint8_t arg0)
{
    return static_cast<uint32_t>(op) | (static_cast<uint32_t>(arg0) << 8);
}

/// @brief Encode a 32-bit instruction with an opcode and one signed 8-bit argument.
/// @details Produces the encoding [opcode:8][arg0:8][0:16] where arg0 is
///          reinterpreted as unsigned for bit packing.
/// @param op   The opcode to encode.
/// @param arg0 The signed 8-bit argument (e.g., small immediate).
/// @return The 32-bit encoded instruction word.
inline constexpr uint32_t encodeOpI8(BCOpcode op, int8_t arg0)
{
    return static_cast<uint32_t>(op) | (static_cast<uint32_t>(static_cast<uint8_t>(arg0)) << 8);
}

/// @brief Encode a 32-bit instruction with an opcode and two unsigned 8-bit arguments.
/// @details Produces the encoding [opcode:8][arg0:8][arg1:8][0:8].
/// @param op   The opcode to encode.
/// @param arg0 First 8-bit unsigned argument.
/// @param arg1 Second 8-bit unsigned argument.
/// @return The 32-bit encoded instruction word.
inline constexpr uint32_t encodeOp88(BCOpcode op, uint8_t arg0, uint8_t arg1)
{
    return static_cast<uint32_t>(op) | (static_cast<uint32_t>(arg0) << 8) |
           (static_cast<uint32_t>(arg1) << 16);
}

/// @brief Encode a 32-bit instruction with an opcode and one unsigned 16-bit argument.
/// @details Produces the encoding [opcode:8][arg0_lo:8][arg0_hi:8][0:8].
///          The 16-bit argument occupies bits 8-23.
/// @param op   The opcode to encode.
/// @param arg0 The 16-bit unsigned argument (e.g., wide local index, pool index).
/// @return The 32-bit encoded instruction word.
inline constexpr uint32_t encodeOp16(BCOpcode op, uint16_t arg0)
{
    return static_cast<uint32_t>(op) | (static_cast<uint32_t>(arg0) << 8);
}

/// @brief Encode a 32-bit instruction with an opcode and one signed 16-bit argument.
/// @details Produces the encoding [opcode:8][arg0_lo:8][arg0_hi:8][0:8] where
///          the signed value is reinterpreted as unsigned for bit packing.
/// @param op   The opcode to encode.
/// @param arg0 The signed 16-bit argument (e.g., branch offset).
/// @return The 32-bit encoded instruction word.
inline constexpr uint32_t encodeOpI16(BCOpcode op, int16_t arg0)
{
    return static_cast<uint32_t>(op) | (static_cast<uint32_t>(static_cast<uint16_t>(arg0)) << 8);
}

/// @brief Encode a 32-bit instruction with an opcode, an 8-bit arg, and a 16-bit arg.
/// @details Produces the encoding [opcode:8][arg0:8][arg1_lo:8][arg1_hi:8].
/// @param op   The opcode to encode.
/// @param arg0 First 8-bit unsigned argument.
/// @param arg1 Second 16-bit unsigned argument.
/// @return The 32-bit encoded instruction word.
inline constexpr uint32_t encodeOp8_16(BCOpcode op, uint8_t arg0, uint16_t arg1)
{
    return static_cast<uint32_t>(op) | (static_cast<uint32_t>(arg0) << 8) |
           (static_cast<uint32_t>(arg1) << 16);
}

/// @brief Encode a 32-bit instruction with an opcode and one unsigned 24-bit argument.
/// @details Produces the encoding [opcode:8][arg0:24]. The argument is masked
///          to 24 bits to prevent overflow into the opcode field.
/// @param op   The opcode to encode.
/// @param arg0 The 24-bit unsigned argument (e.g., long jump offset).
/// @return The 32-bit encoded instruction word.
inline constexpr uint32_t encodeOp24(BCOpcode op, uint32_t arg0)
{
    return static_cast<uint32_t>(op) | ((arg0 & 0xFFFFFF) << 8);
}

/// @brief Encode a 32-bit instruction with an opcode and one signed 24-bit argument.
/// @details Produces the encoding [opcode:8][arg0:24]. The signed value is
///          masked to 24 bits; sign extension is performed during decoding.
/// @param op   The opcode to encode.
/// @param arg0 The signed 24-bit argument (e.g., signed long jump offset).
/// @return The 32-bit encoded instruction word.
inline constexpr uint32_t encodeOpI24(BCOpcode op, int32_t arg0)
{
    return static_cast<uint32_t>(op) | ((static_cast<uint32_t>(arg0) & 0xFFFFFF) << 8);
}

//==============================================================================
// Instruction Decoding Helpers
//==============================================================================

/// @brief Extract the opcode from a 32-bit instruction word.
/// @param instr The encoded instruction word.
/// @return The BCOpcode stored in bits 0-7.
inline constexpr BCOpcode decodeOpcode(uint32_t instr)
{
    return static_cast<BCOpcode>(instr & 0xFF);
}

/// @brief Extract the first unsigned 8-bit argument (bits 8-15).
/// @param instr The encoded instruction word.
/// @return The unsigned 8-bit value in the arg0 field.
inline constexpr uint8_t decodeArg8_0(uint32_t instr)
{
    return static_cast<uint8_t>((instr >> 8) & 0xFF);
}

/// @brief Extract the first signed 8-bit argument (bits 8-15).
/// @param instr The encoded instruction word.
/// @return The signed 8-bit value in the arg0 field.
inline constexpr int8_t decodeArgI8_0(uint32_t instr)
{
    return static_cast<int8_t>((instr >> 8) & 0xFF);
}

/// @brief Extract the second unsigned 8-bit argument (bits 16-23).
/// @param instr The encoded instruction word.
/// @return The unsigned 8-bit value in the arg1 field.
inline constexpr uint8_t decodeArg8_1(uint32_t instr)
{
    return static_cast<uint8_t>((instr >> 16) & 0xFF);
}

/// @brief Extract the third unsigned 8-bit argument (bits 24-31).
/// @param instr The encoded instruction word.
/// @return The unsigned 8-bit value in the arg2 field.
inline constexpr uint8_t decodeArg8_2(uint32_t instr)
{
    return static_cast<uint8_t>((instr >> 24) & 0xFF);
}

/// @brief Extract a 16-bit unsigned argument from bits 8-23.
/// @param instr The encoded instruction word.
/// @return The unsigned 16-bit value spanning arg0 and arg1 fields.
inline constexpr uint16_t decodeArg16(uint32_t instr)
{
    return static_cast<uint16_t>((instr >> 8) & 0xFFFF);
}

/// @brief Extract a 16-bit signed argument from bits 8-23.
/// @param instr The encoded instruction word.
/// @return The signed 16-bit value spanning arg0 and arg1 fields.
inline constexpr int16_t decodeArgI16(uint32_t instr)
{
    return static_cast<int16_t>((instr >> 8) & 0xFFFF);
}

/// @brief Extract the second 16-bit unsigned argument from bits 16-31.
/// @param instr The encoded instruction word.
/// @return The unsigned 16-bit value in the upper half of the argument space.
inline constexpr uint16_t decodeArg16_1(uint32_t instr)
{
    return static_cast<uint16_t>((instr >> 16) & 0xFFFF);
}

/// @brief Extract a 24-bit unsigned argument from bits 8-31.
/// @param instr The encoded instruction word.
/// @return The unsigned 24-bit value occupying the full argument space.
inline constexpr uint32_t decodeArg24(uint32_t instr)
{
    return (instr >> 8) & 0xFFFFFF;
}

/// @brief Extract a 24-bit signed argument from bits 8-31 with sign extension.
/// @details If bit 23 (the sign bit within the 24-bit field) is set, the value
///          is sign-extended to a full 32-bit signed integer.
/// @param instr The encoded instruction word.
/// @return The sign-extended 24-bit signed value.
inline constexpr int32_t decodeArgI24(uint32_t instr)
{
    uint32_t raw = (instr >> 8) & 0xFFFFFF;
    // Sign extend from 24-bit
    if (raw & 0x800000)
    {
        return static_cast<int32_t>(raw | 0xFF000000);
    }
    return static_cast<int32_t>(raw);
}

//==============================================================================
// BCSlot - Runtime Value Type
//==============================================================================

/// @brief Tagged union for runtime values on the operand stack and in local variable slots.
/// @details BCSlot is the fundamental value type used by the bytecode VM. Every
///          entry on the operand stack and every local variable slot holds one BCSlot.
///          The union overlays three representations that share 8 bytes of storage:
///          - i64: 64-bit signed integer (also used for booleans and unsigned values)
///          - f64: IEEE-754 double-precision floating-point
///          - ptr: Generic pointer for objects, strings, and memory references
///
///          The correct interpretation is determined by the opcode that produces or
///          consumes the value. There is no runtime type tag; type safety is ensured
///          at compile time by the bytecode compiler.
/// @invariant sizeof(BCSlot) == 8 (enforced by static_assert).
union BCSlot
{
    int64_t i64; ///< Integer representation (also booleans, unsigned values).
    double f64;  ///< IEEE-754 double-precision floating-point representation.
    void *ptr;   ///< Pointer representation (objects, strings, memory addresses).

    /// @brief Default constructor; zero-initializes the integer field.
    BCSlot() : i64(0) {}

    /// @brief Construct a BCSlot holding a 64-bit integer value.
    /// @param v The integer value to store.
    explicit BCSlot(int64_t v) : i64(v) {}

    /// @brief Construct a BCSlot holding a double-precision floating-point value.
    /// @param v The double value to store.
    explicit BCSlot(double v) : f64(v) {}

    /// @brief Construct a BCSlot holding a pointer value.
    /// @param v The pointer value to store (may be nullptr).
    explicit BCSlot(void *v) : ptr(v) {}

    /// @brief Create a BCSlot from a 64-bit integer value.
    /// @param v The integer value.
    /// @return A new BCSlot with the i64 field set.
    static BCSlot fromInt(int64_t v)
    {
        return BCSlot(v);
    }

    /// @brief Create a BCSlot from a double-precision floating-point value.
    /// @param v The double value.
    /// @return A new BCSlot with the f64 field set.
    static BCSlot fromFloat(double v)
    {
        return BCSlot(v);
    }

    /// @brief Create a BCSlot from a pointer value.
    /// @param v The pointer value (may be nullptr).
    /// @return A new BCSlot with the ptr field set.
    static BCSlot fromPtr(void *v)
    {
        return BCSlot(v);
    }

    /// @brief Create a BCSlot holding a null pointer.
    /// @return A new BCSlot with ptr set to nullptr.
    static BCSlot null()
    {
        return BCSlot(static_cast<void *>(nullptr));
    }
};

static_assert(sizeof(BCSlot) == 8, "BCSlot must be 8 bytes");

} // namespace viper::bytecode

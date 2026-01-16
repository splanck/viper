// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// Bytecode.hpp - Bytecode instruction format and opcode definitions
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

namespace viper::bytecode {

// Magic number for bytecode modules: "VBC\x01"
constexpr uint32_t kBytecodeModuleMagic = 0x01434256;

// Current bytecode version
constexpr uint32_t kBytecodeVersion = 1;

// Maximum call stack depth
constexpr uint32_t kMaxCallDepth = 4096;

// Maximum operand stack size per frame
constexpr uint32_t kMaxStackSize = 1024;

/// Bytecode opcodes
/// Organized by functional category for cache-friendly dispatch
enum class BCOpcode : uint8_t {
    // Stack Operations (0x00-0x0F)
    NOP          = 0x00,  // No operation
    DUP          = 0x01,  // Duplicate TOS
    DUP2         = 0x02,  // Duplicate top 2
    POP          = 0x03,  // Discard TOS
    POP2         = 0x04,  // Discard top 2
    SWAP         = 0x05,  // Swap top 2
    ROT3         = 0x06,  // Rotate top 3

    // Local Variable Operations (0x10-0x1F)
    LOAD_LOCAL   = 0x10,  // Push locals[arg0]
    STORE_LOCAL  = 0x11,  // Pop to locals[arg0]
    LOAD_LOCAL_W = 0x12,  // Wide local load (16-bit index)
    STORE_LOCAL_W= 0x13,  // Wide local store (16-bit index)
    INC_LOCAL    = 0x14,  // locals[arg0]++
    DEC_LOCAL    = 0x15,  // locals[arg0]--

    // Constant Loading (0x20-0x2F)
    LOAD_I8      = 0x20,  // Push signed 8-bit immediate
    LOAD_I16     = 0x21,  // Push signed 16-bit immediate
    LOAD_I32     = 0x22,  // Push signed 32-bit (extended format)
    LOAD_I64     = 0x23,  // Push i64 from pool[arg0:arg1]
    LOAD_F64     = 0x24,  // Push f64 from pool[arg0:arg1]
    LOAD_STR     = 0x25,  // Push string from pool[arg0:arg1]
    LOAD_NULL    = 0x26,  // Push null pointer
    LOAD_ZERO    = 0x27,  // Push i64 zero
    LOAD_ONE     = 0x28,  // Push i64 one
    LOAD_GLOBAL  = 0x29,  // Push global[arg0:arg1]
    STORE_GLOBAL = 0x2A,  // Pop to global[arg0:arg1]

    // Integer Arithmetic (0x30-0x4F)
    ADD_I64      = 0x30,  // a + b
    SUB_I64      = 0x31,  // a - b
    MUL_I64      = 0x32,  // a * b
    SDIV_I64     = 0x33,  // a / b (signed)
    UDIV_I64     = 0x34,  // a / b (unsigned)
    SREM_I64     = 0x35,  // a % b (signed)
    UREM_I64     = 0x36,  // a % b (unsigned)
    NEG_I64      = 0x37,  // -a
    ADD_I64_OVF  = 0x38,  // a + b with overflow trap
    SUB_I64_OVF  = 0x39,  // a - b with overflow trap
    MUL_I64_OVF  = 0x3A,  // a * b with overflow trap
    SDIV_I64_CHK = 0x3B,  // a / b with zero-check trap
    UDIV_I64_CHK = 0x3C,  // a / b with zero-check trap
    SREM_I64_CHK = 0x3D,  // a % b with zero-check trap
    UREM_I64_CHK = 0x3E,  // a % b with zero-check trap
    IDX_CHK      = 0x3F,  // bounds check: lo <= idx < hi

    // Float Arithmetic (0x50-0x5F)
    ADD_F64      = 0x50,  // a + b
    SUB_F64      = 0x51,  // a - b
    MUL_F64      = 0x52,  // a * b
    DIV_F64      = 0x53,  // a / b
    NEG_F64      = 0x54,  // -a

    // Bitwise Operations (0x60-0x6F)
    AND_I64      = 0x60,  // a & b
    OR_I64       = 0x61,  // a | b
    XOR_I64      = 0x62,  // a ^ b
    NOT_I64      = 0x63,  // ~a
    SHL_I64      = 0x64,  // a << b
    LSHR_I64     = 0x65,  // a >>> b (logical)
    ASHR_I64     = 0x66,  // a >> b (arithmetic)

    // Integer Comparisons (0x70-0x7F)
    CMP_EQ_I64   = 0x70,  // a == b
    CMP_NE_I64   = 0x71,  // a != b
    CMP_SLT_I64  = 0x72,  // a < b (signed)
    CMP_SLE_I64  = 0x73,  // a <= b (signed)
    CMP_SGT_I64  = 0x74,  // a > b (signed)
    CMP_SGE_I64  = 0x75,  // a >= b (signed)
    CMP_ULT_I64  = 0x76,  // a < b (unsigned)
    CMP_ULE_I64  = 0x77,  // a <= b (unsigned)
    CMP_UGT_I64  = 0x78,  // a > b (unsigned)
    CMP_UGE_I64  = 0x79,  // a >= b (unsigned)

    // Float Comparisons (0x80-0x8F)
    CMP_EQ_F64   = 0x80,  // a == b
    CMP_NE_F64   = 0x81,  // a != b
    CMP_LT_F64   = 0x82,  // a < b
    CMP_LE_F64   = 0x83,  // a <= b
    CMP_GT_F64   = 0x84,  // a > b
    CMP_GE_F64   = 0x85,  // a >= b

    // Type Conversions (0x90-0x9F)
    I64_TO_F64   = 0x90,  // signed int to float
    U64_TO_F64   = 0x91,  // unsigned int to float
    F64_TO_I64   = 0x92,  // float to signed int
    F64_TO_I64_CHK = 0x93,  // float to int (checked)
    F64_TO_U64_CHK = 0x94,  // float to uint (checked)
    I64_NARROW_CHK = 0x95,  // signed narrow (checked)
    U64_NARROW_CHK = 0x96,  // unsigned narrow (checked)
    BOOL_TO_I64  = 0x97,  // boolean to i64
    I64_TO_BOOL  = 0x98,  // i64 to boolean

    // Memory Operations (0xA0-0xAF)
    ALLOCA       = 0xA0,  // allocate n bytes on stack
    GEP          = 0xA1,  // ptr + offset
    LOAD_I8_MEM  = 0xA2,  // load 8-bit signed
    LOAD_I16_MEM = 0xA3,  // load 16-bit signed
    LOAD_I32_MEM = 0xA4,  // load 32-bit signed
    LOAD_I64_MEM = 0xA5,  // load 64-bit
    LOAD_F64_MEM = 0xA6,  // load float
    LOAD_PTR_MEM = 0xA7,  // load pointer
    LOAD_STR_MEM = 0xA8,  // load string handle
    STORE_I8_MEM = 0xA9,  // store 8-bit
    STORE_I16_MEM= 0xAA,  // store 16-bit
    STORE_I32_MEM= 0xAB,  // store 32-bit
    STORE_I64_MEM= 0xAC,  // store 64-bit
    STORE_F64_MEM= 0xAD,  // store float
    STORE_PTR_MEM= 0xAE,  // store pointer
    STORE_STR_MEM= 0xAF,  // store string

    // Control Flow (0xB0-0xBF)
    JUMP         = 0xB0,  // unconditional jump (16-bit offset)
    JUMP_IF_TRUE = 0xB1,  // jump if TOS != 0
    JUMP_IF_FALSE= 0xB2,  // jump if TOS == 0
    JUMP_LONG    = 0xB3,  // extended jump (24-bit offset)
    SWITCH       = 0xB4,  // table switch
    CALL         = 0xB5,  // call function[arg0:arg1]
    CALL_NATIVE  = 0xB6,  // call runtime function
    CALL_INDIRECT= 0xB7,  // indirect call
    RETURN       = 0xB8,  // return TOS
    RETURN_VOID  = 0xB9,  // return void
    TAIL_CALL    = 0xBA,  // tail call

    // Exception Handling (0xC0-0xCF)
    EH_PUSH      = 0xC0,  // register handler at offset
    EH_POP       = 0xC1,  // unregister handler
    EH_ENTRY     = 0xC2,  // handler entry marker
    TRAP         = 0xC3,  // raise trap
    TRAP_FROM_ERR= 0xC4,  // trap from error
    MAKE_ERROR   = 0xC5,  // create error value
    ERR_GET_KIND = 0xC6,  // extract trap kind
    ERR_GET_CODE = 0xC7,  // extract error code
    ERR_GET_IP   = 0xC8,  // extract fault IP
    ERR_GET_LINE = 0xC9,  // extract source line
    RESUME_SAME  = 0xCA,  // resume at fault
    RESUME_NEXT  = 0xCB,  // resume after fault
    RESUME_LABEL = 0xCC,  // resume at label

    // Debug Operations (0xD0-0xDF)
    LINE         = 0xD0,  // source line marker
    BREAKPOINT   = 0xD1,  // debug breakpoint
    WATCH_VAR    = 0xD2,  // variable watch trigger

    // String Operations (0xE0-0xEF)
    STR_RETAIN   = 0xE0,  // increment refcount
    STR_RELEASE  = 0xE1,  // decrement refcount

    // Sentinel value
    OPCODE_COUNT = 0xFF
};

/// Get string name for opcode (for debugging/disassembly)
const char* opcodeName(BCOpcode op);

/// Check if opcode is a terminator (changes control flow)
inline constexpr bool isTerminator(BCOpcode op) {
    return op == BCOpcode::JUMP ||
           op == BCOpcode::JUMP_IF_TRUE ||
           op == BCOpcode::JUMP_IF_FALSE ||
           op == BCOpcode::JUMP_LONG ||
           op == BCOpcode::SWITCH ||
           op == BCOpcode::RETURN ||
           op == BCOpcode::RETURN_VOID ||
           op == BCOpcode::TAIL_CALL ||
           op == BCOpcode::TRAP ||
           op == BCOpcode::TRAP_FROM_ERR ||
           op == BCOpcode::RESUME_SAME ||
           op == BCOpcode::RESUME_NEXT ||
           op == BCOpcode::RESUME_LABEL;
}

/// Check if opcode can trap (raise exceptions)
inline constexpr bool canTrap(BCOpcode op) {
    switch (op) {
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

/// Encode a 32-bit instruction with opcode only
inline constexpr uint32_t encodeOp(BCOpcode op) {
    return static_cast<uint32_t>(op);
}

/// Encode a 32-bit instruction with opcode and one 8-bit argument
inline constexpr uint32_t encodeOp8(BCOpcode op, uint8_t arg0) {
    return static_cast<uint32_t>(op) |
           (static_cast<uint32_t>(arg0) << 8);
}

/// Encode a 32-bit instruction with opcode and signed 8-bit argument
inline constexpr uint32_t encodeOpI8(BCOpcode op, int8_t arg0) {
    return static_cast<uint32_t>(op) |
           (static_cast<uint32_t>(static_cast<uint8_t>(arg0)) << 8);
}

/// Encode a 32-bit instruction with opcode and two 8-bit arguments
inline constexpr uint32_t encodeOp88(BCOpcode op, uint8_t arg0, uint8_t arg1) {
    return static_cast<uint32_t>(op) |
           (static_cast<uint32_t>(arg0) << 8) |
           (static_cast<uint32_t>(arg1) << 16);
}

/// Encode a 32-bit instruction with opcode and one 16-bit argument
inline constexpr uint32_t encodeOp16(BCOpcode op, uint16_t arg0) {
    return static_cast<uint32_t>(op) |
           (static_cast<uint32_t>(arg0) << 8);
}

/// Encode a 32-bit instruction with opcode and signed 16-bit argument
inline constexpr uint32_t encodeOpI16(BCOpcode op, int16_t arg0) {
    return static_cast<uint32_t>(op) |
           (static_cast<uint32_t>(static_cast<uint16_t>(arg0)) << 8);
}

/// Encode a 32-bit instruction with opcode, 8-bit arg, and 16-bit arg
inline constexpr uint32_t encodeOp8_16(BCOpcode op, uint8_t arg0, uint16_t arg1) {
    return static_cast<uint32_t>(op) |
           (static_cast<uint32_t>(arg0) << 8) |
           (static_cast<uint32_t>(arg1) << 16);
}

/// Encode a 32-bit instruction with opcode and 24-bit argument
inline constexpr uint32_t encodeOp24(BCOpcode op, uint32_t arg0) {
    return static_cast<uint32_t>(op) |
           ((arg0 & 0xFFFFFF) << 8);
}

/// Encode a 32-bit instruction with opcode and signed 24-bit argument
inline constexpr uint32_t encodeOpI24(BCOpcode op, int32_t arg0) {
    return static_cast<uint32_t>(op) |
           ((static_cast<uint32_t>(arg0) & 0xFFFFFF) << 8);
}

//==============================================================================
// Instruction Decoding Helpers
//==============================================================================

/// Extract opcode from instruction
inline constexpr BCOpcode decodeOpcode(uint32_t instr) {
    return static_cast<BCOpcode>(instr & 0xFF);
}

/// Extract first 8-bit argument
inline constexpr uint8_t decodeArg8_0(uint32_t instr) {
    return static_cast<uint8_t>((instr >> 8) & 0xFF);
}

/// Extract first 8-bit argument as signed
inline constexpr int8_t decodeArgI8_0(uint32_t instr) {
    return static_cast<int8_t>((instr >> 8) & 0xFF);
}

/// Extract second 8-bit argument
inline constexpr uint8_t decodeArg8_1(uint32_t instr) {
    return static_cast<uint8_t>((instr >> 16) & 0xFF);
}

/// Extract third 8-bit argument
inline constexpr uint8_t decodeArg8_2(uint32_t instr) {
    return static_cast<uint8_t>((instr >> 24) & 0xFF);
}

/// Extract 16-bit argument (bits 8-23)
inline constexpr uint16_t decodeArg16(uint32_t instr) {
    return static_cast<uint16_t>((instr >> 8) & 0xFFFF);
}

/// Extract 16-bit argument as signed
inline constexpr int16_t decodeArgI16(uint32_t instr) {
    return static_cast<int16_t>((instr >> 8) & 0xFFFF);
}

/// Extract second 16-bit argument (bits 16-31)
inline constexpr uint16_t decodeArg16_1(uint32_t instr) {
    return static_cast<uint16_t>((instr >> 16) & 0xFFFF);
}

/// Extract 24-bit argument (bits 8-31)
inline constexpr uint32_t decodeArg24(uint32_t instr) {
    return (instr >> 8) & 0xFFFFFF;
}

/// Extract 24-bit argument as signed (sign-extended)
inline constexpr int32_t decodeArgI24(uint32_t instr) {
    uint32_t raw = (instr >> 8) & 0xFFFFFF;
    // Sign extend from 24-bit
    if (raw & 0x800000) {
        return static_cast<int32_t>(raw | 0xFF000000);
    }
    return static_cast<int32_t>(raw);
}

//==============================================================================
// BCSlot - Runtime Value Type
//==============================================================================

/// Tagged union for runtime values on the operand stack and in locals
union BCSlot {
    int64_t i64;      // Integers, booleans
    double f64;       // Floating point
    void* ptr;        // Pointers, objects

    BCSlot() : i64(0) {}
    explicit BCSlot(int64_t v) : i64(v) {}
    explicit BCSlot(double v) : f64(v) {}
    explicit BCSlot(void* v) : ptr(v) {}

    static BCSlot fromInt(int64_t v) { return BCSlot(v); }
    static BCSlot fromFloat(double v) { return BCSlot(v); }
    static BCSlot fromPtr(void* v) { return BCSlot(v); }
    static BCSlot null() { return BCSlot(static_cast<void*>(nullptr)); }
};

static_assert(sizeof(BCSlot) == 8, "BCSlot must be 8 bytes");

} // namespace viper::bytecode

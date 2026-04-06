//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/bytecode/BytecodeVM_threaded.cpp
// Purpose: Computed-goto threaded interpreter dispatch for BytecodeVM.
// Key invariants:
//   - Only compiled on GCC/Clang (requires labels-as-values extension).
//   - Dispatch table covers all 256 opcodes; unhandled entries go to L_DEFAULT.
//   - Local copies of pc/sp/locals are synced back via SYNC_STATE before
//     any call that may observe or modify VM state.
// Ownership/Lifetime:
//   - Part of BytecodeVM; operates on member state via `this`.
// Links: BytecodeVM.hpp, BytecodeVM.cpp, Bytecode.hpp
//
//===----------------------------------------------------------------------===//

#include "bytecode/BytecodeVM.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "vm/RuntimeBridge.hpp"
#include "vm/VMContext.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace viper {
namespace bytecode {

#if defined(__GNUC__) || defined(__clang__)

void BytecodeVM::runThreaded() {
    // Dispatch table for computed goto
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-designator"
#endif
    static void *dispatchTable[256] = {
        // Stack Operations (0x00-0x0F)
        [0x00] = &&L_NOP,
        [0x01] = &&L_DUP,
        [0x02] = &&L_DUP2,
        [0x03] = &&L_POP,
        [0x04] = &&L_POP2,
        [0x05] = &&L_SWAP,
        [0x06] = &&L_ROT3,
        // 0x07-0x0F -> default

        // Local Variable Operations (0x10-0x1F)
        [0x10] = &&L_LOAD_LOCAL,
        [0x11] = &&L_STORE_LOCAL,
        [0x12] = &&L_LOAD_LOCAL_W,
        [0x13] = &&L_STORE_LOCAL_W,
        [0x14] = &&L_INC_LOCAL,
        [0x15] = &&L_DEC_LOCAL,
        // 0x16-0x1F -> default

        // Constant Loading (0x20-0x2F)
        [0x20] = &&L_LOAD_I8,
        [0x21] = &&L_LOAD_I16,
        [0x22] = &&L_LOAD_I32,
        [0x23] = &&L_LOAD_I64,
        [0x24] = &&L_LOAD_F64,
        [0x25] = &&L_LOAD_STR,
        [0x26] = &&L_LOAD_NULL,
        [0x27] = &&L_LOAD_ZERO,
        [0x28] = &&L_LOAD_ONE,
        [0x29] = &&L_LOAD_GLOBAL,
        [0x2A] = &&L_STORE_GLOBAL,

        // Integer Arithmetic (0x30-0x3F)
        [0x30] = &&L_ADD_I64,
        [0x31] = &&L_SUB_I64,
        [0x32] = &&L_MUL_I64,
        [0x33] = &&L_SDIV_I64,
        [0x34] = &&L_UDIV_I64,
        [0x35] = &&L_SREM_I64,
        [0x36] = &&L_UREM_I64,
        [0x37] = &&L_NEG_I64,
        [0x38] = &&L_ADD_I64_OVF,
        [0x39] = &&L_SUB_I64_OVF,
        [0x3A] = &&L_MUL_I64_OVF,
        [0x3B] = &&L_SDIV_I64_CHK,
        [0x3C] = &&L_UDIV_I64_CHK,
        [0x3D] = &&L_SREM_I64_CHK,
        [0x3E] = &&L_UREM_I64_CHK,
        [0x3F] = &&L_IDX_CHK,

        // Float Arithmetic (0x50-0x5F)
        [0x50] = &&L_ADD_F64,
        [0x51] = &&L_SUB_F64,
        [0x52] = &&L_MUL_F64,
        [0x53] = &&L_DIV_F64,
        [0x54] = &&L_NEG_F64,

        // Bitwise Operations (0x60-0x6F)
        [0x60] = &&L_AND_I64,
        [0x61] = &&L_OR_I64,
        [0x62] = &&L_XOR_I64,
        [0x63] = &&L_NOT_I64,
        [0x64] = &&L_SHL_I64,
        [0x65] = &&L_LSHR_I64,
        [0x66] = &&L_ASHR_I64,

        // Integer Comparisons (0x70-0x7F)
        [0x70] = &&L_CMP_EQ_I64,
        [0x71] = &&L_CMP_NE_I64,
        [0x72] = &&L_CMP_SLT_I64,
        [0x73] = &&L_CMP_SLE_I64,
        [0x74] = &&L_CMP_SGT_I64,
        [0x75] = &&L_CMP_SGE_I64,
        [0x76] = &&L_CMP_ULT_I64,
        [0x77] = &&L_CMP_ULE_I64,
        [0x78] = &&L_CMP_UGT_I64,
        [0x79] = &&L_CMP_UGE_I64,

        // Float Comparisons (0x80-0x8F)
        [0x80] = &&L_CMP_EQ_F64,
        [0x81] = &&L_CMP_NE_F64,
        [0x82] = &&L_CMP_LT_F64,
        [0x83] = &&L_CMP_LE_F64,
        [0x84] = &&L_CMP_GT_F64,
        [0x85] = &&L_CMP_GE_F64,

        // Type Conversions (0x90-0x9F)
        [0x90] = &&L_I64_TO_F64,
        [0x91] = &&L_U64_TO_F64,
        [0x92] = &&L_F64_TO_I64,
        [0x93] = &&L_F64_TO_I64_CHK,
        [0x94] = &&L_F64_TO_U64_CHK,
        [0x95] = &&L_I64_NARROW_CHK,
        [0x96] = &&L_U64_NARROW_CHK,
        [0x97] = &&L_BOOL_TO_I64,
        [0x98] = &&L_I64_TO_BOOL,

        // Memory Operations (0xA0-0xAF)
        [0xA0] = &&L_ALLOCA,
        [0xA1] = &&L_GEP,
        [0xA2] = &&L_LOAD_I8_MEM,
        [0xA3] = &&L_LOAD_I16_MEM,
        [0xA4] = &&L_LOAD_I32_MEM,
        [0xA5] = &&L_LOAD_I64_MEM,
        [0xA6] = &&L_LOAD_F64_MEM,
        [0xA7] = &&L_LOAD_PTR_MEM,
        [0xA8] = &&L_LOAD_STR_MEM,
        [0xA9] = &&L_STORE_I8_MEM,
        [0xAA] = &&L_STORE_I16_MEM,
        [0xAB] = &&L_STORE_I32_MEM,
        [0xAC] = &&L_STORE_I64_MEM,
        [0xAD] = &&L_STORE_F64_MEM,
        [0xAE] = &&L_STORE_PTR_MEM,
        [0xAF] = &&L_STORE_STR_MEM,

        // Control Flow (0xB0-0xBF)
        [0xB0] = &&L_JUMP,
        [0xB1] = &&L_JUMP_IF_TRUE,
        [0xB2] = &&L_JUMP_IF_FALSE,
        [0xB3] = &&L_JUMP_LONG,
        [0xB4] = &&L_SWITCH,
        [0xB5] = &&L_CALL,
        [0xB6] = &&L_CALL_NATIVE,
        [0xB7] = &&L_CALL_INDIRECT,
        [0xB8] = &&L_RETURN,
        [0xB9] = &&L_RETURN_VOID,
        [0xBA] = &&L_DEFAULT, // TAIL_CALL

        // Exception Handling (0xC0-0xCF)
        [0xC0] = &&L_EH_PUSH,
        [0xC1] = &&L_EH_POP,
        [0xC2] = &&L_EH_ENTRY,
        [0xC3] = &&L_TRAP,
        [0xC4] = &&L_TRAP_FROM_ERR,
        [0xC6] = &&L_ERR_GET_KIND,
        [0xC7] = &&L_ERR_GET_CODE,
        [0xC8] = &&L_ERR_GET_IP,
        [0xC9] = &&L_ERR_GET_LINE,
        [0xCA] = &&L_RESUME_SAME,
        [0xCB] = &&L_RESUME_NEXT,
        [0xCC] = &&L_RESUME_LABEL,
        [0xCD] = &&L_TRAP_KIND,
    };
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    // Fill uninitialized entries with default handler (once only)
    static bool tableInitialized = false;
    if (!tableInitialized) {
        for (int i = 0; i < 256; i++) {
            if (dispatchTable[i] == nullptr) {
                dispatchTable[i] = &&L_DEFAULT;
            }
        }
        tableInitialized = true;
    }

    state_ = VMState::Running;

    // Local copies for performance
    const uint32_t *code = fp_->func->code.data();
    uint32_t pc = fp_->pc;
    BCSlot *sp = sp_;
    BCSlot *locals = fp_->locals;

    uint32_t instr;

#define DISPATCH()                                                                                 \
    do {                                                                                           \
        instr = code[pc++];                                                                        \
        ++instrCount_;                                                                             \
        goto *dispatchTable[instr & 0xFF];                                                         \
    } while (0)

#define SYNC_STATE()                                                                               \
    do {                                                                                           \
        fp_->pc = pc;                                                                              \
        sp_ = sp;                                                                                  \
    } while (0)

#define RELOAD_STATE()                                                                             \
    do {                                                                                           \
        code = fp_->func->code.data();                                                             \
        pc = fp_->pc;                                                                              \
        sp = sp_;                                                                                  \
        locals = fp_->locals;                                                                      \
    } while (0)

    DISPATCH();

    // Stack Operations
L_NOP:
    DISPATCH();

L_DUP:
    *sp = *(sp - 1);
    setSlotOwnsString(sp, false);
    if (slotOwnsString(sp - 1) && sp->ptr) {
        rt_str_retain_maybe(static_cast<rt_string>(sp->ptr));
        setSlotOwnsString(sp, true);
    }
    sp++;
    DISPATCH();

L_DUP2:
    sp[0] = sp[-2];
    sp[1] = sp[-1];
    setSlotOwnsString(sp, false);
    setSlotOwnsString(sp + 1, false);
    if (slotOwnsString(sp - 2) && sp[0].ptr) {
        rt_str_retain_maybe(static_cast<rt_string>(sp[0].ptr));
        setSlotOwnsString(sp, true);
    }
    if (slotOwnsString(sp - 1) && sp[1].ptr) {
        rt_str_retain_maybe(static_cast<rt_string>(sp[1].ptr));
        setSlotOwnsString(sp + 1, true);
    }
    sp += 2;
    DISPATCH();

L_POP:
    releaseOwnedString(sp - 1);
    sp--;
    DISPATCH();

L_POP2:
    releaseOwnedString(sp - 1);
    releaseOwnedString(sp - 2);
    sp -= 2;
    DISPATCH();

L_SWAP: {
    BCSlot tmp = sp[-1];
    const bool tmpOwns = slotOwnsString(sp - 1);
    sp[-1] = sp[-2];
    sp[-2] = tmp;
    const bool lowerOwns = slotOwnsString(sp - 2);
    setSlotOwnsString(sp - 1, lowerOwns);
    setSlotOwnsString(sp - 2, tmpOwns);
    DISPATCH();
}

L_ROT3: {
    BCSlot tmp = sp[-1];
    const bool tmpOwns = slotOwnsString(sp - 1);
    sp[-1] = sp[-2];
    sp[-2] = sp[-3];
    sp[-3] = tmp;
    const bool secondOwns = slotOwnsString(sp - 2);
    const bool firstOwns = slotOwnsString(sp - 3);
    setSlotOwnsString(sp - 1, secondOwns);
    setSlotOwnsString(sp - 2, firstOwns);
    setSlotOwnsString(sp - 3, tmpOwns);
    DISPATCH();
}

    // Local Variable Operations
L_LOAD_LOCAL: {
    uint8_t slot = decodeArg8_0(instr);
    *sp = locals[slot];
    if (localIsString(*fp_, slot) && sp->ptr) {
        if (!validateStringHandle(sp->ptr, "BytecodeVM::pushLocal(threaded)")) {
            SYNC_STATE();
            return;
        }
        rt_str_retain_maybe(static_cast<rt_string>(sp->ptr));
        setSlotOwnsString(sp, true);
    } else {
        setSlotOwnsString(sp, false);
    }
    sp++;
    DISPATCH();
}

L_STORE_LOCAL: {
    uint8_t slot = decodeArg8_0(instr);
    --sp;
    BCSlot *src = sp;
    BCSlot *dst = locals + slot;
    const bool srcOwns = slotOwnsString(src);
    const BCSlot value = *src;
    if (localIsString(*fp_, slot)) {
        releaseOwnedString(dst);
        *dst = value;
        if (value.ptr) {
            if (!validateStringHandle(value.ptr, "BytecodeVM::storeLocal(threaded)")) {
                setSlotOwnsString(src, false);
                setSlotOwnsString(dst, false);
                SYNC_STATE();
                return;
            }
            if (!srcOwns)
                rt_str_retain_maybe(static_cast<rt_string>(value.ptr));
            setSlotOwnsString(dst, true);
        } else {
            setSlotOwnsString(dst, false);
        }
    } else {
        *dst = value;
        setSlotOwnsString(dst, false);
    }
    setSlotOwnsString(src, false);
    DISPATCH();
}

L_LOAD_LOCAL_W:
    *sp = locals[decodeArg16(instr)];
    if (localIsString(*fp_, decodeArg16(instr)) && sp->ptr) {
        if (!validateStringHandle(sp->ptr, "BytecodeVM::pushLocalW(threaded)")) {
            SYNC_STATE();
            return;
        }
        rt_str_retain_maybe(static_cast<rt_string>(sp->ptr));
        setSlotOwnsString(sp, true);
    } else {
        setSlotOwnsString(sp, false);
    }
    sp++;
    DISPATCH();

L_STORE_LOCAL_W:
    --sp;
    {
        uint16_t slot = decodeArg16(instr);
        BCSlot *src = sp;
        BCSlot *dst = locals + slot;
        const bool srcOwns = slotOwnsString(src);
        const BCSlot value = *src;
        if (localIsString(*fp_, slot)) {
            releaseOwnedString(dst);
            *dst = value;
            if (value.ptr) {
                if (!validateStringHandle(value.ptr, "BytecodeVM::storeLocalW(threaded)")) {
                    setSlotOwnsString(src, false);
                    setSlotOwnsString(dst, false);
                    SYNC_STATE();
                    return;
                }
                if (!srcOwns)
                    rt_str_retain_maybe(static_cast<rt_string>(value.ptr));
                setSlotOwnsString(dst, true);
            } else {
                setSlotOwnsString(dst, false);
            }
        } else {
            *dst = value;
            setSlotOwnsString(dst, false);
        }
        setSlotOwnsString(src, false);
    }
    DISPATCH();

L_INC_LOCAL:
    locals[decodeArg8_0(instr)].i64++;
    DISPATCH();

L_DEC_LOCAL:
    locals[decodeArg8_0(instr)].i64--;
    DISPATCH();

    // Constant Loading
L_LOAD_I8:
    sp->i64 = decodeArgI8_0(instr);
    setSlotOwnsString(sp, false);
    sp++;
    DISPATCH();

L_LOAD_I16:
    sp->i64 = decodeArgI16(instr);
    setSlotOwnsString(sp, false);
    sp++;
    DISPATCH();

L_LOAD_I32: {
    // Extended format: the 32-bit value is stored in the next code word.
    int32_t val = static_cast<int32_t>(code[pc++]);
    sp->i64 = val;
    setSlotOwnsString(sp, false);
    sp++;
    DISPATCH();
}

L_LOAD_I64:
    sp->i64 = module_->i64Pool[decodeArg16(instr)];
    setSlotOwnsString(sp, false);
    sp++;
    DISPATCH();

L_LOAD_F64:
    sp->f64 = module_->f64Pool[decodeArg16(instr)];
    setSlotOwnsString(sp, false);
    sp++;
    DISPATCH();

L_LOAD_STR: {
    uint16_t idx = decodeArg16(instr);
    // Use the cached rt_string object (not raw C string!)
    // The runtime expects rt_string (pointer to rt_string_impl struct)
    sp->ptr = (idx < stringCache_.size()) ? stringCache_[idx] : nullptr;
    if (sp->ptr) {
        if (!validateStringHandle(sp->ptr, "BytecodeVM::LOAD_STR(threaded)")) {
            SYNC_STATE();
            return;
        }
        rt_str_retain_maybe(static_cast<rt_string>(sp->ptr));
        setSlotOwnsString(sp, true);
    } else {
        setSlotOwnsString(sp, false);
    }
    sp++;
    DISPATCH();
}

L_LOAD_NULL:
    sp->ptr = nullptr;
    setSlotOwnsString(sp, false);
    sp++;
    DISPATCH();

L_LOAD_ZERO:
    sp->i64 = 0;
    setSlotOwnsString(sp, false);
    sp++;
    DISPATCH();

L_LOAD_ONE:
    sp->i64 = 1;
    setSlotOwnsString(sp, false);
    sp++;
    DISPATCH();

L_LOAD_GLOBAL: {
    uint16_t gIdx = decodeArg16(instr);
    if (gIdx < globals_.size()) {
        *sp = globals_[gIdx];
        setSlotOwnsString(sp, false);
    } else {
        sp->i64 = 0;
        setSlotOwnsString(sp, false);
    }
    sp++;
    DISPATCH();
}

L_STORE_GLOBAL: {
    sp--;
    uint16_t gIdx = decodeArg16(instr);
    if (gIdx < globals_.size()) {
        globals_[gIdx] = *sp;
    }
    setSlotOwnsString(sp, false);
    DISPATCH();
}

    // Integer Arithmetic
L_ADD_I64:
    sp[-2].i64 += sp[-1].i64;
    sp--;
    DISPATCH();

L_SUB_I64:
    sp[-2].i64 -= sp[-1].i64;
    sp--;
    DISPATCH();

L_MUL_I64:
    sp[-2].i64 *= sp[-1].i64;
    sp--;
    DISPATCH();

L_SDIV_I64:
    sp[-2].i64 /= sp[-1].i64;
    sp--;
    DISPATCH();

L_UDIV_I64:
    sp[-2].i64 =
        static_cast<int64_t>(static_cast<uint64_t>(sp[-2].i64) / static_cast<uint64_t>(sp[-1].i64));
    sp--;
    DISPATCH();

L_SREM_I64:
    sp[-2].i64 %= sp[-1].i64;
    sp--;
    DISPATCH();

L_UREM_I64:
    sp[-2].i64 =
        static_cast<int64_t>(static_cast<uint64_t>(sp[-2].i64) % static_cast<uint64_t>(sp[-1].i64));
    sp--;
    DISPATCH();

L_NEG_I64:
    sp[-1].i64 = -sp[-1].i64;
    DISPATCH();

L_ADD_I64_OVF: {
    // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
    uint8_t targetType = decodeArg8_0(instr);
    int64_t a = sp[-2].i64, b = sp[-1].i64;
    int64_t result = a + b;
    bool overflow = false;
    switch (targetType) {
        case 1: // I16
            overflow = (result < INT16_MIN || result > INT16_MAX);
            break;
        case 2: // I32
            overflow = (result < INT32_MIN || result > INT32_MAX);
            break;
        default: // I64
            overflow = addOverflow(a, b, result);
            break;
    }
    if (overflow) {
        SYNC_STATE();
        trap(TrapKind::Overflow, "Overflow: integer overflow in add");
        return;
    }
    sp[-2].i64 = result;
    sp--;
    DISPATCH();
}

L_SUB_I64_OVF: {
    // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
    uint8_t targetType = decodeArg8_0(instr);
    int64_t a = sp[-2].i64, b = sp[-1].i64;
    int64_t result = a - b;
    bool overflow = false;
    switch (targetType) {
        case 1: // I16
            overflow = (result < INT16_MIN || result > INT16_MAX);
            break;
        case 2: // I32
            overflow = (result < INT32_MIN || result > INT32_MAX);
            break;
        default: // I64
            overflow = subOverflow(a, b, result);
            break;
    }
    if (overflow) {
        SYNC_STATE();
        trap(TrapKind::Overflow, "Overflow: integer overflow in sub");
        return;
    }
    sp[-2].i64 = result;
    sp--;
    DISPATCH();
}

L_MUL_I64_OVF: {
    // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
    uint8_t targetType = decodeArg8_0(instr);
    int64_t a = sp[-2].i64, b = sp[-1].i64;
    int64_t result = a * b;
    bool overflow = false;
    switch (targetType) {
        case 1: // I16
            overflow = (result < INT16_MIN || result > INT16_MAX);
            break;
        case 2: // I32
            overflow = (result < INT32_MIN || result > INT32_MAX);
            break;
        default: // I64
            overflow = mulOverflow(a, b, result);
            break;
    }
    if (overflow) {
        SYNC_STATE();
        trap(TrapKind::Overflow, "Overflow: integer overflow in mul");
        return;
    }
    sp[-2].i64 = result;
    sp--;
    DISPATCH();
}

L_SDIV_I64_CHK:
    if (sp[-1].i64 == 0) {
        SYNC_STATE();
        sp_ = sp;
        if (!dispatchTrap(TrapKind::DivideByZero)) {
            trap(TrapKind::DivideByZero, "division by zero");
            return;
        }
        RELOAD_STATE();
        DISPATCH();
    }
    sp[-2].i64 /= sp[-1].i64;
    sp--;
    DISPATCH();

L_UDIV_I64_CHK:
    if (sp[-1].i64 == 0) {
        SYNC_STATE();
        sp_ = sp;
        if (!dispatchTrap(TrapKind::DivideByZero)) {
            trap(TrapKind::DivideByZero, "division by zero");
            return;
        }
        RELOAD_STATE();
        DISPATCH();
    }
    sp[-2].i64 =
        static_cast<int64_t>(static_cast<uint64_t>(sp[-2].i64) / static_cast<uint64_t>(sp[-1].i64));
    sp--;
    DISPATCH();

L_SREM_I64_CHK:
    if (sp[-1].i64 == 0) {
        SYNC_STATE();
        sp_ = sp;
        if (!dispatchTrap(TrapKind::DivideByZero)) {
            trap(TrapKind::DivideByZero, "division by zero");
            return;
        }
        RELOAD_STATE();
        DISPATCH();
    }
    sp[-2].i64 %= sp[-1].i64;
    sp--;
    DISPATCH();

L_UREM_I64_CHK:
    if (sp[-1].i64 == 0) {
        SYNC_STATE();
        sp_ = sp;
        if (!dispatchTrap(TrapKind::DivideByZero)) {
            trap(TrapKind::DivideByZero, "division by zero");
            return;
        }
        RELOAD_STATE();
        DISPATCH();
    }
    sp[-2].i64 =
        static_cast<int64_t>(static_cast<uint64_t>(sp[-2].i64) % static_cast<uint64_t>(sp[-1].i64));
    sp--;
    DISPATCH();

L_IDX_CHK: {
    int64_t hi = sp[-1].i64;
    int64_t lo = sp[-2].i64;
    int64_t idx = sp[-3].i64;
    if (idx < lo || idx >= hi) {
        SYNC_STATE();
        trap(TrapKind::Bounds, "index out of bounds");
        return;
    }
    sp -= 2;
    DISPATCH();
}

    // Float Arithmetic
L_ADD_F64:
    sp[-2].f64 += sp[-1].f64;
    sp--;
    DISPATCH();

L_SUB_F64:
    sp[-2].f64 -= sp[-1].f64;
    sp--;
    DISPATCH();

L_MUL_F64:
    sp[-2].f64 *= sp[-1].f64;
    sp--;
    DISPATCH();

L_DIV_F64:
    sp[-2].f64 /= sp[-1].f64;
    sp--;
    DISPATCH();

L_NEG_F64:
    sp[-1].f64 = -sp[-1].f64;
    DISPATCH();

    // Bitwise Operations
L_AND_I64:
    sp[-2].i64 &= sp[-1].i64;
    sp--;
    DISPATCH();

L_OR_I64:
    sp[-2].i64 |= sp[-1].i64;
    sp--;
    DISPATCH();

L_XOR_I64:
    sp[-2].i64 ^= sp[-1].i64;
    sp--;
    DISPATCH();

L_NOT_I64:
    sp[-1].i64 = ~sp[-1].i64;
    DISPATCH();

L_SHL_I64:
    sp[-2].i64 <<= (sp[-1].i64 & 63);
    sp--;
    DISPATCH();

L_LSHR_I64:
    sp[-2].i64 = static_cast<int64_t>(static_cast<uint64_t>(sp[-2].i64) >> (sp[-1].i64 & 63));
    sp--;
    DISPATCH();

L_ASHR_I64:
    sp[-2].i64 >>= (sp[-1].i64 & 63);
    sp--;
    DISPATCH();

    // Integer Comparisons
L_CMP_EQ_I64:
    sp[-2].i64 = (sp[-2].i64 == sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_NE_I64:
    sp[-2].i64 = (sp[-2].i64 != sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_SLT_I64:
    sp[-2].i64 = (sp[-2].i64 < sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_SLE_I64:
    sp[-2].i64 = (sp[-2].i64 <= sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_SGT_I64:
    sp[-2].i64 = (sp[-2].i64 > sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_SGE_I64:
    sp[-2].i64 = (sp[-2].i64 >= sp[-1].i64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_ULT_I64:
    sp[-2].i64 = (static_cast<uint64_t>(sp[-2].i64) < static_cast<uint64_t>(sp[-1].i64)) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_ULE_I64:
    sp[-2].i64 = (static_cast<uint64_t>(sp[-2].i64) <= static_cast<uint64_t>(sp[-1].i64)) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_UGT_I64:
    sp[-2].i64 = (static_cast<uint64_t>(sp[-2].i64) > static_cast<uint64_t>(sp[-1].i64)) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_UGE_I64:
    sp[-2].i64 = (static_cast<uint64_t>(sp[-2].i64) >= static_cast<uint64_t>(sp[-1].i64)) ? 1 : 0;
    sp--;
    DISPATCH();

    // Float Comparisons
L_CMP_EQ_F64:
    sp[-2].i64 = (sp[-2].f64 == sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_NE_F64:
    sp[-2].i64 = (sp[-2].f64 != sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_LT_F64:
    sp[-2].i64 = (sp[-2].f64 < sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_LE_F64:
    sp[-2].i64 = (sp[-2].f64 <= sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_GT_F64:
    sp[-2].i64 = (sp[-2].f64 > sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

L_CMP_GE_F64:
    sp[-2].i64 = (sp[-2].f64 >= sp[-1].f64) ? 1 : 0;
    sp--;
    DISPATCH();

    // Type Conversions
L_I64_TO_F64:
    sp[-1].f64 = static_cast<double>(sp[-1].i64);
    DISPATCH();

L_U64_TO_F64:
    sp[-1].f64 = static_cast<double>(static_cast<uint64_t>(sp[-1].i64));
    DISPATCH();

L_F64_TO_I64:
    sp[-1].i64 = static_cast<int64_t>(sp[-1].f64);
    DISPATCH();

L_BOOL_TO_I64:
    DISPATCH(); // No-op, already i64

L_I64_TO_BOOL:
    sp[-1].i64 = (sp[-1].i64 != 0) ? 1 : 0;
    DISPATCH();

L_I64_NARROW_CHK: {
    // Signed narrow conversion with overflow check
    // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
    uint8_t targetType = decodeArg8_0(instr);
    int64_t val = sp[-1].i64;
    bool inRange = true;
    switch (targetType) {
        case 0: // I1 (boolean)
            inRange = (val == 0 || val == 1);
            break;
        case 1: // I16
            inRange = (val >= INT16_MIN && val <= INT16_MAX);
            break;
        case 2: // I32
            inRange = (val >= INT32_MIN && val <= INT32_MAX);
            break;
        default: // I64 - always in range
            break;
    }
    if (!inRange) {
        SYNC_STATE();
        trap(TrapKind::Overflow, "Overflow: signed narrow conversion overflow");
        return;
    }
    // Value stays the same (already narrowed semantically)
    DISPATCH();
}

L_U64_NARROW_CHK: {
    // Unsigned narrow conversion with overflow check
    // Target type encoded in arg: 0=I1, 1=I16, 2=I32, 3=I64
    uint8_t targetType = decodeArg8_0(instr);
    uint64_t val = static_cast<uint64_t>(sp[-1].i64);
    bool inRange = true;
    switch (targetType) {
        case 0: // I1 (boolean)
            inRange = (val <= 1);
            break;
        case 1: // I16
            inRange = (val <= UINT16_MAX);
            break;
        case 2: // I32
            inRange = (val <= UINT32_MAX);
            break;
        default: // I64 - always in range
            break;
    }
    if (!inRange) {
        SYNC_STATE();
        trap(TrapKind::Overflow, "Overflow: unsigned narrow conversion overflow");
        return;
    }
    // Value stays the same (already narrowed semantically)
    DISPATCH();
}

L_F64_TO_I64_CHK: {
    // Float to signed int64 with overflow check and round-to-even
    double val = sp[-1].f64;
    // Check for NaN
    if (val != val) {
        SYNC_STATE();
        trap(TrapKind::InvalidCast, "InvalidCast: float to int conversion of NaN");
        return;
    }
    // Round to nearest, ties to even (banker's rounding)
    double rounded = std::rint(val);
    // Check for out of range (INT64_MIN to INT64_MAX)
    // Note: comparing doubles, so we use slightly wider bounds
    constexpr double maxI64 = 9223372036854775807.0;  // INT64_MAX as double
    constexpr double minI64 = -9223372036854775808.0; // INT64_MIN as double
    if (rounded > maxI64 || rounded < minI64) {
        SYNC_STATE();
        trap(TrapKind::InvalidCast, "InvalidCast: float to int conversion overflow");
        return;
    }
    sp[-1].i64 = static_cast<int64_t>(rounded);
    DISPATCH();
}

L_F64_TO_U64_CHK: {
    // Float to unsigned int64 with overflow check and round-to-even
    double val = sp[-1].f64;
    // Check for NaN
    if (val != val) {
        SYNC_STATE();
        trap(TrapKind::InvalidCast, "InvalidCast: float to uint conversion of NaN");
        return;
    }
    // Round to nearest, ties to even (banker's rounding)
    double rounded = std::rint(val);
    // Check for out of range (0 to UINT64_MAX)
    constexpr double maxU64 = 18446744073709551615.0; // UINT64_MAX as double
    if (rounded < 0.0 || rounded > maxU64) {
        SYNC_STATE();
        trap(TrapKind::InvalidCast, "InvalidCast: float to uint conversion overflow");
        return;
    }
    sp[-1].i64 = static_cast<int64_t>(static_cast<uint64_t>(rounded));
    DISPATCH();
}

    // Memory Operations
L_ALLOCA: {
    // Allocate from the separate alloca buffer (not operand stack)
    int64_t size = (--sp)->i64;
    // Align to 8 bytes
    size = (size + 7) & ~7;

    // Check for alloca overflow
    if (allocaTop_ + static_cast<size_t>(size) > allocaBuffer_.size()) {
        size_t newSize = allocaBuffer_.size() * 2;
        if (newSize > 16 * 1024 * 1024 || allocaTop_ + static_cast<size_t>(size) > newSize) {
            trap(TrapKind::StackOverflow, "alloca stack overflow");
            return;
        }
        allocaBuffer_.resize(newSize);
    }

    void *ptr = allocaBuffer_.data() + allocaTop_;
    std::memset(ptr, 0, static_cast<size_t>(size));
    allocaTop_ += static_cast<size_t>(size);
    sp->ptr = ptr;
    setSlotOwnsString(sp, false);
    sp++;
    DISPATCH();
}

L_GEP: {
    int64_t offset = (--sp)->i64;
    uint8_t *ptr = static_cast<uint8_t *>(sp[-1].ptr);
    // Null pointer check for debugging field access
    if (ptr == nullptr && offset != 0) {
        SYNC_STATE();
        fprintf(stderr, "*** NULL POINTER DEREFERENCE ***\n");
        fprintf(stderr,
                "Attempting to access field at offset %lld on null object\n",
                (long long)offset);
        fprintf(stderr,
                "PC: %u, Function: %s\n",
                pc - 1,
                fp_->func->name.empty() ? "<unknown>" : fp_->func->name.c_str());
        // Print call stack
        fprintf(stderr, "Call stack:\n");
        for (size_t i = 0; i < callStack_.size(); i++) {
            const auto &frame = callStack_[i];
            fprintf(stderr,
                    "  [%zu] %s (PC: %u)\n",
                    i,
                    frame.func->name.empty() ? "<unknown>" : frame.func->name.c_str(),
                    frame.pc);
        }
        fflush(stderr);
        trapKind_ = TrapKind::NullPointer;
        state_ = VMState::Trapped;
        return;
    }
    sp[-1].ptr = ptr + offset;
    DISPATCH();
}

L_LOAD_I8_MEM: {
    int8_t val;
    std::memcpy(&val, sp[-1].ptr, sizeof(val));
    sp[-1].i64 = val;
    DISPATCH();
}

L_LOAD_I16_MEM: {
    int16_t val;
    std::memcpy(&val, sp[-1].ptr, sizeof(val));
    sp[-1].i64 = val;
    DISPATCH();
}

L_LOAD_I32_MEM: {
    int32_t val;
    std::memcpy(&val, sp[-1].ptr, sizeof(val));
    sp[-1].i64 = val;
    DISPATCH();
}

L_LOAD_I64_MEM: {
    void *addr = sp[-1].ptr;
    // Null/invalid pointer check for debugging
    if (addr == nullptr || reinterpret_cast<uintptr_t>(addr) < 4096) {
        SYNC_STATE();
        fprintf(stderr, "*** NULL POINTER READ (i64) ***\n");
        fprintf(stderr, "Attempting to read from address %p\n", addr);
        fprintf(stderr,
                "PC: %u, Function: %s\n",
                pc - 1,
                fp_->func->name.empty() ? "<unknown>" : fp_->func->name.c_str());
        // Print call stack
        fprintf(stderr, "Call stack:\n");
        for (size_t i = 0; i < callStack_.size(); i++) {
            const auto &frame = callStack_[i];
            fprintf(stderr,
                    "  [%zu] %s (PC: %u)\n",
                    i,
                    frame.func->name.empty() ? "<unknown>" : frame.func->name.c_str(),
                    frame.pc);
        }
        trapKind_ = TrapKind::NullPointer;
        state_ = VMState::Trapped;
        return;
    }
    int64_t val;
    std::memcpy(&val, addr, sizeof(val));
    sp[-1].i64 = val;
    DISPATCH();
}

L_LOAD_F64_MEM: {
    double val;
    std::memcpy(&val, sp[-1].ptr, sizeof(val));
    sp[-1].f64 = val;
    DISPATCH();
}

L_LOAD_PTR_MEM: {
    void *addr = sp[-1].ptr;
    // Null/invalid pointer check for debugging
    if (addr == nullptr || reinterpret_cast<uintptr_t>(addr) < 4096) {
        SYNC_STATE();
        fprintf(stderr, "*** NULL POINTER READ (ptr) ***\n");
        fprintf(stderr, "Attempting to read from address %p\n", addr);
        fprintf(stderr,
                "PC: %u, Function: %s\n",
                pc - 1,
                fp_->func->name.empty() ? "<unknown>" : fp_->func->name.c_str());
        // Print call stack
        fprintf(stderr, "Call stack:\n");
        for (size_t i = 0; i < callStack_.size(); i++) {
            const auto &frame = callStack_[i];
            fprintf(stderr,
                    "  [%zu] %s (PC: %u)\n",
                    i,
                    frame.func->name.empty() ? "<unknown>" : frame.func->name.c_str(),
                    frame.pc);
        }
        trapKind_ = TrapKind::NullPointer;
        state_ = VMState::Trapped;
        return;
    }
    void *val;
    std::memcpy(&val, addr, sizeof(val));
    sp[-1].ptr = val;
    setSlotOwnsString(sp - 1, false);
    DISPATCH();
}

L_STORE_I8_MEM: {
    int8_t val = static_cast<int8_t>((--sp)->i64);
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

L_STORE_I16_MEM: {
    int16_t val = static_cast<int16_t>((--sp)->i64);
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

L_STORE_I32_MEM: {
    int32_t val = static_cast<int32_t>((--sp)->i64);
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

L_STORE_I64_MEM: {
    int64_t val = (--sp)->i64;
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

L_STORE_F64_MEM: {
    double val = (--sp)->f64;
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    DISPATCH();
}

L_STORE_PTR_MEM: {
    void *val = (--sp)->ptr;
    void *ptr = (--sp)->ptr;
    std::memcpy(ptr, &val, sizeof(val));
    setSlotOwnsString(sp, false);
    setSlotOwnsString(sp + 1, false);
    DISPATCH();
}

L_LOAD_STR_MEM: {
    rt_string val = nullptr;
    std::memcpy(&val, sp[-1].ptr, sizeof(val));
    sp[-1].ptr = val;
    if (val) {
        if (!validateStringHandle(val, "BytecodeVM::LOAD_STR_MEM(threaded)")) {
            SYNC_STATE();
            return;
        }
        rt_str_retain_maybe(val);
        setSlotOwnsString(sp - 1, true);
    } else {
        setSlotOwnsString(sp - 1, false);
    }
    DISPATCH();
}

L_STORE_STR_MEM: {
    BCSlot *valueSlot = --sp;
    rt_string incoming = static_cast<rt_string>(valueSlot->ptr);
    const bool incomingOwns = slotOwnsString(valueSlot);
    void *ptr = (--sp)->ptr;
    rt_string current = nullptr;
    std::memcpy(&current, ptr, sizeof(current));
    if (current && !validateStringHandle(current, "BytecodeVM::STORE_STR_MEM(current, threaded)")) {
        SYNC_STATE();
        return;
    }
    rt_str_release_maybe(current);
    if (incoming && !validateStringHandle(incoming, "BytecodeVM::STORE_STR_MEM(threaded)")) {
        SYNC_STATE();
        return;
    }
    if (incoming && !incomingOwns)
        rt_str_retain_maybe(incoming);
    std::memcpy(ptr, &incoming, sizeof(incoming));
    setSlotOwnsString(valueSlot, false);
    setSlotOwnsString(sp, false);
    DISPATCH();
}

    // Control Flow
L_JUMP:
    pc += decodeArgI16(instr);
    DISPATCH();

L_JUMP_IF_TRUE:
    if ((--sp)->i64 != 0) {
        pc += decodeArgI16(instr);
    }
    DISPATCH();

L_JUMP_IF_FALSE:
    if ((--sp)->i64 == 0) {
        pc += decodeArgI16(instr);
    }
    DISPATCH();

L_JUMP_LONG:
    pc += decodeArgI24(instr);
    DISPATCH();

L_SWITCH: {
    // Format: SWITCH [numCases:u32] [defaultOffset:i32] [caseVal:i32 caseOffset:i32]...
    // Pop scrutinee from stack
    int32_t scrutinee = static_cast<int32_t>((--sp)->i64);

    // pc currently points to the word after SWITCH opcode (numCases)
    uint32_t numCases = code[pc++];

    // Position of default offset word
    uint32_t defaultOffsetPos = pc++;

    // Search for matching case
    bool found = false;
    for (uint32_t i = 0; i < numCases; ++i) {
        int32_t caseVal = static_cast<int32_t>(code[pc++]);
        uint32_t caseOffsetPos = pc++;

        if (caseVal == scrutinee) {
            // Found matching case - jump to its target
            // Offset is relative to the offset word position (same as EH_PUSH)
            int32_t caseOffset = static_cast<int32_t>(code[caseOffsetPos]);
            pc = caseOffsetPos + caseOffset;
            found = true;
            break;
        }
    }

    if (!found) {
        // No match - use default offset
        int32_t defaultOffset = static_cast<int32_t>(code[defaultOffsetPos]);
        pc = defaultOffsetPos + defaultOffset;
    }

    DISPATCH();
}

L_CALL: {
    uint16_t funcIdx = decodeArg16(instr);
    if (funcIdx >= module_->functions.size()) {
        SYNC_STATE();
        trap(TrapKind::RuntimeError, "Invalid function index");
        return;
    }
    SYNC_STATE();
    sp_ = sp;
    fp_->pc = pc;
    call(&module_->functions[funcIdx]);
    RELOAD_STATE();
    DISPATCH();
}

L_CALL_NATIVE: {
    uint8_t argCount = decodeArg8_0(instr);
    uint16_t nativeIdx = decodeArg16_1(instr);

    if (nativeIdx >= module_->nativeFuncs.size()) {
        SYNC_STATE();
        trap(TrapKind::RuntimeError, "Invalid native function index");
        return;
    }

    const NativeFuncRef &ref = module_->nativeFuncs[nativeIdx];
    BCSlot *args = sp - argCount;
    BCSlot result{};

    if (runtimeBridgeEnabled_) {
        auto preservedArgs =
            cloneRuntimeStringArgs(ref.name, args, static_cast<size_t>(argCount));
        struct RuntimeArgGuard {
            const BytecodeVM *vm;
            std::string_view name;
            std::vector<BCSlot> &args;

            ~RuntimeArgGuard() {
                vm->releaseRuntimeStringArgs(name, args);
            }
        } argGuard{this, ref.name, preservedArgs};

        // Use RuntimeBridge for native function calls
        BCSlot *callArgs = preservedArgs.empty() ? args : preservedArgs.data();
        il::vm::Slot *vmArgs = reinterpret_cast<il::vm::Slot *>(callArgs);
        std::vector<il::vm::Slot> argVec(vmArgs, vmArgs + argCount);

        il::vm::RuntimeCallContext ctx;
        il::vm::Slot vmResult =
            il::vm::RuntimeBridge::call(ctx, ref.name, argVec, il::support::SourceLoc{}, "", "");

        result.i64 = vmResult.i64;
    } else {
        auto it = nativeHandlers_.find(ref.name);
        if (it == nativeHandlers_.end()) {
            SYNC_STATE();
            trap(TrapKind::RuntimeError, "Native function not registered");
            return;
        }
        it->second(args, argCount, &result);
    }

    dismissConsumedStringArgs(ref.name, args, argCount);
    releaseCallArgs(args, argCount);

    sp -= argCount;
    if (ref.hasReturn) {
        *sp++ = result;
        const auto *sig = il::runtime::findRuntimeSignature(ref.name);
        if (sig && sig->retType.kind == il::core::Type::Kind::Str && result.ptr) {
            setSlotOwnsString(sp - 1, true);
        } else {
            setSlotOwnsString(sp - 1, false);
        }
    }
    DISPATCH();
}

L_CALL_INDIRECT: {
    // Indirect call through function pointer
    // Stack layout: [callee][arg0][arg1]...[argN] <- sp
    uint8_t argCount = decodeArg8_0(instr);

    // Get callee from below the arguments
    BCSlot *callee = sp - argCount - 1;
    BCSlot *args = sp - argCount;

    // Check if callee is a tagged function pointer (high bit set)
    constexpr uint64_t kFuncPtrTag = 0x8000000000000000ULL;
    uint64_t calleeVal = static_cast<uint64_t>(callee->i64);

    if (calleeVal & kFuncPtrTag) {
        // Tagged function index - extract and call
        uint32_t funcIdx = static_cast<uint32_t>(calleeVal & 0x7FFFFFFFULL);
        if (funcIdx >= module_->functions.size()) {
            SYNC_STATE();
            trap(TrapKind::RuntimeError, "Invalid indirect function index");
            return;
        }

        // Shift arguments down to overwrite the callee slot
        // This is needed because call() expects args at the top of stack
        for (int i = 0; i < argCount; ++i) {
            callee[i] = args[i];
            setSlotOwnsString(callee + i, slotOwnsString(args + i));
            if (callee + i != args + i)
                setSlotOwnsString(args + i, false);
        }
        sp = callee + argCount; // Adjust stack pointer

        SYNC_STATE();
        sp_ = sp;
        fp_->pc = pc;
        call(&module_->functions[funcIdx]);
        RELOAD_STATE();
    } else if (calleeVal == 0) {
        // Null function pointer
        SYNC_STATE();
        trap(TrapKind::NullPointer, "Null indirect callee");
        return;
    } else {
        // Unknown pointer format - try runtime bridge
        // (This handles cases where the function pointer came from runtime)
        SYNC_STATE();
        trap(TrapKind::RuntimeError, "Invalid indirect call target");
        return;
    }
    DISPATCH();
}

L_RETURN: {
    BCSlot *resultSlot = --sp;
    BCSlot result = *resultSlot;
    const bool resultOwnsString = slotOwnsString(resultSlot);
    setSlotOwnsString(resultSlot, false);
    SYNC_STATE();
    sp_ = sp;
    if (!popFrame()) {
        *sp_++ = result;
        setSlotOwnsString(sp_ - 1, resultOwnsString);
        state_ = VMState::Halted;
        return;
    }
    RELOAD_STATE();
    *sp++ = result;
    setSlotOwnsString(sp - 1, resultOwnsString);
    DISPATCH();
}

L_RETURN_VOID: {
    SYNC_STATE();
    sp_ = sp;
    if (!popFrame()) {
        state_ = VMState::Halted;
        return;
    }
    RELOAD_STATE();
    DISPATCH();
}

    //==========================================================================
    // Exception Handling
    //==========================================================================

L_EH_PUSH: {
    // Handler offset is in the next code word (raw i32 offset)
    int32_t offset = static_cast<int32_t>(code[pc++]);
    uint32_t handlerPc = static_cast<uint32_t>(static_cast<int32_t>(pc - 1) + offset);
    SYNC_STATE();
    sp_ = sp;
    pushExceptionHandler(handlerPc);
    DISPATCH();
}

L_EH_POP: {
    SYNC_STATE();
    popExceptionHandler();
    DISPATCH();
}

L_EH_ENTRY: {
    // Handler entry marker - no-op
    // The error and resume token are already on the stack from dispatchTrap
    DISPATCH();
}

L_ERR_GET_KIND: {
    // Get trap kind from error object on stack (or use current trap kind)
    // If there's an operand, it's on the stack; otherwise use current
    // For simplicity, always return the current trap kind
    sp->i64 = static_cast<int64_t>(trapKind_);
    sp++;
    DISPATCH();
}

L_ERR_GET_CODE: {
    // Get error code - maps trap kind to BASIC error code
    sp->i64 = static_cast<int64_t>(currentErrorCode_);
    sp++;
    DISPATCH();
}

L_ERR_GET_IP: {
    // Get fault instruction pointer - return current PC
    sp->i64 = static_cast<int64_t>(fp_->pc);
    sp++;
    DISPATCH();
}

L_ERR_GET_LINE: {
    // Get source line - we don't track this in bytecode VM, return -1
    sp->i64 = -1;
    sp++;
    DISPATCH();
}

L_TRAP: {
    uint8_t kind = decodeArg8_0(instr);
    TrapKind trapKind = static_cast<TrapKind>(kind);
    SYNC_STATE();
    sp_ = sp;
    if (!dispatchTrap(trapKind)) {
        // Include trap kind name in message
        const char *msg =
            (trapKind == TrapKind::Overflow) ? "Overflow: unhandled trap" : "Trap: unhandled trap";
        trap(trapKind, msg);
        return;
    }
    RELOAD_STATE();
    DISPATCH();
}

L_TRAP_FROM_ERR: {
    int64_t errCode = (--sp)->i64;
    TrapKind trapKind = static_cast<TrapKind>(errCode);
    SYNC_STATE();
    sp_ = sp;
    if (!dispatchTrap(trapKind)) {
        trap(trapKind, "Unhandled trap from error");
        return;
    }
    RELOAD_STATE();
    DISPATCH();
}

L_RESUME_SAME: {
    // Resume at fault point - for now just continue
    DISPATCH();
}

L_RESUME_NEXT: {
    // Resume after fault - for now just continue
    DISPATCH();
}

L_RESUME_LABEL: {
    // Target offset is in the next code word (raw i32 offset)
    int32_t offset = static_cast<int32_t>(code[pc++]);
    pc = static_cast<uint32_t>(static_cast<int32_t>(pc - 1) + offset);
    DISPATCH();
}

L_TRAP_KIND: {
    // Push the current trap kind as an I64 for typed-catch comparison.
    // Values 0-11 are aligned with il::vm::TrapKind (vm/Trap.hpp).
    // BC-specific kinds (100+) map to RuntimeError(9) as catch-all.
    uint8_t raw = static_cast<uint8_t>(trapKind_);
    int64_t ilKind = (raw <= 11) ? static_cast<int64_t>(raw) : 9;
    sp->i64 = ilKind;
    sp++;
    DISPATCH();
}

L_DEFAULT:
    SYNC_STATE();
    trap(TrapKind::InvalidOpcode, "Unknown opcode");
    return;

#undef DISPATCH
#undef SYNC_STATE
#undef RELOAD_STATE
}

#endif // __GNUC__ || __clang__

} // namespace bytecode
} // namespace viper

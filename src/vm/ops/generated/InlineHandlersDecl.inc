//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// IMPORTANT: This file is auto-generated. Do not edit manually.
//
// File: vm/ops/generated/InlineHandlersDecl.inc
// Purpose: Forward declarations of inline handler functions for all opcodes.
//
// Generator: Manually maintained (synchronized with Opcode enum).
// Regenerate: Update when adding/removing opcodes in il/core/Opcode.hpp.
//
// Key Invariants:
//   - One declaration per opcode in enum order
//   - Handler signature: void inline_handle_<OpName>(ExecState &st)
//
// Links: docs/il-guide.md#reference, vm/ops/generated/InlineHandlersImpl.inc
//
//===----------------------------------------------------------------------===//

void inline_handle_Add(ExecState &st);
void inline_handle_Sub(ExecState &st);
void inline_handle_Mul(ExecState &st);
void inline_handle_IAddOvf(ExecState &st);
void inline_handle_ISubOvf(ExecState &st);
void inline_handle_IMulOvf(ExecState &st);
void inline_handle_SDiv(ExecState &st);
void inline_handle_UDiv(ExecState &st);
void inline_handle_SRem(ExecState &st);
void inline_handle_URem(ExecState &st);
void inline_handle_SDivChk0(ExecState &st);
void inline_handle_UDivChk0(ExecState &st);
void inline_handle_SRemChk0(ExecState &st);
void inline_handle_URemChk0(ExecState &st);
void inline_handle_IdxChk(ExecState &st);
void inline_handle_And(ExecState &st);
void inline_handle_Or(ExecState &st);
void inline_handle_Xor(ExecState &st);
void inline_handle_Shl(ExecState &st);
void inline_handle_LShr(ExecState &st);
void inline_handle_AShr(ExecState &st);
void inline_handle_FAdd(ExecState &st);
void inline_handle_FSub(ExecState &st);
void inline_handle_FMul(ExecState &st);
void inline_handle_FDiv(ExecState &st);
void inline_handle_ICmpEq(ExecState &st);
void inline_handle_ICmpNe(ExecState &st);
void inline_handle_SCmpLT(ExecState &st);
void inline_handle_SCmpLE(ExecState &st);
void inline_handle_SCmpGT(ExecState &st);
void inline_handle_SCmpGE(ExecState &st);
void inline_handle_UCmpLT(ExecState &st);
void inline_handle_UCmpLE(ExecState &st);
void inline_handle_UCmpGT(ExecState &st);
void inline_handle_UCmpGE(ExecState &st);
void inline_handle_FCmpEQ(ExecState &st);
void inline_handle_FCmpNE(ExecState &st);
void inline_handle_FCmpLT(ExecState &st);
void inline_handle_FCmpLE(ExecState &st);
void inline_handle_FCmpGT(ExecState &st);
void inline_handle_FCmpGE(ExecState &st);
void inline_handle_FCmpOrd(ExecState &st);
void inline_handle_FCmpUno(ExecState &st);
void inline_handle_Sitofp(ExecState &st);
void inline_handle_Fptosi(ExecState &st);
void inline_handle_CastFpToSiRteChk(ExecState &st);
void inline_handle_CastFpToUiRteChk(ExecState &st);
void inline_handle_CastSiNarrowChk(ExecState &st);
void inline_handle_CastUiNarrowChk(ExecState &st);
void inline_handle_CastSiToFp(ExecState &st);
void inline_handle_CastUiToFp(ExecState &st);
void inline_handle_Zext1(ExecState &st);
void inline_handle_Trunc1(ExecState &st);
void inline_handle_Alloca(ExecState &st);
void inline_handle_GEP(ExecState &st);
void inline_handle_Load(ExecState &st);
void inline_handle_Store(ExecState &st);
void inline_handle_AddrOf(ExecState &st);
void inline_handle_ConstStr(ExecState &st);
void inline_handle_GAddr(ExecState &st);
void inline_handle_ConstNull(ExecState &st);
void inline_handle_ConstF64(ExecState &st);
void inline_handle_Call(ExecState &st);
void inline_handle_CallIndirect(ExecState &st);
void inline_handle_SwitchI32(ExecState &st);
void inline_handle_Br(ExecState &st);
void inline_handle_CBr(ExecState &st);
void inline_handle_Ret(ExecState &st);
void inline_handle_TrapKind(ExecState &st);
void inline_handle_TrapFromErr(ExecState &st);
void inline_handle_TrapErr(ExecState &st);
void inline_handle_ErrGetKind(ExecState &st);
void inline_handle_ErrGetCode(ExecState &st);
void inline_handle_ErrGetIp(ExecState &st);
void inline_handle_ErrGetLine(ExecState &st);
void inline_handle_EhPush(ExecState &st);
void inline_handle_EhPop(ExecState &st);
void inline_handle_ResumeSame(ExecState &st);
void inline_handle_ResumeNext(ExecState &st);
void inline_handle_ResumeLabel(ExecState &st);
void inline_handle_EhEntry(ExecState &st);
void inline_handle_Trap(ExecState &st);

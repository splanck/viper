//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// IMPORTANT: This file is auto-generated. Do not edit manually.
//
// File: vm/ops/generated/InlineHandlersImpl.inc
// Purpose: Implementation of inline handler functions for all VM opcodes.
//
// Generator: Manually maintained (synchronized with Opcode enum).
// Regenerate: Update when adding/removing opcodes in il/core/Opcode.hpp.
//
// Key Invariants:
//   - One handler per opcode in enum order
//   - Handler implementation: void VM::inline_handle_<OpName>(ExecState &st)
//   - Each handler delegates to detail::handle<OpName> and calls handleInlineResult
//
// Links: docs/il-guide.md#reference, vm/ops/generated/InlineHandlersDecl.inc
//
//===----------------------------------------------------------------------===//

void VM::inline_handle_Add(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleAdd(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Sub(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleSub(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Mul(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleMul(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_IAddOvf(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleIAddOvf(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ISubOvf(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleISubOvf(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_IMulOvf(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleIMulOvf(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SDiv(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleSDiv(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UDiv(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleUDiv(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SRem(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleSRem(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_URem(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleURem(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SDivChk0(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleSDivChk0(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UDivChk0(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleUDivChk0(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SRemChk0(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleSRemChk0(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_URemChk0(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleURemChk0(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_IdxChk(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleIdxChk(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_And(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleAnd(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Or(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleOr(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Xor(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleXor(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Shl(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleShl(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_LShr(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleLShr(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_AShr(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleAShr(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FAdd(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFAdd(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FSub(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFSub(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FMul(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFMul(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FDiv(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFDiv(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ICmpEq(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleICmpEq(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ICmpNe(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleICmpNe(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpLT(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleSCmpLT(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpLE(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleSCmpLE(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpGT(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleSCmpGT(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpGE(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleSCmpGE(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpLT(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleUCmpLT(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpLE(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleUCmpLE(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpGT(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleUCmpGT(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpGE(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleUCmpGE(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpEQ(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFCmpEQ(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpNE(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFCmpNE(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpLT(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFCmpLT(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpLE(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFCmpLE(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpGT(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFCmpGT(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpGE(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFCmpGE(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpOrd(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFCmpOrd(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpUno(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFCmpUno(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Sitofp(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleSitofp(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Fptosi(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleFptosi(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastFpToSiRteChk(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleCastFpToSiRteChk(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastFpToUiRteChk(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleCastFpToUiRteChk(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastSiNarrowChk(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleCastSiNarrowChk(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastUiNarrowChk(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleCastUiNarrowChk(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastSiToFp(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleCastSiToFp(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastUiToFp(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleCastUiToFp(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Zext1(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleTruncOrZext1(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Trunc1(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleTruncOrZext1(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Alloca(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleAlloca(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_GEP(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleGEP(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Load(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleLoad(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Store(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleStore(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_AddrOf(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleAddrOf(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ConstStr(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleConstStr(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_GAddr(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleGAddr(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ConstNull(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleConstNull(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ConstF64(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleConstF64(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Call(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleCall(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CallIndirect(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleCallIndirect(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SwitchI32(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleSwitchI32(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Br(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleBr(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CBr(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleCBr(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Ret(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleRet(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_TrapKind(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleTrapKind(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_TrapFromErr(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleTrap(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_TrapErr(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleTrapErr(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetKind(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleErrGet(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetCode(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleErrGet(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetIp(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleErrGet(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetLine(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleErrGet(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_EhPush(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleEhPush(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_EhPop(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleEhPop(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ResumeSame(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleResumeSame(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ResumeNext(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleResumeNext(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ResumeLabel(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec =
        il::vm::detail::handleResumeLabel(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_EhEntry(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleEhEntry(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Trap(ExecState &st)
{
    assert(st.currentInstr && "currentInstr set by dispatch loop");
    const Instr &instr = *st.currentInstr;
    ExecResult exec = il::vm::detail::handleTrap(*this, st.fr, instr, *st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

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
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Add);
    }
    ExecResult exec = il::vm::detail::handleAdd(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Sub(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Sub);
    }
    ExecResult exec = il::vm::detail::handleSub(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Mul(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Mul);
    }
    ExecResult exec = il::vm::detail::handleMul(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_IAddOvf(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::IAddOvf);
    }
    ExecResult exec = il::vm::detail::handleIAddOvf(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ISubOvf(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ISubOvf);
    }
    ExecResult exec = il::vm::detail::handleISubOvf(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_IMulOvf(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::IMulOvf);
    }
    ExecResult exec = il::vm::detail::handleIMulOvf(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SDiv(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SDiv);
    }
    ExecResult exec = il::vm::detail::handleSDiv(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UDiv(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UDiv);
    }
    ExecResult exec = il::vm::detail::handleUDiv(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SRem(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SRem);
    }
    ExecResult exec = il::vm::detail::handleSRem(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_URem(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::URem);
    }
    ExecResult exec = il::vm::detail::handleURem(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SDivChk0(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SDivChk0);
    }
    ExecResult exec = il::vm::detail::handleSDivChk0(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UDivChk0(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UDivChk0);
    }
    ExecResult exec = il::vm::detail::handleUDivChk0(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SRemChk0(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SRemChk0);
    }
    ExecResult exec = il::vm::detail::handleSRemChk0(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_URemChk0(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::URemChk0);
    }
    ExecResult exec = il::vm::detail::handleURemChk0(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_IdxChk(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::IdxChk);
    }
    ExecResult exec = il::vm::detail::handleIdxChk(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_And(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::And);
    }
    ExecResult exec = il::vm::detail::handleAnd(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Or(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Or);
    }
    ExecResult exec = il::vm::detail::handleOr(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Xor(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Xor);
    }
    ExecResult exec = il::vm::detail::handleXor(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Shl(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Shl);
    }
    ExecResult exec = il::vm::detail::handleShl(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_LShr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::LShr);
    }
    ExecResult exec = il::vm::detail::handleLShr(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_AShr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::AShr);
    }
    ExecResult exec = il::vm::detail::handleAShr(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FAdd(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FAdd);
    }
    ExecResult exec = il::vm::detail::handleFAdd(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FSub(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FSub);
    }
    ExecResult exec = il::vm::detail::handleFSub(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FMul(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FMul);
    }
    ExecResult exec = il::vm::detail::handleFMul(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FDiv(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FDiv);
    }
    ExecResult exec = il::vm::detail::handleFDiv(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ICmpEq(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ICmpEq);
    }
    ExecResult exec = il::vm::detail::handleICmpEq(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ICmpNe(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ICmpNe);
    }
    ExecResult exec = il::vm::detail::handleICmpNe(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpLT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SCmpLT);
    }
    ExecResult exec = il::vm::detail::handleSCmpLT(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpLE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SCmpLE);
    }
    ExecResult exec = il::vm::detail::handleSCmpLE(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpGT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SCmpGT);
    }
    ExecResult exec = il::vm::detail::handleSCmpGT(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpGE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SCmpGE);
    }
    ExecResult exec = il::vm::detail::handleSCmpGE(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpLT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UCmpLT);
    }
    ExecResult exec = il::vm::detail::handleUCmpLT(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpLE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UCmpLE);
    }
    ExecResult exec = il::vm::detail::handleUCmpLE(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpGT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UCmpGT);
    }
    ExecResult exec = il::vm::detail::handleUCmpGT(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpGE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UCmpGE);
    }
    ExecResult exec = il::vm::detail::handleUCmpGE(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpEQ(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpEQ);
    }
    ExecResult exec = il::vm::detail::handleFCmpEQ(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpNE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpNE);
    }
    ExecResult exec = il::vm::detail::handleFCmpNE(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpLT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpLT);
    }
    ExecResult exec = il::vm::detail::handleFCmpLT(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpLE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpLE);
    }
    ExecResult exec = il::vm::detail::handleFCmpLE(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpGT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpGT);
    }
    ExecResult exec = il::vm::detail::handleFCmpGT(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpGE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpGE);
    }
    ExecResult exec = il::vm::detail::handleFCmpGE(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Sitofp(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Sitofp);
    }
    ExecResult exec = il::vm::detail::handleSitofp(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Fptosi(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Fptosi);
    }
    ExecResult exec = il::vm::detail::handleFptosi(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastFpToSiRteChk(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastFpToSiRteChk);
    }
    ExecResult exec =
        il::vm::detail::handleCastFpToSiRteChk(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastFpToUiRteChk(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastFpToUiRteChk);
    }
    ExecResult exec =
        il::vm::detail::handleCastFpToUiRteChk(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastSiNarrowChk(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastSiNarrowChk);
    }
    ExecResult exec =
        il::vm::detail::handleCastSiNarrowChk(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastUiNarrowChk(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastUiNarrowChk);
    }
    ExecResult exec =
        il::vm::detail::handleCastUiNarrowChk(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastSiToFp(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastSiToFp);
    }
    ExecResult exec =
        il::vm::detail::handleCastSiToFp(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastUiToFp(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastUiToFp);
    }
    ExecResult exec =
        il::vm::detail::handleCastUiToFp(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Zext1(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Zext1);
    }
    ExecResult exec =
        il::vm::detail::handleTruncOrZext1(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Trunc1(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Trunc1);
    }
    ExecResult exec =
        il::vm::detail::handleTruncOrZext1(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Alloca(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Alloca);
    }
    ExecResult exec = il::vm::detail::handleAlloca(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_GEP(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::GEP);
    }
    ExecResult exec = il::vm::detail::handleGEP(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Load(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Load);
    }
    ExecResult exec = il::vm::detail::handleLoad(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Store(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Store);
    }
    ExecResult exec = il::vm::detail::handleStore(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_AddrOf(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::AddrOf);
    }
    ExecResult exec = il::vm::detail::handleAddrOf(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ConstStr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ConstStr);
    }
    ExecResult exec = il::vm::detail::handleConstStr(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ConstNull(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ConstNull);
    }
    ExecResult exec =
        il::vm::detail::handleConstNull(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Call(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Call);
    }
    ExecResult exec = il::vm::detail::handleCall(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CallIndirect(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CallIndirect);
    }
    ExecResult exec =
        il::vm::detail::handleCallIndirect(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SwitchI32(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SwitchI32);
    }
    ExecResult exec =
        il::vm::detail::handleSwitchI32(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Br(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Br);
    }
    ExecResult exec = il::vm::detail::handleBr(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CBr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CBr);
    }
    ExecResult exec = il::vm::detail::handleCBr(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Ret(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Ret);
    }
    ExecResult exec = il::vm::detail::handleRet(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_TrapKind(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::TrapKind);
    }
    ExecResult exec = il::vm::detail::handleTrapKind(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_TrapFromErr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::TrapFromErr);
    }
    ExecResult exec = il::vm::detail::handleTrap(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_TrapErr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::TrapErr);
    }
    ExecResult exec = il::vm::detail::handleTrapErr(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetKind(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ErrGetKind);
    }
    ExecResult exec = il::vm::detail::handleErrGet(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetCode(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ErrGetCode);
    }
    ExecResult exec = il::vm::detail::handleErrGet(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetIp(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ErrGetIp);
    }
    ExecResult exec = il::vm::detail::handleErrGet(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetLine(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ErrGetLine);
    }
    ExecResult exec = il::vm::detail::handleErrGet(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_EhPush(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::EhPush);
    }
    ExecResult exec = il::vm::detail::handleEhPush(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_EhPop(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::EhPop);
    }
    ExecResult exec = il::vm::detail::handleEhPop(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ResumeSame(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ResumeSame);
    }
    ExecResult exec =
        il::vm::detail::handleResumeSame(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ResumeNext(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ResumeNext);
    }
    ExecResult exec =
        il::vm::detail::handleResumeNext(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ResumeLabel(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ResumeLabel);
    }
    ExecResult exec =
        il::vm::detail::handleResumeLabel(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_EhEntry(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::EhEntry);
    }
    ExecResult exec = il::vm::detail::handleEhEntry(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Trap(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Trap);
    }
    ExecResult exec = il::vm::detail::handleTrap(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

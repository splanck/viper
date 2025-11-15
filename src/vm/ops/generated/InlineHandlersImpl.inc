// This file is auto-generated. Do not edit manually.
// Maintained as a committed generated include.

void VM::inline_handle_Add(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Add);
    }
    auto handler = &il::vm::detail::handleAdd;
    if (!handler)
    {
        trapUnimplemented(Opcode::Add);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Sub(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Sub);
    }
    auto handler = &il::vm::detail::handleSub;
    if (!handler)
    {
        trapUnimplemented(Opcode::Sub);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Mul(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Mul);
    }
    auto handler = &il::vm::detail::handleMul;
    if (!handler)
    {
        trapUnimplemented(Opcode::Mul);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_IAddOvf(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::IAddOvf);
    }
    auto handler = &il::vm::detail::handleIAddOvf;
    if (!handler)
    {
        trapUnimplemented(Opcode::IAddOvf);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ISubOvf(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ISubOvf);
    }
    auto handler = &il::vm::detail::handleISubOvf;
    if (!handler)
    {
        trapUnimplemented(Opcode::ISubOvf);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_IMulOvf(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::IMulOvf);
    }
    auto handler = &il::vm::detail::handleIMulOvf;
    if (!handler)
    {
        trapUnimplemented(Opcode::IMulOvf);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SDiv(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SDiv);
    }
    auto handler = &il::vm::detail::handleSDiv;
    if (!handler)
    {
        trapUnimplemented(Opcode::SDiv);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UDiv(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UDiv);
    }
    auto handler = &il::vm::detail::handleUDiv;
    if (!handler)
    {
        trapUnimplemented(Opcode::UDiv);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SRem(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SRem);
    }
    auto handler = &il::vm::detail::handleSRem;
    if (!handler)
    {
        trapUnimplemented(Opcode::SRem);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_URem(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::URem);
    }
    auto handler = &il::vm::detail::handleURem;
    if (!handler)
    {
        trapUnimplemented(Opcode::URem);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SDivChk0(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SDivChk0);
    }
    auto handler = &il::vm::detail::handleSDivChk0;
    if (!handler)
    {
        trapUnimplemented(Opcode::SDivChk0);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UDivChk0(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UDivChk0);
    }
    auto handler = &il::vm::detail::handleUDivChk0;
    if (!handler)
    {
        trapUnimplemented(Opcode::UDivChk0);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SRemChk0(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SRemChk0);
    }
    auto handler = &il::vm::detail::handleSRemChk0;
    if (!handler)
    {
        trapUnimplemented(Opcode::SRemChk0);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_URemChk0(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::URemChk0);
    }
    auto handler = &il::vm::detail::handleURemChk0;
    if (!handler)
    {
        trapUnimplemented(Opcode::URemChk0);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_IdxChk(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::IdxChk);
    }
    auto handler = &il::vm::detail::handleIdxChk;
    if (!handler)
    {
        trapUnimplemented(Opcode::IdxChk);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_And(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::And);
    }
    auto handler = &il::vm::detail::handleAnd;
    if (!handler)
    {
        trapUnimplemented(Opcode::And);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Or(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Or);
    }
    auto handler = &il::vm::detail::handleOr;
    if (!handler)
    {
        trapUnimplemented(Opcode::Or);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Xor(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Xor);
    }
    auto handler = &il::vm::detail::handleXor;
    if (!handler)
    {
        trapUnimplemented(Opcode::Xor);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Shl(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Shl);
    }
    auto handler = &il::vm::detail::handleShl;
    if (!handler)
    {
        trapUnimplemented(Opcode::Shl);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_LShr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::LShr);
    }
    auto handler = &il::vm::detail::handleLShr;
    if (!handler)
    {
        trapUnimplemented(Opcode::LShr);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_AShr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::AShr);
    }
    auto handler = &il::vm::detail::handleAShr;
    if (!handler)
    {
        trapUnimplemented(Opcode::AShr);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FAdd(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FAdd);
    }
    auto handler = &il::vm::detail::handleFAdd;
    if (!handler)
    {
        trapUnimplemented(Opcode::FAdd);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FSub(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FSub);
    }
    auto handler = &il::vm::detail::handleFSub;
    if (!handler)
    {
        trapUnimplemented(Opcode::FSub);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FMul(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FMul);
    }
    auto handler = &il::vm::detail::handleFMul;
    if (!handler)
    {
        trapUnimplemented(Opcode::FMul);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FDiv(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FDiv);
    }
    auto handler = &il::vm::detail::handleFDiv;
    if (!handler)
    {
        trapUnimplemented(Opcode::FDiv);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ICmpEq(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ICmpEq);
    }
    auto handler = &il::vm::detail::handleICmpEq;
    if (!handler)
    {
        trapUnimplemented(Opcode::ICmpEq);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ICmpNe(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ICmpNe);
    }
    auto handler = &il::vm::detail::handleICmpNe;
    if (!handler)
    {
        trapUnimplemented(Opcode::ICmpNe);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpLT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SCmpLT);
    }
    auto handler = &il::vm::detail::handleSCmpLT;
    if (!handler)
    {
        trapUnimplemented(Opcode::SCmpLT);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpLE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SCmpLE);
    }
    auto handler = &il::vm::detail::handleSCmpLE;
    if (!handler)
    {
        trapUnimplemented(Opcode::SCmpLE);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpGT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SCmpGT);
    }
    auto handler = &il::vm::detail::handleSCmpGT;
    if (!handler)
    {
        trapUnimplemented(Opcode::SCmpGT);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SCmpGE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SCmpGE);
    }
    auto handler = &il::vm::detail::handleSCmpGE;
    if (!handler)
    {
        trapUnimplemented(Opcode::SCmpGE);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpLT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UCmpLT);
    }
    auto handler = &il::vm::detail::handleUCmpLT;
    if (!handler)
    {
        trapUnimplemented(Opcode::UCmpLT);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpLE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UCmpLE);
    }
    auto handler = &il::vm::detail::handleUCmpLE;
    if (!handler)
    {
        trapUnimplemented(Opcode::UCmpLE);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpGT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UCmpGT);
    }
    auto handler = &il::vm::detail::handleUCmpGT;
    if (!handler)
    {
        trapUnimplemented(Opcode::UCmpGT);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_UCmpGE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::UCmpGE);
    }
    auto handler = &il::vm::detail::handleUCmpGE;
    if (!handler)
    {
        trapUnimplemented(Opcode::UCmpGE);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpEQ(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpEQ);
    }
    auto handler = &il::vm::detail::handleFCmpEQ;
    if (!handler)
    {
        trapUnimplemented(Opcode::FCmpEQ);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpNE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpNE);
    }
    auto handler = &il::vm::detail::handleFCmpNE;
    if (!handler)
    {
        trapUnimplemented(Opcode::FCmpNE);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpLT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpLT);
    }
    auto handler = &il::vm::detail::handleFCmpLT;
    if (!handler)
    {
        trapUnimplemented(Opcode::FCmpLT);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpLE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpLE);
    }
    auto handler = &il::vm::detail::handleFCmpLE;
    if (!handler)
    {
        trapUnimplemented(Opcode::FCmpLE);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpGT(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpGT);
    }
    auto handler = &il::vm::detail::handleFCmpGT;
    if (!handler)
    {
        trapUnimplemented(Opcode::FCmpGT);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_FCmpGE(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::FCmpGE);
    }
    auto handler = &il::vm::detail::handleFCmpGE;
    if (!handler)
    {
        trapUnimplemented(Opcode::FCmpGE);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Sitofp(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Sitofp);
    }
    auto handler = &il::vm::detail::handleSitofp;
    if (!handler)
    {
        trapUnimplemented(Opcode::Sitofp);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Fptosi(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Fptosi);
    }
    auto handler = &il::vm::detail::handleFptosi;
    if (!handler)
    {
        trapUnimplemented(Opcode::Fptosi);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastFpToSiRteChk(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastFpToSiRteChk);
    }
    auto handler = &il::vm::detail::handleCastFpToSiRteChk;
    if (!handler)
    {
        trapUnimplemented(Opcode::CastFpToSiRteChk);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastFpToUiRteChk(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastFpToUiRteChk);
    }
    auto handler = &il::vm::detail::handleCastFpToUiRteChk;
    if (!handler)
    {
        trapUnimplemented(Opcode::CastFpToUiRteChk);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastSiNarrowChk(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastSiNarrowChk);
    }
    auto handler = &il::vm::detail::handleCastSiNarrowChk;
    if (!handler)
    {
        trapUnimplemented(Opcode::CastSiNarrowChk);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastUiNarrowChk(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastUiNarrowChk);
    }
    auto handler = &il::vm::detail::handleCastUiNarrowChk;
    if (!handler)
    {
        trapUnimplemented(Opcode::CastUiNarrowChk);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastSiToFp(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastSiToFp);
    }
    auto handler = &il::vm::detail::handleCastSiToFp;
    if (!handler)
    {
        trapUnimplemented(Opcode::CastSiToFp);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CastUiToFp(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CastUiToFp);
    }
    auto handler = &il::vm::detail::handleCastUiToFp;
    if (!handler)
    {
        trapUnimplemented(Opcode::CastUiToFp);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Zext1(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Zext1);
    }
    auto handler = &il::vm::detail::handleTruncOrZext1;
    if (!handler)
    {
        trapUnimplemented(Opcode::Zext1);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Trunc1(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Trunc1);
    }
    auto handler = &il::vm::detail::handleTruncOrZext1;
    if (!handler)
    {
        trapUnimplemented(Opcode::Trunc1);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Alloca(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Alloca);
    }
    auto handler = &il::vm::detail::handleAlloca;
    if (!handler)
    {
        trapUnimplemented(Opcode::Alloca);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_GEP(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::GEP);
    }
    auto handler = &il::vm::detail::handleGEP;
    if (!handler)
    {
        trapUnimplemented(Opcode::GEP);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Load(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Load);
    }
    auto handler = &il::vm::detail::handleLoad;
    if (!handler)
    {
        trapUnimplemented(Opcode::Load);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Store(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Store);
    }
    auto handler = &il::vm::detail::handleStore;
    if (!handler)
    {
        trapUnimplemented(Opcode::Store);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_AddrOf(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::AddrOf);
    }
    auto handler = &il::vm::detail::handleAddrOf;
    if (!handler)
    {
        trapUnimplemented(Opcode::AddrOf);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ConstStr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ConstStr);
    }
    auto handler = &il::vm::detail::handleConstStr;
    if (!handler)
    {
        trapUnimplemented(Opcode::ConstStr);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ConstNull(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ConstNull);
    }
    auto handler = &il::vm::detail::handleConstNull;
    if (!handler)
    {
        trapUnimplemented(Opcode::ConstNull);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Call(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Call);
    }
    auto handler = &il::vm::detail::handleCall;
    if (!handler)
    {
        trapUnimplemented(Opcode::Call);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CallIndirect(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CallIndirect);
    }
    auto handler = &il::vm::detail::handleCallIndirect;
    if (!handler)
    {
        trapUnimplemented(Opcode::CallIndirect);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_SwitchI32(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::SwitchI32);
    }
    auto handler = &il::vm::detail::handleSwitchI32;
    if (!handler)
    {
        trapUnimplemented(Opcode::SwitchI32);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Br(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Br);
    }
    auto handler = &il::vm::detail::handleBr;
    if (!handler)
    {
        trapUnimplemented(Opcode::Br);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_CBr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::CBr);
    }
    auto handler = &il::vm::detail::handleCBr;
    if (!handler)
    {
        trapUnimplemented(Opcode::CBr);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Ret(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Ret);
    }
    auto handler = &il::vm::detail::handleRet;
    if (!handler)
    {
        trapUnimplemented(Opcode::Ret);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_TrapKind(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::TrapKind);
    }
    auto handler = &il::vm::detail::handleTrapKind;
    if (!handler)
    {
        trapUnimplemented(Opcode::TrapKind);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_TrapFromErr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::TrapFromErr);
    }
    auto handler = &il::vm::detail::handleTrap;
    if (!handler)
    {
        trapUnimplemented(Opcode::TrapFromErr);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_TrapErr(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::TrapErr);
    }
    auto handler = &il::vm::detail::handleTrapErr;
    if (!handler)
    {
        trapUnimplemented(Opcode::TrapErr);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetKind(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ErrGetKind);
    }
    auto handler = &il::vm::detail::handleErrGet;
    if (!handler)
    {
        trapUnimplemented(Opcode::ErrGetKind);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetCode(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ErrGetCode);
    }
    auto handler = &il::vm::detail::handleErrGet;
    if (!handler)
    {
        trapUnimplemented(Opcode::ErrGetCode);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetIp(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ErrGetIp);
    }
    auto handler = &il::vm::detail::handleErrGet;
    if (!handler)
    {
        trapUnimplemented(Opcode::ErrGetIp);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ErrGetLine(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ErrGetLine);
    }
    auto handler = &il::vm::detail::handleErrGet;
    if (!handler)
    {
        trapUnimplemented(Opcode::ErrGetLine);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_EhPush(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::EhPush);
    }
    auto handler = &il::vm::detail::handleEhPush;
    if (!handler)
    {
        trapUnimplemented(Opcode::EhPush);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_EhPop(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::EhPop);
    }
    auto handler = &il::vm::detail::handleEhPop;
    if (!handler)
    {
        trapUnimplemented(Opcode::EhPop);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ResumeSame(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ResumeSame);
    }
    auto handler = &il::vm::detail::handleResumeSame;
    if (!handler)
    {
        trapUnimplemented(Opcode::ResumeSame);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ResumeNext(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ResumeNext);
    }
    auto handler = &il::vm::detail::handleResumeNext;
    if (!handler)
    {
        trapUnimplemented(Opcode::ResumeNext);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_ResumeLabel(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::ResumeLabel);
    }
    auto handler = &il::vm::detail::handleResumeLabel;
    if (!handler)
    {
        trapUnimplemented(Opcode::ResumeLabel);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_EhEntry(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::EhEntry);
    }
    auto handler = &il::vm::detail::handleEhEntry;
    if (!handler)
    {
        trapUnimplemented(Opcode::EhEntry);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

void VM::inline_handle_Trap(ExecState &st)
{
    const Instr *instr = st.currentInstr;
    if (!instr)
    {
        trapUnimplemented(Opcode::Trap);
    }
    auto handler = &il::vm::detail::handleTrap;
    if (!handler)
    {
        trapUnimplemented(Opcode::Trap);
    }
    ExecResult exec = handler(*this, st.fr, *instr, st.blocks, st.bb, st.ip);
    handleInlineResult(st, exec);
}

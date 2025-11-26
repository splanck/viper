// This file is auto-generated. Do not edit manually.
// Maintained as a committed generated include.
LBL_Add:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Add(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Sub:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Sub(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Mul:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Mul(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_IAddOvf:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_IAddOvf(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ISubOvf:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ISubOvf(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_IMulOvf:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_IMulOvf(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SDiv:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_SDiv(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UDiv:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_UDiv(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SRem:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_SRem(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_URem:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_URem(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SDivChk0:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_SDivChk0(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UDivChk0:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_UDivChk0(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SRemChk0:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_SRemChk0(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_URemChk0:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_URemChk0(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_IdxChk:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_IdxChk(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_And:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_And(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Or:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Or(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Xor:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Xor(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Shl:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Shl(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_LShr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_LShr(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_AShr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_AShr(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FAdd:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_FAdd(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FSub:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_FSub(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FMul:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_FMul(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FDiv:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_FDiv(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ICmpEq:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ICmpEq(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ICmpNe:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ICmpNe(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SCmpLT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_SCmpLT(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SCmpLE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_SCmpLE(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SCmpGT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_SCmpGT(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SCmpGE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_SCmpGE(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UCmpLT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_UCmpLT(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UCmpLE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_UCmpLE(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UCmpGT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_UCmpGT(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UCmpGE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_UCmpGE(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpEQ:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_FCmpEQ(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpNE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_FCmpNE(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpLT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_FCmpLT(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpLE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_FCmpLE(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpGT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_FCmpGT(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpGE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_FCmpGE(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Sitofp:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Sitofp(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Fptosi:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Fptosi(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastFpToSiRteChk:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_CastFpToSiRteChk(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastFpToUiRteChk:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_CastFpToUiRteChk(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastSiNarrowChk:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_CastSiNarrowChk(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastUiNarrowChk:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_CastUiNarrowChk(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastSiToFp:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_CastSiToFp(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastUiToFp:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_CastUiToFp(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Zext1:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Zext1(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Trunc1:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Trunc1(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Alloca:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Alloca(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_GEP:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_GEP(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Load:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Load(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Store:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Store(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_AddrOf:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_AddrOf(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ConstStr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ConstStr(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ConstNull:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ConstNull(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Call:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Call(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SwitchI32:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_SwitchI32(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Br:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Br(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CBr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_CBr(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Ret:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Ret(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_TrapKind:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_TrapKind(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_TrapFromErr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_TrapFromErr(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_TrapErr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_TrapErr(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ErrGetKind:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ErrGetKind(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ErrGetCode:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ErrGetCode(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ErrGetIp:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ErrGetIp(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ErrGetLine:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ErrGetLine(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_EhPush:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_EhPush(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_EhPop:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_EhPop(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ResumeSame:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ResumeSame(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ResumeNext:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ResumeNext(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ResumeLabel:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_ResumeLabel(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_EhEntry:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_EhEntry(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Trap:
{
    vm.traceInstruction(*currentInstr, state.fr);
    vm.inline_handle_Trap(state);
    if (state.exitRequested)
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

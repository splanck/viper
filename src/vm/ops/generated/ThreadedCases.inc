// This file is auto-generated. Do not edit manually.
// Maintained as a committed generated include.
LBL_Add:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Sub:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Mul:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_IAddOvf:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ISubOvf:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_IMulOvf:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SDiv:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UDiv:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SRem:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_URem:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SDivChk0:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UDivChk0:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SRemChk0:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_URemChk0:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_IdxChk:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_And:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Or:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Xor:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Shl:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_LShr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_AShr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FAdd:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FSub:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FMul:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FDiv:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ICmpEq:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ICmpNe:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SCmpLT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SCmpLE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SCmpGT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SCmpGE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UCmpLT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UCmpLE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UCmpGT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_UCmpGE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpEQ:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpNE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpLT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpLE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpGT:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_FCmpGE:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Sitofp:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Fptosi:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastFpToSiRteChk:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastFpToUiRteChk:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastSiNarrowChk:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastUiNarrowChk:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastSiToFp:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CastUiToFp:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Zext1:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Trunc1:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Alloca:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_GEP:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Load:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Store:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_AddrOf:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ConstStr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ConstNull:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Call:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_SwitchI32:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Br:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_CBr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Ret:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_TrapKind:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_TrapFromErr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_TrapErr:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ErrGetKind:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ErrGetCode:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ErrGetIp:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ErrGetLine:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_EhPush:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_EhPop:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ResumeSame:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ResumeNext:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_ResumeLabel:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_EhEntry:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

LBL_Trap:
{
    vm.traceInstruction(*currentInstr, state.fr);
    auto exec = vm.executeOpcode(state.fr, *currentInstr, state.blocks, state.bb, state.ip);
    if (vm.finalizeDispatch(state, exec))
        return true;
    opcode = fetchNext();
    if (state.exitRequested)
        return true;
    DISPATCH_TO(opcode);
}

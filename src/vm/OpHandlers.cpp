// File: src/vm/OpHandlers.cpp
// Purpose: Build the VM opcode dispatch table from declarative opcode metadata.
// Key invariants: Table entries align with il/core/Opcode.def definitions.
// Ownership/Lifetime: Dispatch table is lazily initialised and shared across VMs.
// Links: docs/il-spec.md

#include "vm/OpHandlers.hpp"

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"

using namespace il::core;

namespace il::vm
{
const VM::OpcodeHandlerTable &VM::getOpcodeHandlers()
{
    return detail::getOpcodeHandlers();
}
} // namespace il::vm

namespace il::vm::detail
{
namespace
{
VM::OpcodeHandler handlerForDispatch(VMDispatch dispatch)
{
    switch (dispatch)
    {
        case VMDispatch::None:
            return nullptr;
        case VMDispatch::Alloca:
            return &OpHandlers::handleAlloca;
        case VMDispatch::Load:
            return &OpHandlers::handleLoad;
        case VMDispatch::Store:
            return &OpHandlers::handleStore;
        case VMDispatch::GEP:
            return &OpHandlers::handleGEP;
        case VMDispatch::Add:
            return &OpHandlers::handleAdd;
        case VMDispatch::Sub:
            return &OpHandlers::handleSub;
        case VMDispatch::Mul:
            return &OpHandlers::handleMul;
        case VMDispatch::Xor:
            return &OpHandlers::handleXor;
        case VMDispatch::Shl:
            return &OpHandlers::handleShl;
        case VMDispatch::FAdd:
            return &OpHandlers::handleFAdd;
        case VMDispatch::FSub:
            return &OpHandlers::handleFSub;
        case VMDispatch::FMul:
            return &OpHandlers::handleFMul;
        case VMDispatch::FDiv:
            return &OpHandlers::handleFDiv;
        case VMDispatch::ICmpEq:
            return &OpHandlers::handleICmpEq;
        case VMDispatch::ICmpNe:
            return &OpHandlers::handleICmpNe;
        case VMDispatch::SCmpGT:
            return &OpHandlers::handleSCmpGT;
        case VMDispatch::SCmpLT:
            return &OpHandlers::handleSCmpLT;
        case VMDispatch::SCmpLE:
            return &OpHandlers::handleSCmpLE;
        case VMDispatch::SCmpGE:
            return &OpHandlers::handleSCmpGE;
        case VMDispatch::FCmpEQ:
            return &OpHandlers::handleFCmpEQ;
        case VMDispatch::FCmpNE:
            return &OpHandlers::handleFCmpNE;
        case VMDispatch::FCmpGT:
            return &OpHandlers::handleFCmpGT;
        case VMDispatch::FCmpLT:
            return &OpHandlers::handleFCmpLT;
        case VMDispatch::FCmpLE:
            return &OpHandlers::handleFCmpLE;
        case VMDispatch::FCmpGE:
            return &OpHandlers::handleFCmpGE;
        case VMDispatch::Br:
            return &OpHandlers::handleBr;
        case VMDispatch::CBr:
            return &OpHandlers::handleCBr;
        case VMDispatch::Ret:
            return &OpHandlers::handleRet;
        case VMDispatch::AddrOf:
            return &OpHandlers::handleAddrOf;
        case VMDispatch::ConstStr:
            return &OpHandlers::handleConstStr;
        case VMDispatch::Call:
            return &OpHandlers::handleCall;
        case VMDispatch::Sitofp:
            return &OpHandlers::handleSitofp;
        case VMDispatch::Fptosi:
            return &OpHandlers::handleFptosi;
        case VMDispatch::TruncOrZext1:
            return &OpHandlers::handleTruncOrZext1;
        case VMDispatch::Trap:
            return &OpHandlers::handleTrap;
    }
    return nullptr;
}
} // namespace

const VM::OpcodeHandlerTable &getOpcodeHandlers()
{
    static const VM::OpcodeHandlerTable table = []
    {
        VM::OpcodeHandlerTable handlers{};
#define IL_OPCODE(NAME, MNEMONIC, RES_ARITY, RES_TYPE, MIN_OPS, MAX_OPS, OP0, OP1, OP2, SIDE_EFFECTS, SUCCESSORS, TERMINATOR,      \
                  DISPATCH)                                                                                                       \
        handlers[static_cast<size_t>(Opcode::NAME)] = handlerForDispatch(DISPATCH);
#include "il/core/Opcode.def"
#undef IL_OPCODE
        return handlers;
    }();
    return table;
}

} // namespace il::vm::detail


// File: src/vm/OpHandlers.cpp
// Purpose: Build the VM opcode dispatch table from declarative opcode metadata.
// Key invariants: Table entries align with il/core/Opcode.def definitions.
// Ownership/Lifetime: Dispatch table is lazily initialised and shared across VMs.
// License: MIT (see LICENSE file in the project root for terms).
// Links: docs/il-guide.md#reference

#include "vm/OpHandlers.hpp"

#include "il/core/Opcode.hpp"
#include "il/core/OpcodeInfo.hpp"

using namespace il::core;

namespace il::vm
{
/// @brief Exposes the lazily materialised opcode → handler mapping shared across
/// all VM instances.
/// @details Delegates to detail::getOpcodeHandlers(), which consults the
/// declarative metadata emitted from Opcode.def so each il::core::Opcode
/// enumerator reuses the dispatch handler recorded alongside its definition.
/// Entries are cached after first construction to avoid recomputing the table.
const VM::OpcodeHandlerTable &VM::getOpcodeHandlers()
{
    return detail::getOpcodeHandlers();
}
} // namespace il::vm

namespace il::vm::detail
{
namespace
{
/// @brief Maps opcode metadata dispatch categories to concrete handler
/// functions.
/// @details The dispatch field selected in Opcode.def determines which handler
/// is returned for a given opcode; metadata entries that specify no dispatch or
/// an unsupported category resolve to nullptr so unsupported instructions remain
/// unbound in the table.
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
        case VMDispatch::IAddOvf:
            return &OpHandlers::handleIAddOvf;
        case VMDispatch::ISubOvf:
            return &OpHandlers::handleISubOvf;
        case VMDispatch::IMulOvf:
            return &OpHandlers::handleIMulOvf;
        case VMDispatch::SDivChk0:
            return &OpHandlers::handleSDivChk0;
        case VMDispatch::UDivChk0:
            return &OpHandlers::handleUDivChk0;
        case VMDispatch::SRemChk0:
            return &OpHandlers::handleSRemChk0;
        case VMDispatch::URemChk0:
            return &OpHandlers::handleURemChk0;
        case VMDispatch::IdxChk:
            return &OpHandlers::handleIdxChk;
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
        case VMDispatch::CastFpToSiRteChk:
            return &OpHandlers::handleCastFpToSiRteChk;
        case VMDispatch::CastSiNarrowChk:
            return &OpHandlers::handleCastSiNarrowChk;
        case VMDispatch::CastUiNarrowChk:
            return &OpHandlers::handleCastUiNarrowChk;
        case VMDispatch::CastSiToFp:
            return &OpHandlers::handleCastSiToFp;
        case VMDispatch::CastUiToFp:
            return &OpHandlers::handleCastUiToFp;
        case VMDispatch::TruncOrZext1:
            return &OpHandlers::handleTruncOrZext1;
        case VMDispatch::ErrGet:
            return &OpHandlers::handleErrGet;
        case VMDispatch::Trap:
            return &OpHandlers::handleTrap;
        case VMDispatch::EhPush:
            return &OpHandlers::handleEhPush;
        case VMDispatch::EhPop:
            return &OpHandlers::handleEhPop;
        case VMDispatch::ResumeSame:
            return &OpHandlers::handleResumeSame;
        case VMDispatch::ResumeNext:
            return &OpHandlers::handleResumeNext;
        case VMDispatch::ResumeLabel:
            return &OpHandlers::handleResumeLabel;
    }
    return nullptr;
}
} // namespace

/// @brief Builds and caches the opcode dispatch table from the declarative IL
/// opcode list.
/// @details A function-local static initialises the table exactly once by
/// iterating the IL_OPCODE definitions in Opcode.def, which must stay aligned
/// with the il::core::Opcode enumerators. Each metadata entry contributes its
/// recorded dispatch category, and handlerForDispatch translates that category
/// to the handler pointer stored in the lazily initialised table.
const VM::OpcodeHandlerTable &getOpcodeHandlers()
{
    static const VM::OpcodeHandlerTable table = []
    {
        VM::OpcodeHandlerTable handlers{};
#define IL_OPCODE(NAME,                                                                            \
                  MNEMONIC,                                                                        \
                  RES_ARITY,                                                                       \
                  RES_TYPE,                                                                        \
                  MIN_OPS,                                                                         \
                  MAX_OPS,                                                                         \
                  OP0,                                                                             \
                  OP1,                                                                             \
                  OP2,                                                                             \
                  SIDE_EFFECTS,                                                                    \
                  SUCCESSORS,                                                                      \
                  TERMINATOR,                                                                      \
                  DISPATCH,                                                                        \
                  PARSE0,                                                                          \
                  PARSE1,                                                                          \
                  PARSE2,                                                                          \
                  PARSE3)                                                                          \
    handlers[static_cast<size_t>(Opcode::NAME)] = handlerForDispatch(DISPATCH);
#include "il/core/Opcode.def"
#undef IL_OPCODE
        return handlers;
    }();
    return table;
}

} // namespace il::vm::detail

//===----------------------------------------------------------------------===//

// Auto-generated opcode handler table.
// This file is auto-generated. Do not edit manually.
// Use src/vm/ops/gen/opgen.py to regenerate.
//===----------------------------------------------------------------------===//

#pragma once

#include "vm/OpHandlers.hpp"
#include "vm/VM.hpp"

namespace il::vm::generated
{
inline const VM::OpcodeHandlerTable &opcodeHandlers()
{
    static const VM::OpcodeHandlerTable table = {
        &il::vm::detail::handleAdd,
        &il::vm::detail::handleSub,
        &il::vm::detail::handleMul,
        &il::vm::detail::handleIAddOvf,
        &il::vm::detail::handleISubOvf,
        &il::vm::detail::handleIMulOvf,
        &il::vm::detail::handleSDiv,
        &il::vm::detail::handleUDiv,
        &il::vm::detail::handleSRem,
        &il::vm::detail::handleURem,
        &il::vm::detail::handleSDivChk0,
        &il::vm::detail::handleUDivChk0,
        &il::vm::detail::handleSRemChk0,
        &il::vm::detail::handleURemChk0,
        &il::vm::detail::handleIdxChk,
        &il::vm::detail::handleAnd,
        &il::vm::detail::handleOr,
        &il::vm::detail::handleXor,
        &il::vm::detail::handleShl,
        &il::vm::detail::handleLShr,
        &il::vm::detail::handleAShr,
        &il::vm::detail::handleFAdd,
        &il::vm::detail::handleFSub,
        &il::vm::detail::handleFMul,
        &il::vm::detail::handleFDiv,
        &il::vm::detail::handleICmpEq,
        &il::vm::detail::handleICmpNe,
        &il::vm::detail::handleSCmpLT,
        &il::vm::detail::handleSCmpLE,
        &il::vm::detail::handleSCmpGT,
        &il::vm::detail::handleSCmpGE,
        &il::vm::detail::handleUCmpLT,
        &il::vm::detail::handleUCmpLE,
        &il::vm::detail::handleUCmpGT,
        &il::vm::detail::handleUCmpGE,
        &il::vm::detail::handleFCmpEQ,
        &il::vm::detail::handleFCmpNE,
        &il::vm::detail::handleFCmpLT,
        &il::vm::detail::handleFCmpLE,
        &il::vm::detail::handleFCmpGT,
        &il::vm::detail::handleFCmpGE,
        &il::vm::detail::handleSitofp,
        &il::vm::detail::handleFptosi,
        &il::vm::detail::handleCastFpToSiRteChk,
        &il::vm::detail::handleCastFpToUiRteChk,
        &il::vm::detail::handleCastSiNarrowChk,
        &il::vm::detail::handleCastUiNarrowChk,
        &il::vm::detail::handleCastSiToFp,
        &il::vm::detail::handleCastUiToFp,
        &il::vm::detail::handleTruncOrZext1,
        &il::vm::detail::handleTruncOrZext1,
        &il::vm::detail::handleAlloca,
        &il::vm::detail::handleGEP,
        &il::vm::detail::handleLoad,
        &il::vm::detail::handleStore,
        &il::vm::detail::handleAddrOf,
        &il::vm::detail::handleConstStr,
        &il::vm::detail::handleConstNull,
        &il::vm::detail::handleCall,
        &il::vm::detail::handleSwitchI32,
        &il::vm::detail::handleBr,
        &il::vm::detail::handleCBr,
        &il::vm::detail::handleRet,
        &il::vm::detail::handleTrapKind,
        &il::vm::detail::handleTrap,
        &il::vm::detail::handleTrapErr,
        &il::vm::detail::handleErrGet,
        &il::vm::detail::handleErrGet,
        &il::vm::detail::handleErrGet,
        &il::vm::detail::handleErrGet,
        &il::vm::detail::handleEhPush,
        &il::vm::detail::handleEhPop,
        &il::vm::detail::handleResumeSame,
        &il::vm::detail::handleResumeNext,
        &il::vm::detail::handleResumeLabel,
        &il::vm::detail::handleEhEntry,
        &il::vm::detail::handleTrap,
    };
    return table;
}
} // namespace il::vm::generated

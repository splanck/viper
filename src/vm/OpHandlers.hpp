// File: src/vm/OpHandlers.hpp
// Purpose: Aggregate opcode handler category declarations for VM dispatch wiring.
// Key invariants: Provides unified access points to handler functions grouped by category.
// Ownership/Lifetime: Functions exposed through this header do not own VM resources.
// Links: docs/il-guide.md#reference
#pragma once

#include "vm/OpHandlerAccess.hpp"
#include "vm/OpHandlers_Control.hpp"
#include "vm/OpHandlers_Float.hpp"
#include "vm/OpHandlers_Int.hpp"
#include "vm/OpHandlers_Memory.hpp"
#include <cassert>

namespace il::vm::detail
{
using memory::handleLoadImpl;
using memory::handleStoreImpl;
using memory::handleAlloca;
using memory::handleLoad;
using memory::handleStore;
using memory::handleGEP;
using memory::handleAddrOf;
using memory::handleConstStr;
using memory::handleConstNull;

using integer::handleAddImpl;
using integer::handleSubImpl;
using integer::handleMulImpl;
using integer::handleAdd;
using integer::handleSub;
using integer::handleMul;
using integer::handleISub;
using integer::handleIAddOvf;
using integer::handleISubOvf;
using integer::handleIMulOvf;
using integer::handleSDiv;
using integer::handleUDiv;
using integer::handleSRem;
using integer::handleURem;
using integer::handleSDivChk0;
using integer::handleUDivChk0;
using integer::handleSRemChk0;
using integer::handleURemChk0;
using integer::handleIdxChk;
using integer::handleCastSiNarrowChk;
using integer::handleCastUiNarrowChk;
using integer::handleCastSiToFp;
using integer::handleCastUiToFp;
using integer::handleTruncOrZext1;
using integer::handleAnd;
using integer::handleOr;
using integer::handleXor;
using integer::handleShl;
using integer::handleLShr;
using integer::handleAShr;
using integer::handleICmpEq;
using integer::handleICmpNe;
using integer::handleSCmpGT;
using integer::handleSCmpLT;
using integer::handleSCmpLE;
using integer::handleSCmpGE;
using integer::handleUCmpLT;
using integer::handleUCmpLE;
using integer::handleUCmpGT;
using integer::handleUCmpGE;

using control::branchToTarget;
using control::handleBrImpl;
using control::handleCBrImpl;
using control::handleSwitchI32Impl;
using control::handleSwitchI32;
using control::handleBr;
using control::handleCBr;
using control::handleRet;
using control::handleCall;
using control::handleErrGet;
using control::handleEhEntry;
using control::handleEhPush;
using control::handleEhPop;
using control::handleResumeSame;
using control::handleResumeNext;
using control::handleResumeLabel;
using control::handleTrapKind;
using control::handleTrapErr;
using control::handleTrap;

using floating::handleFAdd;
using floating::handleFSub;
using floating::handleFMul;
using floating::handleFDiv;
using floating::handleFCmpEQ;
using floating::handleFCmpNE;
using floating::handleFCmpGT;
using floating::handleFCmpLT;
using floating::handleFCmpLE;
using floating::handleFCmpGE;
using floating::handleSitofp;
using floating::handleFptosi;
using floating::handleCastFpToSiRteChk;
using floating::handleCastFpToUiRteChk;

const VM::OpcodeHandlerTable &getOpcodeHandlers();

} // namespace il::vm::detail


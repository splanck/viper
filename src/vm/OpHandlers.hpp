//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/OpHandlers.hpp
// Purpose: Aggregate opcode handler category declarations for VM dispatch wiring.
// Key invariants: Provides unified access points to handler functions grouped by category.
// Ownership/Lifetime: Functions exposed through this header do not own VM resources.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vm/OpHandlerAccess.hpp"
#include "vm/OpHandlers_Control.hpp"
#include "vm/OpHandlers_Float.hpp"
#include "vm/OpHandlers_Int.hpp"
#include "vm/OpHandlers_Memory.hpp"
#include <cassert>

namespace il::vm::detail
{
using memory::handleAddrOf;
using memory::handleAlloca;
using memory::handleConstNull;
using memory::handleConstStr;
using memory::handleGAddr;
using memory::handleGEP;
using memory::handleLoad;
using memory::handleLoadImpl;
using memory::handleStore;
using memory::handleStoreImpl;

using integer::handleAdd;
using integer::handleAddImpl;
using integer::handleAnd;
using integer::handleAShr;
using integer::handleCastSiNarrowChk;
using integer::handleCastSiToFp;
using integer::handleCastUiNarrowChk;
using integer::handleCastUiToFp;
using integer::handleIAddOvf;
using integer::handleICmpEq;
using integer::handleICmpNe;
using integer::handleIdxChk;
using integer::handleIMulOvf;
using integer::handleISub;
using integer::handleISubOvf;
using integer::handleLShr;
using integer::handleMul;
using integer::handleMulImpl;
using integer::handleOr;
using integer::handleSCmpGE;
using integer::handleSCmpGT;
using integer::handleSCmpLE;
using integer::handleSCmpLT;
using integer::handleSDiv;
using integer::handleSDivChk0;
using integer::handleShl;
using integer::handleSRem;
using integer::handleSRemChk0;
using integer::handleSub;
using integer::handleSubImpl;
using integer::handleTruncOrZext1;
using integer::handleUCmpGE;
using integer::handleUCmpGT;
using integer::handleUCmpLE;
using integer::handleUCmpLT;
using integer::handleUDiv;
using integer::handleUDivChk0;
using integer::handleURem;
using integer::handleURemChk0;
using integer::handleXor;

using control::branchToTarget;
using control::handleBr;
using control::handleBrImpl;
using control::handleCall;
using control::handleCallIndirect;
using control::handleCBr;
using control::handleCBrImpl;
using control::handleEhEntry;
using control::handleEhPop;
using control::handleEhPush;
using control::handleErrGet;
using control::handleResumeLabel;
using control::handleResumeNext;
using control::handleResumeSame;
using control::handleRet;
using control::handleSwitchI32;
using control::handleSwitchI32Impl;
using control::handleTrap;
using control::handleTrapErr;
using control::handleTrapKind;

using floating::handleCastFpToSiRteChk;
using floating::handleCastFpToUiRteChk;
using floating::handleFAdd;
using floating::handleFCmpEQ;
using floating::handleFCmpGE;
using floating::handleFCmpGT;
using floating::handleFCmpLE;
using floating::handleFCmpLT;
using floating::handleFCmpNE;
using floating::handleFDiv;
using floating::handleFMul;
using floating::handleFptosi;
using floating::handleFSub;
using floating::handleSitofp;

const VM::OpcodeHandlerTable &getOpcodeHandlers();

} // namespace il::vm::detail

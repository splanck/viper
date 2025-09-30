// File: src/vm/OpHandlers.hpp
// Purpose: Declares opcode handler helpers and dispatch table for the VM.
// Key invariants: Handlers implement IL opcode semantics according to docs/il-guide.md#reference.
// Ownership/Lifetime: Handlers operate on VM-owned frames and do not retain references.
// Links: docs/il-guide.md#reference
#pragma once

#include "vm/VM.hpp"

namespace il::vm::detail
{

/// @brief Collection of opcode handler entry points used by the VM dispatcher.
struct OpHandlers
{
    static VM::ExecResult handleAlloca(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleLoad(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleStore(VM &vm,
                                      Frame &fr,
                                      const il::core::Instr &in,
                                      const VM::BlockMap &blocks,
                                      const il::core::BasicBlock *&bb,
                                      size_t &ip);

    static VM::ExecResult handleAdd(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleSub(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleMul(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleIAddOvf(VM &vm,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleISubOvf(VM &vm,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleIMulOvf(VM &vm,
                                        Frame &fr,
                                        const il::core::Instr &in,
                                        const VM::BlockMap &blocks,
                                        const il::core::BasicBlock *&bb,
                                        size_t &ip);

    static VM::ExecResult handleSDivChk0(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleUDivChk0(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleSRemChk0(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleURemChk0(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleIdxChk(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFAdd(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleFSub(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleFMul(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleFDiv(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleXor(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleShl(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleGEP(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleICmpEq(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleICmpNe(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleSCmpGT(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleSCmpLT(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleSCmpLE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleSCmpGE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpEQ(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpNE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpGT(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpLT(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpLE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFCmpGE(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleBr(VM &vm,
                                   Frame &fr,
                                   const il::core::Instr &in,
                                   const VM::BlockMap &blocks,
                                   const il::core::BasicBlock *&bb,
                                   size_t &ip);

    static VM::ExecResult handleCBr(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleRet(VM &vm,
                                    Frame &fr,
                                    const il::core::Instr &in,
                                    const VM::BlockMap &blocks,
                                    const il::core::BasicBlock *&bb,
                                    size_t &ip);

    static VM::ExecResult handleAddrOf(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleConstStr(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);

    static VM::ExecResult handleCall(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

    static VM::ExecResult handleSitofp(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleFptosi(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleCastFpToSiRteChk(VM &vm,
                                                 Frame &fr,
                                                 const il::core::Instr &in,
                                                 const VM::BlockMap &blocks,
                                                 const il::core::BasicBlock *&bb,
                                                 size_t &ip);

    static VM::ExecResult handleCastSiNarrowChk(VM &vm,
                                                Frame &fr,
                                                const il::core::Instr &in,
                                                const VM::BlockMap &blocks,
                                                const il::core::BasicBlock *&bb,
                                                size_t &ip);

    static VM::ExecResult handleCastUiNarrowChk(VM &vm,
                                                Frame &fr,
                                                const il::core::Instr &in,
                                                const VM::BlockMap &blocks,
                                                const il::core::BasicBlock *&bb,
                                                size_t &ip);

    static VM::ExecResult handleCastSiToFp(VM &vm,
                                           Frame &fr,
                                           const il::core::Instr &in,
                                           const VM::BlockMap &blocks,
                                           const il::core::BasicBlock *&bb,
                                           size_t &ip);

    static VM::ExecResult handleCastUiToFp(VM &vm,
                                           Frame &fr,
                                           const il::core::Instr &in,
                                           const VM::BlockMap &blocks,
                                           const il::core::BasicBlock *&bb,
                                           size_t &ip);

    static VM::ExecResult handleTruncOrZext1(VM &vm,
                                             Frame &fr,
                                             const il::core::Instr &in,
                                             const VM::BlockMap &blocks,
                                             const il::core::BasicBlock *&bb,
                                             size_t &ip);

    static VM::ExecResult handleErrGet(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleEhPush(VM &vm,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const VM::BlockMap &blocks,
                                       const il::core::BasicBlock *&bb,
                                       size_t &ip);

    static VM::ExecResult handleEhPop(VM &vm,
                                      Frame &fr,
                                      const il::core::Instr &in,
                                      const VM::BlockMap &blocks,
                                      const il::core::BasicBlock *&bb,
                                      size_t &ip);

    static VM::ExecResult handleResumeSame(VM &vm,
                                           Frame &fr,
                                           const il::core::Instr &in,
                                           const VM::BlockMap &blocks,
                                           const il::core::BasicBlock *&bb,
                                           size_t &ip);

    static VM::ExecResult handleResumeNext(VM &vm,
                                           Frame &fr,
                                           const il::core::Instr &in,
                                           const VM::BlockMap &blocks,
                                           const il::core::BasicBlock *&bb,
                                           size_t &ip);

    static VM::ExecResult handleResumeLabel(VM &vm,
                                            Frame &fr,
                                            const il::core::Instr &in,
                                            const VM::BlockMap &blocks,
                                            const il::core::BasicBlock *&bb,
                                            size_t &ip);

    static VM::ExecResult handleTrap(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip);

  private:
    static VM::ExecResult branchToTarget(VM &vm,
                                         Frame &fr,
                                         const il::core::Instr &in,
                                         size_t idx,
                                         const VM::BlockMap &blocks,
                                         const il::core::BasicBlock *&bb,
                                         size_t &ip);
};

/// @brief Access the lazily initialised opcode handler table.
const VM::OpcodeHandlerTable &getOpcodeHandlers();

} // namespace il::vm::detail

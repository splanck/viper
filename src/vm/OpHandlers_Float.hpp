//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/OpHandlers_Float.hpp
// Purpose: Declare floating-point opcode handlers for the VM dispatcher. 
// Key invariants: Handlers implement IEEE-754 semantics via host double operations.
// Ownership/Lifetime: Handlers only mutate the destination slot in the active frame.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vm/OpHandlerAccess.hpp"
#include "vm/VM.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"

namespace il::vm::detail::floating
{
VM::ExecResult handleFAdd(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

VM::ExecResult handleFSub(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

VM::ExecResult handleFMul(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

VM::ExecResult handleFDiv(VM &vm,
                          Frame &fr,
                          const il::core::Instr &in,
                          const VM::BlockMap &blocks,
                          const il::core::BasicBlock *&bb,
                          size_t &ip);

VM::ExecResult handleFCmpEQ(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleFCmpNE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleFCmpGT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleFCmpLT(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleFCmpLE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleFCmpGE(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleSitofp(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleFptosi(VM &vm,
                            Frame &fr,
                            const il::core::Instr &in,
                            const VM::BlockMap &blocks,
                            const il::core::BasicBlock *&bb,
                            size_t &ip);

VM::ExecResult handleCastFpToSiRteChk(VM &vm,
                                      Frame &fr,
                                      const il::core::Instr &in,
                                      const VM::BlockMap &blocks,
                                      const il::core::BasicBlock *&bb,
                                      size_t &ip);

VM::ExecResult handleCastFpToUiRteChk(VM &vm,
                                      Frame &fr,
                                      const il::core::Instr &in,
                                      const VM::BlockMap &blocks,
                                      const il::core::BasicBlock *&bb,
                                      size_t &ip);

} // namespace il::vm::detail::floating

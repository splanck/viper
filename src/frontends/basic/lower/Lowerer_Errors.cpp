//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Lowerer_Errors.cpp
// Purpose: Centralises diagnostic handling and error-related lowering helpers
//          for the BASIC front-end lowerer.
// Key invariants: Diagnostic emitters are optional; runtime error helpers only
//                 operate when a procedure context is active.
// Ownership/Lifetime: Borrowed DiagnosticEmitter and Lowerer state; no AST
//                     ownership.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <functional>
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

void Lowerer::setDiagnosticEmitter(DiagnosticEmitter *emitter) noexcept
{
    diagnosticEmitter_ = emitter;
    if (emitter)
    {
        TypeRules::setTypeErrorSink(
            [emitter](const TypeRules::TypeError &error)
            {
                emitter->emit(il::support::Severity::Error,
                              error.code,
                              il::support::SourceLoc{},
                              0,
                              error.message);
            });
    }
    else
    {
        TypeRules::setTypeErrorSink({});
    }
}

DiagnosticEmitter *Lowerer::diagnosticEmitter() const noexcept
{
    return diagnosticEmitter_;
}

Lowerer::RVal Lowerer::normalizeChannelToI32(RVal channel, il::support::SourceLoc loc)
{
    if (channel.type.kind == Type::Kind::I32)
        return channel;

    channel = ensureI64(std::move(channel), loc);
    channel.value = emitCommon(loc).narrow_to(channel.value, 64, 32);
    channel.type = Type(Type::Kind::I32);
    return channel;
}

void Lowerer::emitRuntimeErrCheck(Value err,
                                  il::support::SourceLoc loc,
                                  std::string_view labelStem,
                                  const std::function<void(Value)> &onFailure)
{
    ProcedureContext &ctx = context();
    Function *func = ctx.function();
    BasicBlock *original = ctx.current();
    if (!func || !original)
        return;

    size_t curIdx = static_cast<size_t>(original - &func->blocks[0]);
    BlockNamer *blockNamer = ctx.blockNames().namer();
    std::string stem(labelStem);
    std::string failLbl =
        blockNamer ? blockNamer->generic(stem + "_fail") : mangler.block(stem + "_fail");
    std::string contLbl =
        blockNamer ? blockNamer->generic(stem + "_cont") : mangler.block(stem + "_cont");

    size_t failIdx = func->blocks.size();
    builder->addBlock(*func, failLbl);
    size_t contIdx = func->blocks.size();
    builder->addBlock(*func, contLbl);

    BasicBlock *failBlk = &func->blocks[failIdx];
    BasicBlock *contBlk = &func->blocks[contIdx];

    ctx.setCurrent(&func->blocks[curIdx]);
    curLoc = loc;

    Value err64 = err;
    {
        Value scratch = emitAlloca(sizeof(int64_t));
        emitStore(Type(Type::Kind::I64), scratch, Value::constInt(0));
        emitStore(Type(Type::Kind::I32), scratch, err);
        err64 = emitLoad(Type(Type::Kind::I64), scratch);
    }

    Value isFail = emitBinary(Opcode::ICmpNe, ilBoolTy(), err64, Value::constInt(0));
    emitCBr(isFail, failBlk, contBlk);

    ctx.setCurrent(failBlk);
    curLoc = loc;
    onFailure(err);

    ctx.setCurrent(contBlk);
}

} // namespace il::frontends::basic

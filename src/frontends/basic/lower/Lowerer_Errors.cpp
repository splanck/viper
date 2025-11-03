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

#include "viper/il/Module.hpp"

#include <functional>
#include <string_view>
#include <utility>

namespace il::frontends::basic
{

/// @brief Attach a diagnostic emitter to the lowering pipeline.
/// @details Stores the supplied @p emitter for later use and wires its reporting
///          callbacks into @ref TypeRules so that type-check errors surface
///          through the same sink as lowering diagnostics.  Passing `nullptr`
///          detaches the sink and restores the default behaviour.
/// @param emitter Optional diagnostic sink to receive error reports.
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

/// @brief Retrieve the diagnostic emitter associated with the lowering pass.
/// @details Returns the previously installed emitter without transferring
///          ownership.  A null result indicates that diagnostics should be
///          suppressed or routed elsewhere by the caller.
/// @return Pointer to the active diagnostic emitter or `nullptr`.
DiagnosticEmitter *Lowerer::diagnosticEmitter() const noexcept
{
    return diagnosticEmitter_;
}

/// @brief Coerce a BASIC I/O channel value to the 32-bit integer domain.
/// @details Accepts either 32-bit or 64-bit integer expressions.  When a
///          64-bit value is supplied, it inserts a narrowing conversion into the
///          current basic block using @ref emitCommon.  The resulting value is
///          tagged with the 32-bit type so later stages observe the canonical
///          representation.
/// @param channel Lowered r-value representing the channel operand.
/// @param loc Source location used for any generated instructions.
/// @return Normalised r-value guaranteed to carry the 32-bit integer type.
Lowerer::RVal Lowerer::normalizeChannelToI32(RVal channel, il::support::SourceLoc loc)
{
    if (channel.type.kind == Type::Kind::I32)
        return channel;

    channel = ensureI64(std::move(channel), loc);
    channel.value = emitCommon(loc).narrow_to(channel.value, 64, 32);
    channel.type = Type(Type::Kind::I32);
    return channel;
}

/// @brief Emit a branch that checks a runtime error flag and handles failures.
/// @details Spills the @p err value to stack so it can be safely reloaded as a
///          64-bit operand, compares it against zero, and materialises a pair of
///          continuation/failure blocks named using @p labelStem.  When the flag
///          is non-zero, control transfers to a new failure block where
///          @p onFailure is invoked to generate diagnostics or cleanup code.  On
///          success, control resumes in the continuation block, preserving the
///          original block ordering.
/// @param err Value representing the runtime error flag to inspect.
/// @param loc Source location applied to generated instructions.
/// @param labelStem Stem used to derive unique failure/continuation block labels.
/// @param onFailure Callback invoked within the failure block to emit handling code.
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

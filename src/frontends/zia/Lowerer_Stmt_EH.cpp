//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Stmt_EH.cpp
/// @brief Exception handling statement lowering for the Zia IL lowerer.
///
/// @details Implements lowering of try/catch/finally and throw statements to
/// IL exception handling instructions (EhPush, EhPop, EhEntry, ResumeLabel).
///
/// ## IL Pattern for try/catch/finally:
///
/// ```
///   eh.push ^handler
///   [try body]
///   eh.pop
///   br ^finally_normal  (or ^after if no finally)
///
/// ^handler(%err: error, %tok: resumetok):
///   eh.entry
///   [catch body with %err bound]
///   [finally body — duplicated]
///   resume.label %tok, ^after
///
/// ^finally_normal:
///   [finally body]
///   br ^after
///
/// ^after:
///   [continuation]
/// ```
///
/// ## IL Pattern for typed catch — catch(e: ErrorType):
///
/// ```
/// ^handler(%err: error, %tok: resumetok):
///   eh.entry
///   %kind_i64 = trap.kind                // I64
///   %kind_i32 = err.get_kind %err        // I32 (must be in handler block)
///   %expected = const.i64 <kind_value>
///   %match = icmp.eq %kind_i64, %expected
///   cbr %match, ^catch_body(%err, %tok), ^rethrow(%kind_i32)
///
/// ^rethrow(%rk: I32):
///   trap.from_err %rk                    // re-raises with original kind
///
/// ^catch_body(%err: error, %tok: resumetok):
///   eh.entry                             // required: makes block a handler
///   [catch body with %err bound]
///   resume.label %tok, ^after
/// ```
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "frontends/zia/ZiaLocationScope.hpp"

namespace il::frontends::zia {

using namespace runtime;

/// @brief Map a typed-catch error type name to its TrapKind integer value.
/// @details Returns -1 for "Error" (catch-all) or unrecognised names.
static int trapKindFromName(const std::string &name) {
    if (name == "DivideByZero")
        return 0;
    if (name == "Overflow")
        return 1;
    if (name == "InvalidCast")
        return 2;
    if (name == "DomainError")
        return 3;
    if (name == "Bounds")
        return 4;
    if (name == "FileNotFound")
        return 5;
    if (name == "EOF")
        return 6;
    if (name == "IOError")
        return 7;
    if (name == "InvalidOperation")
        return 8;
    if (name == "RuntimeError")
        return 9;
    if (name == "Interrupt")
        return 10;
    if (name == "NetworkError")
        return 11;
    return -1; // "Error" catch-all or unknown
}

void Lowerer::lowerTryStmt(TryStmt *stmt) {
    ZiaLocationScope locScope(*this, stmt->loc);

    bool hasFinally = stmt->finallyBody != nullptr;
    bool hasCatch = stmt->catchBody != nullptr;

    // Determine if this is a typed catch (non-empty catchTypeName that isn't
    // the generic "Error" catch-all).
    bool isTypedCatch = !stmt->catchTypeName.empty() && stmt->catchTypeName != "Error";
    int expectedKind = isTypedCatch ? trapKindFromName(stmt->catchTypeName) : -1;

    // Create all blocks upfront to avoid stale indices after vector reallocation.
    // The handler block needs special params: %err (error type) and %tok (resumetok).
    size_t afterIdx = createBlock("after_try");

    // Create handler block with parameters via the builder directly
    // Handler receives: %err (error) and %tok (resumetok) per IL spec
    std::vector<il::core::Param> handlerParams;
    handlerParams.push_back({"err", Type(Type::Kind::Error)});
    handlerParams.push_back({"tok", Type(Type::Kind::ResumeTok)});

    builder_->createBlock(
        *currentFunc_, "handler_" + std::to_string(blockMgr_.nextBlockId()), handlerParams);
    size_t handlerIdx = currentFunc_->blocks.size() - 1;

    // Optional: finally_normal block (only if we have a finally clause)
    size_t finallyNormalIdx = 0;
    if (hasFinally) {
        finallyNormalIdx = createBlock("finally_normal");
    }

    // For typed catch: create extra blocks for the kind check.
    // catch_body must be a handler block (eh.entry + Error/ResumeTok params) so it
    // can use resume.label. rethrow receives the I32 kind as a branch argument.
    size_t catchBodyIdx = 0;
    size_t rethrowIdx = 0;
    if (isTypedCatch) {
        // catch_body: handler-style block with Error + ResumeTok params
        // Verifier requires params named exactly "err" and "tok"
        std::vector<il::core::Param> catchBodyParams;
        catchBodyParams.push_back({"err", Type(Type::Kind::Error)});
        catchBodyParams.push_back({"tok", Type(Type::Kind::ResumeTok)});
        builder_->createBlock(*currentFunc_,
                              "catch_body_" + std::to_string(blockMgr_.nextBlockId()),
                              catchBodyParams);
        catchBodyIdx = currentFunc_->blocks.size() - 1;

        // rethrow: receives I32 kind value as branch argument
        std::vector<il::core::Param> rethrowParams;
        rethrowParams.push_back({"rk", Type(Type::Kind::I32)});
        builder_->createBlock(
            *currentFunc_, "rethrow_" + std::to_string(blockMgr_.nextBlockId()), rethrowParams);
        rethrowIdx = currentFunc_->blocks.size() - 1;
    }

    // --- Emit eh.push in current block ---
    {
        il::core::Instr ehPushInstr;
        ehPushInstr.op = Opcode::EhPush;
        ehPushInstr.type = Type(Type::Kind::Void);
        ehPushInstr.labels.push_back(currentFunc_->blocks[handlerIdx].label);
        ehPushInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(std::move(ehPushInstr));
    }

    // --- Lower try body ---
    if (stmt->tryBody)
        lowerStmt(stmt->tryBody.get());

    // --- On normal exit from try: eh.pop + branch ---
    if (!isTerminated()) {
        il::core::Instr ehPopInstr;
        ehPopInstr.op = Opcode::EhPop;
        ehPopInstr.type = Type(Type::Kind::Void);
        ehPopInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(std::move(ehPopInstr));

        if (hasFinally)
            emitBr(finallyNormalIdx);
        else
            emitBr(afterIdx);
    }

    // --- Handler block: catch clause ---
    setBlock(handlerIdx);

    // Emit eh.entry marker
    {
        il::core::Instr ehEntryInstr;
        ehEntryInstr.op = Opcode::EhEntry;
        ehEntryInstr.type = Type(Type::Kind::Void);
        ehEntryInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(std::move(ehEntryInstr));
    }

    if (isTypedCatch) {
        // --- Typed catch: check trap kind before entering catch body ---
        // All err.get_* must happen in handler block (verifier constraint).

        const auto &handlerBp = currentFunc_->blocks[handlerIdx].params;
        Value errVal = Value::temp(handlerBp[0].id);
        Value tokVal = Value::temp(handlerBp[1].id);

        // %kind_i64 = trap.kind  (I64, no operands)
        unsigned kindI64Id = nextTempId();
        {
            il::core::Instr trapKindInstr;
            trapKindInstr.op = Opcode::TrapKind;
            trapKindInstr.type = Type(Type::Kind::I64);
            trapKindInstr.result = kindI64Id;
            trapKindInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(std::move(trapKindInstr));
        }

        // %kind_i32 = err.get_kind %err  (I32 — needed for rethrow, must be in handler)
        unsigned kindI32Id = nextTempId();
        {
            il::core::Instr getKindInstr;
            getKindInstr.op = Opcode::ErrGetKind;
            getKindInstr.type = Type(Type::Kind::I32);
            getKindInstr.result = kindI32Id;
            getKindInstr.operands = {errVal};
            getKindInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(std::move(getKindInstr));
        }

        // %match = icmp.eq %kind_i64, <expected>  (I1)
        Value kindI64Val = Value::temp(kindI64Id);
        Value expectedVal = Value::constInt(static_cast<int64_t>(expectedKind));
        Value match = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), kindI64Val, expectedVal);

        // No explicit eh.pop needed here — both prepareTrap() and dispatchTrap()
        // auto-pop the handler from the EH stack when dispatching. The handler
        // block always starts with a clean stack.

        // cbr %match, ^catch_body(%err, %tok), ^rethrow(%kind_i32)
        // Manual CBr construction to pass branch arguments
        {
            il::core::Instr cbrInstr;
            cbrInstr.op = Opcode::CBr;
            cbrInstr.type = Type(Type::Kind::Void);
            cbrInstr.operands.push_back(match);
            cbrInstr.labels.push_back(currentFunc_->blocks[catchBodyIdx].label);
            cbrInstr.labels.push_back(currentFunc_->blocks[rethrowIdx].label);
            // Branch args for catch_body: forward %err, %tok
            cbrInstr.brArgs.push_back({errVal, tokVal});
            // Branch args for rethrow: forward %kind_i32
            cbrInstr.brArgs.push_back({Value::temp(kindI32Id)});
            cbrInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(std::move(cbrInstr));
            blockMgr_.currentBlock()->terminated = true;
        }

        // --- Rethrow block: re-raise with original error kind ---
        setBlock(rethrowIdx);
        {
            // %rk is the block's first (and only) param — the I32 kind value
            const auto &rethrowBp = currentFunc_->blocks[rethrowIdx].params;
            Value rkVal = Value::temp(rethrowBp[0].id);

            il::core::Instr rethrowInstr;
            rethrowInstr.op = Opcode::TrapFromErr;
            rethrowInstr.type = Type(Type::Kind::I32); // verifier requires i32 result type
            rethrowInstr.operands = {rkVal};
            rethrowInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(std::move(rethrowInstr));
            blockMgr_.currentBlock()->terminated = true;
        }

        // --- Catch body block (handler-style: eh.entry + Error/ResumeTok params) ---
        setBlock(catchBodyIdx);
        {
            il::core::Instr ehEntryInstr;
            ehEntryInstr.op = Opcode::EhEntry;
            ehEntryInstr.type = Type(Type::Kind::Void);
            ehEntryInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(std::move(ehEntryInstr));
        }
    }

    // Bind catch variable (if named).
    // For typed catch we're in catch_body (which has its own Error/ResumeTok params).
    // For non-typed catch we're in the handler block itself.
    {
        size_t errBlockIdx = isTypedCatch ? catchBodyIdx : handlerIdx;
        const auto &bp = currentFunc_->blocks[errBlockIdx].params;

        if (!stmt->catchVar.empty() && !bp.empty()) {
            // Retrieve the thrown message via the runtime function.
            // For user throws, this returns the message from throw "msg".
            // For system errors (div by zero etc.), returns empty string.
            Value msgStr = emitCallRet(Type(Type::Kind::Str), "Viper.Error.GetThrowMsg", {});
            createSlot(stmt->catchVar, Type(Type::Kind::Str));
            storeToSlot(stmt->catchVar, msgStr, Type(Type::Kind::Str));
        }
    }

    // Lower catch body
    if (hasCatch && stmt->catchBody)
        lowerStmt(stmt->catchBody.get());

    // Duplicate finally body in handler path (if present)
    if (hasFinally && stmt->finallyBody && !isTerminated())
        lowerStmt(stmt->finallyBody.get());

    // Terminate with resume.label to ^after.
    // Use the current block's own %tok param (catch_body's for typed, handler's for non-typed).
    if (!isTerminated()) {
        size_t tokBlockIdx = isTypedCatch ? catchBodyIdx : handlerIdx;
        const auto &bp = currentFunc_->blocks[tokBlockIdx].params;
        Value resumeTok = Value::temp(bp[1].id); // %tok / %ctok is second param

        il::core::Instr resumeInstr;
        resumeInstr.op = Opcode::ResumeLabel;
        resumeInstr.type = Type(Type::Kind::Void);
        resumeInstr.operands.push_back(resumeTok);
        resumeInstr.labels.push_back(currentFunc_->blocks[afterIdx].label);
        resumeInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(std::move(resumeInstr));
        blockMgr_.currentBlock()->terminated = true;
    }

    // --- Finally normal block (normal path) ---
    if (hasFinally) {
        setBlock(finallyNormalIdx);
        if (stmt->finallyBody)
            lowerStmt(stmt->finallyBody.get());
        if (!isTerminated())
            emitBr(afterIdx);
    }

    // --- Continue at after_try ---
    setBlock(afterIdx);
}

void Lowerer::lowerThrowStmt(ThrowStmt *stmt) {
    ZiaLocationScope locScope(*this, stmt->loc);

    // Lower the thrown expression and store the message via the runtime
    // so catch handlers can retrieve it.
    if (stmt->value) {
        auto result = lowerExpr(stmt->value.get());
        TypeRef throwType = sema_.typeOf(stmt->value.get());
        Value msgStr = emitToString(result.value, throwType);

        // Store the message via rt_throw_msg_set for catch(e) retrieval.
        emitCall("Viper.Error.SetThrowMsg", {msgStr});
    }

    // Emit trap instruction to raise the exception.
    il::core::Instr trapInstr;
    trapInstr.op = Opcode::Trap;
    trapInstr.type = Type(Type::Kind::Void);
    trapInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(trapInstr));
    blockMgr_.currentBlock()->terminated = true;
}

} // namespace il::frontends::zia

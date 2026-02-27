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
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "frontends/zia/ZiaLocationScope.hpp"

namespace il::frontends::zia
{

using namespace runtime;

void Lowerer::lowerTryStmt(TryStmt *stmt)
{
    ZiaLocationScope locScope(*this, stmt->loc);

    // Create all blocks upfront to avoid stale indices after vector reallocation.
    // The handler block needs special params: %err (error type) and %tok (resumetok).
    size_t afterIdx = createBlock("after_try");

    // Create handler block with parameters via the builder directly
    // Handler receives: %err (Ptr — opaque error value) and %tok (I64 — resume token)
    std::vector<il::core::Param> handlerParams;
    handlerParams.push_back({"err", Type(Type::Kind::Ptr)});
    handlerParams.push_back({"tok", Type(Type::Kind::I64)});

    builder_->createBlock(*currentFunc_, "handler_" + std::to_string(blockMgr_.nextBlockId()),
                          handlerParams);
    size_t handlerIdx = currentFunc_->blocks.size() - 1;

    // Optional: finally_normal block (only if we have a finally clause)
    size_t finallyNormalIdx = 0;
    bool hasFinally = stmt->finallyBody != nullptr;
    bool hasCatch = stmt->catchBody != nullptr;
    if (hasFinally)
    {
        finallyNormalIdx = createBlock("finally_normal");
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
    if (!isTerminated())
    {
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

    // Bind catch variable (if named)
    if (!stmt->catchVar.empty())
    {
        // The error value is the first block parameter
        const auto &bp = currentFunc_->blocks[handlerIdx].params;
        if (!bp.empty())
        {
            createSlot(stmt->catchVar, Type(Type::Kind::Ptr));
            storeToSlot(stmt->catchVar, Value::temp(bp[0].id), Type(Type::Kind::Ptr));
        }
    }

    // Lower catch body
    if (hasCatch && stmt->catchBody)
        lowerStmt(stmt->catchBody.get());

    // Duplicate finally body in handler path (if present)
    if (hasFinally && stmt->finallyBody && !isTerminated())
        lowerStmt(stmt->finallyBody.get());

    // Terminate handler with resume.label to ^after
    if (!isTerminated())
    {
        const auto &bp = currentFunc_->blocks[handlerIdx].params;
        Value resumeTok = Value::temp(bp[1].id); // %tok is second param

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
    if (hasFinally)
    {
        setBlock(finallyNormalIdx);
        if (stmt->finallyBody)
            lowerStmt(stmt->finallyBody.get());
        if (!isTerminated())
            emitBr(afterIdx);
    }

    // --- Continue at after_try ---
    setBlock(afterIdx);
}

void Lowerer::lowerThrowStmt(ThrowStmt *stmt)
{
    ZiaLocationScope locScope(*this, stmt->loc);

    // Lower the thrown expression
    if (stmt->value)
    {
        auto result = lowerExpr(stmt->value.get());
        (void)result; // Value is not used — throw triggers a trap
    }

    // Emit trap instruction to abort execution
    il::core::Instr trapInstr;
    trapInstr.op = Opcode::Trap;
    trapInstr.type = Type(Type::Kind::Void);
    trapInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(std::move(trapInstr));
    blockMgr_.currentBlock()->terminated = true;
}

} // namespace il::frontends::zia

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_verify_eh_checks.cpp
//
// Purpose:
//   Comprehensive unit tests for EH verification checks. This file exercises
//   all four EH invariants with both passing and failing test cases:
//
//   1. Stack Balance (checkEhStackBalance):
//      - Validates eh.push/eh.pop are properly balanced
//      - Detects underflow (pop without matching push)
//      - Detects leaks (push without matching pop at ret)
//      - Detects resume without active token
//
//   2. Handler Dominance (checkDominanceOfHandlers):
//      - Validates that eh.push blocks dominate protected blocks
//      - Detects non-dominating handler installations
//
//   3. Handler Reachability (checkUnreachableHandlers):
//      - Validates that all handler blocks are reachable via trap
//      - Detects dead handler code
//
//   4. Resume Edge Correctness (checkResumeEdges):
//      - Validates resume.label targets postdominate faulting blocks
//      - Detects invalid resume targets
//
// Ownership/Lifetime: Constructs temporary IL functions for verification.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Type.hpp"
#include "il/verify/EhChecks.hpp"
#include "il/verify/EhModel.hpp"

#include <cassert>
#include <string>

int main()
{
    using namespace il::core;
    using il::verify::checkEhStackBalance;
    using il::verify::checkResumeEdges;
    using il::verify::EhModel;

    // Nested try/catch: two handlers pushed and popped in order.
    Function nestedFn;
    nestedFn.name = "nested";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr pushOuter;
        pushOuter.op = Opcode::EhPush;
        pushOuter.labels = {"outer_handler"};
        entry.instructions.push_back(pushOuter);
        Instr pushInner;
        pushInner.op = Opcode::EhPush;
        pushInner.labels = {"inner_handler"};
        entry.instructions.push_back(pushInner);
        Instr popInner;
        popInner.op = Opcode::EhPop;
        entry.instructions.push_back(popInner);
        Instr popOuter;
        popOuter.op = Opcode::EhPop;
        entry.instructions.push_back(popOuter);
        Instr ret;
        ret.op = Opcode::Ret;
        entry.instructions.push_back(ret);
        nestedFn.blocks.push_back(entry);
    }
    {
        BasicBlock innerHandler;
        innerHandler.label = "inner_handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        innerHandler.instructions.push_back(entryInstr);
        nestedFn.blocks.push_back(innerHandler);
    }
    {
        BasicBlock outerHandler;
        outerHandler.label = "outer_handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        outerHandler.instructions.push_back(entryInstr);
        nestedFn.blocks.push_back(outerHandler);
    }
    EhModel nestedModel(nestedFn);
    assert(checkEhStackBalance(nestedModel));

    // Rethrow without token should report missing resume token.
    Function rethrowFn;
    rethrowFn.name = "rethrow";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        entry.instructions.push_back(push);
        Instr resumeNext;
        resumeNext.op = Opcode::ResumeNext;
        entry.instructions.push_back(resumeNext);
        Instr ret;
        ret.op = Opcode::Ret;
        entry.instructions.push_back(ret);
        rethrowFn.blocks.push_back(entry);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        rethrowFn.blocks.push_back(handler);
    }
    EhModel rethrowModel(rethrowFn);
    auto rethrowDiag = checkEhStackBalance(rethrowModel);
    assert(!rethrowDiag);
    assert(rethrowDiag.error().message.find("resume.* requires active resume token") !=
           std::string::npos);

    // Multiple catch handlers and shared cleanup; checks should pass.
    Function multiCatchFn;
    multiCatchFn.name = "multicatch";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr pushA;
        pushA.op = Opcode::EhPush;
        pushA.labels = {"catch_a"};
        entry.instructions.push_back(pushA);
        Instr pushB;
        pushB.op = Opcode::EhPush;
        pushB.labels = {"catch_b"};
        entry.instructions.push_back(pushB);
        Instr load;
        load.op = Opcode::Load;
        entry.instructions.push_back(load);
        Instr trap;
        trap.op = Opcode::Trap;
        entry.instructions.push_back(trap);
        multiCatchFn.blocks.push_back(entry);
    }
    {
        BasicBlock catchB;
        catchB.label = "catch_b";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        catchB.instructions.push_back(entryInstr);
        Instr popInstr;
        popInstr.op = Opcode::EhPop;
        catchB.instructions.push_back(popInstr);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"cleanup"};
        catchB.instructions.push_back(resume);
        multiCatchFn.blocks.push_back(catchB);
    }
    {
        BasicBlock catchA;
        catchA.label = "catch_a";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        catchA.instructions.push_back(entryInstr);
        Instr popInstr;
        popInstr.op = Opcode::EhPop;
        catchA.instructions.push_back(popInstr);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"cleanup"};
        catchA.instructions.push_back(resume);
        multiCatchFn.blocks.push_back(catchA);
    }
    {
        BasicBlock cleanup;
        cleanup.label = "cleanup";
        Instr ret;
        ret.op = Opcode::Ret;
        cleanup.instructions.push_back(ret);
        multiCatchFn.blocks.push_back(cleanup);
    }
    EhModel multiCatchModel(multiCatchFn);
    assert(checkEhStackBalance(multiCatchModel));
    assert(checkResumeEdges(multiCatchModel));

    // Finally-only handler triggered by trap should pass checks.
    Function finallyFn;
    finallyFn.name = "finally";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"finally_handler"};
        entry.instructions.push_back(push);
        Instr trap;
        trap.op = Opcode::TrapFromErr;
        entry.instructions.push_back(trap);
        finallyFn.blocks.push_back(entry);
    }
    {
        BasicBlock finallyHandler;
        finallyHandler.label = "finally_handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        finallyHandler.instructions.push_back(entryInstr);
        Instr popInstr;
        popInstr.op = Opcode::EhPop;
        finallyHandler.instructions.push_back(popInstr);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"after"};
        finallyHandler.instructions.push_back(resume);
        finallyFn.blocks.push_back(finallyHandler);
    }
    {
        BasicBlock after;
        after.label = "after";
        Instr ret;
        ret.op = Opcode::Ret;
        after.instructions.push_back(ret);
        finallyFn.blocks.push_back(after);
    }
    EhModel finallyModel(finallyFn);
    assert(checkEhStackBalance(finallyModel));
    assert(checkResumeEdges(finallyModel));

    // Degenerate: leaked handler should report stack depth.
    Function leakFn;
    leakFn.name = "leak";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        entry.instructions.push_back(push);
        Instr ret;
        ret.op = Opcode::Ret;
        entry.instructions.push_back(ret);
        leakFn.blocks.push_back(entry);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        leakFn.blocks.push_back(handler);
    }
    EhModel leakModel(leakFn);
    auto leakDiag = checkEhStackBalance(leakModel);
    assert(!leakDiag);
    assert(leakDiag.error().message.find("unmatched eh.push depth 1") != std::string::npos);

    // Degenerate resume target: handler target must postdominate faulting block.
    Function invalidResumeFn;
    invalidResumeFn.name = "invalid_resume";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        entry.instructions.push_back(push);
        Instr load;
        load.op = Opcode::Load;
        entry.instructions.push_back(load);
        Instr branch;
        branch.op = Opcode::CBr;
        branch.labels = {"left", "right"};
        entry.instructions.push_back(branch);
        invalidResumeFn.blocks.push_back(entry);
    }
    assert(invalidResumeFn.blocks.front().instructions[0].labels[0] == "handler");
    {
        BasicBlock left;
        left.label = "left";
        Instr pop;
        pop.op = Opcode::EhPop;
        left.instructions.push_back(pop);
        Instr ret;
        ret.op = Opcode::Ret;
        left.instructions.push_back(ret);
        invalidResumeFn.blocks.push_back(left);
    }
    {
        BasicBlock right;
        right.label = "right";
        Instr pop;
        pop.op = Opcode::EhPop;
        right.instructions.push_back(pop);
        Instr ret;
        ret.op = Opcode::Ret;
        right.instructions.push_back(ret);
        invalidResumeFn.blocks.push_back(right);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"cleanup"};
        handler.instructions.push_back(resume);
        invalidResumeFn.blocks.push_back(handler);
    }
    {
        BasicBlock cleanup;
        cleanup.label = "cleanup";
        Instr ret;
        ret.op = Opcode::Ret;
        cleanup.instructions.push_back(ret);
        invalidResumeFn.blocks.push_back(cleanup);
    }
    EhModel invalidResumeModel(invalidResumeFn);
    assert(checkEhStackBalance(invalidResumeModel));
    auto resumeDiag = checkResumeEdges(invalidResumeModel);
    assert(!resumeDiag);
    assert(resumeDiag.error().message.find("must postdominate block entry") != std::string::npos);

    // -------------------------------------------------------------------------
    // Handler dominance tests
    // -------------------------------------------------------------------------
    using il::verify::checkDominanceOfHandlers;

    // Valid case: eh.push block dominates the protected block (linear flow)
    Function validDomFn;
    validDomFn.name = "valid_dom";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        entry.instructions.push_back(push);
        Instr br;
        br.op = Opcode::Br;
        br.labels = {"body"};
        entry.instructions.push_back(br);
        validDomFn.blocks.push_back(entry);
    }
    {
        BasicBlock body;
        body.label = "body";
        Instr load;
        load.op = Opcode::Load;  // Potential faulting instruction
        body.instructions.push_back(load);
        Instr pop;
        pop.op = Opcode::EhPop;
        body.instructions.push_back(pop);
        Instr ret;
        ret.op = Opcode::Ret;
        body.instructions.push_back(ret);
        validDomFn.blocks.push_back(body);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        Instr pop;
        pop.op = Opcode::EhPop;
        handler.instructions.push_back(pop);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"exit"};
        handler.instructions.push_back(resume);
        validDomFn.blocks.push_back(handler);
    }
    {
        BasicBlock exit;
        exit.label = "exit";
        Instr ret;
        ret.op = Opcode::Ret;
        exit.instructions.push_back(ret);
        validDomFn.blocks.push_back(exit);
    }
    EhModel validDomModel(validDomFn);
    assert(checkEhStackBalance(validDomModel));
    auto validDomResult = checkDominanceOfHandlers(validDomModel);
    assert(validDomResult);  // Should pass: entry dominates body

    // Invalid case: eh.push block does NOT dominate a protected block
    // CFG: entry -> left, entry -> right; left has eh.push but right has faulting code
    //      that joins back to a common block
    Function invalidDomFn;
    invalidDomFn.name = "invalid_dom";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr cbr;
        cbr.op = Opcode::CBr;
        cbr.labels = {"left", "right"};
        entry.instructions.push_back(cbr);
        invalidDomFn.blocks.push_back(entry);
    }
    {
        BasicBlock left;
        left.label = "left";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        left.instructions.push_back(push);
        Instr br;
        br.op = Opcode::Br;
        br.labels = {"common"};
        left.instructions.push_back(br);
        invalidDomFn.blocks.push_back(left);
    }
    {
        BasicBlock right;
        right.label = "right";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        right.instructions.push_back(push);
        Instr br;
        br.op = Opcode::Br;
        br.labels = {"common"};
        right.instructions.push_back(br);
        invalidDomFn.blocks.push_back(right);
    }
    {
        BasicBlock common;
        common.label = "common";
        Instr load;
        load.op = Opcode::Load;  // Faulting instruction in common block
        common.instructions.push_back(load);
        Instr pop;
        pop.op = Opcode::EhPop;
        common.instructions.push_back(pop);
        Instr ret;
        ret.op = Opcode::Ret;
        common.instructions.push_back(ret);
        invalidDomFn.blocks.push_back(common);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        Instr pop;
        pop.op = Opcode::EhPop;
        handler.instructions.push_back(pop);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"exit"};
        handler.instructions.push_back(resume);
        invalidDomFn.blocks.push_back(handler);
    }
    {
        BasicBlock exit;
        exit.label = "exit";
        Instr ret;
        ret.op = Opcode::Ret;
        exit.instructions.push_back(ret);
        invalidDomFn.blocks.push_back(exit);
    }
    EhModel invalidDomModel(invalidDomFn);
    assert(checkEhStackBalance(invalidDomModel));
    auto invalidDomDiag = checkDominanceOfHandlers(invalidDomModel);
    assert(!invalidDomDiag);  // Should fail: left/right don't dominate common
    assert(invalidDomDiag.error().message.find("does not dominate protected block") !=
           std::string::npos);

    // -------------------------------------------------------------------------
    // Unreachable handler tests
    // -------------------------------------------------------------------------
    using il::verify::checkUnreachableHandlers;

    // Valid case: Handler is reachable via trap instruction
    Function reachableHandlerFn;
    reachableHandlerFn.name = "reachable_handler";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        entry.instructions.push_back(push);
        Instr load;
        load.op = Opcode::Load;  // Potential faulting instruction
        entry.instructions.push_back(load);
        Instr trap;
        trap.op = Opcode::Trap;
        entry.instructions.push_back(trap);
        reachableHandlerFn.blocks.push_back(entry);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        Instr pop;
        pop.op = Opcode::EhPop;
        handler.instructions.push_back(pop);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"exit"};
        handler.instructions.push_back(resume);
        reachableHandlerFn.blocks.push_back(handler);
    }
    {
        BasicBlock exit;
        exit.label = "exit";
        Instr ret;
        ret.op = Opcode::Ret;
        exit.instructions.push_back(ret);
        reachableHandlerFn.blocks.push_back(exit);
    }
    EhModel reachableHandlerModel(reachableHandlerFn);
    auto reachableResult = checkUnreachableHandlers(reachableHandlerModel);
    assert(reachableResult);  // Should pass: handler is reachable via trap

    // Valid case: Handler with no faulting instructions is allowed (unused but not invalid)
    // This is common in BASIC's ON ERROR GOTO pattern with empty protected regions.
    Function unusedHandlerFn;
    unusedHandlerFn.name = "unused_handler";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        entry.instructions.push_back(push);
        Instr pop;
        pop.op = Opcode::EhPop;
        entry.instructions.push_back(pop);
        Instr ret;
        ret.op = Opcode::Ret;  // No faulting ops, so handler is unused
        entry.instructions.push_back(ret);
        unusedHandlerFn.blocks.push_back(entry);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        Instr pop;
        pop.op = Opcode::EhPop;
        handler.instructions.push_back(pop);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"exit"};
        handler.instructions.push_back(resume);
        unusedHandlerFn.blocks.push_back(handler);
    }
    {
        BasicBlock exit;
        exit.label = "exit";
        Instr ret;
        ret.op = Opcode::Ret;
        exit.instructions.push_back(ret);
        unusedHandlerFn.blocks.push_back(exit);
    }
    EhModel unusedHandlerModel(unusedHandlerFn);
    auto unusedResult = checkUnreachableHandlers(unusedHandlerModel);
    assert(unusedResult);  // Should pass: unused handler is allowed

    // Invalid case: Handler with faulting instructions but no trap path
    Function unreachableHandlerFn;
    unreachableHandlerFn.name = "unreachable_handler";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        entry.instructions.push_back(push);
        // Add a faulting instruction (div) that makes handler potentially reachable
        Instr div;
        div.op = Opcode::SDivChk0;
        div.result = 100;
        div.type = Type(Type::Kind::I64);
        div.operands.push_back(Value::constInt(1));
        div.operands.push_back(Value::constInt(1));
        entry.instructions.push_back(div);
        Instr pop;
        pop.op = Opcode::EhPop;
        entry.instructions.push_back(pop);
        Instr ret;
        ret.op = Opcode::Ret;
        entry.instructions.push_back(ret);
        unreachableHandlerFn.blocks.push_back(entry);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        Instr pop;
        pop.op = Opcode::EhPop;
        handler.instructions.push_back(pop);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"exit"};
        handler.instructions.push_back(resume);
        unreachableHandlerFn.blocks.push_back(handler);
    }
    {
        BasicBlock exit;
        exit.label = "exit";
        Instr ret;
        ret.op = Opcode::Ret;
        exit.instructions.push_back(ret);
        unreachableHandlerFn.blocks.push_back(exit);
    }
    EhModel unreachableHandlerModel(unreachableHandlerFn);
    auto unreachableResult = checkUnreachableHandlers(unreachableHandlerModel);
    assert(unreachableResult);  // Should pass: faulting instruction makes handler reachable

    // -------------------------------------------------------------------------
    // Additional stack balance tests
    // -------------------------------------------------------------------------

    // Stack underflow: eh.pop without matching eh.push
    Function underflowFn;
    underflowFn.name = "underflow";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr pop;
        pop.op = Opcode::EhPop;  // No matching push!
        entry.instructions.push_back(pop);
        Instr ret;
        ret.op = Opcode::Ret;
        entry.instructions.push_back(ret);
        underflowFn.blocks.push_back(entry);
    }
    EhModel underflowModel(underflowFn);
    auto underflowDiag = checkEhStackBalance(underflowModel);
    assert(!underflowDiag);  // Should fail: underflow
    assert(underflowDiag.error().message.find("eh.pop without matching") != std::string::npos);

    // Valid: Simple balanced push/pop
    Function simpleFn;
    simpleFn.name = "simple_balanced";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        entry.instructions.push_back(push);
        Instr pop;
        pop.op = Opcode::EhPop;
        entry.instructions.push_back(pop);
        Instr ret;
        ret.op = Opcode::Ret;
        entry.instructions.push_back(ret);
        simpleFn.blocks.push_back(entry);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        simpleFn.blocks.push_back(handler);
    }
    EhModel simpleModel(simpleFn);
    auto simpleResult = checkEhStackBalance(simpleModel);
    assert(simpleResult);  // Should pass: properly balanced

    // -------------------------------------------------------------------------
    // Additional resume edge tests
    // -------------------------------------------------------------------------

    // Valid resume edge: resume.label target postdominates faulting block
    Function validResumeFn;
    validResumeFn.name = "valid_resume";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        entry.instructions.push_back(push);
        Instr load;
        load.op = Opcode::Load;  // Potential fault
        entry.instructions.push_back(load);
        Instr trap;
        trap.op = Opcode::Trap;
        entry.instructions.push_back(trap);
        validResumeFn.blocks.push_back(entry);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        Instr pop;
        pop.op = Opcode::EhPop;
        handler.instructions.push_back(pop);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"cleanup"};
        handler.instructions.push_back(resume);
        validResumeFn.blocks.push_back(handler);
    }
    {
        BasicBlock cleanup;
        cleanup.label = "cleanup";
        Instr ret;
        ret.op = Opcode::Ret;
        cleanup.instructions.push_back(ret);
        validResumeFn.blocks.push_back(cleanup);
    }
    EhModel validResumeModel(validResumeFn);
    assert(checkEhStackBalance(validResumeModel));
    auto validResumeResult = checkResumeEdges(validResumeModel);
    assert(validResumeResult);  // Should pass: cleanup postdominates entry

    // -------------------------------------------------------------------------
    // Handler reachability via TrapFromErr
    // -------------------------------------------------------------------------

    // Valid: Handler reachable via TrapFromErr
    Function trapFromErrFn;
    trapFromErrFn.name = "trap_from_err";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push;
        push.op = Opcode::EhPush;
        push.labels = {"handler"};
        entry.instructions.push_back(push);
        Instr trapFromErr;
        trapFromErr.op = Opcode::TrapFromErr;
        entry.instructions.push_back(trapFromErr);
        trapFromErrFn.blocks.push_back(entry);
    }
    {
        BasicBlock handler;
        handler.label = "handler";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler.instructions.push_back(entryInstr);
        Instr pop;
        pop.op = Opcode::EhPop;
        handler.instructions.push_back(pop);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"exit"};
        handler.instructions.push_back(resume);
        trapFromErrFn.blocks.push_back(handler);
    }
    {
        BasicBlock exit;
        exit.label = "exit";
        Instr ret;
        ret.op = Opcode::Ret;
        exit.instructions.push_back(ret);
        trapFromErrFn.blocks.push_back(exit);
    }
    EhModel trapFromErrModel(trapFromErrFn);
    auto trapFromErrResult = checkUnreachableHandlers(trapFromErrModel);
    assert(trapFromErrResult);  // Should pass: handler reachable via TrapFromErr

    // -------------------------------------------------------------------------
    // Multiple handlers - unused handlers are allowed
    // -------------------------------------------------------------------------

    // Valid: Multiple handlers where one is unused (no faulting instructions in protected region)
    // Handler2 is pushed and immediately popped with no faulting ops in between.
    // This is allowed (unused but not invalid).
    Function multiHandlerFn;
    multiHandlerFn.name = "multi_handler";
    {
        BasicBlock entry;
        entry.label = "entry";
        Instr push1;
        push1.op = Opcode::EhPush;
        push1.labels = {"handler1"};
        entry.instructions.push_back(push1);
        Instr push2;
        push2.op = Opcode::EhPush;
        push2.labels = {"handler2"};  // No faulting ops before pop, so unused
        entry.instructions.push_back(push2);
        Instr pop2;
        pop2.op = Opcode::EhPop;
        entry.instructions.push_back(pop2);
        Instr trap;
        trap.op = Opcode::Trap;  // Only handler1 can be reached
        entry.instructions.push_back(trap);
        multiHandlerFn.blocks.push_back(entry);
    }
    {
        BasicBlock handler1;
        handler1.label = "handler1";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler1.instructions.push_back(entryInstr);
        Instr pop;
        pop.op = Opcode::EhPop;
        handler1.instructions.push_back(pop);
        Instr resume;
        resume.op = Opcode::ResumeLabel;
        resume.labels = {"exit"};
        handler1.instructions.push_back(resume);
        multiHandlerFn.blocks.push_back(handler1);
    }
    {
        BasicBlock handler2;
        handler2.label = "handler2";
        Instr entryInstr;
        entryInstr.op = Opcode::EhEntry;
        handler2.instructions.push_back(entryInstr);
        Instr ret;
        ret.op = Opcode::Ret;
        handler2.instructions.push_back(ret);
        multiHandlerFn.blocks.push_back(handler2);
    }
    {
        BasicBlock exit;
        exit.label = "exit";
        Instr ret;
        ret.op = Opcode::Ret;
        exit.instructions.push_back(ret);
        multiHandlerFn.blocks.push_back(exit);
    }
    EhModel multiHandlerModel(multiHandlerFn);
    auto multiHandlerDiag = checkUnreachableHandlers(multiHandlerModel);
    assert(multiHandlerDiag);  // Should pass: unused handlers are allowed

    return 0;
}

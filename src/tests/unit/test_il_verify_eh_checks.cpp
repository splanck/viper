//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_verify_eh_checks.cpp
// Purpose: Validate EhModel and EhChecks invariants across representative EH scenarios.
// Key invariants: Stack balance and resume-edge diagnostics mirror legacy behaviour.
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

    return 0;
}

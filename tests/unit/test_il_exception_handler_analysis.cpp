// File: tests/unit/test_il_exception_handler_analysis.cpp
// Purpose: Exercise exception-handler analysis helpers for success and failure scenarios.
// Key invariants: Handler signatures and EH stack balance diagnostics behave as expected.
// Ownership/Lifetime: Uses temporary IL objects constructed in-place.
// Links: docs/il-guide.md#reference

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/verify/EhChecks.hpp"
#include "il/verify/EhModel.hpp"
#include "il/verify/ExceptionHandlerAnalysis.hpp"

#include <cassert>
#include <string>

int main()
{
    using namespace il::core;
    using il::verify::analyzeHandlerBlock;
    using il::verify::checkEhStackBalance;
    using il::verify::EhModel;
    using il::verify::HandlerSignature;

    Function fn;
    fn.name = "f";

    BasicBlock handler;
    handler.label = "handler";
    Param errParam;
    errParam.name = "err";
    errParam.type = Type(Type::Kind::Error);
    errParam.id = 1u;
    Param tokParam;
    tokParam.name = "tok";
    tokParam.type = Type(Type::Kind::ResumeTok);
    tokParam.id = 2u;
    handler.params = {errParam, tokParam};

    Instr entry;
    entry.op = Opcode::EhEntry;
    handler.instructions.push_back(entry);

    auto handlerSig = analyzeHandlerBlock(fn, handler);
    assert(handlerSig);
    auto handlerSigOpt = handlerSig.value();
    assert(handlerSigOpt.has_value());
    HandlerSignature sig = *handlerSigOpt;
    assert(sig.errorParam == errParam.id);
    assert(sig.resumeTokenParam == tokParam.id);

    BasicBlock malformed;
    malformed.label = "bad";
    malformed.params = handler.params;
    Instr wrongFront;
    wrongFront.op = Opcode::Ret;
    malformed.instructions.push_back(wrongFront);
    malformed.instructions.push_back(entry);
    auto malformedSig = analyzeHandlerBlock(fn, malformed);
    assert(!malformedSig);
    assert(malformedSig.error().message.find("eh.entry only allowed") != std::string::npos);

    BasicBlock nonHandler;
    nonHandler.label = "body";
    Instr add;
    add.op = Opcode::Add;
    nonHandler.instructions.push_back(add);
    auto nonHandlerSig = analyzeHandlerBlock(fn, nonHandler);
    assert(nonHandlerSig);
    auto nonHandlerOpt = nonHandlerSig.value();
    assert(!nonHandlerOpt.has_value());

    Function stackFn;
    stackFn.name = "stack";
    BasicBlock entryBlock;
    entryBlock.label = "entry";
    Instr pop;
    pop.op = Opcode::EhPop;
    entryBlock.instructions.push_back(pop);
    Instr term;
    term.op = Opcode::Ret;
    entryBlock.instructions.push_back(term);
    stackFn.blocks.push_back(entryBlock);
    EhModel stackModel(stackFn);
    auto stackDiag = checkEhStackBalance(stackModel);
    assert(!stackDiag);
    assert(stackDiag.error().message.find("eh.pop without matching") != std::string::npos);

    Function balancedFn;
    balancedFn.name = "balanced";
    BasicBlock balancedEntry;
    balancedEntry.label = "entry";
    Instr push;
    push.op = Opcode::EhPush;
    balancedEntry.instructions.push_back(push);
    Instr popOk;
    popOk.op = Opcode::EhPop;
    balancedEntry.instructions.push_back(popOk);
    balancedEntry.instructions.push_back(term);
    balancedFn.blocks.push_back(balancedEntry);
    EhModel balancedModel(balancedFn);
    auto balancedResult = checkEhStackBalance(balancedModel);
    assert(balancedResult);

    return 0;
}

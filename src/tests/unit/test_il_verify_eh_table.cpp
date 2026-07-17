//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_il_verify_eh_table.cpp
// Purpose: Validate verifier diagnostics for exception handler table instructions.
// Key invariants: EH stack operations enforce successor arity and resume token typing.
// Ownership/Lifetime: Constructs IL modules locally for verification and discards after use.
// Links: docs/il/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/api/expected_api.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Param.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "il/verify/Verifier.hpp"
#include "support/diag_expected.hpp"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>

namespace {

using namespace il::core;

Module buildEhFixture() {
    Module module;

    Function fn;
    fn.name = "eh_demo";
    fn.retType = Type(Type::Kind::Void);

    BasicBlock entry;
    entry.label = "entry";

    Instr push;
    push.op = Opcode::EhPush;
    push.labels.push_back("handler");

    // Add a potentially faulting instruction to make the handler reachable
    Instr div;
    div.op = Opcode::SDivChk0;
    div.result = 100;
    div.type = Type(Type::Kind::I64);
    div.operands.push_back(Value::constInt(1));
    div.operands.push_back(Value::constInt(1));

    Instr pop;
    pop.op = Opcode::EhPop;

    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);

    entry.instructions.push_back(push);
    entry.instructions.push_back(div);
    entry.instructions.push_back(pop);
    entry.instructions.push_back(ret);
    entry.terminated = true;

    BasicBlock handler;
    handler.label = "handler";

    Param errParam;
    errParam.name = "err";
    errParam.id = 10;
    errParam.type = Type(Type::Kind::Error);

    Param tokParam;
    tokParam.name = "tok";
    tokParam.id = 11;
    tokParam.type = Type(Type::Kind::ResumeTok);

    handler.params.push_back(errParam);
    handler.params.push_back(tokParam);

    Instr entryInstr;
    entryInstr.op = Opcode::EhEntry;

    Instr resumeSame;
    resumeSame.op = Opcode::ResumeSame;
    resumeSame.operands.push_back(Value::temp(tokParam.id));

    handler.instructions.push_back(entryInstr);
    handler.instructions.push_back(resumeSame);
    handler.terminated = true;

    fn.blocks.push_back(entry);
    fn.blocks.push_back(handler);

    module.functions.push_back(fn);

    return module;
}

/// @brief Parse a textual IL module for verifier regression tests.
/// @details The helper asserts that parsing succeeds so individual test cases
///          can focus on verifier behaviour. Verification is intentionally left
///          to the caller because both positive and negative cases use it.
/// @param source Complete textual IL module.
/// @return Parsed module ready for verification.
Module parseModuleOrDie(const std::string &source) {
    std::istringstream input(source);
    Module module;
    auto parse = il::api::v2::parse_text_expected(input, module);
    assert(parse && "test IL should parse");
    return module;
}

std::string verifyAndCaptureMessage(Module &module) {
    auto result = il::verify::Verifier::verify(module);
    assert(!result && "verification should fail for negative cases");
    return result.error().message;
}

} // namespace

int main() {
    {
        Module module = buildEhFixture();

        std::ostringstream diag;
        auto result = il::verify::Verifier::verify(module);
        if (!result) {
            const std::string diagMessage = result.error().message;
            std::fprintf(stderr, "%s\n", diagMessage.c_str());
            il::support::printDiag(result.error(), diag);
        }

        assert(result && "balanced push/pop with resume token should verify");
        assert(diag.str().empty());
    }

    {
        Module module = buildEhFixture();
        auto &handler = module.functions.front().blocks[1];
        Instr redefine;
        redefine.result = handler.params[1].id;
        redefine.op = Opcode::IAddOvf;
        redefine.type = Type(Type::Kind::I64);
        redefine.operands.push_back(Value::constInt(0));
        redefine.operands.push_back(Value::constInt(0));
        handler.instructions.insert(handler.instructions.begin() + 1, redefine);

        const std::string message = verifyAndCaptureMessage(module);
        if (message.find("duplicate temp %11") == std::string::npos) {
            std::fprintf(stderr, "%s\n", message.c_str());
        }
        assert(message.find("duplicate temp %11") != std::string::npos);
    }

    {
        Module module = buildEhFixture();
        auto &entry = module.functions.front().blocks.front();
        auto &pushInstr = entry.instructions.front();
        pushInstr.labels.push_back("duplicate");

        const std::string message = verifyAndCaptureMessage(module);
        if (message.find("expected 1 successor") == std::string::npos) {
            std::fprintf(stderr, "%s\n", message.c_str());
        }
        assert(message.find("expected 1 successor") != std::string::npos);
    }

    {
        Module module = parseModuleOrDie("il 0.3.0\n"
                                         "func @typed_catch_continuation() -> void {\n"
                                         "entry:\n"
                                         "  eh.push ^dispatch\n"
                                         "  trap\n"
                                         "handler ^dispatch(%err:Error, %tok:ResumeTok):\n"
                                         "  eh.entry\n"
                                         "  br ^catch(%err, %tok)\n"
                                         "handler ^catch(%err:Error, %tok:ResumeTok):\n"
                                         "  eh.entry\n"
                                         "  resume.label %tok, ^done\n"
                                         "done:\n"
                                         "  ret\n"
                                         "}\n");
        auto result = il::verify::Verifier::verify(module);
        assert(result && "forwarding the active token to a handler continuation should verify");
    }

    {
        Module module = parseModuleOrDie("il 0.3.0\n"
                                         "func @direct_handler_entry(Error %err, ResumeTok %fake) "
                                         "-> void {\n"
                                         "entry:\n"
                                         "  br ^handler(%err, %fake)\n"
                                         "handler ^handler(%err:Error, %tok:ResumeTok):\n"
                                         "  eh.entry\n"
                                         "  resume.same %tok\n"
                                         "}\n");
        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("handler block entry requires active resume token forwarding") !=
               std::string::npos);
    }

    {
        Module module = parseModuleOrDie("il 0.3.0\n"
                                         "func @mismatched_handler_token(Error %outerErr, "
                                         "ResumeTok %fake) -> void {\n"
                                         "entry:\n"
                                         "  eh.push ^dispatch\n"
                                         "  trap\n"
                                         "handler ^dispatch(%err:Error, %tok:ResumeTok):\n"
                                         "  eh.entry\n"
                                         "  br ^catch(%err, %fake)\n"
                                         "handler ^catch(%err:Error, %tok:ResumeTok):\n"
                                         "  eh.entry\n"
                                         "  resume.same %tok\n"
                                         "}\n");
        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("resume token does not match active handler provenance") !=
               std::string::npos);
    }

    {
        Module module = parseModuleOrDie("il 0.3.0\n"
                                         "func @resume_token_escape() -> ResumeTok {\n"
                                         "entry:\n"
                                         "  eh.push ^handler\n"
                                         "  trap\n"
                                         "handler ^handler(%err:Error, %tok:ResumeTok):\n"
                                         "  eh.entry\n"
                                         "  ret %tok\n"
                                         "}\n");
        const std::string message = verifyAndCaptureMessage(module);
        assert(message.find("resumetok may only be forwarded") != std::string::npos);
    }

    return 0;
}

// File: tests/frontends/basic/LowererSelectCaseTests.cpp
// Purpose: Validate Lowerer SELECT CASE lowering for string, range, and CASE ELSE scenarios.
// Key invariants: String selectors avoid SwitchI32, range arms emit range comparisons, CASE ELSE
//                 lowers to the default branch.
// Ownership/Lifetime: Test owns parser, lowerer, and produced module per scenario.
// Links: docs/codemap.md

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "il/core/Instr.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

const il::core::Function *findMain(const il::core::Module &module)
{
    for (const auto &fn : module.functions)
    {
        if (fn.name == "main")
            return &fn;
    }
    return nullptr;
}

const il::core::BasicBlock *findBlockByLabel(const il::core::Function &fn, const std::string &label)
{
    for (const auto &bb : fn.blocks)
    {
        if (bb.label == label)
            return &bb;
    }
    return nullptr;
}

bool blockPrintsConstant(const il::core::BasicBlock &bb, long long value)
{
    for (const auto &instr : bb.instructions)
    {
        if (instr.op != il::core::Opcode::Call)
            continue;
        if (instr.callee != "rt_print_i64" && instr.callee != "Viper.Console.PrintI64")
            continue;
        if (instr.operands.empty())
            continue;
        const il::core::Value &operand = instr.operands.front();
        if (operand.kind == il::core::Value::Kind::ConstInt && operand.i64 == value)
            return true;
    }
    return false;
}

const il::core::Instr *findSwitch(const il::core::Function &fn)
{
    for (const auto &bb : fn.blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == il::core::Opcode::SwitchI32)
                return &instr;
        }
    }
    return nullptr;
}

il::core::Module lowerSnippet(const std::string &src)
{
    SourceManager sm;
    uint32_t fid = sm.addFile("select_case.bas");
    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    Lowerer lowerer;
    return lowerer.lowerProgram(*program);
}

struct LowerWithDiagnosticsResult
{
    il::core::Module module;
    size_t errorCount = 0;
    std::string diagnostics;
};

LowerWithDiagnosticsResult lowerSnippetWithDiagnostics(const std::string &src)
{
    SourceManager sm;
    uint32_t fid = sm.addFile("select_case.bas");

    il::support::DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();
    assert(program);

    Lowerer lowerer;
    lowerer.setDiagnosticEmitter(&emitter);
    il::core::Module module = lowerer.lowerProgram(*program);

    std::ostringstream oss;
    emitter.printAll(oss);
    return {std::move(module), emitter.errorCount(), oss.str()};
}

} // namespace

int main()
{
    {
        const std::string src = "10 SELECT CASE \"foo\"\n"
                                "20 CASE \"foo\"\n"
                                "30 PRINT \"foo\"\n"
                                "40 CASE \"bar\"\n"
                                "50 PRINT \"bar\"\n"
                                "60 END SELECT\n"
                                "70 END\n";
        il::core::Module module = lowerSnippet(src);
        const il::core::Function *mainFn = findMain(module);
        assert(mainFn);

        size_t eqCalls = 0;
        bool sawSwitch = false;
        for (const auto &bb : mainFn->blocks)
        {
            for (const auto &instr : bb.instructions)
            {
                if (instr.op == il::core::Opcode::Call && instr.callee == "rt_str_eq")
                    ++eqCalls;
                if (instr.op == il::core::Opcode::SwitchI32)
                    sawSwitch = true;
            }
        }
        assert(eqCalls == 2);
        assert(!sawSwitch);
    }

    {
        const std::string src = "10 LET X = 5\n"
                                "20 SELECT CASE X\n"
                                "30 CASE 1 TO 3\n"
                                "40 PRINT 1\n"
                                "50 CASE 4 TO 6\n"
                                "60 PRINT 2\n"
                                "70 END SELECT\n"
                                "80 END\n";
        il::core::Module module = lowerSnippet(src);
        const il::core::Function *mainFn = findMain(module);
        assert(mainFn);

        size_t cmpGe = 0;
        size_t cmpLe = 0;
        size_t andCount = 0;
        bool sawSwitch = false;
        for (const auto &bb : mainFn->blocks)
        {
            for (const auto &instr : bb.instructions)
            {
                if (instr.op == il::core::Opcode::SCmpGE)
                    ++cmpGe;
                if (instr.op == il::core::Opcode::SCmpLE)
                    ++cmpLe;
                if (instr.op == il::core::Opcode::And)
                    ++andCount;
                if (instr.op == il::core::Opcode::SwitchI32)
                    sawSwitch = true;
            }
        }
        assert(sawSwitch);
        assert(cmpGe >= 1);
        assert(cmpLe >= 1);
        assert(andCount >= 1);
    }

    {
        const std::string src = "10 LET X = 10\n"
                                "20 SELECT CASE X\n"
                                "30 CASE 1\n"
                                "40 PRINT 1\n"
                                "50 CASE ELSE\n"
                                "60 PRINT 0\n"
                                "70 END SELECT\n"
                                "80 END\n";
        il::core::Module module = lowerSnippet(src);
        const il::core::Function *mainFn = findMain(module);
        assert(mainFn);

        const il::core::Instr *switchInstr = findSwitch(*mainFn);
        assert(switchInstr);

        const std::string &defaultLabel = il::core::switchDefaultLabel(*switchInstr);
        const il::core::BasicBlock *defaultBlock = findBlockByLabel(*mainFn, defaultLabel);
        assert(defaultBlock);
        assert(blockPrintsConstant(*defaultBlock, 0));
    }

    {
        const std::string src = "10 LET X = 0\n"
                                "20 SELECT CASE X\n"
                                "30 CASE 9223372036854775807\n"
                                "40 PRINT 1\n"
                                "50 END SELECT\n"
                                "60 END\n";
        auto result = lowerSnippetWithDiagnostics(src);
        assert(result.errorCount == 1);
        assert(result.diagnostics.find("error[B2012]") != std::string::npos);
        assert(result.diagnostics.find("outside 32-bit signed range") != std::string::npos);

        const il::core::Function *mainFn = findMain(result.module);
        assert(mainFn);
        const il::core::Instr *switchInstr = findSwitch(*mainFn);
        assert(switchInstr);
        // The switch should not contain a truncated operand for the overflowing label.
        assert(switchInstr->operands.size() == 1);
    }

    return 0;
}

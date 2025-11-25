//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_lowerer_select_case.cpp
// Purpose: Verify BASIC SELECT CASE lowering emits SwitchI32 with default arm.
// Key invariants: Switch default targets CASE ELSE; case labels dispatch to dedicated blocks.
// Ownership/Lifetime: Test owns parser, lowerer, and resulting module.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "il/core/Instr.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>
#include <vector>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{

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
        const il::core::Value &op = instr.operands.front();
        if (op.kind != il::core::Value::Kind::ConstInt)
            continue;
        if (op.i64 == value)
            return true;
    }
    return false;
}

} // namespace

int main()
{
    const std::string src = "10 DIM X AS LONG\n"
                            "20 LET X = 2\n"
                            "30 SELECT CASE X\n"
                            "40 CASE 1\n"
                            "50 PRINT 1\n"
                            "60 CASE 2\n"
                            "70 PRINT 2\n"
                            "80 CASE ELSE\n"
                            "90 PRINT 0\n"
                            "100 END SELECT\n"
                            "110 END\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("select_case.bas");
    Parser parser(src, fid);
    auto prog = parser.parseProgram();
    assert(prog);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*prog);

    const il::core::Function *mainFn = nullptr;
    for (const auto &fn : module.functions)
    {
        if (fn.name == "main")
        {
            mainFn = &fn;
            break;
        }
    }
    assert(mainFn);

    const il::core::Instr *switchInstr = nullptr;
    for (const auto &bb : mainFn->blocks)
    {
        for (const auto &instr : bb.instructions)
        {
            if (instr.op == il::core::Opcode::SwitchI32)
            {
                switchInstr = &instr;
                break;
            }
        }
        if (switchInstr)
            break;
    }
    assert(switchInstr);

    size_t caseCount = il::core::switchCaseCount(*switchInstr);
    assert(caseCount == 2);

    std::vector<long long> caseValues;
    caseValues.reserve(caseCount);
    for (size_t i = 0; i < caseCount; ++i)
    {
        const il::core::Value &val = il::core::switchCaseValue(*switchInstr, i);
        assert(val.kind == il::core::Value::Kind::ConstInt);
        caseValues.push_back(val.i64);
    }
    assert(caseValues.size() == 2);
    assert(caseValues[0] == 1);
    assert(caseValues[1] == 2);

    const std::string &defaultLabel = il::core::switchDefaultLabel(*switchInstr);
    const il::core::BasicBlock *defaultBlock = findBlockByLabel(*mainFn, defaultLabel);
    assert(defaultBlock);
    assert(blockPrintsConstant(*defaultBlock, 0));

    for (size_t i = 0; i < caseCount; ++i)
    {
        const std::string &label = il::core::switchCaseLabel(*switchInstr, i);
        const il::core::BasicBlock *caseBlock = findBlockByLabel(*mainFn, label);
        assert(caseBlock);
        assert(blockPrintsConstant(*caseBlock, caseValues[i]));
    }

    return 0;
}

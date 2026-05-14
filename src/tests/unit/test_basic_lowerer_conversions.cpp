//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_lowerer_conversions.cpp
// Purpose: Verify BASIC lowerer emits conversions for mixed-type statements.
// Key invariants: Assignments, prints, and inputs coerce values to target types.
// Ownership/Lifetime: Test owns parser, lowerer, and resulting module.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

using namespace il::frontends::basic;
using namespace il::support;

namespace {

bool hasLine(const std::vector<int> &lines, int target) {
    return std::find(lines.begin(), lines.end(), target) != lines.end();
}

} // namespace

int main() {
    const std::string src = "10 DIM FLAG AS BOOLEAN\n"
                            "20 LET I = 3.14\n"
                            "30 LET D# = 1\n"
                            "40 LET I = TRUE\n"
                            "50 PRINT TRUE\n"
                            "70 INPUT \"?\", FLAG\n"
                            "80 INPUT \"?\", D#\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("conversions.bas");
    Parser parser(src, fid);
    auto prog = parser.parseProgram();
    assert(prog);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*prog);

    const il::core::Function *mainFn = nullptr;
    for (const auto &fn : module.functions) {
        if (fn.name == "main") {
            mainFn = &fn;
            break;
        }
    }
    assert(mainFn);

    std::vector<int> castChkLines;
    std::vector<int> sitofpLines;
    std::vector<int> truncLines;
    std::vector<int> toDoubleLines;

    for (const auto &block : mainFn->blocks) {
        for (const auto &instr : block.instructions) {
            switch (instr.op) {
                case il::core::Opcode::CastFpToSiRteChk:
                    castChkLines.push_back(instr.loc.line);
                    break;
                case il::core::Opcode::Sitofp:
                    sitofpLines.push_back(instr.loc.line);
                    break;
                case il::core::Opcode::Trunc1:
                    truncLines.push_back(instr.loc.line);
                    break;
                case il::core::Opcode::Call:
                    if (instr.callee == "rt_to_double" ||
                        instr.callee == "Viper.Core.Convert.ToDouble")
                        toDoubleLines.push_back(instr.loc.line);
                    break;
                default:
                    break;
            }
        }
    }

    assert(hasLine(castChkLines, 2)); // LET I = 3.14
    assert(hasLine(sitofpLines, 3));  // LET D# = 1
    assert(hasLine(truncLines, 6));   // INPUT "?", FLAG
    assert(!toDoubleLines.empty());   // INPUT "?", D# uses rt_to_double

    {
        const std::string divSrc = "10 LET Q# = 7 / 2\n"
                                   "20 LET I = 7 \\ 2\n";
        Parser divParser(divSrc, fid);
        auto divProg = divParser.parseProgram();
        assert(divProg);

        il::core::Module divModule = lowerer.lowerProgram(*divProg);
        const il::core::Function *divMain = nullptr;
        for (const auto &fn : divModule.functions) {
            if (fn.name == "main") {
                divMain = &fn;
                break;
            }
        }
        assert(divMain);

        bool hasRealDivision = false;
        bool hasIntegerDivision = false;
        bool promotedIntegerOperand = false;
        for (const auto &block : divMain->blocks) {
            for (const auto &instr : block.instructions) {
                if (instr.op == il::core::Opcode::FDiv && instr.loc.line == 1)
                    hasRealDivision = true;
                if (instr.op == il::core::Opcode::SDivChk0 && instr.loc.line == 2)
                    hasIntegerDivision = true;
                if (instr.op == il::core::Opcode::Sitofp && instr.loc.line == 1)
                    promotedIntegerOperand = true;
            }
        }
        assert(hasRealDivision);
        assert(hasIntegerDivision);
        assert(promotedIntegerOperand);
    }

    return 0;
}

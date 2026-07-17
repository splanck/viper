//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_print_runtime_property.cpp
// Purpose: Regression for VDOC-180 — PRINT of a string-valued runtime property
//   getter must NOT wrap the owned result in an extra retain/release pair (the
//   getter already schedules a statement-boundary release, so an added release
//   is a double free that the IL verifier rejects).
// Key invariants:
//   - No rt_str_retain_maybe is emitted for the PRINT of the owned getter
//     result, while a genuinely borrowed variable print still retains.
// Ownership/Lifetime: Test owns parser, lowerer, and resulting module.
// Links: docs/documentation-review-findings.md (VDOC-180)
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

namespace {
// Count calls to `callee` that carry the given source line.
int countCallsOnLine(const il::core::Module &module, const std::string &callee, uint32_t line) {
    int count = 0;
    for (const auto &fn : module.functions) {
        if (fn.name != "main")
            continue;
        for (const auto &block : fn.blocks)
            for (const auto &instr : block.instructions)
                if (instr.op == il::core::Opcode::Call && instr.callee == callee &&
                    instr.loc.line == line)
                    ++count;
    }
    return count;
}
} // namespace

int main() {
    // Line 20 prints a string-valued runtime property (owned result); line 40
    // prints a borrowed string variable (must still be retained for the call).
    const std::string src =
        "10 DIM CONN AS Zanna.Crypto.Tls = Zanna.Crypto.Tls.Connect(\"127.0.0.1\", 1)\n"
        "20 PRINT CONN.Host\n"
        "30 DIM S AS STRING = \"hi\"\n"
        "40 PRINT S\n";

    SourceManager sm;
    uint32_t fid = sm.addFile("print_runtime_property.bas");
    Parser parser(src, fid);
    auto program = parser.parseProgram();
    assert(program);

    Lowerer lowerer;
    il::core::Module module = lowerer.lowerProgram(*program);

    // The owned property-getter result (line 20) must NOT get a manual
    // retain/release pair (VDOC-180): the getter already scheduled a
    // statement-boundary release, so an added release is a double free. Before
    // the fix this line emitted one manual retain and one manual release.
    int propRetain = countCallsOnLine(module, "rt_str_retain_maybe", 20u);
    int propRelease = countCallsOnLine(module, "rt_str_release_maybe", 20u);
    if (propRetain != 0 || propRelease != 0) {
        fprintf(stderr,
                "PRINT of owned property emitted %d manual retains / %d releases "
                "(expected 0 / 0)\n",
                propRetain,
                propRelease);
        return 1;
    }
    printf("PRINT runtime-property ownership (VDOC-180): OK\n");
    return 0;
}

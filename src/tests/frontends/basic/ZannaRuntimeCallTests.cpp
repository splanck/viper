//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/ZannaRuntimeCallTests.cpp
// Purpose: Ensure Zanna.* runtime classes (Terminal, Time) are callable
//          from BASIC via qualified procedure calls.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main() {
    // Compose a small BASIC snippet that calls Zanna.Terminal and Zanna.Time.
    const std::string src = "SUB Demo()\n"
                            "    Zanna.Terminal.SetPosition(1, 2)\n"
                            "    Zanna.Terminal.SetColor(7, 0)\n"
                            "    Zanna.Terminal.Clear()\n"
                            "    DIM t AS INTEGER\n"
                            "    t = Zanna.Time.Clock.NowMs()\n"
                            "    Zanna.Time.Clock.Sleep(10)\n"
                            "END SUB\n";

    SourceManager sm;
    const uint32_t fid = sm.addFile("zanna_runtime_calls.bas");

    DiagnosticEngine de;
    DiagnosticEmitter emitter(de, sm);
    emitter.addSource(fid, src);

    Parser parser(src, fid, &emitter);
    auto program = parser.parseProgram();
    assert(program);
    // Construct analyzer and verify runtime procedures are seeded.
    SemanticAnalyzer analyzer(emitter);
    {
        bool haveSetPos = false, haveSetColor = false, haveClear = false, haveSleep = false,
             haveTick = false;
        for (const auto &kv : analyzer.procs()) {
            const std::string &name = kv.first;
            if (name == "Zanna.Terminal.SetPosition")
                haveSetPos = true;
            if (name == "Zanna.Terminal.SetColor")
                haveSetColor = true;
            if (name == "Zanna.Terminal.Clear")
                haveClear = true;
            if (name == "Zanna.Time.Clock.Sleep")
                haveSleep = true;
            if (name == "Zanna.Time.Clock.NowMs")
                haveTick = true;
        }
        if (!(haveSetPos && haveSetColor && haveClear && haveSleep && haveTick)) {
            fprintf(stderr,
                    "ProcRegistry missing entries: pos=%d color=%d clear=%d sleep=%d tick=%d\n",
                    (int)haveSetPos,
                    (int)haveSetColor,
                    (int)haveClear,
                    (int)haveSleep,
                    (int)haveTick);
        }
        assert(haveSetPos && haveSetColor && haveClear && haveSleep && haveTick);
    }

    // Run semantic analysis and expect no unknown procedure errors.
    analyzer.analyze(*program);
    if (de.errorCount() != 0) {
        std::ostringstream oss;
        emitter.printAll(oss);
        fprintf(stderr, "Diagnostics:\n%s\n", oss.str().c_str());
    }
    assert(de.errorCount() == 0);
    return 0;
}

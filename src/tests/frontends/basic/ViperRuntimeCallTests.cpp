//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/ViperRuntimeCallTests.cpp
// Purpose: Ensure Viper.* runtime classes (Terminal, Time) are callable
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

int main()
{
    // Compose a small BASIC snippet that calls Viper.Terminal and Viper.Time.
    const std::string src = "SUB Demo()\n"
                            "    Viper.Terminal.SetPosition(1, 2)\n"
                            "    Viper.Terminal.SetColor(7, 0)\n"
                            "    Viper.Terminal.Clear()\n"
                            "    DIM t AS INTEGER\n"
                            "    t = Viper.Time.GetTickCount()\n"
                            "    Viper.Time.SleepMs(10)\n"
                            "END SUB\n";

    SourceManager sm;
    const uint32_t fid = sm.addFile("viper_runtime_calls.bas");

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
        for (const auto &kv : analyzer.procs())
        {
            const std::string &name = kv.first;
            if (name == "Viper.Terminal.SetPosition")
                haveSetPos = true;
            if (name == "Viper.Terminal.SetColor")
                haveSetColor = true;
            if (name == "Viper.Terminal.Clear")
                haveClear = true;
            if (name == "Viper.Time.SleepMs")
                haveSleep = true;
            if (name == "Viper.Time.GetTickCount")
                haveTick = true;
        }
        if (!(haveSetPos && haveSetColor && haveClear && haveSleep && haveTick))
        {
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
    if (de.errorCount() != 0)
    {
        std::ostringstream oss;
        emitter.printAll(oss);
        fprintf(stderr, "Diagnostics:\n%s\n", oss.str().c_str());
    }
    assert(de.errorCount() == 0);
    return 0;
}

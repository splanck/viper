//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/ParserLineLabelDiagnosticsTests.cpp
// Purpose: Ensure BASIC parser does not misreport diagnostics for legitimate line labels. 
// Key invariants: Line-number tokens are consumed by the statement sequencer rather than parsed as
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    {
        const std::string src = "100\n110 PRINT 1\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("line_only.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);

        Parser parser(src, fid, &emitter);
        auto program = parser.parseProgram();
        assert(program);
        assert(emitter.errorCount() == 0);
    }

    {
        const std::string src = "200: PRINT 2\n210 END\n";
        SourceManager sm;
        const uint32_t fid = sm.addFile("colon_label.bas");
        DiagnosticEngine de;
        DiagnosticEmitter emitter(de, sm);
        emitter.addSource(fid, src);

        Parser parser(src, fid, &emitter);
        auto program = parser.parseProgram();
        assert(program);
        assert(emitter.errorCount() == 0);
    }

    return 0;
}

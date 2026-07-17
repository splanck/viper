//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_control_recovery.cpp
// Purpose: Verify the BASIC control-flow checker recovers from malformed loop
//          nesting with structured diagnostics instead of aborting on a stack
//          balance assertion (VDOC-244).
// Key invariants:
//   - Parsing + semantic analysis of invalid loop nesting must return, not abort.
//   - At least one diagnostic is reported for the malformed source.
// Ownership/Lifetime: Test owns parser and semantic analyzer state.
// Links: src/frontends/basic/sem/Check_Common.hpp (ControlCheckContext)
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

// Analyze a source buffer end-to-end; returns the total diagnostic error count.
// The point of the test is simply that this returns at all — the pre-fix
// ControlCheckContext destructor asserted the loop/FOR stacks were balanced and
// aborted the process on malformed nesting.
static int analyzeAndCountErrors(const std::string &src) {
    SourceManager sm;
    const uint32_t fid = sm.addFile("control_recovery.bas");

    DiagnosticEngine parseDiags;
    DiagnosticEmitter parseEmitter(parseDiags, sm);
    parseEmitter.addSource(fid, src);
    Parser parser(src, fid, &parseEmitter);
    auto prog = parser.parseProgram();
    assert(prog);

    DiagnosticEngine semaDiags;
    DiagnosticEmitter semaEmitter(semaDiags, sm);
    semaEmitter.addSource(fid, src);
    SemanticAnalyzer analyzer(semaEmitter);
    analyzer.analyze(*prog);
    return static_cast<int>(parseEmitter.errorCount() + semaEmitter.errorCount());
}

int main() {
    // Reduced one-loop form: a reserved keyword (NEXT) used as an identifier at
    // statement start already diagnosed cleanly before the fix.
    const std::string reduced = "10 DIM NEXT AS OBJECT\n"
                                "20 FOR I = 1 TO 3\n"
                                "30 NEXT.SET(I)\n"
                                "40 NEXT I\n"
                                "50 END\n";
    const int reducedErrors = analyzeAndCountErrors(reduced);
    assert(reducedErrors > 0 && "reduced malformed loop must report diagnostics");

    // Nested form (the Game of Life example shape): `next` as an object used inside
    // two FOR loops left the FOR stack unbalanced and aborted in the control-check
    // destructor. It must now return structured diagnostics instead of crashing.
    const std::string nested = "10 DIM NEXT AS OBJECT\n"
                               "20 FOR I = 1 TO 3\n"
                               "30 FOR J = 1 TO 3\n"
                               "40 NEXT.SET(I, J)\n"
                               "50 NEXT J\n"
                               "60 NEXT I\n"
                               "70 END\n";
    const int nestedErrors = analyzeAndCountErrors(nested);
    assert(nestedErrors > 0 && "nested malformed loop must report diagnostics, not abort");

    // Reaching here means neither analysis aborted.
    return 0;
}

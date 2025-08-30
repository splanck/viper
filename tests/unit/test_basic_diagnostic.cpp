// File: tests/unit/test_basic_diagnostic.cpp
// Purpose: Ensure DiagnosticEmitter formats BASIC diagnostics with carets and codes.
// Key invariants: Output contains error code and caret underlined range.
// Ownership/Lifetime: Test owns all objects locally.
// Links: docs/class-catalog.md

#include "frontends/basic/DiagnosticEmitter.h"
#include "frontends/basic/Parser.h"
#include "frontends/basic/SemanticAnalyzer.h"
#include "support/source_manager.h"
#include <cassert>
#include <sstream>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main() {
  std::string src = "10 PRINT X\n20 END\n";
  SourceManager sm;
  uint32_t fid = sm.addFile("test.bas");
  Parser p(src, fid);
  auto prog = p.parseProgram();

  DiagnosticEngine de;
  DiagnosticEmitter em(de, sm);
  em.addSource(fid, src);
  SemanticAnalyzer sema(em);
  sema.analyze(*prog);

  std::ostringstream oss;
  em.printAll(oss);
  std::string out = oss.str();
  assert(em.errorCount() == 1);
  assert(out.find("error[B1001]") != std::string::npos);
  assert(out.find("unknown variable 'X'") != std::string::npos);
  assert(out.find("^") != std::string::npos);
  return 0;
}

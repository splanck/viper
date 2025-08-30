// File: src/tools/ilc/main.cpp
// Purpose: Main driver for IL compiler and runner.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/class-catalog.md
#include "frontends/basic/Lowerer.h"
#include "frontends/basic/Parser.h"
#include "frontends/basic/SemanticAnalyzer.h"
#include "il/io/Parser.h"
#include "il/io/Serializer.h"
#include "il/verify/Verifier.h"
#include "support/source_manager.h"
#include "vm/VM.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace il;
using namespace il::frontends::basic;
using namespace il::support;

static void usage() {
  std::cerr << "ilc v0.1.0\n"
            << "Usage: ilc -run <file.il> [--trace]\n"
            << "       ilc front basic -emit-il <file.bas>\n"
            << "       ilc front basic -run <file.bas> [--trace]\n";
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return 1;
  }

  std::string cmd = argv[1];
  bool trace = false;

  if (cmd == "-run") {
    if (argc < 3) {
      usage();
      return 1;
    }
    std::string ilFile = argv[2];
    for (int i = 3; i < argc; ++i) {
      if (std::string(argv[i]) == "--trace")
        trace = true;
      else {
        usage();
        return 1;
      }
    }
    std::ifstream ifs(ilFile);
    if (!ifs) {
      std::cerr << "unable to open " << ilFile << "\n";
      return 1;
    }
    core::Module m;
    if (!io::Parser::parse(ifs, m, std::cerr))
      return 1;
    if (!verify::Verifier::verify(m, std::cerr))
      return 1;
    vm::VM vm(m, trace);
    return static_cast<int>(vm.run());
  }

  if (cmd == "front" && argc >= 3) {
    std::string lang = argv[2];
    if (lang == "basic") {
      bool emitIl = false;
      bool run = false;
      std::string file;
      for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-emit-il" && i + 1 < argc) {
          emitIl = true;
          file = argv[++i];
        } else if (arg == "-run" && i + 1 < argc) {
          run = true;
          file = argv[++i];
        } else if (arg == "--trace") {
          trace = true;
        } else {
          usage();
          return 1;
        }
      }
      if ((emitIl == run) || file.empty()) {
        usage();
        return 1;
      }

      std::ifstream in(file);
      if (!in) {
        std::cerr << "unable to open " << file << "\n";
        return 1;
      }
      std::ostringstream ss;
      ss << in.rdbuf();
      std::string src = ss.str();
      SourceManager sm;
      uint32_t fid = sm.addFile(file);
      Parser p(src, fid);
      auto prog = p.parseProgram();
      support::DiagnosticEngine de;
      SemanticAnalyzer sema(de);
      sema.analyze(*prog);
      if (de.errorCount() > 0) {
        de.printAll(std::cerr, &sm);
        return 1;
      }
      Lowerer lower;
      core::Module m = lower.lower(*prog);

      if (emitIl) {
        io::Serializer::write(m, std::cout);
        return 0;
      }

      if (!verify::Verifier::verify(m, std::cerr))
        return 1;
      vm::VM vm(m, trace);
      return static_cast<int>(vm.run());
    }
  }

  usage();
  return 1;
}

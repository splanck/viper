// File: src/tools/ilc/main.cpp
// Purpose: Main driver for IL compiler and runner.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/class-catalog.md
#include "frontends/basic/ConstFolder.h"
#include "frontends/basic/DiagnosticEmitter.h"
#include "frontends/basic/Lowerer.h"
#include "frontends/basic/Parser.h"
#include "frontends/basic/SemanticAnalyzer.h"
#include "il/io/Parser.h"
#include "il/io/Serializer.h"
#include "il/transform/ConstFold.h"
#include "il/transform/PassManager.h"
#include "il/transform/Peephole.h"
#include "il/verify/Verifier.h"
#include "support/source_manager.h"
#include "vm/VM.h"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace il;
using namespace il::frontends::basic;
using namespace il::support;

static void usage() {
  std::cerr << "ilc v0.1.0\n"
            << "Usage: ilc -run <file.il> [--trace] [--stdin-from <file>]\n"
            << "       ilc front basic -emit-il <file.bas>\n"
            << "       ilc front basic -run <file.bas> [--trace] [--stdin-from <file>]\n"
            << "       ilc il-opt <in.il> -o <out.il> --passes p1,p2\n";
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
    std::string stdinPath;
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--trace") {
        trace = true;
      } else if (arg == "--stdin-from" && i + 1 < argc) {
        stdinPath = argv[++i];
      } else {
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
    if (!stdinPath.empty()) {
      if (!freopen(stdinPath.c_str(), "r", stdin)) {
        std::cerr << "unable to open stdin file\n";
        return 1;
      }
    }
    vm::VM vm(m, trace);
    return static_cast<int>(vm.run());
  }

  if (cmd == "il-opt") {
    if (argc < 5) {
      usage();
      return 1;
    }
    std::string inFile = argv[2];
    std::string outFile;
    std::vector<std::string> passList;
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-o" && i + 1 < argc) {
        outFile = argv[++i];
      } else if (arg == "--passes" && i + 1 < argc) {
        std::string passes = argv[++i];
        size_t pos = 0;
        while (pos != std::string::npos) {
          size_t comma = passes.find(',', pos);
          passList.push_back(passes.substr(pos, comma - pos));
          if (comma == std::string::npos)
            break;
          pos = comma + 1;
        }
      } else {
        usage();
        return 1;
      }
    }
    if (outFile.empty()) {
      usage();
      return 1;
    }
    std::ifstream ifs(inFile);
    if (!ifs) {
      std::cerr << "unable to open " << inFile << "\n";
      return 1;
    }
    core::Module m;
    if (!io::Parser::parse(ifs, m, std::cerr))
      return 1;
    transform::PassManager pm;
    pm.addPass("constfold", transform::constFold);
    pm.addPass("peephole", transform::peephole);
    pm.run(m, passList);
    std::ofstream ofs(outFile);
    if (!ofs) {
      std::cerr << "unable to open " << outFile << "\n";
      return 1;
    }
    io::Serializer::write(m, ofs, io::Serializer::Mode::Canonical);
    return 0;
  }

  if (cmd == "front" && argc >= 3) {
    std::string lang = argv[2];
    if (lang == "basic") {
      bool emitIl = false;
      bool run = false;
      std::string file;
      std::string stdinPath;
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
        } else if (arg == "--stdin-from" && i + 1 < argc) {
          stdinPath = argv[++i];
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
      foldConstants(*prog);
      support::DiagnosticEngine de;
      DiagnosticEmitter em(de, sm);
      em.addSource(fid, src);
      SemanticAnalyzer sema(em);
      sema.analyze(*prog);
      if (em.errorCount() > 0) {
        em.printAll(std::cerr);
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
      if (!stdinPath.empty()) {
        if (!freopen(stdinPath.c_str(), "r", stdin)) {
          std::cerr << "unable to open stdin file\n";
          return 1;
        }
      }
      vm::VM vm(m, trace);
      return static_cast<int>(vm.run());
    }
  }

  usage();
  return 1;
}

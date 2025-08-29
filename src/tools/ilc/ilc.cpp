#include "il/io/Parser.h"
#include "il/verify/Verifier.h"
#include "vm/VM.h"
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  bool trace = false;
  std::string runFile;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--trace") {
      trace = true;
    } else if (arg == "-run" && i + 1 < argc) {
      runFile = argv[++i];
    } else {
      std::cout << "ilc v0.1.0\nUsage: ilc -run <file.il> [--trace]\n";
      return 0;
    }
  }
  if (runFile.empty()) {
    std::cout << "ilc v0.1.0\nUsage: ilc -run <file.il> [--trace]\n";
    return 0;
  }
  std::ifstream ifs(runFile);
  if (!ifs) {
    std::cerr << "unable to open " << runFile << "\n";
    return 1;
  }
  il::core::Module m;
  if (!il::io::Parser::parse(ifs, m, std::cerr))
    return 1;
  if (!il::verify::Verifier::verify(m, std::cerr))
    return 1;
  il::vm::VM vm(m, trace);
  return (int)vm.run();
}

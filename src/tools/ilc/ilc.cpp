#include "il/io/Parser.h"
#include "il/verify/Verifier.h"
#include "vm/VM.h"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv) {
  bool run = false;
  bool trace = false;
  std::string file;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-run" && i + 1 < argc) {
      run = true;
      file = argv[++i];
    } else if (arg == "--trace") {
      trace = true;
    }
  }
  if (!run) {
    std::cout << "ilc v0.1.0\n";
    std::cout << "Usage: ilc -run <file.il> [--trace]\n";
    return 0;
  }
  std::ifstream in(file);
  if (!in) {
    std::cerr << "could not open " << file << "\n";
    return 1;
  }
  il::core::Module m;
  std::ostringstream perr;
  if (!il::io::Parser::parse(in, m, perr)) {
    std::cerr << perr.str();
    return 1;
  }
  std::ostringstream verr;
  if (!il::verify::Verifier::verify(m, verr)) {
    std::cerr << verr.str();
    return 1;
  }
  il::vm::VM vm(trace);
  return (int)vm.run(m);
}

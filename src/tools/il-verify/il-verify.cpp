#include "il/io/Parser.h"
#include "il/verify/Verifier.h"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: il-verify <file.il>\n";
    return 1;
  }
  std::ifstream in(argv[1]);
  if (!in) {
    std::cerr << "cannot open " << argv[1] << "\n";
    return 1;
  }
  il::core::Module m;
  if (!il::io::Parser::parse(in, m, std::cerr))
    return 1;
  std::ostringstream diag;
  if (!il::verify::Verifier::verify(m, diag)) {
    std::cerr << diag.str();
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}

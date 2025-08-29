#include "frontends/basic/AstPrinter.h"
#include "frontends/basic/Lexer.h"
#include "frontends/basic/Parser.h"
#include "support/source_manager.h"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: basic-ast <file>\n";
    return 1;
  }
  std::ifstream ifs(argv[1]);
  if (!ifs) {
    std::cerr << "could not open file\n";
    return 1;
  }
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  std::string src = buffer.str();
  il::support::SourceManager sm;
  uint32_t fid = sm.addFile(argv[1]);
  il::basic::Lexer lex(src, fid);
  auto toks = lex.lex();
  il::basic::Parser parser(std::move(toks));
  auto prog = parser.parse();
  std::cout << il::basic::AstPrinter::print(prog);
  return 0;
}

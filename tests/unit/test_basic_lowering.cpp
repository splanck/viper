#include "frontends/basic/Lowerer.h"
#include "frontends/basic/NameMangler.h"
#include "frontends/basic/Parser.h"
#include "il/io/Serializer.h"
#include <cassert>
#include <fstream>
#include <sstream>

using namespace il::frontends::basic;

static std::unique_ptr<Program> parseFile(const std::string &path) {
  std::ifstream in(path);
  std::stringstream buf;
  buf << in.rdbuf();
  Parser p(buf.str(), 0);
  return p.parseProgram();
}

int main() {
  // NameMangler basics
  NameMangler nm;
  assert(nm.nextTemp() == "%t0");
  assert(nm.block("entry") == "entry");

  // Lowerer integration tests on sample programs
  Lowerer lowerer;

    auto prog1 = parseFile(std::string(BASIC_EXAMPLES_DIR) + "/ex1_hello_cond.bas");
    auto mod1 = lowerer.lower(*prog1);
    std::string il1 = il::io::Serializer::toString(mod1);
    assert(!il1.empty());
    // TODO: add golden IL comparison once lowering stabilizes

  return 0;
}

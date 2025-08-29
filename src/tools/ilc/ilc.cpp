#include "support/diagnostics.h"
#include <iostream>
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  il::support::DiagnosticEngine engine;
  engine.report({il::support::Severity::Error, "demo diagnostic", {}});
  engine.printAll(std::cerr);
  std::cout << "ilc v0.1.0\n";
  std::cout << "Usage: ilc [--help]\n";
  return engine.errorCount() ? 1 : 0;
}

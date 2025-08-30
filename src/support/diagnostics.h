// File: src/support/diagnostics.h
// Purpose: Declares diagnostic engine for errors and warnings.
// Key invariants: None.
// Ownership/Lifetime: Engine owns collected diagnostics.
// Links: docs/class-catalog.md
#pragma once
#include "source_manager.h"
#include <ostream>
#include <string>
#include <vector>
/// @brief Records diagnostics and prints them later.
/// @invariant Counts reflect reported diagnostics.
/// @ownership Owns stored diagnostic messages.
namespace il::support {
enum class Severity { Note, Warning, Error };
struct Diagnostic {
  Severity severity;
  std::string message;
  SourceLoc loc;
};
class DiagnosticEngine {
public:
  void report(Diagnostic d);
  void printAll(std::ostream &os, const SourceManager *sm = nullptr) const;
  size_t errorCount() const { return errors_; }
  size_t warningCount() const { return warnings_; }

private:
  std::vector<Diagnostic> diags_;
  size_t errors_ = 0;
  size_t warnings_ = 0;
};
} // namespace il::support

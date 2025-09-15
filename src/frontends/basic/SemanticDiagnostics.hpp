// File: src/frontends/basic/SemanticDiagnostics.hpp
// Purpose: Wraps DiagnosticEmitter for semantic analysis utilities.
// Key invariants: Forwards diagnostics without altering counts.
// Ownership/Lifetime: Borrows DiagnosticEmitter; no ownership of sources.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/DiagnosticEmitter.hpp"

namespace il::frontends::basic
{

class SemanticDiagnostics
{
  public:
    explicit SemanticDiagnostics(DiagnosticEmitter &emitter) : emitter_(emitter) {}

    void emit(il::support::Severity sev,
              std::string code,
              il::support::SourceLoc loc,
              uint32_t length,
              std::string message)
    {
        emitter_.emit(sev, std::move(code), loc, length, std::move(message));
    }

    size_t errorCount() const
    {
        return emitter_.errorCount();
    }

    size_t warningCount() const
    {
        return emitter_.warningCount();
    }

    DiagnosticEmitter &emitter()
    {
        return emitter_;
    }

  private:
    DiagnosticEmitter &emitter_;
};

} // namespace il::frontends::basic

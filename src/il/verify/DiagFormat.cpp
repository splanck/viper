// File: src/il/verify/DiagFormat.cpp
// Purpose: Define shared helpers for formatting IL verifier diagnostics.
// Key invariants: Functions are pure and rely solely on immutable IL inputs.
// Ownership/Lifetime: Callers retain ownership of IL structures; helpers copy strings.
// Links: docs/il-guide.md#reference

#include "il/verify/DiagFormat.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/verify/TypeInference.hpp"

#include <sstream>

namespace il::verify
{

std::string formatBlockDiag(const core::Function &fn,
                            const core::BasicBlock &bb,
                            std::string_view message)
{
    std::ostringstream oss;
    oss << fn.name << ":" << bb.label;
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

std::string formatInstrDiag(const core::Function &fn,
                            const core::BasicBlock &bb,
                            const core::Instr &instr,
                            std::string_view message)
{
    std::ostringstream oss;
    oss << fn.name << ":" << bb.label << ": " << makeSnippet(instr);
    if (!message.empty())
        oss << ": " << message;
    return oss.str();
}

} // namespace il::verify


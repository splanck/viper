// File: src/il/api/expected_api.cpp
// Purpose: Provide Expected-based implementations for IL parsing and verification wrappers.
// Key invariants: Mirrors legacy bool-returning APIs while emitting diagnostics through Expected.
// Ownership/Lifetime: Callers retain ownership of modules and streams passed by reference.
// Links: docs/il-guide.md#reference

#include "il/api/expected_api.hpp"

#include "il/core/Module.hpp"
#include "il/io/Parser.hpp"
#include "il/verify/Verifier.hpp"

namespace il::api::v2
{

il::support::Expected<void> parse_text_expected(std::istream &is, il::core::Module &m)
{
    return il::io::Parser::parse(is, m);
}

il::support::Expected<void> verify_module_expected(const il::core::Module &m)
{
    return il::verify::Verifier::verify(m);
}

} // namespace il::api::v2

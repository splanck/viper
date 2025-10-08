// File: src/il/api/expected_api.cpp
// License: MIT (see LICENSE for details).
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

/// @brief Parse IL text into a module using the modern Expected-oriented API.
/// @details Forwards to @ref il::io::Parser::parse so textual IL is decoded
/// into the provided module. The wrapper exists to keep the public C++ API
/// symmetrical with the legacy bool-returning helpers while surfacing
/// diagnostics via @ref il::support::Expected.
/// @param is Stream supplying UTF-8 IL source text.
/// @param m Module that receives the parsed representation.
/// @returns Empty Expected on success; otherwise the parse diagnostics.
il::support::Expected<void> parse_text_expected(std::istream &is, il::core::Module &m)
{
    return il::io::Parser::parse(is, m);
}

/// @brief Verify a module using the Expected-returning API surface.
/// @details Delegates to @ref il::verify::Verifier::verify so the full suite
/// of structural and semantic checks run on the caller-supplied module. The
/// result mirrors @ref parse_text_expected by forwarding diagnostics through an
/// Expected payload.
/// @param m Module to validate.
/// @returns Empty Expected when verification succeeds; otherwise a populated
/// diagnostic collection.
il::support::Expected<void> verify_module_expected(const il::core::Module &m)
{
    return il::verify::Verifier::verify(m);
}

} // namespace il::api::v2

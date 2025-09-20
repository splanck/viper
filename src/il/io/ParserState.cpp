// File: src/il/io/ParserState.cpp
// Purpose: Implements shared parser state helpers for IL parsing routines.
// Key invariants: Module reference remains valid for the lifetime of the state.
// Ownership/Lifetime: Stores non-owning references to externally managed IR.
// Links: docs/il-spec.md

#include "il/io/ParserState.hpp"

namespace il::io::detail
{
/// @brief Initialize parser state for the supplied module.
/// @param mod Module that receives parsed constructs.
ParserState::ParserState(il::core::Module &mod) : m(mod) {}
} // namespace il::io::detail


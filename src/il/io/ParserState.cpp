//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/ParserState.cpp
// Purpose: Define the constructor logic for the shared ParserState object used
//          by the textual IL parser so that the header can remain lightweight.
// Ownership/Lifetime: ParserState keeps a reference to a caller-owned Module.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Binds parser state objects to the module they populate.
/// @details The constructor is intentionally out-of-line to centralise the
///          documentation around ownership semantics: parser helpers mutate the
///          caller-provided module directly and therefore rely on the reference
///          stored here remaining valid for the parser's lifetime.

#include "il/io/ParserState.hpp"

namespace il::io::detail
{
/// @brief Bind the parser state to a concrete module instance.
///
/// The parser operates over mutable IR, so the constructor captures the module
/// by reference rather than by value.  Storing the reference guarantees that
/// parsed functions, globals, and extern declarations mutate the caller-owned
/// module directly while avoiding copies or lifetime ownership ambiguities.
///
/// @param mod Module that will receive all IR entities materialized by the
///             parser.
ParserState::ParserState(il::core::Module &mod) : m(mod) {}
} // namespace il::io::detail

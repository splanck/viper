//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//
// Implements the shared state object that threads contextual information
// through the IL parsing routines.  ParserState primarily exists to bundle a
// mutable Module reference so that the parser can register newly discovered
// entities without exposing global state.  The constructor lives here to keep
// the header lightweight for users that only require forward declarations.
//===----------------------------------------------------------------------===//

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


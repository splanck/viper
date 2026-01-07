//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/io/FunctionParser.cpp
// Purpose: Parse IL textual function definitions into in-memory IR structures.
// Key invariants: ParserState maintains current function, block, and location
//                 context while enforcing SSA identifier uniqueness.
// Ownership/Lifetime: Populates functions directly within the supplied module.
// Links: docs/il-guide.md#reference
//
// Note: This file is split into multiple compilation units for maintainability:
//   - FunctionParser_Prototype.cpp: Function header/prototype parsing
//   - FunctionParser_Body.cpp: Body/block/instruction parsing
//   - FunctionParser_Internal.hpp: Shared types and utilities
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the streaming parser that materialises IL functions.
/// @details The helpers in this file cooperate with instruction and operand
///          parsers to translate the textual IL syntax into the core IR data
///          structures.  The functions actively validate naming, type, and
///          structural constraints while producing precise diagnostics that
///          mirror the textual form understood by developers.

// This file now serves as an entry point that includes the split implementations.
// The actual parsing logic is implemented in:
//   - FunctionParser_Prototype.cpp (parseFunctionHeader)
//   - FunctionParser_Body.cpp (parseBlockHeader, parseFunction)
//
// All shared types and utilities are in FunctionParser_Internal.hpp

#include "il/internal/io/FunctionParser.hpp"
#include "il/internal/io/FunctionParser_Internal.hpp"

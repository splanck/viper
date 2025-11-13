//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Serializer class, which converts IL modules to their
// textual representation. The serializer is the inverse of the Parser, producing
// human-readable IL code that can be saved to files, inspected for debugging,
// or transmitted between compilation stages.
//
// The Serializer supports two output modes:
// - Pretty mode: Human-readable with indentation, comments, and whitespace
// - Canonical mode: Minimal whitespace for deterministic output and diffing
//
// Serialization is used throughout the Viper toolchain:
// - ilc -emit-il: Output IL from frontend compilation
// - il-dis: Disassemble binary IL to text
// - Test golden files: Canonical output for regression testing
// - Debugging: Inspect intermediate optimization results
// - Error messages: Show context around verification failures
//
// The serializer produces output conforming to the IL grammar defined in
// docs/il-guide.md. All serialized IL should parse back to an equivalent module
// structure (modulo whitespace and comments in Pretty mode).
//
// Key Responsibilities:
// - Module structure: Version header, target triple, extern declarations
// - Global data: String constants and numeric globals with initializers
// - Functions: Signatures, parameters, basic blocks, and instructions
// - Instructions: Opcodes, operands, types, and optional metadata
// - Values: Temporaries (%N), constants, global addresses (@name)
// - Control flow: Branch targets, arguments, switch cases
//
// Design Decisions:
// - Stateless: No persistent state between serialize calls
// - Static methods: No need to instantiate Serializer objects
// - Stream-based: Output to std::ostream for flexibility (files, strings, stdout)
// - Format control: Mode parameter allows callers to choose output style
//
// Thread Safety:
// The Serializer is thread-safe because it's stateless. Multiple threads can
// serialize different modules concurrently without synchronization.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/fwd.hpp"
#include <ostream>
#include <string>

namespace il::io
{

/// @brief Serializes IL modules to their textual form.
class Serializer
{
  public:
    /// @brief Controls output formatting style.
    enum class Mode
    {
        Pretty,
        Canonical
    };

    /// @brief Write module @p m to output stream @p os.
    /// @param m Module to serialize.
    /// @param os Destination stream.
    /// @param mode Formatting mode (pretty by default).
    static void write(const il::core::Module &m, std::ostream &os, Mode mode = Mode::Pretty);

    /// @brief Serialize module @p m to a string.
    /// @param m Module to serialize.
    /// @param mode Formatting mode (pretty by default).
    /// @return Textual IL representation.
    static std::string toString(const il::core::Module &m, Mode mode = Mode::Pretty);
};

} // namespace il::io

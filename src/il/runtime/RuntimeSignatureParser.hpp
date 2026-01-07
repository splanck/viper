//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares parsing utilities for runtime helper signature specifications.
// Runtime helpers (memory allocation, string operations, math functions) have
// signatures encoded as compact string specifications that must be parsed into
// structured type information for IL generation and optimization.
//
// The runtime signature system uses a domain-specific notation to describe function
// signatures compactly: return type, parameter types, and side effect annotations.
// These specifications are stored in data tables and parsed during compiler initialization
// to build the runtime helper registry. This file provides the parsing infrastructure
// that interprets these specifications.
//
// Specification Format:
// Signatures use a simple text format: "RetType(ParamType1, ParamType2, ...)"
// with type names matching IL core types (i32, f64, ptr, etc.). Effect annotations
// like "noalias" and "readonly" may be attached to parameters.
//
// The parser handles whitespace, validates type names, splits parameter lists,
// and constructs RuntimeSignature objects that the compiler uses to generate
// correct IL for runtime helper calls.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/runtime/RuntimeSignatures.hpp"

#include <string_view>
#include <vector>

namespace il::runtime
{

/// @brief Trim ASCII whitespace from both ends of the provided view.
std::string_view trim(std::string_view text);

/// @brief Split a delimited type list into individual type tokens.
std::vector<std::string_view> splitTypeList(std::string_view text);

/// @brief Parse a runtime signature specification string.
RuntimeSignature parseSignatureSpec(std::string_view spec);

} // namespace il::runtime

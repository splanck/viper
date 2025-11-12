//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/OpcodeHandlerHelpers.hpp
// Purpose: Shared helper macros and utilities for VM opcode handlers to reduce
//          boilerplate and improve consistency across handler implementations.
// Key invariants: Helpers must preserve handler signatures and semantics.
// Ownership/Lifetime: Macros have no runtime ownership implications.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

/// @file
/// @brief Common helper macros and patterns for VM opcode handlers.
/// @details Provides reusable patterns to reduce boilerplate in opcode handlers,
///          such as marking unused parameters and common trap message formatting.

#include <cstddef>
#include <sstream>
#include <string>

namespace il::vm::detail
{

/// @brief Mark common unused control-flow parameters in opcode handlers.
/// @details Many arithmetic and comparison handlers don't use blocks, bb, or ip
///          parameters. This macro provides a consistent way to suppress warnings.
#define VM_HANDLER_UNUSED_CONTROL_PARAMS(blocks, bb, ip) \
    do                                                    \
    {                                                     \
        (void)(blocks);                                   \
        (void)(bb);                                       \
        (void)(ip);                                       \
    } while (false)

/// @brief Mark all unused parameters in a simple opcode handler.
/// @details For handlers that only use vm, fr, and in parameters.
#define VM_HANDLER_UNUSED_ALL_CONTROL(blocks, bb, ip) VM_HANDLER_UNUSED_CONTROL_PARAMS(blocks, bb, ip)

/// @brief Build a formatted error message for argument count mismatches.
/// @param name Function or runtime name.
/// @param expected Expected argument count.
/// @param actual Actual argument count provided.
/// @return Formatted error string suitable for trap reporting.
/// @details Uses string_view to avoid unnecessary string copies when called
///          with string literals or existing strings.
inline std::string formatArgumentCountError(std::string_view name, size_t expected, size_t actual)
{
    std::string result;
    result.reserve(name.size() + 64);  // Pre-allocate reasonable buffer
    result.append(name);
    result.append(": expected ");
    result.append(std::to_string(expected));
    result.append(" argument(s), got ");
    result.append(std::to_string(actual));
    if (actual > expected)
        result.append(" (excess runtime operands)");
    return result;
}

/// @brief Build a formatted error message for out-of-range register access.
/// @param tempId Register/temporary ID that was out of range.
/// @param regCount Total number of registers available.
/// @param functionName Name of the function containing the register.
/// @param blockLabel Label of the basic block (may be empty).
/// @return Formatted error string suitable for trap reporting.
/// @details Uses string_view to avoid unnecessary string copies.
inline std::string formatRegisterRangeError(size_t tempId,
                                            size_t regCount,
                                            std::string_view functionName,
                                            std::string_view blockLabel)
{
    std::string result;
    result.reserve(functionName.size() + blockLabel.size() + 80);
    result.append("temp %");
    result.append(std::to_string(tempId));
    result.append(" out of range (regs=");
    result.append(std::to_string(regCount));
    result.append(") in function ");
    result.append(functionName);
    if (!blockLabel.empty())
    {
        result.append(", block ");
        result.append(blockLabel);
    }
    return result;
}

} // namespace il::vm::detail

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/verify/Rules.hpp
//
// Summary:
//   Declares the shared rule registry used by the IL verifier.  Each rule is a
//   lightweight predicate that inspects an instruction and, when it fails,
//   provides a concise diagnostic message.  Registrations are centrally managed
//   so verifiers can iterate the available checks without bespoke wiring.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Rule registry interface for IL verification.
/// @details Exposes the light-weight structures used by verifier passes to
///          iterate over registered rules.  Rules encapsulate individual
///          validation predicates and return human-readable diagnostics when a
///          violation is detected.

#pragma once

#include <string>
#include <vector>

namespace il::core
{
struct Instr;
struct Function;
} // namespace il::core

/// @brief Metadata describing a verifier rule predicate.
struct Rule
{
    const char *name; ///< Stable rule identifier used in diagnostics.
    bool (*check)(const il::core::Function &, const il::core::Instr &, std::string &out_msg);
};

/// @brief Access the global list of verifier rules.
/// @return Const reference to the registry of rule descriptors.
const std::vector<Rule> &viper_verifier_rules();

/// @brief Separator used to encode rule messages with location metadata.
inline constexpr char kRuleMessageSep = '\x1F';


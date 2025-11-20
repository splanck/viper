//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/sem/OverloadResolution.hpp
// Purpose: Resolve method overloads (including property accessors) with
// //         deterministic ranking and diagnostics.
// Key invariants: No user-defined conversions; only exact or widening numeric.
// Ownership/Lifetime: Borrows OopIndex and DiagnosticEmitter; owns no state.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Semantic_OOP.hpp"
#include "support/source_location.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::basic::sem
{

struct ResolvedMethod
{
    const ClassInfo *owner{nullptr};
    const ClassInfo::MethodInfo *method{nullptr};
    std::string qualifiedClass; // declared casing
    std::string methodName;     // selected method name
};

/// @brief Resolve an overloaded method on a class by name and argument types.
/// @param index OOP index to query for method candidates.
/// @param qualifiedClass Class context (qualified, case-insensitive ok).
/// @param methodName Name of method without implicit accessor prefix.
/// @param isStatic True for static calls; false for instance calls.
/// @param argTypes Argument AST types (excluding implicit ME).
/// @param currentClass Fully-qualified name of current lowering class for private checks.
/// @param de Optional diagnostics sink.
/// @param loc Location for error emission.
/// @return Selected method when unique best match exists; empty on error.
std::optional<ResolvedMethod> resolveMethodOverload(const OopIndex &index,
                                                    std::string_view qualifiedClass,
                                                    std::string_view methodName,
                                                    bool isStatic,
                                                    const std::vector<Type> &argTypes,
                                                    std::string_view currentClass,
                                                    DiagnosticEmitter *de,
                                                    il::support::SourceLoc loc);

} // namespace il::frontends::basic::sem

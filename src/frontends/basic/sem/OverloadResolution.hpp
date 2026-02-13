//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "frontends/basic/OopIndex.hpp"
#include "support/source_location.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::basic::sem
{

/// @brief Result of overload resolution for a method call on a user-defined class.
/// @details Contains the owning class, selected method, and name information
///          needed by the lowerer to emit the correct call instruction.
struct ResolvedMethod
{
    const ClassInfo *owner{nullptr};              ///< Class that declares the selected method.
    const ClassInfo::MethodInfo *method{nullptr}; ///< Selected method overload metadata.
    std::string qualifiedClass;                   ///< Qualified class name in declared casing.
    std::string methodName;                       ///< Selected method name after resolution.
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

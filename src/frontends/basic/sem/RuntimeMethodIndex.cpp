//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RuntimeMethodIndex.cpp
/// @brief Implementation of runtime method lookup for the BASIC frontend.
///
/// @details This file implements the RuntimeMethodIndex class which provides
/// the BASIC frontend's interface to runtime class methods. It delegates to
/// the IL-layer RuntimeRegistry for actual method lookup and converts the
/// results from IL types to BASIC types.
///
/// ## Implementation Strategy
///
/// Rather than maintaining its own method index, this implementation:
///
/// 1. Delegates lookups to RuntimeRegistry::instance()
/// 2. Converts ILScalarType results to BasicType
/// 3. Packages the result in a RuntimeMethodInfo structure
///
/// This ensures all frontends use identical signature information and
/// eliminates duplicate parsing/indexing code.
///
/// ## Performance Characteristics
///
/// - **seed()**: O(1) - no-op since RuntimeRegistry handles indexing
/// - **find()**: O(1) - single hash lookup in RuntimeRegistry
/// - **candidates()**: O(n) where n is the number of methods in the index
///   (only used for error messages, so performance is acceptable)
///
/// ## Thread Safety
///
/// All operations are thread-safe:
/// - RuntimeRegistry is immutable after construction
/// - runtimeMethodIndex() uses function-local static initialization
/// - No mutable state in RuntimeMethodIndex
///
/// @see RuntimeMethodIndex.hpp - Interface documentation
/// @see il::runtime::RuntimeRegistry - Underlying method registry
///
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/RuntimeMethodIndex.hpp"

namespace il::frontends::basic
{

//===----------------------------------------------------------------------===//
// Type Conversion
//===----------------------------------------------------------------------===//

/// @brief Converts an IL scalar type to a BASIC type.
///
/// @details Maps the frontend-agnostic ILScalarType enumeration to the
/// BASIC-specific BasicType enumeration. The mapping is direct—each IL
/// type has exactly one corresponding BASIC type.
///
/// The switch covers all ILScalarType values:
/// - I64 → Int: BASIC uses "Int" for 64-bit integers
/// - F64 → Float: BASIC uses "Float" for 64-bit floating point
/// - Bool → Bool: Direct mapping
/// - String → String: Direct mapping
/// - Void → Void: Direct mapping (used for SUBs)
/// - Object → Object: Opaque pointer to runtime class instance
/// - Unknown → Unknown: Error/fallback case
///
/// @note The compiler will warn if new ILScalarType values are added
///       but not handled here, ensuring the mapping stays complete.
///
BasicType toBasicType(il::runtime::ILScalarType t)
{
    switch (t)
    {
        case il::runtime::ILScalarType::I64:
            // BASIC uses "Int" terminology for integers
            return BasicType::Int;

        case il::runtime::ILScalarType::F64:
            // BASIC uses "Float" terminology for floating point
            return BasicType::Float;

        case il::runtime::ILScalarType::Bool:
            // Direct mapping - boolean type
            return BasicType::Bool;

        case il::runtime::ILScalarType::String:
            // Direct mapping - string reference type
            return BasicType::String;

        case il::runtime::ILScalarType::Void:
            // Direct mapping - no return value (SUBs)
            return BasicType::Void;

        case il::runtime::ILScalarType::Object:
            // Opaque pointer to runtime class instance
            return BasicType::Object;

        case il::runtime::ILScalarType::Unknown:
        default:
            // Fallback for unrecognized types or parse errors
            return BasicType::Unknown;
    }
}

//===----------------------------------------------------------------------===//
// RuntimeMethodIndex Implementation
//===----------------------------------------------------------------------===//

/// @brief Initializes the method index.
///
/// @details This method is now a no-op. Previously, it parsed all runtime
/// signatures and built an internal index. After the RuntimeRegistry
/// refactoring, indexing is done once at the IL layer and shared across
/// all frontends.
///
/// The method is retained for backward compatibility with existing code
/// that calls seed() during initialization.
///
void RuntimeMethodIndex::seed()
{
    // No-op: RuntimeRegistry handles all indexing at the IL layer.
    // This method is kept for API compatibility.
}

/// @brief Finds a runtime method by class, name, and arity.
///
/// @details Delegates to RuntimeRegistry::findMethod() and converts the
/// result from IL types to BASIC types. The conversion process:
///
/// 1. Look up the method in RuntimeRegistry
/// 2. If found, extract the ParsedMethod result
/// 3. Convert the return type using toBasicType()
/// 4. Convert each parameter type using toBasicType()
/// 5. Package everything into a RuntimeMethodInfo
///
/// The arity check is handled by RuntimeRegistry—methods are indexed
/// by class|method#arity, so only exact arity matches are returned.
///
std::optional<RuntimeMethodInfo> RuntimeMethodIndex::find(std::string_view classQName,
                                                          std::string_view method,
                                                          std::size_t arity) const
{
    // Delegate to the IL-layer RuntimeRegistry
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    auto parsed = registry.findMethod(classQName, method, arity);

    if (!parsed)
        return std::nullopt;

    // Convert the ParsedMethod to RuntimeMethodInfo
    RuntimeMethodInfo info;

    // Copy the extern target name
    info.target = parsed->target ? parsed->target : "";

    // Convert return type from IL to BASIC
    info.ret = toBasicType(parsed->signature.returnType);

    // Convert parameter types from IL to BASIC
    info.args.reserve(parsed->signature.params.size());
    for (auto p : parsed->signature.params)
        info.args.push_back(toBasicType(p));

    return info;
}

/// @brief Lists available method overloads for error messages.
///
/// @details Delegates directly to RuntimeRegistry::methodCandidates().
/// Used when a method call has the wrong number of arguments to show
/// the user what arities are available.
///
std::vector<std::string> RuntimeMethodIndex::candidates(std::string_view classQName,
                                                        std::string_view method) const
{
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    return registry.methodCandidates(classQName, method);
}

//===----------------------------------------------------------------------===//
// Global Index Access
//===----------------------------------------------------------------------===//

/// @brief Returns the global RuntimeMethodIndex singleton.
///
/// @details Uses a function-local static for thread-safe lazy initialization.
/// The index is constructed on first access and persists for the program
/// lifetime.
///
/// Since RuntimeMethodIndex no longer maintains internal state (it delegates
/// to RuntimeRegistry), this is essentially just providing a consistent
/// access point for the API.
///
RuntimeMethodIndex &runtimeMethodIndex()
{
    static RuntimeMethodIndex idx;
    return idx;
}

} // namespace il::frontends::basic

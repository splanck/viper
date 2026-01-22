//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RuntimeMethodIndex.hpp
/// @brief Runtime method lookup for the BASIC frontend using RuntimeRegistry.
///
/// @details This file provides the BASIC frontend's interface to runtime class
/// methods. It acts as a thin adapter between the IL-layer RuntimeRegistry
/// (which provides frontend-agnostic signatures) and the BASIC type system.
///
/// ## Architecture
///
/// The Viper compiler uses a unified RuntimeRegistry at the IL layer to store
/// parsed signatures for all runtime methods. Each frontend provides an adapter
/// to map IL types to their native type system:
///
/// ```
/// RuntimeRegistry (IL Layer)
///         │
///         │ ILScalarType → BasicType
///         ▼
/// RuntimeMethodIndex (BASIC Frontend)
///         │
///         │ RuntimeMethodInfo
///         ▼
/// BASIC Semantic Analyzer
/// ```
///
/// ## Type Mapping
///
/// The toBasicType() function maps IL scalar types to BASIC types:
///
/// | ILScalarType | BasicType | Description                    |
/// |--------------|-----------|--------------------------------|
/// | I64          | Int       | 64-bit signed integer          |
/// | F64          | Float     | 64-bit floating point          |
/// | Bool         | Bool      | Boolean true/false             |
/// | String       | String    | String reference               |
/// | Void         | Void      | No return value                |
/// | Object       | Object    | Runtime class instance pointer |
/// | Unknown      | Unknown   | Parse error or unrecognized    |
///
/// ## Usage Example
///
/// ```cpp
/// auto& index = runtimeMethodIndex();
/// index.seed();  // No-op, but kept for backward compatibility
///
/// // Look up String.Substring(start, length)
/// auto info = index.find("Viper.String", "Substring", 2);
/// if (info) {
///     // info->ret == BasicType::String
///     // info->args == [BasicType::Int, BasicType::Int]
///     // info->target == "Viper.String.Substring"
/// }
/// ```
///
/// ## Historical Note
///
/// Before the RuntimeRegistry refactoring, this class contained its own
/// signature parsing logic. That code has been consolidated into the IL
/// layer (RuntimeClasses.cpp) to ensure all frontends use identical
/// signature information.
///
/// @see il::runtime::RuntimeRegistry - Source of parsed signatures
/// @see toBasicType() - IL to BASIC type conversion
/// @see RuntimeMethodInfo - Method lookup result structure
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/BasicTypes.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::basic
{

//===----------------------------------------------------------------------===//
/// @name Type Conversion
/// @{
//===----------------------------------------------------------------------===//

/// @brief Converts an IL scalar type to a BASIC frontend type.
///
/// @details This function provides the type mapping between the frontend-agnostic
/// ILScalarType enumeration from the RuntimeRegistry and the BasicType enumeration
/// used by the BASIC semantic analyzer and lowerer.
///
/// The mapping is straightforward since both type systems represent the same
/// underlying IL types, just with different naming conventions:
///
/// - I64 maps to Int (BASIC uses "Int" for integers)
/// - F64 maps to Float (BASIC uses "Float" for floating point)
/// - Bool maps directly to Bool
/// - String maps directly to String
/// - Void maps directly to Void
/// - Object maps directly to Object (opaque pointer)
/// - Unknown maps to Unknown (error case)
///
/// @param t The IL scalar type from a parsed runtime signature.
///
/// @return The corresponding BasicType. Returns BasicType::Unknown for
///         unrecognized or error cases.
///
/// @note This function is used internally by RuntimeMethodIndex::find() to
///       convert signature types. It can also be used directly when working
///       with RuntimeRegistry results.
///
BasicType toBasicType(il::runtime::ILScalarType t);

/// @}

//===----------------------------------------------------------------------===//
/// @name Method Lookup Types
/// @{
//===----------------------------------------------------------------------===//

/// @brief Information about a runtime method returned by lookup.
///
/// @details This structure contains all the information needed by the BASIC
/// semantic analyzer to type-check a runtime method call and by the lowerer
/// to generate the correct extern call.
///
/// ## Fields
///
/// - **ret**: The method's return type in BASIC type system terms. Used for
///   type checking the call expression's result.
///
/// - **args**: Parameter types excluding the receiver (self/this). For a
///   method like String.Substring(start, length), this contains [Int, Int].
///   The receiver is handled separately at the call site.
///
/// - **target**: The canonical extern function name to use in generated IL
///   (e.g., "Viper.String.Substring"). This is used directly in the extern
///   call instruction.
///
/// ## Example
///
/// For `Viper.String.Substring`:
/// ```
/// RuntimeMethodInfo {
///     ret: BasicType::String,
///     args: [BasicType::Int, BasicType::Int],
///     target: "Viper.String.Substring"
/// }
/// ```
///
struct RuntimeMethodInfo
{
    /// @brief Return type of the method.
    BasicType ret{BasicType::Unknown};

    /// @brief Parameter types excluding the implicit receiver (arg0).
    std::vector<BasicType> args;

    /// @brief Canonical extern name for IL generation (e.g., "Viper.String.Substring").
    std::string target;
};

/// @}

//===----------------------------------------------------------------------===//
/// @name Runtime Method Index
/// @{
//===----------------------------------------------------------------------===//

/// @brief Provides O(1) lookup for runtime class methods in the BASIC frontend.
///
/// @details This class wraps the IL-layer RuntimeRegistry to provide BASIC-specific
/// method lookup. It converts the registry's ILScalarType-based signatures to
/// BasicType-based RuntimeMethodInfo structures.
///
/// ## Thread Safety
///
/// The underlying RuntimeRegistry is immutable after construction. This class
/// performs read-only lookups and is safe for concurrent use.
///
/// ## Lookup Semantics
///
/// Methods are looked up by:
/// 1. **Class name**: Fully-qualified name (e.g., "Viper.String")
/// 2. **Method name**: The method identifier (e.g., "Substring")
/// 3. **Arity**: Number of explicit parameters (excludes receiver)
///
/// This supports method overloading by arity—different methods with the same
/// name but different parameter counts are distinct entries.
///
/// ## Example Usage
///
/// ```cpp
/// RuntimeMethodIndex& index = runtimeMethodIndex();
///
/// // Look up a method
/// auto info = index.find("Viper.String", "Substring", 2);
/// if (info) {
///     emit_call(info->target, ...);
/// }
///
/// // Get available overloads for error messages
/// auto overloads = index.candidates("Viper.String", "Substring");
/// // Returns ["Substring/1", "Substring/2"] if both arities exist
/// ```
///
class RuntimeMethodIndex
{
  public:
    /// @brief Initializes the method index (currently a no-op).
    ///
    /// @details Historically, this method parsed and indexed all runtime
    /// signatures. After the RuntimeRegistry refactoring, indexing is done
    /// at the IL layer. This method is retained for API compatibility but
    /// performs no work.
    ///
    /// @note Safe to call multiple times; idempotent.
    ///
    void seed();

    /// @brief Finds a runtime method by class, name, and parameter count.
    ///
    /// @details Performs an O(1) lookup in the RuntimeRegistry and converts
    /// the result to BASIC types. Returns std::nullopt if no method matches
    /// the specified class, name, and arity combination.
    ///
    /// The arity parameter excludes the implicit receiver—for a method call
    /// like `obj.Method(a, b)`, the arity is 2, not 3.
    ///
    /// @param classQName The fully-qualified class name (e.g., "Viper.String").
    ///                   Lookup is case-insensitive.
    /// @param method The method name (e.g., "Substring").
    ///               Lookup is case-insensitive.
    /// @param arity The number of explicit arguments (excluding receiver).
    ///
    /// @return RuntimeMethodInfo if found, std::nullopt otherwise.
    ///
    /// @par Example
    /// ```cpp
    /// // Look up String.Substring with 2 parameters
    /// auto info = index.find("Viper.String", "Substring", 2);
    /// if (info) {
    ///     assert(info->ret == BasicType::String);
    ///     assert(info->args.size() == 2);
    /// }
    /// ```
    ///
    [[nodiscard]] std::optional<RuntimeMethodInfo> find(std::string_view classQName,
                                                        std::string_view method,
                                                        std::size_t arity) const;

    /// @brief Lists available method overloads for diagnostic messages.
    ///
    /// @details When a method call has the wrong number of arguments, this
    /// function provides a list of valid arities. Useful for generating
    /// helpful error messages like "Substring expects 1 or 2 arguments".
    ///
    /// @param classQName The fully-qualified class name.
    /// @param method The method name.
    ///
    /// @return List of strings like "MethodName/arity" for each overload.
    ///         Returns empty vector if no methods match.
    ///
    /// @par Example
    /// ```cpp
    /// auto overloads = index.candidates("Viper.String", "Substring");
    /// // If Substring has 1-arg and 2-arg versions:
    /// // overloads == ["Substring/1", "Substring/2"]
    /// ```
    ///
    [[nodiscard]] std::vector<std::string> candidates(std::string_view classQName,
                                                      std::string_view method) const;
};

/// @}

//===----------------------------------------------------------------------===//
/// @name Global Index Access
/// @{
//===----------------------------------------------------------------------===//

/// @brief Returns the global RuntimeMethodIndex singleton.
///
/// @details Provides access to the shared method index instance. The index
/// is lazily constructed on first access and persists for the program lifetime.
///
/// @return Reference to the global RuntimeMethodIndex.
///
/// @note Thread-safe; uses function-local static initialization.
///
RuntimeMethodIndex &runtimeMethodIndex();

/// @}

} // namespace il::frontends::basic

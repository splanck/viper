//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/RuntimeRegistry.hpp
// Purpose: Unified runtime API registry for all frontend consumption.
//
// This header provides a single point of access to runtime function metadata
// generated from runtime.def. Frontends use this registry to:
//   - Look up function return types for semantic analysis
//   - Validate runtime API availability
//   - Resolve class methods and properties
//
// Key invariants:
//   - Single source of truth (runtime.def via generated includes)
//   - Thread-safe after static initialization
//   - All lookups are O(1) via hash maps
//
// Ownership/Lifetime:
//   - Static storage duration; safe to use process-wide
//
// Links:
//   - il/runtime/runtime.def
//   - il/runtime/RuntimeNameMap.hpp
//   - il/runtime/classes/RuntimeClasses.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/runtime/RuntimeNameMap.hpp"
#include "il/runtime/RuntimeSignatureParser.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::frontends::common
{

/// @brief Return type classification for frontend semantic analysis.
enum class RuntimeReturnKind
{
    Void,    ///< No return value (void)
    Integer, ///< 64-bit integer (i64)
    Boolean, ///< Boolean (i1)
    Float,   ///< 64-bit float (f64)
    String,  ///< String type (str)
    Object,  ///< Object reference (obj)
    Pointer, ///< Raw pointer (ptr)
    Unknown, ///< Unknown/unregistered
};

/// @brief Argument type for signature validation.
enum class RuntimeArgKind
{
    Integer, ///< i64
    Boolean, ///< i1
    Float,   ///< f64
    String,  ///< str
    Object,  ///< obj
    Pointer, ///< ptr
};

/// @brief Full signature information for a runtime function.
struct RuntimeFunctionInfo
{
    std::string_view canonicalName; ///< Viper.* canonical name
    std::string_view runtimeSymbol; ///< C rt_* symbol
    std::string_view signature;     ///< Signature string e.g., "i64(str,i64)"
    RuntimeReturnKind returnKind;   ///< Parsed return type
    std::vector<RuntimeArgKind> argKinds; ///< Parsed argument types
};

/// @brief Class information for OOP-style APIs.
struct RuntimeClassInfo
{
    std::string_view name;                        ///< Class name (e.g., "Viper.String")
    std::string_view constructor;                 ///< Constructor canonical name or empty
    std::vector<runtime::RuntimeProperty> properties; ///< Class properties
    std::vector<runtime::RuntimeMethod> methods;      ///< Class methods
};

/// @brief Central registry for all Viper.* runtime APIs.
///
/// @details Provides unified access to runtime function metadata for all frontends.
/// Built from the generated data in runtime.def, this registry enables:
///   - Type checking for runtime calls
///   - API discovery and validation
///   - Class/method resolution for OOP-style calls
///
/// @example
/// @code
///   auto& reg = RuntimeRegistry::instance();
///   if (auto info = reg.findFunction("Viper.String.Len")) {
///       // info->returnKind == RuntimeReturnKind::Integer
///   }
///   if (auto cls = reg.findClass("Viper.Collections.List")) {
///       for (const auto& method : cls->methods) { ... }
///   }
/// @endcode
class RuntimeRegistry
{
  public:
    /// @brief Get the singleton registry instance.
    /// @return Reference to the process-wide registry.
    static const RuntimeRegistry &instance();

    /// @brief Look up a runtime function by canonical name.
    /// @param canonicalName Viper.* name (e.g., "Viper.String.Len")
    /// @return Function info if found, nullopt otherwise.
    [[nodiscard]] std::optional<RuntimeFunctionInfo> findFunction(
        std::string_view canonicalName) const;

    /// @brief Look up a runtime class by name.
    /// @param className Class name (e.g., "Viper.String")
    /// @return Class info if found, nullopt otherwise.
    [[nodiscard]] std::optional<RuntimeClassInfo> findClass(std::string_view className) const;

    /// @brief Check if a canonical name is a registered runtime function.
    /// @param canonicalName Viper.* name to check
    /// @return True if registered.
    [[nodiscard]] bool hasFunction(std::string_view canonicalName) const;

    /// @brief Check if a class name is a registered runtime class.
    /// @param className Class name to check
    /// @return True if registered.
    [[nodiscard]] bool hasClass(std::string_view className) const;

    /// @brief Get the return type kind for a function.
    /// @param canonicalName Viper.* name
    /// @return Return type kind, or Unknown if not found.
    [[nodiscard]] RuntimeReturnKind getReturnKind(std::string_view canonicalName) const;

    /// @brief Get the C runtime symbol for a canonical name.
    /// @param canonicalName Viper.* name
    /// @return Runtime symbol (e.g., "rt_string_len") or nullopt.
    [[nodiscard]] std::optional<std::string_view> getRuntimeSymbol(
        std::string_view canonicalName) const;

    /// @brief Get all registered function names.
    /// @return Vector of all canonical function names.
    [[nodiscard]] const std::vector<std::string_view> &allFunctionNames() const;

    /// @brief Get all registered class names.
    /// @return Vector of all class names.
    [[nodiscard]] const std::vector<std::string_view> &allClassNames() const;

    /// @brief Parse a signature string into return kind.
    /// @param signature Signature string (e.g., "i64(str,i64)")
    /// @return Parsed return kind.
    [[nodiscard]] static RuntimeReturnKind parseReturnKind(std::string_view signature);

    /// @brief Parse a type abbreviation into return kind.
    /// @param typeAbbrev Type abbreviation (e.g., "i64", "str", "obj")
    /// @return Parsed return kind.
    [[nodiscard]] static RuntimeReturnKind typeToReturnKind(std::string_view typeAbbrev);

    /// @brief Parse a type abbreviation into argument kind.
    /// @param typeAbbrev Type abbreviation
    /// @return Parsed argument kind.
    [[nodiscard]] static RuntimeArgKind typeToArgKind(std::string_view typeAbbrev);

  private:
    RuntimeRegistry();

    void buildFunctionIndex();
    void buildClassIndex();

    // Function lookup by canonical name
    std::unordered_map<std::string_view, RuntimeFunctionInfo> functionIndex_;

    // Class lookup by name
    std::unordered_map<std::string_view, RuntimeClassInfo> classIndex_;

    // Ordered lists for iteration
    std::vector<std::string_view> allFunctions_;
    std::vector<std::string_view> allClasses_;
};

} // namespace il::frontends::common

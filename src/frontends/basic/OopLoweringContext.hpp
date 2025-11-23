//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/OopLoweringContext.hpp
// Purpose: Context object for OOP lowering pipeline.
// Key invariants: Provides consistent access to OOP metadata and lowering state.
// Ownership/Lifetime: Non-owning references to Lowerer and OopIndex; short-lived.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <unordered_map>

namespace il::frontends::basic
{

// Forward declarations to minimize dependencies
class Lowerer;
class OopIndex;
class ClassInfo;

/// @brief Context object passed through OOP lowering pipeline.
/// @details Provides a consistent interface for OOP lowering functions to access
///          metadata and emit IL, without directly coupling to Lowerer internals.
///          This context is created at the start of OOP-related lowering operations
///          and passed through the pipeline.
struct OopLoweringContext
{
    /// @brief Reference to the main lowering state.
    /// @details Provides access to IL emission, symbol tables, and general lowering utilities.
    Lowerer &lowerer;

    /// @brief Reference to the OOP metadata index.
    /// @details Contains class definitions, method signatures, and inheritance information.
    OopIndex &oopIndex;

    /// @brief Cache for resolved class info.
    /// @details Maps qualified class names to their metadata, avoiding repeated lookups.
    std::unordered_map<std::string, const ClassInfo*> classCache;

    /// @brief Create a context for OOP lowering.
    /// @param lowerer Reference to the active lowering state.
    /// @param oopIndex Reference to the OOP metadata index.
    OopLoweringContext(Lowerer &lowerer, OopIndex &oopIndex)
        : lowerer(lowerer), oopIndex(oopIndex) {}

    /// @brief Look up class info with caching.
    /// @param className Qualified class name.
    /// @return Pointer to class info or nullptr if not found.
    const ClassInfo* findClassInfo(const std::string &className);
};

} // namespace il::frontends::basic
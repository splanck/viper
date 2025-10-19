// File: src/frontends/basic/Semantic_OOP.hpp
// Purpose: Declares lightweight OOP indexing helpers for BASIC class metadata.
// Key invariants: Index stores one entry per class name with immutable signature data.
// Ownership/Lifetime: OopIndex stores copies of AST metadata without owning AST nodes.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/AST.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace il::frontends::basic
{

class DiagnosticEmitter;

/// @brief Captures the signature of a CLASS method.
struct MethodSig
{
    /// Ordered parameter types, excluding the implicit instance parameter.
    std::vector<Type> paramTypes;

    /// Optional return type for methods producing a value.
    std::optional<Type> returnType;
};

/// @brief Aggregated information extracted from a CLASS declaration.
struct ClassInfo
{
    /// @brief Field metadata copied from the CLASS definition.
    struct FieldInfo
    {
        std::string name; ///< Declared field name.
        Type type = Type::I64; ///< Declared field type.
    };

    std::string name; ///< Class identifier.
    std::vector<FieldInfo> fields; ///< Ordered field declarations.
    bool hasConstructor = false; ///< True if CLASS declares a constructor.
    bool hasDestructor = false; ///< True if CLASS declares a destructor.
    std::unordered_map<std::string, MethodSig> methods; ///< Declared methods indexed by name.
};

/// @brief Container mapping class names to extracted metadata.
class OopIndex
{
  public:
    using ClassTable = std::unordered_map<std::string, ClassInfo>;

    /// @brief Access the mutable class table.
    [[nodiscard]] ClassTable &classes() noexcept { return classes_; }

    /// @brief Access the immutable class table.
    [[nodiscard]] const ClassTable &classes() const noexcept { return classes_; }

    /// @brief Remove all indexed classes.
    void clear() noexcept { classes_.clear(); }

    /// @brief Find a class by name.
    [[nodiscard]] ClassInfo *findClass(const std::string &name);

    /// @brief Find a class by name (const overload).
    [[nodiscard]] const ClassInfo *findClass(const std::string &name) const;

  private:
    ClassTable classes_;
};

/// @brief Populate @p index with class metadata extracted from @p program.
void buildOopIndex(const Program &program, OopIndex &index, DiagnosticEmitter *emitter);

} // namespace il::frontends::basic



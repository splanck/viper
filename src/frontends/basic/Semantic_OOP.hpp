// File: src/frontends/basic/Semantic_OOP.hpp
// Purpose: Declares lightweight OOP indexing helpers for BASIC class metadata.
// Key invariants: Index stores one entry per class name with immutable signature data.
// Ownership/Lifetime: OopIndex stores copies of AST metadata without owning AST nodes.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/NodeFwd.hpp"
#include "frontends/basic/BasicTypes.hpp"
#include "support/source_location.hpp"

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

    /// Access specifier for the method (default Public).
    Access access{Access::Public};
};

/// @brief Aggregated information extracted from a CLASS declaration.
struct ClassInfo
{
    /// @brief Field metadata copied from the CLASS definition.
    struct FieldInfo
    {
        std::string name;      ///< Declared field name.
        Type type = Type::I64; ///< Declared field type.
        Access access{Access::Public}; ///< Field access control.
    };

    /// @brief Signature metadata for constructor parameters.
    struct CtorParam
    {
        Type type = Type::I64; ///< Declared parameter type.
        bool isArray = false;  ///< True when parameter declared with trailing ().
    };

    std::string name;                  ///< Unqualified class identifier.
    std::string qualifiedName;         ///< Fully-qualified class name (namespaces + name).
    std::string baseQualified;         ///< Fully-qualified base name (empty when none or unresolved).
    bool isAbstract{false};            ///< True when class is abstract.
    bool isFinal{false};               ///< True when class is final.
    il::support::SourceLoc loc{};      ///< Location of the CLASS keyword.
    std::vector<FieldInfo> fields;     ///< Ordered field declarations.
    bool hasConstructor = false;       ///< True if CLASS declares a constructor.
    bool hasSynthCtor = false;         ///< True when lowering must synthesise a constructor.
    bool hasDestructor = false;        ///< True if CLASS declares a destructor.
    std::vector<CtorParam> ctorParams; ///< Constructor signature if declared.
    /// @brief Extended method metadata used for vtable construction and checks.
    struct MethodInfo
    {
        MethodSig sig;         ///< Signature (params/return/access).
        bool isVirtual = false;///< Declared or implied virtual.
        bool isAbstract = false;///< Declared abstract.
        bool isFinal = false;  ///< Declared final.
        int slot = -1;         ///< Virtual slot index; -1 for non-virtual.
    };

    /// Declared methods indexed by name.
    std::unordered_map<std::string, MethodInfo> methods;

    /// Ordered virtual method names by slot for deterministic ABI layout.
    std::vector<std::string> vtable;

    /// Method declaration source locations (for diagnostics).
    std::unordered_map<std::string, il::support::SourceLoc> methodLocs;
};

/// @brief Container mapping class names to extracted metadata.
class OopIndex
{
  public:
    using ClassTable = std::unordered_map<std::string, ClassInfo>;

    /// @brief Access the mutable class table.
    [[nodiscard]] ClassTable &classes() noexcept
    {
        return classes_;
    }

    /// @brief Access the immutable class table.
    [[nodiscard]] const ClassTable &classes() const noexcept
    {
        return classes_;
    }

    /// @brief Remove all indexed classes.
    void clear() noexcept
    {
        classes_.clear();
    }

    /// @brief Find a class by name.
    [[nodiscard]] ClassInfo *findClass(const std::string &name);

    /// @brief Find a class by name (const overload).
    [[nodiscard]] const ClassInfo *findClass(const std::string &name) const;

  private:
    ClassTable classes_;
};

/// @brief Populate @p index with class metadata extracted from @p program.
void buildOopIndex(const Program &program, OopIndex &index, DiagnosticEmitter *emitter);

/// @brief Query the virtual slot for a method if it is virtual.
/// @param index OOP index containing class metadata.
/// @param qualifiedClass Fully-qualified class name.
/// @param methodName Method identifier.
/// @return Slot index (>=0) when virtual; -1 for non-virtual or when not found.
int getVirtualSlot(const OopIndex &index, const std::string &qualifiedClass, const std::string &methodName);

} // namespace il::frontends::basic

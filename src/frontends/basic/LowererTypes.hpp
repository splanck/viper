//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/LowererTypes.hpp
// Purpose: Core type definitions shared across Lowerer components.
// Key invariants: Types are POD or simple structs; no methods beyond trivial
//                 accessors.
// Ownership/Lifetime: Included transitively via Lowerer.hpp.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/BasicTypes.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "viper/il/Module.hpp"
#include <optional>
#include <string>
#include <vector>

namespace il::frontends::basic
{

/// @brief Result of lowering an expression to a value and type pair.
struct RVal
{
    il::core::Value value;
    il::core::Type type;
};

/// @brief Result of lowering a PRINT# argument to a string.
struct PrintChArgString
{
    il::core::Value text;
    std::optional<il::runtime::RuntimeFeature> feature;
};

/// @brief Result of lowering an array access expression.
struct ArrayAccess
{
    il::core::Value base;  ///< Array handle loaded from the BASIC slot.
    il::core::Value index; ///< Zero-based element index, coerced to i64.
};

/// @brief Classify how an array access will be consumed.
enum class ArrayAccessKind
{
    Load,  ///< The caller will read from the computed element.
    Store, ///< The caller will write to the computed element.
};

/// @brief Aggregated metadata for a BASIC symbol.
struct SymbolInfo
{
    Type type{Type::I64};           ///< BASIC type derived from declarations or suffixes.
    bool hasType{false};            ///< True when @ref type was explicitly recorded.
    bool isArray{false};            ///< True when symbol refers to an array.
    bool isBoolean{false};          ///< True when scalar bool storage is required.
    bool referenced{false};         ///< Tracks whether lowering observed the symbol.
    bool isStatic{false};           ///< True when symbol is a STATIC procedure-local variable.
    std::optional<unsigned> slotId; ///< Stack slot id for the variable when materialized.
    std::optional<unsigned> arrayLengthSlot; ///< Optional slot for array length (bounds checks).
    std::string stringLabel;                 ///< Cached label for deduplicated string literals.
    bool isObject{false};                    ///< True when symbol references an object slot.
    std::string objectClass;                 ///< Class name for object symbols; empty otherwise.
    bool isByRefParam{false};                ///< True when symbol represents a BYREF parameter.
};

/// @brief Slot type and metadata for variable storage.
struct SlotType
{
    il::core::Type type{il::core::Type(il::core::Type::Kind::I64)};
    bool isArray{false};
    bool isBoolean{false};
    bool isObject{false};
    std::string objectClass;
};

/// @brief Variable storage location and metadata.
struct VariableStorage
{
    SlotType slotInfo;
    il::core::Value pointer;
    bool isField{false};
};

/// @brief Cached signature for a user-defined procedure.
struct ProcedureSignature
{
    il::core::Type retType{il::core::Type(il::core::Type::Kind::I64)}; ///< Declared return type.
    std::vector<il::core::Type> paramTypes; ///< Declared parameter types.
    std::vector<bool> byRefFlags;           ///< True when parameter is BYREF.
};

/// @brief Computed memory layout for a BASIC CLASS or TYPE declaration.
struct ClassLayout
{
    /// @brief Metadata describing a single field within the class layout.
    struct Field
    {
        std::string name;
        Type type{Type::I64};
        std::size_t offset{0};
        std::size_t size{0};
        /// @brief True when this field is declared as an array.
        /// @details Preserves array metadata from the AST so lowering can
        ///          distinguish implicit field-array accesses inside
        ///          methods (e.g., `inventory(i)`) from scalar fields.
        bool isArray{false};
        /// @brief Declared array extents (upper bounds per dimension).
        /// @details Used for multi-dimensional index linearization. Each
        ///          entry is an inclusive upper bound; length = bound + 1.
        std::vector<long long> arrayExtents{};
        /// @brief BUG-082 fix: Class name for object-typed fields.
        /// @details Empty for primitive types; holds the class name for object references.
        std::string objectClassName;
    };

    /// @brief Ordered field entries preserving declaration order.
    std::vector<Field> fields;
    /// @brief Mapping from field name to its index within @ref fields.
    std::unordered_map<std::string, std::size_t> fieldIndex;
    /// @brief Total storage size in bytes rounded up to the alignment requirement.
    std::size_t size{0};
    /// @brief Stable identifier assigned during OOP scanning for runtime dispatch.
    std::int64_t classId{0};

    [[nodiscard]] const Field *findField(std::string_view name) const
    {
        auto it = fieldIndex.find(std::string(name));
        if (it == fieldIndex.end())
            return nullptr;
        return &fields[it->second];
    }
};

/// @brief Describes the address and type of a resolved member field.
struct MemberFieldAccess
{
    il::core::Value ptr; ///< Pointer to the field storage.
    il::core::Type ilType{
        il::core::Type(il::core::Type::Kind::I64)}; ///< IL type used for loads/stores.
    ::il::frontends::basic::Type astType{::il::frontends::basic::Type::I64}; ///< Original AST type.
    std::string objectClassName; ///< BUG-082: Class name for object-typed fields.
};

/// @brief Field scope for tracking fields during class method lowering.
struct FieldScope
{
    const ClassLayout *layout{nullptr};
    std::unordered_map<std::string, SymbolInfo> symbols;
};

/// @brief Layout of blocks emitted for an IF/ELSEIF chain.
struct IfBlocks
{
    std::vector<std::size_t> tests; ///< indexes of test blocks
    std::vector<std::size_t> thens; ///< indexes of THEN blocks
    std::size_t elseIdx;            ///< index of ELSE block
    std::size_t exitIdx;            ///< index of common exit block
};

/// @brief Control-flow state emitted by structured statement helpers.
/// @details `cur` tracks the block left active after lowering, while
///          `after` stores the merge/done block when it survives the
///          lowering step. Helpers mark `fallthrough` when execution can
///          reach `after` without an explicit transfer, ensuring callers
///          can reason about terminators consistently.
struct CtrlState
{
    il::core::BasicBlock *cur{nullptr};   ///< Block left active after lowering.
    il::core::BasicBlock *after{nullptr}; ///< Merge/done block if retained.
    bool fallthrough{false};              ///< True when `after` remains reachable.

    [[nodiscard]] bool terminated() const
    {
        return !cur || cur->terminated;
    }
};

} // namespace il::frontends::basic

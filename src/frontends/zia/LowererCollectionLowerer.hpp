//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file LowererCollectionLowerer.hpp
/// @brief Focused helper for Zia collection and tuple expression lowering.
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/Lowerer.hpp"

namespace il::frontends::zia {

/// @brief Lowers list/set/map literals, tuple expressions, and index access.
///
/// @details This helper keeps collection-specific lowering decisions out of the
///          main Lowerer expression files while preserving Lowerer as the owner
///          of IL emission state.
class CollectionLowerer final {
  public:
    explicit CollectionLowerer(Lowerer &lowerer) : lowerer_(lowerer) {}

    /// @brief Lower a list literal `[a, b, ...]` to runtime list construction.
    LowerResult lowerListLiteral(ListLiteralExpr *expr);
    /// @brief Lower a set literal `{a, b, ...}` to runtime set construction.
    LowerResult lowerSetLiteral(SetLiteralExpr *expr);
    /// @brief Lower a map literal `{k: v, ...}` to runtime map construction.
    LowerResult lowerMapLiteral(MapLiteralExpr *expr);
    /// @brief Lower a tuple literal `(a, b, ...)` to a packed aggregate value.
    LowerResult lowerTuple(TupleExpr *expr);
    /// @brief Lower a tuple element access `t.N` to a field load.
    LowerResult lowerTupleIndex(TupleIndexExpr *expr);
    /// @brief Lower an index expression `base[idx]`, dispatching on the base
    ///        type (fixed array, string, or boxed collection).
    LowerResult lowerIndex(IndexExpr *expr);

  private:
    using Type = il::core::Type;
    using Value = il::core::Value;
    using Opcode = il::core::Opcode;

    Lowerer &lowerer_;

    LowerResult lowerBoxedElementLiteral(const std::vector<ExprPtr> &elements,
                                         const char *constructor,
                                         const char *addElement);
    /// @brief Compute the address of the tuple element at byte @p offset.
    Value emitTupleElementAddress(Value tuplePtr, size_t offset);
    /// @brief Compute @p basePtr + @p byteOffset as a runtime pointer.
    Value emitRuntimeOffsetAddress(Value basePtr, Value byteOffset);
    LowerResult lowerFixedArrayIndex(Value baseValue,
                                     Value indexValue,
                                     Type indexType,
                                     TypeRef baseType);
    /// @brief Lower `str[idx]` to a single-character/byte access on a string.
    LowerResult lowerStringIndex(Value baseValue, Value indexValue, Type indexType);
    LowerResult lowerBoxedCollectionIndex(Value baseValue,
                                          Value indexValue,
                                          Type indexType,
                                          IndexExpr *expr);
};

} // namespace il::frontends::zia

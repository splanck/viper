//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

    LowerResult lowerListLiteral(ListLiteralExpr *expr);
    LowerResult lowerSetLiteral(SetLiteralExpr *expr);
    LowerResult lowerMapLiteral(MapLiteralExpr *expr);
    LowerResult lowerTuple(TupleExpr *expr);
    LowerResult lowerTupleIndex(TupleIndexExpr *expr);
    LowerResult lowerIndex(IndexExpr *expr);

  private:
    using Type = il::core::Type;
    using Value = il::core::Value;
    using Opcode = il::core::Opcode;

    Lowerer &lowerer_;

    LowerResult lowerBoxedElementLiteral(const std::vector<ExprPtr> &elements,
                                         const char *constructor,
                                         const char *addElement);
    Value emitTupleElementAddress(Value tuplePtr, size_t offset);
    Value emitRuntimeOffsetAddress(Value basePtr, Value byteOffset);
    LowerResult lowerFixedArrayIndex(Value baseValue, Value indexValue, TypeRef baseType);
    LowerResult lowerStringIndex(Value baseValue, Value indexValue);
    LowerResult lowerBoxedCollectionIndex(Value baseValue, Value indexValue, IndexExpr *expr);
};

} // namespace il::frontends::zia

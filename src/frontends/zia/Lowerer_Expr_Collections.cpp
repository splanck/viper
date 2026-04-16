//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr_Collections.cpp
/// @brief Lowerer entry points for collection, tuple, and index expressions.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/LowererCollectionLowerer.hpp"

namespace il::frontends::zia {

LowerResult Lowerer::lowerListLiteral(ListLiteralExpr *expr) {
    return CollectionLowerer(*this).lowerListLiteral(expr);
}

LowerResult Lowerer::lowerSetLiteral(SetLiteralExpr *expr) {
    return CollectionLowerer(*this).lowerSetLiteral(expr);
}

LowerResult Lowerer::lowerMapLiteral(MapLiteralExpr *expr) {
    return CollectionLowerer(*this).lowerMapLiteral(expr);
}

LowerResult Lowerer::lowerTuple(TupleExpr *expr) {
    return CollectionLowerer(*this).lowerTuple(expr);
}

LowerResult Lowerer::lowerTupleIndex(TupleIndexExpr *expr) {
    return CollectionLowerer(*this).lowerTupleIndex(expr);
}

LowerResult Lowerer::lowerIndex(IndexExpr *expr) {
    return CollectionLowerer(*this).lowerIndex(expr);
}

} // namespace il::frontends::zia

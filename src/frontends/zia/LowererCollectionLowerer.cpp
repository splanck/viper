//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file LowererCollectionLowerer.cpp
/// @brief CollectionLowerer implementation for collection and tuple expressions.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/LowererCollectionLowerer.hpp"

#include "frontends/zia/RuntimeNames.hpp"

#include <utility>

namespace il::frontends::zia {

using namespace runtime;

LowerResult CollectionLowerer::lowerBoxedElementLiteral(const std::vector<ExprPtr> &elements,
                                                        const char *constructor,
                                                        const char *addElement) {
    Value collection = lowerer_.emitCallRet(Type(Type::Kind::Ptr), constructor, {});

    for (auto &elem : elements) {
        auto result = lowerer_.lowerExpr(elem.get());
        TypeRef elemType = lowerer_.sema_.typeOf(elem.get());
        Value boxed = lowerer_.emitBoxValue(result.value, result.type, elemType);
        lowerer_.emitCall(addElement, {collection, boxed});
    }

    return {collection, Type(Type::Kind::Ptr)};
}

LowerResult CollectionLowerer::lowerListLiteral(ListLiteralExpr *expr) {
    return lowerBoxedElementLiteral(expr->elements, kListNew, kListAdd);
}

LowerResult CollectionLowerer::lowerSetLiteral(SetLiteralExpr *expr) {
    return lowerBoxedElementLiteral(expr->elements, kSetNew, kSetPut);
}

LowerResult CollectionLowerer::lowerMapLiteral(MapLiteralExpr *expr) {
    Value map = lowerer_.emitCallRet(Type(Type::Kind::Ptr), kMapNew, {});

    for (auto &entry : expr->entries) {
        auto keyResult = lowerer_.lowerExpr(entry.key.get());
        auto valueResult = lowerer_.lowerExpr(entry.value.get());
        TypeRef valueType = lowerer_.sema_.typeOf(entry.value.get());
        Value boxedValue = lowerer_.emitBoxValue(valueResult.value, valueResult.type, valueType);
        lowerer_.emitCall(kMapSet, {map, keyResult.value, boxedValue});
    }

    return {map, Type(Type::Kind::Ptr)};
}

LowerResult CollectionLowerer::lowerTuple(TupleExpr *expr) {
    TypeRef tupleType = lowerer_.sema_.typeOf(expr);
    size_t tupleSize = tupleType->tupleElementTypes().size() * 8;

    unsigned slotId = lowerer_.nextTempId();
    il::core::Instr allocInstr;
    allocInstr.result = slotId;
    allocInstr.op = Opcode::Alloca;
    allocInstr.type = Type(Type::Kind::Ptr);
    allocInstr.operands = {Value::constInt(static_cast<int64_t>(tupleSize))};
    allocInstr.loc = lowerer_.curLoc_;
    lowerer_.blockMgr_.currentBlock()->instructions.push_back(std::move(allocInstr));
    Value tuplePtr = Value::temp(slotId);

    size_t offset = 0;
    for (auto &elem : expr->elements) {
        auto result = lowerer_.lowerExpr(elem.get());
        Value elemPtr = emitTupleElementAddress(tuplePtr, offset);
        lowerer_.emitStore(elemPtr, result.value, result.type);
        offset += 8;
    }

    return {tuplePtr, Type(Type::Kind::Ptr)};
}

LowerResult CollectionLowerer::lowerTupleIndex(TupleIndexExpr *expr) {
    auto tupleResult = lowerer_.lowerExpr(expr->tuple.get());

    TypeRef tupleType = lowerer_.sema_.typeOf(expr->tuple.get());
    TypeRef elemType = tupleType->tupleElementType(expr->index);
    Type ilType = lowerer_.mapType(elemType);

    Value elemPtr = emitTupleElementAddress(tupleResult.value, expr->index * 8);
    Value elemVal = lowerer_.emitLoad(elemPtr, ilType);
    return {elemVal, ilType};
}

LowerResult CollectionLowerer::lowerIndex(IndexExpr *expr) {
    auto base = lowerer_.lowerExpr(expr->base.get());
    auto index = lowerer_.lowerExpr(expr->index.get());

    TypeRef baseType = lowerer_.sema_.typeOf(expr->base.get());

    if (baseType && baseType->kind == TypeKindSem::FixedArray)
        return lowerFixedArrayIndex(base.value, index.value, baseType);

    if (baseType && baseType->kind == TypeKindSem::String)
        return lowerStringIndex(base.value, index.value);

    return lowerBoxedCollectionIndex(base.value, index.value, expr);
}

CollectionLowerer::Value CollectionLowerer::emitTupleElementAddress(Value tuplePtr, size_t offset) {
    if (offset == 0)
        return tuplePtr;
    return lowerer_.emitGEP(tuplePtr, static_cast<int64_t>(offset));
}

CollectionLowerer::Value CollectionLowerer::emitRuntimeOffsetAddress(Value basePtr,
                                                                     Value byteOffset) {
    unsigned gepId = lowerer_.nextTempId();
    il::core::Instr gepInstr;
    gepInstr.result = gepId;
    gepInstr.op = Opcode::GEP;
    gepInstr.type = Type(Type::Kind::Ptr);
    gepInstr.operands = {basePtr, byteOffset};
    gepInstr.loc = lowerer_.curLoc_;
    lowerer_.blockMgr_.currentBlock()->instructions.push_back(std::move(gepInstr));
    return Value::temp(gepId);
}

LowerResult CollectionLowerer::lowerFixedArrayIndex(Value baseValue,
                                                    Value indexValue,
                                                    TypeRef baseType) {
    TypeRef elemType = baseType->elementType();
    Type ilElemType = elemType ? lowerer_.mapType(elemType) : Type(Type::Kind::I64);
    size_t elemSize = lowerer_.getILTypeSize(ilElemType);

    unsigned mulId = lowerer_.nextTempId();
    il::core::Instr mulInstr;
    mulInstr.result = mulId;
    mulInstr.op = Opcode::Mul;
    mulInstr.type = Type(Type::Kind::I64);
    mulInstr.operands = {indexValue, Value::constInt(static_cast<int64_t>(elemSize))};
    mulInstr.loc = lowerer_.curLoc_;
    lowerer_.blockMgr_.currentBlock()->instructions.push_back(std::move(mulInstr));
    Value byteOffset = Value::temp(mulId);

    Value elemAddr = emitRuntimeOffsetAddress(baseValue, byteOffset);
    Value elemVal = lowerer_.emitLoad(elemAddr, ilElemType);
    return {elemVal, ilElemType};
}

LowerResult CollectionLowerer::lowerStringIndex(Value baseValue, Value indexValue) {
    Value one = Value::constInt(1);
    Value result =
        lowerer_.emitCallRet(Type(Type::Kind::Str), kStringSubstring, {baseValue, indexValue, one});
    return {result, Type(Type::Kind::Str)};
}

LowerResult CollectionLowerer::lowerBoxedCollectionIndex(Value baseValue,
                                                         Value indexValue,
                                                         IndexExpr *expr) {
    TypeRef baseType = lowerer_.sema_.typeOf(expr->base.get());
    Value boxed;
    if (baseType && baseType->kind == TypeKindSem::Map) {
        boxed = lowerer_.emitCallRet(Type(Type::Kind::Ptr), kMapGet, {baseValue, indexValue});
    } else {
        boxed = lowerer_.emitCallRet(Type(Type::Kind::Ptr), kListGet, {baseValue, indexValue});
    }

    TypeRef elemType = lowerer_.sema_.typeOf(expr);
    Type ilType = lowerer_.mapType(elemType);
    return lowerer_.emitUnboxValue(boxed, ilType, elemType);
}

} // namespace il::frontends::zia

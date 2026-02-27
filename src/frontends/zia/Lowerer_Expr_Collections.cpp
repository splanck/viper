//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr_Collections.cpp
/// @brief Collection expression lowering for the Zia IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"

namespace il::frontends::zia
{

using namespace runtime;

//=============================================================================
// Collection Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerListLiteral(ListLiteralExpr *expr)
{
    // Create a new list
    Value list = emitCallRet(Type(Type::Kind::Ptr), kListNew, {});

    // Add each element to the list (boxed)
    for (auto &elem : expr->elements)
    {
        auto result = lowerExpr(elem.get());
        TypeRef elemType = sema_.typeOf(elem.get());
        Value boxed = emitBoxValue(result.value, result.type, elemType);
        emitCall(kListAdd, {list, boxed});
    }

    return {list, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerMapLiteral(MapLiteralExpr *expr)
{
    Value map = emitCallRet(Type(Type::Kind::Ptr), kMapNew, {});

    for (auto &entry : expr->entries)
    {
        auto keyResult = lowerExpr(entry.key.get());
        auto valueResult = lowerExpr(entry.value.get());
        TypeRef valueType = sema_.typeOf(entry.value.get());
        Value boxedValue = emitBoxValue(valueResult.value, valueResult.type, valueType);
        emitCall(kMapSet, {map, keyResult.value, boxedValue});
    }

    return {map, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerTuple(TupleExpr *expr)
{
    // Get the tuple type from sema
    TypeRef tupleType = sema_.typeOf(expr);

    // Calculate total size (assuming 8 bytes per element for simplicity)
    size_t tupleSize = tupleType->tupleElementTypes().size() * 8;

    // Allocate space for the tuple on the stack
    unsigned slotId = nextTempId();
    il::core::Instr allocInstr;
    allocInstr.result = slotId;
    allocInstr.op = Opcode::Alloca;
    allocInstr.type = Type(Type::Kind::Ptr);
    allocInstr.operands = {Value::constInt(static_cast<int64_t>(tupleSize))};
    allocInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(allocInstr);
    Value tuplePtr = Value::temp(slotId);

    // Store each element in the tuple
    size_t offset = 0;
    for (auto &elem : expr->elements)
    {
        auto result = lowerExpr(elem.get());

        // Calculate element pointer
        Value elemPtr = tuplePtr;
        if (offset > 0)
        {
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {tuplePtr, Value::constInt(static_cast<int64_t>(offset))};
            gepInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            elemPtr = Value::temp(gepId);
        }

        // Store the value
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = result.type;
        storeInstr.operands = {elemPtr, result.value};
        storeInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);

        offset += 8;
    }

    return {tuplePtr, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerTupleIndex(TupleIndexExpr *expr)
{
    // Lower the tuple expression
    auto tupleResult = lowerExpr(expr->tuple.get());

    // Get the tuple type to determine element type
    TypeRef tupleType = sema_.typeOf(expr->tuple.get());
    TypeRef elemType = tupleType->tupleElementType(expr->index);
    Type ilType = mapType(elemType);

    // Calculate offset (assuming 8 bytes per element)
    size_t offset = expr->index * 8;

    // Calculate element pointer
    Value elemPtr = tupleResult.value;
    if (offset > 0)
    {
        unsigned gepId = nextTempId();
        il::core::Instr gepInstr;
        gepInstr.result = gepId;
        gepInstr.op = Opcode::GEP;
        gepInstr.type = Type(Type::Kind::Ptr);
        gepInstr.operands = {tupleResult.value, Value::constInt(static_cast<int64_t>(offset))};
        gepInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(gepInstr);
        elemPtr = Value::temp(gepId);
    }

    // Load the element value
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilType;
    loadInstr.operands = {elemPtr};
    loadInstr.loc = curLoc_;
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilType};
}

LowerResult Lowerer::lowerIndex(IndexExpr *expr)
{
    auto base = lowerExpr(expr->base.get());
    auto index = lowerExpr(expr->index.get());

    // Get the base type to determine if it's a FixedArray, List, or Map
    TypeRef baseType = sema_.typeOf(expr->base.get());

    // Fixed-size array: direct GEP + Load (no boxing, no runtime call)
    if (baseType && baseType->kind == TypeKindSem::FixedArray)
    {
        TypeRef elemType = baseType->elementType();
        Type ilElemType = elemType ? mapType(elemType) : Type(Type::Kind::I64);
        size_t elemSize = getILTypeSize(ilElemType);

        // Compute byte offset: index * elemSize
        unsigned mulId = nextTempId();
        il::core::Instr mulInstr;
        mulInstr.result = mulId;
        mulInstr.op = Opcode::Mul;
        mulInstr.type = Type(Type::Kind::I64);
        mulInstr.operands = {index.value, Value::constInt(static_cast<int64_t>(elemSize))};
        mulInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(mulInstr);
        Value byteOffset = Value::temp(mulId);

        // GEP with runtime offset to reach the element
        unsigned gepId = nextTempId();
        il::core::Instr gepInstr;
        gepInstr.result = gepId;
        gepInstr.op = Opcode::GEP;
        gepInstr.type = Type(Type::Kind::Ptr);
        gepInstr.operands = {base.value, byteOffset};
        gepInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(gepInstr);
        Value elemAddr = Value::temp(gepId);

        // Load the element
        Value elemVal = emitLoad(elemAddr, ilElemType);
        return {elemVal, ilElemType};
    }

    Value boxed;
    if (baseType && baseType->kind == TypeKindSem::Map)
    {
        // Map index access - key is a string
        boxed = emitCallRet(Type(Type::Kind::Ptr), kMapGet, {base.value, index.value});
    }
    else
    {
        // List index access (default)
        boxed = emitCallRet(Type(Type::Kind::Ptr), kListGet, {base.value, index.value});
    }

    // Get the expected element type from semantic analysis
    TypeRef elemType = sema_.typeOf(expr);
    Type ilType = mapType(elemType);

    return emitUnboxValue(boxed, ilType, elemType);
}

} // namespace il::frontends::zia

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer_Expr.cpp
/// @brief Expression lowering for the Zia IL lowerer.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"

namespace il::frontends::zia
{

using namespace runtime;

//=============================================================================
// Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerExpr(Expr *expr)
{
    if (!expr)
        return {Value::constInt(0), Type(Type::Kind::I64)};

    switch (expr->kind)
    {
        case ExprKind::IntLiteral:
            return lowerIntLiteral(static_cast<IntLiteralExpr *>(expr));
        case ExprKind::NumberLiteral:
            return lowerNumberLiteral(static_cast<NumberLiteralExpr *>(expr));
        case ExprKind::StringLiteral:
            return lowerStringLiteral(static_cast<StringLiteralExpr *>(expr));
        case ExprKind::BoolLiteral:
            return lowerBoolLiteral(static_cast<BoolLiteralExpr *>(expr));
        case ExprKind::NullLiteral:
            return lowerNullLiteral(static_cast<NullLiteralExpr *>(expr));
        case ExprKind::Ident:
            return lowerIdent(static_cast<IdentExpr *>(expr));
        case ExprKind::SelfExpr:
        {
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                return {selfPtr, Type(Type::Kind::Ptr)};
            }
            return {Value::constInt(0), Type(Type::Kind::Ptr)};
        }
        case ExprKind::SuperExpr:
        {
            // Super returns self pointer but is used for dispatching to parent methods
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                return {selfPtr, Type(Type::Kind::Ptr)};
            }
            return {Value::constInt(0), Type(Type::Kind::Ptr)};
        }
        case ExprKind::Binary:
            return lowerBinary(static_cast<BinaryExpr *>(expr));
        case ExprKind::Unary:
            return lowerUnary(static_cast<UnaryExpr *>(expr));
        case ExprKind::Ternary:
            return lowerTernary(static_cast<TernaryExpr *>(expr));
        case ExprKind::Call:
            return lowerCall(static_cast<CallExpr *>(expr));
        case ExprKind::Field:
            return lowerField(static_cast<FieldExpr *>(expr));
        case ExprKind::New:
            return lowerNew(static_cast<NewExpr *>(expr));
        case ExprKind::Coalesce:
            return lowerCoalesce(static_cast<CoalesceExpr *>(expr));
        case ExprKind::OptionalChain:
            return lowerOptionalChain(static_cast<OptionalChainExpr *>(expr));
        case ExprKind::ListLiteral:
            return lowerListLiteral(static_cast<ListLiteralExpr *>(expr));
        case ExprKind::MapLiteral:
            return lowerMapLiteral(static_cast<MapLiteralExpr *>(expr));
        case ExprKind::Index:
            return lowerIndex(static_cast<IndexExpr *>(expr));
        case ExprKind::Try:
            return lowerTry(static_cast<TryExpr *>(expr));
        case ExprKind::Lambda:
            return lowerLambda(static_cast<LambdaExpr *>(expr));
        case ExprKind::Tuple:
            return lowerTuple(static_cast<TupleExpr *>(expr));
        case ExprKind::TupleIndex:
            return lowerTupleIndex(static_cast<TupleIndexExpr *>(expr));
        case ExprKind::Block:
            return lowerBlockExpr(static_cast<BlockExpr *>(expr));
        case ExprKind::Match:
            return lowerMatchExpr(static_cast<MatchExpr *>(expr));
        default:
            return {Value::constInt(0), Type(Type::Kind::I64)};
    }
}

LowerResult Lowerer::lowerIdent(IdentExpr *expr)
{
    // Check for slot-based mutable variables first (e.g., loop variables)
    auto slotIt = slots_.find(expr->name);
    if (slotIt != slots_.end())
    {
        // Use localTypes_ first (set for parameters in generic method bodies), fall back to sema_.typeOf()
        auto localTypeIt = localTypes_.find(expr->name);
        TypeRef type = (localTypeIt != localTypes_.end()) ? localTypeIt->second : sema_.typeOf(expr);
        Type ilType = mapType(type);
        Value loaded = loadFromSlot(expr->name, ilType);
        return {loaded, ilType};
    }

    Value *local = lookupLocal(expr->name);
    if (local)
    {
        auto localTypeIt = localTypes_.find(expr->name);
        TypeRef type = (localTypeIt != localTypes_.end()) ? localTypeIt->second : sema_.typeOf(expr);
        return {*local, mapType(type)};
    }

    // Check for implicit field access (self.field) inside a value type method
    if (currentValueType_)
    {
        const FieldLayout *field = currentValueType_->findField(expr->name);
        if (field)
        {
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                Value loaded = emitFieldLoad(field, selfPtr);
                return {loaded, mapType(field->type)};
            }
        }
    }

    // Check for implicit field access (self.field) inside an entity method
    if (currentEntityType_)
    {
        const FieldLayout *field = currentEntityType_->findField(expr->name);
        if (field)
        {
            Value selfPtr;
            if (getSelfPtr(selfPtr))
            {
                Value loaded = emitFieldLoad(field, selfPtr);
                return {loaded, mapType(field->type)};
            }
        }
    }

    // Check for global constants (module-level const declarations)
    auto constIt = globalConstants_.find(expr->name);
    if (constIt != globalConstants_.end())
    {
        const Value &val = constIt->second;
        // Determine the type from the value kind
        Type ilType;
        switch (val.kind)
        {
            case Value::Kind::ConstFloat:
                ilType = Type(Type::Kind::F64);
                break;
            case Value::Kind::ConstStr:
            {
                // String constants need to emit a const_str instruction to load the global
                // The stored value's str field contains the global label (e.g., ".L10")
                Value loaded = emitConstStr(val.str);
                return {loaded, Type(Type::Kind::Str)};
            }
            case Value::Kind::GlobalAddr:
                ilType = Type(Type::Kind::Str);
                break;
            case Value::Kind::ConstInt:
                // Check if it's a boolean (i1) or integer (i64)
                ilType = val.isBool ? Type(Type::Kind::I1) : Type(Type::Kind::I64);
                break;
            default:
                ilType = Type(Type::Kind::I64);
                break;
        }
        return {val, ilType};
    }

    // Check for global mutable variables (module-level var declarations)
    auto globalIt = globalVariables_.find(expr->name);
    if (globalIt != globalVariables_.end())
    {
        TypeRef type = globalIt->second;
        Type ilType = mapType(type);
        Value addr = getGlobalVarAddr(expr->name, type);
        Value loaded = emitLoad(addr, ilType);
        return {loaded, ilType};
    }

    // Unknown identifier
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerField(FieldExpr *expr)
{
    // BUG-012 fix: Check if this field expression was resolved as a runtime getter
    // (e.g., Viper.Math.Pi -> Viper.Math.get_Pi)
    std::string getterName = sema_.runtimeFieldGetter(expr);
    if (!getterName.empty())
    {
        // Get the return type of the getter from the expression type
        TypeRef resultType = sema_.typeOf(expr);
        Type ilType = mapType(resultType);
        // Emit a no-argument call to the getter
        Value result = emitCallRet(ilType, getterName, {});
        return {result, ilType};
    }

    // Get the type of the base expression first (before lowering)
    TypeRef baseType = sema_.typeOf(expr->base.get());
    if (!baseType)
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Unwrap Optional types for field access
    // This handles variables assigned from optionals after null checks
    // (e.g., `var col = maybeCol;` where maybeCol is Column?)
    if (baseType->kind == TypeKindSem::Optional && baseType->innerType())
    {
        baseType = baseType->innerType();
    }

    // Handle module-qualified identifier access (e.g., colors.BLACK)
    // The module is just a namespace - we load the symbol directly
    if (baseType->kind == TypeKindSem::Module)
    {
        // Look up the symbol as a global variable or function
        std::string symbolName = expr->field;

        // Check for global constants first (compile-time constants)
        auto constIt = globalConstants_.find(symbolName);
        if (constIt != globalConstants_.end())
        {
            const Value &val = constIt->second;
            Type ilType;
            switch (val.kind)
            {
                case Value::Kind::ConstFloat:
                    ilType = Type(Type::Kind::F64);
                    break;
                case Value::Kind::ConstStr:
                {
                    Value loaded = emitConstStr(val.str);
                    return {loaded, Type(Type::Kind::Str)};
                }
                case Value::Kind::ConstInt:
                    ilType = val.isBool ? Type(Type::Kind::I1) : Type(Type::Kind::I64);
                    break;
                default:
                    ilType = Type(Type::Kind::I64);
                    break;
            }
            return {val, ilType};
        }

        // Check for global mutable variables
        auto globalIt = globalVariables_.find(symbolName);
        if (globalIt != globalVariables_.end())
        {
            TypeRef varType = globalIt->second;
            Type ilType = mapType(varType);
            Value addr = getGlobalVarAddr(symbolName, varType);
            Value loaded = emitLoad(addr, ilType);
            return {loaded, ilType};
        }

        // For function references, return a placeholder (call handling is separate)
        return {Value::constInt(0), Type(Type::Kind::Ptr)};
    }

    // Lower the base expression
    auto base = lowerExpr(expr->base.get());

    // Check if base is a value type
    std::string typeName = baseType->name;
    const ValueTypeInfo *info = getOrCreateValueTypeInfo(typeName);
    if (info)
    {
        const FieldLayout *field = info->findField(expr->field);

        if (field)
        {
            // GEP to get field address
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {base.value, Value::constInt(static_cast<int64_t>(field->offset))};
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            Value fieldAddr = Value::temp(gepId);

            // Load the field value
            Type fieldType = mapType(field->type);
            unsigned loadId = nextTempId();
            il::core::Instr loadInstr;
            loadInstr.result = loadId;
            loadInstr.op = Opcode::Load;
            loadInstr.type = fieldType;
            loadInstr.operands = {fieldAddr};
            blockMgr_.currentBlock()->instructions.push_back(loadInstr);

            return {Value::temp(loadId), fieldType};
        }
    }

    // Check if base is an entity type
    const EntityTypeInfo *entityInfoPtr = getOrCreateEntityTypeInfo(typeName);
    if (entityInfoPtr)
    {
        const EntityTypeInfo &info = *entityInfoPtr;
        const FieldLayout *field = info.findField(expr->field);

        if (field)
        {
            // GEP to get field address
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {base.value, Value::constInt(static_cast<int64_t>(field->offset))};
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            Value fieldAddr = Value::temp(gepId);

            // Load the field value
            Type fieldType = mapType(field->type);
            unsigned loadId = nextTempId();
            il::core::Instr loadInstr;
            loadInstr.result = loadId;
            loadInstr.op = Opcode::Load;
            loadInstr.type = fieldType;
            loadInstr.operands = {fieldAddr};
            blockMgr_.currentBlock()->instructions.push_back(loadInstr);

            return {Value::temp(loadId), fieldType};
        }
    }

    // Handle String.Length and String.length property (Bug #3 fix)
    if (baseType->kind == TypeKindSem::String)
    {
        if (expr->field == "Length" || expr->field == "length")
        {
            // Synthesize a call to Viper.String.Length(str)
            // Note: Using "Viper.String.Length" to match Sema.cpp registration
            Value result = emitCallRet(Type(Type::Kind::I64), "Viper.String.Length", {base.value});
            return {result, Type(Type::Kind::I64)};
        }
    }

    // Handle List.count, List.size, and List.length property
    if (baseType->kind == TypeKindSem::List)
    {
        if (expr->field == "Count" || expr->field == "count" || expr->field == "size" ||
            expr->field == "length")
        {
            // Synthesize a call to Viper.Collections.List.get_Count(list)
            Value result = emitCallRet(Type(Type::Kind::I64), kListCount, {base.value});
            return {result, Type(Type::Kind::I64)};
        }
    }

    // Handle runtime class property access (e.g., app.ShouldClose, editor.LineCount)
    // Runtime classes are Ptr types with a non-empty name like "Viper.GUI.App"
    if (baseType->kind == TypeKindSem::Ptr && !baseType->name.empty())
    {
        // Construct getter function name: {ClassName}.get_{PropertyName}
        std::string getterName = baseType->name + ".get_" + expr->field;

        // Look up the getter function
        Symbol *getterSym = sema_.findExternFunction(getterName);
        if (getterSym && getterSym->type)
        {
            // Determine the return type
            Type retType = mapType(getterSym->type);

            // Emit call to the getter function
            Value result = emitCallRet(retType, getterName, {base.value});
            return {result, retType};
        }
    }

    // Unknown field access
    return {Value::constInt(0), Type(Type::Kind::I64)};
}

LowerResult Lowerer::lowerNew(NewExpr *expr)
{
    // Get the type from the new expression
    TypeRef type = sema_.resolveType(expr->type.get());
    if (!type)
    {
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    // Handle built-in collection types
    if (type->kind == TypeKindSem::List)
    {
        // Create a new list via runtime
        Value list = emitCallRet(Type(Type::Kind::Ptr), kListNew, {});
        return {list, Type(Type::Kind::Ptr)};
    }
    if (type->kind == TypeKindSem::Set)
    {
        Value set = emitCallRet(Type(Type::Kind::Ptr), kSetNew, {});
        return {set, Type(Type::Kind::Ptr)};
    }
    if (type->kind == TypeKindSem::Map)
    {
        Value map = emitCallRet(Type(Type::Kind::Ptr), kMapNew, {});
        return {map, Type(Type::Kind::Ptr)};
    }

    // Handle runtime class types (Ptr types with names like "Viper.Graphics.Canvas")
    if (type->kind == TypeKindSem::Ptr && !type->name.empty())
    {
        std::string ctorName = type->name + ".New";

        // Lower arguments
        std::vector<Value> argValues;
        for (auto &arg : expr->args)
        {
            auto result = lowerExpr(arg.value.get());
            argValues.push_back(result.value);
        }

        // Call the runtime constructor
        Value result = emitCallRet(Type(Type::Kind::Ptr), ctorName, argValues);
        return {result, Type(Type::Kind::Ptr)};
    }

    // BUG-010 fix: Check for value type construction via 'new' keyword
    // Value types can be instantiated with 'new' just like entity types
    std::string typeName = type->name;
    const ValueTypeInfo *valueInfo = getOrCreateValueTypeInfo(typeName);
    if (valueInfo)
    {
        const ValueTypeInfo &info = *valueInfo;

        // Lower arguments
        std::vector<Value> argValues;
        for (auto &arg : expr->args)
        {
            auto result = lowerExpr(arg.value.get());
            argValues.push_back(result.value);
        }

        // Allocate stack space for the value
        unsigned allocaId = nextTempId();
        il::core::Instr allocaInstr;
        allocaInstr.result = allocaId;
        allocaInstr.op = Opcode::Alloca;
        allocaInstr.type = Type(Type::Kind::Ptr);
        allocaInstr.operands = {Value::constInt(static_cast<int64_t>(info.totalSize))};
        blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
        Value ptr = Value::temp(allocaId);

        // Check if the value type has an explicit init method
        auto initIt = info.methodMap.find("init");
        if (initIt != info.methodMap.end())
        {
            // Call the explicit init method
            std::string initName = typeName + ".init";
            std::vector<Value> initArgs;
            initArgs.push_back(ptr); // self is first argument
            for (const auto &argVal : argValues)
            {
                initArgs.push_back(argVal);
            }
            emitCall(initName, initArgs);
        }
        else
        {
            // No init method - store arguments directly into fields
            for (size_t i = 0; i < argValues.size() && i < info.fields.size(); ++i)
            {
                const FieldLayout &field = info.fields[i];

                // GEP to get field address
                unsigned gepId = nextTempId();
                il::core::Instr gepInstr;
                gepInstr.result = gepId;
                gepInstr.op = Opcode::GEP;
                gepInstr.type = Type(Type::Kind::Ptr);
                gepInstr.operands = {ptr, Value::constInt(static_cast<int64_t>(field.offset))};
                blockMgr_.currentBlock()->instructions.push_back(gepInstr);
                Value fieldAddr = Value::temp(gepId);

                // Store the value
                il::core::Instr storeInstr;
                storeInstr.op = Opcode::Store;
                storeInstr.type = mapType(field.type);
                storeInstr.operands = {fieldAddr, argValues[i]};
                blockMgr_.currentBlock()->instructions.push_back(storeInstr);
            }
        }

        return LowerResult{ptr, Type(Type::Kind::Ptr)};
    }

    // Find the entity type info
    const EntityTypeInfo *infoPtr = getOrCreateEntityTypeInfo(typeName);
    if (!infoPtr)
    {
        // Not an entity type
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    const EntityTypeInfo &info = *infoPtr;

    // Lower arguments
    std::vector<Value> argValues;
    for (auto &arg : expr->args)
    {
        auto result = lowerExpr(arg.value.get());
        argValues.push_back(result.value);
    }

    // Allocate heap memory for the entity using rt_obj_new_i64
    // This properly initializes the heap header with magic, refcount, etc.
    // so that entities can be added to lists and other reference-counted collections
    Value ptr = emitCallRet(Type(Type::Kind::Ptr),
                            "rt_obj_new_i64",
                            {Value::constInt(static_cast<int64_t>(info.classId)),
                             Value::constInt(static_cast<int64_t>(info.totalSize))});

    // Check if the entity has an explicit init method
    auto initIt = info.methodMap.find("init");
    if (initIt != info.methodMap.end())
    {
        // BUG-VL-008 fix: Call the explicit init method
        // This ensures fields are assigned in the order specified by init()
        std::string initName = typeName + ".init";
        std::vector<Value> initArgs;
        initArgs.push_back(ptr); // self is first argument
        for (const auto &argVal : argValues)
        {
            initArgs.push_back(argVal);
        }
        emitCall(initName, initArgs);
    }
    else
    {
        // No explicit init - do inline field initialization
        // Constructor args map directly to fields in declaration order
        for (size_t i = 0; i < info.fields.size(); ++i)
        {
            const auto &field = info.fields[i];
            Type ilFieldType = mapType(field.type);
            Value fieldValue;

            if (i < argValues.size())
            {
                // Use constructor argument
                fieldValue = argValues[i];
            }
            else
            {
                // Use default value
                switch (ilFieldType.kind)
                {
                    case Type::Kind::I1:
                        fieldValue = Value::constBool(false);
                        break;
                    case Type::Kind::I64:
                    case Type::Kind::I16:
                    case Type::Kind::I32:
                        fieldValue = Value::constInt(0);
                        break;
                    case Type::Kind::F64:
                        fieldValue = Value::constFloat(0.0);
                        break;
                    case Type::Kind::Str:
                        fieldValue = emitConstStr("");
                        break;
                    case Type::Kind::Ptr:
                        fieldValue = Value::null();
                        break;
                    default:
                        fieldValue = Value::constInt(0);
                        break;
                }
            }

            Value fieldAddr = emitGEP(ptr, static_cast<int64_t>(field.offset));
            emitStore(fieldAddr, fieldValue, ilFieldType);
        }
    }

    // Return pointer to the allocated entity
    return {ptr, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerCoalesce(CoalesceExpr *expr)
{
    // Get the type to determine how to handle the coalesce
    TypeRef leftType = sema_.typeOf(expr->left.get());
    TypeRef resultType = sema_.typeOf(expr);
    Type ilResultType = mapType(resultType);
    bool expectsOptional = resultType && resultType->kind == TypeKindSem::Optional;
    TypeRef optionalInner = expectsOptional ? resultType->innerType() : nullptr;
    TypeRef innerType = resultType;

    // For reference types (entities, etc.), check if the pointer is null
    // For value-type optionals, we would need to check the flag field
    // Currently implementing reference-type coalesce

    // Allocate a stack slot for the result BEFORE branching
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)}; // 8 bytes for ptr/i64
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value resultSlot = Value::temp(allocaId);

    // Lower the left expression
    auto left = lowerExpr(expr->left.get());

    // Create blocks for the coalesce
    size_t hasValueIdx = createBlock("coalesce_has");
    size_t isNullIdx = createBlock("coalesce_null");
    size_t mergeIdx = createBlock("coalesce_merge");

    // Check if it's null (for reference types, compare pointer to 0)
    // Note: ICmpNe requires i64 operands, so we convert the pointer via alloca/store/load
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, left.value};
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNotNull =
        emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
    emitCBr(isNotNull, hasValueIdx, isNullIdx);

    // Has value block - store left value and branch to merge
    setBlock(hasValueIdx);
    {
        Value unwrapped = left.value;
        if (innerType)
        {
            auto innerVal = emitOptionalUnwrap(left.value, innerType);
            unwrapped = innerVal.value;
        }
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = ilResultType;
        storeInstr.operands = {resultSlot, unwrapped};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }
    emitBr(mergeIdx);

    // Is null block - evaluate right, store, and branch to merge
    setBlock(isNullIdx);
    auto right = lowerExpr(expr->right.get());
    {
        il::core::Instr storeInstr;
        storeInstr.op = Opcode::Store;
        storeInstr.type = ilResultType;
        storeInstr.operands = {resultSlot, right.value};
        blockMgr_.currentBlock()->instructions.push_back(storeInstr);
    }
    emitBr(mergeIdx);

    // Merge block - load the result
    setBlock(mergeIdx);
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilResultType;
    loadInstr.operands = {resultSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilResultType};
}

LowerResult Lowerer::lowerTernary(TernaryExpr *expr)
{
    auto cond = lowerExpr(expr->condition.get());
    TypeRef resultType = sema_.typeOf(expr);
    Type ilResultType = mapType(resultType);
    bool expectsOptional = resultType && resultType->kind == TypeKindSem::Optional;
    TypeRef optionalInner = expectsOptional ? resultType->innerType() : nullptr;

    // Allocate a stack slot for the result before branching.
    unsigned allocaId = nextTempId();
    il::core::Instr allocaInstr;
    allocaInstr.result = allocaId;
    allocaInstr.op = Opcode::Alloca;
    allocaInstr.type = Type(Type::Kind::Ptr);
    allocaInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
    Value resultSlot = Value::temp(allocaId);

    size_t thenIdx = createBlock("ternary_then");
    size_t elseIdx = createBlock("ternary_else");
    size_t mergeIdx = createBlock("ternary_merge");

    emitCBr(cond.value, thenIdx, elseIdx);

    setBlock(thenIdx);
    {
        auto thenResult = lowerExpr(expr->thenExpr.get());
        Value thenValue = thenResult.value;
        if (expectsOptional)
        {
            TypeRef thenType = sema_.typeOf(expr->thenExpr.get());
            if (!thenType || thenType->kind != TypeKindSem::Optional)
            {
                if (optionalInner)
                    thenValue = emitOptionalWrap(thenResult.value, optionalInner);
            }
        }
        if (ilResultType.kind != Type::Kind::Void)
        {
            il::core::Instr storeInstr;
            storeInstr.op = Opcode::Store;
            storeInstr.type = ilResultType;
            storeInstr.operands = {resultSlot, thenValue};
            blockMgr_.currentBlock()->instructions.push_back(storeInstr);
        }
    }
    emitBr(mergeIdx);

    setBlock(elseIdx);
    {
        auto elseResult = lowerExpr(expr->elseExpr.get());
        Value elseValue = elseResult.value;
        if (expectsOptional)
        {
            TypeRef elseType = sema_.typeOf(expr->elseExpr.get());
            if (!elseType || elseType->kind != TypeKindSem::Optional)
            {
                if (optionalInner)
                    elseValue = emitOptionalWrap(elseResult.value, optionalInner);
            }
        }
        if (ilResultType.kind != Type::Kind::Void)
        {
            il::core::Instr storeInstr;
            storeInstr.op = Opcode::Store;
            storeInstr.type = ilResultType;
            storeInstr.operands = {resultSlot, elseValue};
            blockMgr_.currentBlock()->instructions.push_back(storeInstr);
        }
    }
    emitBr(mergeIdx);

    setBlock(mergeIdx);
    if (ilResultType.kind == Type::Kind::Void)
        return {Value::constInt(0), Type(Type::Kind::Void)};

    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = ilResultType;
    loadInstr.operands = {resultSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), ilResultType};
}

LowerResult Lowerer::lowerOptionalChain(OptionalChainExpr *expr)
{
    auto base = lowerExpr(expr->base.get());
    TypeRef baseType = sema_.typeOf(expr->base.get());
    if (!baseType || baseType->kind != TypeKindSem::Optional)
    {
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    TypeRef innerType = baseType->innerType();
    TypeRef fieldType = types::unknown();

    // Allocate a stack slot for the result (optional pointer)
    unsigned resultSlotId = nextTempId();
    il::core::Instr resultAlloca;
    resultAlloca.result = resultSlotId;
    resultAlloca.op = Opcode::Alloca;
    resultAlloca.type = Type(Type::Kind::Ptr);
    resultAlloca.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(resultAlloca);
    Value resultSlot = Value::temp(resultSlotId);

    // Compare optional pointer with null
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, base.value};
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNull = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));

    size_t hasValueIdx = createBlock("optchain_has");
    size_t isNullIdx = createBlock("optchain_null");
    size_t mergeIdx = createBlock("optchain_merge");
    emitCBr(isNull, isNullIdx, hasValueIdx);

    // Null block
    setBlock(isNullIdx);
    il::core::Instr storeNull;
    storeNull.op = Opcode::Store;
    storeNull.type = Type(Type::Kind::Ptr);
    storeNull.operands = {resultSlot, Value::null()};
    blockMgr_.currentBlock()->instructions.push_back(storeNull);
    emitBr(mergeIdx);

    // Has value block
    setBlock(hasValueIdx);
    Value fieldValue = Value::null();
    if (innerType)
    {
        if (innerType->kind == TypeKindSem::Value || innerType->kind == TypeKindSem::Entity)
        {
            const std::map<std::string, ValueTypeInfo> &valueTypes = valueTypes_;
            const std::map<std::string, EntityTypeInfo> &entityTypes = entityTypes_;
            if (innerType->kind == TypeKindSem::Value)
            {
                auto it = valueTypes.find(innerType->name);
                if (it != valueTypes.end())
                {
                    const FieldLayout *field = it->second.findField(expr->field);
                    if (field)
                    {
                        fieldType = field->type;
                        fieldValue = emitFieldLoad(field, base.value);
                    }
                }
            }
            else
            {
                auto it = entityTypes.find(innerType->name);
                if (it != entityTypes.end())
                {
                    const FieldLayout *field = it->second.findField(expr->field);
                    if (field)
                    {
                        fieldType = field->type;
                        fieldValue = emitFieldLoad(field, base.value);
                    }
                }
            }
        }
        else if (innerType->kind == TypeKindSem::List)
        {
            if (expr->field == "count" || expr->field == "size" || expr->field == "length")
            {
                fieldType = types::integer();
                fieldValue = emitCallRet(Type(Type::Kind::I64), kListCount, {base.value});
            }
        }
    }

    Value optionalValue = Value::null();
    if (fieldType && fieldType->kind == TypeKindSem::Optional)
    {
        optionalValue = fieldValue;
    }
    else if (fieldType && fieldType->kind != TypeKindSem::Unknown)
    {
        optionalValue = emitOptionalWrap(fieldValue, fieldType);
    }

    il::core::Instr storeVal;
    storeVal.op = Opcode::Store;
    storeVal.type = Type(Type::Kind::Ptr);
    storeVal.operands = {resultSlot, optionalValue};
    blockMgr_.currentBlock()->instructions.push_back(storeVal);
    emitBr(mergeIdx);

    setBlock(mergeIdx);
    unsigned loadId = nextTempId();
    il::core::Instr loadInstr;
    loadInstr.result = loadId;
    loadInstr.op = Opcode::Load;
    loadInstr.type = Type(Type::Kind::Ptr);
    loadInstr.operands = {resultSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadInstr);

    return {Value::temp(loadId), Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerTry(TryExpr *expr)
{
    // The ? operator propagates null/error by returning early from the function
    // For now, we implement this for optional types (null propagation)

    auto operand = lowerExpr(expr->operand.get());

    // Create blocks for the null check
    size_t hasValueIdx = createBlock("try.hasvalue");
    size_t returnNullIdx = createBlock("try.returnnull");

    // Check if the value is null (comparing pointer as i64 to 0)
    // First, store the pointer and load as i64 for comparison
    unsigned ptrSlotId = nextTempId();
    il::core::Instr ptrSlotInstr;
    ptrSlotInstr.result = ptrSlotId;
    ptrSlotInstr.op = Opcode::Alloca;
    ptrSlotInstr.type = Type(Type::Kind::Ptr);
    ptrSlotInstr.operands = {Value::constInt(8)};
    blockMgr_.currentBlock()->instructions.push_back(ptrSlotInstr);
    Value ptrSlot = Value::temp(ptrSlotId);

    il::core::Instr storePtrInstr;
    storePtrInstr.op = Opcode::Store;
    storePtrInstr.type = Type(Type::Kind::Ptr);
    storePtrInstr.operands = {ptrSlot, operand.value};
    blockMgr_.currentBlock()->instructions.push_back(storePtrInstr);

    unsigned ptrAsI64Id = nextTempId();
    il::core::Instr loadAsI64Instr;
    loadAsI64Instr.result = ptrAsI64Id;
    loadAsI64Instr.op = Opcode::Load;
    loadAsI64Instr.type = Type(Type::Kind::I64);
    loadAsI64Instr.operands = {ptrSlot};
    blockMgr_.currentBlock()->instructions.push_back(loadAsI64Instr);
    Value ptrAsI64 = Value::temp(ptrAsI64Id);

    Value isNotNull =
        emitBinary(Opcode::ICmpNe, Type(Type::Kind::I1), ptrAsI64, Value::constInt(0));
    emitCBr(isNotNull, hasValueIdx, returnNullIdx);

    // Return null block - return null from the current function
    setBlock(returnNullIdx);
    // For functions returning optional types, return null (0 as pointer)
    // For void functions, we just return void
    if (currentFunc_->retType.kind == Type::Kind::Void)
    {
        emitRetVoid();
    }
    else
    {
        // Return null for optional/pointer return types
        emitRet(Value::constInt(0));
    }

    // Has value block - continue with the unwrapped value
    setBlock(hasValueIdx);

    // Return the operand value (unwrap optionals when needed)
    TypeRef operandType = sema_.typeOf(expr->operand.get());
    if (operandType && operandType->kind == TypeKindSem::Optional)
    {
        TypeRef innerType = operandType->innerType();
        if (innerType)
            return emitOptionalUnwrap(operand.value, innerType);
    }
    return operand;
}

LowerResult Lowerer::lowerLambda(LambdaExpr *expr)
{
    // Generate unique lambda function name
    static int lambdaCounter = 0;
    std::string lambdaName = "__lambda_" + std::to_string(lambdaCounter++);

    // Check if lambda has captured variables
    bool hasCaptures = !expr->captures.empty();

    // Determine return type (inferred as the body's type if not specified)
    TypeRef returnType = types::unknown();
    if (expr->returnType)
    {
        returnType = sema_.resolveType(expr->returnType.get());
    }
    else
    {
        returnType = sema_.typeOf(expr->body.get());
    }
    Type ilReturnType = mapType(returnType);

    // Build parameter list - always add env pointer as first param for uniform closure ABI
    std::vector<il::core::Param> params;
    params.reserve(expr->params.size() + 1);
    params.push_back({"__env", Type(Type::Kind::Ptr)});
    for (const auto &param : expr->params)
    {
        TypeRef paramType = param.type ? sema_.resolveType(param.type.get()) : types::unknown();
        params.push_back({param.name, mapType(paramType)});
    }

    // Collect info about captured variables before switching contexts
    // We need to capture their current values/slot pointers
    struct CaptureInfo
    {
        std::string name;
        Value value;
        Type type;
        TypeRef semType;
        bool isSlot;
    };

    std::vector<CaptureInfo> captureInfos;
    if (hasCaptures)
    {
        for (const auto &cap : expr->captures)
        {
            CaptureInfo info;
            info.name = cap.name;
            info.isSlot = false;

            // Look up the variable in current scope
            auto slotIt = slots_.find(cap.name);
            if (slotIt != slots_.end())
            {
                // Load from slot to capture by value
                TypeRef varType = sema_.lookupVarType(cap.name);
                info.type = varType ? mapType(varType) : Type(Type::Kind::I64);
                info.semType = varType;
                info.value = loadFromSlot(cap.name, info.type);
                info.isSlot = true;
            }
            else
            {
                auto localIt = locals_.find(cap.name);
                if (localIt != locals_.end())
                {
                    info.value = localIt->second;
                    TypeRef varType = sema_.lookupVarType(cap.name);
                    info.type = varType ? mapType(varType) : Type(Type::Kind::I64);
                    info.semType = varType;
                }
                else
                {
                    // Not found - might be a global or error
                    info.value = Value::constInt(0);
                    info.type = Type(Type::Kind::I64);
                    info.semType = types::unknown();
                }
            }
            captureInfos.push_back(info);
        }
    }

    // Save current function context (use index instead of pointer to handle vector reallocation)
    TypeRef savedReturnType = currentReturnType_;
    size_t savedFuncIdx = static_cast<size_t>(-1);
    if (currentFunc_)
    {
        for (size_t i = 0; i < module_->functions.size(); ++i)
        {
            if (&module_->functions[i] == currentFunc_)
            {
                savedFuncIdx = i;
                break;
            }
        }
    }
    size_t savedBlockIdx = blockMgr_.currentBlockIndex();
    unsigned savedNextBlockId = blockMgr_.nextBlockId();
    auto savedLocals = std::move(locals_);
    auto savedSlots = std::move(slots_);
    auto savedLocalTypes = std::move(localTypes_);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();

    // Create the lambda function and entry block via IRBuilder so param IDs are assigned.
    currentFunc_ = &builder_->startFunction(lambdaName, ilReturnType, params);
    currentReturnType_ = returnType;
    definedFunctions_.insert(lambdaName);

    blockMgr_.bind(builder_.get(), currentFunc_);

    // Create entry block with the lambda's params as block params.
    builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
    const size_t entryIdx = currentFunc_->blocks.size() - 1;
    setBlock(entryIdx);

    // Load captured variables from the environment struct if we have captures
    const auto &blockParams = currentFunc_->blocks[entryIdx].params;
    // First parameter is always __env (may be null for no-capture lambdas)
    if (hasCaptures)
    {
        Value envPtr = Value::temp(blockParams[0].id);

        // Load each captured variable from the environment
        size_t offset = 0;
        for (size_t i = 0; i < captureInfos.size(); ++i)
        {
            const auto &info = captureInfos[i];

            // GEP to get field address within env struct
            Value fieldAddr = emitGEP(envPtr, static_cast<int64_t>(offset));

            // Load the captured value
            Value capturedVal = emitLoad(fieldAddr, info.type);

            // Create a slot for mutable captured variables
            createSlot(info.name, info.type);
            storeToSlot(info.name, capturedVal, info.type);
            localTypes_[info.name] = info.semType ? info.semType : types::unknown();

            // Advance offset by the size of this type
            offset += getILTypeSize(info.type);
        }
    }

    // Define user parameters as locals (skip __env at index 0)
    for (size_t i = 0; i < expr->params.size(); ++i)
    {
        size_t paramIdx = i + 1; // Skip __env
        if (paramIdx < blockParams.size())
        {
            TypeRef paramType = expr->params[i].type ? sema_.resolveType(expr->params[i].type.get())
                                                     : types::unknown();
            Type ilParamType = mapType(paramType);
            createSlot(expr->params[i].name, ilParamType);
            storeToSlot(expr->params[i].name, Value::temp(blockParams[paramIdx].id), ilParamType);
            localTypes_[expr->params[i].name] = paramType;
        }
    }

    // Lower the body - handle both block expressions and simple expressions
    LowerResult bodyResult{Value::constInt(0), Type(Type::Kind::Void)};
    if (auto *blockExpr = dynamic_cast<BlockExpr *>(expr->body.get()))
    {
        // Lower each statement in the block
        for (auto &stmt : blockExpr->statements)
        {
            lowerStmt(stmt.get());
        }
        // The block may have a final value expression
        if (blockExpr->value)
        {
            bodyResult = lowerExpr(blockExpr->value.get());
        }
    }
    else
    {
        bodyResult = lowerExpr(expr->body.get());
    }

    // Return the body result
    if (ilReturnType.kind == Type::Kind::Void)
    {
        if (!blockMgr_.isTerminated())
        {
            emitRetVoid();
        }
    }
    else
    {
        if (!blockMgr_.isTerminated())
        {
            Value returnValue = bodyResult.value;
            if (returnType && returnType->kind == TypeKindSem::Optional)
            {
                TypeRef bodyType = sema_.typeOf(expr->body.get());
                if (!bodyType || bodyType->kind != TypeKindSem::Optional)
                {
                    TypeRef innerType = returnType->innerType();
                    if (innerType)
                        returnValue = emitOptionalWrap(bodyResult.value, innerType);
                }
            }
            emitRet(returnValue);
        }
    }

    // Restore context (use saved index to get fresh pointer after potential vector reallocation)
    if (savedFuncIdx != static_cast<size_t>(-1))
    {
        currentFunc_ = &module_->functions[savedFuncIdx];
        blockMgr_.reset(currentFunc_);
        blockMgr_.setNextBlockId(savedNextBlockId);
        blockMgr_.setBlock(savedBlockIdx);
    }
    else
    {
        currentFunc_ = nullptr;
    }
    locals_ = std::move(savedLocals);
    slots_ = std::move(savedSlots);
    localTypes_ = std::move(savedLocalTypes);
    currentReturnType_ = savedReturnType;

    // Get the function pointer
    Value funcPtr = Value::global(lambdaName);

    // Always create a uniform closure struct: { funcPtr, envPtr }
    // For no-capture lambdas, envPtr is null

    // Allocate environment if we have captures
    Value envPtr = Value::null(); // null for no captures
    if (hasCaptures)
    {
        size_t envSize = 0;
        for (const auto &info : captureInfos)
        {
            envSize += getILTypeSize(info.type);
        }

        // Allocate environment struct using rt_alloc
        Value envSizeVal = Value::constInt(static_cast<int64_t>(envSize));
        envPtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {envSizeVal});

        // Store captured values into the environment
        size_t offset = 0;
        for (const auto &info : captureInfos)
        {
            Value fieldAddr = emitGEP(envPtr, static_cast<int64_t>(offset));
            emitStore(fieldAddr, info.value, info.type);
            offset += getILTypeSize(info.type);
        }
    }

    // Allocate closure struct: { ptr funcPtr, ptr envPtr } = 16 bytes
    Value closureSizeVal = Value::constInt(16);
    Value closurePtr =
        emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {closureSizeVal});

    // Store function pointer at offset 0
    emitStore(closurePtr, funcPtr, Type(Type::Kind::Ptr));

    // Store environment pointer at offset 8 (null for no captures)
    Value envFieldAddr = emitGEP(closurePtr, 8);
    emitStore(envFieldAddr, envPtr, Type(Type::Kind::Ptr));

    return {closurePtr, Type(Type::Kind::Ptr)};
}

LowerResult Lowerer::lowerBlockExpr(BlockExpr *expr)
{
    // Lower each statement in the block
    for (auto &stmt : expr->statements)
    {
        lowerStmt(stmt.get());
    }

    // If there's a trailing value expression, lower it and return
    if (expr->value)
    {
        return lowerExpr(expr->value.get());
    }

    // No value expression - return void/unit
    return {Value::constInt(0), Type(Type::Kind::Void)};
}

} // namespace il::frontends::zia

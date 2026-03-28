//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/zia/Lowerer_Expr_Complex.cpp
// Purpose: Field access and new-expression lowering for the Zia IL lowerer.
// Key invariants:
//   - Field access checks value types, entity types, and built-in properties
//   - New expressions handle collections, runtime classes, value types, entities
// Ownership/Lifetime:
//   - Lowerer owns IL builder; field lookups use entityTypes_/valueTypes_ maps
// Links: src/frontends/zia/Lowerer.hpp,
//        src/frontends/zia/Lowerer_Expr_Optional.cpp,
//        src/frontends/zia/Lowerer_Expr_Lambda.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

namespace il::frontends::zia {

using namespace runtime;

//=============================================================================
// Field Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerField(FieldExpr *expr) {
    auto dottedName = [](Expr *node, std::string &out, auto &self) -> bool {
        if (auto *ident = dynamic_cast<IdentExpr *>(node)) {
            out = ident->name;
            return true;
        }
        if (auto *field = dynamic_cast<FieldExpr *>(node)) {
            std::string base;
            if (!self(field->base.get(), base, self))
                return false;
            out = base + "." + field->field;
            return true;
        }
        return false;
    };

    // Property access lowers to a synthesized getter call for both runtime and
    // user-defined properties.
    std::string getterName = sema_.resolvedFieldGetter(expr);
    if (!getterName.empty()) {
        TypeRef resultType = sema_.typeOf(expr);
        Type ilType = mapType(resultType);
        TypeRef baseType = sema_.typeOf(expr->base.get());
        std::vector<Value> args;
        if (!baseType || baseType->kind != TypeKindSem::Module) {
            auto base = lowerExpr(expr->base.get());
            args.push_back(base.value);
        }
        Value result = emitCallRet(ilType, getterName, args);
        return {result, ilType};
    }

    // Handle dotted enum variant access even when the base expression itself was
    // not cached as an Enum type during semantic analysis (e.g. Color.Red).
    std::string dottedBase;
    if (dottedName(expr->base.get(), dottedBase, dottedName)) {
        std::string key = dottedBase + "." + expr->field;
        auto it = enumVariantValues_.find(key);
        if (it != enumVariantValues_.end())
            return {Value::constInt(it->second), Type(Type::Kind::I64)};
    }

    // Get the type of the base expression first (before lowering)
    TypeRef baseType = sema_.typeOf(expr->base.get());
    if (!baseType) {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Unwrap Optional types for field access
    // This handles variables assigned from optionals after null checks
    // (e.g., `var col = maybeCol;` where maybeCol is Column?)
    if (baseType->kind == TypeKindSem::Optional && baseType->innerType()) {
        baseType = baseType->innerType();
    }

    // Handle enum variant access (e.g., Color.Red) -- emit I64 constant
    if (baseType->kind == TypeKindSem::Enum) {
        std::string key = baseType->name + "." + expr->field;
        auto it = enumVariantValues_.find(key);
        if (it != enumVariantValues_.end()) {
            return {Value::constInt(it->second), Type(Type::Kind::I64)};
        }
        // Fallthrough should not happen (Sema catches unknown variants)
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Handle module-qualified identifier access (e.g., colors.BLACK)
    // The module is just a namespace - we load the symbol directly
    if (baseType->kind == TypeKindSem::Module) {
        // Look up the symbol as a global variable or function
        std::string symbolName = expr->field;

        // Check for global constants first (compile-time constants)
        auto constIt = globalConstants_.find(symbolName);
        if (constIt != globalConstants_.end()) {
            const Value &val = constIt->second;
            Type ilType;
            switch (val.kind) {
                case Value::Kind::ConstFloat:
                    ilType = Type(Type::Kind::F64);
                    break;
                case Value::Kind::ConstStr: {
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
        if (globalIt != globalVariables_.end()) {
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
    if (info) {
        const FieldLayout *field = info->findField(expr->field);

        if (field) {
            // GEP to get field address
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {base.value, Value::constInt(static_cast<int64_t>(field->offset))};
            gepInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            Value fieldAddr = Value::temp(gepId);

            // Fixed-size arrays: return the base pointer to inline storage (no load).
            if (field->type && field->type->kind == TypeKindSem::FixedArray)
                return {fieldAddr, Type(Type::Kind::Ptr)};

            // Load the field value
            Type fieldType = mapType(field->type);
            unsigned loadId = nextTempId();
            il::core::Instr loadInstr;
            loadInstr.result = loadId;
            loadInstr.op = Opcode::Load;
            loadInstr.type = fieldType;
            loadInstr.operands = {fieldAddr};
            loadInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(loadInstr);

            // BUG-ADV-001: Retain loaded string fields from value types.
            if (fieldType.kind == Type::Kind::Str)
                emitCall(runtime::kStrRetainMaybe, {Value::temp(loadId)});

            return {Value::temp(loadId), fieldType};
        }
    }

    // Check if base is an entity type
    const EntityTypeInfo *entityInfoPtr = getOrCreateEntityTypeInfo(typeName);
    if (entityInfoPtr) {
        const EntityTypeInfo &entityInfo = *entityInfoPtr;
        const FieldLayout *field = entityInfo.findField(expr->field);

        if (field) {
            // GEP to get field address
            unsigned gepId = nextTempId();
            il::core::Instr gepInstr;
            gepInstr.result = gepId;
            gepInstr.op = Opcode::GEP;
            gepInstr.type = Type(Type::Kind::Ptr);
            gepInstr.operands = {base.value, Value::constInt(static_cast<int64_t>(field->offset))};
            gepInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(gepInstr);
            Value fieldAddr = Value::temp(gepId);

            // Fixed-size arrays: return the base pointer to inline storage (no load).
            if (field->type && field->type->kind == TypeKindSem::FixedArray)
                return {fieldAddr, Type(Type::Kind::Ptr)};

            // Load the field value
            Type fieldType = mapType(field->type);
            unsigned loadId = nextTempId();
            il::core::Instr loadInstr;
            loadInstr.result = loadId;
            loadInstr.op = Opcode::Load;
            loadInstr.type = fieldType;
            loadInstr.operands = {fieldAddr};
            loadInstr.loc = curLoc_;
            blockMgr_.currentBlock()->instructions.push_back(loadInstr);

            // BUG-ADV-001: Retain loaded string fields from entity types.
            // Load gives a borrowed reference; retain converts it to owned,
            // preventing use-after-free when the string is consumed by
            // concatenation or passed cross-module.
            if (fieldType.kind == Type::Kind::Str)
                emitCall(runtime::kStrRetainMaybe, {Value::temp(loadId)});

            return {Value::temp(loadId), fieldType};
        }
    }

    // Handle String.Length and String.length property (Bug #3 fix)
    if (baseType->kind == TypeKindSem::String) {
        if (expr->field == "Length" || expr->field == "length") {
            // Synthesize a call to Viper.String.Length(str)
            Value result = emitCallRet(Type(Type::Kind::I64), kStringLength, {base.value});
            return {result, Type(Type::Kind::I64)};
        }
    }

    // Handle List.count, List.size, List.length, and List.Len property
    if (baseType->kind == TypeKindSem::List) {
        if (expr->field == "Count" || expr->field == "count" || expr->field == "size" ||
            expr->field == "length" || expr->field == "Len" || expr->field == "Length") {
            // Synthesize a call to Viper.Collections.List.get_Count(list)
            Value result = emitCallRet(Type(Type::Kind::I64), kListCount, {base.value});
            return {result, Type(Type::Kind::I64)};
        }
    }

    // Handle Map.Length, Map.Len, Map.Count, etc. property
    if (baseType->kind == TypeKindSem::Map) {
        if (expr->field == "Length" || expr->field == "length" || expr->field == "Len" ||
            expr->field == "Count" || expr->field == "count" || expr->field == "size") {
            Value result = emitCallRet(Type(Type::Kind::I64), kMapCount, {base.value});
            return {result, Type(Type::Kind::I64)};
        }
    }

    // Handle Set.Length, Set.Len, Set.Count, etc. property
    if (baseType->kind == TypeKindSem::Set) {
        if (expr->field == "Length" || expr->field == "length" || expr->field == "Len" ||
            expr->field == "Count" || expr->field == "count" || expr->field == "size") {
            Value result = emitCallRet(Type(Type::Kind::I64), kSetCount, {base.value});
            return {result, Type(Type::Kind::I64)};
        }
    }

    // Handle runtime class property access (e.g., app.ShouldClose, editor.LineCount)
    // Runtime classes are Ptr types with a non-empty name like "Viper.GUI.App"
    if (baseType->kind == TypeKindSem::Ptr && !baseType->name.empty()) {
        // Construct getter function name: {ClassName}.get_{PropertyName}
        std::string rtGetterName = baseType->name + ".get_" + expr->field;

        // Look up the getter function
        Symbol *getterSym = sema_.findExternFunction(rtGetterName);
        if (getterSym && getterSym->type) {
            // Determine the return type - extract from function type if needed
            TypeRef symType = getterSym->type;
            if (symType->kind == TypeKindSem::Function && symType->returnType()) {
                symType = symType->returnType();
            }
            Type retType = mapType(symType);

            // Emit call to the getter function
            Value result = emitCallRet(retType, rtGetterName, {base.value});
            return {result, retType};
        }
    }

    // Unknown field access -- use sema type to determine correct IL type as fallback.
    // This prevents silent I64 mistyping for non-integer fields (BUG-FE-006 safety net).
    TypeRef exprType = sema_.typeOf(expr);
    Type fallbackType = exprType ? mapType(exprType) : Type(Type::Kind::I64);
    return {Value::constInt(0), fallbackType};
}

//=============================================================================
// New Expression Lowering
//=============================================================================

LowerResult Lowerer::lowerNew(NewExpr *expr) {
    // Get the type from the new expression
    TypeRef type = sema_.resolveType(expr->type.get());
    if (!type) {
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    // Handle built-in collection types
    if (type->kind == TypeKindSem::List) {
        // Create a new list via runtime
        Value list = emitCallRet(Type(Type::Kind::Ptr), kListNew, {});
        return {list, Type(Type::Kind::Ptr)};
    }
    if (type->kind == TypeKindSem::Set) {
        Value set = emitCallRet(Type(Type::Kind::Ptr), kSetNew, {});
        return {set, Type(Type::Kind::Ptr)};
    }
    if (type->kind == TypeKindSem::Map) {
        Value map = emitCallRet(Type(Type::Kind::Ptr), kMapNew, {});
        return {map, Type(Type::Kind::Ptr)};
    }

    // Handle runtime class types (Ptr) -- look up ctor from RuntimeRegistry catalog.
    // The ctor field is already a fully-qualified extern target, e.g.,
    // "Viper.Collections.FrozenSet.FromSeq"
    // Skip Entity and Value types -- they have their own lowering below.
    if (type && !type->name.empty() && type->kind != TypeKindSem::Entity &&
        type->kind != TypeKindSem::Value) {
        std::string ctorName;
        if (const auto *rtClass = il::runtime::findRuntimeClassByQName(type->name)) {
            if (rtClass->ctor)
                ctorName = rtClass->ctor;
        }
        // Fall back to conventional .New suffix
        if (ctorName.empty())
            ctorName = type->name + ".New";

        // Lower arguments
        std::vector<Value> argValues;
        for (auto &arg : expr->args) {
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
    if (valueInfo) {
        const ValueTypeInfo &valInfo = *valueInfo;

        // Lower arguments
        std::vector<Value> argValues;
        for (auto &arg : expr->args) {
            auto result = lowerExpr(arg.value.get());
            argValues.push_back(result.value);
        }

        // Allocate stack space for the value
        unsigned allocaId = nextTempId();
        il::core::Instr allocaInstr;
        allocaInstr.result = allocaId;
        allocaInstr.op = Opcode::Alloca;
        allocaInstr.type = Type(Type::Kind::Ptr);
        allocaInstr.operands = {Value::constInt(static_cast<int64_t>(valInfo.totalSize))};
        allocaInstr.loc = curLoc_;
        blockMgr_.currentBlock()->instructions.push_back(allocaInstr);
        Value ptr = Value::temp(allocaId);

        // Check if the value type has an explicit init method
        auto initIt = valInfo.methodMap.find("init");
        if (initIt != valInfo.methodMap.end()) {
            // Call the explicit init method
            std::string initName = typeName + ".init";
            std::vector<Value> initArgs;
            initArgs.push_back(ptr); // self is first argument
            for (const auto &argVal : argValues) {
                initArgs.push_back(argVal);
            }
            emitCall(initName, initArgs);
        } else {
            // No init method - store arguments directly into fields
            for (size_t i = 0; i < argValues.size() && i < valInfo.fields.size(); ++i) {
                const FieldLayout &field = valInfo.fields[i];

                // GEP to get field address
                unsigned gepId = nextTempId();
                il::core::Instr gepInstr;
                gepInstr.result = gepId;
                gepInstr.op = Opcode::GEP;
                gepInstr.type = Type(Type::Kind::Ptr);
                gepInstr.operands = {ptr, Value::constInt(static_cast<int64_t>(field.offset))};
                gepInstr.loc = curLoc_;
                blockMgr_.currentBlock()->instructions.push_back(gepInstr);
                Value fieldAddr = Value::temp(gepId);

                // Store the value
                il::core::Instr storeInstr;
                storeInstr.op = Opcode::Store;
                storeInstr.type = mapType(field.type);
                storeInstr.operands = {fieldAddr, argValues[i]};
                storeInstr.loc = curLoc_;
                blockMgr_.currentBlock()->instructions.push_back(storeInstr);
            }
        }

        return LowerResult{ptr, Type(Type::Kind::Ptr)};
    }

    // Find the entity type info
    const EntityTypeInfo *infoPtr = getOrCreateEntityTypeInfo(typeName);
    if (!infoPtr) {
        // Not an entity type
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    const EntityTypeInfo &entityInfo = *infoPtr;

    // Lower arguments
    std::vector<Value> argValues;
    for (auto &arg : expr->args) {
        auto result = lowerExpr(arg.value.get());
        argValues.push_back(result.value);
    }

    // Allocate heap memory for the entity using rt_obj_new_i64
    // This properly initializes the heap header with magic, refcount, etc.
    // so that entities can be added to lists and other reference-counted collections
    Value ptr = emitCallRet(Type(Type::Kind::Ptr),
                            "rt_obj_new_i64",
                            {Value::constInt(static_cast<int64_t>(entityInfo.classId)),
                             Value::constInt(static_cast<int64_t>(entityInfo.totalSize))});

    // Check if the entity has an explicit init method
    MethodDecl *resolvedInit = sema_.resolvedInitDecl(expr);
    auto initIt = entityInfo.methodMap.find("init");
    if (resolvedInit || initIt != entityInfo.methodMap.end()) {
        MethodDecl *initDecl = resolvedInit ? resolvedInit : initIt->second;
        std::string initName = sema_.loweredMethodName(typeName, initDecl);
        if (initName.empty())
            initName = typeName + ".init";
        std::vector<Value> initArgs;
        initArgs.push_back(ptr); // self is first argument
        for (const auto &argVal : argValues) {
            initArgs.push_back(argVal);
        }
        emitCall(initName, initArgs);
    } else {
        // No explicit init - do inline field initialization
        // Constructor args map directly to fields in declaration order
        for (size_t i = 0; i < entityInfo.fields.size(); ++i) {
            const auto &field = entityInfo.fields[i];
            Type ilFieldType = mapType(field.type);
            Value fieldValue;

            if (i < argValues.size()) {
                // Use constructor argument
                fieldValue = argValues[i];
            } else {
                // Use default value
                switch (ilFieldType.kind) {
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

} // namespace il::frontends::zia

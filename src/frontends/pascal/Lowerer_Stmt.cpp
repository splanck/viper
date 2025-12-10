//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer_Stmt.cpp
// Purpose: Statement lowering for Pascal AST to IL.
// Key invariants: Produces valid IL control flow.
// Ownership/Lifetime: Part of Lowerer; operates on borrowed AST.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Lowerer.hpp"
#include "frontends/common/CharUtils.hpp"

namespace il::frontends::pascal
{

using common::char_utils::toLowercase;

inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// Statement Lowering
//===----------------------------------------------------------------------===//

void Lowerer::lowerStmt(const Stmt &stmt)
{
    switch (stmt.kind)
    {
    case StmtKind::Assign:
        lowerAssign(static_cast<const AssignStmt &>(stmt));
        break;
    case StmtKind::Call:
        lowerCallStmt(static_cast<const CallStmt &>(stmt));
        break;
    case StmtKind::Block:
        lowerBlock(static_cast<const BlockStmt &>(stmt));
        break;
    case StmtKind::If:
        lowerIf(static_cast<const IfStmt &>(stmt));
        break;
    case StmtKind::Case:
        lowerCase(static_cast<const CaseStmt &>(stmt));
        break;
    case StmtKind::For:
        lowerFor(static_cast<const ForStmt &>(stmt));
        break;
    case StmtKind::ForIn:
        lowerForIn(static_cast<const ForInStmt &>(stmt));
        break;
    case StmtKind::While:
        lowerWhile(static_cast<const WhileStmt &>(stmt));
        break;
    case StmtKind::Repeat:
        lowerRepeat(static_cast<const RepeatStmt &>(stmt));
        break;
    case StmtKind::Break:
        lowerBreak(static_cast<const BreakStmt &>(stmt));
        break;
    case StmtKind::Continue:
        lowerContinue(static_cast<const ContinueStmt &>(stmt));
        break;
    case StmtKind::Empty:
        // No-op
        break;
    case StmtKind::Raise:
        lowerRaise(static_cast<const RaiseStmt &>(stmt));
        break;
    case StmtKind::Exit:
        lowerExit(static_cast<const ExitStmt &>(stmt));
        break;
    case StmtKind::TryExcept:
        lowerTryExcept(static_cast<const TryExceptStmt &>(stmt));
        break;
    case StmtKind::TryFinally:
        lowerTryFinally(static_cast<const TryFinallyStmt &>(stmt));
        break;
    case StmtKind::Inherited:
        lowerInherited(static_cast<const InheritedStmt &>(stmt));
        break;
    case StmtKind::With:
        lowerWith(static_cast<const WithStmt &>(stmt));
        break;
    default:
        // Other statements not yet implemented
        break;
    }
}

void Lowerer::lowerInherited(const InheritedStmt &stmt)
{
    // Ensure we are inside a method with a current class
    if (currentClassName_.empty())
        return;

    // Lookup base class
    auto *ci = sema_->lookupClass(toLower(currentClassName_));
    if (!ci || ci->baseClass.empty())
        return;
    std::string baseClass = ci->baseClass;

    // Determine method name
    std::string methodName = stmt.methodName;
    if (methodName.empty())
    {
        // Derive from current function name: Class.Method
        std::string fname = currentFunc_ ? currentFunc_->name : std::string();
        auto pos = fname.find('.');
        if (pos != std::string::npos && pos + 1 < fname.size())
            methodName = fname.substr(pos + 1);
        if (methodName.empty())
            return;
    }

    // Build callee name: BaseClass.Method
    std::string funcName = baseClass + "." + methodName;

    // Prepare arguments: Self first, then user-provided args (if any)
    std::vector<Value> args;
    auto itSelf = locals_.find("self");
    if (itSelf == locals_.end())
        return;
    Value selfPtr = emitLoad(Type(Type::Kind::Ptr), itSelf->second);
    args.push_back(selfPtr);

    for (const auto &arg : stmt.args)
    {
        LowerResult lr = lowerExpr(*arg);
        args.push_back(lr.value);
    }

    // Direct call to base implementation (void return expected in statement context)
    emitCall(funcName, args);
}

void Lowerer::lowerAssign(const AssignStmt &stmt)
{
    if (!stmt.target || !stmt.value)
        return;

    // Get target slot
    if (stmt.target->kind == ExprKind::Name)
    {
        auto &nameExpr = static_cast<const NameExpr &>(*stmt.target);
        std::string key = toLower(nameExpr.name);

        // Map "Result" to the current function's return slot
        if (key == "result" && !currentFuncName_.empty())
        {
            key = currentFuncName_;
        }

        auto it = locals_.find(key);
        if (it != locals_.end())
        {
            Value slot = it->second;

            // Get target type
            auto varType = sema_->lookupVariable(key);

            // Check for interface assignment (class to interface)
            if (varType && varType->kind == PasTypeKind::Interface)
            {
                // Get the source type
                PasType srcType = typeOfExpr(*stmt.value);

                if (srcType.kind == PasTypeKind::Class)
                {
                    // Lower the class value (object pointer)
                    LowerResult value = lowerExpr(*stmt.value);
                    Value objPtr = value.value;

                    // Get interface name and class name
                    std::string ifaceName = varType->name;
                    std::string className = srcType.name;

                    // Store object pointer at offset 0
                    emitStore(Type(Type::Kind::Ptr), slot, objPtr);

                    // Look up interface table for this class+interface
                    std::string ifaceKey = toLower(ifaceName);
                    std::string classKey = toLower(className);

                    auto layoutIt = interfaceLayouts_.find(ifaceKey);
                    if (layoutIt != interfaceLayouts_.end())
                    {
                        // Get itable pointer via runtime lookup
                        auto classLayoutIt = classLayouts_.find(classKey);
                        if (classLayoutIt != classLayouts_.end())
                        {
                            usedExterns_.insert("rt_get_interface_impl");
                            Value itablePtr = emitCallRet(Type(Type::Kind::Ptr), "rt_get_interface_impl",
                                                          {Value::constInt(classLayoutIt->second.classId),
                                                           Value::constInt(layoutIt->second.interfaceId)});

                            // Store itable pointer at offset 8
                            Value itablePtrAddr = emitGep(slot, Value::constInt(8));
                            emitStore(Type(Type::Kind::Ptr), itablePtrAddr, itablePtr);
                        }
                    }
                    return;
                }
                else if (srcType.kind == PasTypeKind::Interface)
                {
                    // Interface to interface assignment - copy the fat pointer
                    LowerResult value = lowerExpr(*stmt.value);
                    Value srcSlot = value.value;

                    // Copy object pointer
                    Value objPtr = emitLoad(Type(Type::Kind::Ptr), srcSlot);
                    emitStore(Type(Type::Kind::Ptr), slot, objPtr);

                    // Copy itable pointer
                    Value srcItablePtrAddr = emitGep(srcSlot, Value::constInt(8));
                    Value itablePtr = emitLoad(Type(Type::Kind::Ptr), srcItablePtrAddr);
                    Value dstItablePtrAddr = emitGep(slot, Value::constInt(8));
                    emitStore(Type(Type::Kind::Ptr), dstItablePtrAddr, itablePtr);
                    return;
                }
            }

            // Lower value
            LowerResult value = lowerExpr(*stmt.value);

            Type ilType = varType ? mapType(*varType) : value.type;

            emitStore(ilType, slot, value.value);
            return;
        }

        // Check 'with' contexts for field/property assignment
        for (auto it = withContexts_.rbegin(); it != withContexts_.rend(); ++it)
        {
            const WithContext &ctx = *it;
            if (ctx.type.kind == PasTypeKind::Class)
            {
                auto *classInfo = sema_->lookupClass(toLower(ctx.type.name));
                if (classInfo)
                {
                    // Check for property setter
                    auto propIt = classInfo->properties.find(key);
                    if (propIt != classInfo->properties.end())
                    {
                        Value objPtr = emitLoad(Type(Type::Kind::Ptr), ctx.slot);
                        LowerResult value = lowerExpr(*stmt.value);
                        const auto &p = propIt->second;
                        if (p.setter.kind == PropertyAccessor::Kind::Method)
                        {
                            std::string funcName = classInfo->name + "." + p.setter.name;
                            emitCall(funcName, {objPtr, value.value});
                            return;
                        }
                        if (p.setter.kind == PropertyAccessor::Kind::Field)
                        {
                            PasType classTypeWithFields = ctx.type;
                            for (const auto &[fname, finfo] : classInfo->fields)
                            {
                                classTypeWithFields.fields[fname] = std::make_shared<PasType>(finfo.type);
                            }
                            auto [fieldAddr, fieldType] = getFieldAddress(objPtr, classTypeWithFields, p.setter.name);
                            emitStore(fieldType, fieldAddr, value.value);
                            return;
                        }
                    }
                    // Check for field
                    auto fieldIt = classInfo->fields.find(key);
                    if (fieldIt != classInfo->fields.end())
                    {
                        Value objPtr = emitLoad(Type(Type::Kind::Ptr), ctx.slot);
                        PasType classTypeWithFields = ctx.type;
                        for (const auto &[fname, finfo] : classInfo->fields)
                        {
                            classTypeWithFields.fields[fname] = std::make_shared<PasType>(finfo.type);
                        }
                        auto [fieldAddr, fieldType] = getFieldAddress(objPtr, classTypeWithFields, nameExpr.name);
                        LowerResult value = lowerExpr(*stmt.value);
                        emitStore(fieldType, fieldAddr, value.value);
                        return;
                    }
                }
            }
            else if (ctx.type.kind == PasTypeKind::Record)
            {
                auto fieldIt = ctx.type.fields.find(key);
                if (fieldIt != ctx.type.fields.end() && fieldIt->second)
                {
                    auto [fieldAddr, fieldType] = getFieldAddress(ctx.slot, ctx.type, nameExpr.name);
                    LowerResult value = lowerExpr(*stmt.value);
                    emitStore(fieldType, fieldAddr, value.value);
                    return;
                }
            }
        }

        // Check if this is a class field/property assignment (inside a method)
        if (!currentClassName_.empty())
        {
            auto *classInfo = sema_->lookupClass(toLower(currentClassName_));
            if (classInfo)
            {
                // Property setter first
                auto pit = classInfo->properties.find(key);
                if (pit != classInfo->properties.end())
                {
                    auto selfIt = locals_.find("self");
                    if (selfIt != locals_.end())
                    {
                        Value selfPtr = emitLoad(Type(Type::Kind::Ptr), selfIt->second);
                        LowerResult value = lowerExpr(*stmt.value);
                        const auto &p = pit->second;
                        if (p.setter.kind == PropertyAccessor::Kind::Method)
                        {
                            std::string funcName = currentClassName_ + "." + p.setter.name;
                            emitCall(funcName, {selfPtr, value.value});
                            return;
                        }
                        if (p.setter.kind == PropertyAccessor::Kind::Field)
                        {
                            // Store to Self.<field>
                            PasType selfType = PasType::classType(currentClassName_);
                            for (const auto &[fname, finfo] : classInfo->fields)
                            {
                                selfType.fields[fname] = std::make_shared<PasType>(finfo.type);
                            }
                            auto [fieldAddr, fieldType] = getFieldAddress(selfPtr, selfType, p.setter.name);
                            emitStore(fieldType, fieldAddr, value.value);
                            return;
                        }
                        // No setter: ignore for now
                    }
                }

                auto fieldIt = classInfo->fields.find(key);
                if (fieldIt != classInfo->fields.end())
                {
                    // Assign to Self.fieldName
                    auto selfIt = locals_.find("self");
                    if (selfIt != locals_.end())
                    {
                        Value selfPtr = emitLoad(Type(Type::Kind::Ptr), selfIt->second);

                        // Build a PasType with the class fields for getFieldAddress
                        PasType selfType = PasType::classType(currentClassName_);
                        for (const auto &[fname, finfo] : classInfo->fields)
                        {
                            selfType.fields[fname] = std::make_shared<PasType>(finfo.type);
                        }
                        auto [fieldAddr, fieldType] = getFieldAddress(selfPtr, selfType, nameExpr.name);

                        // Lower value
                        LowerResult value = lowerExpr(*stmt.value);

                        // Store to field
                        emitStore(fieldType, fieldAddr, value.value);
                        return;
                    }
                }
            }
        }
        // Unknown target - just return
        return;
    }
    else if (stmt.target->kind == ExprKind::Field)
    {
        // Field assignment: rec.field := value
        auto &fieldExpr = static_cast<const FieldExpr &>(*stmt.target);
        if (!fieldExpr.base)
            return;

        // Get base type
        PasType baseType = typeOfExpr(*fieldExpr.base);

        if ((baseType.kind == PasTypeKind::Record || baseType.kind == PasTypeKind::Class) &&
            fieldExpr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*fieldExpr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                Value baseAddr = it->second;
                // If class with property, handle setter
                if (baseType.kind == PasTypeKind::Class)
                {
                    // Search for property in class hierarchy
                    std::string propKey = toLower(fieldExpr.field);
                    const PropertyInfo *foundProperty = nullptr;
                    std::string definingClassName;
                    {
                        std::string cur = toLower(baseType.name);
                        while (!cur.empty())
                        {
                            auto *ci = sema_->lookupClass(cur);
                            if (!ci)
                                break;
                            auto pit = ci->properties.find(propKey);
                            if (pit != ci->properties.end())
                            {
                                foundProperty = &pit->second;
                                definingClassName = ci->name;
                                break;
                            }
                            if (ci->baseClass.empty())
                                break;
                            cur = toLower(ci->baseClass);
                        }
                    }
                    if (foundProperty)
                    {
                        LowerResult value = lowerExpr(*stmt.value);
                        const auto &p = *foundProperty;
                        if (p.setter.kind == PropertyAccessor::Kind::Method)
                        {
                            std::string funcName = definingClassName + "." + p.setter.name;
                            // Load object pointer from baseAddr
                            Value objPtr = emitLoad(Type(Type::Kind::Ptr), baseAddr);
                            emitCall(funcName, {objPtr, value.value});
                            return;
                        }
                        if (p.setter.kind == PropertyAccessor::Kind::Field)
                        {
                            auto *defClassInfo = sema_->lookupClass(toLower(definingClassName));
                            PasType classTypeWithFields = PasType::classType(definingClassName);
                            if (defClassInfo)
                            {
                                for (const auto &[fname, finfo] : defClassInfo->fields)
                                {
                                    classTypeWithFields.fields[fname] = std::make_shared<PasType>(finfo.type);
                                }
                            }
                            // Load object pointer for class field store
                            Value objPtr = emitLoad(Type(Type::Kind::Ptr), baseAddr);
                            auto [faddr, ftype] = getFieldAddress(objPtr, classTypeWithFields, p.setter.name);
                            emitStore(ftype, faddr, value.value);
                            return;
                        }
                    }
                }
                // Default: direct field store
                if (baseType.kind == PasTypeKind::Class)
                {
                    auto *classInfo = sema_->lookupClass(toLower(baseType.name));
                    PasType classTypeWithFields = baseType;
                    if (classInfo)
                    {
                        for (const auto &[fname, finfo] : classInfo->fields)
                        {
                            classTypeWithFields.fields[fname] = std::make_shared<PasType>(finfo.type);
                        }
                    }
                    Value objPtr = emitLoad(Type(Type::Kind::Ptr), baseAddr);
                    auto [fieldAddr, fieldType] = getFieldAddress(objPtr, classTypeWithFields, fieldExpr.field);
                    LowerResult value = lowerExpr(*stmt.value);
                    emitStore(fieldType, fieldAddr, value.value);
                }
                else
                {
                    auto [fieldAddr, fieldType] = getFieldAddress(baseAddr, baseType, fieldExpr.field);
                    LowerResult value = lowerExpr(*stmt.value);
                    emitStore(fieldType, fieldAddr, value.value);
                }
            }
            else if (!currentClassName_.empty())
            {
                // Base name is a field on Self (e.g., Inner.Val := ...)
                auto *selfClass = sema_->lookupClass(toLower(currentClassName_));
                if (selfClass)
                {
                    auto selfIt = locals_.find("self");
                    if (selfIt != locals_.end())
                    {
                        // Address of Self.baseField
                        Value selfPtr = emitLoad(Type(Type::Kind::Ptr), selfIt->second);
                        PasType selfType = PasType::classType(currentClassName_);
                        for (const auto &[fname, finfo] : selfClass->fields)
                        {
                            selfType.fields[fname] = std::make_shared<PasType>(finfo.type);
                        }
                        // For class-typed base, load object pointer from Self.baseField
                        auto [baseFieldAddr, baseFieldType] = getFieldAddress(selfPtr, selfType, nameExpr.name);
                        if (baseType.kind == PasTypeKind::Class)
                        {
                            // Now address of nested field on that object
                            auto *innerClass = sema_->lookupClass(toLower(baseType.name));
                            PasType innerType = PasType::classType(baseType.name);
                            if (innerClass)
                            {
                                for (const auto &[fname, finfo] : innerClass->fields)
                                {
                                    innerType.fields[fname] = std::make_shared<PasType>(finfo.type);
                                }
                            }
                            Value objPtr = emitLoad(Type(Type::Kind::Ptr), baseFieldAddr);
                            auto [fieldAddr, fieldType] = getFieldAddress(objPtr, innerType, fieldExpr.field);
                            LowerResult value = lowerExpr(*stmt.value);
                            emitStore(fieldType, fieldAddr, value.value);
                            return;
                        }
                        else
                        {
                            // Base is a record stored inline under Self
                            auto [fieldAddr, fieldType] = getFieldAddress(baseFieldAddr, baseType, fieldExpr.field);
                            LowerResult value = lowerExpr(*stmt.value);
                            emitStore(fieldType, fieldAddr, value.value);
                            return;
                        }
                    }
                }
            }
        }
    }
    else if (stmt.target->kind == ExprKind::Index)
    {
        // Array index assignment: arr[i] := value
        auto &indexExpr = static_cast<const IndexExpr &>(*stmt.target);
        if (!indexExpr.base || indexExpr.indices.empty())
            return;

        PasType baseType = typeOfExpr(*indexExpr.base);

        if (baseType.kind == PasTypeKind::Array && indexExpr.base->kind == ExprKind::Name)
        {
            const auto &nameExpr = static_cast<const NameExpr &>(*indexExpr.base);
            std::string key = toLower(nameExpr.name);
            auto it = locals_.find(key);
            if (it != locals_.end())
            {
                Value baseAddr = it->second;

                // Get element type and size
                Type elemType = Type(Type::Kind::I64);
                int64_t elemSize = 8;
                if (baseType.elementType)
                {
                    elemType = mapType(*baseType.elementType);
                    elemSize = sizeOf(*baseType.elementType);
                }

                // Calculate offset: index * elemSize
                LowerResult index = lowerExpr(*indexExpr.indices[0]);
                Value offset = emitBinary(Opcode::IMulOvf, Type(Type::Kind::I64),
                                          index.value, Value::constInt(elemSize));

                // GEP to get element address
                Value elemAddr = emitGep(baseAddr, offset);

                // Lower value
                LowerResult value = lowerExpr(*stmt.value);

                // Store to element
                emitStore(elemType, elemAddr, value.value);
            }
        }
    }
}

void Lowerer::lowerCallStmt(const CallStmt &stmt)
{
    if (stmt.call && stmt.call->kind == ExprKind::Call)
    {
        lowerCall(static_cast<const CallExpr &>(*stmt.call));
    }
}

void Lowerer::lowerBlock(const BlockStmt &stmt)
{
    for (const auto &s : stmt.stmts)
    {
        if (s)
            lowerStmt(*s);
    }
}

void Lowerer::lowerIf(const IfStmt &stmt)
{
    size_t thenBlock = createBlock("if_then");
    size_t endBlock = createBlock("if_end");
    size_t elseBlock = stmt.elseBranch ? createBlock("if_else") : endBlock;

    // Evaluate condition
    LowerResult cond = lowerExpr(*stmt.condition);
    emitCBr(cond.value, thenBlock, elseBlock);

    // Then branch
    setBlock(thenBlock);
    if (stmt.thenBranch)
        lowerStmt(*stmt.thenBranch);
    emitBr(endBlock);

    // Else branch
    if (stmt.elseBranch)
    {
        setBlock(elseBlock);
        lowerStmt(*stmt.elseBranch);
        emitBr(endBlock);
    }

    setBlock(endBlock);
}

void Lowerer::lowerCase(const CaseStmt &stmt)
{
    // Lower case as if-else cascade
    LowerResult scrutinee = lowerExpr(*stmt.expr);
    size_t endBlock = createBlock("case_end");

    for (size_t i = 0; i < stmt.arms.size(); ++i)
    {
        const CaseArm &arm = stmt.arms[i];
        size_t armBlock = createBlock("case_arm");
        size_t nextBlock;
        if (i + 1 < stmt.arms.size())
        {
            nextBlock = createBlock("case_next");
        }
        else if (stmt.elseBody)
        {
            nextBlock = createBlock("case_else");
        }
        else
        {
            nextBlock = endBlock;
        }

        // Check each label - create test blocks for each label
        // Pattern: test_0 -> (match? arm : test_1) -> (match? arm : test_2) -> ... -> nextBlock
        for (size_t j = 0; j < arm.labels.size(); ++j)
        {
            LowerResult labelVal = lowerExpr(*arm.labels[j]);
            Value match = emitBinary(Opcode::ICmpEq, Type(Type::Kind::I1),
                                     scrutinee.value, labelVal.value);

            // If this is not the last label, create another test block
            size_t falseBlock = (j + 1 < arm.labels.size()) ? createBlock("case_test") : nextBlock;
            emitCBr(match, armBlock, falseBlock);

            // Move to the next test block for the next label check
            if (j + 1 < arm.labels.size())
            {
                setBlock(falseBlock);
            }
        }

        // Arm body
        setBlock(armBlock);
        if (arm.body)
            lowerStmt(*arm.body);
        emitBr(endBlock);

        if (nextBlock != endBlock)
            setBlock(nextBlock);
    }

    // Else body
    if (stmt.elseBody)
    {
        lowerStmt(*stmt.elseBody);
        emitBr(endBlock);
    }

    setBlock(endBlock);
}

void Lowerer::lowerFor(const ForStmt &stmt)
{
    size_t headerBlock = createBlock("for_header");
    size_t bodyBlock = createBlock("for_body");
    size_t afterBlock = createBlock("for_after");
    size_t exitBlock = createBlock("for_exit");

    // Allocate loop variable if not already
    std::string key = toLower(stmt.loopVar);
    Value loopSlot;
    auto it = locals_.find(key);
    if (it == locals_.end())
    {
        loopSlot = emitAlloca(8);
        locals_[key] = loopSlot;
    }
    else
    {
        loopSlot = it->second;
    }

    // Initialize loop variable
    LowerResult startVal = lowerExpr(*stmt.start);
    emitStore(Type(Type::Kind::I64), loopSlot, startVal.value);

    // Evaluate bound once
    LowerResult boundVal = lowerExpr(*stmt.bound);
    Value bound = boundVal.value;

    emitBr(headerBlock);

    // Header: check condition
    setBlock(headerBlock);
    Value loopVal = emitLoad(Type(Type::Kind::I64), loopSlot);
    Value cond;
    if (stmt.direction == ForDirection::To)
    {
        cond = emitBinary(Opcode::SCmpLE, Type(Type::Kind::I1), loopVal, bound);
    }
    else
    {
        cond = emitBinary(Opcode::SCmpGE, Type(Type::Kind::I1), loopVal, bound);
    }
    emitCBr(cond, bodyBlock, exitBlock);

    // Body
    loopStack_.push(exitBlock, afterBlock);
    setBlock(bodyBlock);
    if (stmt.body)
        lowerStmt(*stmt.body);
    emitBr(afterBlock);
    loopStack_.pop();

    // After: increment/decrement (use overflow-checking variants for signed integers)
    setBlock(afterBlock);
    Value currentVal = emitLoad(Type(Type::Kind::I64), loopSlot);
    Value one = Value::constInt(1);
    Value newVal;
    if (stmt.direction == ForDirection::To)
    {
        newVal = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), currentVal, one);
    }
    else
    {
        newVal = emitBinary(Opcode::ISubOvf, Type(Type::Kind::I64), currentVal, one);
    }
    emitStore(Type(Type::Kind::I64), loopSlot, newVal);
    emitBr(headerBlock);

    setBlock(exitBlock);
}

void Lowerer::lowerForIn(const ForInStmt &stmt)
{
    // Desugar to index-based loop:
    // for item in arr do body  =>  for i := 0 to Length(arr)-1 do begin item := arr[i]; body end
    // for ch in s do body      =>  for i := 0 to Length(s)-1 do begin ch := s[i]; body end

    size_t headerBlock = createBlock("forin_header");
    size_t bodyBlock = createBlock("forin_body");
    size_t afterBlock = createBlock("forin_after");
    size_t exitBlock = createBlock("forin_exit");

    // Get collection type
    PasType collType = typeOfExpr(*stmt.collection);
    bool isString = (collType.kind == PasTypeKind::String);
    bool isArray = (collType.kind == PasTypeKind::Array);

    // Allocate index variable
    Value indexSlot = emitAlloca(8);
    emitStore(Type(Type::Kind::I64), indexSlot, Value::constInt(0));

    // Get collection value
    LowerResult collection = lowerExpr(*stmt.collection);

    // Get length based on collection type
    Value length;
    if (isString)
    {
        // Call rt_len for strings
        length = emitCallRet(Type(Type::Kind::I64), "rt_len", {collection.value});
    }
    else if (isArray)
    {
        // Call appropriate rt_arr_*_len based on element type
        // For now, use generic i64 array length as placeholder
        length = emitCallRet(Type(Type::Kind::I64), "rt_arr_i64_len", {collection.value});
    }
    else
    {
        // Fallback for unsupported types
        length = Value::constInt(0);
    }

    emitBr(headerBlock);

    // Header: check i < length
    setBlock(headerBlock);
    Value indexVal = emitLoad(Type(Type::Kind::I64), indexSlot);
    Value cond = emitBinary(Opcode::SCmpLT, Type(Type::Kind::I1), indexVal, length);
    emitCBr(cond, bodyBlock, exitBlock);

    // Body
    loopStack_.push(exitBlock, afterBlock);
    setBlock(bodyBlock);

    // Allocate loop variable slot if not already present
    std::string key = toLower(stmt.loopVar);
    Value varSlot;
    if (locals_.find(key) == locals_.end())
    {
        varSlot = emitAlloca(8);
        locals_[key] = varSlot;
    }
    else
    {
        varSlot = locals_[key];
    }

    // Get element at current index and store in loop variable
    Value currentIdx = emitLoad(Type(Type::Kind::I64), indexSlot);
    if (isString)
    {
        // Get single character as a string: rt_substr(s, i, 1)
        Value elem = emitCallRet(Type(Type::Kind::Str), "rt_substr",
                                 {collection.value, currentIdx, Value::constInt(1)});
        emitStore(Type(Type::Kind::Str), varSlot, elem);
    }
    else if (isArray)
    {
        // Get array element: rt_arr_i64_get(arr, i)
        Value elem = emitCallRet(Type(Type::Kind::I64), "rt_arr_i64_get",
                                 {collection.value, currentIdx});
        emitStore(Type(Type::Kind::I64), varSlot, elem);
    }

    if (stmt.body)
        lowerStmt(*stmt.body);
    emitBr(afterBlock);
    loopStack_.pop();

    // After: increment index (use overflow-checking addition)
    setBlock(afterBlock);
    Value idxAfter = emitLoad(Type(Type::Kind::I64), indexSlot);
    Value newIdx = emitBinary(Opcode::IAddOvf, Type(Type::Kind::I64), idxAfter, Value::constInt(1));
    emitStore(Type(Type::Kind::I64), indexSlot, newIdx);
    emitBr(headerBlock);

    setBlock(exitBlock);
}

void Lowerer::lowerWhile(const WhileStmt &stmt)
{
    size_t headerBlock = createBlock("while_header");
    size_t bodyBlock = createBlock("while_body");
    size_t exitBlock = createBlock("while_exit");

    emitBr(headerBlock);

    // Header: evaluate condition
    setBlock(headerBlock);
    LowerResult cond = lowerExpr(*stmt.condition);
    emitCBr(cond.value, bodyBlock, exitBlock);

    // Body
    loopStack_.push(exitBlock, headerBlock);
    setBlock(bodyBlock);
    if (stmt.body)
        lowerStmt(*stmt.body);
    emitBr(headerBlock);
    loopStack_.pop();

    setBlock(exitBlock);
}

void Lowerer::lowerRepeat(const RepeatStmt &stmt)
{
    size_t bodyBlock = createBlock("repeat_body");
    size_t headerBlock = createBlock("repeat_header");
    size_t exitBlock = createBlock("repeat_exit");

    emitBr(bodyBlock);

    // Body (executes first)
    loopStack_.push(exitBlock, headerBlock);
    setBlock(bodyBlock);
    if (stmt.body)
        lowerStmt(*stmt.body);
    emitBr(headerBlock);
    loopStack_.pop();

    // Header: evaluate condition (until condition is true)
    setBlock(headerBlock);
    LowerResult cond = lowerExpr(*stmt.condition);
    // Repeat until: loop while condition is false
    emitCBr(cond.value, exitBlock, bodyBlock);

    setBlock(exitBlock);
}

void Lowerer::lowerBreak(const BreakStmt &)
{
    if (!loopStack_.empty())
    {
        emitBr(loopStack_.breakTarget());
        // Create a dead block for any following code
        size_t deadBlock = createBlock("after_break");
        setBlock(deadBlock);
    }
}

void Lowerer::lowerContinue(const ContinueStmt &)
{
    if (!loopStack_.empty())
    {
        emitBr(loopStack_.continueTarget());
        // Create a dead block for any following code
        size_t deadBlock = createBlock("after_continue");
        setBlock(deadBlock);
    }
}

//===----------------------------------------------------------------------===//
// Instruction Emission Helpers
void Lowerer::lowerRaise(const RaiseStmt &stmt)
{
    if (stmt.exception)
    {
        // raise Expr; - evaluate expression and call rt_throw
        LowerResult excVal = lowerExpr(*stmt.exception);
        emitCall("rt_throw", {excVal.value});
        // rt_throw does not return, but we still need to mark block as unterminated
        // for subsequent code (which is dead but may exist)
    }
    else
    {
        // raise; (re-raise) - only valid inside except handler
        // Use ResumeSame with the current handler's resume token
        if (inExceptHandler_)
        {
            emitResumeSame(currentResumeTok_);
        }
        // If not in handler, semantic analysis should have caught this error
    }
}

void Lowerer::lowerExit(const ExitStmt &stmt)
{
    if (stmt.value)
    {
        // Exit(value) - store value in Result slot and return
        LowerResult value = lowerExpr(*stmt.value);

        // Store to the function's result variable (named after function)
        if (!currentFuncName_.empty())
        {
            auto it = locals_.find(currentFuncName_);
            if (it != locals_.end())
            {
                // Get return type
                auto retType = sema_->lookupVariable(currentFuncName_);
                Type ilType = retType ? mapType(*retType) : value.type;
                emitStore(ilType, it->second, value.value);
            }
        }
    }

    // Emit return
    if (!currentFuncName_.empty())
    {
        // Function - load and return Result value
        auto it = locals_.find(currentFuncName_);
        if (it != locals_.end())
        {
            auto retType = sema_->lookupVariable(currentFuncName_);
            Type ilType = retType ? mapType(*retType) : Type(Type::Kind::I64);
            Value retVal = emitLoad(ilType, it->second);
            emitRet(retVal);
        }
        else
        {
            // Fallback for void return
            emitRetVoid();
        }
    }
    else
    {
        // Procedure or fallback - void return
        emitRetVoid();
    }

    // Create a dead block for any following code (like Break/Continue)
    size_t deadBlock = createBlock("after_exit");
    setBlock(deadBlock);
}

void Lowerer::lowerTryExcept(const TryExceptStmt &stmt)
{
    // Create blocks:
    // - handler: receives exception, dispatches to matching handler or propagates
    // - after: continuation after try-except
    size_t handlerIdx = createHandlerBlock("except_handler");
    size_t afterIdx = createBlock("except_after");

    // Get handler params for later use
    BasicBlock &handlerBlk = currentFunc_->blocks[handlerIdx];
    Value errParam = Value::temp(handlerBlk.params[0].id);
    Value tokParam = Value::temp(handlerBlk.params[1].id);

    // In current block: EhPush, then branch to try body
    size_t tryBodyIdx = createBlock("try_body");
    emitEhPush(handlerIdx);
    emitBr(tryBodyIdx);

    // Lower try body
    setBlock(tryBodyIdx);
    if (stmt.tryBody)
    {
        lowerBlock(*stmt.tryBody);
    }

    // Normal exit: EhPop and branch to after
    if (!currentBlock()->terminated)
    {
        emitEhPop();
        emitBr(afterIdx);
    }

    // Lower handler block: dispatch to matching handlers
    setBlock(handlerIdx);

    // Save previous handler state and set current
    bool prevInHandler = inExceptHandler_;
    Value prevResumeTok = currentResumeTok_;
    inExceptHandler_ = true;
    currentResumeTok_ = tokParam;

    // For each handler: check type and dispatch
    // Pascal "on E: Type do" checks if exception is of Type
    // For simplicity, we'll use a cascade of type checks

    // Pascal "on E: Type do" exception handling
    // NOTE: Full type-based dispatch requires runtime support (rt_exc_is_type)
    // For now, implement catch-all: first handler catches all exceptions.
    // The type name is stored but not checked at runtime.

    if (!stmt.handlers.empty())
    {
        // Use the first handler as catch-all (Pascal semantics: first match wins)
        const ExceptHandler &h = stmt.handlers[0];

        // Bind exception variable if named
        if (!h.varName.empty())
        {
            std::string key = toLower(h.varName);
            Value slot = emitAlloca(8);
            locals_[key] = slot;
            emitStore(Type(Type::Kind::Ptr), slot, errParam);
        }

        // Lower handler body
        if (h.body)
        {
            lowerStmt(*h.body);
        }

        // Exit handler: ResumeLabel to after
        if (!currentBlock()->terminated)
        {
            emitResumeLabel(tokParam, afterIdx);
        }
    }
    else if (stmt.elseBody)
    {
        // No typed handlers, but have else clause - execute else
        lowerStmt(*stmt.elseBody);
        if (!currentBlock()->terminated)
        {
            emitResumeLabel(tokParam, afterIdx);
        }
    }
    else
    {
        // No handlers and no else - propagate exception
        emitResumeSame(tokParam);
    }

    // Restore handler state
    inExceptHandler_ = prevInHandler;
    currentResumeTok_ = prevResumeTok;

    // Continue at after block
    setBlock(afterIdx);
}

void Lowerer::lowerTryFinally(const TryFinallyStmt &stmt)
{
    // Create blocks:
    // - handler: receives exception, runs finally, propagates
    // - finally_normal: runs finally on normal path
    // - after: continuation after try-finally
    size_t handlerIdx = createHandlerBlock("finally_handler");
    size_t finallyNormalIdx = createBlock("finally_normal");
    size_t afterIdx = createBlock("finally_after");

    // Get handler params
    BasicBlock &handlerBlk = currentFunc_->blocks[handlerIdx];
    Value tokParam = Value::temp(handlerBlk.params[1].id);

    // In current block: EhPush, then branch to try body
    size_t tryBodyIdx = createBlock("try_body");
    emitEhPush(handlerIdx);
    emitBr(tryBodyIdx);

    // Lower try body
    setBlock(tryBodyIdx);
    if (stmt.tryBody)
    {
        lowerBlock(*stmt.tryBody);
    }

    // Normal exit: EhPop and branch to finally_normal
    if (!currentBlock()->terminated)
    {
        emitEhPop();
        emitBr(finallyNormalIdx);
    }

    // finally_normal: run finally body, then branch to after
    setBlock(finallyNormalIdx);
    if (stmt.finallyBody)
    {
        lowerBlock(*stmt.finallyBody);
    }
    if (!currentBlock()->terminated)
    {
        emitBr(afterIdx);
    }

    // Handler: run finally body, then propagate exception
    setBlock(handlerIdx);
    if (stmt.finallyBody)
    {
        lowerBlock(*stmt.finallyBody);
    }
    if (!currentBlock()->terminated)
    {
        emitResumeSame(tokParam);
    }

    // Continue at after block
    setBlock(afterIdx);
}

void Lowerer::lowerWith(const WithStmt &stmt)
{
    // For each 'with' object, evaluate the expression and store in a temp slot.
    // Push a WithContext so that lowerName can find fields of the with target.
    std::vector<WithContext> pushedContexts;

    for (const auto &obj : stmt.objects)
    {
        // Get the type of the with expression
        PasType objType = typeOfExpr(*obj);

        // Create temp slot for this with target
        int64_t slotSize = (objType.kind == PasTypeKind::Class) ? 8 : sizeOf(objType);
        Value slot = emitAlloca(slotSize);

        // Evaluate the with expression
        LowerResult result = lowerExpr(*obj);

        // Store into temp slot
        Type ilType = mapType(objType);
        emitStore(ilType, slot, result.value);

        // Push the with context
        WithContext ctx;
        ctx.type = objType;
        ctx.slot = slot;
        withContexts_.push_back(ctx);
        pushedContexts.push_back(ctx);
    }

    // Lower the body with the with contexts active
    if (stmt.body)
    {
        lowerStmt(*stmt.body);
    }

    // Pop the contexts
    for (size_t i = 0; i < pushedContexts.size(); ++i)
    {
        withContexts_.pop_back();
    }
}

} // namespace il::frontends::pascal

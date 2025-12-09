//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer_OOP.cpp
// Purpose: OOP lowering for Pascal classes to IL.
// Key invariants: Vtable at offset 0; fields follow in declaration order.
// Ownership/Lifetime: Part of Lowerer; operates on borrowed AST.
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Lowerer.hpp"
#include "frontends/common/CharUtils.hpp"
#include "il/core/Instr.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <algorithm>
#include <queue>

namespace il::frontends::pascal
{

using common::char_utils::toLowercase;

inline std::string toLower(const std::string &s)
{
    return toLowercase(s);
}

//===----------------------------------------------------------------------===//
// ClassLayout Helper
//===----------------------------------------------------------------------===//

const ClassFieldLayout *ClassLayout::findField(const std::string &name) const
{
    std::string key = toLower(name);
    for (const auto &field : fields)
    {
        if (toLower(field.name) == key)
            return &field;
    }
    return nullptr;
}

//===----------------------------------------------------------------------===//
// Name Mangling
//===----------------------------------------------------------------------===//

std::string Lowerer::mangleMethod(const std::string &className, const std::string &methodName)
{
    return className + "." + methodName;
}

std::string Lowerer::mangleConstructor(const std::string &className, const std::string &ctorName)
{
    return className + "." + ctorName;
}

std::string Lowerer::mangleDestructor(const std::string &className, const std::string &dtorName)
{
    return className + "." + dtorName;
}

//===----------------------------------------------------------------------===//
// Class Scanning and Layout Computation
//===----------------------------------------------------------------------===//

void Lowerer::scanClasses(const std::vector<std::unique_ptr<Decl>> &decls)
{
    // Collect all class names first
    std::vector<std::string> classNames;
    for (const auto &decl : decls)
    {
        if (decl && decl->kind == DeclKind::Class)
        {
            auto &classDecl = static_cast<const ClassDecl &>(*decl);
            classNames.push_back(classDecl.name);
        }
    }

    // Sort classes so base classes come before derived (topological sort)
    // This ensures we can look up base class layouts when computing derived layouts
    std::vector<std::string> sorted;
    std::set<std::string> visited;

    std::function<void(const std::string &)> visit = [&](const std::string &name) {
        std::string key = toLower(name);
        if (visited.count(key))
            return;
        visited.insert(key);

        const ClassInfo *info = sema_->lookupClass(key);
        if (info && !info->baseClass.empty())
        {
            visit(info->baseClass);
        }
        sorted.push_back(name);
    };

    for (const auto &name : classNames)
    {
        visit(name);
    }

    classRegistrationOrder_ = sorted;

    // Compute layouts in topological order
    for (const auto &name : sorted)
    {
        computeClassLayout(name);
        computeVtableLayout(name);
    }
}

void Lowerer::computeClassLayout(const std::string &className)
{
    std::string key = toLower(className);
    const ClassInfo *info = sema_->lookupClass(key);
    if (!info)
        return;

    ClassLayout layout;
    layout.name = className;
    layout.classId = nextClassId_++;

    // Start with vtable pointer at offset 0
    std::size_t currentOffset = 8; // vtable pointer is 8 bytes

    // If there's a base class, inherit its fields first
    if (!info->baseClass.empty())
    {
        auto baseIt = classLayouts_.find(toLower(info->baseClass));
        if (baseIt != classLayouts_.end())
        {
            // Copy base class fields (they're already at correct offsets)
            layout.fields = baseIt->second.fields;
            currentOffset = baseIt->second.size;
        }
    }

    // Add this class's own fields
    for (const auto &[fieldName, fieldInfo] : info->fields)
    {
        ClassFieldLayout fieldLayout;
        fieldLayout.name = fieldInfo.name;
        fieldLayout.type = fieldInfo.type;
        fieldLayout.size = static_cast<std::size_t>(sizeOf(fieldInfo.type));

        // Align to 8 bytes for simplicity
        if (currentOffset % 8 != 0)
            currentOffset = ((currentOffset / 8) + 1) * 8;

        fieldLayout.offset = currentOffset;
        currentOffset += fieldLayout.size;

        layout.fields.push_back(fieldLayout);
    }

    // Align total size to 8 bytes
    if (currentOffset % 8 != 0)
        currentOffset = ((currentOffset / 8) + 1) * 8;

    // Minimum object size is 8 (for vtable pointer)
    if (currentOffset < 8)
        currentOffset = 8;

    layout.size = currentOffset;

    classLayouts_[key] = std::move(layout);
}

void Lowerer::computeVtableLayout(const std::string &className)
{
    std::string key = toLower(className);
    const ClassInfo *info = sema_->lookupClass(key);
    if (!info)
        return;

    VtableLayout vtable;
    vtable.className = className;

    // If there's a base class, inherit its vtable slots
    if (!info->baseClass.empty())
    {
        auto baseIt = vtableLayouts_.find(toLower(info->baseClass));
        if (baseIt != vtableLayouts_.end())
        {
            vtable.slots = baseIt->second.slots;
        }
    }

    // Process this class's methods
    for (const auto &[methodName, methodInfo] : info->methods)
    {
        if (!methodInfo.isVirtual && !methodInfo.isOverride)
            continue; // Skip non-virtual methods

        std::string methodKey = toLower(methodInfo.name);

        if (methodInfo.isOverride)
        {
            // Find existing slot and update implementation class
            for (auto &slot : vtable.slots)
            {
                if (toLower(slot.methodName) == methodKey)
                {
                    slot.implClass = className;
                    break;
                }
            }
        }
        else if (methodInfo.isVirtual)
        {
            // New virtual method - add a new slot
            VtableSlot slot;
            slot.methodName = methodInfo.name;
            slot.implClass = className;
            slot.slot = static_cast<int>(vtable.slots.size());
            vtable.slots.push_back(slot);
        }
    }

    vtable.slotCount = vtable.slots.size();
    vtableLayouts_[key] = std::move(vtable);
}

int Lowerer::getVirtualSlot(const std::string &className, const std::string &methodName) const
{
    auto it = vtableLayouts_.find(toLower(className));
    if (it == vtableLayouts_.end())
        return -1;

    std::string methodKey = toLower(methodName);
    for (const auto &slot : it->second.slots)
    {
        if (toLower(slot.methodName) == methodKey)
            return slot.slot;
    }
    return -1;
}

std::size_t Lowerer::getFieldOffset(const std::string &className, const std::string &fieldName) const
{
    auto it = classLayouts_.find(toLower(className));
    if (it == classLayouts_.end())
        return 0;

    const ClassFieldLayout *field = it->second.findField(fieldName);
    return field ? field->offset : 0;
}

//===----------------------------------------------------------------------===//
// OOP Module Initialization
//===----------------------------------------------------------------------===//

void Lowerer::emitOopModuleInit()
{
    if (classRegistrationOrder_.empty())
        return;

    // Create __pas_oop_init function
    Function *savedFunc = currentFunc_;
    currentFunc_ = &builder_->startFunction("__pas_oop_init", Type(Type::Kind::Void), {});

    size_t entryIdx = createBlock("entry");
    setBlock(entryIdx);

    // Register classes in topological order (base before derived)
    for (const auto &className : classRegistrationOrder_)
    {
        emitVtableRegistration(className);
    }

    emitRetVoid();
    currentFunc_ = savedFunc;
}

void Lowerer::emitVtableRegistration(const std::string &className)
{
    std::string key = toLower(className);
    auto layoutIt = classLayouts_.find(key);
    auto vtableIt = vtableLayouts_.find(key);

    if (layoutIt == classLayouts_.end())
        return;

    const ClassLayout &layout = layoutIt->second;
    const ClassInfo *info = sema_->lookupClass(key);
    if (!info)
        return;

    // Allocate vtable if there are virtual methods
    std::size_t slotCount = vtableIt != vtableLayouts_.end() ? vtableIt->second.slotCount : 0;
    long long vtableBytes = slotCount > 0 ? static_cast<long long>(slotCount * 8) : 8;

    usedExterns_.insert("rt_alloc");
    Value vtablePtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(vtableBytes)});

    // Populate vtable slots
    if (vtableIt != vtableLayouts_.end())
    {
        for (const auto &slot : vtableIt->second.slots)
        {
            long long offset = static_cast<long long>(slot.slot * 8);
            Value slotPtr = emitGep(vtablePtr, Value::constInt(offset));

            // Get function pointer for the implementation
            std::string funcName = mangleMethod(slot.implClass, slot.methodName);
            Value funcPtr = Value::global(funcName);
            emitStore(Type(Type::Kind::Ptr), slotPtr, funcPtr);
        }
    }

    // Get base class ID (0 if no base)
    long long baseClassId = 0;
    if (!info->baseClass.empty())
    {
        auto baseLayoutIt = classLayouts_.find(toLower(info->baseClass));
        if (baseLayoutIt != classLayouts_.end())
        {
            baseClassId = baseLayoutIt->second.classId;
        }
    }

    // Create class name string
    std::string nameGlobal = getStringGlobal(className);
    Value nameStr = emitConstStr(nameGlobal);

    // Register class with runtime
    usedExterns_.insert("rt_register_class_with_base_rs");
    emitCall("rt_register_class_with_base_rs",
             {Value::constInt(layout.classId), vtablePtr, nameStr,
              Value::constInt(static_cast<long long>(slotCount)),
              Value::constInt(baseClassId)});
}

//===----------------------------------------------------------------------===//
// Indirect Calls
//===----------------------------------------------------------------------===//

Lowerer::Value Lowerer::emitCallIndirectRet(Type retTy, Value callee, const std::vector<Value> &args)
{
    unsigned id = nextTempId();
    il::core::Instr instr;
    instr.result = id;
    instr.op = Opcode::CallIndirect;
    instr.type = retTy;
    instr.operands.push_back(callee);
    for (const auto &arg : args)
        instr.operands.push_back(arg);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
    return Value::temp(id);
}

void Lowerer::emitCallIndirect(Value callee, const std::vector<Value> &args)
{
    il::core::Instr instr;
    instr.op = Opcode::CallIndirect;
    instr.type = Type(Type::Kind::Void);
    instr.operands.push_back(callee);
    for (const auto &arg : args)
        instr.operands.push_back(arg);
    instr.loc = {};
    currentBlock()->instructions.push_back(std::move(instr));
}

//===----------------------------------------------------------------------===//
// Constructor Call Lowering
//===----------------------------------------------------------------------===//

LowerResult Lowerer::lowerConstructorCall(const CallExpr &expr)
{
    // This handles ClassName.Create(args) constructor calls
    // The semantic analyzer has already marked this as a constructor call

    std::string className = expr.constructorClassName;
    std::string key = toLower(className);

    auto layoutIt = classLayouts_.find(key);
    if (layoutIt == classLayouts_.end())
    {
        // No layout computed - class not found
        return {Value::null(), Type(Type::Kind::Ptr)};
    }

    const ClassLayout &layout = layoutIt->second;

    // Step 1: Allocate object
    usedExterns_.insert("rt_obj_new_i64");
    Value objPtr = emitCallRet(Type(Type::Kind::Ptr), "rt_obj_new_i64",
                               {Value::constInt(layout.classId),
                                Value::constInt(static_cast<long long>(layout.size))});

    // Step 2: Initialize vtable pointer (offset 0)
    usedExterns_.insert("rt_get_class_vtable");
    Value vtablePtr = emitCallRet(Type(Type::Kind::Ptr), "rt_get_class_vtable",
                                  {Value::constInt(layout.classId)});
    emitStore(Type(Type::Kind::Ptr), objPtr, vtablePtr);

    // Step 3: Get constructor name from the call expression
    std::string ctorName = "Create"; // Default
    if (expr.callee && expr.callee->kind == ExprKind::Field)
    {
        const auto &fieldExpr = static_cast<const FieldExpr &>(*expr.callee);
        ctorName = fieldExpr.field;
    }

    // Step 4: Build constructor arguments (Self first, then user args)
    std::vector<Value> ctorArgs;
    ctorArgs.push_back(objPtr); // Self parameter

    for (const auto &arg : expr.args)
    {
        LowerResult argResult = lowerExpr(*arg);
        ctorArgs.push_back(argResult.value);
    }

    // Step 5: Call the constructor
    std::string ctorFunc = mangleConstructor(className, ctorName);
    emitCall(ctorFunc, ctorArgs);

    // Return the object pointer
    return {objPtr, Type(Type::Kind::Ptr)};
}

//===----------------------------------------------------------------------===//
// Method Call Lowering
//===----------------------------------------------------------------------===//

LowerResult Lowerer::lowerMethodCall(const FieldExpr &fieldExpr, const CallExpr &callExpr)
{
    // Lower the receiver (base object)
    LowerResult base = lowerExpr(*fieldExpr.base);
    Value selfPtr = base.value;

    // Get the class name from the base type using semantic analyzer
    PasType baseType = sema_->typeOf(*fieldExpr.base);
    std::string className;

    if (baseType.kind == PasTypeKind::Class)
    {
        className = baseType.name;
    }

    if (className.empty())
    {
        // Can't determine class - fall back to direct call
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    std::string methodName = fieldExpr.field;

    // Get method info
    const ClassInfo *classInfo = sema_->lookupClass(toLower(className));
    if (!classInfo)
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    auto methodIt = classInfo->methods.find(toLower(methodName));
    if (methodIt == classInfo->methods.end())
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    const MethodInfo &methodInfo = methodIt->second;

    // Build argument list (Self first)
    std::vector<Value> args;
    args.push_back(selfPtr);

    for (const auto &arg : callExpr.args)
    {
        LowerResult argResult = lowerExpr(*arg);
        args.push_back(argResult.value);
    }

    // Determine return type
    Type retTy = mapType(methodInfo.returnType);

    // Check if this is a virtual method call
    int slot = getVirtualSlot(className, methodName);

    if (slot >= 0 && (methodInfo.isVirtual || methodInfo.isOverride))
    {
        // Virtual dispatch: load vtable, load function pointer, call indirect
        Value vtablePtr = emitLoad(Type(Type::Kind::Ptr), selfPtr);
        Value slotPtr = emitGep(vtablePtr, Value::constInt(static_cast<long long>(slot * 8)));
        Value funcPtr = emitLoad(Type(Type::Kind::Ptr), slotPtr);

        if (retTy.kind == Type::Kind::Void)
        {
            emitCallIndirect(funcPtr, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        else
        {
            Value result = emitCallIndirectRet(retTy, funcPtr, args);
            return {result, retTy};
        }
    }
    else
    {
        // Direct call for non-virtual methods
        std::string funcName = mangleMethod(className, methodName);

        if (retTy.kind == Type::Kind::Void)
        {
            emitCall(funcName, args);
            return {Value::constInt(0), Type(Type::Kind::Void)};
        }
        else
        {
            Value result = emitCallRet(retTy, funcName, args);
            return {result, retTy};
        }
    }
}

//===----------------------------------------------------------------------===//
// Object Field Access Lowering
//===----------------------------------------------------------------------===//

LowerResult Lowerer::lowerObjectFieldAccess(const FieldExpr &expr)
{
    // Lower the base object
    LowerResult base = lowerExpr(*expr.base);
    Value objPtr = base.value;

    // Determine the class name
    std::string className;
    if (expr.base->kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(*expr.base);
        std::string varName = toLower(nameExpr.name);

        if (varName == "self" && !currentClassName_.empty())
        {
            className = currentClassName_;
        }
        else
        {
            auto typeIt = localTypes_.find(varName);
            if (typeIt != localTypes_.end() && typeIt->second.kind == PasTypeKind::Class)
            {
                className = typeIt->second.name;
            }
        }
    }

    if (className.empty())
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Get field offset
    auto layoutIt = classLayouts_.find(toLower(className));
    if (layoutIt == classLayouts_.end())
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    const ClassFieldLayout *field = layoutIt->second.findField(expr.field);
    if (!field)
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Compute field pointer
    Value fieldPtr = emitGep(objPtr, Value::constInt(static_cast<long long>(field->offset)));

    // Load the field value
    Type fieldTy = mapType(field->type);
    Value fieldVal = emitLoad(fieldTy, fieldPtr);

    return {fieldVal, fieldTy};
}

} // namespace il::frontends::pascal

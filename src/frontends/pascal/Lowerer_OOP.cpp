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

#include "frontends/common/CharUtils.hpp"
#include "frontends/pascal/Lowerer.hpp"
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
// Name Mangling - Use common library functions
//===----------------------------------------------------------------------===//

// Import common name mangling functions for use in this file
using common::mangleConstructor;
using common::mangleDestructor;
using common::mangleMethod;

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

    std::function<void(const std::string &)> visit = [&](const std::string &name)
    {
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

    // Process this class's methods (all overloads)
    for (const auto &[methodName, overloads] : info->methods)
    {
        for (const auto &methodInfo : overloads)
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

std::size_t Lowerer::getFieldOffset(const std::string &className,
                                    const std::string &fieldName) const
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
    if (classRegistrationOrder_.empty() && interfaceRegistrationOrder_.empty())
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

    // Register interface implementation tables for each class
    for (const auto &className : classRegistrationOrder_)
    {
        const ClassInfo *classInfo = sema_->lookupClass(toLower(className));
        if (!classInfo)
            continue;

        // Direct interfaces
        for (const auto &ifaceName : classInfo->interfaces)
        {
            emitInterfaceTableRegistration(className, ifaceName);
        }

        // Also inherited interfaces from base class
        if (!classInfo->baseClass.empty())
        {
            const ClassInfo *baseInfo = sema_->lookupClass(toLower(classInfo->baseClass));
            if (baseInfo)
            {
                for (const auto &ifaceName : baseInfo->interfaces)
                {
                    // Only if not already registered as direct
                    bool isDirect = false;
                    for (const auto &directIface : classInfo->interfaces)
                    {
                        if (toLower(directIface) == toLower(ifaceName))
                        {
                            isDirect = true;
                            break;
                        }
                    }
                    if (!isDirect)
                    {
                        emitInterfaceTableRegistration(className, ifaceName);
                    }
                }
            }
        }
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
    Value vtablePtr =
        emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(vtableBytes)});

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
             {Value::constInt(layout.classId),
              vtablePtr,
              nameStr,
              Value::constInt(static_cast<long long>(slotCount)),
              Value::constInt(baseClassId)});
}

//===----------------------------------------------------------------------===//
// Indirect Calls
//===----------------------------------------------------------------------===//

Lowerer::Value Lowerer::emitCallIndirectRet(Type retTy,
                                            Value callee,
                                            const std::vector<Value> &args)
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
    Value objPtr = emitCallRet(
        Type(Type::Kind::Ptr),
        "rt_obj_new_i64",
        {Value::constInt(layout.classId), Value::constInt(static_cast<long long>(layout.size))});

    // Step 2: Initialize vtable pointer (offset 0)
    usedExterns_.insert("rt_get_class_vtable");
    Value vtablePtr = emitCallRet(
        Type(Type::Kind::Ptr), "rt_get_class_vtable", {Value::constInt(layout.classId)});
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

    // Get the class name from the base type
    PasType baseType = typeOfExpr(*fieldExpr.base);
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

    const MethodInfo *methodInfo = classInfo->findMethod(toLower(methodName));
    if (!methodInfo)
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Build argument list (Self first)
    std::vector<Value> args;
    args.push_back(selfPtr);

    for (const auto &arg : callExpr.args)
    {
        LowerResult argResult = lowerExpr(*arg);
        args.push_back(argResult.value);
    }

    // Determine return type
    Type retTy = mapType(methodInfo->returnType);

    // Check if this is a virtual method call
    int slot = getVirtualSlot(className, methodName);

    if (slot >= 0 && (methodInfo->isVirtual || methodInfo->isOverride))
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

//===----------------------------------------------------------------------===//
// Interface Scanning and Layout Computation
//===----------------------------------------------------------------------===//

void Lowerer::scanInterfaces(const std::vector<std::unique_ptr<Decl>> &decls)
{
    // Collect all interface names first
    std::vector<std::string> ifaceNames;
    for (const auto &decl : decls)
    {
        if (decl && decl->kind == DeclKind::Interface)
        {
            auto &ifaceDecl = static_cast<const InterfaceDecl &>(*decl);
            ifaceNames.push_back(ifaceDecl.name);
        }
    }

    // Sort interfaces so base interfaces come before derived (topological sort)
    std::vector<std::string> sorted;
    std::set<std::string> visited;

    std::function<void(const std::string &)> visit = [&](const std::string &name)
    {
        std::string key = toLower(name);
        if (visited.count(key))
            return;
        visited.insert(key);

        const InterfaceInfo *info = sema_->lookupInterface(key);
        if (info)
        {
            for (const auto &baseName : info->baseInterfaces)
            {
                visit(baseName);
            }
        }
        sorted.push_back(name);
    };

    for (const auto &name : ifaceNames)
    {
        visit(name);
    }

    interfaceRegistrationOrder_ = sorted;

    // Compute layouts in topological order
    for (const auto &name : sorted)
    {
        computeInterfaceLayout(name);
    }
}

void Lowerer::computeInterfaceLayout(const std::string &ifaceName)
{
    std::string key = toLower(ifaceName);
    const InterfaceInfo *info = sema_->lookupInterface(key);
    if (!info)
        return;

    InterfaceLayout layout;
    layout.name = ifaceName;
    layout.interfaceId = nextInterfaceId_++;

    // Collect all methods including from base interfaces
    std::map<std::string, MethodInfo> allMethods;
    sema_->collectInterfaceMethods(key, allMethods);

    // Assign slots in deterministic order (alphabetical by method name)
    std::vector<std::string> methodNames;
    for (const auto &[methodName, methodInfo] : allMethods)
    {
        methodNames.push_back(methodName);
    }
    std::sort(methodNames.begin(), methodNames.end());

    int slotIndex = 0;
    for (const auto &methodName : methodNames)
    {
        InterfaceSlot slot;
        slot.methodName = allMethods[methodName].name; // Use original case
        slot.slot = slotIndex++;
        layout.slots.push_back(slot);
    }

    layout.slotCount = layout.slots.size();
    interfaceLayouts_[key] = std::move(layout);
}

void Lowerer::computeInterfaceImplTables(const std::string &className)
{
    std::string classKey = toLower(className);
    const ClassInfo *classInfo = sema_->lookupClass(classKey);
    if (!classInfo)
        return;

    // Process each interface this class implements
    for (const auto &ifaceName : classInfo->interfaces)
    {
        std::string ifaceKey = toLower(ifaceName);
        auto layoutIt = interfaceLayouts_.find(ifaceKey);
        if (layoutIt == interfaceLayouts_.end())
            continue;

        const InterfaceLayout &ifaceLayout = layoutIt->second;

        InterfaceImplTable implTable;
        implTable.className = className;
        implTable.interfaceName = ifaceName;

        // For each slot in the interface, find the implementing method in the class
        for (const auto &slot : ifaceLayout.slots)
        {
            std::string methodKey = toLower(slot.methodName);

            // Search for the method in this class or its base classes
            std::string implClassName = className;
            const ClassInfo *searchClass = classInfo;
            while (searchClass)
            {
                auto methodIt = searchClass->methods.find(methodKey);
                if (methodIt != searchClass->methods.end())
                {
                    implClassName = searchClass->name;
                    break;
                }
                if (searchClass->baseClass.empty())
                    break;
                searchClass = sema_->lookupClass(toLower(searchClass->baseClass));
            }

            // Add mangled method name
            std::string mangledName = mangleMethod(implClassName, slot.methodName);
            implTable.implMethods.push_back(mangledName);
        }

        // Store with composite key
        std::string tableKey = classKey + "." + ifaceKey;
        interfaceImplTables_[tableKey] = std::move(implTable);
    }

    // Also handle interfaces inherited from base class
    if (!classInfo->baseClass.empty())
    {
        std::string baseKey = toLower(classInfo->baseClass);
        const ClassInfo *baseInfo = sema_->lookupClass(baseKey);
        if (baseInfo)
        {
            for (const auto &ifaceName : baseInfo->interfaces)
            {
                std::string ifaceKey = toLower(ifaceName);
                std::string tableKey = classKey + "." + ifaceKey;

                // Only add if not already handled (direct implementation takes precedence)
                if (interfaceImplTables_.find(tableKey) != interfaceImplTables_.end())
                    continue;

                auto layoutIt = interfaceLayouts_.find(ifaceKey);
                if (layoutIt == interfaceLayouts_.end())
                    continue;

                const InterfaceLayout &ifaceLayout = layoutIt->second;

                InterfaceImplTable implTable;
                implTable.className = className;
                implTable.interfaceName = ifaceName;

                // For inherited interfaces, methods may come from this class or base
                for (const auto &slot : ifaceLayout.slots)
                {
                    std::string methodKey = toLower(slot.methodName);

                    std::string implClassName = className;
                    const ClassInfo *searchClass = classInfo;
                    while (searchClass)
                    {
                        auto methodIt = searchClass->methods.find(methodKey);
                        if (methodIt != searchClass->methods.end())
                        {
                            implClassName = searchClass->name;
                            break;
                        }
                        if (searchClass->baseClass.empty())
                            break;
                        searchClass = sema_->lookupClass(toLower(searchClass->baseClass));
                    }

                    std::string mangledName = mangleMethod(implClassName, slot.methodName);
                    implTable.implMethods.push_back(mangledName);
                }

                interfaceImplTables_[tableKey] = std::move(implTable);
            }
        }
    }
}

void Lowerer::emitInterfaceTableRegistration(const std::string &className,
                                             const std::string &ifaceName)
{
    std::string classKey = toLower(className);
    std::string ifaceKey = toLower(ifaceName);
    std::string tableKey = classKey + "." + ifaceKey;

    auto implIt = interfaceImplTables_.find(tableKey);
    if (implIt == interfaceImplTables_.end())
        return;

    const InterfaceImplTable &implTable = implIt->second;

    auto layoutIt = interfaceLayouts_.find(ifaceKey);
    if (layoutIt == interfaceLayouts_.end())
        return;

    const InterfaceLayout &ifaceLayout = layoutIt->second;

    auto classLayoutIt = classLayouts_.find(classKey);
    if (classLayoutIt == classLayouts_.end())
        return;

    // Allocate interface method table
    std::size_t tableSize = ifaceLayout.slotCount * 8;
    if (tableSize == 0)
        tableSize = 8; // Minimum allocation

    usedExterns_.insert("rt_alloc");
    Value itablePtr = emitCallRet(
        Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(static_cast<long long>(tableSize))});

    // Populate interface method table slots
    for (std::size_t i = 0; i < implTable.implMethods.size(); ++i)
    {
        Value slotPtr = emitGep(itablePtr, Value::constInt(static_cast<long long>(i * 8)));
        Value funcPtr = Value::global(implTable.implMethods[i]);
        emitStore(Type(Type::Kind::Ptr), slotPtr, funcPtr);
    }

    // Register with runtime: rt_register_interface_impl(classId, interfaceId, itable)
    usedExterns_.insert("rt_register_interface_impl");
    emitCall("rt_register_interface_impl",
             {Value::constInt(classLayoutIt->second.classId),
              Value::constInt(ifaceLayout.interfaceId),
              itablePtr});
}

//===----------------------------------------------------------------------===//
// Interface Method Call Lowering
//===----------------------------------------------------------------------===//

LowerResult Lowerer::lowerInterfaceMethodCall(const FieldExpr &fieldExpr, const CallExpr &callExpr)
{
    // Get interface name from callExpr (set by semantic analyzer)
    std::string ifaceName = callExpr.interfaceName;
    std::string methodName = fieldExpr.field;

    // Get interface layout
    const InterfaceLayout *ifaceLayout = getInterfaceLayout(ifaceName);
    if (!ifaceLayout)
    {
        // Fallback: try to get from expression type
        PasType ifaceType = typeOfExpr(*fieldExpr.base);
        if (ifaceType.kind == PasTypeKind::Interface)
        {
            ifaceLayout = getInterfaceLayout(ifaceType.name);
            ifaceName = ifaceType.name;
        }
    }

    if (!ifaceLayout)
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Get method slot
    int slot = getInterfaceSlot(ifaceName, methodName);
    if (slot < 0)
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    // Get method return type from interface info
    const InterfaceInfo *ifaceInfo = sema_->lookupInterface(toLower(ifaceName));
    if (!ifaceInfo)
    {
        return {Value::constInt(0), Type(Type::Kind::I64)};
    }

    const MethodInfo *methodInfo = ifaceInfo->findMethod(toLower(methodName));
    Type retTy = Type(Type::Kind::Void);
    if (methodInfo)
    {
        retTy = mapType(methodInfo->returnType);
    }

    // Get the interface variable slot address (not the loaded value)
    // We need the address of the fat pointer { objPtr, itablePtr }
    Value ifaceSlot;
    if (fieldExpr.base->kind == ExprKind::Name)
    {
        const auto &nameExpr = static_cast<const NameExpr &>(*fieldExpr.base);
        std::string key = toLower(nameExpr.name);
        auto localIt = locals_.find(key);
        if (localIt != locals_.end())
        {
            ifaceSlot = localIt->second;
        }
        else
        {
            // Fallback: try lowering and hope it's a pointer
            LowerResult base = lowerExpr(*fieldExpr.base);
            ifaceSlot = base.value;
        }
    }
    else
    {
        // For more complex expressions, we'd need to handle differently
        LowerResult base = lowerExpr(*fieldExpr.base);
        ifaceSlot = base.value;
    }

    // Interface call dispatch:
    // Interface variable is a fat pointer: { objPtr (offset 0), itablePtr (offset 8) }

    // Step 1: Load the object pointer from the interface variable (offset 0)
    Value objPtr = emitLoad(Type(Type::Kind::Ptr), ifaceSlot);

    // Step 2: Load the interface table pointer (offset 8)
    Value itablePtrAddr = emitGep(ifaceSlot, Value::constInt(8));
    Value itablePtr = emitLoad(Type(Type::Kind::Ptr), itablePtrAddr);

    // Step 3: Load method pointer from itable
    Value methodSlotPtr = emitGep(itablePtr, Value::constInt(static_cast<long long>(slot * 8)));
    Value methodPtr = emitLoad(Type(Type::Kind::Ptr), methodSlotPtr);

    // Step 4: Build argument list (object pointer as Self, then user args)
    std::vector<Value> args;
    args.push_back(objPtr); // Self parameter

    for (const auto &arg : callExpr.args)
    {
        LowerResult argResult = lowerExpr(*arg);
        args.push_back(argResult.value);
    }

    // Step 5: Call through function pointer
    if (retTy.kind == Type::Kind::Void)
    {
        emitCallIndirect(methodPtr, args);
        return {Value::constInt(0), Type(Type::Kind::Void)};
    }
    else
    {
        Value result = emitCallIndirectRet(retTy, methodPtr, args);
        return {result, retTy};
    }
}

int Lowerer::getInterfaceSlot(const std::string &ifaceName, const std::string &methodName) const
{
    auto it = interfaceLayouts_.find(toLower(ifaceName));
    if (it == interfaceLayouts_.end())
        return -1;

    std::string methodKey = toLower(methodName);
    for (const auto &slot : it->second.slots)
    {
        if (toLower(slot.methodName) == methodKey)
            return slot.slot;
    }
    return -1;
}

const InterfaceLayout *Lowerer::getInterfaceLayout(const std::string &ifaceName) const
{
    auto it = interfaceLayouts_.find(toLower(ifaceName));
    if (it == interfaceLayouts_.end())
        return nullptr;
    return &it->second;
}

} // namespace il::frontends::pascal

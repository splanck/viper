//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/zia/Lowerer_Decl_Types.cpp
// Purpose: Type layout registration for the Zia IL lowerer — class/struct
//          type field offset computation, vtable construction, itable init,
//          and on-demand generic type instantiation.
// Key invariants:
//   - registerClassLayout/registerStructLayout are idempotent (skip if cached)
//   - Entity field offsets start at kClassFieldsOffset (after header)
//   - Struct type field offsets start at 0
// Ownership/Lifetime:
//   - Lowerer owns classTypes_/structTypes_ maps; pointers are stable across calls
// Links: src/frontends/zia/Lowerer.hpp, src/frontends/zia/Lowerer_Decl.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"

#include <functional>
#include <unordered_set>

namespace il::frontends::zia {

using namespace runtime;

//=============================================================================
// Type Layout Pre-Registration (BUG-FE-006 fix)
//=============================================================================

void Lowerer::registerAllTypeLayouts(std::vector<DeclPtr> &declarations) {
    for (auto &decl : declarations) {
        if (decl->kind == DeclKind::Class) {
            registerClassLayout(*static_cast<ClassDecl *>(decl.get()));
        } else if (decl->kind == DeclKind::Struct) {
            registerStructLayout(*static_cast<StructDecl *>(decl.get()));
        } else if (decl->kind == DeclKind::Namespace) {
            auto *ns = static_cast<NamespaceDecl *>(decl.get());
            std::string savedPrefix = namespacePrefix_;
            if (namespacePrefix_.empty())
                namespacePrefix_ = ns->name;
            else
                namespacePrefix_ = namespacePrefix_ + "." + ns->name;

            registerAllTypeLayouts(ns->declarations);

            namespacePrefix_ = savedPrefix;
        }
    }
}

void Lowerer::registerClassLayout(ClassDecl &decl) {
    // Skip uninstantiated generic types
    if (!decl.genericParams.empty())
        return;

    std::string qualifiedName = declarationName(decl, decl.name);

    // Skip if already registered
    if (classTypes_.find(qualifiedName) != classTypes_.end())
        return;

    ClassTypeInfo info;
    info.name = qualifiedName;
    if (!decl.baseClass.empty()) {
        TypeRef baseType = sema_.resolveNamedType(decl.baseClass, decl.loc);
        info.baseClass = baseType ? baseType->name : decl.baseClass;
    }
    info.totalSize = kClassFieldsOffset;
    info.classId = nextClassId_++;
    info.vtableName = "__vtable_" + qualifiedName;

    for (const auto &iface : decl.interfaces) {
        TypeRef ifaceType = sema_.resolveNamedType(iface, decl.loc);
        info.implementedInterfaces.insert(ifaceType ? ifaceType->name : iface);
    }

    // Copy inherited fields from parent class
    if (!decl.baseClass.empty()) {
        auto parentIt = classTypes_.find(decl.baseClass);
        if (parentIt != classTypes_.end()) {
            inheritClassMembers(info, parentIt->second);
        }
    }

    // Compute field layout and build vtable from this class's own members
    computeClassFieldLayout(decl, info, qualifiedName);
    buildClassVtable(decl, info, qualifiedName);

    classTypes_[qualifiedName] = std::move(info);
}

void Lowerer::computeClassFieldLayout(ClassDecl &decl,
                                      ClassTypeInfo &info,
                                      const std::string &qualifiedName) {
    (void)qualifiedName; // used implicitly via caller context
    for (auto &member : decl.members) {
        if (member->kind != DeclKind::Field)
            continue;

        auto *field = static_cast<FieldDecl *>(member.get());

        // Static fields become module-level globals, not instance fields
        if (field->isStatic)
            continue;

        TypeRef fieldType = sema_.getFieldType(qualifiedName, field->name);
        if (!fieldType && qualifiedName != decl.name)
            fieldType = sema_.getFieldType(decl.name, field->name);
        if (!fieldType)
            fieldType = field->type ? sema_.resolveType(field->type.get()) : types::unknown();

        // Compute size and alignment using semantic inline layout so nested
        // structs, tuples, and fixed-size arrays occupy their full storage.
        size_t fieldLayoutSize =
            field->isWeak ? getILTypeSize(Type(Type::Kind::Ptr)) : getSemanticTypeSize(fieldType);
        size_t fieldLayoutAlignment = field->isWeak ? getILTypeAlignment(Type(Type::Kind::Ptr))
                                                    : getSemanticTypeAlignment(fieldType);

        FieldLayout layout;
        layout.name = field->name;
        layout.type = fieldType;
        layout.isWeak = field->isWeak;
        layout.offset = alignTo(info.totalSize, fieldLayoutAlignment);
        layout.size = fieldLayoutSize;

        info.fieldIndex[field->name] = info.fields.size();
        info.fields.push_back(layout);
        info.totalSize = layout.offset + layout.size;
    }
}

void Lowerer::buildClassVtable(ClassDecl &decl,
                               ClassTypeInfo &info,
                               const std::string &qualifiedName) {
    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Method) {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);

            // Build vtable (static methods don't go in vtable)
            if (!method->isStatic) {
                std::string slotKey = sema_.methodSlotKey(qualifiedName, method);
                std::string methodQualName = sema_.loweredMethodName(qualifiedName, method);
                if (methodQualName.empty())
                    methodQualName = qualifiedName + "." + method->name;
                auto vtableIt = info.vtableIndex.find(slotKey);
                if (vtableIt != info.vtableIndex.end()) {
                    info.vtable[vtableIt->second] = methodQualName;
                } else {
                    info.vtableIndex[slotKey] = info.vtable.size();
                    info.vtable.push_back(methodQualName);
                }
            }
        } else if (member->kind == DeclKind::Property) {
            // Properties are synthesized into get_X/set_X methods during lowering
            auto *prop = static_cast<PropertyDecl *>(member.get());

            // Register getter as a method
            std::string getterName = "get_" + prop->name;
            info.propertyGetters.insert(getterName);

            // Register setter if present
            if (prop->setterBody) {
                std::string setterName = "set_" + prop->name;
                info.propertySetters.insert(setterName);
            }
        }
    }
}

void Lowerer::inheritClassMembers(ClassTypeInfo &info, const ClassTypeInfo &parent) {
    for (const auto &parentField : parent.fields) {
        info.fieldIndex[parentField.name] = info.fields.size();
        info.fields.push_back(parentField);
    }
    info.totalSize = parent.totalSize;
    info.vtable = parent.vtable;
    info.vtableIndex = parent.vtableIndex;
}

void Lowerer::registerStructLayout(StructDecl &decl) {
    // Skip uninstantiated generic types
    if (!decl.genericParams.empty())
        return;

    std::string qualifiedName = declarationName(decl, decl.name);

    // Skip if already registered
    if (structTypes_.find(qualifiedName) != structTypes_.end())
        return;

    StructTypeInfo info;
    info.name = qualifiedName;
    info.totalSize = 0;
    info.classId = nextClassId_++;
    for (const auto &iface : decl.interfaces) {
        TypeRef ifaceType = sema_.resolveNamedType(iface, decl.loc);
        info.implementedInterfaces.insert(ifaceType ? ifaceType->name : iface);
    }

    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Field) {
            auto *field = static_cast<FieldDecl *>(member.get());
            TypeRef fieldType = sema_.getFieldType(qualifiedName, field->name);
            if (!fieldType && qualifiedName != decl.name)
                fieldType = sema_.getFieldType(decl.name, field->name);
            if (!fieldType)
                fieldType = field->type ? sema_.resolveType(field->type.get()) : types::unknown();

            // Compute size and alignment using semantic inline layout so nested
            // structs, tuples, and fixed-size arrays occupy their full storage.
            size_t fieldLayoutSize = field->isWeak ? getILTypeSize(Type(Type::Kind::Ptr))
                                                   : getSemanticTypeSize(fieldType);
            size_t fieldLayoutAlignment = field->isWeak ? getILTypeAlignment(Type(Type::Kind::Ptr))
                                                        : getSemanticTypeAlignment(fieldType);

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.isWeak = field->isWeak;
            layout.offset = alignTo(info.totalSize, fieldLayoutAlignment);
            layout.size = fieldLayoutSize;

            info.fieldIndex[field->name] = info.fields.size();
            info.fields.push_back(layout);
            info.totalSize = layout.offset + layout.size;
        } else if (member->kind == DeclKind::Method) {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);
        }
    }

    structTypes_[qualifiedName] = std::move(info);
}

void Lowerer::emitVtable(const ClassTypeInfo & /*info*/) {
    // BUG-VL-011: Virtual dispatch is now handled via class_id-based dispatch
    // instead of vtable pointers. The vtable info is used at compile time
    // to generate dispatch code, not runtime vtable lookup.
    // This function is kept as a placeholder for future vtable-based dispatch.
}

//=============================================================================
// Interface Registration and ITable Binding
//=============================================================================

void Lowerer::emitItableInit() {
    // Skip if no interfaces are defined (no call was emitted in start())
    if (interfaceTypes_.empty())
        return;

    // Save current function context
    Function *savedFunc = currentFunc_;
    auto savedLocals = std::move(locals_);
    auto savedSlots = std::move(slots_);
    auto savedLocalTypes = std::move(localTypes_);

    auto adapterKey = [](const std::string &structName,
                         const std::string &ifaceName,
                         const std::string &methodName) {
        return structName + "|" + ifaceName + "|" + methodName;
    };

    std::unordered_map<std::string, std::string> structIfaceAdapters;

    auto defaultInterfaceMethodName = [&](const InterfaceTypeInfo &ifaceInfo,
                                          MethodDecl *ifaceMethod) -> std::string {
        if (!ifaceMethod || !ifaceMethod->body)
            return "";
        std::string lowered = sema_.loweredMethodName(ifaceInfo.name, ifaceMethod);
        if (!lowered.empty())
            return lowered;
        return ifaceInfo.name + "." + ifaceMethod->name;
    };

    auto emitStructInterfaceAdapter = [&](const StructTypeInfo &structInfo,
                                          const InterfaceTypeInfo &ifaceInfo,
                                          MethodDecl *ifaceMethod,
                                          MethodDecl *implMethod) -> std::string {
        std::string adapterName =
            structInfo.name + ".__iface_" + ifaceInfo.name + "." + ifaceMethod->name;
        if (definedFunctions_.count(adapterName))
            return adapterName;

        TypeRef methodType = sema_.getMethodType(ifaceInfo.name, ifaceMethod);
        if (!methodType)
            methodType = sema_.getMethodType(structInfo.name, implMethod);

        std::vector<TypeRef> paramTypes;
        TypeRef returnType = types::voidType();
        if (methodType && methodType->kind == TypeKindSem::Function) {
            paramTypes = methodType->paramTypes();
            returnType = methodType->returnType();
        }

        Type ilReturnType = mapType(returnType);
        std::vector<il::core::Param> params;
        params.push_back({"self", Type(Type::Kind::Ptr)});
        for (size_t i = 0; i < ifaceMethod->params.size(); ++i) {
            TypeRef paramType = i < paramTypes.size() ? paramTypes[i] : types::unknown();
            params.push_back({ifaceMethod->params[i].name, mapType(paramType)});
        }

        currentFunc_ = &builder_->startFunction(adapterName, ilReturnType, params);
        definedFunctions_.insert(adapterName);
        blockMgr_.bind(builder_.get(), currentFunc_);
        locals_.clear();
        slots_.clear();
        localTypes_.clear();
        deferredTemps_.clear();
        currentStructType_ = nullptr;
        currentClassType_ = nullptr;

        builder_->createBlock(*currentFunc_, "entry_0", currentFunc_->params);
        size_t entryIdx = currentFunc_->blocks.size() - 1;
        setBlock(entryIdx);

        const auto &bp = currentFunc_->blocks[entryIdx].params;
        Value wrapperSelf = Value::temp(bp[0].id);
        Value structSelf = emitGEP(wrapperSelf, static_cast<int64_t>(kClassFieldsOffset));

        std::vector<Value> args;
        args.reserve(bp.size());
        args.push_back(structSelf);
        for (size_t i = 1; i < bp.size(); ++i)
            args.push_back(Value::temp(bp[i].id));

        std::string implName = sema_.loweredMethodName(structInfo.name, implMethod);
        if (implName.empty())
            implName = structInfo.name + "." + implMethod->name;

        if (ilReturnType.kind == Type::Kind::Void) {
            emitCall(implName, args);
            releaseDeferredTemps();
            emitRetVoid();
        } else {
            Value result = emitCallRet(ilReturnType, implName, args);
            consumeDeferred(result);
            releaseDeferredTemps();
            emitRet(result);
        }

        currentFunc_ = nullptr;
        currentReturnType_ = nullptr;
        deferredTemps_.clear();
        return adapterName;
    };

    for (const auto &[structName, structInfo] : structTypes_) {
        for (const auto &ifaceName : structInfo.implementedInterfaces) {
            auto ifaceIt = interfaceTypes_.find(ifaceName);
            if (ifaceIt == interfaceTypes_.end())
                continue;
            const InterfaceTypeInfo &ifaceInfo = ifaceIt->second;
            for (auto *ifaceMethod : ifaceInfo.methods) {
                MethodDecl *implMethod = structInfo.findMethod(ifaceMethod->name);
                if (!implMethod)
                    continue;
                std::string key = adapterKey(structName, ifaceName, ifaceMethod->name);
                structIfaceAdapters[key] =
                    emitStructInterfaceAdapter(structInfo, ifaceInfo, ifaceMethod, implMethod);
            }
        }
    }

    // Create __zia_iface_init() function
    auto &fn = builder_->startFunction("__zia_iface_init", Type(Type::Kind::Void), {});
    currentFunc_ = &fn;
    definedFunctions_.insert("__zia_iface_init");
    blockMgr_.bind(builder_.get(), &fn);
    locals_.clear();
    slots_.clear();
    localTypes_.clear();

    // Create entry block
    builder_->createBlock(fn, "entry_0", {});
    setBlock(fn.blocks.size() - 1);

    // Phase 1: Register each class before interface bindings reference it.
    std::vector<std::string> classOrder;
    std::unordered_set<std::string> visitedClasses;
    std::function<void(const std::string &)> visitClass = [&](const std::string &className) {
        if (visitedClasses.count(className))
            return;
        auto classIt = classTypes_.find(className);
        if (classIt == classTypes_.end())
            return;
        if (!classIt->second.baseClass.empty())
            visitClass(classIt->second.baseClass);
        visitedClasses.insert(className);
        classOrder.push_back(className);
    };
    for (const auto &entry : classTypes_)
        visitClass(entry.first);

    for (const auto &className : classOrder) {
        auto classIt = classTypes_.find(className);
        if (classIt == classTypes_.end())
            continue;
        const ClassTypeInfo &classInfo = classIt->second;

        const size_t slotCount = classInfo.vtable.size();
        const int64_t bytes = slotCount > 0 ? static_cast<int64_t>(slotCount * 8ULL) : 8LL;
        Value vtablePtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(bytes)});

        for (size_t s = 0; s < slotCount; ++s) {
            int64_t offset = static_cast<int64_t>(s * 8ULL);
            Value slotPtr =
                emitBinary(Opcode::GEP, Type(Type::Kind::Ptr), vtablePtr, Value::constInt(offset));
            const std::string &methodName = classInfo.vtable[s];
            if (methodName.empty()) {
                emitStore(slotPtr, Value::null(), Type(Type::Kind::Ptr));
            } else {
                emitStore(slotPtr, Value::global(methodName), Type(Type::Kind::Ptr));
            }
        }

        int64_t baseClassId = -1;
        if (!classInfo.baseClass.empty()) {
            auto baseIt = classTypes_.find(classInfo.baseClass);
            if (baseIt != classTypes_.end())
                baseClassId = static_cast<int64_t>(baseIt->second.classId);
        }

        Value qnameStr = emitConstStr(stringTable_.intern(classInfo.name));
        emitCall("rt_register_class_with_base_rs",
                 {Value::constInt(static_cast<int64_t>(classInfo.classId)),
                  vtablePtr,
                  qnameStr,
                  Value::constInt(static_cast<int64_t>(slotCount)),
                  Value::constInt(baseClassId)});
    }

    for (const auto &[structName, structInfo] : structTypes_) {
        if (structInfo.implementedInterfaces.empty())
            continue;

        Value vtablePtr = emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(8)});
        Value qnameStr = emitConstStr(stringTable_.intern(structName));
        emitCall("rt_register_class_with_base_rs",
                 {Value::constInt(static_cast<int64_t>(structInfo.classId)),
                  vtablePtr,
                  qnameStr,
                  Value::constInt(0),
                  Value::constInt(-1)});
    }

    // Phase 2: Register each interface
    for (const auto &[ifaceName, ifaceInfo] : interfaceTypes_) {
        // rt_register_interface_direct_rs(ifaceId, qname, slotCount)
        Value qnameStr = emitConstStr(stringTable_.intern(ifaceName));
        emitCall("rt_register_interface_direct_rs",
                 {Value::constInt(static_cast<int64_t>(ifaceInfo.ifaceId)),
                  qnameStr,
                  Value::constInt(static_cast<int64_t>(ifaceInfo.methods.size()))});
    }

    // Phase 3: For each class implementing an interface, build and bind itable
    for (const auto &[entityName, entityInfo] : classTypes_) {
        for (const auto &ifaceName : entityInfo.implementedInterfaces) {
            auto ifaceIt = interfaceTypes_.find(ifaceName);
            if (ifaceIt == interfaceTypes_.end())
                continue;
            const InterfaceTypeInfo &ifaceInfo = ifaceIt->second;
            if (ifaceInfo.methods.empty())
                continue;

            // Allocate itable: slotCount * 8 bytes
            size_t slotCount = ifaceInfo.methods.size();
            int64_t bytes = static_cast<int64_t>(slotCount * 8ULL);
            Value itablePtr =
                emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(bytes)});

            // Populate each slot with a function pointer
            for (size_t s = 0; s < slotCount; ++s) {
                const std::string &methodName = ifaceInfo.methods[s]->name;
                const std::string slotKey =
                    sema_.methodSlotKey(ifaceInfo.name, ifaceInfo.methods[s]);
                int64_t offset = static_cast<int64_t>(s * 8ULL);
                Value slotPtr = emitBinary(
                    Opcode::GEP, Type(Type::Kind::Ptr), itablePtr, Value::constInt(offset));

                // Find the implementing method in the class (or its bases)
                std::string implName;
                std::string searchEntity = entityName;
                while (!searchEntity.empty()) {
                    auto entIt = classTypes_.find(searchEntity);
                    if (entIt == classTypes_.end())
                        break;
                    auto vtIt = entIt->second.vtableIndex.find(slotKey);
                    if (vtIt != entIt->second.vtableIndex.end()) {
                        implName = entIt->second.vtable[vtIt->second];
                        break;
                    }
                    searchEntity = entIt->second.baseClass;
                }

                if (implName.empty()) {
                    implName = defaultInterfaceMethodName(ifaceInfo, ifaceInfo.methods[s]);
                }

                if (implName.empty()) {
                    emitStore(slotPtr, Value::null(), Type(Type::Kind::Ptr));
                } else {
                    emitStore(slotPtr, Value::global(implName), Type(Type::Kind::Ptr));
                }
            }

            // Bind the itable: rt_bind_interface(typeId, ifaceId, itable)
            emitCall("rt_bind_interface",
                     {Value::constInt(static_cast<int64_t>(entityInfo.classId)),
                      Value::constInt(static_cast<int64_t>(ifaceInfo.ifaceId)),
                      itablePtr});
        }
    }

    // Phase 4: Struct interface bindings use heap wrappers plus small adapters
    // that translate wrapper self pointers back to the inline struct payload.
    for (const auto &[structName, structInfo] : structTypes_) {
        for (const auto &ifaceName : structInfo.implementedInterfaces) {
            auto ifaceIt = interfaceTypes_.find(ifaceName);
            if (ifaceIt == interfaceTypes_.end())
                continue;
            const InterfaceTypeInfo &ifaceInfo = ifaceIt->second;
            if (ifaceInfo.methods.empty())
                continue;

            size_t slotCount = ifaceInfo.methods.size();
            int64_t bytes = static_cast<int64_t>(slotCount * 8ULL);
            Value itablePtr =
                emitCallRet(Type(Type::Kind::Ptr), "rt_alloc", {Value::constInt(bytes)});

            for (size_t s = 0; s < slotCount; ++s) {
                MethodDecl *ifaceMethod = ifaceInfo.methods[s];
                int64_t offset = static_cast<int64_t>(s * 8ULL);
                Value slotPtr = emitBinary(
                    Opcode::GEP, Type(Type::Kind::Ptr), itablePtr, Value::constInt(offset));

                std::string key = adapterKey(structName, ifaceName, ifaceMethod->name);
                auto adapterIt = structIfaceAdapters.find(key);
                if (adapterIt == structIfaceAdapters.end()) {
                    std::string defaultImpl = defaultInterfaceMethodName(ifaceInfo, ifaceMethod);
                    if (defaultImpl.empty())
                        emitStore(slotPtr, Value::null(), Type(Type::Kind::Ptr));
                    else
                        emitStore(slotPtr, Value::global(defaultImpl), Type(Type::Kind::Ptr));
                } else {
                    emitStore(slotPtr, Value::global(adapterIt->second), Type(Type::Kind::Ptr));
                }
            }

            emitCall("rt_bind_interface",
                     {Value::constInt(static_cast<int64_t>(structInfo.classId)),
                      Value::constInt(static_cast<int64_t>(ifaceInfo.ifaceId)),
                      itablePtr});
        }
    }

    emitRetVoid();

    // Restore previous function context
    currentFunc_ = savedFunc;
    locals_ = std::move(savedLocals);
    slots_ = std::move(savedSlots);
    localTypes_ = std::move(savedLocalTypes);
}

//=============================================================================
// On-Demand Generic Type Instantiation
//=============================================================================

const StructTypeInfo *Lowerer::getOrCreateStructTypeInfo(const std::string &typeName) {
    // Check existing cache
    auto it = structTypes_.find(typeName);
    if (it != structTypes_.end()) {
        return &it->second;
    }

    if (typeName.find('.') == std::string::npos) {
        const StructTypeInfo *matched = nullptr;
        const std::string suffix = "." + typeName;
        for (const auto &[registeredName, info] : structTypes_) {
            if (registeredName.size() < suffix.size() ||
                registeredName.compare(
                    registeredName.size() - suffix.size(), suffix.size(), suffix) != 0)
                continue;
            if (matched)
                return nullptr;
            matched = &info;
        }
        if (matched)
            return matched;
    }

    // Check if this is an instantiated generic
    if (!sema_.isInstantiatedGeneric(typeName)) {
        return nullptr;
    }

    // Get the original generic declaration
    Decl *genericDecl = sema_.getGenericDeclForInstantiation(typeName);
    if (!genericDecl || genericDecl->kind != DeclKind::Struct) {
        return nullptr;
    }

    auto *structDecl = static_cast<StructDecl *>(genericDecl);

    // Build StructTypeInfo for the instantiated type
    StructTypeInfo info;
    info.name = typeName;
    info.totalSize = 0;
    info.classId = nextClassId_++;
    for (const auto &iface : structDecl->interfaces) {
        info.implementedInterfaces.insert(iface);
    }

    for (auto &member : structDecl->members) {
        if (member->kind == DeclKind::Field) {
            auto *field = static_cast<FieldDecl *>(member.get());
            // Get the substituted field type from Sema
            TypeRef fieldType = sema_.getFieldType(typeName, field->name);
            if (!fieldType)
                fieldType = types::unknown();

            // Compute size and alignment using semantic inline layout so nested
            // structs, tuples, and fixed-size arrays occupy their full storage.
            size_t fieldLayoutSize = field->isWeak ? getILTypeSize(Type(Type::Kind::Ptr))
                                                   : getSemanticTypeSize(fieldType);
            size_t fieldLayoutAlignment = field->isWeak ? getILTypeAlignment(Type(Type::Kind::Ptr))
                                                        : getSemanticTypeAlignment(fieldType);

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.isWeak = field->isWeak;
            layout.offset = alignTo(info.totalSize, fieldLayoutAlignment);
            layout.size = fieldLayoutSize;

            info.fieldIndex[field->name] = info.fields.size();
            info.fields.push_back(layout);
            info.totalSize = layout.offset + layout.size;
        } else if (member->kind == DeclKind::Method) {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);
        }
    }

    // Store the struct type info
    structTypes_[typeName] = std::move(info);

    // Defer method lowering until after all declarations are processed
    // (we may be in the middle of lowering another function body)
    pendingStructInstantiations_.push_back(typeName);

    return &structTypes_[typeName];
}

const ClassTypeInfo *Lowerer::getOrCreateClassTypeInfo(const std::string &typeName) {
    // Check existing cache
    auto it = classTypes_.find(typeName);
    if (it != classTypes_.end()) {
        return &it->second;
    }

    if (typeName.find('.') == std::string::npos) {
        const ClassTypeInfo *matched = nullptr;
        const std::string suffix = "." + typeName;
        for (const auto &[registeredName, info] : classTypes_) {
            if (registeredName.size() < suffix.size() ||
                registeredName.compare(
                    registeredName.size() - suffix.size(), suffix.size(), suffix) != 0)
                continue;
            if (matched)
                return nullptr;
            matched = &info;
        }
        if (matched)
            return matched;
    }

    // Check if this is an instantiated generic
    if (!sema_.isInstantiatedGeneric(typeName)) {
        return nullptr;
    }

    // Get the original generic declaration
    Decl *genericDecl = sema_.getGenericDeclForInstantiation(typeName);
    if (!genericDecl || genericDecl->kind != DeclKind::Class) {
        return nullptr;
    }

    auto *classDecl = static_cast<ClassDecl *>(genericDecl);

    // Build ClassTypeInfo for the instantiated type
    ClassTypeInfo info;
    info.name = typeName;
    info.baseClass = classDecl->baseClass;
    info.totalSize = kClassFieldsOffset; // Space for header + vtable ptr
    info.classId = nextClassId_++;
    info.vtableName = "__vtable_" + typeName;

    // Store implemented interfaces
    for (const auto &iface : classDecl->interfaces) {
        info.implementedInterfaces.insert(iface);
    }

    // Handle inheritance (if base class exists, copy its fields)
    if (!classDecl->baseClass.empty()) {
        auto parentIt = classTypes_.find(classDecl->baseClass);
        if (parentIt != classTypes_.end()) {
            inheritClassMembers(info, parentIt->second);
        }
    }

    // Process members
    for (auto &member : classDecl->members) {
        if (member->kind == DeclKind::Field) {
            auto *field = static_cast<FieldDecl *>(member.get());
            // Get the substituted field type from Sema
            TypeRef fieldType = sema_.getFieldType(typeName, field->name);
            if (!fieldType)
                fieldType = types::unknown();

            // Compute size and alignment using semantic inline layout so nested
            // structs, tuples, and fixed-size arrays occupy their full storage.
            size_t fieldLayoutSize = field->isWeak ? getILTypeSize(Type(Type::Kind::Ptr))
                                                   : getSemanticTypeSize(fieldType);
            size_t fieldLayoutAlignment = field->isWeak ? getILTypeAlignment(Type(Type::Kind::Ptr))
                                                        : getSemanticTypeAlignment(fieldType);

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
            layout.isWeak = field->isWeak;
            layout.offset = alignTo(info.totalSize, fieldLayoutAlignment);
            layout.size = fieldLayoutSize;

            info.fieldIndex[field->name] = info.fields.size();
            info.fields.push_back(layout);
            info.totalSize = layout.offset + layout.size;
        } else if (member->kind == DeclKind::Method) {
            auto *method = static_cast<MethodDecl *>(member.get());
            info.methodMap[method->name] = method;
            info.methods.push_back(method);

            // Build vtable
            std::string slotKey = sema_.methodSlotKey(typeName, method);
            std::string methodQualName = sema_.loweredMethodName(typeName, method);
            if (methodQualName.empty())
                methodQualName = typeName + "." + method->name;
            auto vtableIt = info.vtableIndex.find(slotKey);
            if (vtableIt != info.vtableIndex.end()) {
                info.vtable[vtableIt->second] = methodQualName;
            } else {
                info.vtableIndex[slotKey] = info.vtable.size();
                info.vtable.push_back(methodQualName);
            }
        }
    }

    // Store the class type info
    classTypes_[typeName] = std::move(info);

    // Defer method lowering until after all declarations are processed
    // (we may be in the middle of lowering another function body)
    pendingClassInstantiations_.push_back(typeName);

    return &classTypes_[typeName];
}

} // namespace il::frontends::zia

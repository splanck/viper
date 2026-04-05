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

    std::string qualifiedName = qualifyName(decl.name);

    // Skip if already registered
    if (classTypes_.find(qualifiedName) != classTypes_.end())
        return;

    ClassTypeInfo info;
    info.name = qualifiedName;
    info.baseClass = decl.baseClass;
    info.totalSize = kClassFieldsOffset;
    info.classId = nextClassId_++;
    info.vtableName = "__vtable_" + qualifiedName;

    for (const auto &iface : decl.interfaces) {
        info.implementedInterfaces.insert(iface);
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

        TypeRef fieldType = field->type ? sema_.resolveType(field->type.get()) : types::unknown();

        // Compute size and alignment; fixed-size arrays are stored inline.
        size_t fieldLayoutSize, fieldLayoutAlignment;
        if (fieldType && fieldType->kind == TypeKindSem::FixedArray) {
            TypeRef elemType = fieldType->elementType();
            Type ilElemType = elemType ? mapType(elemType) : Type(Type::Kind::I64);
            size_t elemSize = getILTypeSize(ilElemType);
            fieldLayoutSize = elemSize * fieldType->elementCount;
            fieldLayoutAlignment = elemSize;
        } else {
            Type ilFieldType = mapType(fieldType);
            fieldLayoutSize = getILTypeSize(ilFieldType);
            fieldLayoutAlignment = getILTypeAlignment(ilFieldType);
        }

        FieldLayout layout;
        layout.name = field->name;
        layout.type = fieldType;
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

    std::string qualifiedName = qualifyName(decl.name);

    // Skip if already registered
    if (structTypes_.find(qualifiedName) != structTypes_.end())
        return;

    StructTypeInfo info;
    info.name = qualifiedName;
    info.totalSize = 0;

    for (auto &member : decl.members) {
        if (member->kind == DeclKind::Field) {
            auto *field = static_cast<FieldDecl *>(member.get());
            TypeRef fieldType =
                field->type ? sema_.resolveType(field->type.get()) : types::unknown();

            // Compute size and alignment; fixed-size arrays are stored inline.
            size_t fieldLayoutSize, fieldLayoutAlignment;
            if (fieldType && fieldType->kind == TypeKindSem::FixedArray) {
                TypeRef elemType = fieldType->elementType();
                Type ilElemType = elemType ? mapType(elemType) : Type(Type::Kind::I64);
                size_t elemSize = getILTypeSize(ilElemType);
                fieldLayoutSize = elemSize * fieldType->elementCount;
                fieldLayoutAlignment = elemSize;
            } else {
                Type ilFieldType = mapType(fieldType);
                fieldLayoutSize = getILTypeSize(ilFieldType);
                fieldLayoutAlignment = getILTypeAlignment(ilFieldType);
            }

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
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

    // Phase 1: Register each interface
    for (const auto &[ifaceName, ifaceInfo] : interfaceTypes_) {
        // rt_register_interface_direct(ifaceId, qname, slotCount)
        Value qnameStr = emitConstStr(stringTable_.intern(ifaceName));
        emitCall("rt_register_interface_direct",
                 {Value::constInt(static_cast<int64_t>(ifaceInfo.ifaceId)),
                  qnameStr,
                  Value::constInt(static_cast<int64_t>(ifaceInfo.methods.size()))});
    }

    // Phase 2: For each class implementing an interface, build and bind itable
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
                    // No implementation found -- store null
                    emitStore(slotPtr, Value::null(), Type(Type::Kind::Ptr));
                } else {
                    // Store function pointer
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

    for (auto &member : structDecl->members) {
        if (member->kind == DeclKind::Field) {
            auto *field = static_cast<FieldDecl *>(member.get());
            // Get the substituted field type from Sema
            TypeRef fieldType = sema_.getFieldType(typeName, field->name);
            if (!fieldType)
                fieldType = types::unknown();

            // Compute size and alignment; fixed-size arrays are stored inline.
            size_t fieldLayoutSize, fieldLayoutAlignment;
            if (fieldType && fieldType->kind == TypeKindSem::FixedArray) {
                TypeRef elemType = fieldType->elementType();
                Type ilElemType = elemType ? mapType(elemType) : Type(Type::Kind::I64);
                size_t elemSize = getILTypeSize(ilElemType);
                fieldLayoutSize = elemSize * fieldType->elementCount;
                fieldLayoutAlignment = elemSize;
            } else {
                Type ilFieldType = mapType(fieldType);
                fieldLayoutSize = getILTypeSize(ilFieldType);
                fieldLayoutAlignment = getILTypeAlignment(ilFieldType);
            }

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
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

            // Compute size and alignment; fixed-size arrays are stored inline.
            size_t fieldLayoutSize, fieldLayoutAlignment;
            if (fieldType && fieldType->kind == TypeKindSem::FixedArray) {
                TypeRef elemType = fieldType->elementType();
                Type ilElemType = elemType ? mapType(elemType) : Type(Type::Kind::I64);
                size_t elemSize = getILTypeSize(ilElemType);
                fieldLayoutSize = elemSize * fieldType->elementCount;
                fieldLayoutAlignment = elemSize;
            } else {
                Type ilFieldType = mapType(fieldType);
                fieldLayoutSize = getILTypeSize(ilFieldType);
                fieldLayoutAlignment = getILTypeAlignment(ilFieldType);
            }

            FieldLayout layout;
            layout.name = field->name;
            layout.type = fieldType;
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

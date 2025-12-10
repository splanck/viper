//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/Lowerer.cpp
// Purpose: Main entry points for Pascal AST to IL lowering.
// Key invariants: Produces valid SSA with deterministic block naming.
// Ownership/Lifetime: Borrows AST; produces new Module.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Lowerer.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

namespace il::frontends::pascal
{

//===----------------------------------------------------------------------===//
// Construction
//===----------------------------------------------------------------------===//

Lowerer::Lowerer() = default;

//===----------------------------------------------------------------------===//
// Main Entry Points
//===----------------------------------------------------------------------===//

Lowerer::Module Lowerer::lower(Program &prog, SemanticAnalyzer &sema)
{
    // Initialize state
    module_ = std::make_unique<Module>();
    builder_ = std::make_unique<il::build::IRBuilder>(*module_);
    sema_ = &sema;
    locals_.clear();
    localTypes_.clear();
    constants_.clear();
    stringTable_.clear();
    stringTable_.setEmitter([this](const std::string &label, const std::string &content)
                            { builder_->addGlobalStr(label, content); });
    loopStack_.clear();
    usedExterns_.clear();
    blockCounter_ = 0;
    classLayouts_.clear();
    vtableLayouts_.clear();
    nextClassId_ = 1;
    classRegistrationOrder_.clear();

    // Clear interface state
    interfaceLayouts_.clear();
    interfaceImplTables_.clear();
    nextInterfaceId_ = 1;
    interfaceRegistrationOrder_.clear();

    // Scan interfaces and compute layouts
    scanInterfaces(prog.decls);

    // Scan classes and compute layouts/vtables
    scanClasses(prog.decls);

    // Compute interface implementation tables for each class
    for (const auto &className : classRegistrationOrder_)
    {
        computeInterfaceImplTables(className);
    }

    // Emit OOP module initialization if there are classes
    emitOopModuleInit();

    // Lower all function/procedure declarations
    for (const auto &decl : prog.decls)
    {
        if (!decl)
            continue;

        if (decl->kind == DeclKind::Function)
        {
            auto &funcDecl = static_cast<FunctionDecl &>(*decl);
            lowerFunctionDecl(funcDecl);
        }
        else if (decl->kind == DeclKind::Procedure)
        {
            auto &procDecl = static_cast<ProcedureDecl &>(*decl);
            lowerProcedureDecl(procDecl);
        }
        else if (decl->kind == DeclKind::Constructor)
        {
            auto &ctorDecl = static_cast<ConstructorDecl &>(*decl);
            lowerConstructorDecl(ctorDecl);
        }
        else if (decl->kind == DeclKind::Destructor)
        {
            auto &dtorDecl = static_cast<DestructorDecl &>(*decl);
            lowerDestructorDecl(dtorDecl);
        }
    }

    // Create @main function
    currentFunc_ = &builder_->startFunction("main", Type(Type::Kind::I64), {});

    locals_.clear();
    localTypes_.clear();
    currentFuncName_.clear();

    size_t entryIdx = createBlock("entry");
    setBlock(entryIdx);

    // Call OOP module init at start of main if there are classes
    if (!classRegistrationOrder_.empty())
    {
        emitCall("__pas_oop_init", {});
    }

    allocateLocals(prog.decls);

    if (prog.body)
    {
        lowerBlock(*prog.body);
    }

    emitRet(Value::constInt(0));

    // Add extern declarations
    for (const auto &externName : usedExterns_)
    {
        const auto *desc = il::runtime::findRuntimeDescriptor(externName);
        if (desc)
        {
            builder_->addExtern(
                std::string(desc->name), desc->signature.retType, desc->signature.paramTypes);
        }
    }

    return std::move(*module_);
}

Lowerer::Module Lowerer::lower(Unit &unit, SemanticAnalyzer &sema)
{
    // Initialize state
    module_ = std::make_unique<Module>();
    builder_ = std::make_unique<il::build::IRBuilder>(*module_);
    sema_ = &sema;
    locals_.clear();
    localTypes_.clear();
    constants_.clear();
    stringTable_.clear();
    stringTable_.setEmitter([this](const std::string &label, const std::string &content)
                            { builder_->addGlobalStr(label, content); });
    loopStack_.clear();
    usedExterns_.clear();
    blockCounter_ = 0;
    classLayouts_.clear();
    vtableLayouts_.clear();
    nextClassId_ = 1;
    classRegistrationOrder_.clear();

    // Scan classes and compute layouts/vtables
    scanClasses(unit.implDecls);

    // Emit OOP module initialization if there are classes
    emitOopModuleInit();

    // Lower all function/procedure declarations from implementation
    for (const auto &decl : unit.implDecls)
    {
        if (!decl)
            continue;

        if (decl->kind == DeclKind::Function)
        {
            auto &funcDecl = static_cast<FunctionDecl &>(*decl);
            lowerFunctionDecl(funcDecl);
        }
        else if (decl->kind == DeclKind::Procedure)
        {
            auto &procDecl = static_cast<ProcedureDecl &>(*decl);
            lowerProcedureDecl(procDecl);
        }
        else if (decl->kind == DeclKind::Constructor)
        {
            auto &ctorDecl = static_cast<ConstructorDecl &>(*decl);
            lowerConstructorDecl(ctorDecl);
        }
        else if (decl->kind == DeclKind::Destructor)
        {
            auto &dtorDecl = static_cast<DestructorDecl &>(*decl);
            lowerDestructorDecl(dtorDecl);
        }
    }

    // Lower initialization section
    if (unit.initSection)
    {
        std::string initName = unit.name + "_init";
        currentFunc_ = &builder_->startFunction(initName, Type(Type::Kind::Void), {});
        size_t entryIdx = createBlock("entry");
        setBlock(entryIdx);
        lowerBlock(*unit.initSection);
        emitRetVoid();
    }

    // Add extern declarations
    for (const auto &externName : usedExterns_)
    {
        const auto *desc = il::runtime::findRuntimeDescriptor(externName);
        if (desc)
        {
            builder_->addExtern(
                std::string(desc->name), desc->signature.retType, desc->signature.paramTypes);
        }
    }

    return std::move(*module_);
}

//===----------------------------------------------------------------------===//
// Module Merging
//===----------------------------------------------------------------------===//

void Lowerer::mergeModule(Module &target, Module &source)
{
    // Merge functions
    for (auto &func : source.functions)
    {
        target.functions.push_back(std::move(func));
    }

    // Merge externs (avoid duplicates)
    for (const auto &ext : source.externs)
    {
        bool found = false;
        for (const auto &existing : target.externs)
        {
            if (existing.name == ext.name)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            target.externs.push_back(ext);
        }
    }

    // Merge globals
    for (auto &glob : source.globals)
    {
        target.globals.push_back(std::move(glob));
    }
}

} // namespace il::frontends::pascal

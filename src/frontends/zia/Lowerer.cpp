//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Lowerer.cpp
/// @brief Implementation of Zia to IL code generation.
///
/// @details This file implements the Lowerer class which transforms a
/// type-checked Zia AST into Viper IL. Key implementation details:
///
/// ## Lowering Process
///
/// The lower() method:
/// 1. Initializes module and IR builder
/// 2. Lowers all declarations to IL functions/globals
/// 3. Emits string constants via stringTable_
/// 4. Declares external runtime functions
///
/// ## Control Flow
///
/// Control flow constructs are lowered to basic blocks:
/// - if: Emit condition, conditional branch to then/else blocks, merge
/// - while: Header block (condition), body block, back-edge to header
/// - for-in: Lower to while loop with iterator variable
/// - match: Chain of conditional branches for patterns
///
/// ## Type Layout
///
/// Value and entity types compute field layouts:
/// - ValueTypeInfo: Inline field layout with total size
/// - EntityTypeInfo: Fields after 16-byte object header, class ID for RTTI
/// - Field offsets computed during type registration
///
/// ## Runtime Integration
///
/// Runtime calls use RuntimeNames.hpp constants. The lowerer:
/// 1. Tracks used external functions in usedExterns_
/// 2. Emits extern declarations for all used runtime functions
/// 3. Uses RuntimeSignatures.hpp for function signatures
///
/// ## Boxing/Unboxing
///
/// For generic collections (List[T], Map[K,V]):
/// - emitBox(): Allocate heap space and store primitive value
/// - emitUnbox(): Load primitive value from boxed pointer
///
/// @see Lowerer.hpp for the class interface
/// @see RuntimeNames.hpp for runtime function name constants
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/Lowerer.hpp"
#include "frontends/zia/RuntimeNames.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include <cctype>

namespace il::frontends::zia
{

using namespace runtime;

/// @brief Construct a Lowerer with a reference to the semantic analyzer and compiler options.
/// @param sema The semantic analyzer providing type and symbol resolution.
/// @param options Compiler options controlling code generation behaviour.
Lowerer::Lowerer(Sema &sema, CompilerOptions options) : sema_(sema), options_(options) {}

/// @brief Lower a complete Zia module AST to IL.
/// @details Initializes the IL module, lowers all declarations, processes pending generic
///          instantiations, emits string constants, and declares used external functions.
/// @param module The analyzed module AST to lower.
/// @return The generated IL module.
Lowerer::Module Lowerer::lower(ModuleDecl &module)
{
    // Initialize state
    module_ = std::make_unique<Module>();
    builder_ = std::make_unique<il::build::IRBuilder>(*module_);
    locals_.clear();
    usedExterns_.clear();
    definedFunctions_.clear();
    pendingEntityInstantiations_.clear();
    pendingValueInstantiations_.clear();
    pendingFunctionInstantiations_.clear();

    // Setup string table emitter
    stringTable_.setEmitter([this](const std::string &label, const std::string &content)
                            { builder_->addGlobalStr(label, content); });

    // Lower all declarations
    for (auto &decl : module.declarations)
    {
        lowerDecl(decl.get());
    }

    // Process pending generic entity instantiations
    // These were deferred during expression lowering because we couldn't
    // lower methods while inside another function's body
    while (!pendingEntityInstantiations_.empty())
    {
        std::string typeName = pendingEntityInstantiations_.back();
        pendingEntityInstantiations_.pop_back();

        auto it = entityTypes_.find(typeName);
        if (it == entityTypes_.end())
            continue;

        EntityTypeInfo &info = it->second;

        // Push substitution context so type parameters resolve correctly
        bool pushedContext = sema_.pushSubstitutionContext(typeName);

        // Lower all methods for this instantiated generic entity
        for (auto *method : info.methods)
        {
            lowerMethodDecl(*method, typeName, true);
        }

        // Emit vtable
        if (!info.vtable.empty())
        {
            emitVtable(info);
        }

        // Pop substitution context if we pushed one
        if (pushedContext)
        {
            sema_.popTypeParams();
        }
    }

    // Process pending generic value type instantiations
    while (!pendingValueInstantiations_.empty())
    {
        std::string typeName = pendingValueInstantiations_.back();
        pendingValueInstantiations_.pop_back();

        auto it = valueTypes_.find(typeName);
        if (it == valueTypes_.end())
            continue;

        ValueTypeInfo &info = it->second;

        // Push substitution context so type parameters resolve correctly
        bool pushedContext = sema_.pushSubstitutionContext(typeName);

        // Lower all methods for this instantiated generic value type
        for (auto *method : info.methods)
        {
            lowerMethodDecl(*method, typeName, false);
        }

        // Pop substitution context if we pushed one
        if (pushedContext)
        {
            sema_.popTypeParams();
        }
    }

    // Process pending generic function instantiations
    while (!pendingFunctionInstantiations_.empty())
    {
        auto [mangledName, decl] = pendingFunctionInstantiations_.back();
        pendingFunctionInstantiations_.pop_back();

        // Lower the generic function instantiation
        lowerGenericFunctionInstantiation(mangledName, decl);
    }

    // Add extern declarations for used runtime functions
    for (const auto &externName : usedExterns_)
    {
        // Skip functions defined in this module
        if (definedFunctions_.count(externName) > 0)
            continue;

        // Skip methods defined in this module (value type and entity type methods)
        bool isLocalMethod = false;
        auto dotPos = externName.find('.');
        if (dotPos != std::string::npos)
        {
            std::string typeName = externName.substr(0, dotPos);
            if (getOrCreateValueTypeInfo(typeName) ||
                entityTypes_.find(typeName) != entityTypes_.end())
            {
                isLocalMethod = true;
            }
        }
        if (isLocalMethod)
            continue;

        const auto *desc = il::runtime::findRuntimeDescriptor(externName);
        if (!desc)
            continue;
        builder_->addExtern(
            std::string(desc->name), desc->signature.retType, desc->signature.paramTypes);
    }

    return std::move(*module_);
}

} // namespace il::frontends::zia

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

Lowerer::Lowerer(Sema &sema, CompilerOptions options) : sema_(sema), options_(options) {}

Lowerer::Module Lowerer::lower(ModuleDecl &module)
{
    // Initialize state
    module_ = std::make_unique<Module>();
    builder_ = std::make_unique<il::build::IRBuilder>(*module_);
    locals_.clear();
    usedExterns_.clear();
    definedFunctions_.clear();

    // Setup string table emitter
    stringTable_.setEmitter([this](const std::string &label, const std::string &content)
                            { builder_->addGlobalStr(label, content); });

    // Lower all declarations
    for (auto &decl : module.declarations)
    {
        lowerDecl(decl.get());
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
            if (valueTypes_.find(typeName) != valueTypes_.end() ||
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

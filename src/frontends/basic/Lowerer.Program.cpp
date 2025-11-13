//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lowerer.Program.cpp
// Purpose: Hosts the program-level helpers that bridge the BASIC AST to the IL
//          builder. The lowering pipeline coordinates scanning, declaration
//          emission, runtime discovery, and final IL generation. Concentrating
//          the orchestration logic in this file keeps the main `Lowerer`
//          interface focused while documenting the lifecycle of program
//          compilation.
// Key invariants: Shared lowering state is reset before each run and builders
//                 are released once emission finishes to avoid dangling
//                 pointers.
// Ownership/Lifetime: ProgramLowering borrows the Lowerer; emitted modules are
//                     owned by the caller-provided builder.
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements program-level helpers for the BASIC-to-IL lowering pipeline.
/// @details These utilities reset shared lowering state, construct IR builders,
///          and drive the staged emission sequence used by the BASIC front end.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include "viper/il/IRBuilder.hpp"

namespace il::frontends::basic
{

namespace pipeline_detail
{
/// @brief Translate a BASIC AST type enumeration into an IL core type handle.
///
/// @details Lowering frequently needs to turn semantic types expressed by the
///          BASIC AST (`Type`) into the concrete IL type descriptor understood by
///          the builder.  The mapping is intentionally narrow: each BASIC type
///          collapses to a single IL `Type::Kind`.  Should the language evolve
///          new cases can be added here without touching call sites.
///
/// @param ty BASIC type enumeration value.
/// @return Concrete IL type used during lowering. Defaults to `I64` for
///         robustness when the caller passes an unrecognised type.
il::core::Type coreTypeForAstType(::il::frontends::basic::Type ty)
{
    using il::core::Type;
    switch (ty)
    {
        case ::il::frontends::basic::Type::I64:
            return Type(Type::Kind::I64);
        case ::il::frontends::basic::Type::F64:
            return Type(Type::Kind::F64);
        case ::il::frontends::basic::Type::Str:
            return Type(Type::Kind::Str);
        case ::il::frontends::basic::Type::Bool:
            return Type(Type::Kind::I1);
    }
    return Type(Type::Kind::I64);
}
} // namespace pipeline_detail

/// @brief Create a program-lowering helper bound to an owning @c Lowerer.
///
/// @details The helper is a thin façade around the shared `Lowerer` instance.
///          Storing a reference keeps the orchestration code decoupled from the
///          parser and IR builder while still having access to shared caches,
///          manglers, and runtime trackers the front end maintains.
///
/// @param lowerer Borrowed lowering pipeline that manages shared state.
ProgramLowering::ProgramLowering(Lowerer &lowerer) : lowerer(lowerer) {}

/// @brief Lower a parsed BASIC program into IL.
///
/// @details The orchestration proceeds as follows:
///          1. Bind the destination module and initialise a fresh IR builder.
///          2. Reset lowering caches (name mangler, string pool, runtime state)
///             so previous compilations cannot leak into the new translation.
///          3. Run scanning passes that gather type and runtime requirements
///             prior to emission.
///          4. Declare and emit runtime helpers and program bodies in a
///             deterministic order, reusing the builder for all procedures.
///          5. Release borrowed references to ensure no dangling pointers remain
///             once the module is fully populated.
///
/// @param prog AST representing the BASIC program.
/// @param module IL module receiving the lowered output.
void ProgramLowering::run(const Program &prog, il::core::Module &module)
{
    lowerer.mod = &module;
    build::IRBuilder builder(module);
    lowerer.builder = &builder;

    lowerer.mangler = NameMangler();
    auto &ctx = lowerer.context();
    ctx.reset();
    lowerer.symbols.clear();
    lowerer.nextStringId = 0;
    lowerer.procSignatures.clear();

    lowerer.runtimeTracker.reset();
    lowerer.resetManualHelpers();

    lowerer.scanOOP(prog);          // Must scan OOP first to populate classLayouts_
    lowerer.scanProgram(prog);       // Then scan program (needs classLayouts_ for field assignments)
    lowerer.emitOopDeclsAndBodies(prog);
    lowerer.declareRequiredRuntime(builder);
    lowerer.emitProgram(prog);

    lowerer.builder = nullptr;
    lowerer.mod = nullptr;
}

} // namespace il::frontends::basic

// File: src/frontends/basic/Lowerer.Program.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
// Purpose: Implements program-level helpers that seed lowering state and drive
//          BASIC-to-IL emission through the shared Lowerer pipeline.
// Key invariants: Program lowering resets shared state before emitting any IR.
// Ownership/Lifetime: Operates on a borrowed Lowerer instance.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include "il/build/IRBuilder.hpp"

namespace il::frontends::basic
{

namespace pipeline_detail
{
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

ProgramLowering::ProgramLowering(Lowerer &lowerer) : lowerer(lowerer) {}

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

    lowerer.scanProgram(prog);
    lowerer.declareRequiredRuntime(builder);
    lowerer.emitProgram(prog);

    lowerer.builder = nullptr;
    lowerer.mod = nullptr;
}

} // namespace il::frontends::basic


//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements program-level helpers for the BASIC-to-IL lowering pipeline.
/// @details These utilities reset shared lowering state, construct IR builders,
/// and drive the program emission sequence used by the BASIC front end.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include "il/build/IRBuilder.hpp"

namespace il::frontends::basic
{

namespace pipeline_detail
{
/// @brief Translate a BASIC AST type into an IL core type.
/// @param ty BASIC type enumeration value.
/// @return Concrete IL type used during lowering.
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
/// @param lowerer Borrowed lowering pipeline that manages shared state.
ProgramLowering::ProgramLowering(Lowerer &lowerer) : lowerer(lowerer) {}

/// @brief Lower a parsed BASIC program into IL.
/// @details Resets shared lowering state, initialises the IR builder, declares
/// runtime support, and invokes the staged emission pipeline for every procedure.
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

    lowerer.scanProgram(prog);
    lowerer.scanOOP(prog);
    lowerer.emitOopDeclsAndBodies(prog);
    lowerer.declareRequiredRuntime(builder);
    lowerer.emitProgram(prog);

    lowerer.builder = nullptr;
    lowerer.mod = nullptr;
}

} // namespace il::frontends::basic


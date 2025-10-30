//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lowerer.cpp
// Purpose: Provide the orchestration entry points that drive BASIC to IL
//          lowering. The implementation wires together the specialised helper
//          translation units while keeping this TU as a lightweight dispatcher.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

namespace il::frontends::basic
{

Lowerer::Lowerer(bool boundsChecks)
    : programLowering(std::make_unique<ProgramLowering>(*this)),
      procedureLowering(std::make_unique<ProcedureLowering>(*this)),
      statementLowering(std::make_unique<StatementLowering>(*this)), boundsChecks(boundsChecks),
      emitter_(std::make_unique<lower::Emitter>(*this))
{
}

Lowerer::~Lowerer() = default;

Lowerer::Module Lowerer::lowerProgram(const Program &prog)
{
    Module module;
    programLowering->run(prog, module);
    return module;
}

Lowerer::Module Lowerer::lower(const Program &prog)
{
    return lowerProgram(prog);
}

void Lowerer::lowerProcedure(const std::string &name,
                             const std::vector<Param> &params,
                             const std::vector<StmtPtr> &body,
                             const ProcedureConfig &config)
{
    auto ctx = procedureLowering->makeContext(name, params, body, config);
    procedureLowering->resetContext(ctx);
    procedureLowering->collectProcedureInfo(ctx);
    procedureLowering->scheduleBlocks(ctx);
    procedureLowering->emitProcedureIL(ctx);
}

} // namespace il::frontends::basic


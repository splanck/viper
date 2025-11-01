//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lowerer.Procedure.cpp
// Purpose: Lightweight driver that orchestrates the BASIC procedure lowering
//          pipeline by delegating to modular helpers.
// Key invariants: Driver remains logic-free, deferring all work to the staged
//                 helpers housed under viper::basic::lower.
// Ownership/Lifetime: Borrows AST nodes and lowering helpers from the owning
//                     Lowerer instance without assuming ownership.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

#include "frontends/basic/LoweringPipeline.hpp"

namespace il::frontends::basic
{

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


//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lowerer.Procedure.cpp
// Purpose: Lightweight dispatcher that sequences the procedure lowering stages
//          (calls, locals, SSA, control).
// Key invariants: Stage order remains fixed to preserve existing lowering
//                 semantics; the driver performs no heavy lifting itself.
// Ownership/Lifetime: Borrows the Lowerer-owned ProcedureLowering helper and
//                     forwards contexts between specialised stages.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

namespace viper::basic::lower::calls
{
::il::frontends::basic::ProcedureLowering::LoweringContext
make_context(::il::frontends::basic::ProcedureLowering &lowering,
             const std::string &name,
             const std::vector<::il::frontends::basic::Param> &params,
             const std::vector<::il::frontends::basic::StmtPtr> &body,
             const ::il::frontends::basic::Lowerer::ProcedureConfig &config);
}

namespace viper::basic::lower::locals
{
void prepare(::il::frontends::basic::ProcedureLowering &lowering,
             ::il::frontends::basic::ProcedureLowering::LoweringContext &ctx);
}

namespace viper::basic::lower::ssa
{
void build(::il::frontends::basic::ProcedureLowering &lowering,
           ::il::frontends::basic::ProcedureLowering::LoweringContext &ctx);
}

namespace viper::basic::lower::control
{
void emit(::il::frontends::basic::ProcedureLowering &lowering,
          ::il::frontends::basic::ProcedureLowering::LoweringContext &ctx);
}

namespace il::frontends::basic
{

void Lowerer::lowerProcedure(const std::string &name,
                             const std::vector<Param> &params,
                             const std::vector<StmtPtr> &body,
                             const ProcedureConfig &config)
{
    auto ctx = viper::basic::lower::calls::make_context(*procedureLowering, name, params, body, config);
    viper::basic::lower::locals::prepare(*procedureLowering, ctx);
    viper::basic::lower::ssa::build(*procedureLowering, ctx);
    viper::basic::lower::control::emit(*procedureLowering, ctx);
}

} // namespace il::frontends::basic

// File: src/frontends/basic/LoweringPipeline.hpp
// Purpose: Declares modular lowering helpers composing the BASIC lowering pipeline.
// Key invariants: Helpers operate on Lowerer state without taking ownership.
// Ownership/Lifetime: Helper structs borrow a Lowerer instance for the duration of calls.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/DeclNodes.hpp"
#include "frontends/basic/ast/StmtNodesAll.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include <functional>
#include <string>
#include <vector>

namespace il::frontends::basic
{

class Lowerer;

namespace pipeline_detail
{
/// @brief Translate a BASIC AST scalar type into the IL core representation.
/// @param ty BASIC semantic type sourced from the front-end AST.
/// @return Corresponding IL type used for stack slots and temporaries.
il::core::Type coreTypeForAstType(::il::frontends::basic::Type ty);

} // namespace pipeline_detail

/// @brief Coordinates program-level lowering by seeding module state and driving emission.
struct ProgramLowering
{
    explicit ProgramLowering(Lowerer &lowerer);

    /// @brief Lower the full program @p prog into @p module.
    void run(const Program &prog, il::core::Module &module);

  private:
    Lowerer &lowerer;
};

/// @brief Handles procedure signature caching, variable collection, and body emission.
struct ProcedureLowering
{
    explicit ProcedureLowering(Lowerer &lowerer);

    /// @brief Cache declared signatures for all user-defined procedures.
    void collectProcedureSignatures(const Program &prog);

    /// @brief Discover variable usage within a statement list.
    void collectVars(const std::vector<const Stmt *> &stmts);

    /// @brief Collect variables from every procedure and main body statement.
    void collectVars(const Program &prog);

    /// @brief Emit a BASIC procedure using the provided configuration.
    void emit(const std::string &name,
              const std::vector<Param> &params,
              const std::vector<StmtPtr> &body,
              const Lowerer::ProcedureConfig &config);

  private:
    Lowerer &lowerer;
};

/// @brief Emits control flow for sequential statement lowering within a procedure.
struct StatementLowering
{
    explicit StatementLowering(Lowerer &lowerer);

    /// @brief Lower a sequence of statements, branching between numbered blocks.
    void lowerSequence(const std::vector<const Stmt *> &stmts,
                       bool stopOnTerminated,
                       const std::function<void(const Stmt &)> &beforeBranch = {});

  private:
    Lowerer &lowerer;
};

} // namespace il::frontends::basic

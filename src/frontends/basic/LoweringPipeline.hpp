// File: src/frontends/basic/LoweringPipeline.hpp
// Purpose: Declares modular lowering helpers composing the BASIC lowering pipeline.
// Key invariants: Helpers operate on Lowerer state without taking ownership.
// Ownership/Lifetime: Helper structs borrow a Lowerer instance for the duration of calls.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/ast/DeclNodes.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::build
{
class IRBuilder;
} // namespace il::build

namespace viper::basic::lower::calls
{
struct Access;
} // namespace viper::basic::lower::calls

namespace viper::basic::lower::locals
{
struct Access;
} // namespace viper::basic::lower::locals

namespace viper::basic::lower::ssa
{
struct Access;
} // namespace viper::basic::lower::ssa

namespace viper::basic::lower::control
{
struct Access;
} // namespace viper::basic::lower::control

namespace il::frontends::basic
{

namespace lower
{
class Emitter;
} // namespace lower

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
    struct LoweringContext
    {
        LoweringContext(Lowerer &lowerer,
                        std::unordered_map<std::string, Lowerer::SymbolInfo> &symbols,
                        il::build::IRBuilder &builder,
                        lower::Emitter &emitter,
                        std::string name,
                        const std::vector<Param> &params,
                        const std::vector<StmtPtr> &body,
                        const Lowerer::ProcedureConfig &config);

        Lowerer &lowerer;
        std::unordered_map<std::string, Lowerer::SymbolInfo> &symbols;
        il::build::IRBuilder &builder;
        lower::Emitter &emitter;
        std::string name;
        const std::vector<Param> &params;
        const std::vector<StmtPtr> &body;
        const Lowerer::ProcedureConfig &config;
        std::vector<const Stmt *> bodyStmts;
        std::unordered_set<std::string> paramNames;
        std::vector<il::core::Param> irParams;
        size_t paramCount{0};
        il::core::Function *function{nullptr};
        std::shared_ptr<Lowerer::ProcedureMetadata> metadata;
    };

    explicit ProcedureLowering(Lowerer &lowerer);

    /// @brief Cache declared signatures for all user-defined procedures.
    void collectProcedureSignatures(const Program &prog);

    /// @brief Discover variable usage within a statement list.
    void collectVars(const std::vector<const Stmt *> &stmts);

    /// @brief Collect variables from every procedure and main body statement.
    void collectVars(const Program &prog);

    /// @brief Build a lowering context describing a BASIC procedure.
    LoweringContext makeContext(const std::string &name,
                                const std::vector<Param> &params,
                                const std::vector<StmtPtr> &body,
                                const Lowerer::ProcedureConfig &config);

    /// @brief Reset per-procedure state prior to emission.
    void resetContext(LoweringContext &ctx);

    /// @brief Collect metadata required for lowering the procedure body.
    void collectProcedureInfo(LoweringContext &ctx);

    /// @brief Construct the IL function skeleton and allocate locals.
    void scheduleBlocks(LoweringContext &ctx);

    /// @brief Emit IL instructions for the lowered BASIC procedure.
    void emitProcedureIL(LoweringContext &ctx);

  private:
    Lowerer &lowerer;
    friend struct viper::basic::lower::calls::Access;
    friend struct viper::basic::lower::locals::Access;
    friend struct viper::basic::lower::ssa::Access;
    friend struct viper::basic::lower::control::Access;
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

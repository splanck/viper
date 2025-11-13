//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the modular lowering helper components that compose the
// BASIC lowering pipeline, transforming AST nodes into IL instructions.
//
// Lowering Pipeline Architecture:
// The BASIC lowering process is decomposed into specialized helper components,
// each responsible for a specific category of AST nodes or operations:
//
// - ProgramLowering: Top-level program structure, global initialization
// - ProcedureLowering: SUB/FUNCTION declarations and procedure bodies
// - StatementLowering: Statement-level lowering dispatch
// - LowerExprNumeric: Arithmetic and numeric expressions
// - LowerExprLogical: Boolean and comparison expressions
// - LowerExprBuiltin: Built-in function calls
// - LowerStmt_Core: Assignment and basic statements
// - LowerStmt_Control: Control flow (IF, FOR, WHILE, SELECT)
// - LowerStmt_IO: I/O operations (PRINT, INPUT)
// - LowerRuntime: Runtime library function declarations
//
// Design Philosophy:
// Rather than a monolithic Lowerer class, the pipeline is decomposed into
// focused helper components that:
// - Encapsulate specific lowering concerns
// - Share common Lowerer state (IRBuilder, NameMangler, symbol tables)
// - Can be tested independently
// - Are easier to understand and maintain
//
// Helper Coordination:
// All helpers:
// - Borrow a Lowerer instance for state access
// - Do not take ownership of AST or IL module
// - Use IRBuilder for IL instruction emission
// - Use NameMangler for deterministic symbol naming
// - Return IL values or register names as appropriate
//
// Integration:
// - Composed by: Lowerer class to handle different AST node types
// - Shared state: All helpers access Lowerer state
// - Modular testing: Each helper can be tested in isolation
//
// Design Notes:
// - Helpers are structs with operator() or named methods
// - No ownership transfer; all state is borrowed
// - Consistent interface across helper types
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/ast/DeclNodes.hpp"
#include "viper/il/Module.hpp"
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

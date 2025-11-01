//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Lowerer.Procedure.cpp
// Purpose: Lightweight driver that routes procedure-level lowering entry points
//          to specialised helper modules split by concern.
// Key invariants: Dispatch remains deterministic and defers heavy lifting to the
//                 lower::* helpers.
// Ownership/Lifetime: Functions borrow the owning Lowerer/ProcedureLowering
//                     instances and forward state references to helpers.
// Links: docs/codemap.md, docs/basic-language.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/LoweringPipeline.hpp"

#include <utility>

namespace viper::basic::lower::calls
{
struct Access
{
    static void collectProcedureSignatures(il::frontends::basic::ProcedureLowering &self,
                                           const il::frontends::basic::Program &prog);
    static const il::frontends::basic::Lowerer::ProcedureSignature *findProcSignature(
        const il::frontends::basic::Lowerer &lowerer,
        const std::string &name);
    static void lowerFunctionDecl(il::frontends::basic::Lowerer &lowerer, const il::frontends::basic::FunctionDecl &decl);
    static void lowerSubDecl(il::frontends::basic::Lowerer &lowerer, const il::frontends::basic::SubDecl &decl);
};
} // namespace viper::basic::lower::calls

namespace viper::basic::lower::locals
{
struct Access
{
    static il::frontends::basic::Lowerer::SymbolInfo &ensureSymbol(il::frontends::basic::Lowerer &lowerer,
                                                                  std::string_view name);
    static il::frontends::basic::Lowerer::SymbolInfo *findSymbol(il::frontends::basic::Lowerer &lowerer,
                                                                std::string_view name);
    static const il::frontends::basic::Lowerer::SymbolInfo *findSymbol(const il::frontends::basic::Lowerer &lowerer,
                                                                      std::string_view name);
    static void setSymbolType(il::frontends::basic::Lowerer &lowerer, std::string_view name, il::frontends::basic::Type type);
    static void setSymbolObjectType(il::frontends::basic::Lowerer &lowerer,
                                    std::string_view name,
                                    std::string className);
    static void markSymbolReferenced(il::frontends::basic::Lowerer &lowerer, std::string_view name);
    static void markArray(il::frontends::basic::Lowerer &lowerer, std::string_view name);
    static void resetSymbolState(il::frontends::basic::Lowerer &lowerer);
    static il::frontends::basic::Lowerer::SlotType getSlotType(const il::frontends::basic::Lowerer &lowerer,
                                                              std::string_view name);
    static void collectVars(il::frontends::basic::ProcedureLowering &self,
                            const std::vector<const il::frontends::basic::Stmt *> &stmts);
    static void collectVars(il::frontends::basic::ProcedureLowering &self, const il::frontends::basic::Program &prog);
    static il::frontends::basic::Lowerer::ProcedureMetadata
    collectProcedureMetadata(il::frontends::basic::Lowerer &lowerer,
                             const std::vector<il::frontends::basic::Param> &params,
                             const std::vector<il::frontends::basic::StmtPtr> &body,
                             const il::frontends::basic::Lowerer::ProcedureConfig &config);
    static void allocateLocalSlots(il::frontends::basic::Lowerer &lowerer,
                                   const std::unordered_set<std::string> &paramNames,
                                   bool includeParams);
    static void materializeParams(il::frontends::basic::Lowerer &lowerer,
                                  const std::vector<il::frontends::basic::Param> &params);
};
} // namespace viper::basic::lower::locals

namespace viper::basic::lower::ssa
{
struct Access
{
    static il::frontends::basic::ProcedureLowering::LoweringContext
    makeContext(il::frontends::basic::ProcedureLowering &self,
                const std::string &name,
                const std::vector<il::frontends::basic::Param> &params,
                const std::vector<il::frontends::basic::StmtPtr> &body,
                const il::frontends::basic::Lowerer::ProcedureConfig &config);
    static void resetContext(il::frontends::basic::ProcedureLowering &self,
                             il::frontends::basic::ProcedureLowering::LoweringContext &ctx);
    static void collectProcedureInfo(il::frontends::basic::ProcedureLowering &self,
                                     il::frontends::basic::ProcedureLowering::LoweringContext &ctx);
    static void resetLoweringState(il::frontends::basic::Lowerer &lowerer);
    static il::frontends::basic::Lowerer::ProcedureContext &context(il::frontends::basic::Lowerer &lowerer);
    static const il::frontends::basic::Lowerer::ProcedureContext &context(const il::frontends::basic::Lowerer &lowerer);
    static il::frontends::basic::Emit emitCommon(il::frontends::basic::Lowerer &lowerer) noexcept;
    static il::frontends::basic::Emit emitCommonAt(il::frontends::basic::Lowerer &lowerer,
                                                   il::support::SourceLoc loc) noexcept;
    static il::frontends::basic::lower::Emitter &emitter(il::frontends::basic::Lowerer &lowerer) noexcept;
    static const il::frontends::basic::lower::Emitter &emitter(const il::frontends::basic::Lowerer &lowerer) noexcept;
    static unsigned nextTempId(il::frontends::basic::Lowerer &lowerer);
};
} // namespace viper::basic::lower::ssa

namespace viper::basic::lower::control
{
struct Access
{
    static void scheduleBlocks(il::frontends::basic::ProcedureLowering &self,
                               il::frontends::basic::ProcedureLowering::LoweringContext &ctx);
    static void emitProcedureIL(il::frontends::basic::ProcedureLowering &self,
                                il::frontends::basic::ProcedureLowering::LoweringContext &ctx);
    static int virtualLine(il::frontends::basic::Lowerer &lowerer, const il::frontends::basic::Stmt &stmt);
    static void buildProcedureSkeleton(il::frontends::basic::Lowerer &lowerer,
                                       il::core::Function &f,
                                       const std::string &name,
                                       const il::frontends::basic::Lowerer::ProcedureMetadata &metadata);
    static void ensureGosubStack(il::frontends::basic::Lowerer &lowerer);
    static std::string nextFallbackBlockLabel(il::frontends::basic::Lowerer &lowerer);
};
} // namespace viper::basic::lower::control

namespace il::frontends::basic
{
namespace calls = viper::basic::lower::calls;
namespace control = viper::basic::lower::control;
namespace locals = viper::basic::lower::locals;
namespace ssa = viper::basic::lower::ssa;

Lowerer::SymbolInfo &Lowerer::ensureSymbol(std::string_view name) { return locals::Access::ensureSymbol(*this, name); }
Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name) { return locals::Access::findSymbol(*this, name); }
const Lowerer::SymbolInfo *Lowerer::findSymbol(std::string_view name) const { return locals::Access::findSymbol(*this, name); }
void Lowerer::setSymbolType(std::string_view name, AstType type) { locals::Access::setSymbolType(*this, name, type); }
void Lowerer::setSymbolObjectType(std::string_view name, std::string className)
{
    locals::Access::setSymbolObjectType(*this, name, std::move(className));
}
void Lowerer::markSymbolReferenced(std::string_view name) { locals::Access::markSymbolReferenced(*this, name); }
void Lowerer::markArray(std::string_view name) { locals::Access::markArray(*this, name); }
void Lowerer::resetSymbolState() { locals::Access::resetSymbolState(*this); }
Lowerer::SlotType Lowerer::getSlotType(std::string_view name) const { return locals::Access::getSlotType(*this, name); }
const Lowerer::ProcedureSignature *Lowerer::findProcSignature(const std::string &name) const
{
    return calls::Access::findProcSignature(*this, name);
}

void Lowerer::collectVars(const Program &prog)
{
    if (procedureLowering)
        locals::Access::collectVars(*procedureLowering, prog);
}
void Lowerer::collectVars(const std::vector<const Stmt *> &stmts)
{
    if (procedureLowering)
        locals::Access::collectVars(*procedureLowering, stmts);
}
void Lowerer::collectProcedureSignatures(const Program &prog)
{
    if (procedureLowering)
        calls::Access::collectProcedureSignatures(*procedureLowering, prog);
}
Lowerer::ProcedureMetadata
Lowerer::collectProcedureMetadata(const std::vector<Param> &params, const std::vector<StmtPtr> &body, const ProcedureConfig &cfg)
{
    return locals::Access::collectProcedureMetadata(*this, params, body, cfg);
}
void Lowerer::allocateLocalSlots(const std::unordered_set<std::string> &paramNames, bool includeParams)
{
    locals::Access::allocateLocalSlots(*this, paramNames, includeParams);
}
void Lowerer::materializeParams(const std::vector<Param> &params) { locals::Access::materializeParams(*this, params); }
void Lowerer::lowerFunctionDecl(const FunctionDecl &decl) { calls::Access::lowerFunctionDecl(*this, decl); }
void Lowerer::lowerSubDecl(const SubDecl &decl) { calls::Access::lowerSubDecl(*this, decl); }
void Lowerer::resetLoweringState() { ssa::Access::resetLoweringState(*this); }
Lowerer::ProcedureContext &Lowerer::context() noexcept { return ssa::Access::context(*this); }
const Lowerer::ProcedureContext &Lowerer::context() const noexcept { return ssa::Access::context(*this); }
Emit Lowerer::emitCommon() noexcept { return ssa::Access::emitCommon(*this); }
Emit Lowerer::emitCommon(il::support::SourceLoc loc) noexcept { return ssa::Access::emitCommonAt(*this, loc); }
lower::Emitter &Lowerer::emitter() noexcept { return ssa::Access::emitter(*this); }
const lower::Emitter &Lowerer::emitter() const noexcept { return ssa::Access::emitter(*this); }
unsigned Lowerer::nextTempId() { return ssa::Access::nextTempId(*this); }
void Lowerer::ensureGosubStack() { control::Access::ensureGosubStack(*this); }
int Lowerer::virtualLine(const Stmt &stmt) { return control::Access::virtualLine(*this, stmt); }
void Lowerer::buildProcedureSkeleton(Function &f, const std::string &name, const ProcedureMetadata &md)
{
    control::Access::buildProcedureSkeleton(*this, f, name, md);
}
std::string Lowerer::nextFallbackBlockLabel() { return control::Access::nextFallbackBlockLabel(*this); }

ProcedureLowering::LoweringContext ProcedureLowering::makeContext(const std::string &name,
                                                                  const std::vector<Param> &params,
                                                                  const std::vector<StmtPtr> &body,
                                                                  const Lowerer::ProcedureConfig &cfg)
{
    return ssa::Access::makeContext(*this, name, params, body, cfg);
}
void ProcedureLowering::resetContext(LoweringContext &ctx) { ssa::Access::resetContext(*this, ctx); }
void ProcedureLowering::collectProcedureInfo(LoweringContext &ctx) { ssa::Access::collectProcedureInfo(*this, ctx); }
void ProcedureLowering::collectProcedureSignatures(const Program &prog) { calls::Access::collectProcedureSignatures(*this, prog); }
void ProcedureLowering::collectVars(const std::vector<const Stmt *> &stmts) { locals::Access::collectVars(*this, stmts); }
void ProcedureLowering::collectVars(const Program &prog) { locals::Access::collectVars(*this, prog); }
void ProcedureLowering::scheduleBlocks(LoweringContext &ctx) { control::Access::scheduleBlocks(*this, ctx); }
void ProcedureLowering::emitProcedureIL(LoweringContext &ctx) { control::Access::emitProcedureIL(*this, ctx); }

} // namespace il::frontends::basic

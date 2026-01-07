//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/sem/Check_Expr_Call.cpp
// Purpose: Validate BASIC procedure/function invocations and infer their result
//          type during semantic analysis.
// Key invariants:
//   * Call expressions are resolved against the procedure registry so overload
//     selection stays consistent across the compiler pipeline.
//   * Diagnostics surface both signature mismatches and invalid arguments while
//     preserving the analyzer's contextual state (loop stacks, scope guards).
// References: docs/codemap/basic.md, docs/basic-language.md#procedures
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Semantic analysis helper for function call expressions.
/// @details Resolves function signatures, validates argument lists, and forwards
///          return-type inference to the analyzer's shared helpers.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/sem/Check_Common.hpp"

namespace il::frontends::basic::sem
{

/// @brief Type-check a BASIC call expression and compute its resulting type.
///
/// The helper constructs an @ref ExprCheckContext to preserve the analyzer's
/// state machine while performing the multi-step validation required for calls:
///
/// 1. Resolve the callee symbol against the procedure registry using
///    @ref ExprCheckContext::resolveCallee.  This step reports diagnostics when
///    the identifier is unknown or refers to the wrong callable category.
/// 2. Validate and convert arguments with @ref ExprCheckContext::checkCallArgs,
///    ensuring arity, type compatibility, and implicit conversions are tracked
///    for later lowering phases.
/// 3. Ask the context to infer the call's return type so semantic analysis can
///    propagate it to the AST and subsequent expression checks.
///
/// Each sub-step is responsible for emitting detailed diagnostics; the helper
/// simply threads the resulting signature pointer through the pipeline and
/// returns the final type classification.
///
/// @param analyzer Semantic analyzer coordinating the current compilation.
/// @param expr Call expression to validate.
/// @return Semantic type that the call expression produces (or Unknown on
///         failure when diagnostics have been emitted).
SemanticAnalyzer::Type analyzeCallExpr(SemanticAnalyzer &analyzer, const CallExpr &expr)
{
    ExprCheckContext context(analyzer);
    const auto *sig = context.resolveCallee(expr, ProcSignature::Kind::Function);
    auto argTypes [[maybe_unused]] = context.checkCallArgs(expr, sig);
    return context.inferCallType(expr, sig);
}

} // namespace il::frontends::basic::sem

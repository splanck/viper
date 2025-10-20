//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
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

SemanticAnalyzer::Type analyzeCallExpr(SemanticAnalyzer &analyzer, const CallExpr &expr)
{
    ExprCheckContext context(analyzer);
    const auto *sig = context.resolveCallee(expr, ProcSignature::Kind::Function);
    auto argTypes [[maybe_unused]] = context.checkCallArgs(expr, sig);
    return context.inferCallType(expr, sig);
}

} // namespace il::frontends::basic::sem

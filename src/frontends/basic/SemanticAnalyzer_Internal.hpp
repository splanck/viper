//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/SemanticAnalyzer_Internal.hpp
// Purpose: Declares shared helper utilities for SemanticAnalyzer implementation
// Key invariants: Helpers remain internal to the BASIC front end and avoid
// Ownership/Lifetime: Stateless free functions used by SemanticAnalyzer
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/BasicTypes.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "frontends/basic/SemanticAnalyzer_Stmts_Control.hpp"
#include "frontends/basic/SemanticAnalyzer_Stmts_IO.hpp"
#include "frontends/basic/SemanticAnalyzer_Stmts_Runtime.hpp"
#include "frontends/basic/SemanticAnalyzer_Stmts_Shared.hpp"

namespace il::frontends::basic::sem
{
class ExprCheckContext;
SemanticAnalyzer::Type analyzeUnaryExpr(SemanticAnalyzer &analyzer, const UnaryExpr &expr);
SemanticAnalyzer::Type analyzeBinaryExpr(SemanticAnalyzer &analyzer, const BinaryExpr &expr);
SemanticAnalyzer::Type analyzeCallExpr(SemanticAnalyzer &analyzer, const CallExpr &expr);
SemanticAnalyzer::Type analyzeVarExpr(SemanticAnalyzer &analyzer, VarExpr &expr);
SemanticAnalyzer::Type analyzeArrayExpr(SemanticAnalyzer &analyzer, ArrayExpr &expr);
SemanticAnalyzer::Type analyzeLBoundExpr(SemanticAnalyzer &analyzer, LBoundExpr &expr);
SemanticAnalyzer::Type analyzeUBoundExpr(SemanticAnalyzer &analyzer, UBoundExpr &expr);
} // namespace il::frontends::basic::sem

namespace il::frontends::basic::semantic_analyzer_detail
{

struct ExprRule
{
    using OperandValidator = void (*)(sem::ExprCheckContext &,
                                      const BinaryExpr &,
                                      SemanticAnalyzer::Type,
                                      SemanticAnalyzer::Type,
                                      std::string_view);
    using ResultTypeFn = SemanticAnalyzer::Type (*)(SemanticAnalyzer::Type, SemanticAnalyzer::Type);

    BinaryExpr::Op op;
    OperandValidator validator;
    ResultTypeFn result;
    std::string_view mismatchDiag;
};

const ExprRule &exprRule(BinaryExpr::Op op);

std::string formatLogicalOperandMessage(BinaryExpr::Op op,
                                        SemanticAnalyzer::Type lhs,
                                        SemanticAnalyzer::Type rhs);

SemanticAnalyzer::Type commonNumericType(SemanticAnalyzer::Type lhs,
                                         SemanticAnalyzer::Type rhs) noexcept;

size_t levenshtein(const std::string &a, const std::string &b);
SemanticAnalyzer::Type astToSemanticType(::il::frontends::basic::Type ty);
const char *builtinName(BuiltinCallExpr::Builtin b);
const char *semanticTypeName(SemanticAnalyzer::Type type);
const char *logicalOpName(BinaryExpr::Op op);
std::string conditionExprText(const Expr &expr);
std::optional<BasicType> suffixBasicType(std::string_view name);
std::optional<SemanticAnalyzer::Type> semanticTypeFromBasic(BasicType type);
std::string uppercaseBasicTypeName(BasicType type);
bool isNumericSemanticType(SemanticAnalyzer::Type type) noexcept;

} // namespace il::frontends::basic::semantic_analyzer_detail

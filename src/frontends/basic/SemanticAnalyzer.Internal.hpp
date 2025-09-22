// File: src/frontends/basic/SemanticAnalyzer.Internal.hpp
// Purpose: Declares shared helper utilities for SemanticAnalyzer implementation
//          split across multiple translation units.
// Key invariants: Helpers remain internal to the BASIC front end and avoid
//                 leaking into public headers.
// Ownership/Lifetime: Stateless free functions used by SemanticAnalyzer
//                     implementation files.
// Links: docs/class-catalog.md
#pragma once

#include "frontends/basic/SemanticAnalyzer.hpp"

#include <cstddef>
#include <string>

namespace il::frontends::basic::semantic_analyzer_detail
{

size_t levenshtein(const std::string &a, const std::string &b);
SemanticAnalyzer::Type astToSemanticType(::il::frontends::basic::Type ty);
const char *builtinName(BuiltinCallExpr::Builtin b);
const char *semanticTypeName(SemanticAnalyzer::Type type);
const char *logicalOpName(BinaryExpr::Op op);
std::string conditionExprText(const Expr &expr);

} // namespace il::frontends::basic::semantic_analyzer_detail

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Lower_OOP_Helpers.cpp
// Purpose: Shared helper functions for BASIC OOP lowering operations.
// Key invariants: Provides common utilities for type resolution and orchestration.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Lower_OOP_Internal.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/OopLoweringContext.hpp"
#include "frontends/basic/SemanticAnalyzer.hpp"
#include "frontends/basic/StringUtils.hpp"

#include <functional>
#include <string>

namespace il::frontends::basic
{

// resolveObjectClass is implemented in Lower_OOP_Expr.cpp

// -------------------------------------------------------------------------
// Centralized OOP Resolution Helpers
// -------------------------------------------------------------------------
// These helpers consolidate patterns that were duplicated across OOP lowering
// code (BUG-061, BUG-082, BUG-089, etc.).

std::string resolveFieldObjectClass(const ClassLayout *layout,
                                    std::string_view fieldName,
                                    const std::function<std::string(const std::string &)> &qualify)
{
    if (!layout)
        return {};
    const auto *field = layout->findField(fieldName);
    if (!field || field->objectClassName.empty())
        return {};
    return qualify ? qualify(field->objectClassName) : field->objectClassName;
}

std::string resolveFieldArrayElementClass(const ClassLayout *layout,
                                          std::string_view fieldName,
                                          const std::function<std::string(const std::string &)> &qualify)
{
    if (!layout)
        return {};
    const auto *field = layout->findField(fieldName);
    if (!field || !field->isArray || field->objectClassName.empty())
        return {};
    return qualify ? qualify(field->objectClassName) : field->objectClassName;
}

} // namespace il::frontends::basic

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/oop/Lower_OOP_MethodCall.cpp
// Purpose: Lower BASIC OOP method calls and virtual dispatch operations.
// Key invariants: Method calls use vtable for virtual dispatch; property
//                 accessors follow get_/set_ naming conventions.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/ILTypeUtils.hpp"
#include "frontends/basic/lower/oop/Lower_OOP_Internal.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "frontends/basic/sem/OverloadResolution.hpp"
#include "frontends/basic/sem/RuntimeMethodIndex.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

#include <string>
#include <vector>

namespace il::frontends::basic
{

// Functions to be moved here from original files:
// - lowerMethodCallExpr (from Lower_OOP_Expr.cpp lines 740-1241)
// - emitClassMethod (from Lower_OOP_Emit.cpp lines 561-741)
// - emitClassMethodWithBody (from Lower_OOP_Emit.cpp lines 742-916)

// Temporary: Keep functions in original files until full migration

} // namespace il::frontends::basic

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/oop/Lower_OOP_MemberAccess.cpp
// Purpose: Lower BASIC OOP field and property access operations.
// Key invariants: Field access respects recorded offsets; nullable receivers
//                 are handled with appropriate runtime checks.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/ILTypeUtils.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "frontends/basic/lower/oop/Lower_OOP_Internal.hpp"
#include "frontends/basic/sem/RuntimePropertyIndex.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

#include <optional>
#include <string>
#include <vector>

namespace il::frontends::basic
{

// Functions to be moved here from original files:
// - resolveMemberField (from Lower_OOP_Expr.cpp lines 445-522)
// - resolveImplicitField (from Lower_OOP_Expr.cpp lines 523-553)
// - lowerMemberAccessExpr (from Lower_OOP_Expr.cpp lines 555-738)
// - lowerMeExpr (from Lower_OOP_Expr.cpp lines 424-443)
// - materializeSelfSlot (from Lower_OOP_Emit.cpp lines 113-134)
// - loadSelfPointer (from Lower_OOP_Emit.cpp lines 135-151)

// Temporary: Keep functions in original files until full migration

} // namespace il::frontends::basic

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/Lower_OOP_Alloc.cpp
// Purpose: Lower BASIC OOP allocation, construction, and destruction operations.
// Key invariants: Object allocations route through runtime helpers; constructors
//                 and destructors follow the recorded class layouts.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/ILTypeUtils.hpp"
#include "frontends/basic/Lower_OOP_Internal.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"
#include "frontends/basic/OopIndex.hpp"
#include "frontends/basic/Options.hpp"
#include "frontends/basic/StringUtils.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace il::frontends::basic
{

// Functions to be moved here from original files:
// - lowerNewExpr (from Lower_OOP_Expr.cpp lines 221-413)
// - lowerDelete (from Lower_OOP_Stmt.cpp lines 58-107)
// - emitClassConstructor (from Lower_OOP_Emit.cpp lines 213-480)
// - emitClassDestructor (from Lower_OOP_Emit.cpp lines 481-560)
// - emitFieldReleaseSequence (from Lower_OOP_Emit.cpp lines 152-212)

// Temporary: Keep functions in original files until full migration

} // namespace il::frontends::basic
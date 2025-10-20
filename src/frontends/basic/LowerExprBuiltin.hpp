// File: src/frontends/basic/LowerExprBuiltin.hpp
// Purpose: Declares builtin expression lowering helpers that interact with the
//          BASIC Lowerer.
// Key invariants: Builtin lowering preserves runtime feature tracking and
//                 common coercions provided by Lowerer utilities.
// Ownership/Lifetime: Helpers borrow the Lowerer reference for a single
//                      lowering step.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

struct BuiltinExprLowering
{
    explicit BuiltinExprLowering(Lowerer &lowerer) noexcept;

    [[nodiscard]] Lowerer::RVal lower(const BuiltinCallExpr &expr);

    using EmitFn = Lowerer::RVal (*)(Lowerer &, const BuiltinCallExpr &);

    static Lowerer::RVal emitRuleDrivenBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);
    static Lowerer::RVal emitLofBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);
    static Lowerer::RVal emitEofBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);
    static Lowerer::RVal emitLocBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);
    static Lowerer::RVal emitUnsupportedBuiltin(Lowerer &lowerer, const BuiltinCallExpr &expr);

  private:
    Lowerer *lowerer_{nullptr};
};

[[nodiscard]] Lowerer::RVal lowerBuiltinCall(Lowerer &lowerer, const BuiltinCallExpr &expr);

} // namespace il::frontends::basic

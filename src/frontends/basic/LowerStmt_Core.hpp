// File: src/frontends/basic/LowerStmt_Core.hpp
// License: GPL-3.0-only. See LICENSE in the project root for full license information.
// Purpose: Declares core statement-lowering helpers shared across categories.
// Key invariants: Helpers operate on the active Lowerer context without
//                 re-dispatching or duplicating state transitions.
// Ownership/Lifetime: Declarations are included within Lowerer to extend its
//                     private interface for core statement lowering.
// Links: docs/codemap.md
#pragma once

void lowerStmtList(const StmtList &stmt);
void lowerCallStmt(const CallStmt &stmt);
void lowerReturn(const ReturnStmt &stmt);
RVal normalizeChannelToI32(RVal channel, il::support::SourceLoc loc);
void emitRuntimeErrCheck(Value err,
                         il::support::SourceLoc loc,
                         std::string_view labelStem,
                         const std::function<void(Value)> &onFailure);

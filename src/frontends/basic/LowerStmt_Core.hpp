//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/LowerStmt_Core.hpp
// Purpose: Declares core statement-lowering helpers shared across categories. 
// Key invariants: Helpers operate on the active Lowerer context without
// Ownership/Lifetime: Declarations are included within Lowerer to extend its
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

void lowerStmtList(const StmtList &stmt);
void lowerCallStmt(const CallStmt &stmt);
void lowerReturn(const ReturnStmt &stmt);
RVal normalizeChannelToI32(RVal channel, il::support::SourceLoc loc);
void emitRuntimeErrCheck(Value err,
                         il::support::SourceLoc loc,
                         std::string_view labelStem,
                         const std::function<void(Value)> &onFailure);

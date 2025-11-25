//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/LowerStmt_IO.hpp
// Purpose: Declares BASIC I/O statement lowering helpers grouped by channel or
// Key invariants: Helpers convert BASIC semantics to runtime shims using the
// Ownership/Lifetime: Declarations extend Lowerer's private interface when
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

void lowerOpen(const OpenStmt &stmt);
void lowerClose(const CloseStmt &stmt);
void lowerSeek(const SeekStmt &stmt);
void lowerPrint(const PrintStmt &stmt);
void lowerPrintCh(const PrintChStmt &stmt);
void lowerInput(const InputStmt &stmt);
void lowerInputCh(const InputChStmt &stmt);
void lowerLineInputCh(const LineInputChStmt &stmt);

// File: src/frontends/basic/LowerStmt_IO.hpp
// License: GPL-3.0-only. See LICENSE in the project root for full license information.
// Purpose: Declares BASIC I/O statement lowering helpers grouped by channel or
//          terminal operations.
// Key invariants: Helpers convert BASIC semantics to runtime shims using the
//                 active Lowerer context for diagnostics and block state.
// Ownership/Lifetime: Declarations extend Lowerer's private interface when
//                     included from LowerEmit.hpp.
// Links: docs/codemap.md
#pragma once

void lowerOpen(const OpenStmt &stmt);
void lowerClose(const CloseStmt &stmt);
void lowerSeek(const SeekStmt &stmt);
void lowerPrint(const PrintStmt &stmt);
void lowerPrintCh(const PrintChStmt &stmt);
void lowerInput(const InputStmt &stmt);
void lowerInputCh(const InputChStmt &stmt);
void lowerLineInputCh(const LineInputChStmt &stmt);

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/LowerStmt_IO.cpp
// Purpose: Delegation layer for I/O statement lowering.
//          All implementations have been migrated to IoStatementLowerer.
// Key invariants: Maintains backward compatibility by forwarding to IoStatementLowerer
// Ownership/Lifetime: Functions delegate to ioStmtLowerer_ member
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/IoStatementLowerer.hpp"
#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

// Forward declarations for I/O helper functions (defined in IoStatementLowerer.cpp)
Lowerer::PrintChArgString lowerPrintChArgToString(IoStatementLowerer &self,
                                                   const Expr &expr,
                                                   Lowerer::RVal value,
                                                   bool quoteStrings);
Value buildPrintChWriteRecord(IoStatementLowerer &self, const PrintChStmt &stmt);

void Lowerer::lowerOpen(const OpenStmt &stmt)
{
    ioStmtLowerer_->lowerOpen(stmt);
}

void Lowerer::lowerClose(const CloseStmt &stmt)
{
    ioStmtLowerer_->lowerClose(stmt);
}

void Lowerer::lowerSeek(const SeekStmt &stmt)
{
    ioStmtLowerer_->lowerSeek(stmt);
}

void Lowerer::lowerPrint(const PrintStmt &stmt)
{
    ioStmtLowerer_->lowerPrint(stmt);
}

void Lowerer::lowerPrintCh(const PrintChStmt &stmt)
{
    ioStmtLowerer_->lowerPrintCh(stmt);
}

void Lowerer::lowerInput(const InputStmt &stmt)
{
    ioStmtLowerer_->lowerInput(stmt);
}

void Lowerer::lowerInputCh(const InputChStmt &stmt)
{
    ioStmtLowerer_->lowerInputCh(stmt);
}

void Lowerer::lowerLineInputCh(const LineInputChStmt &stmt)
{
    ioStmtLowerer_->lowerLineInputCh(stmt);
}

Lowerer::PrintChArgString Lowerer::lowerPrintChArgToString(const Expr &expr, RVal value, bool quoteStrings)
{
    return ::il::frontends::basic::lowerPrintChArgToString(*ioStmtLowerer_, expr, value, quoteStrings);
}

Value Lowerer::buildPrintChWriteRecord(const PrintChStmt &stmt)
{
    return ::il::frontends::basic::buildPrintChWriteRecord(*ioStmtLowerer_, stmt);
}

} // namespace il::frontends::basic

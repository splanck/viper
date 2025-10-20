//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/print/Print_Stmt_Common.hpp
// Purpose: Shared helpers for BASIC statement printing.
// Whitespace invariants: Helpers never emit trailing whitespace and only insert
//   spaces or commas that mirror canonical BASIC formatting.
// Ownership/Lifetime: Context references are non-owning and must outlive the
//   helper calls.
// Notes: Used internally by AstPrinter statement visitors.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/AstPrinter.hpp"

#include <memory>
#include <ostream>
#include <vector>

namespace il::frontends::basic
{
class AstPrinter;
} // namespace il::frontends::basic

namespace il::frontends::basic::print_stmt
{
struct Context
{
    AstPrinter::Printer &printer;
    AstPrinter::PrintStyle &style;
    AstPrinter::StmtPrinter &dispatcher;

    std::ostream &stream() const { return printer.os; }

    void printExpr(const Expr &expr) const;

    void printOptionalExpr(const Expr *expr) const;

    void printStmt(const Stmt &stmt) const;

    void printNumberedBody(const std::vector<std::unique_ptr<Stmt>> &body) const;
};

inline const char *typeToString(Type ty)
{
    switch (ty)
    {
        case Type::I64:
            return "I64";
        case Type::F64:
            return "F64";
        case Type::Str:
            return "STR";
        case Type::Bool:
            return "BOOLEAN";
    }
    return "I64";
}

inline const char *openModeToString(OpenStmt::Mode mode)
{
    switch (mode)
    {
        case OpenStmt::Mode::Input:
            return "INPUT";
        case OpenStmt::Mode::Output:
            return "OUTPUT";
        case OpenStmt::Mode::Append:
            return "APPEND";
        case OpenStmt::Mode::Binary:
            return "BINARY";
        case OpenStmt::Mode::Random:
            return "RANDOM";
    }
    return "INPUT";
}

void printPrint(const PrintStmt &stmt, Context &ctx);
void printPrintChannel(const PrintChStmt &stmt, Context &ctx);
void printOpen(const OpenStmt &stmt, Context &ctx);
void printClose(const CloseStmt &stmt, Context &ctx);
void printSeek(const SeekStmt &stmt, Context &ctx);
void printInput(const InputStmt &stmt, Context &ctx);
void printInputChannel(const InputChStmt &stmt, Context &ctx);
void printLineInputChannel(const LineInputChStmt &stmt, Context &ctx);

void printIf(const IfStmt &stmt, Context &ctx);
void printSelectCase(const SelectCaseStmt &stmt, Context &ctx);
void printWhile(const WhileStmt &stmt, Context &ctx);
void printDo(const DoStmt &stmt, Context &ctx);
void printFor(const ForStmt &stmt, Context &ctx);
void printNext(const NextStmt &stmt, Context &ctx);
void printExit(const ExitStmt &stmt, Context &ctx);

void printGoto(const GotoStmt &stmt, Context &ctx);
void printGosub(const GosubStmt &stmt, Context &ctx);
void printReturn(const ReturnStmt &stmt, Context &ctx);
void printOnErrorGoto(const OnErrorGoto &stmt, Context &ctx);
void printResume(const Resume &stmt, Context &ctx);

void printLet(const LetStmt &stmt, Context &ctx);
void printDim(const DimStmt &stmt, Context &ctx);
void printReDim(const ReDimStmt &stmt, Context &ctx);
void printFunction(const FunctionDecl &stmt, Context &ctx);
void printSub(const SubDecl &stmt, Context &ctx);
void printConstructor(const ConstructorDecl &stmt, Context &ctx);
void printDestructor(const DestructorDecl &stmt, Context &ctx);
void printMethod(const MethodDecl &stmt, Context &ctx);
void printClass(const ClassDecl &stmt, Context &ctx);
void printType(const TypeDecl &stmt, Context &ctx);
void printDelete(const DeleteStmt &stmt, Context &ctx);

} // namespace il::frontends::basic::print_stmt

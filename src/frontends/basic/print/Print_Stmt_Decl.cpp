//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/print/Print_Stmt_Decl.cpp
// Purpose: Emit BASIC declaration and binding statements for the AST printer.
// Whitespace invariants: Helper functions only emit single spaces where the
//   original printer did, relying on Context utilities for nested spacing.
// Ownership/Lifetime: Context and statement nodes are managed by the caller.
// Notes: Utility lambdas ensure consistent parameter and field rendering.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/print/Print_Stmt_Common.hpp"

namespace il::frontends::basic::print_stmt
{
namespace
{
void printParamList(const std::vector<Param> &params, Context &ctx)
{
    bool firstParam = true;
    for (const auto &param : params)
    {
        if (!firstParam)
        {
            ctx.stream() << ' ';
        }
        firstParam = false;
        ctx.stream() << param.name;
        if (param.is_array)
        {
            ctx.stream() << "()";
        }
    }
}

template <typename FieldT> void printFields(const std::vector<FieldT> &fields, Context &ctx)
{
    if (fields.empty())
    {
        return;
    }
    auto &os = ctx.stream();
    os << " (FIELDS";
    for (const auto &field : fields)
    {
        os << ' ' << field.name << ':' << typeToString(field.type);
    }
    os << ')';
}
} // namespace

void printLet(const LetStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(LET ";
    ctx.printExpr(*stmt.target);
    os << ' ';
    ctx.printExpr(*stmt.expr);
    os << ')';
}

void printDim(const DimStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(DIM " << stmt.name;
    if (stmt.isArray)
    {
        if (stmt.size)
        {
            os << ' ';
            ctx.printExpr(*stmt.size);
        }
        if (stmt.type != Type::I64)
        {
            os << " AS " << typeToString(stmt.type);
        }
    }
    else
    {
        os << " AS " << typeToString(stmt.type);
    }
    os << ')';
}

void printReDim(const ReDimStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(REDIM " << stmt.name;
    if (stmt.size)
    {
        os << ' ';
        ctx.printExpr(*stmt.size);
    }
    os << ')';
}

void printFunction(const FunctionDecl &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(FUNCTION " << stmt.name << " RET " << typeToString(stmt.ret) << " (";
    printParamList(stmt.params, ctx);
    os << ")";
    ctx.printNumberedBody(stmt.body);
}

void printSub(const SubDecl &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(SUB " << stmt.name << " (";
    printParamList(stmt.params, ctx);
    os << ")";
    ctx.printNumberedBody(stmt.body);
}

void printConstructor(const ConstructorDecl &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(CONSTRUCTOR (";
    printParamList(stmt.params, ctx);
    os << ")";
    ctx.printNumberedBody(stmt.body);
}

void printDestructor(const DestructorDecl &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(DESTRUCTOR";
    ctx.printNumberedBody(stmt.body);
}

void printMethod(const MethodDecl &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(METHOD " << stmt.name;
    if (stmt.ret)
    {
        os << " RET " << typeToString(*stmt.ret);
    }
    os << " (";
    printParamList(stmt.params, ctx);
    os << ")";
    ctx.printNumberedBody(stmt.body);
}

void printClass(const ClassDecl &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(CLASS " << stmt.name;
    printFields(stmt.fields, ctx);
    ctx.printNumberedBody(stmt.members);
}

void printType(const TypeDecl &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(TYPE " << stmt.name;
    printFields(stmt.fields, ctx);
    os << ')';
}

void printDelete(const DeleteStmt &stmt, Context &ctx)
{
    auto &os = ctx.stream();
    os << "(DELETE ";
    ctx.printExpr(*stmt.target);
    os << ')';
}

} // namespace il::frontends::basic::print_stmt

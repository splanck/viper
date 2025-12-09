//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/AST.cpp
// Purpose: Implements helper functions for Pascal AST nodes.
// Key invariants: Kind-to-string functions cover all enum values.
// Ownership/Lifetime: N/A (stateless functions).
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/AST.hpp"

namespace il::frontends::pascal
{

const char *exprKindToString(ExprKind kind)
{
    switch (kind)
    {
        case ExprKind::IntLiteral:     return "IntLiteral";
        case ExprKind::RealLiteral:    return "RealLiteral";
        case ExprKind::StringLiteral:  return "StringLiteral";
        case ExprKind::BoolLiteral:    return "BoolLiteral";
        case ExprKind::NilLiteral:     return "NilLiteral";
        case ExprKind::Name:           return "Name";
        case ExprKind::Unary:          return "Unary";
        case ExprKind::Binary:         return "Binary";
        case ExprKind::Call:           return "Call";
        case ExprKind::Index:          return "Index";
        case ExprKind::Field:          return "Field";
        case ExprKind::TypeCast:       return "TypeCast";
        case ExprKind::SetConstructor: return "SetConstructor";
        case ExprKind::AddressOf:      return "AddressOf";
        case ExprKind::Dereference:    return "Dereference";
        case ExprKind::Is:             return "Is";
    }
    return "?";
}

const char *stmtKindToString(StmtKind kind)
{
    switch (kind)
    {
        case StmtKind::Assign:     return "Assign";
        case StmtKind::Call:       return "Call";
        case StmtKind::Block:      return "Block";
        case StmtKind::If:         return "If";
        case StmtKind::Case:       return "Case";
        case StmtKind::For:        return "For";
        case StmtKind::ForIn:      return "ForIn";
        case StmtKind::While:      return "While";
        case StmtKind::Repeat:     return "Repeat";
        case StmtKind::Break:      return "Break";
        case StmtKind::Continue:   return "Continue";
        case StmtKind::Exit:       return "Exit";
        case StmtKind::Raise:      return "Raise";
        case StmtKind::TryExcept:  return "TryExcept";
        case StmtKind::TryFinally: return "TryFinally";
        case StmtKind::With:       return "With";
        case StmtKind::Inherited:  return "Inherited";
        case StmtKind::Empty:      return "Empty";
    }
    return "?";
}

const char *declKindToString(DeclKind kind)
{
    switch (kind)
    {
        case DeclKind::Const:       return "Const";
        case DeclKind::Var:         return "Var";
        case DeclKind::Type:        return "Type";
        case DeclKind::Procedure:   return "Procedure";
        case DeclKind::Function:    return "Function";
        case DeclKind::Class:       return "Class";
        case DeclKind::Interface:   return "Interface";
        case DeclKind::Constructor: return "Constructor";
        case DeclKind::Destructor:  return "Destructor";
        case DeclKind::Method:      return "Method";
        case DeclKind::Property:    return "Property";
        case DeclKind::Label:       return "Label";
        case DeclKind::Uses:        return "Uses";
    }
    return "?";
}

const char *typeKindToString(TypeKind kind)
{
    switch (kind)
    {
        case TypeKind::Named:     return "Named";
        case TypeKind::Optional:  return "Optional";
        case TypeKind::Array:     return "Array";
        case TypeKind::Record:    return "Record";
        case TypeKind::Pointer:   return "Pointer";
        case TypeKind::Procedure: return "Procedure";
        case TypeKind::Function:  return "Function";
        case TypeKind::Set:       return "Set";
        case TypeKind::Range:     return "Range";
        case TypeKind::Enum:      return "Enum";
    }
    return "?";
}

} // namespace il::frontends::pascal

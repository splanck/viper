//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/pascal/SemanticAnalyzer.cpp
// Purpose: Core entry points for Viper Pascal semantic analysis.
// Key invariants: Two-pass analysis; error recovery returns Unknown type.
// Ownership/Lifetime: Borrows DiagnosticEngine; AST not owned.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "frontends/common/CharUtils.hpp"
#include "frontends/pascal/BuiltinRegistry.hpp"

namespace il::frontends::pascal
{

// Use common toLowercase for case-insensitive comparison
using common::char_utils::toLowercase;

//===----------------------------------------------------------------------===//
// PasType Implementation
//===----------------------------------------------------------------------===//

std::string PasType::toString() const
{
    switch (kind)
    {
        case PasTypeKind::Integer:
            return "Integer";
        case PasTypeKind::Real:
            return "Real";
        case PasTypeKind::Boolean:
            return "Boolean";
        case PasTypeKind::String:
            return "String";
        case PasTypeKind::Enum:
            return "enum";
        case PasTypeKind::Array:
            if (elementType)
            {
                if (dimensions == 0)
                    return "array of " + elementType->toString();
                return "array[" + std::to_string(dimensions) + "] of " + elementType->toString();
            }
            return "array";
        case PasTypeKind::Record:
            return "record";
        case PasTypeKind::Class:
            return name.empty() ? "class" : name;
        case PasTypeKind::Interface:
            return name.empty() ? "interface" : name;
        case PasTypeKind::Optional:
            if (innerType)
                return innerType->toString() + "?";
            return "optional";
        case PasTypeKind::Pointer:
            if (pointeeType)
                return "^" + pointeeType->toString();
            return "pointer";
        case PasTypeKind::Procedure:
            return "procedure";
        case PasTypeKind::Function:
            return "function";
        case PasTypeKind::Set:
            return "set";
        case PasTypeKind::Range:
            return "range";
        case PasTypeKind::Nil:
            return "nil";
        case PasTypeKind::Unknown:
            return "<unknown>";
        case PasTypeKind::Void:
            return "void";
    }
    return "<invalid>";
}

//===----------------------------------------------------------------------===//
// SemanticAnalyzer Constructor
//===----------------------------------------------------------------------===//

SemanticAnalyzer::SemanticAnalyzer(il::support::DiagnosticEngine &diag) : diag_(diag)
{
    registerPrimitives();
    registerBuiltins();
    // Start with global scope
    pushScope();
}

//===----------------------------------------------------------------------===//
// Public Analysis Entry Points
//===----------------------------------------------------------------------===//

bool SemanticAnalyzer::analyze(Program &prog)
{
    // Import symbols from used units
    if (!prog.usedUnits.empty())
    {
        importUnits(prog.usedUnits);
    }

    // Pass 1: Collect declarations
    collectDeclarations(prog);

    // Check class/interface semantics after all declarations are collected
    checkClassSemantics();

    // Pass 2: Analyze bodies
    analyzeBodies(prog);

    return !hasError_;
}

bool SemanticAnalyzer::analyze(Unit &unit)
{
    // Import symbols from interface-level uses
    if (!unit.usedUnits.empty())
    {
        importUnits(unit.usedUnits);
    }

    // Pass 1: Collect declarations (interface + implementation)
    collectDeclarations(unit);

    // Import symbols from implementation-level uses (after interface decls)
    if (!unit.implUsedUnits.empty())
    {
        importUnits(unit.implUsedUnits);
    }

    // Check class/interface semantics after all declarations are collected
    checkClassSemantics();

    // Pass 2: Analyze bodies
    analyzeBodies(unit);

    // Extract and register this unit's exports for other units to use
    UnitInfo exports = extractUnitExports(unit);
    registerUnit(exports);

    return !hasError_;
}

} // namespace il::frontends::pascal

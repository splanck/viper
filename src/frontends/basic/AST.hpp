//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file serves as the umbrella header for the BASIC frontend Abstract Syntax
// Tree (AST) representation.
//
// The AST is the central data structure in the BASIC compilation pipeline:
//   Lexer → Parser → AST → Semantic → Lowerer → IL
//
// Purpose:
// This header aggregates all AST node type families into a single convenient
// include point, providing a complete view of the BASIC AST structure.
//
// AST Organization:
// The BASIC AST is organized into several node families:
// - DeclNodes: Declarations (Program, DimDecl, ProcedureDecl, FunctionDecl,
//   TypeDecl, NamespaceDecl, UsingDecl)
// - ExprNodes: Expressions (literals, identifiers, operators, function calls)
// - StmtNodes: Statements (assignments, control flow, I/O operations)
// - StmtBase/StmtControl/StmtDecl/StmtExpr: Statement base classes and variants
//
// Design Philosophy:
// - AST nodes are immutable after construction, ensuring thread-safety and
//   simplifying analysis passes
// - Nodes are allocated on the heap and managed via std::unique_ptr to enable
//   polymorphic storage and clear ownership semantics
// - Each node carries source location information for precise diagnostic reporting
// - The AST preserves source-level structure (statement ordering, nesting) before
//   semantic transformations
//
// Integration:
// - Produced by: Parser::parseProgram()
// - Consumed by: SemanticAnalyzer for validation and symbol resolution
// - Transformed by: Lowerer to generate IL instructions
// - Analyzed by: Various passes (type checking, control flow analysis)
//
// This umbrella header maintains backward compatibility for code that includes
// "AST.hpp" to access all node types.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/BasicTypes.hpp"
#include "frontends/basic/ast/DeclNodes.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include "frontends/basic/ast/NodeFwd.hpp"
#include "frontends/basic/ast/StmtNodesAll.hpp"

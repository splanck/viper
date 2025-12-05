//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/LowererFwd.hpp
// Purpose: Forward declarations for the BASIC lowering subsystem.
//
// This header provides minimal forward declarations that allow other headers
// to reference lowering types without including the full Lowerer.hpp. This
// reduces compilation times and header dependencies.
//
// Usage:
//   #include "frontends/basic/LowererFwd.hpp"
//   void someFunction(il::frontends::basic::Lowerer &lowerer);
//
//===----------------------------------------------------------------------===//
#pragma once

namespace il::frontends::basic
{

// Main lowerer class
class Lowerer;

// Statement lowering subsystems
class StatementLowerer;
class IoStatementLowerer;
class ControlStatementLowerer;
class RuntimeStatementLowerer;

// Expression lowering visitors
class LowererExprVisitor;
class LowererStmtVisitor;

// Pipeline stages
struct ProgramLowering;
struct ProcedureLowering;
struct StatementLowering;

// Expression lowering helpers
struct LogicalExprLowering;
struct NumericExprLowering;
struct BuiltinExprLowering;

// Select/Case lowering
class SelectCaseLowering;

// Diagnostic integration
class DiagnosticEmitter;

// Semantic analysis integration
class SemanticAnalyzer;

// OOP lowering
struct OopLoweringContext;
class OopEmitHelper;

// Emission helpers
class Emit;
class RuntimeCallBuilder;
class TypeCoercionEngine;

// Overflow and signedness policies
enum class OverflowPolicy;
enum class Signedness;

// Nested namespaces
namespace builtins
{
class LowerCtx;
} // namespace builtins

namespace lower
{
class Emitter;
class BuiltinLowerContext;

namespace common
{
class CommonLowering;
} // namespace common

namespace detail
{
class ExprTypeScanner;
class RuntimeNeedsScanner;

// Modular lowering helper classes
class ExprLoweringHelper;
class ControlLoweringHelper;
class OopLoweringHelper;
class RuntimeLoweringHelper;
} // namespace detail

} // namespace lower

} // namespace il::frontends::basic

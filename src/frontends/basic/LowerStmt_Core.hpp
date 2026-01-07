//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Declares core statement-lowering helpers shared across categories.
/// @details These helpers implement the common lowering steps for statement
///          lists, statement-level calls, and return statements, along with
///          shared runtime utilities such as channel normalization and error
///          checks. The declarations correspond to methods on @ref Lowerer and
///          centralize logic that is reused by multiple statement categories.
//
//===----------------------------------------------------------------------===//

#pragma once

/// @brief Lower a list of statements in sequence.
/// @details Iterates over the statement list, skips null entries, and stops when
///          the current block becomes terminated to avoid emitting unreachable
///          IL. Each statement is lowered via the standard statement visitor.
/// @param stmt Statement list to lower.
void lowerStmtList(const StmtList &stmt);

/// @brief Lower a statement-level procedure invocation.
/// @details Handles CALL statements by resolving the callee, performing argument
///          lowering and coercions, and emitting the call. For runtime helpers
///          and method calls it selects the correct lowering path and discards
///          results when the call is used in statement position.
/// @param stmt CALL statement node containing the invocation expression.
void lowerCallStmt(const CallStmt &stmt);

/// @brief Lower a RETURN statement.
/// @details Distinguishes between procedure returns and GOSUB returns, emits
///          the appropriate control flow, and enforces function return type
///          rules (including boolean coercion and pointer returns).
/// @param stmt RETURN statement node.
void lowerReturn(const ReturnStmt &stmt);

/// @brief Normalize a BASIC channel operand to a 32-bit integer.
/// @details Accepts either 32-bit or 64-bit integer r-values and inserts the
///          required narrowing conversion so runtime helpers observe the
///          canonical i32 channel representation.
/// @param channel Lowered channel operand.
/// @param loc Source location for emitted instructions.
/// @return Normalized r-value carrying the i32 type.
RVal normalizeChannelToI32(RVal channel, il::support::SourceLoc loc);

/// @brief Emit a runtime error check and failure handler branch.
/// @details Creates failure/continuation blocks derived from @p labelStem,
///          branches on a non-zero error flag, and invokes @p onFailure in the
///          failure block to emit diagnostics or cleanup. Control resumes in the
///          continuation block when no error occurs.
/// @param err Runtime error flag to inspect.
/// @param loc Source location for emitted instructions.
/// @param labelStem Prefix used for naming helper blocks.
/// @param onFailure Callback invoked to emit failure handling code.
void emitRuntimeErrCheck(Value err,
                         il::support::SourceLoc loc,
                         std::string_view labelStem,
                         const std::function<void(Value)> &onFailure);

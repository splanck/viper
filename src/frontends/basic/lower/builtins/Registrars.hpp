//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Declares the registration entry points for BASIC builtin lowering families.
// Domain-specific translation units expose a single function that binds the
// appropriate handlers into the global builtin registry.  This keeps
// BuiltinUtils.cpp lightweight while allowing each family to evolve
// independently.
//
//===----------------------------------------------------------------------===//

#pragma once

namespace il::frontends::basic::lower::builtins
{
/// @brief Populate the builtin registry with the core scalar operations.
/// @details Installs handlers that implement arithmetic, comparison, and
///          control-flow helper intrinsics required by every BASIC program.
///          The routine is idempotent and can be invoked by both production
///          lowering and targeted unit tests without duplicating entries.
void registerDefaultBuiltins();

/// @brief Register builtin handlers that operate on BASIC strings.
/// @details Binds lowering callbacks for concatenation, substring, trim, and
///          case conversion intrinsics.  Each handler maps the high-level BASIC
///          semantics onto the runtime's string helper functions so IL
///          generation remains consistent.
void registerStringBuiltins();

/// @brief Register conversion intrinsic handlers such as VAL and STR$.
/// @details Extends the builtin registry with routines that convert between
///          numeric and textual forms, including locale-stable parsing helpers.
///          These handlers rely on the runtime conversion layer to surface
///          familiar BASIC error codes when conversions fail.
void registerConversionBuiltins();

/// @brief Register math intrinsic lowering handlers.
/// @details Adds lowering callbacks for trigonometric, exponential, and other
///          math intrinsics.  Each handler wires the BASIC intrinsic into the
///          runtime's deterministic math implementations while preserving the
///          language's domain and error semantics.
void registerMathBuiltins();

/// @brief Register array helper intrinsics such as LBOUND/UBOUND.
/// @details Installs handlers that translate BASIC array metadata queries and
///          mutation helpers into runtime calls.  Lowering uses the bindings to
///          emit IL that manipulates the reference-counted array representation
///          shared across front-end features.
void registerArrayBuiltins();

/// @brief Register file and console I/O intrinsics.
/// @details Binds handlers for OPEN, CLOSE, INPUT#, PRINT#, and related
///          operations.  The lowering routines convert BASIC I/O semantics into
///          runtime calls that manage channels, file handles, and formatted
///          output consistently across interpreters and native back ends.
void registerIoBuiltins();
} // namespace il::frontends::basic::lower::builtins

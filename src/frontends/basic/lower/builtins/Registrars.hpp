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
void registerDefaultBuiltins();
void registerStringBuiltins();
void registerConversionBuiltins();
void registerMathBuiltins();
void registerArrayBuiltins();
void registerIoBuiltins();
} // namespace il::frontends::basic::lower::builtins

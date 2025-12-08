//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/Common.hpp
// Purpose: Aggregate header for common frontend infrastructure.
//
// This header includes all shared components used by language frontends.
// Include this for convenient access to the common library.
//
//===----------------------------------------------------------------------===//
#pragma once

// Core types
#include "frontends/common/ExprResult.hpp"

// Block and control flow management
#include "frontends/common/BlockManager.hpp"
#include "frontends/common/LoopContext.hpp"

// Instruction emission
#include "frontends/common/InstructionEmitter.hpp"

// String utilities
#include "frontends/common/StringHash.hpp"
#include "frontends/common/StringTable.hpp"

// Type utilities
#include "frontends/common/TypeUtils.hpp"

// Scope management
#include "frontends/common/ScopeTracker.hpp"

// Character utilities for lexers
#include "frontends/common/CharUtils.hpp"

// Number parsing utilities for lexers
#include "frontends/common/NumberParsing.hpp"

// Diagnostic formatting helpers
#include "frontends/common/DiagnosticHelpers.hpp"

// Constant folding utilities
#include "frontends/common/ConstantFolding.hpp"

// Name mangling utilities
#include "frontends/common/NameMangler.hpp"

namespace il::frontends::common
{

// Convenience type aliases that frontends can use

/// @brief Type alias for IL types.
using Type = il::core::Type;

/// @brief Type alias for IL values.
using Value = il::core::Value;

/// @brief Type alias for IL opcodes.
using Opcode = il::core::Opcode;

/// @brief Type alias for IL basic blocks.
using BasicBlock = il::core::BasicBlock;

/// @brief Type alias for IL functions.
using Function = il::core::Function;

/// @brief Type alias for IL modules.
using Module = il::core::Module;

} // namespace il::frontends::common

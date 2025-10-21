//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Declares the helper routine powering the standalone `il-verify` CLI.  The
// entry point is factored into a separate unit so tests can exercise the
// verification pipeline with a custom SourceManager configuration.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Exposes the reusable verification pipeline behind the `il-verify`
///        executable.
/// @details The helper wires together the module loader, verifier, and output
///          reporting.  It accepts an injected SourceManager to allow tests to
///          preconfigure file identifiers and to observe the tool's behaviour
///          without spawning the full CLI.

#pragma once

#include "support/source_manager.hpp"

#include <iosfwd>
#include <string_view>

namespace il::tools::verify
{

/// @brief Run the il-verify pipeline for @p path using @p sm.
///
/// @param path Filesystem path to the textual IL module.
/// @param out Stream receiving success messages ("OK\n").
/// @param err Stream receiving loader or verifier diagnostics.
/// @param sm Source manager responsible for tracking file identifiers.
/// @return True when parsing and verification succeed.
bool runVerificationPipeline(std::string_view path,
                             std::ostream &out,
                             std::ostream &err,
                             il::support::SourceManager &sm);

} // namespace il::tools::verify


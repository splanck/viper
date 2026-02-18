//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file il/transform/AnalysisIDs.hpp
/// @brief Named constants for all well-known IL analysis identifiers.
///
/// @details Analysis results are registered and retrieved from the
///          @ref AnalysisManager by a string key.  Using raw string literals at
///          every call site creates a risk of silent typos: a misspelled key
///          causes the cache to miss on every query with no compile-time
///          diagnostic.
///
///          This header centralises the canonical spelling of every built-in
///          analysis ID so that:
///            - A typo becomes a compile error (undefined identifier).
///            - Renaming an analysis requires a single change here and a
///              project-wide find/replace of the old identifier.
///            - Readers can immediately see the full set of registered analyses.
///
/// @note Include this header in any file that calls
///       @c AnalysisManager::getFunctionResult or
///       @c AnalysisManager::getModuleResult with a hard-coded key.
///
/// @note The constants use @c const @c char* rather than @c std::string_view so
///       that they implicitly convert to @c const @c std::string& at call sites
///       without an explicit construction step.
///
//===----------------------------------------------------------------------===//

#pragma once

namespace il::transform
{

//===----------------------------------------------------------------------===//
// Function-level analysis identifiers
//===----------------------------------------------------------------------===//

/// @brief Identifier for the control-flow graph analysis.
/// @see il::transform::CFGInfo
inline constexpr const char *kAnalysisCFG = "cfg";

/// @brief Identifier for the dominator-tree analysis.
/// @see viper::analysis::DomTree
inline constexpr const char *kAnalysisDominators = "dominators";

/// @brief Identifier for the post-dominator-tree analysis.
/// @see viper::analysis::PostDomTree
inline constexpr const char *kAnalysisPostDominators = "post-dominators";

/// @brief Identifier for the loop-information analysis.
/// @see il::transform::LoopInfo
inline constexpr const char *kAnalysisLoopInfo = "loop-info";

/// @brief Identifier for the liveness analysis.
/// @see il::transform::LivenessInfo
inline constexpr const char *kAnalysisLiveness = "liveness";

/// @brief Identifier for the basic alias analysis.
/// @see viper::analysis::BasicAA
inline constexpr const char *kAnalysisBasicAA = "basic-aa";

/// @brief Identifier for the Memory SSA analysis.
/// @see viper::analysis::MemorySSA
inline constexpr const char *kAnalysisMemorySSA = "memory-ssa";

} // namespace il::transform

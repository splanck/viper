//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/BasicCompletion.hpp
// Purpose: Code-completion engine for the Viper BASIC language.
// Key invariants:
//   - Reuses same CompletionKind/CompletionItem types as Zia
//   - One-entry LRU cache keyed by FNV-1a source hash plus file path
//   - Provider architecture: keywords, snippets, builtins, scope symbols, members
// Ownership/Lifetime:
//   - CompletionEngine owns the cache (BasicAnalysisResult)
//   - All returned items are fully owned
// Links: frontends/basic/BasicAnalysis.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/BasicAnalysis.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::basic {

/// @brief Category of a completion item (matches Zia's CompletionKind for LSP mapping).
enum class CompletionKind : uint8_t {
    Keyword = 0,
    Snippet = 1,
    Variable = 2,
    Parameter = 3,
    Field = 4,
    Method = 5,
    Function = 6,
    Entity = 7, // Class
    Value = 8,
    Interface = 9,
    Module = 10,
    RuntimeClass = 11,
    Property = 12,
};

/// @brief A single code-completion suggestion.
struct CompletionItem {
    std::string label;      ///< Text shown in the popup list
    std::string insertText; ///< Text inserted into the editor buffer
    CompletionKind kind{CompletionKind::Variable};
    std::string detail;    ///< Type/signature shown right-aligned
    int sortPriority{100}; ///< Lower = ranked higher
};

/// @brief Stateful code-completion engine for BASIC source files.
///
/// `complete()` accepts a full source file and a 1-based cursor position,
/// returning ranked suggestions. A one-entry LRU cache avoids re-parsing
/// on consecutive keystrokes.
class BasicCompletionEngine {
  public:
    BasicCompletionEngine();
    ~BasicCompletionEngine();

    /// @brief Compute completions for source at (line, col).
    /// @param source     Full source text of the file being edited.
    /// @param line       1-based line number of the cursor.
    /// @param col        1-based column of the cursor.
    /// @param filePath   Virtual path for SourceManager.
    /// @param maxResults Maximum items returned (0 = unlimited).
    /// @return           Filtered, ranked completion items.
    std::vector<CompletionItem> complete(std::string_view source,
                                         int line,
                                         int col,
                                         std::string_view filePath = "<editor>",
                                         int maxResults = 50);

    /// @brief Discard the cached analysis result.
    void clearCache();

  private:
    /// @brief Trigger kind for completion dispatch.
    enum class TriggerKind : uint8_t {
        CtrlSpace,    ///< Explicit request — provide all in-scope symbols
        MemberAccess, ///< Dot ('.') — enumerate members of LHS object
    };

    /// @brief Parsed context at the completion cursor.
    struct Context {
        TriggerKind trigger{TriggerKind::CtrlSpace};
        std::string triggerExpr; ///< Expression left of '.'
        std::string prefix;      ///< Typed prefix after trigger
    };

    /// @brief Extract completion context from source at (line, col).
    Context extractContext(std::string_view src, int line, int col) const;

    // --- Providers ---
    // Each provider contributes one category of suggestions; complete() merges
    // their outputs before post-processing. All filter against @p prefix.

    /// @brief Suggest BASIC language keywords matching @p prefix.
    std::vector<CompletionItem> provideKeywords(const std::string &prefix) const;
    /// @brief Suggest multi-line code snippets (e.g. FOR/NEXT skeletons).
    std::vector<CompletionItem> provideSnippets(const std::string &prefix) const;
    /// @brief Suggest intrinsic/builtin function names matching @p prefix.
    std::vector<CompletionItem> provideBuiltins(const std::string &prefix) const;
    /// @brief Suggest symbols visible in scope at the cursor (vars, procs, params).
    /// @param sema Analyzer holding the resolved symbol tables to enumerate.
    std::vector<CompletionItem> provideScopeSymbols(const SemanticAnalyzer &sema,
                                                    const std::string &prefix) const;
    /// @brief Suggest members of the object left of a `.` member-access trigger.
    /// @param sema Analyzer used to resolve the trigger expression's type.
    /// @param ctx Parsed completion context describing the trigger expression.
    std::vector<CompletionItem> provideMemberCompletions(const SemanticAnalyzer &sema,
                                                         const Context &ctx) const;
    /// @brief Suggest members of a runtime class resolved purely by name.
    /// @param className Fully-qualified runtime class (e.g. "Viper.String").
    std::vector<CompletionItem> provideRuntimeMembers(const std::string &className,
                                                      const std::string &prefix) const;

    // --- Post-processing ---

    /// @brief Drop items whose label does not match @p prefix (case-insensitive).
    void filterByPrefix(std::vector<CompletionItem> &items, const std::string &prefix) const;
    /// @brief Stable-sort items by prefix affinity then @ref CompletionItem::sortPriority.
    void rank(std::vector<CompletionItem> &items, const std::string &prefix) const;
    /// @brief Remove duplicate suggestions sharing the same label and kind.
    void deduplicate(std::vector<CompletionItem> &items) const;

    // --- Cache ---

    /// @brief FNV-1a 64-bit hash used to key the one-entry analysis cache.
    static uint64_t fnv1a(std::string_view data);

    /// @brief One-entry LRU cache of the last analyzed source buffer.
    struct Cache {
        uint64_t hash{0};                            ///< FNV-1a hash of cached source
        std::string filePath;                        ///< Path the cached result was analyzed under
        std::unique_ptr<BasicAnalysisResult> result; ///< Cached parse+sema result (may be null)
    };

    Cache cache_; ///< Last analysis result, reused across keystrokes
    std::unique_ptr<il::support::SourceManager> sm_; ///< Source manager backing cached analyses
};

} // namespace il::frontends::basic

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file ZiaCompletion.hpp
/// @brief C++ code-completion engine for the Zia language.
///
/// @details Provides `CompletionEngine`, a stateful object that accepts raw
/// source text and a cursor position and returns ranked `CompletionItem`
/// suggestions suitable for display in an IntelliSense-style popup.
///
/// ## Architecture
///
/// ```
///  source + (line,col)
///       │
///       ▼
///  extractContext()   ← backward scan: detect trigger, collect prefix
///       │
///       ▼
///  parseAndAnalyze()  ← error-tolerant Zia pipeline (stages 1–4 only)
///       │             ← one-entry LRU cache keyed by FNV-1a source hash
///       ▼
///  provider dispatch  ← per TriggerKind (MemberAccess / CtrlSpace / etc.)
///       │
///       ▼
///  filterByPrefix()   ← remove non-matching items
///  rank()             ← sort by relevance (exact > prefix > contains)
///       │
///       ▼
///  vector<CompletionItem>  ← serializable to tab-delimited text
/// ```
///
/// ## Serialization
///
/// `serialize(items)` returns a newline-terminated string of tab-delimited
/// records, one per item:
///
///   label TAB insertText TAB kindInt TAB detail NEWLINE
///
/// `kind` integers: Keyword=0 Snippet=1 Variable=2 Parameter=3 Field=4
/// Method=5 Function=6 Entity=7 Value=8 Interface=9 Module=10
/// RuntimeClass=11 Property=12
///
/// @see ZiaAnalysis.hpp — parseAndAnalyze() (error-tolerant partial compile)
/// @see Sema.hpp        — symbol enumeration APIs used by providers
///
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/zia/ZiaAnalysis.hpp" // AnalysisResult, parseAndAnalyze()
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::zia
{

//===----------------------------------------------------------------------===//
/// @name Public data types
/// @{
//===----------------------------------------------------------------------===//

/// @brief Category of a completion item (maps to an icon in the UI).
enum class CompletionKind : uint8_t
{
    Keyword      = 0,
    Snippet      = 1,
    Variable     = 2,
    Parameter    = 3,
    Field        = 4,
    Method       = 5,
    Function     = 6,
    Entity       = 7,
    Value        = 8,
    Interface    = 9,
    Module       = 10,
    RuntimeClass = 11,
    Property     = 12,
};

/// @brief A single code-completion suggestion.
struct CompletionItem
{
    std::string    label;        ///< Text shown in the popup list
    std::string    insertText;   ///< Text inserted into the editor buffer
    CompletionKind kind{CompletionKind::Variable};
    std::string    detail;       ///< Type/signature shown right-aligned in popup
    int            sortPriority{100}; ///< Lower = ranked higher
};

/// @brief Serialize a list of items to tab-delimited text for the runtime bridge.
/// @details Format per line: label\tinsertText\tkindInt\tdetail\n
/// @param items  Completion items (already filtered and ranked).
/// @return       Newline-terminated serialized string.
std::string serialize(const std::vector<CompletionItem> &items);

/// @}

//===----------------------------------------------------------------------===//
/// @name CompletionEngine
/// @{
//===----------------------------------------------------------------------===//

/// @brief Stateful code-completion engine for Zia source files.
///
/// @details `complete()` is the primary entry point.  It accepts a full source
/// file (as a string) and a 1-based line / 0-based column position, and
/// returns up to `maxResults` ranked suggestions.
///
/// A one-entry LRU cache avoids re-parsing the same file on consecutive
/// keystrokes.  The cache is keyed by an FNV-1a hash of the source bytes,
/// so any edit invalidates it.
///
/// ## Thread safety
///
/// Not thread-safe.  Each IDE connection should have its own engine instance.
class CompletionEngine
{
  public:
    CompletionEngine();
    ~CompletionEngine();

    /// @brief Compute completions for source at (line, col).
    /// @param source     Full source text of the file being edited.
    /// @param line       1-based line number of the cursor.
    /// @param col        0-based column of the cursor (chars from start of line).
    /// @param filePath   Virtual path used when registering with SourceManager.
    /// @param maxResults Maximum number of items returned (0 = unlimited).
    /// @return           Filtered, ranked completion items.
    std::vector<CompletionItem> complete(std::string_view source,
                                         int              line,
                                         int              col,
                                         std::string_view filePath  = "<editor>",
                                         int              maxResults = 50);

    /// @brief Discard the cached AnalysisResult (forces re-parse next call).
    void clearCache();

  private:
    //=========================================================================
    /// @name Context extraction
    /// @{
    //=========================================================================

    /// @brief Describes what triggered the completion request.
    enum class TriggerKind : uint8_t
    {
        CtrlSpace,    ///< Explicit request — provide all in-scope symbols
        MemberAccess, ///< Dot ('.') — enumerate members of LHS type
        AfterNew,     ///< 'new ' keyword — provide constructible type names
        AfterColon,   ///< ': ' in a type annotation — provide type names
        AfterReturn,  ///< 'return ' — provide scope symbols + keywords
    };

    /// @brief Parsed context at the completion cursor.
    struct Context
    {
        TriggerKind trigger{TriggerKind::CtrlSpace};
        /// Expression to the left of '.', e.g. "shell.app" for "shell.app.X"
        std::string triggerExpr;
        /// Identifier chars typed after the trigger (may be empty)
        std::string prefix;
        /// Column at which prefix begins (insertion point for replacement)
        int replaceStart{0};
    };

    /// @brief Extract completion context from source at (line, col).
    Context extractContext(std::string_view src, int line, int col) const;

    /// @}
    //=========================================================================
    /// @name Providers
    /// @{
    //=========================================================================

    std::vector<CompletionItem> provideKeywords(const std::string &prefix) const;
    std::vector<CompletionItem> provideSnippets(const std::string &prefix) const;

    std::vector<CompletionItem> provideScopeSymbols(const Sema        &sema,
                                                     const std::string &prefix) const;

    std::vector<CompletionItem> provideMemberCompletions(const Sema    &sema,
                                                          const Context &ctx) const;

    std::vector<CompletionItem> provideTypeNames(const Sema        &sema,
                                                  const std::string &prefix) const;

    std::vector<CompletionItem> provideModuleMembers(const Sema        &sema,
                                                      const std::string &moduleAlias,
                                                      const std::string &prefix) const;

    std::vector<CompletionItem> provideRuntimeMembers(const Sema        &sema,
                                                       const std::string &fullClassName,
                                                       const std::string &prefix) const;

    /// @brief Enumerate classes that are direct children of a runtime namespace.
    /// @details For example, with nsPrefix="Viper.GUI", this returns items for
    ///          Canvas, App, ListBox, FloatingPanel, etc.  Handles user typing
    ///          a module alias followed by a dot (e.g. "GUI.").
    /// @param nsPrefix  Full dotted namespace path (e.g. "Viper.GUI").
    /// @param prefix    Typed prefix filter (case-insensitive).
    std::vector<CompletionItem> provideNamespaceMembers(const Sema        &sema,
                                                         const std::string &nsPrefix,
                                                         const std::string &prefix) const;

    /// @}
    //=========================================================================
    /// @name Type resolution
    /// @{
    //=========================================================================

    /// @brief Resolve the Zia TypeRef for a dotted expression string.
    /// @details Walks the expression step-by-step via global symbols and member
    /// types.  For example "shell.app" first resolves `shell` from globals,
    /// then looks up field `app` on the resulting type.
    /// @return TypeRef (may be unknown if resolution fails).
    TypeRef resolveExprType(const Sema &sema, const std::string &expr) const;

    /// @}
    //=========================================================================
    /// @name Post-processing
    /// @{
    //=========================================================================

    void filterByPrefix(std::vector<CompletionItem> &items,
                        const std::string           &prefix) const;

    void rank(std::vector<CompletionItem> &items, const std::string &prefix) const;

    void deduplicate(std::vector<CompletionItem> &items) const;

    /// @}
    //=========================================================================
    /// @name Cache
    /// @{
    //=========================================================================

    /// @brief FNV-1a hash of a string (fast, ~1µs for 10 KB).
    static uint64_t fnv1a(std::string_view data);

    struct Cache
    {
        uint64_t                        hash{0};
        std::unique_ptr<AnalysisResult> result;
    };

    Cache                                cache_;
    std::unique_ptr<il::support::SourceManager> sm_;

    /// @}
};

/// @}

} // namespace il::frontends::zia

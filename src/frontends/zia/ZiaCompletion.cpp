//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file ZiaCompletion.cpp
/// @brief Implementation of the Zia code-completion engine.
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/ZiaCompletion.hpp"
#include "frontends/zia/Sema.hpp"
#include "support/source_manager.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <unordered_set>

namespace il::frontends::zia
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool isIdentChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/// @brief Map a Symbol::Kind to the corresponding CompletionKind.
static CompletionKind kindFromSymbol(const Symbol &sym)
{
    switch (sym.kind)
    {
        case Symbol::Kind::Variable:
            return CompletionKind::Variable;
        case Symbol::Kind::Parameter:
            return CompletionKind::Parameter;
        case Symbol::Kind::Function:
            return CompletionKind::Function;
        case Symbol::Kind::Method:
            return CompletionKind::Method;
        case Symbol::Kind::Field:
            // getRuntimeMembers() encodes RT properties as Kind::Field with isExtern=true.
            return sym.isExtern ? CompletionKind::Property : CompletionKind::Field;
        case Symbol::Kind::Type:
            return CompletionKind::Entity;
        case Symbol::Kind::Module:
            return CompletionKind::Module;
    }
    return CompletionKind::Variable;
}

/// @brief Build a human-readable detail string for a symbol's type.
static std::string typeDetail(const TypeRef &type)
{
    if (!type)
        return {};
    return type->name.empty() ? type->toString() : type->name;
}

/// @brief Convert a CompletionItem to its tab-delimited serialized form.
static std::string serializeItem(const CompletionItem &item)
{
    return item.label + '\t' + item.insertText + '\t' +
           std::to_string(static_cast<int>(item.kind)) + '\t' + item.detail + '\n';
}

// ---------------------------------------------------------------------------
// serialize
// ---------------------------------------------------------------------------

std::string serialize(const std::vector<CompletionItem> &items)
{
    std::string out;
    out.reserve(items.size() * 40);
    for (const auto &item : items)
        out += serializeItem(item);
    return out;
}

// ---------------------------------------------------------------------------
// FNV-1a hash
// ---------------------------------------------------------------------------

uint64_t CompletionEngine::fnv1a(std::string_view data)
{
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : data)
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// ---------------------------------------------------------------------------
// Static keyword / snippet data
// ---------------------------------------------------------------------------

static const char *const kKeywords[] = {
    // Statement keywords
    "var",
    "func",
    "if",
    "else",
    "while",
    "for",
    "in",
    "return",
    "break",
    "continue",
    "and",
    "or",
    "not",
    "is",
    "as",
    "new",
    "true",
    "false",
    "null",
    "match",
    // Declaration keywords
    "entity",
    "interface",
    "value",
    "expose",
    "module",
    "bind",
    // Built-in types
    "Integer",
    "Number",
    "Boolean",
    "String",
    "Byte",
    "Bytes",
    "List",
    "Map",
    "Set",
    "Object",
    nullptr,
};

struct SnippetData
{
    const char *label;
    const char *insertText;
};

static const SnippetData kSnippets[] = {
    {"if", "if  {\n    \n}"},
    {"if-else", "if  {\n    \n} else {\n    \n}"},
    {"while", "while  {\n    \n}"},
    {"for", "for i in 0..n {\n    \n}"},
    {"for-in", "for item in  {\n    \n}"},
    {"func", "func name() {\n    \n}"},
    {"entity", "entity Name {\n    expose func init() {\n    }\n}"},
    {nullptr, nullptr},
};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

CompletionEngine::CompletionEngine() : sm_(std::make_unique<il::support::SourceManager>()) {}

CompletionEngine::~CompletionEngine() = default;

void CompletionEngine::clearCache()
{
    cache_.hash = 0;
    cache_.result = nullptr;
    // Recreate SourceManager so file IDs are fresh.
    sm_ = std::make_unique<il::support::SourceManager>();
}

// ---------------------------------------------------------------------------
// Context extraction
// ---------------------------------------------------------------------------

CompletionEngine::Context CompletionEngine::extractContext(std::string_view src,
                                                           int line,
                                                           int col) const
{
    Context ctx;

    // Find the start of the requested line (1-based).
    size_t lineStart = 0;
    int curLine = 1;
    for (size_t i = 0; i < src.size() && curLine < line; ++i)
    {
        if (src[i] == '\n')
        {
            ++curLine;
            lineStart = i + 1;
        }
    }

    // Extract line text up to col (clamp to line length).
    size_t lineEnd = lineStart;
    while (lineEnd < src.size() && src[lineEnd] != '\n')
        ++lineEnd;

    size_t cursorOff = lineStart + static_cast<size_t>(col);
    if (cursorOff > lineEnd)
        cursorOff = lineEnd;

    std::string_view lineUpToCursor = src.substr(lineStart, cursorOff - lineStart);

    // ── Step 1: collect identifier prefix (chars user has already typed) ────
    int prefixLen = 0;
    for (int i = static_cast<int>(lineUpToCursor.size()) - 1;
         i >= 0 && isIdentChar(lineUpToCursor[i]);
         --i)
    {
        ++prefixLen;
    }
    ctx.prefix = std::string(lineUpToCursor.substr(lineUpToCursor.size() - prefixLen));
    ctx.replaceStart = col - prefixLen;

    // Position just before the prefix starts.
    int triggerPos = static_cast<int>(lineUpToCursor.size()) - prefixLen - 1;

    // ── Step 2: detect trigger ───────────────────────────────────────────────
    if (triggerPos >= 0 && lineUpToCursor[triggerPos] == '.')
    {
        ctx.trigger = TriggerKind::MemberAccess;

        // Collect the expression to the left of '.': scan backward through
        // identifier chars and embedded dots (for chained access like a.b.c).
        int exprEnd = triggerPos;
        int exprStart = exprEnd - 1;
        while (exprStart >= 0 &&
               (isIdentChar(lineUpToCursor[exprStart]) || lineUpToCursor[exprStart] == '.'))
        {
            --exprStart;
        }
        ++exprStart;
        if (exprStart < exprEnd)
        {
            ctx.triggerExpr = std::string(lineUpToCursor.substr(exprStart, exprEnd - exprStart));
        }
    }
    else
    {
        // Check for keyword triggers by looking at the word just before the prefix.
        // We need at least 4 chars before to match "new " or "return ".
        std::string_view before = lineUpToCursor.substr(0, lineUpToCursor.size() - prefixLen);

        auto endsWith = [](std::string_view sv, const char *suffix) -> bool
        {
            size_t n = std::strlen(suffix);
            return sv.size() >= n && sv.substr(sv.size() - n) == suffix;
        };

        if (endsWith(before, "new "))
            ctx.trigger = TriggerKind::AfterNew;
        else if (endsWith(before, "return "))
            ctx.trigger = TriggerKind::AfterReturn;
        else if (triggerPos >= 0 && lineUpToCursor[triggerPos] == ':')
            ctx.trigger = TriggerKind::AfterColon;
        else
            ctx.trigger = TriggerKind::CtrlSpace;
    }

    return ctx;
}

// ---------------------------------------------------------------------------
// Type resolution for dotted expressions
// ---------------------------------------------------------------------------

TypeRef CompletionEngine::resolveExprType(const Sema &sema, const std::string &expr) const
{
    if (expr.empty())
        return nullptr;

    // Split on '.'
    std::vector<std::string> parts;
    std::string token;
    for (char c : expr)
    {
        if (c == '.')
        {
            if (!token.empty())
                parts.push_back(token);
            token.clear();
        }
        else
        {
            token += c;
        }
    }
    if (!token.empty())
        parts.push_back(token);

    if (parts.empty())
        return nullptr;

    // Look up the first part in global symbols.
    TypeRef current;
    auto globals = sema.getGlobalSymbols();
    for (const auto &sym : globals)
    {
        if (sym.name == parts[0])
        {
            current = sym.type;
            // For Type symbols, the symbol's *type* is a metatype — the actual
            // instance type is what we need for member access.  Use as-is;
            // getMembersOf handles Entity/Value/Ptr kinds.
            break;
        }
    }

    if (!current)
    {
        // parts[0] not found as a Zia symbol. Try alias expansion:
        // e.g. "GUI.Canvas" → alias "GUI" resolves to "Viper.GUI"
        //      → reconstruct qname "Viper.GUI.Canvas"
        std::string ns = sema.resolveModuleAlias(parts[0]);
        if (!ns.empty() && parts.size() > 1)
        {
            std::string fullQname = ns;
            for (size_t i = 1; i < parts.size(); ++i)
                fullQname += "." + parts[i];
            // Return as runtimeClass (Ptr+name) so getMembersOf delegates to getRuntimeMembers.
            if (!sema.getRuntimeMembers(fullQname).empty())
                return types::runtimeClass(fullQname);
        }
        // Last resort: treat the entire expr as a literal runtime class qname
        // (e.g. "Viper.GUI.Canvas" typed without a binding alias).
        if (!sema.getRuntimeMembers(expr).empty())
            return types::runtimeClass(expr);
        return nullptr;
    }

    // Walk remaining parts.
    for (size_t i = 1; i < parts.size(); ++i)
    {
        // When current is a Module type (from a namespace alias like "bind GUI = Viper.GUI"),
        // getMembersOf returns nothing useful.  Instead, reconstruct the full class qname by
        // appending the remaining parts to the module's namespace name.
        if (current->kind == TypeKindSem::Module && !current->name.empty())
        {
            std::string fullQname = current->name;
            for (size_t j = i; j < parts.size(); ++j)
                fullQname += "." + parts[j];
            if (!sema.getRuntimeMembers(fullQname).empty())
                return types::runtimeClass(fullQname);
            return nullptr;
        }

        auto members = sema.getMembersOf(current);
        bool found = false;
        for (const auto &mem : members)
        {
            if (mem.name == parts[i])
            {
                // For method symbols, the type is a function type; we want the
                // return type for further member chaining.
                if (mem.type && mem.type->kind == TypeKindSem::Function)
                    current = mem.type->returnType();
                else
                    current = mem.type;
                found = true;
                break;
            }
        }
        if (!found)
            return nullptr;
    }

    return current;
}

// ---------------------------------------------------------------------------
// Providers
// ---------------------------------------------------------------------------

std::vector<CompletionItem> CompletionEngine::provideKeywords(const std::string &prefix) const
{
    std::vector<CompletionItem> items;
    for (int i = 0; kKeywords[i]; ++i)
    {
        CompletionItem item;
        item.label = kKeywords[i];
        item.insertText = kKeywords[i];
        item.kind = CompletionKind::Keyword;
        item.sortPriority = 50;
        items.push_back(std::move(item));
    }
    filterByPrefix(items, prefix);
    return items;
}

std::vector<CompletionItem> CompletionEngine::provideSnippets(const std::string &prefix) const
{
    std::vector<CompletionItem> items;
    for (int i = 0; kSnippets[i].label; ++i)
    {
        CompletionItem item;
        item.label = kSnippets[i].label;
        item.insertText = kSnippets[i].insertText;
        item.kind = CompletionKind::Snippet;
        item.detail = "snippet";
        item.sortPriority = 60;
        items.push_back(std::move(item));
    }
    filterByPrefix(items, prefix);
    return items;
}

std::vector<CompletionItem> CompletionEngine::provideScopeSymbols(const Sema &sema,
                                                                  const std::string &prefix) const
{
    std::vector<CompletionItem> items;
    auto globals = sema.getGlobalSymbols();
    for (const auto &sym : globals)
    {
        CompletionItem item;
        item.label = sym.name;
        item.insertText = sym.name;
        item.kind = kindFromSymbol(sym);
        item.detail = typeDetail(sym.type);
        item.sortPriority = 10;
        items.push_back(std::move(item));
    }
    filterByPrefix(items, prefix);
    return items;
}

std::vector<CompletionItem> CompletionEngine::provideMemberCompletions(const Sema &sema,
                                                                       const Context &ctx) const
{
    std::vector<CompletionItem> items;
    if (ctx.triggerExpr.empty())
        return items;

    // ── Step 1: split triggerExpr on '.' ────────────────────────────────────
    std::vector<std::string> parts;
    {
        std::string tok;
        for (char c : ctx.triggerExpr)
        {
            if (c == '.')
            {
                if (!tok.empty())
                    parts.push_back(tok);
                tok.clear();
            }
            else
                tok += c;
        }
        if (!tok.empty())
            parts.push_back(tok);
    }
    if (parts.empty())
        return items;

    // ── Step 2: check whether the first part is a bound namespace alias ──────
    // e.g. "GUI"        → resolves to "Viper.GUI"
    //      "GUI.Canvas" → parts[0]="GUI" → alias → reconstruct "Viper.GUI.Canvas"
    std::string resolved = sema.resolveModuleAlias(parts[0]);
    if (!resolved.empty())
    {
        if (parts.size() == 1)
        {
            // User typed e.g. "Math." or "GUI." after a namespace alias.
            // Case A: the resolved path IS a class (e.g. "Viper.Math" with Sqrt/Abs/…).
            auto rtMembers = provideRuntimeMembers(sema, resolved, ctx.prefix);
            if (!rtMembers.empty())
                return rtMembers;
            // Case B: the resolved path is a namespace containing classes (e.g. "Viper.GUI").
            return provideNamespaceMembers(sema, resolved, ctx.prefix);
        }

        // Reconstruct full class/sub-namespace qname from alias + remaining parts.
        std::string fullClass = resolved;
        for (size_t i = 1; i < parts.size(); ++i)
            fullClass += "." + parts[i];

        // Try as a specific runtime class (has methods/properties).
        auto rtMembers = provideRuntimeMembers(sema, fullClass, ctx.prefix);
        if (!rtMembers.empty())
            return rtMembers;

        // Otherwise it may be a sub-namespace — enumerate its child classes.
        return provideNamespaceMembers(sema, fullClass, ctx.prefix);
    }

    // ── Step 3: try the entire triggerExpr as a literal runtime qname ────────
    // This handles bare "Viper.GUI.Canvas." typed without a binding alias.
    {
        auto rtMembers = provideRuntimeMembers(sema, ctx.triggerExpr, ctx.prefix);
        if (!rtMembers.empty())
            return rtMembers;
        auto nsMembers = provideNamespaceMembers(sema, ctx.triggerExpr, ctx.prefix);
        if (!nsMembers.empty())
            return nsMembers;
    }

    // ── Step 4: resolve via expression type (for user-defined entity fields) ─
    TypeRef type = resolveExprType(sema, ctx.triggerExpr);
    if (!type)
        return items;

    auto members = sema.getMembersOf(type);
    for (const auto &sym : members)
    {
        CompletionItem item;
        item.label = sym.name;
        item.insertText = sym.name;
        item.kind = kindFromSymbol(sym);
        item.detail = typeDetail(sym.type);
        item.sortPriority = 5;
        items.push_back(std::move(item));
    }
    filterByPrefix(items, ctx.prefix);
    return items;
}

std::vector<CompletionItem> CompletionEngine::provideTypeNames(const Sema &sema,
                                                               const std::string &prefix) const
{
    std::vector<CompletionItem> items;
    auto names = sema.getTypeNames();
    for (auto &name : names)
    {
        CompletionItem item;
        item.label = name;
        item.insertText = name;
        item.kind = CompletionKind::Entity;
        item.sortPriority = 20;
        items.push_back(std::move(item));
    }
    filterByPrefix(items, prefix);
    return items;
}

std::vector<CompletionItem> CompletionEngine::provideModuleMembers(const Sema &sema,
                                                                   const std::string &moduleAlias,
                                                                   const std::string &prefix) const
{
    std::vector<CompletionItem> items;
    auto exports = sema.getModuleExports(moduleAlias);
    for (const auto &sym : exports)
    {
        CompletionItem item;
        item.label = sym.name;
        item.insertText = sym.name;
        item.kind = kindFromSymbol(sym);
        item.detail = typeDetail(sym.type);
        item.sortPriority = 5;
        items.push_back(std::move(item));
    }
    filterByPrefix(items, prefix);
    return items;
}

std::vector<CompletionItem> CompletionEngine::provideRuntimeMembers(
    const Sema &sema, const std::string &fullClassName, const std::string &prefix) const
{
    std::vector<CompletionItem> items;
    auto members = sema.getRuntimeMembers(fullClassName);
    for (const auto &sym : members)
    {
        CompletionItem item;
        item.label = sym.name;
        item.insertText = sym.name;
        // Distinguish methods (Function type) from properties.
        if (sym.type && sym.type->kind == TypeKindSem::Function)
            item.kind = CompletionKind::Method;
        else
            item.kind = CompletionKind::Property;
        item.detail = typeDetail(sym.type);
        item.sortPriority = 5;
        items.push_back(std::move(item));
    }
    filterByPrefix(items, prefix);
    return items;
}

std::vector<CompletionItem> CompletionEngine::provideNamespaceMembers(
    const Sema &sema, const std::string &nsPrefix, const std::string &prefix) const
{
    std::vector<CompletionItem> items;
    auto classNames = sema.getNamespaceClasses(nsPrefix);
    for (auto &name : classNames)
    {
        CompletionItem item;
        item.label = name;
        item.insertText = name;
        item.kind = CompletionKind::RuntimeClass;
        item.sortPriority = 5;
        items.push_back(std::move(item));
    }
    filterByPrefix(items, prefix);
    return items;
}

// ---------------------------------------------------------------------------
// Filtering, ranking, deduplication
// ---------------------------------------------------------------------------

void CompletionEngine::filterByPrefix(std::vector<CompletionItem> &items,
                                      const std::string &prefix) const
{
    if (prefix.empty())
        return;

    items.erase(
        std::remove_if(items.begin(),
                       items.end(),
                       [&](const CompletionItem &item)
                       {
                           // Case-insensitive prefix match.
                           if (item.label.size() < prefix.size())
                               return true;
                           for (size_t i = 0; i < prefix.size(); ++i)
                           {
                               if (std::tolower(static_cast<unsigned char>(item.label[i])) !=
                                   std::tolower(static_cast<unsigned char>(prefix[i])))
                                   return true;
                           }
                           return false;
                       }),
        items.end());
}

void CompletionEngine::rank(std::vector<CompletionItem> &items, const std::string &prefix) const
{
    if (prefix.empty())
    {
        std::stable_sort(items.begin(),
                         items.end(),
                         [](const CompletionItem &a, const CompletionItem &b)
                         { return a.sortPriority < b.sortPriority; });
        return;
    }

    // Score: 0 = exact, 1 = prefix (case-sensitive), 2 = prefix (insensitive), 3 = other.
    auto score = [&](const CompletionItem &item) -> int
    {
        if (item.label == prefix)
            return 0;
        if (item.label.size() >= prefix.size() && item.label.substr(0, prefix.size()) == prefix)
            return 1;
        return 2;
    };

    std::stable_sort(items.begin(),
                     items.end(),
                     [&](const CompletionItem &a, const CompletionItem &b)
                     {
                         int sa = score(a), sb = score(b);
                         if (sa != sb)
                             return sa < sb;
                         return a.sortPriority < b.sortPriority;
                     });
}

void CompletionEngine::deduplicate(std::vector<CompletionItem> &items) const
{
    std::unordered_set<std::string> seen;
    items.erase(std::remove_if(items.begin(),
                               items.end(),
                               [&](const CompletionItem &item)
                               { return !seen.insert(item.label).second; }),
                items.end());
}

// ---------------------------------------------------------------------------
// Primary entry point
// ---------------------------------------------------------------------------

std::vector<CompletionItem> CompletionEngine::complete(
    std::string_view source, int line, int col, std::string_view filePath, int maxResults)
{
    // ── Cache lookup ─────────────────────────────────────────────────────────
    uint64_t hash = fnv1a(source);
    if (hash != cache_.hash || !cache_.result)
    {
        cache_.hash = 0;
        cache_.result = nullptr;
        sm_ = std::make_unique<il::support::SourceManager>();

        // Keep source and path strings alive for the duration of the parse.
        // CompilerInput holds string_views — the backing strings must outlive it.
        std::string sourceStr(source);
        std::string pathStr(filePath);
        CompilerInput input;
        input.source = sourceStr;
        input.path = pathStr;
        CompilerOptions opts{};

        cache_.result = parseAndAnalyze(input, opts, *sm_);
        if (cache_.result)
            cache_.hash = hash;
    }

    // ── Context extraction ───────────────────────────────────────────────────
    // Always extract context first — does not require a valid sema.
    Context ctx = extractContext(source, line, col);

    // ── Provider dispatch ────────────────────────────────────────────────────
    // Gate sema-dependent providers on hasSema; keywords and snippets always run.
    std::vector<CompletionItem> items;
    bool hasSema = cache_.result && cache_.result->sema;

    switch (ctx.trigger)
    {
        case TriggerKind::MemberAccess:
        {
            if (!hasSema)
                break;
            const Sema &sema = *cache_.result->sema;
            // Member access: enumerate members of the LHS type.
            // Also check if triggerExpr is a bound module alias with dot
            // (e.g. "Viper.Math.Pi" — triggerExpr="Viper.Math", prefix="Pi").
            auto members = provideMemberCompletions(sema, ctx);
            items.insert(items.end(), members.begin(), members.end());
            break;
        }

        case TriggerKind::AfterNew:
        {
            if (!hasSema)
                break;
            const Sema &sema = *cache_.result->sema;
            auto types = provideTypeNames(sema, ctx.prefix);
            items.insert(items.end(), types.begin(), types.end());
            break;
        }

        case TriggerKind::AfterColon:
        {
            if (!hasSema)
                break;
            const Sema &sema = *cache_.result->sema;
            auto types = provideTypeNames(sema, ctx.prefix);
            items.insert(items.end(), types.begin(), types.end());
            // Built-in type keywords
            auto kws = provideKeywords(ctx.prefix);
            for (auto &kw : kws)
            {
                // Filter to just type keywords.
                static const char *const typeKws[] = {
                    "Integer",
                    "Number",
                    "Boolean",
                    "String",
                    "Byte",
                    "Bytes",
                    "List",
                    "Map",
                    "Set",
                    "Object",
                    nullptr,
                };
                bool isType = false;
                for (int i = 0; typeKws[i]; ++i)
                    if (kw.label == typeKws[i])
                    {
                        isType = true;
                        break;
                    }
                if (isType)
                    items.push_back(std::move(kw));
            }
            break;
        }

        case TriggerKind::AfterReturn:
        case TriggerKind::CtrlSpace:
        {
            // Scope symbols and type names require sema.
            if (hasSema)
            {
                const Sema &sema = *cache_.result->sema;
                auto scope = provideScopeSymbols(sema, ctx.prefix);
                items.insert(items.end(), scope.begin(), scope.end());
                auto types = provideTypeNames(sema, ctx.prefix);
                items.insert(items.end(), types.begin(), types.end());
            }
            // Keywords and snippets always available — no sema needed.
            auto kws = provideKeywords(ctx.prefix);
            items.insert(items.end(), kws.begin(), kws.end());
            auto snips = provideSnippets(ctx.prefix);
            items.insert(items.end(), snips.begin(), snips.end());
            break;
        }
    }

    // ── Post-processing ──────────────────────────────────────────────────────
    rank(items, ctx.prefix);
    deduplicate(items);

    if (maxResults > 0 && static_cast<int>(items.size()) > maxResults)
        items.resize(static_cast<size_t>(maxResults));

    return items;
}

} // namespace il::frontends::zia

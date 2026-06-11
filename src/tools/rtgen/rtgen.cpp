//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/rtgen/rtgen.cpp
// Purpose: Build-time generator that produces runtime registry .inc files from
//          the single source of truth: runtime.def
//
// Usage: rtgen <input.def> <output_dir>
//
// Outputs:
//   - RuntimeNameMap.inc     (canonical Viper.* -> rt_* symbol mapping)
//   - RuntimeClasses.inc     (OOP class/method/property catalog)
//   - RuntimeSignatures.inc  (runtime descriptor rows)
//   - RuntimeNames.hpp       (C++ constants for frontend use)
//
// Key invariants:
//   - Parses runtime.def line by line
//   - Validates no duplicate symbols or missing targets
//   - Pure C++17, no external dependencies
//
//===----------------------------------------------------------------------===//

#include "../common/packaging/PkgUtils.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

static constexpr std::uintmax_t kMaxRtgenTextFileBytes = 16ULL * 1024ULL * 1024ULL;

//===----------------------------------------------------------------------===//
// Data Structures
//===----------------------------------------------------------------------===//

/// @brief One RT_FUNC entry from runtime.def: a runtime function and its mapping.
struct RuntimeFunc {
    std::string id;                       // Unique identifier (e.g., "PrintStr")
    std::string c_symbol;                 // C runtime symbol (e.g., "rt_print_str")
    std::string canonical;                // Canonical Viper.* name (e.g., "Viper.Console.PrintStr")
    std::string signature;                // Type signature (e.g., "void(str)")
    std::string lowering;                 // Lowering kind: "always" or "" (default: manual)
    std::vector<std::string> bridgeRoles; // Safe Zia bridge roles: none/callback/payload
};

/// @brief An RT_ALIAS entry: an extra canonical name pointing at an existing function.
struct RuntimeAlias {
    std::string canonical; // Alias canonical name
    std::string target_id; // Target function id
};

/// @brief A property exposed by a runtime class (RT_PROP entry).
struct RuntimeProperty {
    std::string name;      // Property name (e.g., "Length")
    std::string type;      // Property type (e.g., "i64")
    std::string getter_id; // Getter function id (or canonical name)
    std::string setter_id; // Setter function id or "none"
};

/// @brief A method exposed by a runtime class (RT_METHOD entry).
struct RuntimeMethod {
    std::string name;      // Method name (e.g., "Substring")
    std::string signature; // Signature without receiver (e.g., "str(i64,i64)")
    std::string target_id; // Target function id (or canonical name)
};

/// @brief A runtime OOP class (RT_CLASS block) with its properties and methods.
struct RuntimeClass {
    std::string name;                   // Class name (e.g., "Viper.String")
    std::string type_id;                // Type ID suffix (e.g., "String")
    std::string layout;                 // Layout type (e.g., "opaque*", "obj")
    std::string ctor_id;                // Constructor function id or empty
    std::vector<RuntimeProperty> props; // Properties
    std::vector<RuntimeMethod> methods; // Methods
};

/// @brief A C function signature parsed from a runtime header declaration.
struct CSignature {
    std::string returnType;            ///< C return type.
    std::vector<std::string> argTypes; ///< C parameter types, in order.
};

/// @brief Fields used to emit one descriptor row in RuntimeSignatures.inc.
struct DescriptorFields {
    std::string signatureId; ///< Signature identifier.
    std::string spec;        ///< Signature-spec expression.
    std::string handler;     ///< VM handler expression.
    std::string lowering;    ///< Lowering kind.
    std::string hidden;      ///< Hidden-argument expression.
    std::string hiddenCount; ///< Number of hidden arguments.
    std::string trapClass;   ///< Trap classification.
};

/// @brief A runtime function prototype recovered from a runtime header.
struct RuntimePrototype {
    CSignature signature;                ///< Parsed C signature.
    std::vector<std::string> paramNames; ///< Parameter names, in order.
    std::string headerPath;              ///< Header file the declaration came from.
};

/// @brief A class property with canonical getter/setter names resolved.
struct ResolvedRuntimeProperty {
    std::string name;            ///< Property name.
    std::string type;            ///< Property type.
    std::string getterCanonical; ///< Canonical getter name.
    std::string setterCanonical; ///< Canonical setter name, or empty.
};

/// @brief A class method with its target canonical name resolved.
struct ResolvedRuntimeMethod {
    std::string name;            ///< Method name.
    std::string signature;       ///< Signature without receiver.
    std::string targetCanonical; ///< Canonical target function name.
};

/// @brief A runtime class after id references are resolved to canonical names.
struct ResolvedRuntimeClass {
    std::string name;                           ///< Class name.
    std::string type_id;                        ///< Type ID suffix.
    std::string layout;                         ///< Layout type.
    std::string ctorCanonical;                  ///< Canonical constructor name, or empty.
    std::vector<ResolvedRuntimeProperty> props; ///< Resolved properties.
    std::vector<ResolvedRuntimeMethod> methods; ///< Resolved methods.
};

/// @brief Expected runtime surface parsed from the surface-policy file, used by the
///        audit to detect drift between runtime.def and the actual runtime.
struct RuntimeSurfacePolicy {
    std::unordered_set<std::string> internalHeaders; ///< Headers excluded from the surface.
    std::unordered_set<std::string> internalSymbols; ///< Symbols excluded from the surface.
    std::unordered_map<std::string, std::string> expectedFunctions; ///< Required functions.
    std::vector<ResolvedRuntimeMethod> expectedMethods;             ///< Required class methods.
    std::vector<ResolvedRuntimeProperty> expectedProperties;        ///< Required class properties.
};

//===----------------------------------------------------------------------===//
// Parser State
//===----------------------------------------------------------------------===//

/// @brief Mutable parser state accumulated while reading runtime.def.
/// @details Holds the parsed functions/aliases/classes plus validation indices and
///          the current line context used for error/warning reporting.
struct ParseState {
    std::vector<RuntimeFunc> functions; ///< All parsed RT_FUNC entries.
    std::vector<RuntimeAlias> aliases;  ///< All parsed RT_ALIAS entries.
    std::vector<RuntimeClass> classes;  ///< All parsed RT_CLASS blocks.

    // Maps for validation
    std::map<std::string, size_t> func_by_id;        ///< Function id → index in @c functions.
    std::map<std::string, size_t> func_by_canonical; ///< Canonical name → index in @c functions.
    std::set<std::string> all_canonicals; ///< All canonical names seen (duplicate guard).

    // Current class being parsed
    std::optional<RuntimeClass> current_class; ///< Class block currently open, if any.
    int line_num = 0;                          ///< 1-based current line for diagnostics.
    std::string filename;                      ///< Source filename for diagnostics.

    /// @brief Print a file:line error to stderr and terminate the process.
    void error(const std::string &msg) const {
        throw std::runtime_error(filename + ":" + std::to_string(line_num) + ": error: " + msg);
    }

    /// @brief Print a non-fatal file:line warning to stderr.
    void warning(const std::string &msg) const {
        std::cerr << filename << ":" << line_num << ": warning: " << msg << "\n";
    }
};

//===----------------------------------------------------------------------===//
// String Utilities
//===----------------------------------------------------------------------===//

/// @brief Return a copy of @p sv with leading/trailing ASCII whitespace removed.
static std::string trim(std::string_view sv) {
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!sv.empty() && is_space(sv.front()))
        sv.remove_prefix(1);
    while (!sv.empty() && is_space(sv.back()))
        sv.remove_suffix(1);
    return std::string(sv);
}

/// @brief Split @p sv on @p delim, ignoring delimiters inside quotes or parentheses.
/// @details Each field is trimmed; empty fields are skipped. Used to parse the
///          comma-separated argument lists of runtime.def directives.
static std::vector<std::string> split(std::string_view sv, char delim) {
    std::vector<std::string> result;
    size_t start = 0;
    bool in_quotes = false;
    int paren_depth = 0;

    for (size_t i = 0; i <= sv.size(); ++i) {
        if (i < sv.size()) {
            if (sv[i] == '"' && (i == 0 || sv[i - 1] != '\\'))
                in_quotes = !in_quotes;
            else if (!in_quotes && sv[i] == '(')
                paren_depth++;
            else if (!in_quotes && sv[i] == ')') {
                paren_depth--;
                if (paren_depth < 0)
                    throw std::runtime_error("unbalanced ')' in comma-separated field");
            }
        }

        if (i == sv.size() || (!in_quotes && paren_depth == 0 && sv[i] == delim)) {
            if (i > start)
                result.push_back(trim(sv.substr(start, i - start)));
            start = i + 1;
        }
    }
    if (in_quotes)
        throw std::runtime_error("unterminated quote in comma-separated field");
    if (paren_depth != 0)
        throw std::runtime_error("unbalanced parentheses in comma-separated field");
    return result;
}

/// @brief Split @p sv on @p delim only at the top level of nesting.
/// @details Like split(), but also balances angle brackets, braces, and square
///          brackets in addition to parentheses and quotes — needed to split
///          generic-bearing type signatures (e.g. `List<Map<a,b>>`).
static std::vector<std::string> splitTopLevel(std::string_view sv, char delim) {
    std::vector<std::string> result;
    size_t start = 0;
    bool in_quotes = false;
    int paren_depth = 0;
    int angle_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;

    for (size_t i = 0; i <= sv.size(); ++i) {
        if (i < sv.size()) {
            if (sv[i] == '"' && (i == 0 || sv[i - 1] != '\\'))
                in_quotes = !in_quotes;
            else if (!in_quotes) {
                if (sv[i] == '(')
                    paren_depth++;
                else if (sv[i] == ')')
                    paren_depth--;
                else if (sv[i] == '<')
                    angle_depth++;
                else if (sv[i] == '>')
                    angle_depth--;
                else if (sv[i] == '{')
                    brace_depth++;
                else if (sv[i] == '}')
                    brace_depth--;
                else if (sv[i] == '[')
                    bracket_depth++;
                else if (sv[i] == ']')
                    bracket_depth--;
            }
        }

        if (i == sv.size() || (!in_quotes && paren_depth == 0 && angle_depth == 0 &&
                               brace_depth == 0 && bracket_depth == 0 && sv[i] == delim)) {
            if (i > start)
                result.push_back(trim(sv.substr(start, i - start)));
            start = i + 1;
        }
    }
    return result;
}

/// @brief Encode @p value as a quoted, escaped C++ string literal for emitted code.
/// @details Escapes quotes/backslashes/standard control chars and renders other
///          bytes below 0x20 (and 0x7F) as `\\xNN`, so generated .inc files always
///          compile regardless of the source text.
static std::string cppStringLiteral(const std::string &value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char c : value) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) == 0x7F) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\x%02X", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

/// @brief Return true if @p sv begins with @p prefix.
static bool startsWith(std::string_view sv, std::string_view prefix) {
    return sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix;
}

/// @brief Return @p sv with @p prefix removed, or unchanged if it does not match.
static std::string_view stripPrefix(std::string_view sv, std::string_view prefix) {
    if (startsWith(sv, prefix))
        return sv.substr(prefix.size());
    return sv;
}

/// @brief Return @p sv with leading/trailing ASCII whitespace removed (view, no copy).
static std::string_view trimView(std::string_view sv) {
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!sv.empty() && is_space(sv.front()))
        sv.remove_prefix(1);
    while (!sv.empty() && is_space(sv.back()))
        sv.remove_suffix(1);
    return sv;
}

/// @brief Trim @p sv and remove one layer of surrounding double quotes, if present.
static std::string stripQuotes(std::string_view sv) {
    std::string s = trim(sv);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

/// @brief Drop a trailing parameter name from a C parameter declaration.
/// @details Given e.g. "const char *path", returns "const char *"; returns the
///          input unchanged for "void" or when no trailing identifier is found.
static std::string stripParamName(std::string_view sv) {
    std::string param = trim(sv);
    if (param.empty() || param == "void")
        return param;

    size_t end = param.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(param[end - 1])))
        --end;

    size_t i = end;
    while (i > 0 && (std::isalnum(static_cast<unsigned char>(param[i - 1])) || param[i - 1] == '_'))
        --i;

    if (i == end)
        return param;

    return trim(param.substr(0, i));
}

/// @brief Extract the parameter name from a C parameter declaration.
/// @details Handles function-pointer parameters (`ret (*name)(...)`) as well as
///          ordinary `type name` declarations; returns "" when there is no name
///          (or the parameter is "void").
static std::string extractParamName(std::string_view sv) {
    std::string param = trim(sv);
    if (param.empty() || param == "void")
        return {};

    if (size_t fnPtr = param.find("(*"); fnPtr != std::string::npos) {
        size_t start = fnPtr + 2;
        while (start < param.size() && std::isspace(static_cast<unsigned char>(param[start])))
            ++start;
        size_t end = start;
        while (end < param.size() &&
               (std::isalnum(static_cast<unsigned char>(param[end])) || param[end] == '_')) {
            ++end;
        }
        return end > start ? param.substr(start, end - start) : std::string();
    }

    size_t end = param.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(param[end - 1])))
        --end;

    size_t start = end;
    while (start > 0 &&
           (std::isalnum(static_cast<unsigned char>(param[start - 1])) || param[start - 1] == '_'))
        --start;

    if (start == end)
        return {};
    return param.substr(start, end - start);
}

/// @brief Extract the argument text inside a macro call's parentheses.
/// @details For input like `FOO(a, b, c)` with @p macro = "FOO", returns
///          "a, b, c", matching the closing paren while respecting nested parens
///          and quotes. Returns nullopt when @p line is not a call to @p macro or
///          the parentheses are unbalanced.
static std::optional<std::string> extractParens(std::string_view line, std::string_view macro) {
    if (!startsWith(line, macro))
        return std::nullopt;
    line = stripPrefix(line, macro);
    // Skip whitespace to find opening paren
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        line.remove_prefix(1);
    if (line.empty() || line.front() != '(')
        return std::nullopt;
    line.remove_prefix(1);
    // Find matching close paren, respecting nested parens and quotes
    int depth = 1;
    bool in_quotes = false;
    size_t i = 0;
    for (; i < line.size() && depth > 0; ++i) {
        if (line[i] == '"' && (i == 0 || line[i - 1] != '\\'))
            in_quotes = !in_quotes;
        else if (!in_quotes && line[i] == '(')
            depth++;
        else if (!in_quotes && line[i] == ')')
            depth--;
    }
    if (depth != 0)
        return std::nullopt;
    return std::string(line.substr(0, i - 1));
}

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// @brief Parse an RT_FUNC directive and append a RuntimeFunc to @p state.
/// @details Validates the 4–5 argument arity, strips quotes, and enforces unique
///          ids and canonical names (reporting a fatal error otherwise).
static void parseRtFunc(ParseState &state, const std::string &args) {
    // RT_FUNC(id, c_symbol, canonical, signature [, lowering])
    auto parts = splitTopLevel(args, ',');
    if (parts.size() < 4 || parts.size() > 5) {
        state.error(
            "RT_FUNC requires 4-5 arguments: id, c_symbol, canonical, signature [, lowering]");
    }

    RuntimeFunc func;
    func.id = parts[0];
    func.c_symbol = parts[1];
    func.canonical = parts[2];
    func.signature = parts[3];
    if (parts.size() == 5)
        func.lowering = parts[4];

    // Remove quotes from canonical and signature
    if (func.canonical.size() >= 2 && func.canonical.front() == '"')
        func.canonical = func.canonical.substr(1, func.canonical.size() - 2);
    if (func.signature.size() >= 2 && func.signature.front() == '"')
        func.signature = func.signature.substr(1, func.signature.size() - 2);

    // Validate uniqueness
    if (state.func_by_id.count(func.id))
        state.error("Duplicate function id: " + func.id);
    if (state.func_by_canonical.count(func.canonical))
        state.error("Duplicate canonical name: " + func.canonical);

    size_t idx = state.functions.size();
    state.func_by_id[func.id] = idx;
    state.func_by_canonical[func.canonical] = idx;
    state.all_canonicals.insert(func.canonical);
    state.functions.push_back(std::move(func));
}

/// @brief Parse an RT_ALIAS directive, validating the target exists and the alias
///        name is not already taken.
static void parseRtAlias(ParseState &state, const std::string &args) {
    // RT_ALIAS(canonical, target_id)
    auto parts = split(args, ',');
    if (parts.size() != 2) {
        state.error("RT_ALIAS requires 2 arguments: canonical, target_id");
    }

    RuntimeAlias alias;
    alias.canonical = parts[0];
    alias.target_id = parts[1];

    // Remove quotes from canonical
    if (alias.canonical.size() >= 2 && alias.canonical.front() == '"')
        alias.canonical = alias.canonical.substr(1, alias.canonical.size() - 2);

    // Check target exists
    if (!state.func_by_id.count(alias.target_id))
        state.error("RT_ALIAS target not found: " + alias.target_id);

    // Check alias not already defined
    if (state.all_canonicals.count(alias.canonical))
        state.error("Duplicate canonical name (alias): " + alias.canonical);

    state.all_canonicals.insert(alias.canonical);
    state.aliases.push_back(std::move(alias));
}

/// @brief Parse an RT_BRIDGE directive assigning Zia bridge roles to a function's
///        parameters.
/// @details Requires one role per surface parameter and validates each role is
///          one of none/callback/payload before recording them on the target.
static void parseRtBridge(ParseState &state, const std::string &args) {
    // RT_BRIDGE(target_id, "role0,role1,...")
    auto parts = splitTopLevel(args, ',');
    if (parts.size() != 2)
        state.error("RT_BRIDGE requires 2 arguments: target_id, role_list");

    const std::string targetId = parts[0];
    auto it = state.func_by_id.find(targetId);
    if (it == state.func_by_id.end())
        state.error("RT_BRIDGE target not found: " + targetId);

    RuntimeFunc &func = state.functions[it->second];
    size_t surfaceParamCount = 0;
    size_t parenPos = func.signature.find('(');
    size_t closePos = func.signature.rfind(')');
    if (parenPos != std::string::npos && closePos != std::string::npos && closePos > parenPos + 1) {
        surfaceParamCount =
            splitTopLevel(func.signature.substr(parenPos + 1, closePos - parenPos - 1), ',').size();
    }
    auto roles = splitTopLevel(stripQuotes(parts[1]), ',');
    if (roles.size() != surfaceParamCount) {
        state.error("RT_BRIDGE for " + targetId + " has " + std::to_string(roles.size()) +
                    " roles but signature has " + std::to_string(surfaceParamCount) +
                    " parameters");
    }
    for (const auto &role : roles) {
        if (role != "none" && role != "callback" && role != "payload")
            state.error("RT_BRIDGE role must be none, callback, or payload: " + role);
    }
    func.bridgeRoles = std::move(roles);
}

/// @brief Parse RT_CLASS_BEGIN, opening a new class block in @p state.
/// @details Rejects nested class blocks; the class is finalized by parseRtClassEnd.
static void parseRtClassBegin(ParseState &state, const std::string &args) {
    // RT_CLASS_BEGIN(name, type_id, layout, ctor_id)
    if (state.current_class.has_value())
        state.error("Nested RT_CLASS_BEGIN not allowed");

    auto parts = split(args, ',');
    if (parts.size() != 4) {
        state.error("RT_CLASS_BEGIN requires 4 arguments: name, type_id, layout, ctor_id");
    }

    RuntimeClass cls;
    cls.name = parts[0];
    cls.type_id = parts[1];
    cls.layout = parts[2];
    cls.ctor_id = parts[3];

    // Remove quotes from all string fields
    auto stripQuotes = [](std::string &s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size() - 2);
    };
    stripQuotes(cls.name);
    stripQuotes(cls.layout);
    stripQuotes(cls.ctor_id);

    state.current_class = std::move(cls);
}

/// @brief Parse an RT_PROP directive, adding a property to the open class block.
static void parseRtProp(ParseState &state, const std::string &args) {
    // RT_PROP(name, type, getter_id, setter_id_or_none)
    if (!state.current_class.has_value())
        state.error("RT_PROP outside of RT_CLASS_BEGIN/END block");

    auto parts = split(args, ',');
    if (parts.size() != 4) {
        state.error("RT_PROP requires 4 arguments: name, type, getter_id, setter_id");
    }

    RuntimeProperty prop;
    prop.name = parts[0];
    prop.type = parts[1];
    prop.getter_id = parts[2];
    prop.setter_id = parts[3];

    // Remove quotes from all string fields
    auto stripQuotes = [](std::string &s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size() - 2);
    };
    stripQuotes(prop.name);
    stripQuotes(prop.type);
    stripQuotes(prop.getter_id);
    stripQuotes(prop.setter_id);

    state.current_class->props.push_back(std::move(prop));
}

/// @brief Parse an RT_METHOD directive, adding a method to the open class block.
static void parseRtMethod(ParseState &state, const std::string &args) {
    // RT_METHOD(name, signature, target_id)
    if (!state.current_class.has_value())
        state.error("RT_METHOD outside of RT_CLASS_BEGIN/END block");

    auto parts = split(args, ',');
    if (parts.size() != 3) {
        state.error("RT_METHOD requires 3 arguments: name, signature, target_id");
    }

    RuntimeMethod method;
    method.name = parts[0];
    method.signature = parts[1];
    method.target_id = parts[2];

    // Remove quotes from all string fields
    auto stripQuotes = [](std::string &s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size() - 2);
    };
    stripQuotes(method.name);
    stripQuotes(method.signature);
    stripQuotes(method.target_id);

    state.current_class->methods.push_back(std::move(method));
}

/// @brief Parse RT_CLASS_END, committing the open class block to @p state.classes.
static void parseRtClassEnd(ParseState &state) {
    if (!state.current_class.has_value())
        state.error("RT_CLASS_END without matching RT_CLASS_BEGIN");

    state.classes.push_back(std::move(*state.current_class));
    state.current_class.reset();
}

/// @brief Dispatch one runtime.def line to the matching RT_* directive parser.
/// @details Skips blank lines and // / # comments; an unrecognised directive is a
///          fatal error.
static void parseLine(ParseState &state, const std::string &line) {
    std::string trimmed = trim(line);

    // Skip empty lines and comments
    if (trimmed.empty() || startsWith(trimmed, "//") || startsWith(trimmed, "#"))
        return;

    // Parse macros
    if (auto args = extractParens(trimmed, "RT_FUNC")) {
        parseRtFunc(state, *args);
    } else if (auto args = extractParens(trimmed, "RT_ALIAS")) {
        parseRtAlias(state, *args);
    } else if (auto args = extractParens(trimmed, "RT_BRIDGE")) {
        parseRtBridge(state, *args);
    } else if (auto args = extractParens(trimmed, "RT_CLASS_BEGIN")) {
        parseRtClassBegin(state, *args);
    } else if (auto args = extractParens(trimmed, "RT_PROP")) {
        parseRtProp(state, *args);
    } else if (auto args = extractParens(trimmed, "RT_METHOD")) {
        parseRtMethod(state, *args);
    } else if (trimmed == "RT_CLASS_END()") {
        parseRtClassEnd(state);
    } else {
        state.error("Unknown directive: " + trimmed);
    }
}

/// @brief Read and parse an entire runtime.def file into a ParseState.
/// @details Opens @p path (fatal error on failure), parses each line, and verifies
///          there is no unclosed class block at end of file.
static ParseState parseFile(const fs::path &path) {
    ParseState state;
    state.filename = path.string();

    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }

    std::string line;
    while (std::getline(in, line)) {
        state.line_num++;
        parseLine(state, line);
    }

    if (state.current_class.has_value()) {
        state.error("Unclosed RT_CLASS_BEGIN (missing RT_CLASS_END)");
    }

    return state;
}

//===----------------------------------------------------------------------===//
// Type Mapping (IL signature types to C types)
//===----------------------------------------------------------------------===//

/// @brief Map IL type to C type for DirectHandler template.
static std::string ilTypeToCType(const std::string &ilType) {
    std::string baseType = ilType;
    size_t langle = baseType.find('<');
    if (langle != std::string::npos)
        baseType.resize(langle);

    if (baseType == "str")
        return "rt_string";
    if (baseType == "i64")
        return "int64_t";
    if (baseType == "i32")
        return "int32_t";
    if (baseType == "i16")
        return "int16_t";
    if (baseType == "i8" || baseType == "i1")
        return "int8_t";
    if (baseType == "f64")
        return "double";
    if (baseType == "f32")
        return "float";
    if (baseType == "void")
        return "void";
    if (baseType == "bool")
        return "int8_t";
    if (baseType == "obj" || baseType == "ptr")
        return "void *";
    throw std::runtime_error("unknown IL type in runtime signature: " + ilType);
}

/// @brief Return the base IL type with any `<...>` generic arguments removed.
static std::string stripTypeArgs(const std::string &ilType) {
    size_t langle = ilType.find('<');
    if (langle == std::string::npos)
        return ilType;
    return ilType.substr(0, langle);
}

/// @brief Return the text between the outermost `<` and `>` of an IL type, or "".
static std::string extractTypeArg(const std::string &ilType) {
    size_t langle = ilType.find('<');
    size_t rangle = ilType.rfind('>');
    if (langle == std::string::npos || rangle == std::string::npos || rangle <= langle)
        return {};
    return ilType.substr(langle + 1, rangle - langle - 1);
}

/// @brief Map IL type to signature string format.
static std::string ilTypeToSigType(const std::string &ilType) {
    // Handle optional return types (trailing '?') — at the IL level, optional
    // reference types keep their inner type (null pointer = none).
    if (!ilType.empty() && ilType.back() == '?')
        return ilTypeToSigType(ilType.substr(0, ilType.size() - 1));

    std::string baseType = stripTypeArgs(ilType);
    if (baseType == "str")
        return "string";
    if (baseType == "obj")
        return ilType;
    if (baseType == "ptr")
        return "ptr";
    if (baseType == "bool")
        return "i1";
    // Most types map directly
    return baseType;
}

/// @brief A runtime signature split into its return type and argument types.
struct ParsedSignature {
    std::string returnType;            ///< Return type text (before the '(').
    std::vector<std::string> argTypes; ///< Argument type texts, in order.
};

/// @brief Parse a signature like "str(i64,str)" into return type and arg types.
/// @details A signature with no '(' is treated as a bare return type with no args.
static ParsedSignature parseSignature(const std::string &sig) {
    ParsedSignature result;

    // Find the opening paren
    size_t parenPos = sig.find('(');
    if (parenPos == std::string::npos) {
        result.returnType = sig;
        return result;
    }

    result.returnType = sig.substr(0, parenPos);

    // Extract args between parens
    size_t closePos = sig.rfind(')');
    if (closePos == std::string::npos || closePos <= parenPos + 1) {
        return result; // No args
    }

    std::string argsStr = sig.substr(parenPos + 1, closePos - parenPos - 1);
    if (argsStr.empty()) {
        return result; // Empty args "()"
    }

    for (auto &arg : splitTopLevel(argsStr, ',')) {
        if (!arg.empty()) {
            result.argTypes.push_back(std::move(arg));
        }
    }

    return result;
}

/// @brief Return true if the signature's return or any argument is a raw `ptr`.
/// @details Used by the Zia bridge to flag functions that expose unsafe pointers.
static bool signatureExposesRawPointer(const std::string &sig) {
    ParsedSignature parsed = parseSignature(sig);
    if (stripTypeArgs(parsed.returnType) == "ptr")
        return true;
    for (const auto &arg : parsed.argTypes) {
        if (stripTypeArgs(arg) == "ptr")
            return true;
    }
    return false;
}

//===----------------------------------------------------------------------===//
// Runtime signature helpers
//===----------------------------------------------------------------------===//

/// @brief Read the signature names (first field of each SIG(...) row) from a
///        RuntimeSigs.def file, in declaration order.
static std::vector<std::string> parseRtSigNames(const fs::path &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }

    std::vector<std::string> names;
    std::string line;
    while (std::getline(in, line)) {
        std::string_view view = trimView(line);
        if (!startsWith(view, "SIG"))
            continue;

        auto parens = extractParens(view, "SIG");
        if (!parens)
            continue;
        auto parts = split(*parens, ',');
        if (parts.empty())
            continue;
        names.push_back(parts[0]);
    }
    return names;
}

/// @brief Read the ordered symbol-name string list from the kRtSigSymbolNames
///        array in RuntimeSignaturesData.hpp.
/// @details Locates the `kRtSigSymbolNames { ... }` block and extracts each
///          double-quoted string; returns empty if the marker is absent.
static std::vector<std::string> parseRtSigSymbols(const fs::path &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }

    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string marker = "kRtSigSymbolNames";
    size_t start = contents.find(marker);
    if (start == std::string::npos) {
        return {};
    }

    start = contents.find('{', start);
    if (start == std::string::npos)
        return {};
    size_t end = contents.find("};", start);
    if (end == std::string::npos)
        return {};

    std::string_view block = std::string_view(contents).substr(start + 1, end - start - 1);
    std::vector<std::string> symbols;
    std::string current;
    bool in_quotes = false;
    for (size_t i = 0; i < block.size(); ++i) {
        char c = block[i];
        if (c == '"' && (i == 0 || block[i - 1] != '\\')) {
            if (in_quotes) {
                symbols.push_back(current);
                current.clear();
            }
            in_quotes = !in_quotes;
            continue;
        }
        if (in_quotes)
            current.push_back(c);
    }
    return symbols;
}

/// @brief Build a map from runtime symbol name to its RtSig:: enum expression.
/// @details Pairs the names from RuntimeSigs.def with the symbol strings from
///          RuntimeSignaturesData.hpp positionally; a length mismatch is fatal.
static std::unordered_map<std::string, std::string> buildRtSigMap(const fs::path &runtimeDir) {
    const fs::path sigsPath = runtimeDir / "RuntimeSigs.def";
    const fs::path dataPath = runtimeDir / "RuntimeSignaturesData.hpp";
    std::vector<std::string> sigNames = parseRtSigNames(sigsPath);
    std::vector<std::string> sigSymbols = parseRtSigSymbols(dataPath);

    if (sigNames.size() != sigSymbols.size()) {
        throw std::runtime_error("RuntimeSigs.def and RuntimeSignaturesData.hpp mismatch");
    }

    std::unordered_map<std::string, std::string> result;
    for (size_t i = 0; i < sigNames.size(); ++i) {
        result[sigSymbols[i]] = "RtSig::" + sigNames[i];
    }
    return result;
}

/// @brief Build the C++ expression that indexes the spec table by signature id.
static std::string buildSigSpecExpr(const std::string &sigId) {
    return "data::kRtSigSpecs[static_cast<std::size_t>(" + sigId + ")]";
}

/// @brief Remove // line comments and block comments from C source text.
/// @details Quote handling is intentionally simplistic — adequate for scanning
///          runtime headers for declarations, not for full C tokenization.
static std::string stripComments(const std::string &input) {
    std::string out;
    out.reserve(input.size());
    bool in_line = false;
    bool in_block = false;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        char next = (i + 1 < input.size()) ? input[i + 1] : '\0';

        if (in_line) {
            if (c == '\n') {
                in_line = false;
                out.push_back(c);
            }
            continue;
        }

        if (in_block) {
            if (c == '*' && next == '/') {
                in_block = false;
                ++i;
            }
            continue;
        }

        if (c == '/' && next == '/') {
            in_line = true;
            ++i;
            continue;
        }
        if (c == '/' && next == '*') {
            in_block = true;
            ++i;
            continue;
        }

        out.push_back(c);
    }
    return out;
}

/// @brief Return true if @p line ends with a backslash (continues a directive).
static bool lineContinuesPreprocessorDirective(std::string_view line) {
    size_t end = line.find_last_not_of(" \t\r");
    return end != std::string_view::npos && line[end] == '\\';
}

/// @brief Remove preprocessor directive lines (and their `\`-continuations).
/// @details Drops any line whose first non-space character is `#`, so header
///          scanning sees only declaration text.
static std::string stripPreprocessor(const std::string &input) {
    std::ostringstream out;
    std::istringstream in(input);
    std::string line;
    bool inDirectiveContinuation = false;
    while (std::getline(in, line)) {
        if (inDirectiveContinuation) {
            inDirectiveContinuation = lineContinuesPreprocessorDirective(line);
            continue;
        }

        std::string_view trimmed = trimView(line);
        if (!trimmed.empty() && trimmed.front() == '#') {
            inDirectiveContinuation = lineContinuesPreprocessorDirective(line);
            continue;
        }
        out << line << '\n';
    }
    return out.str();
}

/// @brief Read an entire text file into a string with a generator-local size cap.
/// @details Runtime definition and header scans are expected to be small source
///          files. The cap prevents accidental reads of huge generated artifacts
///          or device files when an input path is wrong.
static std::string readTextFile(const fs::path &path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec)
        throw std::runtime_error("cannot stat " + path.string() + ": " + ec.message());
    if (size > kMaxRtgenTextFileBytes)
        throw std::runtime_error("rtgen input file is too large: " + path.string());
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// @brief Atomically write one generated rtgen output file.
/// @details All generator functions build complete text in memory and call this
///          helper once. That keeps stale output intact if a later write fails and
///          avoids consumers observing partially-generated include files.
static void writeGeneratedTextFile(const fs::path &path, const std::ostringstream &contents) {
    viper::pkg::writeTextFileAtomic(path, contents.str());
}

/// @brief Normalize @p path and return it with forward slashes.
static std::string pathToGenericString(const fs::path &path) {
    return path.lexically_normal().generic_string();
}

/// @brief Return @p path relative to @p base in forward-slash form (absolute if
///        no relative path can be formed).
static std::string relativePathString(const fs::path &path, const fs::path &base) {
    fs::path rel = path.lexically_relative(base);
    if (rel.empty())
        return pathToGenericString(path);
    return pathToGenericString(rel);
}

/// @brief Return true if @p c is a C identifier character (alphanumeric or '_').
static bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/// @brief Return true if a whole-token `rt_` symbol begins at @p pos in @p text.
/// @details Requires `rt_` not to be preceded by an identifier char and to be
///          followed by one, so it matches runtime symbol names but not substrings.
static bool isRuntimeSymbolAt(const std::string &text, size_t pos) {
    if (pos + 3 > text.size() || text.compare(pos, 3, "rt_") != 0)
        return false;
    if (pos > 0 && isIdentifierChar(text[pos - 1]))
        return false;
    if (pos + 3 >= text.size() || !isIdentifierChar(text[pos + 3]))
        return false;
    return true;
}

/// @brief Find the ')' matching the '(' at @p openPos, honoring nesting and quotes.
/// @return Index of the matching ')', or std::string::npos if unbalanced.
static size_t findMatchingParen(const std::string &text, size_t openPos) {
    int depth = 1;
    bool inQuotes = false;
    for (size_t cursor = openPos + 1; cursor < text.size(); ++cursor) {
        char c = text[cursor];
        if (c == '"' && (cursor == 0 || text[cursor - 1] != '\\')) {
            inQuotes = !inQuotes;
        } else if (!inQuotes) {
            if (c == '(')
                ++depth;
            else if (c == ')' && --depth == 0)
                return cursor;
        }
    }
    return std::string::npos;
}

/// @brief Scan backwards from a symbol to the start of its declaration.
/// @details Stops at the previous statement boundary (`;`, `{`, or `}`) so the
///          text in between can be parsed as the return-type qualifier list.
static size_t findDeclarationStart(const std::string &text, size_t symbolPos) {
    size_t start = symbolPos;
    while (start > 0) {
        char c = text[start - 1];
        if (c == ';' || c == '{' || c == '}')
            break;
        --start;
    }
    return start;
}

/// @brief Trim a declaration's return type and strip leading storage/qualifier
///        keywords (extern, static, inline, _Noreturn).
static std::string normalizeReturnType(std::string retType) {
    retType = trim(retType);

    // Keep the parser tolerant of annotations that appear on their own line.
    if (size_t newline = retType.find_last_of("\r\n"); newline != std::string::npos)
        retType = trim(retType.substr(newline + 1));

    bool changed = true;
    while (changed) {
        changed = false;
        for (const char *kw : {"extern ", "_Noreturn ", "static ", "inline "}) {
            std::string kwStr(kw);
            if (retType.substr(0, kwStr.size()) == kwStr) {
                retType = trim(retType.substr(kwStr.size()));
                changed = true;
            }
        }
    }
    return retType;
}

/// @brief Scan all runtime headers under @p runtimeDir for `rt_*(...)` prototypes.
/// @details Strips comments/preprocessor lines, then for each whole-token rt_
///          symbol followed by a parenthesised, semicolon-terminated declaration
///          records its return type, argument types, and parameter names.
/// @param runtimeDir Directory tree of runtime .h/.hpp files to scan.
/// @param repoRoot Repository root, used to record header paths relatively.
/// @return Map from runtime symbol name to its parsed prototype.
static std::unordered_map<std::string, RuntimePrototype> loadRuntimeHeaderDeclarations(
    const fs::path &runtimeDir, const fs::path &repoRoot) {
    std::unordered_map<std::string, RuntimePrototype> result;
    if (!fs::exists(runtimeDir))
        return result;

    for (const auto &entry : fs::recursive_directory_iterator(runtimeDir)) {
        if (!entry.is_regular_file())
            continue;
        const fs::path path = entry.path();
        if (path.extension() != ".h" && path.extension() != ".hpp")
            continue;

        std::ifstream in(path);
        if (!in)
            continue;

        std::string contents((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
        contents = stripComments(contents);
        contents = stripPreprocessor(contents);

        for (size_t pos = 0; (pos = contents.find("rt_", pos)) != std::string::npos;) {
            if (!isRuntimeSymbolAt(contents, pos)) {
                pos += 3;
                continue;
            }

            size_t nameEnd = pos + 3;
            while (nameEnd < contents.size() && isIdentifierChar(contents[nameEnd]))
                ++nameEnd;

            size_t cursor = nameEnd;
            while (cursor < contents.size() &&
                   std::isspace(static_cast<unsigned char>(contents[cursor])))
                ++cursor;
            if (cursor >= contents.size() || contents[cursor] != '(') {
                pos = nameEnd;
                continue;
            }

            size_t close = findMatchingParen(contents, cursor);
            if (close == std::string::npos) {
                pos = nameEnd;
                continue;
            }

            size_t afterClose = close + 1;
            while (afterClose < contents.size() &&
                   std::isspace(static_cast<unsigned char>(contents[afterClose])))
                ++afterClose;
            if (afterClose >= contents.size() || contents[afterClose] != ';') {
                pos = nameEnd;
                continue;
            }

            const size_t declStart = findDeclarationStart(contents, pos);
            std::string retType = normalizeReturnType(contents.substr(declStart, pos - declStart));
            if (retType.find('=') != std::string::npos) {
                pos = afterClose + 1;
                continue;
            }
            std::string funcName = contents.substr(pos, nameEnd - pos);
            std::string argsStr = contents.substr(cursor + 1, close - cursor - 1);

            auto existing = result.find(funcName);
            bool haveExisting = existing != result.end();

            RuntimePrototype proto;
            CSignature sig;
            sig.returnType = retType;
            std::vector<std::string> args = splitTopLevel(argsStr, ',');
            for (const auto &arg : args) {
                std::string type = stripParamName(arg);
                if (type.empty() || type == "void")
                    continue;
                sig.argTypes.push_back(type);
                proto.paramNames.push_back(extractParamName(arg));
            }

            if (sig.returnType.empty()) {
                pos = afterClose + 1;
                continue;
            }

            proto.signature = std::move(sig);
            proto.headerPath = relativePathString(path, repoRoot);
            if (!haveExisting) {
                result.emplace(funcName, std::move(proto));
            } else if (existing->second.signature.returnType.empty()) {
                existing->second = std::move(proto);
            }

            pos = afterClose + 1;
        }
    }

    return result;
}

/// @brief Convenience over loadRuntimeHeaderDeclarations returning just signatures.
/// @return Map from runtime symbol name to its parsed C signature.
static std::unordered_map<std::string, CSignature> loadRuntimeCSignatures(
    const fs::path &runtimeDir, const fs::path &repoRoot) {
    std::unordered_map<std::string, CSignature> result;
    auto decls = loadRuntimeHeaderDeclarations(runtimeDir, repoRoot);
    for (auto &[symbol, proto] : decls) {
        result.emplace(symbol, std::move(proto.signature));
    }
    return result;
}

/// @brief Collect the set of all `rt_*` symbol tokens referenced in runtime .c/.cpp.
/// @details Used by the audit to detect runtime functions that exist in source but
///          are missing from runtime.def (or vice versa).
static std::unordered_set<std::string> loadRuntimeSourceTokens(const fs::path &runtimeDir) {
    std::unordered_set<std::string> tokens;
    if (!fs::exists(runtimeDir))
        return tokens;

    for (const auto &entry : fs::recursive_directory_iterator(runtimeDir)) {
        if (!entry.is_regular_file())
            continue;
        const fs::path path = entry.path();
        const fs::path ext = path.extension();
        // .inc and .m are runtime implementation sources too: large 3D translation units are
        // split into cohesive .inc fragments (#included into their .c), and the Metal backend
        // is .m. Implementation tokens can therefore live in any of these.
        if (ext != ".c" && ext != ".cpp" && ext != ".inc" && ext != ".m")
            continue;

        const std::string text = readTextFile(path);
        for (size_t pos = 0; (pos = text.find("rt_", pos)) != std::string::npos;) {
            if (!isRuntimeSymbolAt(text, pos)) {
                pos += 3;
                continue;
            }
            size_t end = pos + 3;
            while (end < text.size() && isIdentifierChar(text[end]))
                ++end;
            tokens.insert(text.substr(pos, end - pos));
            pos = end;
        }
    }
    return tokens;
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

/// @brief A flattened runtime function row used during code generation.
struct RuntimeEntry {
    std::string name;      ///< Canonical Viper.* name.
    std::string c_symbol;  ///< C runtime symbol.
    std::string signature; ///< IL type signature.
    std::string lowering;  ///< "always" or "" (default: manual)
};

/// @brief Build the standard "AUTO-GENERATED — DO NOT EDIT" banner for an .inc file.
/// @param filename Logical name of the generated file (for the header).
/// @param purpose One-line description of the file's contents.
/// @return The comment-block header text, including a trailing blank line.
static std::string fileHeader(const std::string &filename, const std::string &purpose) {
    std::ostringstream out;
    out << "//===----------------------------------------------------------------------===//\n";
    out << "//\n";
    out << "// AUTO-GENERATED FILE - DO NOT EDIT\n";
    out << "// Generated by rtgen from runtime.def\n";
    out << "//\n";
    out << "//===----------------------------------------------------------------------===//\n";
    out << "//\n";
    out << "// File: " << filename << "\n";
    out << "// Purpose: " << purpose << "\n";
    out << "//\n";
    out << "//===----------------------------------------------------------------------===//\n\n";
    return out.str();
}

/// @brief Resolve a function id or canonical name to its RuntimeFunc.
/// @return Pointer into @p state, or nullptr when neither lookup matches.
static const RuntimeFunc *resolveRuntimeFunc(const ParseState &state,
                                             const std::string &idOrCanonical) {
    if (auto it = state.func_by_id.find(idOrCanonical); it != state.func_by_id.end())
        return &state.functions[it->second];
    if (auto it = state.func_by_canonical.find(idOrCanonical); it != state.func_by_canonical.end())
        return &state.functions[it->second];
    return nullptr;
}

/// @brief Find a RuntimeAlias by its canonical name.
/// @return Pointer into @p state.aliases, or nullptr if not an alias.
static const RuntimeAlias *resolveRuntimeAlias(const ParseState &state,
                                               const std::string &canonical) {
    for (const auto &alias : state.aliases) {
        if (alias.canonical == canonical)
            return &alias;
    }
    return nullptr;
}

/// @brief Resolve an id/canonical/alias reference to its canonical name.
/// @return The canonical name; an empty string for "none"/empty; nullopt when the
///         reference does not resolve.
static std::optional<std::string> resolveRuntimeCanonical(const ParseState &state,
                                                          const std::string &idOrCanonical) {
    if (idOrCanonical.empty() || idOrCanonical == "none")
        return std::string();
    if (const auto *fn = resolveRuntimeFunc(state, idOrCanonical))
        return fn->canonical;
    if (const auto *alias = resolveRuntimeAlias(state, idOrCanonical))
        return alias->canonical;
    return std::nullopt;
}

/// @brief Resolve an id/canonical/alias reference to its underlying C symbol.
/// @return The C symbol; an empty string for "none"/empty; nullopt when the
///         reference does not resolve.
static std::optional<std::string> resolveRuntimeSymbol(const ParseState &state,
                                                       const std::string &idOrCanonical) {
    if (idOrCanonical.empty() || idOrCanonical == "none")
        return std::string();
    if (const auto *fn = resolveRuntimeFunc(state, idOrCanonical))
        return fn->c_symbol;
    if (const auto *alias = resolveRuntimeAlias(state, idOrCanonical)) {
        if (auto it = state.func_by_id.find(alias->target_id); it != state.func_by_id.end())
            return state.functions[it->second].c_symbol;
    }
    return std::nullopt;
}

/// @brief Return the final dot-separated segment of @p dotted (e.g. the method name).
static std::string lastSegment(std::string_view dotted) {
    size_t pos = dotted.rfind('.');
    if (pos == std::string_view::npos)
        return std::string(dotted);
    return std::string(dotted.substr(pos + 1));
}

/// @brief Build a method-slot key from a (case-insensitive name, signature) pair.
/// @details Used to deduplicate methods across declared, constructor, and
///          synthesized entries when resolving a class.
static std::string methodSlotKey(std::string_view name, std::string_view signature) {
    std::string key;
    key.reserve(name.size() + signature.size() + 1);
    for (unsigned char c : name)
        key.push_back(static_cast<char>(std::tolower(c)));
    key.push_back('|');
    key.append(signature);
    return key;
}

/// @brief Resolve every parsed class into a ResolvedRuntimeClass for codegen.
/// @details For each class this resolves ctor/getter/setter/method id references
///          to canonical names, then synthesizes method entries for any remaining
///          functions whose canonical name is prefixed by the class (excluding
///          get_/set_ accessors and already-covered slots), deduplicating by
///          canonical name and method slot.
static std::vector<ResolvedRuntimeClass> buildResolvedClasses(const ParseState &state) {
    std::vector<ResolvedRuntimeClass> resolved;
    resolved.reserve(state.classes.size());

    for (const auto &cls : state.classes) {
        ResolvedRuntimeClass outClass;
        outClass.name = cls.name;
        outClass.type_id = cls.type_id;
        outClass.layout = cls.layout;
        if (auto ctorCanonical = resolveRuntimeCanonical(state, cls.ctor_id))
            outClass.ctorCanonical = *ctorCanonical;

        for (const auto &prop : cls.props) {
            ResolvedRuntimeProperty outProp;
            outProp.name = prop.name;
            outProp.type = prop.type;
            if (auto getterCanonical = resolveRuntimeCanonical(state, prop.getter_id))
                outProp.getterCanonical = *getterCanonical;
            else
                outProp.getterCanonical = prop.getter_id;
            if (auto setterCanonical = resolveRuntimeCanonical(state, prop.setter_id))
                outProp.setterCanonical = *setterCanonical;
            else
                outProp.setterCanonical = prop.setter_id;
            outClass.props.push_back(std::move(outProp));
        }

        std::vector<ResolvedRuntimeMethod> methods;
        methods.reserve(cls.methods.size() + 4);
        std::unordered_set<std::string> coveredCanonicals;
        std::unordered_set<std::string> coveredMethodSlots;

        for (const auto &prop : cls.props) {
            if (auto getterCanonical = resolveRuntimeCanonical(state, prop.getter_id);
                getterCanonical && !getterCanonical->empty()) {
                coveredCanonicals.insert(*getterCanonical);
            }
            if (auto setterCanonical = resolveRuntimeCanonical(state, prop.setter_id);
                setterCanonical && !setterCanonical->empty()) {
                coveredCanonicals.insert(*setterCanonical);
            }
        }

        for (const auto &method : cls.methods) {
            ResolvedRuntimeMethod outMethod;
            outMethod.name = method.name;
            outMethod.signature = method.signature;
            if (auto targetCanonical = resolveRuntimeCanonical(state, method.target_id))
                outMethod.targetCanonical = *targetCanonical;
            else
                outMethod.targetCanonical = method.target_id;
            coveredMethodSlots.insert(methodSlotKey(outMethod.name, outMethod.signature));
            if (!outMethod.targetCanonical.empty())
                coveredCanonicals.insert(outMethod.targetCanonical);
            methods.push_back(std::move(outMethod));
        }

        if (!outClass.ctorCanonical.empty()) {
            bool hasCtorMethod = false;
            for (const auto &method : methods) {
                if (method.targetCanonical == outClass.ctorCanonical) {
                    hasCtorMethod = true;
                    break;
                }
            }
            if (!hasCtorMethod) {
                if (const auto *ctorFunc = resolveRuntimeFunc(
                        state, cls.ctor_id.empty() ? outClass.ctorCanonical : cls.ctor_id)) {
                    ResolvedRuntimeMethod ctorMethod;
                    ctorMethod.name = lastSegment(ctorFunc->canonical);
                    ctorMethod.signature = ctorFunc->signature;
                    ctorMethod.targetCanonical = ctorFunc->canonical;
                    coveredMethodSlots.insert(methodSlotKey(ctorMethod.name, ctorMethod.signature));
                    coveredCanonicals.insert(ctorMethod.targetCanonical);
                    methods.push_back(std::move(ctorMethod));
                }
            }
        }

        const std::string classPrefix = cls.name + ".";
        for (const auto &fn : state.functions) {
            if (!startsWith(fn.canonical, classPrefix))
                continue;
            if (coveredCanonicals.count(fn.canonical))
                continue;
            std::string methodName = lastSegment(fn.canonical);
            if (startsWith(methodName, "get_") || startsWith(methodName, "set_"))
                continue;
            if (coveredMethodSlots.count(methodSlotKey(methodName, fn.signature)))
                continue;

            ResolvedRuntimeMethod synthetic;
            synthetic.name = std::move(methodName);
            synthetic.signature = fn.signature;
            synthetic.targetCanonical = fn.canonical;
            coveredCanonicals.insert(synthetic.targetCanonical);
            coveredMethodSlots.insert(methodSlotKey(synthetic.name, synthetic.signature));
            methods.push_back(std::move(synthetic));
        }

        outClass.methods = std::move(methods);
        resolved.push_back(std::move(outClass));
    }

    return resolved;
}

/// @brief Invoke @p handler with the argument text of every @p macroName call in @p text.
/// @details Recognises whole-token macro names followed by a parenthesised
///          argument list (respecting nesting and quotes); an unterminated call is
///          a fatal error. Used to scan the runtime surface-policy file.
/// @param text Source text to scan.
/// @param macroName Macro identifier to match.
/// @param handler Callback receiving the raw text between the parentheses.
static void scanMacroCalls(const std::string &text,
                           std::string_view macroName,
                           const std::function<void(std::string_view)> &handler) {
    size_t pos = 0;
    while ((pos = text.find(macroName, pos)) != std::string::npos) {
        if (pos > 0) {
            char prev = text[pos - 1];
            if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_') {
                pos += macroName.size();
                continue;
            }
        }

        size_t cursor = pos + macroName.size();
        while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])))
            ++cursor;
        if (cursor >= text.size() || text[cursor] != '(') {
            pos += macroName.size();
            continue;
        }

        size_t argsStart = cursor + 1;
        int depth = 1;
        bool inQuotes = false;
        ++cursor;
        for (; cursor < text.size() && depth > 0; ++cursor) {
            char c = text[cursor];
            if (c == '"' && (cursor == 0 || text[cursor - 1] != '\\')) {
                inQuotes = !inQuotes;
            } else if (!inQuotes) {
                if (c == '(')
                    ++depth;
                else if (c == ')')
                    --depth;
            }
        }
        if (depth != 0) {
            throw std::runtime_error("unterminated " + std::string(macroName) +
                                     " macro in runtime surface policy");
        }

        handler(std::string_view(text).substr(argsStart, cursor - argsStart - 1));
        pos = cursor;
    }
}

/// @brief Parse the runtime surface-policy file into a RuntimeSurfacePolicy.
/// @details Scans for the RUNTIME_SURFACE_* macros declaring internal headers,
///          internal symbols, and expected functions/methods/properties; a
///          missing file yields an empty (permissive) policy. Used by the audit.
static RuntimeSurfacePolicy parseRuntimeSurfacePolicy(const fs::path &policyPath) {
    RuntimeSurfacePolicy policy;
    if (!fs::exists(policyPath))
        return policy;

    std::string text = stripComments(readTextFile(policyPath));

    scanMacroCalls(text, "RUNTIME_SURFACE_INTERNAL_HEADER", [&](std::string_view argsView) {
        auto parts = split(argsView, ',');
        if (parts.size() != 1) {
            throw std::runtime_error("RUNTIME_SURFACE_INTERNAL_HEADER requires 1 argument");
        }
        policy.internalHeaders.insert(pathToGenericString(stripQuotes(parts[0])));
    });

    scanMacroCalls(text, "RUNTIME_SURFACE_INTERNAL_SYMBOL", [&](std::string_view argsView) {
        auto parts = split(argsView, ',');
        if (parts.size() != 1) {
            throw std::runtime_error("RUNTIME_SURFACE_INTERNAL_SYMBOL requires 1 argument");
        }
        policy.internalSymbols.insert(stripQuotes(parts[0]));
    });

    scanMacroCalls(text, "RUNTIME_SURFACE_EXPECT_FUNCTION", [&](std::string_view argsView) {
        auto parts = split(argsView, ',');
        if (parts.size() != 2) {
            throw std::runtime_error("RUNTIME_SURFACE_EXPECT_FUNCTION requires 2 arguments");
        }
        policy.expectedFunctions.emplace(stripQuotes(parts[0]), stripQuotes(parts[1]));
    });

    scanMacroCalls(text, "RUNTIME_SURFACE_EXPECT_METHOD", [&](std::string_view argsView) {
        auto parts = split(argsView, ',');
        if (parts.size() != 3) {
            throw std::runtime_error("RUNTIME_SURFACE_EXPECT_METHOD requires 3 arguments");
        }
        ResolvedRuntimeMethod method;
        method.name = stripQuotes(parts[1]);
        method.signature = stripQuotes(parts[2]);
        method.targetCanonical = stripQuotes(parts[0]);
        policy.expectedMethods.push_back(std::move(method));
    });

    scanMacroCalls(text, "RUNTIME_SURFACE_EXPECT_PROPERTY", [&](std::string_view argsView) {
        auto parts = split(argsView, ',');
        if (parts.size() != 3) {
            throw std::runtime_error("RUNTIME_SURFACE_EXPECT_PROPERTY requires 3 arguments");
        }
        ResolvedRuntimeProperty prop;
        prop.name = stripQuotes(parts[1]);
        prop.type = stripQuotes(parts[2]);
        prop.getterCanonical = stripQuotes(parts[0]);
        policy.expectedProperties.push_back(std::move(prop));
    });

    return policy;
}

/// @brief Build the `&DirectHandler<...>::invoke` expression for a runtime symbol.
/// @details Instantiates the VM's DirectHandler template with the C symbol, return
///          type, and argument types so the interpreter can call it directly.
static std::string buildDirectHandlerExpr(const std::string &c_symbol, const CSignature &sig) {
    std::string args = "&" + c_symbol + ", " + sig.returnType;
    for (const auto &arg : sig.argTypes) {
        args += ", " + arg;
    }
    return "&DirectHandler<" + args + ">::invoke";
}

/// @brief Build the `&ConsumingStringHandler<...>::invoke` expression.
/// @details Same as buildDirectHandlerExpr but for functions that take ownership
///          of (consume) their string arguments, so the VM retains them first.
static std::string buildConsumingStringHandlerExpr(const std::string &c_symbol,
                                                   const CSignature &sig) {
    std::string args = "&" + c_symbol + ", " + sig.returnType;
    for (const auto &arg : sig.argTypes) {
        args += ", " + arg;
    }
    return "&ConsumingStringHandler<" + args + ">::invoke";
}

/// @brief Check if a runtime function consumes its string arguments.
/// @details Functions like rt_str_concat release their string arguments after use,
///          so the VM must retain them before the call to prevent use-after-free.
static bool needsConsumingStringHandler(const std::string &c_symbol) {
    // rt_str_concat releases both of its string arguments after use
    return c_symbol == "rt_str_concat";
}

/// @brief Compute the descriptor fields for one runtime entry's signature row.
/// @details Resolves the signature id/spec (preferring a known RtSig entry, else
///          synthesizing a spec string from the IL signature) and the VM handler
///          expression (direct or consuming-string), leaving lowering/hidden/trap
///          fields at their defaults for the caller to override.
/// @param entry The runtime function being described.
/// @param cSignatures Map of C symbol → parsed C signature (for handler types).
/// @param rtSigMap Map of C symbol → RtSig:: signature-id expression.
/// @return The populated descriptor fields.
static DescriptorFields buildDefaultDescriptor(
    const RuntimeEntry &entry,
    const std::unordered_map<std::string, CSignature> &cSignatures,
    const std::unordered_map<std::string, std::string> &rtSigMap) {
    DescriptorFields fields;

    auto sigIt = rtSigMap.find(entry.c_symbol);
    if (sigIt != rtSigMap.end()) {
        fields.signatureId = sigIt->second;
        fields.spec = buildSigSpecExpr(fields.signatureId);
    } else {
        fields.signatureId = "std::nullopt";
        ParsedSignature parsed = parseSignature(entry.signature);
        std::string sigStr = ilTypeToSigType(parsed.returnType) + "(";
        for (size_t i = 0; i < parsed.argTypes.size(); ++i) {
            if (i > 0)
                sigStr += ", ";
            sigStr += ilTypeToSigType(parsed.argTypes[i]);
        }
        sigStr += ")";
        fields.spec = cppStringLiteral(sigStr);
    }

    auto cSigIt = cSignatures.find(entry.c_symbol);
    bool useConsumingHandler = needsConsumingStringHandler(entry.c_symbol);
    if (cSigIt != cSignatures.end()) {
        fields.handler = useConsumingHandler
                             ? buildConsumingStringHandlerExpr(entry.c_symbol, cSigIt->second)
                             : buildDirectHandlerExpr(entry.c_symbol, cSigIt->second);
    } else {
        ParsedSignature parsed = parseSignature(entry.signature);
        CSignature fallback;
        fallback.returnType = ilTypeToCType(parsed.returnType);
        for (const auto &arg : parsed.argTypes)
            fallback.argTypes.push_back(ilTypeToCType(arg));
        fields.handler = useConsumingHandler
                             ? buildConsumingStringHandlerExpr(entry.c_symbol, fallback)
                             : buildDirectHandlerExpr(entry.c_symbol, fallback);
    }

    fields.lowering = (entry.lowering == "always") ? "kAlwaysLowering" : "kManualLowering";
    fields.hidden = "nullptr";
    fields.hiddenCount = "0";
    fields.trapClass = "RuntimeTrapClass::None";
    return fields;
}

/// @brief Emit one `DescriptorRow{...}` initializer for the signatures table.
/// @param out Output stream for the generated code.
/// @param name Canonical name for the row.
/// @param fields Pre-computed descriptor fields.
/// @param indent Leading indentation (spaces).
static void emitDescriptorRow(std::ostream &out,
                              const std::string &name,
                              const DescriptorFields &fields,
                              int indent = 4) {
    std::string pad(static_cast<size_t>(indent), ' ');
    out << pad << "DescriptorRow{" << cppStringLiteral(name) << ",\n";
    out << pad << "              " << fields.signatureId << ",\n";
    out << pad << "              " << fields.spec << ",\n";
    out << pad << "              " << fields.handler << ",\n";
    out << pad << "              " << fields.lowering << ",\n";
    out << pad << "              " << fields.hidden << ",\n";
    out << pad << "              " << fields.hiddenCount << ",\n";
    out << pad << "              " << fields.trapClass << "},\n";
}

/// @brief Generate RuntimeNameMap.inc: canonical Viper.* → C rt_* symbol mappings.
/// @details Emits a RUNTIME_NAME_ALIAS row for every function and every alias
///          (resolving aliases to their target's C symbol). Fatal error on write
///          failure.
static void generateNameMap(const ParseState &state, const fs::path &outDir) {
    fs::path outPath = outDir / "RuntimeNameMap.inc";
    std::ostringstream out;

    out << fileHeader("RuntimeNameMap.inc",
                      "Canonical Viper.* to C rt_* symbol mapping for native codegen.");

    // Emit primary mappings
    for (const auto &func : state.functions) {
        out << "RUNTIME_NAME_ALIAS(" << cppStringLiteral(func.canonical) << ", "
            << cppStringLiteral(func.c_symbol) << ")\n";
    }

    // Emit aliases
    for (const auto &alias : state.aliases) {
        auto it = state.func_by_id.find(alias.target_id);
        if (it != state.func_by_id.end()) {
            const auto &target = state.functions[it->second];
            out << "RUNTIME_NAME_ALIAS(" << cppStringLiteral(alias.canonical) << ", "
                << cppStringLiteral(target.c_symbol) << ")\n";
        }
    }

    writeGeneratedTextFile(outPath, out);
    std::cout << "  Generated " << outPath << "\n";
}

/// @brief Generate RuntimeClasses.inc: the OOP class/property/method catalog.
/// @details Resolves classes via buildResolvedClasses() and emits a RUNTIME_CLASS
///          block per class containing RUNTIME_PROPS and RUNTIME_METHODS lists.
///          Fatal error on write failure.
static void generateClasses(const ParseState &state, const fs::path &outDir) {
    fs::path outPath = outDir / "RuntimeClasses.inc";
    std::ostringstream out;

    out << fileHeader("RuntimeClasses.inc", "Runtime class catalog with properties and methods.");

    for (const auto &cls : buildResolvedClasses(state)) {
        out << "RUNTIME_CLASS(\n";
        out << "    " << cppStringLiteral(cls.name) << ",\n";
        out << "    RTCLS_" << cls.type_id << ",\n";
        out << "    " << cppStringLiteral(cls.layout) << ",\n";

        if (cls.ctorCanonical.empty()) {
            out << "    " << cppStringLiteral("") << ",\n";
        } else {
            out << "    " << cppStringLiteral(cls.ctorCanonical) << ",\n";
        }

        out << "    RUNTIME_PROPS(";
        for (size_t i = 0; i < cls.props.size(); ++i) {
            const auto &prop = cls.props[i];
            if (i > 0)
                out << ",\n                  ";

            out << "RUNTIME_PROP(" << cppStringLiteral(prop.name) << ", "
                << cppStringLiteral(prop.type) << ", " << cppStringLiteral(prop.getterCanonical)
                << ", ";

            if (prop.setterCanonical == "none" || prop.setterCanonical.empty()) {
                out << "nullptr";
            } else {
                out << cppStringLiteral(prop.setterCanonical);
            }
            out << ")";
        }
        out << "),\n";

        out << "    RUNTIME_METHODS(";
        for (size_t i = 0; i < cls.methods.size(); ++i) {
            const auto &method = cls.methods[i];
            if (i > 0)
                out << ",\n                    ";
            out << "RUNTIME_METHOD(" << cppStringLiteral(method.name) << ", "
                << cppStringLiteral(method.signature) << ", "
                << cppStringLiteral(method.targetCanonical) << ")";
        }
        out << "))\n\n";
    }

    writeGeneratedTextFile(outPath, out);
    std::cout << "  Generated " << outPath << "\n";
}

/// @brief Generate RuntimeSignatures.inc: the descriptor row per runtime function.
/// @details Loads C signatures from the runtime headers and the RtSig map, then
///          emits a DescriptorRow for every function via buildDefaultDescriptor()
///          and emitDescriptorRow(). Fatal error on write failure.
/// @param state Parsed runtime definitions.
/// @param outDir Directory to write the .inc file into.
/// @param inputPath Path to runtime.def, used to locate the runtime tree.
static void generateSignatures(const ParseState &state,
                               const fs::path &outDir,
                               const fs::path &inputPath) {
    fs::path outPath = outDir / "RuntimeSignatures.inc";
    const fs::path runtimeDir = inputPath.parent_path();

    std::ostringstream out;

    out << fileHeader("RuntimeSignatures.inc",
                      "Runtime descriptor rows for all runtime functions.");

    const fs::path srcRoot = runtimeDir.parent_path().parent_path();
    const fs::path repoRoot = srcRoot.parent_path();
    const fs::path runtimeHeaders = srcRoot / "runtime";
    auto cSignatures = loadRuntimeCSignatures(runtimeHeaders, repoRoot);
    auto rtSigMap = buildRtSigMap(runtimeDir);

    // Build entries from functions and aliases
    std::unordered_map<std::string, RuntimeEntry> entries;
    entries.reserve(state.functions.size() + state.aliases.size());

    std::unordered_map<std::string, const RuntimeFunc *> cSymbolToFunc;
    for (const auto &func : state.functions) {
        entries.emplace(func.canonical,
                        RuntimeEntry{func.canonical, func.c_symbol, func.signature, func.lowering});
        cSymbolToFunc[func.c_symbol] = &func;
    }
    for (const auto &alias : state.aliases) {
        auto it = state.func_by_id.find(alias.target_id);
        if (it == state.func_by_id.end())
            continue;
        const auto &target = state.functions[it->second];
        entries.emplace(
            alias.canonical,
            RuntimeEntry{alias.canonical, target.c_symbol, target.signature, target.lowering});
    }

    // Emit canonical entries in definition order
    std::vector<std::string> orderedNames;
    orderedNames.reserve(entries.size());
    for (const auto &func : state.functions)
        orderedNames.push_back(func.canonical);
    for (const auto &alias : state.aliases)
        orderedNames.push_back(alias.canonical);

    for (const auto &name : orderedNames) {
        auto entryIt = entries.find(name);
        if (entryIt == entries.end())
            continue;

        const RuntimeEntry &entry = entryIt->second;
        DescriptorFields fields = buildDefaultDescriptor(entry, cSignatures, rtSigMap);
        emitDescriptorRow(out, name, fields);
    }

    writeGeneratedTextFile(outPath, out);
    std::cout << "  Generated " << outPath << "\n";
}

// Set of known runtime class qnames, populated before ZiaExterns generation.
static std::unordered_set<std::string> knownClassNames;

/// @brief Map IL return type to Zia types:: expression.
static std::string ilTypeToZiaType(const std::string &ilType, const std::string &canonical) {
    // Handle optional return types (trailing '?')
    if (!ilType.empty() && ilType.back() == '?') {
        std::string inner = ilType.substr(0, ilType.size() - 1);
        return "types::optional(" + ilTypeToZiaType(inner, canonical) + ")";
    }

    std::string baseType = stripTypeArgs(ilType);
    std::string typeArg = extractTypeArg(ilType);

    if (baseType == "str")
        return "types::string()";
    if (baseType == "i64" || baseType == "i32" || baseType == "i16" || baseType == "i8")
        return "types::integer()";
    if (baseType == "f64" || baseType == "f32")
        return "types::number()";
    if (baseType == "i1" || baseType == "bool")
        return "types::boolean()";
    if (baseType == "void")
        return "types::voidType()";
    if (baseType == "seq") {
        if (typeArg.empty())
            return "types::runtimeClass(\"Viper.Collections.Seq\")";
        return "types::seqOf(" + ilTypeToZiaType(typeArg, canonical) + ")";
    }
    if (baseType == "list") {
        if (typeArg.empty())
            return "types::runtimeClass(\"Viper.Collections.List\")";
        return "types::list(" + ilTypeToZiaType(typeArg, canonical) + ")";
    }
    if (baseType == "obj" && !typeArg.empty())
        return "types::runtimeClass(\"" + typeArg + "\")";
    if (baseType == "ptr")
        return "types::ptr()";
    if (baseType == "obj") {
        // Explicit return type overrides for functions that return objects
        // from a different namespace than their own.
        static const std::unordered_map<std::string, std::string> returnTypeOverrides = {
            // Crypto functions returning Viper.Collections.Bytes
            {"Viper.Crypto.Rand.Bytes", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.Encrypt", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.Decrypt", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.EncryptAAD", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.DecryptAAD", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.EncryptWithKey", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.DecryptWithKey", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.EncryptWithKeyAAD", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.DecryptWithKeyAAD", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.GenerateKey", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.DeriveKey", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Aes.Encrypt", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Aes.Decrypt", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Aes.EncryptAuth", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Aes.DecryptAuth", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Aes.EncryptStr", "Viper.Collections.Bytes"},
            {"Viper.Crypto.KeyDerive.Pbkdf2SHA256", "Viper.Collections.Bytes"},
            {"Viper.Crypto.KeyDerive.ScryptSHA256", "Viper.Collections.Bytes"},
            // IO functions returning Viper.Collections.Bytes
            {"Viper.IO.Stream.ToBytes", "Viper.Collections.Bytes"},
            // Text functions returning Viper.Collections.Seq
            {"Viper.Text.Csv.ParseLine", "Viper.Collections.Seq"},
            {"Viper.Text.Csv.ParseLineWith", "Viper.Collections.Seq"},
            {"Viper.Text.Csv.Parse", "Viper.Collections.Seq"},
            {"Viper.Text.Csv.ParseWith", "Viper.Collections.Seq"},
            {"Viper.Text.Markdown.ExtractLinks", "Viper.Collections.Seq"},
            {"Viper.Text.Markdown.ExtractHeadings", "Viper.Collections.Seq"},
            {"Viper.Text.Html.ExtractLinks", "Viper.Collections.Seq"},
            {"Viper.Text.Html.ExtractText", "Viper.Collections.Seq"},
            // Collection methods returning Seq from non-Seq classes
            {"Viper.Collections.Bag.Items", "Viper.Collections.Seq"},
            {"Viper.Collections.SortedSet.Items", "Viper.Collections.Seq"},
            {"Viper.Collections.Set.Items", "Viper.Collections.Seq"},
        };

        auto overrideIt = returnTypeOverrides.find(canonical);
        if (overrideIt != returnTypeOverrides.end())
            return "types::runtimeClass(\"" + overrideIt->second + "\")";

        // For factory methods that return instances of their class, derive the class type
        // e.g., "Viper.GUI.App.New" -> runtimeClass("Viper.GUI.App")
        // e.g., "Viper.Graphics.Pixels.LoadBmp" -> runtimeClass("Viper.Graphics.Pixels")
        size_t lastDot = canonical.rfind('.');
        if (lastDot != std::string::npos) {
            std::string method = canonical.substr(lastDot + 1);
            // Check for factory method patterns (exact matches or prefixes)
            bool isFactory = (method == "New" || method == "Clone" || method == "Copy" ||
                              method == "Zero" || method == "Range");
            // Also check for prefix patterns like Open*, Load*, From*, etc.
            if (!isFactory) {
                isFactory = (method.rfind("Open", 0) == 0 || method.rfind("Load", 0) == 0 ||
                             method.rfind("From", 0) == 0 || method.rfind("Parse", 0) == 0 ||
                             method.rfind("Read", 0) == 0 || method.rfind("Decode", 0) == 0 ||
                             method.rfind("Create", 0) == 0);
            }
            if (isFactory) {
                std::string className = canonical.substr(0, lastDot);
                return "types::runtimeClass(\"" + className + "\")";
            }
            // For any method on a known runtime class that returns obj,
            // infer the class type (e.g., Vec2.Add returns Vec2).
            if (!knownClassNames.empty()) {
                std::string className = canonical.substr(0, lastDot);
                if (knownClassNames.count(className))
                    return "types::runtimeClass(\"" + className + "\")";
            }
        }
        return "types::any()";
    }
    // Default unrecognized object-like tokens to safe type erasure.
    return "types::any()";
}

/// @brief Map IL parameter type to a Zia types:: expression.
static std::string ilParamTypeToZiaType(const std::string &ilType) {
    if (!ilType.empty() && ilType.back() == '?') {
        std::string inner = ilType.substr(0, ilType.size() - 1);
        return "types::optional(" + ilParamTypeToZiaType(inner) + ")";
    }

    std::string baseType = stripTypeArgs(ilType);

    if (baseType == "str")
        return "types::string()";
    if (baseType == "i64" || baseType == "i32" || baseType == "i16" || baseType == "i8")
        return "types::integer()";
    if (baseType == "f64" || baseType == "f32")
        return "types::number()";
    if (baseType == "i1" || baseType == "bool")
        return "types::boolean()";
    if (baseType == "void")
        return "types::voidType()";
    if (baseType == "seq") {
        std::string typeArg = extractTypeArg(ilType);
        if (typeArg.empty())
            return "types::ptr()";
        return "types::seqOf(" + ilParamTypeToZiaType(typeArg) + ")";
    }
    if (baseType == "list") {
        std::string typeArg = extractTypeArg(ilType);
        if (typeArg.empty())
            return "types::ptr()";
        return "types::list(" + ilParamTypeToZiaType(typeArg) + ")";
    }
    if (baseType == "obj")
        return "types::any()";
    if (baseType == "ptr")
        return "types::ptr()";
    return "types::any()";
}

/// @brief Emit a `, { "name", ... }` brace-list of Zia extern parameter names.
static void emitZiaParamNames(std::ostream &out, const std::vector<std::string> &paramNames) {
    out << ", {";
    for (size_t i = 0; i < paramNames.size(); ++i) {
        if (i > 0)
            out << ", ";
        out << cppStringLiteral(paramNames[i]);
    }
    out << "}";
}

/// @brief Return true if an IL type token denotes a raw `ptr` (after trimming
///        whitespace, an optional `?`, and any generic arguments).
static bool ziaTypeTokenIsRawPointer(std::string type) {
    while (!type.empty() && std::isspace(static_cast<unsigned char>(type.front())))
        type.erase(type.begin());
    while (!type.empty() && std::isspace(static_cast<unsigned char>(type.back())))
        type.pop_back();
    if (!type.empty() && type.back() == '?')
        type.pop_back();
    return stripTypeArgs(type) == "ptr";
}

/// @brief Map a bridge role string to its RuntimePointerBridgeRole enum expression.
static std::string bridgeRoleExpr(const std::string &role) {
    if (role == "callback")
        return "RuntimePointerBridgeRole::Callback";
    if (role == "payload")
        return "RuntimePointerBridgeRole::Payload";
    return "RuntimePointerBridgeRole::None";
}

/// @brief Emit a `, RuntimePointerSafety{...}` initializer for a Zia extern.
/// @details Records whether the return and each argument is a raw pointer plus
///          the per-argument bridge role, so the Zia frontend can enforce pointer
///          safety at call sites.
static void emitZiaPointerSafety(std::ostream &out,
                                 const ParsedSignature &sig,
                                 const std::vector<std::string> &bridgeRoles) {
    out << ", RuntimePointerSafety{"
        << (ziaTypeTokenIsRawPointer(sig.returnType) ? "true" : "false") << ", {";
    for (size_t i = 0; i < sig.argTypes.size(); ++i) {
        if (i > 0)
            out << ", ";
        out << (ziaTypeTokenIsRawPointer(sig.argTypes[i]) ? "true" : "false");
    }
    out << "}, {";
    for (size_t i = 0; i < sig.argTypes.size(); ++i) {
        if (i > 0)
            out << ", ";
        std::string role = i < bridgeRoles.size() ? bridgeRoles[i] : "none";
        out << bridgeRoleExpr(role);
    }
    out << "}}";
}

/// @brief Determine the Zia extern's parameter names for a runtime function.
/// @details Takes the trailing surface-parameter names from the C prototype (the
///          last N, since leading C params may be hidden receivers), padding with
///          empty names if the prototype has fewer. Returns empty when there is no
///          prototype or no surface parameters.
static std::vector<std::string> ziaExternParamNamesFor(const RuntimeFunc &func,
                                                       const RuntimePrototype *proto) {
    if (!proto)
        return {};

    ParsedSignature sig = parseSignature(func.signature);
    const size_t surfaceCount = sig.argTypes.size();
    if (surfaceCount == 0)
        return {};

    std::vector<std::string> names;
    if (proto->paramNames.size() >= surfaceCount) {
        names.assign(proto->paramNames.end() - static_cast<std::ptrdiff_t>(surfaceCount),
                     proto->paramNames.end());
    } else {
        names = proto->paramNames;
    }

    while (names.size() < surfaceCount)
        names.push_back({});
    return names;
}

/// @brief Generate the Zia extern declarations table for runtime functions.
/// @details Emits each runtime function as a Zia extern with its mapped Zia
///          parameter/return types, parameter names recovered from the C
///          prototype, and pointer-safety/bridge-role metadata. Fatal error on
///          write failure.
static void generateZiaExterns(const ParseState &state,
                               const fs::path &outDir,
                               const fs::path &inputPath) {
    // Populate known class names for type inference
    knownClassNames.clear();
    for (const auto &cls : state.classes)
        knownClassNames.insert(cls.name);

    fs::path outPath = outDir / "ZiaRuntimeExterns.inc";
    std::ostringstream out;

    out << fileHeader("ZiaRuntimeExterns.inc",
                      "Zia frontend extern function definitions generated from runtime.def.");

    out << "// This file is included by Sema_Runtime.cpp to register all runtime functions.\n";
    out << "// Usage: #include \"il/runtime/ZiaRuntimeExterns.inc\"\n\n";

    const fs::path runtimeDir = inputPath.parent_path();
    const fs::path srcRoot = runtimeDir.parent_path().parent_path();
    const fs::path repoRoot = srcRoot.parent_path();
    const fs::path runtimeHeaders = srcRoot / "runtime";
    auto headerDecls = loadRuntimeHeaderDeclarations(runtimeHeaders, repoRoot);

    // First emit type registry entries for runtime classes
    out << "// " << std::string(75, '=') << "\n";
    out << "// RUNTIME CLASS TYPE REGISTRATIONS\n";
    out << "// " << std::string(75, '=') << "\n";
    for (const auto &cls : state.classes) {
        out << "typeRegistry_[" << cppStringLiteral(cls.name) << "] = types::runtimeClass("
            << cppStringLiteral(cls.name) << ");\n";
    }
    out << "\n";

    // Group functions by namespace for readability
    std::map<std::string, std::vector<const RuntimeFunc *>> byNamespace;

    for (const auto &func : state.functions) {
        // Extract namespace: "Viper.GUI.App.New" -> "Viper.GUI"
        size_t firstDot = func.canonical.find('.');
        size_t secondDot = func.canonical.find('.', firstDot + 1);
        std::string ns = "Other";
        if (secondDot != std::string::npos) {
            ns = func.canonical.substr(0, secondDot);
        }
        byNamespace[ns].push_back(&func);
    }

    // Each namespace group is wrapped in a lambda to limit stack depth.
    // MSVC Debug mode allocates separate stack slots for every temporary in a
    // function, so 4000+ inline defineExternFunction calls (each creating
    // temporary vectors) would blow the default 1 MB Windows stack.  The lambda
    // boundary forces each batch into its own stack frame.
    for (const auto &[ns, funcs] : byNamespace) {
        out << "// " << std::string(75, '=') << "\n";
        out << "// " << ns << "\n";
        out << "// " << std::string(75, '=') << "\n";
        out << "[&]() {\n";

        for (const auto *func : funcs) {
            ParsedSignature sig = parseSignature(func->signature);
            std::string ziaType = ilTypeToZiaType(sig.returnType, func->canonical);
            out << "defineExternFunction(" << cppStringLiteral(func->canonical) << ", " << ziaType;
            out << ", {";
            for (size_t i = 0; i < sig.argTypes.size(); ++i) {
                if (i > 0)
                    out << ", ";
                out << ilParamTypeToZiaType(sig.argTypes[i]);
            }
            out << "}";
            if (auto declIt = headerDecls.find(func->c_symbol); declIt != headerDecls.end()) {
                auto paramNames = ziaExternParamNamesFor(*func, &declIt->second);
                emitZiaParamNames(out, paramNames);
            } else {
                emitZiaParamNames(out, {});
            }
            emitZiaPointerSafety(out, sig, func->bridgeRoles);
            out << ");\n";
        }
        out << "}();\n\n";
    }

    // Also emit aliases
    if (!state.aliases.empty()) {
        out << "// " << std::string(75, '=') << "\n";
        out << "// ALIASES\n";
        out << "// " << std::string(75, '=') << "\n";
        out << "[&]() {\n";

        for (const auto &alias : state.aliases) {
            auto it = state.func_by_id.find(alias.target_id);
            if (it != state.func_by_id.end()) {
                const auto &target = state.functions[it->second];
                ParsedSignature sig = parseSignature(target.signature);
                std::string ziaType = ilTypeToZiaType(sig.returnType, alias.canonical);
                out << "defineExternFunction(" << cppStringLiteral(alias.canonical) << ", "
                    << ziaType;
                out << ", {";
                for (size_t i = 0; i < sig.argTypes.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << ilParamTypeToZiaType(sig.argTypes[i]);
                }
                out << "}";
                if (auto declIt = headerDecls.find(target.c_symbol); declIt != headerDecls.end()) {
                    auto paramNames = ziaExternParamNamesFor(target, &declIt->second);
                    emitZiaParamNames(out, paramNames);
                } else {
                    emitZiaParamNames(out, {});
                }
                emitZiaPointerSafety(out, sig, target.bridgeRoles);
                out << ");\n";
            }
        }
        out << "}();\n";
        out << "\n";
    }

    writeGeneratedTextFile(outPath, out);
    std::cout << "  Generated " << outPath << "\n";
}

/// @brief Convert a canonical name to a C++ constant identifier.
/// @details "Viper.String.Concat" -> "kStringConcat"
///          "Viper.Time.DateTime.Now" -> "kTimeDateTimeNow"
static std::string canonicalToIdentifier(const std::string &canonical) {
    // Skip "Viper." prefix
    std::string name = canonical;
    if (name.substr(0, 6) == "Viper.") {
        name = name.substr(6);
    }

    // Convert dots to nothing, capitalize each segment
    std::string result = "k";
    bool capitalizeNext = true;

    for (char c : name) {
        if (c == '.') {
            capitalizeNext = true;
        } else if (c == '_') {
            capitalizeNext = true;
        } else {
            if (capitalizeNext) {
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                capitalizeNext = false;
            } else {
                result += c;
            }
        }
    }

    return result;
}

/// @brief Generate RuntimeNames.hpp: C++ constants exposing canonical names to the
///        frontends. Fatal error on write failure.
static void generateFrontendNames(const ParseState &state, const fs::path &outDir) {
    fs::path outPath = outDir / "RuntimeNames.hpp";
    std::ostringstream out;

    out << "//===----------------------------------------------------------------------===//\n";
    out << "//\n";
    out << "// AUTO-GENERATED FILE - DO NOT EDIT\n";
    out << "// Generated by rtgen from runtime.def\n";
    out << "//\n";
    out << "//===----------------------------------------------------------------------===//\n";
    out << "//\n";
    out << "// File: RuntimeNames.hpp\n";
    out << "// Purpose: Canonical runtime function name constants for all frontends.\n";
    out << "//\n";
    out << "// Usage: #include \"il/runtime/RuntimeNames.hpp\"\n";
    out << "//        Then use il::runtime::names::kStringConcat etc.\n";
    out << "//\n";
    out << "//===----------------------------------------------------------------------===//\n\n";

    out << "#pragma once\n\n";
    out << "namespace il::runtime::names {\n\n";

    // Group functions by namespace for readability
    std::map<std::string, std::vector<const RuntimeFunc *>> byNamespace;
    std::set<std::string> emittedIdentifiers; // Track duplicates

    for (const auto &func : state.functions) {
        // Extract namespace: "Viper.String.Concat" -> "Viper.String"
        size_t lastDot = func.canonical.rfind('.');
        std::string ns = "Other";
        if (lastDot != std::string::npos) {
            ns = func.canonical.substr(0, lastDot);
        }
        byNamespace[ns].push_back(&func);
    }

    for (const auto &[ns, funcs] : byNamespace) {
        out << "// " << std::string(75, '=') << "\n";
        out << "// " << ns << "\n";
        out << "// " << std::string(75, '=') << "\n\n";

        for (const auto *func : funcs) {
            std::string id = canonicalToIdentifier(func->canonical);

            // Handle duplicate identifiers by appending a suffix
            std::string uniqueId = id;
            int suffix = 2;
            while (emittedIdentifiers.count(uniqueId)) {
                uniqueId = id + std::to_string(suffix++);
            }
            emittedIdentifiers.insert(uniqueId);

            out << "/// @brief " << func->canonical << "\n";
            out << "inline constexpr const char *" << uniqueId << " = "
                << cppStringLiteral(func->canonical) << ";\n\n";
        }
    }

    // Also emit aliases
    if (!state.aliases.empty()) {
        out << "// " << std::string(75, '=') << "\n";
        out << "// ALIASES\n";
        out << "// " << std::string(75, '=') << "\n\n";

        for (const auto &alias : state.aliases) {
            std::string id = canonicalToIdentifier(alias.canonical);

            // Handle duplicate identifiers
            std::string uniqueId = id;
            int suffix = 2;
            while (emittedIdentifiers.count(uniqueId)) {
                uniqueId = id + std::to_string(suffix++);
            }
            emittedIdentifiers.insert(uniqueId);

            out << "/// @brief " << alias.canonical << " (alias)\n";
            out << "inline constexpr const char *" << uniqueId << " = "
                << cppStringLiteral(alias.canonical) << ";\n\n";
        }
    }

    out << "} // namespace il::runtime::names\n";

    writeGeneratedTextFile(outPath, out);
    std::cout << "  Generated " << outPath << "\n";
}

/// @brief Audit runtime.def against the actual runtime headers/sources and policy.
/// @details Cross-checks that declared functions exist, that runtime symbols are
///          classified, and that the surface policy's expectations hold; prints a
///          report and returns non-zero when strict checks fail.
/// @param state Parsed runtime definitions.
/// @param inputPath Path to runtime.def (locates the runtime tree and policy).
/// @param strictHeaderSync Treat header/def drift as a failure.
/// @param strictUnclassified Treat unclassified runtime symbols as a failure.
/// @param summaryOnly Print only the summary counts, not per-item detail.
/// @return 0 when the audit passes (under the chosen strictness), non-zero otherwise.
static int runAudit(const ParseState &state,
                    const fs::path &inputPath,
                    bool strictHeaderSync,
                    bool strictUnclassified,
                    bool summaryOnly) {
    const fs::path runtimeDir = inputPath.parent_path();
    const fs::path srcRoot = runtimeDir.parent_path().parent_path();
    const fs::path repoRoot = srcRoot.parent_path();
    const fs::path runtimeRoot = srcRoot / "runtime";
    const fs::path policyPath = runtimeDir / "RuntimeSurfacePolicy.inc";

    RuntimeSurfacePolicy policy = parseRuntimeSurfacePolicy(policyPath);
    auto headerDecls = loadRuntimeHeaderDeclarations(runtimeRoot, repoRoot);
    auto sourceTokens = loadRuntimeSourceTokens(runtimeRoot);
    auto resolvedClasses = buildResolvedClasses(state);

    std::unordered_map<std::string, std::string> canonicalToSymbol;
    canonicalToSymbol.reserve(state.functions.size() + state.aliases.size());
    for (const auto &func : state.functions)
        canonicalToSymbol.emplace(func.canonical, func.c_symbol);
    for (const auto &alias : state.aliases) {
        if (auto symbol = resolveRuntimeSymbol(state, alias.canonical); symbol && !symbol->empty())
            canonicalToSymbol.emplace(alias.canonical, *symbol);
    }

    std::unordered_map<std::string, const ResolvedRuntimeClass *> classesByName;
    for (const auto &cls : resolvedClasses)
        classesByName.emplace(cls.name, &cls);

    std::unordered_set<std::string> publicSymbols;
    for (const auto &func : state.functions)
        publicSymbols.insert(func.c_symbol);

    std::vector<std::string> errors;
    std::vector<std::string> headerSyncFindings;
    std::vector<std::string> unclassifiedFindings;
    auto addError = [&](std::string msg) { errors.push_back(std::move(msg)); };
    auto addHeaderFinding = [&](std::string msg) { headerSyncFindings.push_back(std::move(msg)); };
    auto addUnclassifiedFinding = [&](std::string msg) {
        unclassifiedFindings.push_back(std::move(msg));
    };

    for (const auto &func : state.functions) {
        if (signatureExposesRawPointer(func.signature)) {
            addError("runtime.def function " + func.canonical +
                     " exposes raw ptr in frontend signature " + func.signature);
        }
        if (headerDecls.find(func.c_symbol) == headerDecls.end()) {
            addHeaderFinding("runtime.def function " + func.canonical + " maps to " +
                             func.c_symbol +
                             " but no declaration was found in src/runtime headers");
        }
        if (sourceTokens.count(func.c_symbol) == 0) {
            addError("runtime.def function " + func.canonical + " maps to " + func.c_symbol +
                     " but no implementation token was found in src/runtime sources");
        }
    }

    for (const auto &cls : state.classes) {
        if (!cls.ctor_id.empty() && cls.ctor_id != "none" &&
            !resolveRuntimeCanonical(state, cls.ctor_id)) {
            addError("runtime class " + cls.name + " has unresolved ctor target " + cls.ctor_id);
        }
        for (const auto &prop : cls.props) {
            if (!prop.getter_id.empty() && prop.getter_id != "none" &&
                !resolveRuntimeCanonical(state, prop.getter_id)) {
                addError("runtime property " + cls.name + "." + prop.name +
                         " has unresolved getter target " + prop.getter_id);
            }
            if (!prop.setter_id.empty() && prop.setter_id != "none" &&
                !resolveRuntimeCanonical(state, prop.setter_id)) {
                addError("runtime property " + cls.name + "." + prop.name +
                         " has unresolved setter target " + prop.setter_id);
            }
        }
        for (const auto &method : cls.methods) {
            if (signatureExposesRawPointer(method.signature)) {
                addError("runtime method " + cls.name + "." + method.name +
                         " exposes raw ptr in frontend signature " + method.signature);
            }
            if (!method.target_id.empty() && method.target_id != "none" &&
                !resolveRuntimeCanonical(state, method.target_id)) {
                addError("runtime method " + cls.name + "." + method.name +
                         " has unresolved target " + method.target_id);
            }
        }
    }

    for (const auto &[canonical, expectedSymbol] : policy.expectedFunctions) {
        auto it = canonicalToSymbol.find(canonical);
        if (it == canonicalToSymbol.end()) {
            addError("runtime surface policy expects function " + canonical +
                     " but it is missing from runtime.def");
            continue;
        }
        if (it->second != expectedSymbol) {
            addError("runtime surface policy expects " + canonical + " to map to " +
                     expectedSymbol + " but runtime.def maps it to " + it->second);
        }
    }

    for (const auto &expected : policy.expectedMethods) {
        auto classIt = classesByName.find(expected.targetCanonical);
        if (classIt == classesByName.end()) {
            addError("runtime surface policy expects class " + expected.targetCanonical +
                     " but it is missing from the runtime catalog");
            continue;
        }

        bool found = false;
        for (const auto &method : classIt->second->methods) {
            if (method.name == expected.name && method.signature == expected.signature) {
                found = true;
                break;
            }
        }
        if (!found) {
            addError("runtime surface policy expects method " + expected.targetCanonical + "." +
                     expected.name + " with signature " + expected.signature +
                     " but it is missing from the runtime catalog");
        }
    }

    for (const auto &expected : policy.expectedProperties) {
        auto classIt = classesByName.find(expected.getterCanonical);
        if (classIt == classesByName.end()) {
            addError("runtime surface policy expects class " + expected.getterCanonical +
                     " but it is missing from the runtime catalog");
            continue;
        }

        bool found = false;
        for (const auto &prop : classIt->second->props) {
            if (prop.name == expected.name && prop.type == expected.type) {
                found = true;
                break;
            }
        }
        if (!found) {
            addError("runtime surface policy expects property " + expected.getterCanonical + "." +
                     expected.name + " : " + expected.type +
                     " but it is missing from the runtime catalog");
        }
    }

    for (const auto &[symbol, proto] : headerDecls) {
        if (policy.internalSymbols.count(symbol))
            continue;
        if (policy.internalHeaders.count(proto.headerPath))
            continue;
        if (publicSymbols.count(symbol))
            continue;

        addUnclassifiedFinding("unclassified runtime header symbol " + symbol + " declared in " +
                               proto.headerPath + " is not represented in runtime.def or policy");
    }

    std::sort(errors.begin(), errors.end());
    std::sort(headerSyncFindings.begin(), headerSyncFindings.end());
    std::sort(unclassifiedFindings.begin(), unclassifiedFindings.end());

    std::cout << "rtgen audit: " << state.functions.size() << " functions, " << state.aliases.size()
              << " aliases, " << state.classes.size() << " classes, " << headerDecls.size()
              << " header declarations\n";

    if (!summaryOnly) {
        for (const auto &finding : headerSyncFindings)
            std::cerr << "warning: " << finding << "\n";
        for (const auto &finding : unclassifiedFindings)
            std::cerr << "warning: " << finding << "\n";
        for (const auto &error : errors)
            std::cerr << "error: " << error << "\n";
    }

    if (strictHeaderSync && !headerSyncFindings.empty()) {
        std::cerr << "error: strict header sync mode is enabled and " << headerSyncFindings.size()
                  << " runtime.def/header mismatch(es) were found\n";
        return 1;
    }
    if (strictUnclassified && !unclassifiedFindings.empty()) {
        std::cerr << "error: strict unclassified mode is enabled and "
                  << unclassifiedFindings.size()
                  << " unclassified runtime header symbol(s) were found\n";
        return 1;
    }
    if (!errors.empty()) {
        std::cerr << "rtgen audit failed with " << errors.size() << " error(s)";
        if (!headerSyncFindings.empty())
            std::cerr << ", " << headerSyncFindings.size() << " header-sync finding(s)";
        if (!unclassifiedFindings.empty())
            std::cerr << ", and " << unclassifiedFindings.size() << " unclassified finding(s)";
        std::cerr << "\n";
        return 1;
    }

    std::cout << "rtgen audit passed";
    if (!headerSyncFindings.empty() || !unclassifiedFindings.empty()) {
        std::cout << " with " << headerSyncFindings.size() << " header-sync finding(s) and "
                  << unclassifiedFindings.size() << " unclassified finding(s)";
    }
    std::cout << "\n";
    return 0;
}

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

/// @brief Print rtgen command-line usage (generate and audit modes) to stderr.
static void printUsage(const char *prog) {
    std::cerr << "Usage: " << prog << " <input.def> <output_dir>\n";
    std::cerr
        << "       " << prog
        << " --audit [--strict-header-sync] [--strict-unclassified] [--summary-only] <input.def>\n";
    std::cerr << "\n";
    std::cerr << "Generates runtime registry .inc files from runtime.def\n";
}

/// @brief rtgen entry point: parse runtime.def and either audit it or generate the
///        registry .inc files.
/// @details Parses flags (--audit and its --strict-*/--summary-only modifiers),
///          validates positional arguments, then in audit mode runs runAudit() and
///          in generate mode writes RuntimeNameMap.inc, RuntimeClasses.inc,
///          RuntimeSignatures.inc, the Zia externs, and RuntimeNames.hpp.
/// @param argc Argument count from the C runtime.
/// @param argv Argument vector from the C runtime.
/// @return 0 on success; non-zero on usage error or audit failure.
int main(int argc, char **argv) {
    bool auditMode = false;
    bool strictHeaderSync = false;
    bool strictUnclassified = false;
    bool summaryOnly = false;
    std::vector<std::string> positional;
    positional.reserve(static_cast<size_t>(argc));
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--audit") {
            auditMode = true;
        } else if (arg == "--strict-header-sync") {
            strictHeaderSync = true;
        } else if (arg == "--strict-unclassified") {
            strictUnclassified = true;
        } else if (arg == "--summary-only") {
            summaryOnly = true;
        } else {
            positional.push_back(std::move(arg));
        }
    }

    if ((!auditMode && positional.size() != 2) || (auditMode && positional.size() != 1)) {
        printUsage(argv[0]);
        return 1;
    }

    fs::path inputPath = positional[0];

    std::error_code inputEc;
    if (!fs::exists(inputPath, inputEc)) {
        std::cerr << "error: input file not found: " << inputPath << "\n";
        return 1;
    }

    ParseState state;
    try {
        std::cout << "rtgen: Parsing " << inputPath << "\n";
        state = parseFile(inputPath);
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    if (auditMode)
        return runAudit(state, inputPath, strictHeaderSync, strictUnclassified, summaryOnly);

    fs::path outputDir = positional[1];

    // Create output directory if needed
    std::error_code ec;
    if (!fs::exists(outputDir, ec)) {
        if (!fs::create_directories(outputDir, ec) || ec) {
            std::cerr << "error: cannot create output directory " << outputDir << ": "
                      << ec.message() << "\n";
            return 1;
        }
    }

    std::cout << "rtgen: Parsed " << state.functions.size() << " functions, "
              << state.aliases.size() << " aliases, " << state.classes.size() << " classes\n";

    std::cout << "rtgen: Generating output files in " << outputDir << "\n";
    try {
        generateNameMap(state, outputDir);
        generateClasses(state, outputDir);
        generateSignatures(state, outputDir, inputPath);
        generateZiaExterns(state, outputDir, inputPath);
        generateFrontendNames(state, outputDir);
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "rtgen: Done\n";
    return 0;
}

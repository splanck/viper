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

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

//===----------------------------------------------------------------------===//
// Data Structures
//===----------------------------------------------------------------------===//

struct RuntimeFunc {
    std::string id;        // Unique identifier (e.g., "PrintStr")
    std::string c_symbol;  // C runtime symbol (e.g., "rt_print_str")
    std::string canonical; // Canonical Viper.* name (e.g., "Viper.Console.PrintStr")
    std::string signature; // Type signature (e.g., "void(str)")
    std::string lowering;  // Lowering kind: "always" or "" (default: manual)
};

struct RuntimeAlias {
    std::string canonical; // Alias canonical name
    std::string target_id; // Target function id
};

struct RuntimeProperty {
    std::string name;      // Property name (e.g., "Length")
    std::string type;      // Property type (e.g., "i64")
    std::string getter_id; // Getter function id (or canonical name)
    std::string setter_id; // Setter function id or "none"
};

struct RuntimeMethod {
    std::string name;      // Method name (e.g., "Substring")
    std::string signature; // Signature without receiver (e.g., "str(i64,i64)")
    std::string target_id; // Target function id (or canonical name)
};

struct RuntimeClass {
    std::string name;                   // Class name (e.g., "Viper.String")
    std::string type_id;                // Type ID suffix (e.g., "String")
    std::string layout;                 // Layout type (e.g., "opaque*", "obj")
    std::string ctor_id;                // Constructor function id or empty
    std::vector<RuntimeProperty> props; // Properties
    std::vector<RuntimeMethod> methods; // Methods
};

struct CSignature {
    std::string returnType;
    std::vector<std::string> argTypes;
};

struct DescriptorFields {
    std::string signatureId;
    std::string spec;
    std::string handler;
    std::string lowering;
    std::string hidden;
    std::string hiddenCount;
    std::string trapClass;
};

struct RuntimePrototype {
    CSignature signature;
    std::vector<std::string> paramNames;
    std::string headerPath;
};

struct ResolvedRuntimeProperty {
    std::string name;
    std::string type;
    std::string getterCanonical;
    std::string setterCanonical;
};

struct ResolvedRuntimeMethod {
    std::string name;
    std::string signature;
    std::string targetCanonical;
};

struct ResolvedRuntimeClass {
    std::string name;
    std::string type_id;
    std::string layout;
    std::string ctorCanonical;
    std::vector<ResolvedRuntimeProperty> props;
    std::vector<ResolvedRuntimeMethod> methods;
};

struct RuntimeSurfacePolicy {
    std::unordered_set<std::string> internalHeaders;
    std::unordered_set<std::string> internalSymbols;
    std::unordered_map<std::string, std::string> expectedFunctions;
    std::vector<ResolvedRuntimeMethod> expectedMethods;
    std::vector<ResolvedRuntimeProperty> expectedProperties;
};

//===----------------------------------------------------------------------===//
// Parser State
//===----------------------------------------------------------------------===//

struct ParseState {
    std::vector<RuntimeFunc> functions;
    std::vector<RuntimeAlias> aliases;
    std::vector<RuntimeClass> classes;

    // Maps for validation
    std::map<std::string, size_t> func_by_id;
    std::map<std::string, size_t> func_by_canonical;
    std::set<std::string> all_canonicals;

    // Current class being parsed
    std::optional<RuntimeClass> current_class;
    int line_num = 0;
    std::string filename;

    void error(const std::string &msg) const {
        std::cerr << filename << ":" << line_num << ": error: " << msg << "\n";
        std::exit(1);
    }

    void warning(const std::string &msg) const {
        std::cerr << filename << ":" << line_num << ": warning: " << msg << "\n";
    }
};

//===----------------------------------------------------------------------===//
// String Utilities
//===----------------------------------------------------------------------===//

static std::string trim(std::string_view sv) {
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!sv.empty() && is_space(sv.front()))
        sv.remove_prefix(1);
    while (!sv.empty() && is_space(sv.back()))
        sv.remove_suffix(1);
    return std::string(sv);
}

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
            else if (!in_quotes && sv[i] == ')')
                paren_depth--;
        }

        if (i == sv.size() || (!in_quotes && paren_depth == 0 && sv[i] == delim)) {
            if (i > start)
                result.push_back(trim(sv.substr(start, i - start)));
            start = i + 1;
        }
    }
    return result;
}

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

static bool startsWith(std::string_view sv, std::string_view prefix) {
    return sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix;
}

static std::string_view stripPrefix(std::string_view sv, std::string_view prefix) {
    if (startsWith(sv, prefix))
        return sv.substr(prefix.size());
    return sv;
}

// Trim a string_view in place
static std::string_view trimView(std::string_view sv) {
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!sv.empty() && is_space(sv.front()))
        sv.remove_prefix(1);
    while (!sv.empty() && is_space(sv.back()))
        sv.remove_suffix(1);
    return sv;
}

static std::string stripQuotes(std::string_view sv) {
    std::string s = trim(sv);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

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

// Extract content between parentheses: "FOO(a, b, c)" -> "a, b, c"
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

static void parseRtFunc(ParseState &state, const std::string &args) {
    // RT_FUNC(id, c_symbol, canonical, signature [, lowering])
    auto parts = split(args, ',');
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

static void parseRtClassEnd(ParseState &state) {
    if (!state.current_class.has_value())
        state.error("RT_CLASS_END without matching RT_CLASS_BEGIN");

    state.classes.push_back(std::move(*state.current_class));
    state.current_class.reset();
}

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

static ParseState parseFile(const fs::path &path) {
    ParseState state;
    state.filename = path.string();

    std::ifstream in(path);
    if (!in) {
        std::cerr << "error: cannot open " << path << "\n";
        std::exit(1);
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
        baseType = baseType.substr(0, langle);

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
    // Default to void* for unknown types
    return "void *";
}

static std::string stripTypeArgs(const std::string &ilType) {
    size_t langle = ilType.find('<');
    if (langle == std::string::npos)
        return ilType;
    return ilType.substr(0, langle);
}

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
    if (baseType == "obj" || baseType == "ptr")
        return "ptr";
    if (baseType == "bool")
        return "i1";
    // Most types map directly
    return baseType;
}

/// @brief Parse a signature like "str(i64,str)" into return type and arg types.
struct ParsedSignature {
    std::string returnType;
    std::vector<std::string> argTypes;
};

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

    // Split by comma
    size_t start = 0;
    for (size_t i = 0; i <= argsStr.size(); ++i) {
        if (i == argsStr.size() || argsStr[i] == ',') {
            std::string arg = argsStr.substr(start, i - start);
            // Trim whitespace
            while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
                arg.erase(0, 1);
            while (!arg.empty() && (arg.back() == ' ' || arg.back() == '\t'))
                arg.pop_back();
            if (!arg.empty()) {
                result.argTypes.push_back(arg);
            }
            start = i + 1;
        }
    }

    return result;
}

//===----------------------------------------------------------------------===//
// Runtime signature helpers
//===----------------------------------------------------------------------===//

static std::vector<std::string> parseRtSigNames(const fs::path &path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "error: cannot read " << path << "\n";
        std::exit(1);
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

static std::vector<std::string> parseRtSigSymbols(const fs::path &path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "error: cannot read " << path << "\n";
        std::exit(1);
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

static std::unordered_map<std::string, std::string> buildRtSigMap(const fs::path &runtimeDir) {
    const fs::path sigsPath = runtimeDir / "RuntimeSigs.def";
    const fs::path dataPath = runtimeDir / "RuntimeSignaturesData.hpp";
    std::vector<std::string> sigNames = parseRtSigNames(sigsPath);
    std::vector<std::string> sigSymbols = parseRtSigSymbols(dataPath);

    if (sigNames.size() != sigSymbols.size()) {
        std::cerr << "error: RuntimeSigs.def and RuntimeSignaturesData.hpp mismatch\n";
        std::exit(1);
    }

    std::unordered_map<std::string, std::string> result;
    for (size_t i = 0; i < sigNames.size(); ++i) {
        result[sigSymbols[i]] = "RtSig::" + sigNames[i];
    }
    return result;
}

static std::string buildSigSpecExpr(const std::string &sigId) {
    return "data::kRtSigSpecs[static_cast<std::size_t>(" + sigId + ")]";
}

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

static bool lineContinuesPreprocessorDirective(std::string_view line) {
    size_t end = line.find_last_not_of(" \t\r");
    return end != std::string_view::npos && line[end] == '\\';
}

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

static std::string readTextFile(const fs::path &path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "error: cannot read " << path << "\n";
        std::exit(1);
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::string pathToGenericString(const fs::path &path) {
    return path.lexically_normal().generic_string();
}

static std::string relativePathString(const fs::path &path, const fs::path &base) {
    fs::path rel = path.lexically_relative(base);
    if (rel.empty())
        return pathToGenericString(path);
    return pathToGenericString(rel);
}

static std::unordered_map<std::string, RuntimePrototype> loadRuntimeHeaderDeclarations(
    const fs::path &runtimeDir, const fs::path &repoRoot) {
    std::unordered_map<std::string, RuntimePrototype> result;
    if (!fs::exists(runtimeDir))
        return result;

    // Accept both `void *rt_name(...)` and `void * rt_name(...)` styles.
    std::regex proto(R"(([\w\s\*]+?)\s*(rt_[A-Za-z0-9_]+)\s*\(([^;{}]*)\)\s*;)");

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

        for (std::sregex_iterator it(contents.begin(), contents.end(), proto), end; it != end;
             ++it) {
            std::string retType = trim((*it)[1].str());
            std::string funcName = (*it)[2].str();
            std::string argsStr = (*it)[3].str();

            if (result.find(funcName) != result.end())
                continue;

            // Strip storage-class specifiers and attributes from the return
            // type.  The regex can capture these when scanning .c files that
            // contain forward declarations like "extern void rt_trap(...);"
            // or "_Noreturn void rt_trap(...);" etc.
            for (const char *kw : {"extern ", "_Noreturn ", "static ", "inline "}) {
                std::string kwStr(kw);
                if (retType.substr(0, kwStr.size()) == kwStr)
                    retType = trim(retType.substr(kwStr.size()));
            }

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

            proto.signature = std::move(sig);
            proto.headerPath = relativePathString(path, repoRoot);
            result.emplace(funcName, std::move(proto));
        }
    }

    return result;
}

static std::unordered_map<std::string, CSignature> loadRuntimeCSignatures(
    const fs::path &runtimeDir, const fs::path &repoRoot) {
    std::unordered_map<std::string, CSignature> result;
    auto decls = loadRuntimeHeaderDeclarations(runtimeDir, repoRoot);
    for (auto &[symbol, proto] : decls) {
        result.emplace(symbol, std::move(proto.signature));
    }
    return result;
}

static std::unordered_set<std::string> loadRuntimeSourceTokens(const fs::path &runtimeDir) {
    std::unordered_set<std::string> tokens;
    if (!fs::exists(runtimeDir))
        return tokens;

    const std::regex re(R"(\brt_[A-Za-z0-9_]+\b)");
    for (const auto &entry : fs::recursive_directory_iterator(runtimeDir)) {
        if (!entry.is_regular_file())
            continue;
        const fs::path path = entry.path();
        if (path.extension() != ".c" && path.extension() != ".cpp")
            continue;

        const std::string text = readTextFile(path);
        for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
            tokens.insert((*it)[0].str());
    }
    return tokens;
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

struct RuntimeEntry {
    std::string name;
    std::string c_symbol;
    std::string signature;
    std::string lowering; // "always" or "" (default: manual)
};

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

static const RuntimeFunc *resolveRuntimeFunc(const ParseState &state,
                                             const std::string &idOrCanonical) {
    if (auto it = state.func_by_id.find(idOrCanonical); it != state.func_by_id.end())
        return &state.functions[it->second];
    if (auto it = state.func_by_canonical.find(idOrCanonical); it != state.func_by_canonical.end())
        return &state.functions[it->second];
    return nullptr;
}

static const RuntimeAlias *resolveRuntimeAlias(const ParseState &state,
                                               const std::string &canonical) {
    for (const auto &alias : state.aliases) {
        if (alias.canonical == canonical)
            return &alias;
    }
    return nullptr;
}

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

static std::string lastSegment(std::string_view dotted) {
    size_t pos = dotted.rfind('.');
    if (pos == std::string_view::npos)
        return std::string(dotted);
    return std::string(dotted.substr(pos + 1));
}

static std::string methodSlotKey(std::string_view name, std::string_view signature) {
    std::string key;
    key.reserve(name.size() + signature.size() + 1);
    for (unsigned char c : name)
        key.push_back(static_cast<char>(std::tolower(c)));
    key.push_back('|');
    key.append(signature);
    return key;
}

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
            std::cerr << "error: unterminated " << macroName
                      << " macro in runtime surface policy\n";
            std::exit(1);
        }

        handler(std::string_view(text).substr(argsStart, cursor - argsStart - 1));
        pos = cursor;
    }
}

static RuntimeSurfacePolicy parseRuntimeSurfacePolicy(const fs::path &policyPath) {
    RuntimeSurfacePolicy policy;
    if (!fs::exists(policyPath))
        return policy;

    std::string text = stripComments(readTextFile(policyPath));

    scanMacroCalls(text, "RUNTIME_SURFACE_INTERNAL_HEADER", [&](std::string_view argsView) {
        auto parts = split(argsView, ',');
        if (parts.size() != 1) {
            std::cerr << "error: RUNTIME_SURFACE_INTERNAL_HEADER requires 1 argument\n";
            std::exit(1);
        }
        policy.internalHeaders.insert(pathToGenericString(stripQuotes(parts[0])));
    });

    scanMacroCalls(text, "RUNTIME_SURFACE_INTERNAL_SYMBOL", [&](std::string_view argsView) {
        auto parts = split(argsView, ',');
        if (parts.size() != 1) {
            std::cerr << "error: RUNTIME_SURFACE_INTERNAL_SYMBOL requires 1 argument\n";
            std::exit(1);
        }
        policy.internalSymbols.insert(stripQuotes(parts[0]));
    });

    scanMacroCalls(text, "RUNTIME_SURFACE_EXPECT_FUNCTION", [&](std::string_view argsView) {
        auto parts = split(argsView, ',');
        if (parts.size() != 2) {
            std::cerr << "error: RUNTIME_SURFACE_EXPECT_FUNCTION requires 2 arguments\n";
            std::exit(1);
        }
        policy.expectedFunctions.emplace(stripQuotes(parts[0]), stripQuotes(parts[1]));
    });

    scanMacroCalls(text, "RUNTIME_SURFACE_EXPECT_METHOD", [&](std::string_view argsView) {
        auto parts = split(argsView, ',');
        if (parts.size() != 3) {
            std::cerr << "error: RUNTIME_SURFACE_EXPECT_METHOD requires 3 arguments\n";
            std::exit(1);
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
            std::cerr << "error: RUNTIME_SURFACE_EXPECT_PROPERTY requires 3 arguments\n";
            std::exit(1);
        }
        ResolvedRuntimeProperty prop;
        prop.name = stripQuotes(parts[1]);
        prop.type = stripQuotes(parts[2]);
        prop.getterCanonical = stripQuotes(parts[0]);
        policy.expectedProperties.push_back(std::move(prop));
    });

    return policy;
}

static std::string buildDirectHandlerExpr(const std::string &c_symbol, const CSignature &sig) {
    std::string args = "&" + c_symbol + ", " + sig.returnType;
    for (const auto &arg : sig.argTypes) {
        args += ", " + arg;
    }
    return "&DirectHandler<" + args + ">::invoke";
}

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
        fields.spec = "\"" + sigStr + "\"";
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

static void emitDescriptorRow(std::ostream &out,
                              const std::string &name,
                              const DescriptorFields &fields,
                              int indent = 4) {
    std::string pad(static_cast<size_t>(indent), ' ');
    out << pad << "DescriptorRow{\"" << name << "\",\n";
    out << pad << "              " << fields.signatureId << ",\n";
    out << pad << "              " << fields.spec << ",\n";
    out << pad << "              " << fields.handler << ",\n";
    out << pad << "              " << fields.lowering << ",\n";
    out << pad << "              " << fields.hidden << ",\n";
    out << pad << "              " << fields.hiddenCount << ",\n";
    out << pad << "              " << fields.trapClass << "},\n";
}

static void generateNameMap(const ParseState &state, const fs::path &outDir) {
    fs::path outPath = outDir / "RuntimeNameMap.inc";
    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "error: cannot write " << outPath << "\n";
        std::exit(1);
    }

    out << fileHeader("RuntimeNameMap.inc",
                      "Canonical Viper.* to C rt_* symbol mapping for native codegen.");

    // Emit primary mappings
    for (const auto &func : state.functions) {
        out << "RUNTIME_NAME_ALIAS(\"" << func.canonical << "\", \"" << func.c_symbol << "\")\n";
    }

    // Emit aliases
    for (const auto &alias : state.aliases) {
        auto it = state.func_by_id.find(alias.target_id);
        if (it != state.func_by_id.end()) {
            const auto &target = state.functions[it->second];
            out << "RUNTIME_NAME_ALIAS(\"" << alias.canonical << "\", \"" << target.c_symbol
                << "\")\n";
        }
    }

    std::cout << "  Generated " << outPath << "\n";
}

static void generateClasses(const ParseState &state, const fs::path &outDir) {
    fs::path outPath = outDir / "RuntimeClasses.inc";
    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "error: cannot write " << outPath << "\n";
        std::exit(1);
    }

    out << fileHeader("RuntimeClasses.inc", "Runtime class catalog with properties and methods.");

    for (const auto &cls : buildResolvedClasses(state)) {
        out << "RUNTIME_CLASS(\n";
        out << "    \"" << cls.name << "\",\n";
        out << "    RTCLS_" << cls.type_id << ",\n";
        out << "    \"" << cls.layout << "\",\n";

        if (cls.ctorCanonical.empty()) {
            out << "    \"\",\n";
        } else {
            out << "    \"" << cls.ctorCanonical << "\",\n";
        }

        out << "    RUNTIME_PROPS(";
        for (size_t i = 0; i < cls.props.size(); ++i) {
            const auto &prop = cls.props[i];
            if (i > 0)
                out << ",\n                  ";

            out << "RUNTIME_PROP(\"" << prop.name << "\", \"" << prop.type << "\", \""
                << prop.getterCanonical << "\", ";

            if (prop.setterCanonical == "none" || prop.setterCanonical.empty()) {
                out << "nullptr";
            } else {
                out << "\"" << prop.setterCanonical << "\"";
            }
            out << ")";
        }
        out << "),\n";

        out << "    RUNTIME_METHODS(";
        for (size_t i = 0; i < cls.methods.size(); ++i) {
            const auto &method = cls.methods[i];
            if (i > 0)
                out << ",\n                    ";
            out << "RUNTIME_METHOD(\"" << method.name << "\", \"" << method.signature << "\", \""
                << method.targetCanonical << "\")";
        }
        out << "))\n\n";
    }

    std::cout << "  Generated " << outPath << "\n";
}

static void generateSignatures(const ParseState &state,
                               const fs::path &outDir,
                               const fs::path &inputPath) {
    fs::path outPath = outDir / "RuntimeSignatures.inc";
    const fs::path runtimeDir = inputPath.parent_path();

    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "error: cannot write " << outPath << "\n";
        std::exit(1);
    }

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
    if (baseType == "i64")
        return "types::integer()";
    if (baseType == "f64")
        return "types::number()";
    if (baseType == "i1" || baseType == "bool")
        return "types::boolean()";
    if (baseType == "void")
        return "types::voidType()";
    if ((baseType == "obj" || baseType == "ptr") && !typeArg.empty())
        return "types::runtimeClass(\"" + typeArg + "\")";
    if (baseType == "obj" || baseType == "ptr") {
        // Explicit return type overrides for functions that return objects
        // from a different namespace than their own.
        static const std::unordered_map<std::string, std::string> returnTypeOverrides = {
            // Crypto functions returning Viper.Collections.Bytes
            {"Viper.Crypto.Rand.Bytes", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.Encrypt", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.Decrypt", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.EncryptWithKey", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.DecryptWithKey", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.GenerateKey", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Cipher.DeriveKey", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Aes.Encrypt", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Aes.Decrypt", "Viper.Collections.Bytes"},
            {"Viper.Crypto.Aes.EncryptStr", "Viper.Collections.Bytes"},
            {"Viper.Crypto.KeyDerive.Pbkdf2SHA256", "Viper.Collections.Bytes"},
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
        return "types::ptr()";
    }
    // Default to ptr for unknown types
    return "types::ptr()";
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
    if (baseType == "i64")
        return "types::integer()";
    if (baseType == "f64")
        return "types::number()";
    if (baseType == "i1" || baseType == "bool")
        return "types::boolean()";
    if (baseType == "void")
        return "types::voidType()";
    if (baseType == "obj" || baseType == "ptr")
        return "types::ptr()";
    return "types::ptr()";
}

static void emitZiaParamNames(std::ostream &out, const std::vector<std::string> &paramNames) {
    out << ", {";
    for (size_t i = 0; i < paramNames.size(); ++i) {
        if (i > 0)
            out << ", ";
        out << "\"" << paramNames[i] << "\"";
    }
    out << "}";
}

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

static void generateZiaExterns(const ParseState &state,
                               const fs::path &outDir,
                               const fs::path &inputPath) {
    // Populate known class names for type inference
    knownClassNames.clear();
    for (const auto &cls : state.classes)
        knownClassNames.insert(cls.name);

    fs::path outPath = outDir / "ZiaRuntimeExterns.inc";
    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "error: cannot write " << outPath << "\n";
        std::exit(1);
    }

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
        out << "typeRegistry_[\"" << cls.name << "\"] = types::runtimeClass(\"" << cls.name
            << "\");\n";
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
            out << "defineExternFunction(\"" << func->canonical << "\", " << ziaType;
            if (!sig.argTypes.empty()) {
                out << ", {";
                for (size_t i = 0; i < sig.argTypes.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << ilParamTypeToZiaType(sig.argTypes[i]);
                }
                out << "}";
            }
            if (auto declIt = headerDecls.find(func->c_symbol); declIt != headerDecls.end()) {
                auto paramNames = ziaExternParamNamesFor(*func, &declIt->second);
                if (!paramNames.empty())
                    emitZiaParamNames(out, paramNames);
            }
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
                out << "defineExternFunction(\"" << alias.canonical << "\", " << ziaType;
                if (!sig.argTypes.empty()) {
                    out << ", {";
                    for (size_t i = 0; i < sig.argTypes.size(); ++i) {
                        if (i > 0)
                            out << ", ";
                        out << ilParamTypeToZiaType(sig.argTypes[i]);
                    }
                    out << "}";
                }
                if (auto declIt = headerDecls.find(target.c_symbol); declIt != headerDecls.end()) {
                    auto paramNames = ziaExternParamNamesFor(target, &declIt->second);
                    if (!paramNames.empty())
                        emitZiaParamNames(out, paramNames);
                }
                out << ");\n";
            }
        }
        out << "}();\n";
        out << "\n";
    }

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

static void generateFrontendNames(const ParseState &state, const fs::path &outDir) {
    fs::path outPath = outDir / "RuntimeNames.hpp";
    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "error: cannot write " << outPath << "\n";
        std::exit(1);
    }

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
            out << "inline constexpr const char *" << uniqueId << " = \"" << func->canonical
                << "\";\n\n";
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
            out << "inline constexpr const char *" << uniqueId << " = \"" << alias.canonical
                << "\";\n\n";
        }
    }

    out << "} // namespace il::runtime::names\n";

    std::cout << "  Generated " << outPath << "\n";
}

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

static void printUsage(const char *prog) {
    std::cerr << "Usage: " << prog << " <input.def> <output_dir>\n";
    std::cerr
        << "       " << prog
        << " --audit [--strict-header-sync] [--strict-unclassified] [--summary-only] <input.def>\n";
    std::cerr << "\n";
    std::cerr << "Generates runtime registry .inc files from runtime.def\n";
}

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

    if (!fs::exists(inputPath)) {
        std::cerr << "error: input file not found: " << inputPath << "\n";
        return 1;
    }

    std::cout << "rtgen: Parsing " << inputPath << "\n";
    ParseState state = parseFile(inputPath);

    if (auditMode)
        return runAudit(state, inputPath, strictHeaderSync, strictUnclassified, summaryOnly);

    fs::path outputDir = positional[1];

    // Create output directory if needed
    if (!fs::exists(outputDir)) {
        fs::create_directories(outputDir);
    }

    std::cout << "rtgen: Parsed " << state.functions.size() << " functions, "
              << state.aliases.size() << " aliases, " << state.classes.size() << " classes\n";

    std::cout << "rtgen: Generating output files in " << outputDir << "\n";
    generateNameMap(state, outputDir);
    generateClasses(state, outputDir);
    generateSignatures(state, outputDir, inputPath);
    generateZiaExterns(state, outputDir, inputPath);
    generateFrontendNames(state, outputDir);

    std::cout << "rtgen: Done\n";
    return 0;
}

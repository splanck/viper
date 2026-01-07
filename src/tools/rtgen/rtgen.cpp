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
//
// Key invariants:
//   - Parses runtime.def line by line
//   - Validates no duplicate symbols or missing targets
//   - Pure C++17, no external dependencies
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

//===----------------------------------------------------------------------===//
// Data Structures
//===----------------------------------------------------------------------===//

struct RuntimeFunc
{
    std::string id;        // Unique identifier (e.g., "PrintStr")
    std::string c_symbol;  // C runtime symbol (e.g., "rt_print_str")
    std::string canonical; // Canonical Viper.* name (e.g., "Viper.Console.PrintStr")
    std::string signature; // Type signature (e.g., "void(str)")
};

struct RuntimeAlias
{
    std::string canonical; // Alias canonical name
    std::string target_id; // Target function id
};

struct RuntimeProperty
{
    std::string name;      // Property name (e.g., "Length")
    std::string type;      // Property type (e.g., "i64")
    std::string getter_id; // Getter function id (or canonical name)
    std::string setter_id; // Setter function id or "none"
};

struct RuntimeMethod
{
    std::string name;      // Method name (e.g., "Substring")
    std::string signature; // Signature without receiver (e.g., "str(i64,i64)")
    std::string target_id; // Target function id (or canonical name)
};

struct RuntimeClass
{
    std::string name;                   // Class name (e.g., "Viper.String")
    std::string type_id;                // Type ID suffix (e.g., "String")
    std::string layout;                 // Layout type (e.g., "opaque*", "obj")
    std::string ctor_id;                // Constructor function id or empty
    std::vector<RuntimeProperty> props; // Properties
    std::vector<RuntimeMethod> methods; // Methods
};

struct CSignature
{
    std::string returnType;
    std::vector<std::string> argTypes;
};

struct DescriptorFields
{
    std::string signatureId;
    std::string spec;
    std::string handler;
    std::string lowering;
    std::string hidden;
    std::string hiddenCount;
    std::string trapClass;
};

struct RowOverride
{
    std::optional<DescriptorFields> always;
    std::optional<DescriptorFields> dual_if;
    std::optional<DescriptorFields> dual_else;
};

struct OverrideData
{
    std::vector<std::string> order;
    std::unordered_map<std::string, RowOverride> rows;
};

enum class DualState
{
    None,
    If,
    Else,
};

//===----------------------------------------------------------------------===//
// Parser State
//===----------------------------------------------------------------------===//

struct ParseState
{
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

    void error(const std::string &msg) const
    {
        std::cerr << filename << ":" << line_num << ": error: " << msg << "\n";
        std::exit(1);
    }

    void warning(const std::string &msg) const
    {
        std::cerr << filename << ":" << line_num << ": warning: " << msg << "\n";
    }
};

//===----------------------------------------------------------------------===//
// String Utilities
//===----------------------------------------------------------------------===//

static std::string trim(std::string_view sv)
{
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!sv.empty() && is_space(sv.front()))
        sv.remove_prefix(1);
    while (!sv.empty() && is_space(sv.back()))
        sv.remove_suffix(1);
    return std::string(sv);
}

static std::vector<std::string> split(std::string_view sv, char delim)
{
    std::vector<std::string> result;
    size_t start = 0;
    bool in_quotes = false;
    int paren_depth = 0;

    for (size_t i = 0; i <= sv.size(); ++i)
    {
        if (i < sv.size())
        {
            if (sv[i] == '"' && (i == 0 || sv[i - 1] != '\\'))
                in_quotes = !in_quotes;
            else if (!in_quotes && sv[i] == '(')
                paren_depth++;
            else if (!in_quotes && sv[i] == ')')
                paren_depth--;
        }

        if (i == sv.size() || (!in_quotes && paren_depth == 0 && sv[i] == delim))
        {
            if (i > start)
                result.push_back(trim(sv.substr(start, i - start)));
            start = i + 1;
        }
    }
    return result;
}

static std::vector<std::string> splitTopLevel(std::string_view sv, char delim)
{
    std::vector<std::string> result;
    size_t start = 0;
    bool in_quotes = false;
    int paren_depth = 0;
    int angle_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;

    for (size_t i = 0; i <= sv.size(); ++i)
    {
        if (i < sv.size())
        {
            if (sv[i] == '"' && (i == 0 || sv[i - 1] != '\\'))
                in_quotes = !in_quotes;
            else if (!in_quotes)
            {
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
                               brace_depth == 0 && bracket_depth == 0 && sv[i] == delim))
        {
            if (i > start)
                result.push_back(trim(sv.substr(start, i - start)));
            start = i + 1;
        }
    }
    return result;
}

static bool startsWith(std::string_view sv, std::string_view prefix)
{
    return sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix;
}

static std::string_view stripPrefix(std::string_view sv, std::string_view prefix)
{
    if (startsWith(sv, prefix))
        return sv.substr(prefix.size());
    return sv;
}

// Trim a string_view in place
static std::string_view trimView(std::string_view sv)
{
    auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!sv.empty() && is_space(sv.front()))
        sv.remove_prefix(1);
    while (!sv.empty() && is_space(sv.back()))
        sv.remove_suffix(1);
    return sv;
}

static std::string stripQuotes(std::string_view sv)
{
    std::string s = trim(sv);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

static std::string stripParamName(std::string_view sv)
{
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

// Extract content between parentheses: "FOO(a, b, c)" -> "a, b, c"
static std::optional<std::string> extractParens(std::string_view line, std::string_view macro)
{
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
    for (; i < line.size() && depth > 0; ++i)
    {
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

static void parseRtFunc(ParseState &state, const std::string &args)
{
    // RT_FUNC(id, c_symbol, canonical, signature)
    auto parts = split(args, ',');
    if (parts.size() != 4)
    {
        state.error("RT_FUNC requires 4 arguments: id, c_symbol, canonical, signature");
    }

    RuntimeFunc func;
    func.id = parts[0];
    func.c_symbol = parts[1];
    func.canonical = parts[2];
    func.signature = parts[3];

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

static void parseRtAlias(ParseState &state, const std::string &args)
{
    // RT_ALIAS(canonical, target_id)
    auto parts = split(args, ',');
    if (parts.size() != 2)
    {
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

static void parseRtClassBegin(ParseState &state, const std::string &args)
{
    // RT_CLASS_BEGIN(name, type_id, layout, ctor_id)
    if (state.current_class.has_value())
        state.error("Nested RT_CLASS_BEGIN not allowed");

    auto parts = split(args, ',');
    if (parts.size() != 4)
    {
        state.error("RT_CLASS_BEGIN requires 4 arguments: name, type_id, layout, ctor_id");
    }

    RuntimeClass cls;
    cls.name = parts[0];
    cls.type_id = parts[1];
    cls.layout = parts[2];
    cls.ctor_id = parts[3];

    // Remove quotes from all string fields
    auto stripQuotes = [](std::string &s)
    {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size() - 2);
    };
    stripQuotes(cls.name);
    stripQuotes(cls.layout);
    stripQuotes(cls.ctor_id);

    state.current_class = std::move(cls);
}

static void parseRtProp(ParseState &state, const std::string &args)
{
    // RT_PROP(name, type, getter_id, setter_id_or_none)
    if (!state.current_class.has_value())
        state.error("RT_PROP outside of RT_CLASS_BEGIN/END block");

    auto parts = split(args, ',');
    if (parts.size() != 4)
    {
        state.error("RT_PROP requires 4 arguments: name, type, getter_id, setter_id");
    }

    RuntimeProperty prop;
    prop.name = parts[0];
    prop.type = parts[1];
    prop.getter_id = parts[2];
    prop.setter_id = parts[3];

    // Remove quotes from all string fields
    auto stripQuotes = [](std::string &s)
    {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size() - 2);
    };
    stripQuotes(prop.name);
    stripQuotes(prop.type);
    stripQuotes(prop.getter_id);
    stripQuotes(prop.setter_id);

    state.current_class->props.push_back(std::move(prop));
}

static void parseRtMethod(ParseState &state, const std::string &args)
{
    // RT_METHOD(name, signature, target_id)
    if (!state.current_class.has_value())
        state.error("RT_METHOD outside of RT_CLASS_BEGIN/END block");

    auto parts = split(args, ',');
    if (parts.size() != 3)
    {
        state.error("RT_METHOD requires 3 arguments: name, signature, target_id");
    }

    RuntimeMethod method;
    method.name = parts[0];
    method.signature = parts[1];
    method.target_id = parts[2];

    // Remove quotes from all string fields
    auto stripQuotes = [](std::string &s)
    {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            s = s.substr(1, s.size() - 2);
    };
    stripQuotes(method.name);
    stripQuotes(method.signature);
    stripQuotes(method.target_id);

    state.current_class->methods.push_back(std::move(method));
}

static void parseRtClassEnd(ParseState &state)
{
    if (!state.current_class.has_value())
        state.error("RT_CLASS_END without matching RT_CLASS_BEGIN");

    state.classes.push_back(std::move(*state.current_class));
    state.current_class.reset();
}

static void parseLine(ParseState &state, const std::string &line)
{
    std::string trimmed = trim(line);

    // Skip empty lines and comments
    if (trimmed.empty() || startsWith(trimmed, "//") || startsWith(trimmed, "#"))
        return;

    // Parse macros
    if (auto args = extractParens(trimmed, "RT_FUNC"))
    {
        parseRtFunc(state, *args);
    }
    else if (auto args = extractParens(trimmed, "RT_ALIAS"))
    {
        parseRtAlias(state, *args);
    }
    else if (auto args = extractParens(trimmed, "RT_CLASS_BEGIN"))
    {
        parseRtClassBegin(state, *args);
    }
    else if (auto args = extractParens(trimmed, "RT_PROP"))
    {
        parseRtProp(state, *args);
    }
    else if (auto args = extractParens(trimmed, "RT_METHOD"))
    {
        parseRtMethod(state, *args);
    }
    else if (trimmed == "RT_CLASS_END()")
    {
        parseRtClassEnd(state);
    }
    else
    {
        state.error("Unknown directive: " + trimmed);
    }
}

static ParseState parseFile(const fs::path &path)
{
    ParseState state;
    state.filename = path.string();

    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "error: cannot open " << path << "\n";
        std::exit(1);
    }

    std::string line;
    while (std::getline(in, line))
    {
        state.line_num++;
        parseLine(state, line);
    }

    if (state.current_class.has_value())
    {
        state.error("Unclosed RT_CLASS_BEGIN (missing RT_CLASS_END)");
    }

    return state;
}

//===----------------------------------------------------------------------===//
// Type Mapping (IL signature types to C types)
//===----------------------------------------------------------------------===//

/// @brief Map IL type to C type for DirectHandler template.
static std::string ilTypeToCType(const std::string &ilType)
{
    if (ilType == "str")
        return "rt_string";
    if (ilType == "i64")
        return "int64_t";
    if (ilType == "i32")
        return "int32_t";
    if (ilType == "i16")
        return "int16_t";
    if (ilType == "i8" || ilType == "i1")
        return "int8_t";
    if (ilType == "f64")
        return "double";
    if (ilType == "f32")
        return "float";
    if (ilType == "void")
        return "void";
    if (ilType == "obj" || ilType == "ptr")
        return "void *";
    // Default to void* for unknown types
    return "void *";
}

/// @brief Map IL type to signature string format.
static std::string ilTypeToSigType(const std::string &ilType)
{
    if (ilType == "str")
        return "string";
    if (ilType == "obj")
        return "ptr";
    // Most types map directly
    return ilType;
}

/// @brief Parse a signature like "str(i64,str)" into return type and arg types.
struct ParsedSignature
{
    std::string returnType;
    std::vector<std::string> argTypes;
};

static ParsedSignature parseSignature(const std::string &sig)
{
    ParsedSignature result;

    // Find the opening paren
    size_t parenPos = sig.find('(');
    if (parenPos == std::string::npos)
    {
        result.returnType = sig;
        return result;
    }

    result.returnType = sig.substr(0, parenPos);

    // Extract args between parens
    size_t closePos = sig.rfind(')');
    if (closePos == std::string::npos || closePos <= parenPos + 1)
    {
        return result; // No args
    }

    std::string argsStr = sig.substr(parenPos + 1, closePos - parenPos - 1);
    if (argsStr.empty())
    {
        return result; // Empty args "()"
    }

    // Split by comma
    size_t start = 0;
    for (size_t i = 0; i <= argsStr.size(); ++i)
    {
        if (i == argsStr.size() || argsStr[i] == ',')
        {
            std::string arg = argsStr.substr(start, i - start);
            // Trim whitespace
            while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
                arg.erase(0, 1);
            while (!arg.empty() && (arg.back() == ' ' || arg.back() == '\t'))
                arg.pop_back();
            if (!arg.empty())
            {
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

static std::vector<std::string> parseRtSigNames(const fs::path &path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "error: cannot read " << path << "\n";
        std::exit(1);
    }

    std::vector<std::string> names;
    std::string line;
    while (std::getline(in, line))
    {
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

static std::vector<std::string> parseRtSigSymbols(const fs::path &path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "error: cannot read " << path << "\n";
        std::exit(1);
    }

    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string marker = "kRtSigSymbolNames";
    size_t start = contents.find(marker);
    if (start == std::string::npos)
    {
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
    for (size_t i = 0; i < block.size(); ++i)
    {
        char c = block[i];
        if (c == '"' && (i == 0 || block[i - 1] != '\\'))
        {
            if (in_quotes)
            {
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

static std::unordered_map<std::string, std::string> buildRtSigMap(const fs::path &runtimeDir)
{
    const fs::path sigsPath = runtimeDir / "RuntimeSigs.def";
    const fs::path dataPath = runtimeDir / "RuntimeSignaturesData.hpp";
    std::vector<std::string> sigNames = parseRtSigNames(sigsPath);
    std::vector<std::string> sigSymbols = parseRtSigSymbols(dataPath);

    if (sigNames.size() != sigSymbols.size())
    {
        std::cerr << "error: RuntimeSigs.def and RuntimeSignaturesData.hpp mismatch\n";
        std::exit(1);
    }

    std::unordered_map<std::string, std::string> result;
    for (size_t i = 0; i < sigNames.size(); ++i)
    {
        result[sigSymbols[i]] = "RtSig::" + sigNames[i];
    }
    return result;
}

static std::string buildSigSpecExpr(const std::string &sigId)
{
    return "data::kRtSigSpecs[static_cast<std::size_t>(" + sigId + ")]";
}

static std::string stripComments(const std::string &input)
{
    std::string out;
    out.reserve(input.size());
    bool in_line = false;
    bool in_block = false;

    for (size_t i = 0; i < input.size(); ++i)
    {
        char c = input[i];
        char next = (i + 1 < input.size()) ? input[i + 1] : '\0';

        if (in_line)
        {
            if (c == '\n')
            {
                in_line = false;
                out.push_back(c);
            }
            continue;
        }

        if (in_block)
        {
            if (c == '*' && next == '/')
            {
                in_block = false;
                ++i;
            }
            continue;
        }

        if (c == '/' && next == '/')
        {
            in_line = true;
            ++i;
            continue;
        }
        if (c == '/' && next == '*')
        {
            in_block = true;
            ++i;
            continue;
        }

        out.push_back(c);
    }
    return out;
}

static std::string stripPreprocessor(const std::string &input)
{
    std::ostringstream out;
    std::istringstream in(input);
    std::string line;
    while (std::getline(in, line))
    {
        std::string_view trimmed = trimView(line);
        if (!trimmed.empty() && trimmed.front() == '#')
            continue;
        out << line << '\n';
    }
    return out.str();
}

static std::unordered_map<std::string, CSignature> loadRuntimeCSignatures(
    const fs::path &runtimeDir)
{
    std::unordered_map<std::string, CSignature> result;
    if (!fs::exists(runtimeDir))
        return result;

    std::regex proto(R"(([\w\s\*]+?)\s+(rt_[A-Za-z0-9_]+)\s*\(([^;{}]*)\)\s*;)");

    for (const auto &entry : fs::recursive_directory_iterator(runtimeDir))
    {
        if (!entry.is_regular_file())
            continue;
        const fs::path path = entry.path();
        if (path.extension() != ".h")
            continue;

        std::ifstream in(path);
        if (!in)
            continue;

        std::string contents((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
        contents = stripComments(contents);
        contents = stripPreprocessor(contents);

        for (std::sregex_iterator it(contents.begin(), contents.end(), proto), end; it != end; ++it)
        {
            std::string retType = trim((*it)[1].str());
            std::string funcName = (*it)[2].str();
            std::string argsStr = (*it)[3].str();

            if (result.find(funcName) != result.end())
                continue;

            CSignature sig;
            sig.returnType = retType;
            std::vector<std::string> args = splitTopLevel(argsStr, ',');
            for (const auto &arg : args)
            {
                std::string type = stripParamName(arg);
                if (type.empty() || type == "void")
                    continue;
                sig.argTypes.push_back(type);
            }

            result.emplace(funcName, std::move(sig));
        }
    }

    return result;
}

static std::optional<std::pair<std::string, DescriptorFields>> parseDescriptorRowBlock(
    std::string_view block)
{
    size_t open = block.find('{');
    size_t close = block.rfind('}');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
        return std::nullopt;

    std::string_view inner = block.substr(open + 1, close - open - 1);
    std::vector<std::string> fields = splitTopLevel(inner, ',');
    if (fields.size() < 8)
        return std::nullopt;

    DescriptorFields row;
    std::string name = stripQuotes(fields[0]);
    row.signatureId = trim(fields[1]);
    row.spec = trim(fields[2]);
    row.handler = trim(fields[3]);
    row.lowering = trim(fields[4]);
    row.hidden = trim(fields[5]);
    row.hiddenCount = trim(fields[6]);
    row.trapClass = trim(fields[7]);

    return std::make_pair(name, row);
}

static OverrideData loadSignatureOverrides(const fs::path &path)
{
    OverrideData data;
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "warning: signature overrides not found: " << path << "\n";
        return data;
    }

    DualState state = DualState::None;
    std::size_t rows_seen = 0;
    std::size_t rows_parsed = 0;
    std::string line;
    while (std::getline(in, line))
    {
        std::string_view view = trimView(line);
        if (startsWith(view, "#if") && view.find("VIPER_RUNTIME_NS_DUAL") != std::string_view::npos)
        {
            state = DualState::If;
            continue;
        }
        if (startsWith(view, "#else") && state != DualState::None)
        {
            state = DualState::Else;
            continue;
        }
        if (startsWith(view, "#endif"))
        {
            state = DualState::None;
            continue;
        }

        if (line.find("DescriptorRow") == std::string::npos)
            continue;
        ++rows_seen;

        std::string block = line;
        int brace_depth = 0;
        for (char c : line)
        {
            if (c == '{')
                brace_depth++;
            else if (c == '}')
                brace_depth--;
        }

        while (brace_depth > 0 && std::getline(in, line))
        {
            block.append("\n");
            block.append(line);
            for (char c : line)
            {
                if (c == '{')
                    brace_depth++;
                else if (c == '}')
                    brace_depth--;
            }
        }

        auto parsed = parseDescriptorRowBlock(block);
        if (!parsed)
            continue;
        ++rows_parsed;

        const std::string &name = parsed->first;
        const DescriptorFields &fields = parsed->second;

        if (std::find(data.order.begin(), data.order.end(), name) == data.order.end())
            data.order.push_back(name);

        RowOverride &row = data.rows[name];
        if (state == DualState::If)
            row.dual_if = fields;
        else if (state == DualState::Else)
            row.dual_else = fields;
        else
            row.always = fields;
    }

    if (rows_seen > 0 && rows_parsed == 0)
    {
        std::cerr << "warning: failed to parse any DescriptorRow blocks from " << path << "\n";
    }

    return data;
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

struct RuntimeEntry
{
    std::string name;
    std::string c_symbol;
    std::string signature;
};

static std::string fileHeader(const std::string &filename, const std::string &purpose)
{
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

static std::string buildDirectHandlerExpr(const std::string &c_symbol, const CSignature &sig)
{
    std::string args = "&" + c_symbol + ", " + sig.returnType;
    for (const auto &arg : sig.argTypes)
    {
        args += ", " + arg;
    }
    return "&DirectHandler<" + args + ">::invoke";
}

static DescriptorFields buildDefaultDescriptor(
    const RuntimeEntry &entry,
    const std::unordered_map<std::string, CSignature> &cSignatures,
    const std::unordered_map<std::string, std::string> &rtSigMap)
{
    DescriptorFields fields;

    auto sigIt = rtSigMap.find(entry.c_symbol);
    if (sigIt != rtSigMap.end())
    {
        fields.signatureId = sigIt->second;
        fields.spec = buildSigSpecExpr(fields.signatureId);
    }
    else
    {
        fields.signatureId = "std::nullopt";
        ParsedSignature parsed = parseSignature(entry.signature);
        std::string sigStr = ilTypeToSigType(parsed.returnType) + "(";
        for (size_t i = 0; i < parsed.argTypes.size(); ++i)
        {
            if (i > 0)
                sigStr += ", ";
            sigStr += ilTypeToSigType(parsed.argTypes[i]);
        }
        sigStr += ")";
        fields.spec = "\"" + sigStr + "\"";
    }

    auto cSigIt = cSignatures.find(entry.c_symbol);
    if (cSigIt != cSignatures.end())
    {
        fields.handler = buildDirectHandlerExpr(entry.c_symbol, cSigIt->second);
    }
    else
    {
        ParsedSignature parsed = parseSignature(entry.signature);
        CSignature fallback;
        fallback.returnType = ilTypeToCType(parsed.returnType);
        for (const auto &arg : parsed.argTypes)
            fallback.argTypes.push_back(ilTypeToCType(arg));
        fields.handler = buildDirectHandlerExpr(entry.c_symbol, fallback);
    }

    fields.lowering = "kManualLowering";
    fields.hidden = "nullptr";
    fields.hiddenCount = "0";
    fields.trapClass = "RuntimeTrapClass::None";
    return fields;
}

static void emitDescriptorRow(std::ostream &out,
                              const std::string &name,
                              const DescriptorFields &fields,
                              int indent = 4)
{
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

static bool fieldsEqual(const DescriptorFields &a, const DescriptorFields &b)
{
    return a.signatureId == b.signatureId && a.spec == b.spec && a.handler == b.handler &&
           a.lowering == b.lowering && a.hidden == b.hidden && a.hiddenCount == b.hiddenCount &&
           a.trapClass == b.trapClass;
}

static void generateNameMap(const ParseState &state, const fs::path &outDir)
{
    fs::path outPath = outDir / "RuntimeNameMap.inc";
    std::ofstream out(outPath);
    if (!out)
    {
        std::cerr << "error: cannot write " << outPath << "\n";
        std::exit(1);
    }

    out << fileHeader("RuntimeNameMap.inc",
                      "Canonical Viper.* to C rt_* symbol mapping for native codegen.");

    // Emit primary mappings
    for (const auto &func : state.functions)
    {
        out << "RUNTIME_NAME_ALIAS(\"" << func.canonical << "\", \"" << func.c_symbol << "\")\n";
    }

    // Emit aliases
    for (const auto &alias : state.aliases)
    {
        auto it = state.func_by_id.find(alias.target_id);
        if (it != state.func_by_id.end())
        {
            const auto &target = state.functions[it->second];
            out << "RUNTIME_NAME_ALIAS(\"" << alias.canonical << "\", \"" << target.c_symbol
                << "\")\n";
        }
    }

    std::cout << "  Generated " << outPath << "\n";
}

static void generateClasses(const ParseState &state, const fs::path &outDir)
{
    fs::path outPath = outDir / "RuntimeClasses.inc";
    std::ofstream out(outPath);
    if (!out)
    {
        std::cerr << "error: cannot write " << outPath << "\n";
        std::exit(1);
    }

    out << fileHeader("RuntimeClasses.inc", "Runtime class catalog with properties and methods.");

    for (const auto &cls : state.classes)
    {
        out << "RUNTIME_CLASS(\n";
        out << "    \"" << cls.name << "\",\n";
        out << "    RTCLS_" << cls.type_id << ",\n";
        out << "    \"" << cls.layout << "\",\n";

        // Constructor - resolve to canonical name
        if (cls.ctor_id.empty() || cls.ctor_id == "none")
        {
            out << "    \"\",\n";
        }
        else
        {
            // Look up canonical name from id
            auto it = state.func_by_id.find(cls.ctor_id);
            if (it != state.func_by_id.end())
            {
                out << "    \"" << state.functions[it->second].canonical << "\",\n";
            }
            else
            {
                // Assume it's already a canonical name
                out << "    \"" << cls.ctor_id << "\",\n";
            }
        }

        // Properties
        out << "    RUNTIME_PROPS(";
        for (size_t i = 0; i < cls.props.size(); ++i)
        {
            const auto &prop = cls.props[i];
            if (i > 0)
                out << ",\n                  ";

            // Resolve getter canonical name
            std::string getter_canonical = prop.getter_id;
            auto git = state.func_by_id.find(prop.getter_id);
            if (git != state.func_by_id.end())
            {
                getter_canonical = state.functions[git->second].canonical;
            }

            out << "RUNTIME_PROP(\"" << prop.name << "\", \"" << prop.type << "\", \""
                << getter_canonical << "\", ";

            if (prop.setter_id == "none" || prop.setter_id.empty())
            {
                out << "nullptr";
            }
            else
            {
                std::string setter_canonical = prop.setter_id;
                auto sit = state.func_by_id.find(prop.setter_id);
                if (sit != state.func_by_id.end())
                {
                    setter_canonical = state.functions[sit->second].canonical;
                }
                out << "\"" << setter_canonical << "\"";
            }
            out << ")";
        }
        out << "),\n";

        // Methods
        out << "    RUNTIME_METHODS(";
        for (size_t i = 0; i < cls.methods.size(); ++i)
        {
            const auto &method = cls.methods[i];
            if (i > 0)
                out << ",\n                    ";

            // Resolve target canonical name
            std::string target_canonical = method.target_id;
            auto mit = state.func_by_id.find(method.target_id);
            if (mit != state.func_by_id.end())
            {
                target_canonical = state.functions[mit->second].canonical;
            }

            out << "RUNTIME_METHOD(\"" << method.name << "\", \"" << method.signature << "\", \""
                << target_canonical << "\")";
        }
        out << "))\n\n";
    }

    std::cout << "  Generated " << outPath << "\n";
}

static void generateSignatures(const ParseState &state,
                               const fs::path &outDir,
                               const fs::path &inputPath)
{
    fs::path outPath = outDir / "RuntimeSignatures.inc";
    const fs::path runtimeDir = inputPath.parent_path();
    const fs::path overridesPath = runtimeDir / "generated" / "RuntimeSignatures.inc";
    OverrideData overrides = loadSignatureOverrides(overridesPath);
    std::cout << "rtgen: Loaded " << overrides.rows.size() << " signature overrides\n";

    std::ofstream out(outPath);
    if (!out)
    {
        std::cerr << "error: cannot write " << outPath << "\n";
        std::exit(1);
    }

    out << fileHeader("RuntimeSignatures.inc",
                      "Runtime descriptor rows for all runtime functions.");

    const fs::path srcRoot = runtimeDir.parent_path().parent_path();
    const fs::path runtimeHeaders = srcRoot / "runtime";
    auto cSignatures = loadRuntimeCSignatures(runtimeHeaders);
    auto rtSigMap = buildRtSigMap(runtimeDir);

    std::unordered_map<std::string, RuntimeEntry> entries;
    entries.reserve(state.functions.size() + state.aliases.size());

    for (const auto &func : state.functions)
    {
        entries.emplace(func.canonical,
                        RuntimeEntry{func.canonical, func.c_symbol, func.signature});
    }
    for (const auto &alias : state.aliases)
    {
        auto it = state.func_by_id.find(alias.target_id);
        if (it == state.func_by_id.end())
            continue;
        const auto &target = state.functions[it->second];
        entries.emplace(alias.canonical,
                        RuntimeEntry{alias.canonical, target.c_symbol, target.signature});
    }

    std::set<std::string> seen;
    std::vector<std::string> orderedNames;
    orderedNames.reserve(entries.size());

    for (const auto &name : overrides.order)
    {
        if (seen.insert(name).second)
            orderedNames.push_back(name);
    }

    for (const auto &func : state.functions)
    {
        if (seen.insert(func.canonical).second)
            orderedNames.push_back(func.canonical);
    }

    for (const auto &alias : state.aliases)
    {
        if (seen.insert(alias.canonical).second)
            orderedNames.push_back(alias.canonical);
    }

    for (const auto &name : orderedNames)
    {
        auto overrideIt = overrides.rows.find(name);
        if (overrideIt == overrides.rows.end())
        {
            auto entryIt = entries.find(name);
            if (entryIt == entries.end())
                continue;

            const RuntimeEntry &entry = entryIt->second;
            DescriptorFields defaultFields = buildDefaultDescriptor(entry, cSignatures, rtSigMap);
            emitDescriptorRow(out, name, defaultFields);
            continue;
        }

        const RowOverride &row = overrideIt->second;
        if (row.always)
        {
            emitDescriptorRow(out, name, *row.always);
            continue;
        }

        const bool hasIf = row.dual_if.has_value();
        const bool hasElse = row.dual_else.has_value();

        if (hasIf && hasElse)
        {
            if (fieldsEqual(*row.dual_if, *row.dual_else))
            {
                emitDescriptorRow(out, name, *row.dual_if);
            }
            else
            {
                out << "#if VIPER_RUNTIME_NS_DUAL\n";
                emitDescriptorRow(out, name, *row.dual_if);
                out << "#else\n";
                emitDescriptorRow(out, name, *row.dual_else);
                out << "#endif\n";
            }
        }
        else if (hasIf)
        {
            out << "#if VIPER_RUNTIME_NS_DUAL\n";
            emitDescriptorRow(out, name, *row.dual_if);
            out << "#endif\n";
        }
        else if (hasElse)
        {
            out << "#if !VIPER_RUNTIME_NS_DUAL\n";
            emitDescriptorRow(out, name, *row.dual_else);
            out << "#endif\n";
        }
        else
        {
            auto entryIt = entries.find(name);
            if (entryIt == entries.end())
                continue;
            DescriptorFields defaultFields =
                buildDefaultDescriptor(entryIt->second, cSignatures, rtSigMap);
            emitDescriptorRow(out, name, defaultFields);
        }
    }

    std::cout << "  Generated " << outPath << "\n";
}

//===----------------------------------------------------------------------===//
// Main
//===----------------------------------------------------------------------===//

static void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <input.def> <output_dir>\n";
    std::cerr << "\n";
    std::cerr << "Generates runtime registry .inc files from runtime.def\n";
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printUsage(argv[0]);
        return 1;
    }

    fs::path inputPath = argv[1];
    fs::path outputDir = argv[2];

    if (!fs::exists(inputPath))
    {
        std::cerr << "error: input file not found: " << inputPath << "\n";
        return 1;
    }

    // Create output directory if needed
    if (!fs::exists(outputDir))
    {
        fs::create_directories(outputDir);
    }

    std::cout << "rtgen: Parsing " << inputPath << "\n";
    ParseState state = parseFile(inputPath);

    std::cout << "rtgen: Parsed " << state.functions.size() << " functions, "
              << state.aliases.size() << " aliases, " << state.classes.size() << " classes\n";

    std::cout << "rtgen: Generating output files in " << outputDir << "\n";
    generateNameMap(state, outputDir);
    generateClasses(state, outputDir);
    generateSignatures(state, outputDir, inputPath);

    std::cout << "rtgen: Done\n";
    return 0;
}

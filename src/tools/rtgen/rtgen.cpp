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
//   - RuntimeSigs.def        (signature enumerator definitions)
//
// Key invariants:
//   - Parses runtime.def line by line
//   - Validates no duplicate symbols or missing targets
//   - Pure C++17, no external dependencies
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
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
    std::string name;                       // Class name (e.g., "Viper.String")
    std::string type_id;                    // Type ID suffix (e.g., "String")
    std::string layout;                     // Layout type (e.g., "opaque*", "obj")
    std::string ctor_id;                    // Constructor function id or empty
    std::vector<RuntimeProperty> props;     // Properties
    std::vector<RuntimeMethod> methods;     // Methods
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
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t'))
        sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t'))
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
            if (sv[i] == '"' && (i == 0 || sv[i-1] != '\\'))
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
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t'))
        sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t'))
        sv.remove_suffix(1);
    return sv;
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
        if (line[i] == '"' && (i == 0 || line[i-1] != '\\'))
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
    auto stripQuotes = [](std::string &s) {
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
    auto stripQuotes = [](std::string &s) {
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
// Code Generation
//===----------------------------------------------------------------------===//

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

    out << fileHeader("RuntimeClasses.inc",
                      "Runtime class catalog with properties and methods.");

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

    std::cout << "rtgen: Done\n";
    return 0;
}

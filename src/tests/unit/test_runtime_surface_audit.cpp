//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_runtime_surface_audit.cpp
// Purpose: Audit the deliberate frontend-visible Viper.* runtime surface.
//
//===----------------------------------------------------------------------===//

#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct FunctionExpectation {
    std::string canonical;
    std::string symbol;
};

struct MethodExpectation {
    std::string className;
    std::string methodName;
    std::string signature;
};

struct PropertyExpectation {
    std::string className;
    std::string propertyName;
    std::string typeName;
};

struct SignatureRecord {
    std::string owner;
    std::string signature;
};

std::vector<std::string> internalHeaders() {
    std::vector<std::string> headers;
#define RUNTIME_SURFACE_INTERNAL_HEADER(path) headers.emplace_back(path);
#define RUNTIME_SURFACE_INTERNAL_SYMBOL(symbol)
#define RUNTIME_SURFACE_EXPECT_FUNCTION(canonical, symbol)
#define RUNTIME_SURFACE_EXPECT_METHOD(class_name, method_name, signature)
#define RUNTIME_SURFACE_EXPECT_PROPERTY(class_name, property_name, type_name)
#include "il/runtime/RuntimeSurfacePolicy.inc"
#undef RUNTIME_SURFACE_INTERNAL_HEADER
#undef RUNTIME_SURFACE_INTERNAL_SYMBOL
#undef RUNTIME_SURFACE_EXPECT_FUNCTION
#undef RUNTIME_SURFACE_EXPECT_METHOD
#undef RUNTIME_SURFACE_EXPECT_PROPERTY
    return headers;
}

std::vector<std::string> internalSymbols() {
    std::vector<std::string> symbols;
#define RUNTIME_SURFACE_INTERNAL_HEADER(path)
#define RUNTIME_SURFACE_INTERNAL_SYMBOL(symbol) symbols.emplace_back(symbol);
#define RUNTIME_SURFACE_EXPECT_FUNCTION(canonical, symbol)
#define RUNTIME_SURFACE_EXPECT_METHOD(class_name, method_name, signature)
#define RUNTIME_SURFACE_EXPECT_PROPERTY(class_name, property_name, type_name)
#include "il/runtime/RuntimeSurfacePolicy.inc"
#undef RUNTIME_SURFACE_INTERNAL_HEADER
#undef RUNTIME_SURFACE_INTERNAL_SYMBOL
#undef RUNTIME_SURFACE_EXPECT_FUNCTION
#undef RUNTIME_SURFACE_EXPECT_METHOD
#undef RUNTIME_SURFACE_EXPECT_PROPERTY
    return symbols;
}

std::vector<FunctionExpectation> expectedFunctions() {
    std::vector<FunctionExpectation> functions;
#define RUNTIME_SURFACE_INTERNAL_HEADER(path)
#define RUNTIME_SURFACE_INTERNAL_SYMBOL(symbol)
#define RUNTIME_SURFACE_EXPECT_FUNCTION(canonical, symbol)                                         \
    functions.push_back(FunctionExpectation{canonical, symbol});
#define RUNTIME_SURFACE_EXPECT_METHOD(class_name, method_name, signature)
#define RUNTIME_SURFACE_EXPECT_PROPERTY(class_name, property_name, type_name)
#include "il/runtime/RuntimeSurfacePolicy.inc"
#undef RUNTIME_SURFACE_INTERNAL_HEADER
#undef RUNTIME_SURFACE_INTERNAL_SYMBOL
#undef RUNTIME_SURFACE_EXPECT_FUNCTION
#undef RUNTIME_SURFACE_EXPECT_METHOD
#undef RUNTIME_SURFACE_EXPECT_PROPERTY
    return functions;
}

std::vector<MethodExpectation> expectedMethods() {
    std::vector<MethodExpectation> methods;
#define RUNTIME_SURFACE_INTERNAL_HEADER(path)
#define RUNTIME_SURFACE_INTERNAL_SYMBOL(symbol)
#define RUNTIME_SURFACE_EXPECT_FUNCTION(canonical, symbol)
#define RUNTIME_SURFACE_EXPECT_METHOD(class_name, method_name, signature)                          \
    methods.push_back(MethodExpectation{class_name, method_name, signature});
#define RUNTIME_SURFACE_EXPECT_PROPERTY(class_name, property_name, type_name)
#include "il/runtime/RuntimeSurfacePolicy.inc"
#undef RUNTIME_SURFACE_INTERNAL_HEADER
#undef RUNTIME_SURFACE_INTERNAL_SYMBOL
#undef RUNTIME_SURFACE_EXPECT_FUNCTION
#undef RUNTIME_SURFACE_EXPECT_METHOD
#undef RUNTIME_SURFACE_EXPECT_PROPERTY
    return methods;
}

std::vector<PropertyExpectation> expectedProperties() {
    std::vector<PropertyExpectation> properties;
#define RUNTIME_SURFACE_INTERNAL_HEADER(path)
#define RUNTIME_SURFACE_INTERNAL_SYMBOL(symbol)
#define RUNTIME_SURFACE_EXPECT_FUNCTION(canonical, symbol)
#define RUNTIME_SURFACE_EXPECT_METHOD(class_name, method_name, signature)
#define RUNTIME_SURFACE_EXPECT_PROPERTY(class_name, property_name, type_name)                      \
    properties.push_back(PropertyExpectation{class_name, property_name, type_name});
#include "il/runtime/RuntimeSurfacePolicy.inc"
#undef RUNTIME_SURFACE_INTERNAL_HEADER
#undef RUNTIME_SURFACE_INTERNAL_SYMBOL
#undef RUNTIME_SURFACE_EXPECT_FUNCTION
#undef RUNTIME_SURFACE_EXPECT_METHOD
#undef RUNTIME_SURFACE_EXPECT_PROPERTY
    return properties;
}

fs::path repoRoot() {
    return fs::path(VIPER_SOURCE_DIR);
}

std::string readText(const fs::path &path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string stripComments(std::string input) {
    std::string out;
    out.reserve(input.size());
    bool inLine = false;
    bool inBlock = false;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        char next = (i + 1 < input.size()) ? input[i + 1] : '\0';

        if (inLine) {
            if (c == '\n') {
                inLine = false;
                out.push_back(c);
            }
            continue;
        }

        if (inBlock) {
            if (c == '*' && next == '/') {
                inBlock = false;
                ++i;
            }
            continue;
        }

        if (c == '/' && next == '/') {
            inLine = true;
            ++i;
            continue;
        }
        if (c == '/' && next == '*') {
            inBlock = true;
            ++i;
            continue;
        }

        out.push_back(c);
    }
    return out;
}

bool lineContinuesPreprocessorDirective(std::string_view line) {
    size_t end = line.find_last_not_of(" \t\r");
    return end != std::string_view::npos && line[end] == '\\';
}

std::string stripPreprocessor(std::string input) {
    std::ostringstream out;
    std::istringstream in(input);
    std::string line;
    bool inDirectiveContinuation = false;

    while (std::getline(in, line)) {
        if (inDirectiveContinuation) {
            inDirectiveContinuation = lineContinuesPreprocessorDirective(line);
            continue;
        }

        std::string_view trimmed(line);
        size_t first = trimmed.find_first_not_of(" \t\r");
        if (first != std::string_view::npos)
            trimmed.remove_prefix(first);
        else
            trimmed = {};

        if (!trimmed.empty() && trimmed.front() == '#') {
            inDirectiveContinuation = lineContinuesPreprocessorDirective(line);
            continue;
        }

        out << line << '\n';
    }

    return out.str();
}

std::unordered_map<std::string, std::string> runtimeDefCanonicalsToSymbols() {
    const std::string text = readText(repoRoot() / "src/il/runtime/runtime.def");
    const std::regex funcRe(
        R"RTFUNC(RT_FUNC\(\s*([A-Za-z0-9_]+)\s*,\s*(rt_[A-Za-z0-9_]+)\s*,\s*"([^"]+)")RTFUNC");
    const std::regex aliasRe(R"RTALIAS(RT_ALIAS\(\s*"([^"]+)"\s*,\s*([A-Za-z0-9_]+)\s*\))RTALIAS");
    std::unordered_map<std::string, std::string> out;
    std::unordered_map<std::string, std::string> idsToSymbols;

    for (std::sregex_iterator it(text.begin(), text.end(), funcRe), end; it != end; ++it) {
        const std::string id = (*it)[1].str();
        const std::string symbol = (*it)[2].str();
        idsToSymbols.emplace(id, symbol);
        out.emplace((*it)[3].str(), symbol);
    }

    for (std::sregex_iterator it(text.begin(), text.end(), aliasRe), end; it != end; ++it) {
        const auto target = idsToSymbols.find((*it)[2].str());
        if (target != idsToSymbols.end())
            out.emplace((*it)[1].str(), target->second);
    }
    return out;
}

std::unordered_set<std::string> runtimeDefSymbols() {
    std::unordered_set<std::string> symbols;
    for (const auto &[canonical, symbol] : runtimeDefCanonicalsToSymbols())
        symbols.insert(symbol);
    return symbols;
}

std::vector<SignatureRecord> runtimeDefSignatures() {
    const std::string text = readText(repoRoot() / "src/il/runtime/runtime.def");
    std::vector<SignatureRecord> signatures;

    const std::regex funcRe(
        R"RTFUNC(RT_FUNC\([^,]+,\s*rt_[A-Za-z0-9_]+\s*,\s*"([^"]+)"\s*,\s*"([^"]+)")RTFUNC");
    for (std::sregex_iterator it(text.begin(), text.end(), funcRe), end; it != end; ++it)
        signatures.push_back(SignatureRecord{(*it)[1].str(), (*it)[2].str()});

    const std::regex methodRe(R"RTMETHOD(RT_METHOD\(\s*"([^"]+)"\s*,\s*"([^"]+)")RTMETHOD");
    for (std::sregex_iterator it(text.begin(), text.end(), methodRe), end; it != end; ++it)
        signatures.push_back(SignatureRecord{(*it)[1].str(), (*it)[2].str()});

    return signatures;
}

std::string signatureReturnToken(std::string signature) {
    size_t paren = signature.find('(');
    if (paren != std::string::npos)
        signature.resize(paren);
    while (!signature.empty() && std::isspace(static_cast<unsigned char>(signature.front())))
        signature.erase(signature.begin());
    while (!signature.empty() && std::isspace(static_cast<unsigned char>(signature.back())))
        signature.pop_back();
    if (!signature.empty() && signature.back() == '?')
        signature.pop_back();
    size_t typeArgs = signature.find('<');
    if (typeArgs != std::string::npos)
        signature.resize(typeArgs);
    return signature;
}

bool signatureHasRawPointerToken(const std::string &signature) {
    const std::regex rawPtrToken(R"((^|[,(]\s*)ptr(\s*[,)])?)");
    return std::regex_search(signature, rawPtrToken);
}

std::unordered_set<std::string> runtimeSourceTokens() {
    std::unordered_set<std::string> tokens;
    const std::regex re(R"(\brt_[A-Za-z0-9_]+\b)");
    const fs::path root = repoRoot() / "src/runtime";
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (!it->is_regular_file())
            continue;
        const fs::path path = it->path();
        const fs::path ext = path.extension();
        // .inc and .m are runtime implementation sources too: large 3D translation units are
        // split into cohesive .inc fragments (#included into their .c), and the Metal backend
        // is .m. Implementation tokens can therefore live in any of these.
        if (ext != ".c" && ext != ".cpp" && ext != ".inc" && ext != ".m")
            continue;
        const std::string text = readText(path);
        for (std::sregex_iterator rit(text.begin(), text.end(), re), rend; rit != rend; ++rit) {
            tokens.insert((*rit)[0].str());
        }
    }
    return tokens;
}

std::unordered_set<std::string> runtimeHeaderFunctionSymbols() {
    std::unordered_set<std::string> symbols;
    const std::regex re(R"(\b(rt_[A-Za-z0-9_]+)\s*\()");
    const fs::path root = repoRoot() / "src/runtime";
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (!it->is_regular_file())
            continue;
        const fs::path path = it->path();
        if (path.extension() != ".h" && path.extension() != ".hpp")
            continue;

        std::string text = readText(path);
        text = stripComments(std::move(text));
        text = stripPreprocessor(std::move(text));
        for (std::sregex_iterator rit(text.begin(), text.end(), re), rend; rit != rend; ++rit)
            symbols.insert((*rit)[1].str());
    }
    return symbols;
}

std::unordered_map<std::string, std::unordered_set<std::string>>
runtimeHeaderFunctionSymbolsByHeader() {
    std::unordered_map<std::string, std::unordered_set<std::string>> symbolsByHeader;
    const std::regex re(R"(\b(rt_[A-Za-z0-9_]+)\s*\()");
    const fs::path root = repoRoot() / "src/runtime";
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (!it->is_regular_file())
            continue;
        const fs::path path = it->path();
        if (path.extension() != ".h" && path.extension() != ".hpp")
            continue;

        std::string text = readText(path);
        text = stripComments(std::move(text));
        text = stripPreprocessor(std::move(text));
        auto &symbols = symbolsByHeader[fs::relative(path, repoRoot()).generic_string()];
        for (std::sregex_iterator rit(text.begin(), text.end(), re), rend; rit != rend; ++rit)
            symbols.insert((*rit)[1].str());
    }
    return symbolsByHeader;
}

const il::runtime::RuntimeClass *findClass(std::string_view qname) {
    const auto &catalog = il::runtime::runtimeClassCatalog();
    for (const auto &cls : catalog) {
        if (std::string_view(cls.qname) == qname)
            return &cls;
    }
    return nullptr;
}

} // namespace

TEST(RuntimeSurfaceAudit, InternalHeadersExist) {
    for (const auto &header : internalHeaders()) {
        const fs::path path = repoRoot() / header;
        EXPECT_TRUE(fs::exists(path));
    }
}

TEST(RuntimeSurfaceAudit, InternalSymbolsExistInRuntimeHeaders) {
    const auto headerSymbols = runtimeHeaderFunctionSymbols();
    for (const auto &symbol : internalSymbols()) {
        const bool present = headerSymbols.count(symbol) != 0;
        if (!present)
            std::cerr << "Missing internal runtime header symbol " << symbol << "\n";
        EXPECT_TRUE(present);
    }
}

TEST(RuntimeSurfaceAudit, BroadInternalHeadersDoNotExposeRuntimeDefSymbols) {
    const auto headerSymbols = runtimeHeaderFunctionSymbolsByHeader();
    const auto defSymbols = runtimeDefSymbols();

    const std::unordered_map<std::string, std::unordered_set<std::string>> allowedPublicTokens = {
        // `rt_gui_internal.h` contains inline helpers that call public string APIs; the
        // broad-header
        // policy remains correct because the header does not declare these functions.
        {"src/runtime/graphics/gui/rt_gui_internal.h", {"rt_str_len", "rt_string_cstr"}},
    };

    for (const auto &header : internalHeaders()) {
        const auto headerIt = headerSymbols.find(header);
        if (headerIt == headerSymbols.end())
            continue;

        const auto allowIt = allowedPublicTokens.find(header);
        const std::unordered_set<std::string> emptyAllow;
        const auto &allowed = (allowIt != allowedPublicTokens.end()) ? allowIt->second : emptyAllow;

        for (const auto &symbol : headerIt->second) {
            if (defSymbols.count(symbol) == 0 || allowed.count(symbol) != 0)
                continue;
            std::cerr << "Broad internal header still references runtime.def symbol " << symbol
                      << " in " << header << "\n";
            EXPECT_TRUE(false);
        }
    }
}

TEST(RuntimeSurfaceAudit, RuntimeDefSymbolsExistInRuntimeSources) {
    const auto defSymbols = runtimeDefSymbols();
    const auto sourceTokens = runtimeSourceTokens();

    for (const auto &symbol : defSymbols) {
        const bool present = sourceTokens.count(symbol) != 0;
        if (!present)
            std::cerr << "Missing runtime implementation token for " << symbol << "\n";
        EXPECT_TRUE(present);
    }
}

TEST(RuntimeSurfaceAudit, RuntimeDefHasNoRawPointerSurfaceTypes) {
    for (const auto &record : runtimeDefSignatures()) {
        if (signatureReturnToken(record.signature) == "ptr" ||
            signatureHasRawPointerToken(record.signature)) {
            std::cerr << "Raw pointer type remains in runtime.def surface: " << record.owner
                      << " : " << record.signature << "\n";
            EXPECT_TRUE(false);
        }
    }
}

TEST(RuntimeSurfaceAudit, ZiaCallbackBridgeRolesAreDeclaredInRuntimeDef) {
    const std::string text = readText(repoRoot() / "src/il/runtime/runtime.def");
    const std::vector<std::string> required = {
        "RT_BRIDGE(ThreadStart, \"callback,payload\")",
        "RT_BRIDGE(ThreadStartSafe, \"callback,payload\")",
        "RT_BRIDGE(AsyncRun, \"callback,payload\")",
        "RT_BRIDGE(AsyncMap, \"none,callback,payload\")",
        "RT_BRIDGE(HttpServerBindHandler, \"none,none,callback\")",
        "RT_BRIDGE(HttpsServerBindHandler, \"none,none,callback\")",
    };
    for (const auto &needle : required) {
        if (text.find(needle) == std::string::npos)
            std::cerr << "Missing runtime.def callback bridge metadata: " << needle << "\n";
        EXPECT_TRUE(text.find(needle) != std::string::npos);
    }
}

TEST(RuntimeSurfaceAudit, ExpectedFunctionsAreRegistered) {
    const auto canonicals = runtimeDefCanonicalsToSymbols();

    for (const auto &expected : expectedFunctions()) {
        const auto it = canonicals.find(expected.canonical);
        const bool found = it != canonicals.end();
        if (!found)
            std::cerr << "Missing runtime.def function " << expected.canonical << "\n";
        ASSERT_TRUE(found);
        EXPECT_EQ(it->second, expected.symbol);
    }
}

TEST(RuntimeSurfaceAudit, ExpectedRuntimeMethodsExistInCatalog) {
    for (const auto &expected : expectedMethods()) {
        const auto *cls = findClass(expected.className);
        if (!cls)
            std::cerr << "Missing runtime class " << expected.className << "\n";
        ASSERT_TRUE(cls != nullptr);

        bool found = false;
        for (const auto &method : cls->methods) {
            if (std::string_view(method.name) == expected.methodName &&
                std::string_view(method.signature) == expected.signature) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "Missing runtime method " << expected.className << "."
                      << expected.methodName << " with signature " << expected.signature << "\n";
        }
        EXPECT_TRUE(found);
    }
}

TEST(RuntimeSurfaceAudit, ExpectedRuntimePropertiesExistInCatalog) {
    for (const auto &expected : expectedProperties()) {
        const auto *cls = findClass(expected.className);
        if (!cls)
            std::cerr << "Missing runtime class " << expected.className << "\n";
        ASSERT_TRUE(cls != nullptr);

        bool found = false;
        for (const auto &prop : cls->properties) {
            if (std::string_view(prop.name) == expected.propertyName &&
                std::string_view(prop.type) == expected.typeName) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "Missing runtime property " << expected.className << "."
                      << expected.propertyName << " : " << expected.typeName << "\n";
        }
        EXPECT_TRUE(found);
    }
}

TEST(RuntimeSurfaceAudit, GuiVideoWidgetReadWritePropertiesHaveGetterAndSetterTargets) {
    const auto *cls = findClass("Viper.GUI.VideoWidget");
    ASSERT_TRUE(cls != nullptr);

    for (std::string_view name : {"ShowControls", "Loop"}) {
        const il::runtime::RuntimeProperty *found = nullptr;
        for (const auto &prop : cls->properties) {
            if (std::string_view(prop.name) == name) {
                found = &prop;
                break;
            }
        }
        ASSERT_TRUE(found != nullptr);
        EXPECT_TRUE(found->getter != nullptr);
        EXPECT_TRUE(found->setter != nullptr);
        EXPECT_FALSE(found->readonly);
    }
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}

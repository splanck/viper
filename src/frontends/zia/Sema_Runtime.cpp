//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file Sema_Runtime.cpp
/// @brief Runtime function registration for the Zia semantic analyzer.
///
/// @details This file implements the initialization of runtime function bindings
/// for the Zia semantic analyzer. It bridges the IL-layer RuntimeRegistry with
/// the Zia type system, enabling full type-checking of runtime function calls.
///
/// ## Registration Process
///
/// The initRuntimeFunctions() method performs three phases of registration:
///
/// ### Phase 1: Runtime Class Types
///
/// Registers each runtime class (e.g., "Viper.String", "Viper.File") as a
/// type in the Zia type registry. This enables the semantic analyzer to
/// recognize expressions like `new Viper.Graphics.Canvas(...)` and property
/// accesses like `canvas.Width`.
///
/// ### Phase 2: Runtime Function Fallbacks (ZiaRuntimeExterns.inc)
///
/// Reads the generated ZiaRuntimeExterns.inc metadata table and registers
/// ABI-shaped fallback extern signatures for every RT_FUNC entry.
/// These cover runtime calls that are not described by the runtime-class
/// catalog, such as `Viper.Time.Clock.Sleep` or `Viper.Game.LevelData.ObjectType`.
///
/// ### Phase 3: Methods and Properties from RuntimeRegistry
///
/// For each runtime class in the catalog:
///
/// 1. **Methods**: Parses the signature string (e.g., "str(i64,i64)") and
///    refines the function with full parameter type information. This enables
///    the semantic analyzer to validate argument types at compile time.
///
/// 2. **Properties**: Registers getter and setter functions. Getters are
///    zero-parameter functions returning the property type. Setters are
///    void functions taking the property type as a parameter.
///
/// ## Type Conversion
///
/// The RuntimeAdapter functions (toZiaType, toZiaParamTypes) convert IL-layer
/// type representations to Zia semantic types:
///
/// - ILScalarType::I64 → types::integer()
/// - ILScalarType::F64 → types::number()
/// - ILScalarType::Bool → types::boolean()
/// - ILScalarType::String → types::string()
/// - ILScalarType::Object → typed runtime classes or types::any()
/// - ILScalarType::Void → types::voidType()
///
/// ## Example Registration
///
/// For `Viper.String.Substring` with signature "str(i64,i64)":
///
/// ```cpp
/// // Parsed signature: returnType=String, params=[I64, I64]
/// defineExternFunction(
///     "Viper.String.Substring",  // extern target name
///     types::string(),           // return type
///     {types::integer(), types::integer()}  // parameter types
/// );
/// ```
///
/// This enables the semantic analyzer to verify that calls like:
/// ```zia
/// var s = "hello".Substring(0, 3)  // OK: Integer arguments
/// var s = "hello".Substring("a")   // ERROR: String argument, expected Integer
/// ```
///
/// ## Thread Safety
///
/// This function is called once during Sema initialization before any
/// concurrent access. The RuntimeRegistry itself is thread-safe and immutable.
///
/// @see RuntimeAdapter.hpp - Type conversion between IL and Zia types
/// @see il::runtime::RuntimeRegistry - Source of runtime signatures
/// @see Sema::defineExternFunction - Registers extern functions in symbol table
/// @see ZiaRuntimeExterns.inc - Generated fallback extern metadata
///
//===----------------------------------------------------------------------===//

#include "frontends/zia/RuntimeAdapter.hpp"
#include "frontends/zia/Sema.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace il::frontends::zia {

namespace {

struct ZiaRuntimeExternSpec {
    std::string_view canonical;
    std::string_view signature;
    std::string_view paramNames;
    std::string_view bridgeRoles;
};

#include "il/runtime/ZiaRuntimeExterns.inc"

struct RuntimeReturnOverride {
    std::string_view canonical;
    std::string_view className;
};

static std::string_view trimRuntimeToken(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
        value.remove_suffix(1);
    return value;
}

static std::string_view runtimeTokenBase(std::string_view token) {
    token = trimRuntimeToken(token);
    size_t genericStart = token.find('<');
    if (genericStart == std::string_view::npos)
        return token;
    return trimRuntimeToken(token.substr(0, genericStart));
}

static std::string_view runtimeTokenTypeArg(std::string_view token) {
    token = trimRuntimeToken(token);
    size_t genericStart = token.find('<');
    size_t genericEnd = token.rfind('>');
    if (genericStart == std::string_view::npos || genericEnd == std::string_view::npos ||
        genericEnd <= genericStart)
        return {};
    return trimRuntimeToken(token.substr(genericStart + 1, genericEnd - genericStart - 1));
}

static bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static bool isGeneratedFactoryMethod(std::string_view method) {
    if (method == "New" || method == "Clone" || method == "Copy" || method == "Zero" ||
        method == "Range")
        return true;

    return startsWith(method, "Open") || startsWith(method, "Load") || startsWith(method, "From") ||
           startsWith(method, "Parse") || startsWith(method, "Read") ||
           startsWith(method, "Decode") || startsWith(method, "Create");
}

static bool isKnownRuntimeClass(const std::vector<il::runtime::RuntimeClass> &catalog,
                                std::string_view className) {
    for (const auto &cls : catalog) {
        if (cls.qname && className == cls.qname)
            return true;
    }
    return false;
}

static std::string_view generatedReturnOverride(std::string_view canonical) {
    static constexpr RuntimeReturnOverride kReturnOverrides[] = {
        // Crypto functions returning Viper.Collections.Bytes
        {"Viper.Crypto.SecureRandom.Bytes", "Viper.Collections.Bytes"},
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
        {"Viper.Data.Csv.ParseLine", "Viper.Collections.Seq"},
        {"Viper.Data.Csv.ParseLineWith", "Viper.Collections.Seq"},
        {"Viper.Data.Csv.Parse", "Viper.Collections.Seq"},
        {"Viper.Data.Csv.ParseWith", "Viper.Collections.Seq"},
        {"Viper.Text.Markdown.ExtractLinks", "Viper.Collections.Seq"},
        {"Viper.Text.Markdown.ExtractHeadings", "Viper.Collections.Seq"},
        {"Viper.Text.Html.ExtractLinks", "Viper.Collections.Seq"},
        {"Viper.Text.Html.ExtractText", "Viper.Collections.Seq"},
        // Collection methods returning Seq from non-Seq classes
        {"Viper.Collections.StringSet.Items", "Viper.Collections.Seq"},
        {"Viper.Collections.SortedSet.Items", "Viper.Collections.Seq"},
        {"Viper.Collections.Set.Items", "Viper.Collections.Seq"},
    };

    for (const auto &override : kReturnOverrides) {
        if (canonical == override.canonical)
            return override.className;
    }
    return {};
}

static TypeRef ziaParamTypeForGeneratedToken(std::string_view token) {
    token = trimRuntimeToken(token);
    if (!token.empty() && token.back() == '?') {
        token.remove_suffix(1);
        return types::optional(ziaParamTypeForGeneratedToken(token));
    }

    std::string_view base = runtimeTokenBase(token);
    if (base == "str")
        return types::string();
    if (base == "i64" || base == "i32" || base == "i16")
        return types::integer();
    if (base == "f64")
        return types::number();
    if (base == "i1" || base == "bool")
        return types::boolean();
    if (base == "void")
        return types::voidType();
    if (base == "seq") {
        std::string_view elem = runtimeTokenTypeArg(token);
        if (elem.empty())
            return types::ptr();
        return types::seqOf(ziaParamTypeForGeneratedToken(elem));
    }
    if (base == "list") {
        std::string_view elem = runtimeTokenTypeArg(token);
        if (elem.empty())
            return types::ptr();
        return types::list(ziaParamTypeForGeneratedToken(elem));
    }
    if (base == "ptr")
        return types::ptr();
    return types::any();
}

static std::vector<std::string_view> generatedSignatureParamTokens(std::string_view signature) {
    std::vector<std::string_view> tokens;
    size_t open = signature.find('(');
    size_t close = signature.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
        return tokens;

    std::string_view args = signature.substr(open + 1, close - open - 1);
    size_t start = 0;
    int angleDepth = 0;
    for (size_t i = 0; i <= args.size(); ++i) {
        if (i < args.size()) {
            if (args[i] == '<')
                ++angleDepth;
            else if (args[i] == '>' && angleDepth > 0)
                --angleDepth;
        }

        if (i == args.size() || (args[i] == ',' && angleDepth == 0)) {
            std::string_view token = trimRuntimeToken(args.substr(start, i - start));
            if (!token.empty())
                tokens.push_back(token);
            start = i + 1;
        }
    }
    return tokens;
}

static std::vector<TypeRef> ziaParamTypesForGeneratedExtern(std::string_view signature) {
    std::vector<TypeRef> paramTypes;
    auto tokens = generatedSignatureParamTokens(signature);
    paramTypes.reserve(tokens.size());
    for (std::string_view token : tokens)
        paramTypes.push_back(ziaParamTypeForGeneratedToken(token));
    return paramTypes;
}

static TypeRef ziaReturnTypeForGeneratedExtern(
    std::string_view canonical,
    const il::runtime::ParsedSignature &sig,
    const std::vector<il::runtime::RuntimeClass> &catalog) {
    if (!sig.objectTypeName.empty())
        return types::runtimeClass(sig.objectTypeName);

    if (!sig.elementTypeName.empty()) {
        TypeRef elemType = ziaParamTypeForGeneratedToken(sig.elementTypeName);
        if (sig.containerTypeName == "list")
            return types::list(elemType);
        return types::seqOf(elemType);
    }

    if (sig.returnType != il::runtime::ILScalarType::Object)
        return toZiaType(sig.returnType);

    if (std::string_view overrideClass = generatedReturnOverride(canonical); !overrideClass.empty())
        return types::runtimeClass(std::string(overrideClass));

    size_t lastDot = canonical.rfind('.');
    if (lastDot != std::string_view::npos) {
        std::string_view method = canonical.substr(lastDot + 1);
        std::string_view className = canonical.substr(0, lastDot);
        if (isGeneratedFactoryMethod(method) || isKnownRuntimeClass(catalog, className))
            return types::runtimeClass(std::string(className));
    }

    return types::any();
}

static std::vector<std::string> splitGeneratedParamNames(std::string_view encoded) {
    std::vector<std::string> names;
    if (encoded.empty())
        return names;

    size_t start = 0;
    while (true) {
        size_t end = encoded.find('\n', start);
        if (end == std::string_view::npos) {
            names.emplace_back(encoded.substr(start));
            break;
        }
        names.emplace_back(encoded.substr(start, end - start));
        start = end + 1;
    }
    return names;
}

static Sema::RuntimePointerBridgeRole generatedBridgeRole(char code) {
    if (code == 'c')
        return Sema::RuntimePointerBridgeRole::Callback;
    if (code == 'p')
        return Sema::RuntimePointerBridgeRole::Payload;
    return Sema::RuntimePointerBridgeRole::None;
}

static std::vector<Sema::RuntimePointerBridgeRole> decodeGeneratedBridgeRoles(
    std::string_view encoded, std::size_t paramCount) {
    if (encoded.empty())
        return {};

    std::vector<Sema::RuntimePointerBridgeRole> roles;
    roles.reserve(paramCount);
    for (std::size_t i = 0; i < paramCount; ++i) {
        char code = i < encoded.size() ? encoded[i] : 'n';
        roles.push_back(generatedBridgeRole(code));
    }
    return roles;
}

} // namespace

/// @brief Heuristic: should a plain-object runtime @p method be treated as
///        returning the owner type? Accessor-style names (Get*/Keys/Values/
///        Pop/Peek/First/Last/Find/…) return an element instead, so they are
///        excluded; everything else infers an owner-typed return.
static bool shouldInferOwnerReturnForPlainObject(const il::runtime::RuntimeMethod &method) {
    if (!method.name)
        return true;

    std::string_view name(method.name);
    if (name == "Keys" || name == "Values" || name == "Items" || name == "Indices" ||
        name == "Get" || name == "GetOr" || name == "GetFirst" || name == "Peek" || name == "Pop" ||
        name == "TryPop" || name == "PeekFront" || name == "PeekBack" || name == "PopFront" ||
        name == "PopBack" || name == "TryPopFront" || name == "TryPopBack" || name == "First" ||
        name == "Last" || name == "Next" || name == "Find" || name == "FindWhere" || name == "Fold")
        return false;

    if (name.rfind("Get", 0) == 0)
        return false;

    return true;
}

/// @brief True if runtime @p method's target function belongs to runtime
///        class @p className (matches the "<class>." prefix on the target).
static bool methodTargetBelongsToClass(const il::runtime::RuntimeMethod &method,
                                       const char *className) {
    if (!method.target || !className)
        return false;

    std::string_view target(method.target);
    std::string_view owner(className);
    return target.size() > owner.size() && target.compare(0, owner.size(), owner) == 0 &&
           target[owner.size()] == '.';
}

/// @brief Initializes all runtime function bindings for semantic analysis.
///
/// @details This method populates the Zia semantic analyzer's symbol table
/// with extern declarations for all runtime functions. It uses the unified
/// RuntimeRegistry to ensure signature information is consistent with other
/// frontends.
///
/// The registration happens in three phases:
///
/// 1. **Type Registration**: Each runtime class is registered as a named type,
///    enabling `new ClassName()` expressions and type annotations.
///
/// 2. **Fallback Registration**: Each generated RT_FUNC row is
///    registered with ABI-shaped parameter types and pointer-safety metadata.
///
/// 3. **Method/Property Registration**: For each class, all methods and
///    properties refine the fallback signatures with catalog-level type
///    information. Methods get their signature from parseRuntimeSignature();
///    properties get separate getter and setter registrations.
///
/// ## Error Handling
///
/// Methods with unparseable signatures (isValid() returns false) are silently
/// skipped. This is acceptable because:
/// - The signature format is well-defined and generated by rtgen
/// - Invalid signatures indicate a bug in runtime.def, not user code
/// - The method simply won't be available for use
///
/// ## Performance
///
/// This function is called once during Sema construction. The cost is O(n*m)
/// where n is the number of runtime classes and m is the average number of
/// methods/properties per class. With the current runtime library (~150
/// classes, ~2000 methods), this takes negligible time.
///
void Sema::initRuntimeFunctions() {
    // Access the singleton RuntimeRegistry which contains all parsed signatures
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    const auto &catalog = registry.rawCatalog();
    auto registerOrRefineExtern = [&](const std::string &name,
                                      TypeRef returnType,
                                      const std::vector<TypeRef> &fallbackParamTypes,
                                      std::optional<RuntimePointerSafety> pointerSafety =
                                          std::nullopt,
                                      const std::vector<std::string> &paramNames = {}) {
        if (name.empty())
            return;

        if (Symbol *existing = currentScope_->lookupLocal(name);
            existing && existing->isExtern && existing->kind == Symbol::Kind::Function &&
            existing->type && existing->type->kind == TypeKindSem::Function) {
            std::optional<RuntimePointerSafety> refinedSafety = std::nullopt;
            if (runtimePointerSafety_.find(name) == runtimePointerSafety_.end())
                refinedSafety = std::move(pointerSafety);
            std::vector<std::string> effectiveParamNames =
                paramNames.empty() ? existing->paramNames : paramNames;
            defineExternFunction(
                name, returnType, existing->type->paramTypes(), effectiveParamNames, refinedSafety);
            return;
        }

        defineExternFunction(
            name, returnType, fallbackParamTypes, paramNames, std::move(pointerSafety));
    };

    //==========================================================================
    // Phase 1: Register runtime class types
    //==========================================================================
    // Each runtime class becomes a named type in the Zia type registry.
    // This enables type checking for:
    // - Variable declarations: `var f: Viper.File`
    // - Constructor calls: `new Viper.File("path.txt")`
    // - Type comparisons and casts
    for (const auto &cls : catalog) {
        typeRegistry_[cls.qname] = types::runtimeClass(cls.qname);
    }

    //==========================================================================
    // Phase 2: Register RT_FUNC fallback externs from runtime.def
    //==========================================================================
    // The ZiaRuntimeExterns.inc table is generated by rtgen from runtime.def.
    // It provides ABI-shaped fallback signatures for all RT_FUNC entries without
    // forcing clang to optimize thousands of generated registration statements.
    // Phase 3 below overrides any entries that have richer runtime-class
    // metadata (e.g. receiver-less method signatures or typed seq<T> returns).
    for (const auto &entry : kZiaRuntimeExterns) {
        auto sig = il::runtime::parseRuntimeSignature(entry.signature);
        if (!sig.isValid())
            continue;

        TypeRef returnType = ziaReturnTypeForGeneratedExtern(entry.canonical, sig, catalog);
        if (sig.isOptionalReturn)
            returnType = types::optional(returnType);

        RuntimePointerSafety pointerSafety{
            sig.rawPointerReturn,
            sig.rawPointerParams,
            decodeGeneratedBridgeRoles(entry.bridgeRoles, sig.params.size()),
        };

        registerOrRefineExtern(std::string(entry.canonical),
                               returnType,
                               ziaParamTypesForGeneratedExtern(entry.signature),
                               std::move(pointerSafety),
                               splitGeneratedParamNames(entry.paramNames));
    }

    //==========================================================================
    // Phase 3: Register methods and properties with full signatures (fine)
    //==========================================================================
    // This phase runs AFTER Phase 2 so that typed returns (e.g. seqOf(string)
    // for seq<str>-annotated methods) override the coarse ptr() from Phase 2.
    for (const auto &cls : catalog) {
        //----------------------------------------------------------------------
        // Register all methods for this class
        //----------------------------------------------------------------------
        for (const auto &m : cls.methods) {
            // Parse the signature string (e.g., "str(i64,i64)") into structured form
            auto sig = il::runtime::parseRuntimeSignature(m.signature ? m.signature : "");
            if (!sig.isValid())
                continue; // Skip methods with unparseable signatures

            // Convert IL types to Zia types, honouring element type hints from seq<T>.
            TypeRef returnType = toZiaReturnType(sig);
            // When a class method returns plain 'obj' (no element type annotation),
            // infer the owning class type only for methods that conventionally return
            // the receiver's class. Accessors and snapshot methods intentionally remain
            // opaque unless runtime.def provides an explicit obj<Class> or seq<T> return.
            if (sig.returnType == il::runtime::ILScalarType::Object &&
                sig.elementTypeName.empty() && sig.objectTypeName.empty() && cls.qname &&
                methodTargetBelongsToClass(m, cls.qname) && shouldInferOwnerReturnForPlainObject(m))
                returnType = types::runtimeClass(cls.qname);
            if (sig.isOptionalReturn)
                returnType = types::optional(returnType);
            std::vector<TypeRef> paramTypes = toZiaParamTypes(sig);
            RuntimePointerSafety pointerSafety{sig.rawPointerReturn, sig.rawPointerParams, {}};

            // Preserve ABI-shaped explicit receiver signatures from Phase 2 while
            // refining the return type for method-style semantic analysis.
            registerOrRefineExtern(m.target ? m.target : "", returnType, paramTypes, pointerSafety);
        }

        //----------------------------------------------------------------------
        // Register property getters and setters
        //----------------------------------------------------------------------
        for (const auto &p : cls.properties) {
            // Convert the property's IL type to a Zia type. Property type strings
            // may use the same typed object annotation as function signatures
            // (for example obj<Viper.GUI.Widget>).
            TypeRef propType = nullptr;
            std::string propSigText = std::string(p.type ? p.type : "") + "()";
            auto propSig = il::runtime::parseRuntimeSignature(propSigText);
            if (propSig.isValid()) {
                propType = toZiaReturnType(propSig);
                if (propSig.isOptionalReturn)
                    propType = types::optional(propType);
            } else {
                propType = toZiaType(il::runtime::mapILToken(p.type ? p.type : ""));
            }

            // Register getter: no parameters, returns property type
            // Example: Viper.String.get_Length() -> Integer
            if (p.getter) {
                registerOrRefineExtern(p.getter, propType, {});
            }

            // Register setter: takes property type, returns void
            // Example: Viper.GUI.Widget.set_Visible(Boolean) -> void
            if (p.setter) {
                registerOrRefineExtern(p.setter, types::voidType(), {propType});
            }
        }
    }
}

} // namespace il::frontends::zia

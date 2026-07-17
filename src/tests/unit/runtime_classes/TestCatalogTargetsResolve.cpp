//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestCatalogTargetsResolve.cpp
// Purpose: Lint runtime class catalog against the runtime signature registry.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//
#include "il/runtime/RuntimeSignatures.hpp"
#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using il::core::Type;
using il::runtime::RuntimeDescriptor;

std::string trim(std::string s) {
    auto isspace2 = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !isspace2(c); }));
    s.erase(
        std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !isspace2(c); }).base(),
        s.end());
    return s;
}

std::vector<std::string> parseArgs(std::string_view sig) {
    std::vector<std::string> out;
    auto lp = sig.find('(');
    auto rp = sig.rfind(')');
    if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp)
        return out;
    std::string args(sig.substr(lp + 1, rp - lp - 1));
    std::stringstream ss(args);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        tok = trim(tok);
        if (!tok.empty())
            out.push_back(tok);
    }
    return out;
}

Type::Kind mapTokenToKind(const std::string &tok) {
    std::string lower;
    lower.reserve(tok.size());
    for (unsigned char c : tok)
        lower.push_back(static_cast<char>(std::tolower(c)));

    if (lower == "i64")
        return Type::Kind::I64;
    if (lower == "i32")
        return Type::Kind::I32;
    if (lower == "i16")
        return Type::Kind::I16;
    if (lower == "f64")
        return Type::Kind::F64;
    if (lower == "i1")
        return Type::Kind::I1;
    if (lower == "str" || lower == "string")
        return Type::Kind::Str;
    if (lower == "obj" || lower == "ptr" || lower.rfind("obj<", 0) == 0 ||
        lower.rfind("seq<", 0) == 0 || lower.rfind("list<", 0) == 0 || lower.rfind("map<", 0) == 0)
        return Type::Kind::Ptr;
    if (lower == "void")
        return Type::Kind::Void;
    // default conservative
    return Type::Kind::I64;
}

bool nonEmpty(const char *s) {
    return s && s[0] != '\0';
}

bool kindCompatible(Type::Kind got, Type::Kind want) {
    if (got == want)
        return true;
    return (got == Type::Kind::I1 && want == Type::Kind::I64) ||
           (got == Type::Kind::I64 && want == Type::Kind::I1);
}

} // namespace

TEST(RuntimeClassCatalogTargets, AllTargetsResolveAndMatchArity) {
    const auto &reg = il::runtime::runtimeRegistry();
    std::map<std::string, const RuntimeDescriptor *> map;
    for (const auto &d : reg)
        map.emplace(std::string(d.name), &d);

    std::vector<std::string> errors;

    const auto &classes = il::runtime::runtimeClassCatalog();
    for (const auto &c : classes) {
        // Properties
        for (const auto &p : c.properties) {
            const Type::Kind propKind = mapTokenToKind(p.type ? std::string(p.type) : "");
            if (nonEmpty(p.getter)) {
                auto it = map.find(p.getter);
                if (it == map.end()) {
                    errors.push_back("missing descriptor for getter: " + std::string(p.getter));
                } else {
                    const auto *d = it->second;
                    const size_t gotParams = d->signature.paramTypes.size();
                    if (gotParams != 0 && gotParams != 1) {
                        std::ostringstream os;
                        os << "getter arity mismatch for '" << p.getter << "': got " << gotParams
                           << ", want 0 for static or 1 for instance";
                        errors.push_back(os.str());
                    }
                    if (!kindCompatible(d->signature.retType.kind, propKind)) {
                        std::ostringstream os;
                        os << "getter return kind mismatch for '" << p.getter << "': got "
                           << il::core::kindToString(d->signature.retType.kind) << ", want "
                           << il::core::kindToString(propKind);
                        errors.push_back(os.str());
                    }
                }
            }
            if (nonEmpty(p.setter)) {
                auto it = map.find(p.setter);
                if (it == map.end()) {
                    errors.push_back("missing descriptor for setter: " + std::string(p.setter));
                } else {
                    const auto *d = it->second;
                    const size_t gotParams = d->signature.paramTypes.size();
                    if (gotParams != 1 && gotParams != 2) {
                        std::ostringstream os;
                        os << "setter arity mismatch for '" << p.setter << "': got " << gotParams
                           << ", want 1 for static or 2 for instance";
                        errors.push_back(os.str());
                    } else {
                        const Type::Kind got = d->signature.paramTypes[gotParams - 1].kind;
                        if (!kindCompatible(got, propKind)) {
                            std::ostringstream os;
                            os << "setter value kind mismatch for '" << p.setter << "': got "
                               << il::core::kindToString(got) << ", want "
                               << il::core::kindToString(propKind);
                            errors.push_back(os.str());
                        }
                    }
                }
            }
        }

        // Methods
        for (const auto &m : c.methods) {
            if (!nonEmpty(m.target))
                continue;
            auto it = map.find(m.target);
            if (it == map.end()) {
                errors.push_back("missing descriptor for method: " + std::string(m.target));
                continue;
            }
            const auto *d = it->second;
            auto args =
                parseArgs(m.signature ? std::string_view(m.signature) : std::string_view(""));
            const size_t expectedInstanceParams = 1 + args.size();
            const size_t expectedStaticParams = args.size();
            const size_t gotParams = d->signature.paramTypes.size();
            if (gotParams != expectedInstanceParams && gotParams != expectedStaticParams) {
                std::ostringstream os;
                os << "method arity mismatch for '" << m.target << "': got " << gotParams
                   << ", want " << expectedInstanceParams << " for instance or "
                   << expectedStaticParams << " for static/factory";
                errors.push_back(os.str());
            } else {
                const bool hasReceiver = gotParams == expectedInstanceParams;
                // Optional: verify arg kinds beyond receiver
                for (size_t i = 0; i < args.size(); ++i) {
                    Type::Kind want = mapTokenToKind(args[i]);
                    Type::Kind got = d->signature.paramTypes[(hasReceiver ? 1U : 0U) + i].kind;
                    if (!kindCompatible(got, want)) {
                        std::ostringstream os;
                        os << "param[" << i << "] kind mismatch for '" << m.target << "': got "
                           << il::core::kindToString(got) << ", want "
                           << il::core::kindToString(want);
                        errors.push_back(os.str());
                    }
                }
            }
        }
    }

    if (!errors.empty()) {
        std::ostringstream os;
        os << "Runtime class catalog target check failed (" << errors.size() << "):\n";
        for (const auto &e : errors)
            os << "  - " << e << "\n";
        std::cerr << os.str();
        EXPECT_TRUE(errors.empty());
    }
}

TEST(RuntimeClassCatalogTargets, TypedReturnMetadataMatchesRawTarget) {
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    std::vector<std::string> errors;

    const auto &classes = il::runtime::runtimeClassCatalog();
    for (const auto &cls : classes) {
        for (const auto &method : cls.methods) {
            if (!method.target || !method.signature)
                continue;

            auto methodSig = il::runtime::parseRuntimeSignature(method.signature);
            auto rawSig = registry.findFunction(method.target);
            if (!rawSig || !methodSig.isValid())
                continue;

            const bool methodHasTypedReturn = !methodSig.containerTypeName.empty() ||
                                              !methodSig.elementTypeName.empty() ||
                                              !methodSig.objectTypeName.empty();
            const bool rawHasTypedReturn = !rawSig->containerTypeName.empty() ||
                                           !rawSig->elementTypeName.empty() ||
                                           !rawSig->objectTypeName.empty();
            if (!methodHasTypedReturn && !rawHasTypedReturn)
                continue;

            if (methodSig.returnType != rawSig->returnType ||
                methodSig.containerTypeName != rawSig->containerTypeName ||
                methodSig.elementTypeName != rawSig->elementTypeName ||
                methodSig.objectTypeName != rawSig->objectTypeName) {
                std::ostringstream os;
                os << "typed return mismatch for '" << method.target << "' in " << cls.qname << "."
                   << method.name;
                errors.push_back(os.str());
            }
        }
    }

    if (!errors.empty()) {
        std::ostringstream os;
        os << "Typed runtime return metadata drifted (" << errors.size() << "):\n";
        for (const auto &e : errors)
            os << "  - " << e << "\n";
        std::cerr << os.str();
    }
    EXPECT_TRUE(errors.empty());
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}

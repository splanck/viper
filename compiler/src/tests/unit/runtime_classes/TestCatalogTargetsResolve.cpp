//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime_classes/TestCatalogTargetsResolve.cpp
// Purpose: Lint runtime class catalog against the runtime signature registry.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
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

namespace
{

using il::core::Type;
using il::runtime::RuntimeDescriptor;

std::string trim(std::string s)
{
    auto isspace2 = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), [&](unsigned char c) { return !isspace2(c); }));
    s.erase(
        std::find_if(s.rbegin(), s.rend(), [&](unsigned char c) { return !isspace2(c); }).base(),
        s.end());
    return s;
}

std::vector<std::string> parseArgs(std::string_view sig)
{
    std::vector<std::string> out;
    auto lp = sig.find('(');
    auto rp = sig.rfind(')');
    if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp)
        return out;
    std::string args(sig.substr(lp + 1, rp - lp - 1));
    std::stringstream ss(args);
    std::string tok;
    while (std::getline(ss, tok, ','))
    {
        tok = trim(tok);
        if (!tok.empty())
            out.push_back(tok);
    }
    return out;
}

Type::Kind mapTokenToKind(const std::string &tok)
{
    if (tok == "i64")
        return Type::Kind::I64;
    if (tok == "f64")
        return Type::Kind::F64;
    if (tok == "i1")
        return Type::Kind::I1;
    if (tok == "str" || tok == "string")
        return Type::Kind::Str;
    if (tok == "obj" || tok == "ptr")
        return Type::Kind::Ptr;
    if (tok == "void")
        return Type::Kind::Void;
    // default conservative
    return Type::Kind::I64;
}

std::string toLower(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

} // namespace

TEST(RuntimeClassCatalogTargets, AllTargetsResolveAndMatchArity)
{
    const auto &reg = il::runtime::runtimeRegistry();
    std::map<std::string, const RuntimeDescriptor *> map;
    for (const auto &d : reg)
        map.emplace(std::string(d.name), &d);

    std::vector<std::string> errors;

    const auto &classes = il::runtime::runtimeClassCatalog();
    for (const auto &c : classes)
    {
        const std::string qname = c.qname ? c.qname : "";
        const bool isString = toLower(qname) == "viper.string";
        auto checkReceiverKind = [&](const RuntimeDescriptor &desc)
        {
            if (desc.signature.paramTypes.empty())
                return; // don't over-report
            auto got = desc.signature.paramTypes[0].kind;
            auto want = isString ? Type::Kind::Str : Type::Kind::Ptr;
            if (got != want)
            {
                std::ostringstream os;
                os << "receiver type mismatch for '" << desc.name << "': got "
                   << il::core::kindToString(got) << ", want " << il::core::kindToString(want);
                errors.push_back(os.str());
            }
        };

        // Properties
        for (const auto &p : c.properties)
        {
            if (p.getter)
            {
                auto it = map.find(p.getter);
                if (it == map.end())
                {
                    errors.push_back("missing descriptor for getter: " + std::string(p.getter));
                }
                else
                {
                    const auto *d = it->second;
                    // expect 1 param (receiver)
                    if (d->signature.paramTypes.size() != 1)
                    {
                        std::ostringstream os;
                        os << "getter arity mismatch for '" << p.getter << "': got "
                           << d->signature.paramTypes.size() << ", want 1";
                        errors.push_back(os.str());
                    }
                    else
                    {
                        checkReceiverKind(*d);
                    }
                }
            }
            if (p.setter)
            {
                auto it = map.find(p.setter);
                if (it == map.end())
                {
                    errors.push_back("missing descriptor for setter: " + std::string(p.setter));
                }
                else
                {
                    const auto *d = it->second;
                    // expect 2 params (receiver, value)
                    if (d->signature.paramTypes.size() != 2)
                    {
                        std::ostringstream os;
                        os << "setter arity mismatch for '" << p.setter << "': got "
                           << d->signature.paramTypes.size() << ", want 2";
                        errors.push_back(os.str());
                    }
                    else
                    {
                        checkReceiverKind(*d);
                    }
                }
            }
        }

        // Methods
        for (const auto &m : c.methods)
        {
            if (!m.target)
                continue;
            auto it = map.find(m.target);
            if (it == map.end())
            {
                errors.push_back("missing descriptor for method: " + std::string(m.target));
                continue;
            }
            const auto *d = it->second;
            auto args =
                parseArgs(m.signature ? std::string_view(m.signature) : std::string_view(""));
            const size_t expectedParams = 1 + args.size();
            if (d->signature.paramTypes.size() != expectedParams)
            {
                std::ostringstream os;
                os << "method arity mismatch for '" << m.target << "': got "
                   << d->signature.paramTypes.size() << ", want " << expectedParams;
                errors.push_back(os.str());
            }
            else
            {
                checkReceiverKind(*d);
                // Optional: verify arg kinds beyond receiver
                for (size_t i = 0; i < args.size(); ++i)
                {
                    Type::Kind want = mapTokenToKind(args[i]);
                    Type::Kind got = d->signature.paramTypes[1 + i].kind;
                    if (want != got)
                    {
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

    if (!errors.empty())
    {
        std::ostringstream os;
        os << "Runtime class catalog target check failed (" << errors.size() << "):\n";
        for (const auto &e : errors)
            os << "  - " << e << "\n";
        std::cerr << os.str();
        EXPECT_TRUE(errors.empty());
    }
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}

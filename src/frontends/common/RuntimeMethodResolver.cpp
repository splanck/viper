//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/common/RuntimeMethodResolver.cpp
// Purpose: Implement shared runtime method lookup and overload resolution.
// Key invariants: Runtime method metadata is read from RuntimeRegistry only;
//                 no frontend keeps a parallel parsed-signature catalog.
// Ownership/Lifetime: Lookup results own converted strings and vectors, while
//                     raw catalog data remains owned by RuntimeRegistry.
// Links: src/frontends/common/RuntimeMethodResolver.hpp,
//        src/il/runtime/classes/RuntimeClasses.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/common/RuntimeMethodResolver.hpp"

#include "frontends/common/StringUtils.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

#include <limits>
#include <optional>
#include <utility>

namespace il::frontends::common {
namespace {

/**
 * @brief Build a frontend-neutral method descriptor from a parsed runtime method.
 *
 * @details This function normalizes legacy target names by checking whether the
 * parsed target exists as-is and falling back to `class.method` when the
 * catalog stores a short target. It also derives receiver expectations from the
 * runtime descriptor's physical parameter count.
 *
 * @param classQName Runtime class that owns the method.
 * @param method Method name from the runtime catalog.
 * @param parsed Parsed method signature from RuntimeRegistry.
 * @return Fully populated method descriptor.
 */
RuntimeMethodInfo makeRuntimeMethodInfo(std::string_view classQName,
                                        std::string_view method,
                                        const il::runtime::ParsedMethod &parsed) {
    RuntimeMethodInfo info;

    info.target = parsed.target ? parsed.target : "";
    if (!info.target.empty() && !il::runtime::findRuntimeDescriptor(info.target)) {
        std::string qualifiedTarget = std::string(classQName) + "." + std::string(method);
        if (il::runtime::findRuntimeDescriptor(qualifiedTarget)) {
            info.target = std::move(qualifiedTarget);
        }
    }

    info.ret = parsed.signature.returnType;
    info.returnClassQName = il::runtime::concreteRuntimeReturnClassQName(parsed.signature);
    info.rawPointerReturn = parsed.signature.rawPointerReturn;
    info.rawPointerParams = parsed.signature.rawPointerParams;
    info.args = parsed.signature.params;

    if (const auto *descriptor = il::runtime::findRuntimeDescriptor(info.target)) {
        const auto runtimeParamCount = descriptor->signature.paramTypes.size();
        const auto explicitParamCount = parsed.signature.params.size();
        if (runtimeParamCount == explicitParamCount) {
            info.hasReceiver = false;
        } else if (runtimeParamCount == explicitParamCount + 1U) {
            info.hasReceiver = true;
        }
    }

    return info;
}

/**
 * @brief Score one actual argument type against one expected runtime parameter.
 *
 * @details Lower scores are better. A missing optional return indicates the
 * argument cannot be passed to that parameter without violating existing
 * frontend compatibility rules.
 *
 * @param expected Runtime parameter type.
 * @param actual Source argument type after frontend conversion to IL terms.
 * @return Compatibility score, or std::nullopt for an incompatible pair.
 */
std::optional<int> scoreArgMatch(il::runtime::ILScalarType expected,
                                 il::runtime::ILScalarType actual) {
    if (expected == actual) {
        return 0;
    }
    if (expected == il::runtime::ILScalarType::Object) {
        if (actual == il::runtime::ILScalarType::Void) {
            return std::nullopt;
        }
        return actual == il::runtime::ILScalarType::Unknown ? 2 : 1;
    }
    if (actual == il::runtime::ILScalarType::Unknown) {
        return 2;
    }
    if (expected == il::runtime::ILScalarType::F64 && actual == il::runtime::ILScalarType::I64) {
        return 1;
    }
    if (expected == il::runtime::ILScalarType::Bool && actual == il::runtime::ILScalarType::I64) {
        return 1;
    }
    return std::nullopt;
}

/**
 * @brief Score a complete runtime signature against actual argument types.
 *
 * @details The signature must have the same explicit arity as the call. The
 * total score is the sum of parameter compatibility scores.
 *
 * @param expected Runtime parameter types.
 * @param actual Source argument types converted to IL terms.
 * @return Total compatibility score, or std::nullopt when any argument fails.
 */
std::optional<int> scoreSignature(const std::vector<il::runtime::ILScalarType> &expected,
                                  const std::vector<il::runtime::ILScalarType> &actual) {
    if (expected.size() != actual.size()) {
        return std::nullopt;
    }

    int score = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        auto argScore = scoreArgMatch(expected[i], actual[i]);
        if (!argScore) {
            return std::nullopt;
        }
        score += *argScore;
    }
    return score;
}

} // namespace

const RuntimeMethodResolver &RuntimeMethodResolver::instance() {
    static const RuntimeMethodResolver resolver;
    return resolver;
}

std::optional<RuntimeMethodInfo> RuntimeMethodResolver::find(std::string_view classQName,
                                                             std::string_view method,
                                                             std::size_t arity) const {
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    auto parsed = registry.findMethod(classQName, method, arity);
    if (!parsed) {
        return std::nullopt;
    }
    return makeRuntimeMethodInfo(classQName, method, *parsed);
}

std::optional<RuntimeMethodInfo> RuntimeMethodResolver::find(
    std::string_view classQName,
    std::string_view method,
    const std::vector<il::runtime::ILScalarType> &argTypes) const {
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    std::optional<RuntimeMethodInfo> best;
    int bestScore = std::numeric_limits<int>::max();
    bool ambiguous = false;

    for (const auto &klass : registry.rawCatalog()) {
        if (!klass.qname || !string_utils::iequals(klass.qname, classQName)) {
            continue;
        }

        for (const auto &candidate : klass.methods) {
            if (!candidate.name || !string_utils::iequals(candidate.name, method)) {
                continue;
            }

            auto sig =
                il::runtime::parseRuntimeSignature(candidate.signature ? candidate.signature : "");
            if (!sig.isValid() || sig.params.size() != argTypes.size()) {
                continue;
            }

            il::runtime::ParsedMethod parsed{candidate.name, candidate.target, sig};
            RuntimeMethodInfo info = makeRuntimeMethodInfo(klass.qname, candidate.name, parsed);
            auto score = scoreSignature(info.args, argTypes);
            if (!score) {
                continue;
            }

            if (*score < bestScore) {
                best = std::move(info);
                bestScore = *score;
                ambiguous = false;
            } else if (*score == bestScore) {
                ambiguous = true;
            }
        }
    }

    if (ambiguous) {
        return std::nullopt;
    }
    return best;
}

std::vector<std::string> RuntimeMethodResolver::candidates(std::string_view classQName,
                                                           std::string_view method) const {
    return il::runtime::RuntimeRegistry::instance().methodCandidates(classQName, method);
}

} // namespace il::frontends::common

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/SymbolResolver.cpp
// Purpose: Implementation of global symbol resolution.
//          Iteratively extracts archive members until all symbols resolved.
// Key invariants:
//   - Strong > Weak > Undefined precedence
//   - Multiple strong definitions of the same symbol = linker error
//   - Archives re-scanned until fixed point (handles cross-archive deps)
// Links: codegen/common/linker/SymbolResolver.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/SymbolResolver.hpp"
#include "codegen/common/linker/DynamicSymbolPolicy.hpp"
#include "codegen/common/linker/NameMangling.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>

namespace viper::codegen::linker {

static bool preferArchiveDefinition(const std::string &name, LinkPlatform platform);
static bool isPreferredArchiveDefinitionObject(const ObjFile &obj, LinkPlatform platform);
static bool allowDuplicateStrongDefinition(const std::string &name,
                                           LinkPlatform platform,
                                           bool allowArchiveDefinitionPreference);

/// Add symbols from a single object file into the global table.
/// @param obj        The object file.
/// @param objIdx     Its index in allObjects.
/// @param globalSyms The global symbol table to update.
/// @param undefined  Set of currently undefined symbol names.
/// @param err        Error stream.
/// @return false if multiply-defined symbol error.
static bool addObjSymbols(const ObjFile &obj,
                          size_t objIdx,
                          std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                          std::unordered_set<std::string> &undefined,
                          LinkPlatform platform,
                          bool allowArchiveDefinitionPreference,
                          std::ostream &err) {
    auto eraseUndefinedVariants = [&](const std::string &name) {
        undefined.erase(name);
        if (!name.empty() && name[0] == '_')
            undefined.erase(name.substr(1));
        if (!name.empty())
            undefined.erase("_" + name);
    };

    for (size_t i = 1; i < obj.symbols.size(); ++i) {
        const auto &sym = obj.symbols[i];
        if (sym.name.empty())
            continue;

        if (sym.binding == ObjSymbol::Undefined) {
            auto addUndefined = [&](const std::string &undefName) {
                auto it = findWithMachoFallback(globalSyms, undefName);
                if (it == globalSyms.end() || it->second.binding == GlobalSymEntry::Undefined)
                    undefined.insert(undefName);
                if (it == globalSyms.end()) {
                    GlobalSymEntry e;
                    e.name = undefName;
                    e.binding = GlobalSymEntry::Undefined;
                    globalSyms[undefName] = std::move(e);
                }
            };

            if (sym.weakExternal) {
                if (!sym.weakDefaultName.empty())
                    addUndefined(sym.weakDefaultName);
                continue;
            }

            addUndefined(sym.name);
            continue;
        }

        if (sym.binding == ObjSymbol::Local)
            continue; // Locals don't participate in global resolution.

        const bool isWeak = (sym.binding == ObjSymbol::Weak);
        auto it = findWithMachoFallback(globalSyms, sym.name);
        if (it == globalSyms.end()) {
            // New symbol.
            GlobalSymEntry e;
            e.name = sym.name;
            e.binding = isWeak ? GlobalSymEntry::Weak : GlobalSymEntry::Global;
            e.objIndex = objIdx;
            e.secIndex = sym.sectionIndex;
            e.offset = sym.offset;
            e.absolute = sym.absolute;
            if (sym.absolute)
                e.resolvedAddr = static_cast<uint64_t>(sym.offset);
            globalSyms[sym.name] = std::move(e);
            eraseUndefinedVariants(sym.name);
        } else {
            auto &existing = it->second;
            if (existing.binding == GlobalSymEntry::Undefined) {
                // Was undefined, now defined.
                existing.binding = isWeak ? GlobalSymEntry::Weak : GlobalSymEntry::Global;
                existing.objIndex = objIdx;
                existing.secIndex = sym.sectionIndex;
                existing.offset = sym.offset;
                existing.absolute = sym.absolute;
                if (sym.absolute)
                    existing.resolvedAddr = static_cast<uint64_t>(sym.offset);
                eraseUndefinedVariants(sym.name);
            } else if (existing.binding == GlobalSymEntry::Weak && !isWeak) {
                // Strong overrides weak.
                existing.binding = GlobalSymEntry::Global;
                existing.objIndex = objIdx;
                existing.secIndex = sym.sectionIndex;
                existing.offset = sym.offset;
                existing.absolute = sym.absolute;
                if (sym.absolute)
                    existing.resolvedAddr = static_cast<uint64_t>(sym.offset);
            } else if (existing.binding == GlobalSymEntry::Global && !isWeak) {
                if (allowDuplicateStrongDefinition(
                        sym.name, platform, allowArchiveDefinitionPreference))
                    continue;
                err << "error: multiply defined symbol '" << sym.name << "' in " << obj.name
                    << "\n";
                return false;
            }
            // Weak doesn't override anything.
        }
    }
    return true;
}

/// Runtime archives provide Windows compatibility shims for a small set of
/// formatting functions. Those definitions must participate in archive
/// resolution instead of being forced down the dynamic-import path.
static bool preferArchiveDefinition(const std::string &name, LinkPlatform platform) {
    if (platform != LinkPlatform::Windows)
        return false;

    if (name == "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9" ||
        name == "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9")
        return false;

    if (name == "fprintf" || name == "snprintf" || name == "vsnprintf" ||
        name == "_vfprintf_l" || name == "_vfscanf_l" || name == "_vsprintf_l" ||
        name == "_vsnprintf_l" || name == "_vswprintf_l" || name == "_vfwprintf_l" ||
        name == "fstat" || name == "_fstat64i32" || name == "stat" || name == "_stat64i32" ||
        name == "mainCRTStartup" || name == "WinMainCRTStartup" || name == "wmainCRTStartup" ||
        name == "wWinMainCRTStartup" || name == "__security_check_cookie" ||
        name == "__security_init_cookie" || name == "__GSHandlerCheck" || name == "__chkstk")
        return true;

    return name.find("__scrt_") != std::string::npos ||
           name.find("__local_stdio_printf_options") != std::string::npos ||
           name.find("__local_stdio_scanf_options") != std::string::npos ||
           name.rfind("__xi_", 0) == 0 || name.rfind("__xc_", 0) == 0 ||
           name.rfind("__xl_", 0) == 0 || name.rfind("__dyn_tls_", 0) == 0 ||
           name.rfind("__tls_", 0) == 0;
}

static bool isPreferredArchiveDefinitionObject(const ObjFile &obj, LinkPlatform platform) {
    if (platform != LinkPlatform::Windows)
        return false;

    // Keep the Windows CRT/runtime shim exception scoped to Viper's own runtime
    // archives. User archives that accidentally define the same names should
    // still participate in normal multiple-definition diagnostics.
    return obj.name.find("viper_rt") != std::string::npos ||
           obj.name.find("viper-runtime") != std::string::npos ||
           obj.name.find("viper_runtime") != std::string::npos;
}

/// MSVC emits some CRT inline-function local statics in every object that uses
/// the inline helper. Link.exe picks one definition; keep normal user duplicate
/// strong definitions strict while modeling that pick-any behavior.
static bool allowDuplicateStrongDefinition(const std::string &name,
                                           LinkPlatform platform,
                                           bool allowArchiveDefinitionPreference) {
    if (platform == LinkPlatform::Windows && isWindowsStdioOptionsStorageSymbol(name))
        return true;
    return allowArchiveDefinitionPreference && preferArchiveDefinition(name, platform);
}

bool resolveSymbols(const std::vector<ObjFile> &initialObjects,
                    std::vector<Archive> &archives,
                    std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                    std::vector<ObjFile> &allObjects,
                    std::unordered_set<std::string> &dynamicSyms,
                    std::ostream &err,
                    LinkPlatform platform) {
    // Start with initial objects.
    allObjects = initialObjects;
    std::unordered_set<std::string> undefined;

    for (size_t i = 0; i < allObjects.size(); ++i) {
        if (!addObjSymbols(allObjects[i], i, globalSyms, undefined, platform, false, err))
            return false;
    }

    // Iteratively resolve from archives until fixed point.
    std::unordered_set<InputSectionKey, InputSectionKeyHash> extractedMembers;
    constexpr size_t kMaxResolveIterations = 1000;
    size_t iteration = 0;
    bool changed = true;
    while (changed) {
        if (++iteration > kMaxResolveIterations) {
            err << "error: symbol resolution exceeded " << kMaxResolveIterations << " iterations\n";
            return false;
        }
        changed = false;
        // Snapshot undefined set — addObjSymbols modifies it, invalidating iterators.
        std::vector<std::string> undefSnapshot(undefined.begin(), undefined.end());
        for (size_t ai = 0; ai < archives.size(); ++ai) {
            auto &ar = archives[ai];
            for (const auto &undef : undefSnapshot) {
                if (undefined.find(undef) == undefined.end())
                    continue;
                // Mach-O archives use underscore-prefixed symbol names.
                auto candIt = findWithMachoFallback(ar.symbolCandidates, undef);
                std::vector<size_t> legacyCandidate;
                const std::vector<size_t> *candidates = nullptr;
                if (candIt != ar.symbolCandidates.end()) {
                    candidates = &candIt->second;
                } else {
                    auto symIt = findWithMachoFallback(ar.symbolIndex, undef);
                    if (symIt == ar.symbolIndex.end())
                        continue;
                    legacyCandidate.push_back(symIt->second);
                    candidates = &legacyCandidate;
                }
                if (candidates == nullptr || candidates->empty())
                    continue;

                for (size_t memberIdx : *candidates) {
                    if (memberIdx >= ar.members.size()) {
                        err << "error: archive symbol index references missing member "
                            << memberIdx << "\n";
                        return false;
                    }

                    InputSectionKey key{ai, memberIdx};
                    if (extractedMembers.count(key))
                        continue;

                    // Extract and parse this member.
                    extractedMembers.insert(key);
                    auto memberData = memberDataView(ar, ar.members[memberIdx]);
                    if (memberData.data == nullptr || memberData.size == 0)
                        continue;

                    ObjFile memberObj;
                    std::ostringstream memberErr;
                    if (!readObjFile(memberData.data,
                                     memberData.size,
                                     ar.path + "(" + ar.members[memberIdx].name + ")",
                                     memberObj,
                                     memberErr)) {
                        err << memberErr.str();
                        return false;
                    }

                    size_t newIdx = allObjects.size();
                    allObjects.push_back(std::move(memberObj));
                    const bool allowArchiveDefinitionPreference =
                        isPreferredArchiveDefinitionObject(allObjects[newIdx], platform);
                    if (!addObjSymbols(allObjects[newIdx],
                                       newIdx,
                                       globalSyms,
                                       undefined,
                                       platform,
                                       allowArchiveDefinitionPreference,
                                       err))
                        return false;
                    changed = true;
                    break;
                }
            }
        }
    }

    // Mark remaining undefined as dynamic only when the symbol is explicitly
    // allowlisted as a shared-library import on this platform.
    // Unknown unresolveds remain hard link errors so we do not silently emit
    // binaries that fail at load time due to missing symbols.
    std::vector<std::string> unresolvedErrors;
    for (const auto &undef : undefined) {
        auto it = globalSyms.find(undef);
        if (it != globalSyms.end() && it->second.binding != GlobalSymEntry::Undefined)
            continue; // Was resolved during iteration.

        const bool allowSynthetic =
            platform == LinkPlatform::Windows && isWindowsLinkerHelperSymbol(undef);
        const bool allowDynamic =
            allowSynthetic ||
            (isKnownDynamicSymbol(undef, platform) && !preferArchiveDefinition(undef, platform));
        if (!allowDynamic) {
            unresolvedErrors.push_back(undef);
            continue;
        }

        dynamicSyms.insert(undef);
        if (it != globalSyms.end())
            it->second.binding = GlobalSymEntry::Dynamic;
    }

    if (!unresolvedErrors.empty()) {
        std::sort(unresolvedErrors.begin(), unresolvedErrors.end());
        unresolvedErrors.erase(
            std::unique(unresolvedErrors.begin(), unresolvedErrors.end()), unresolvedErrors.end());
        for (const auto &name : unresolvedErrors) {
            err << "error: undefined symbol '" << name << "'";
            if (preferArchiveDefinition(name, platform))
                err << " (expected a static/archive definition)";
            err << "\n";
        }
        return false;
    }

    return true;
}

} // namespace viper::codegen::linker

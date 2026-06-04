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
#include <cctype>
#include <cstdint>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace viper::codegen::linker {

static bool preferArchiveDefinition(const std::string &name, LinkPlatform platform);
static bool isPreferredArchiveDefinitionObject(const ObjFile &obj, LinkPlatform platform);
static bool allowDuplicateStrongDefinition(const std::string &name,
                                           LinkPlatform platform,
                                           bool allowArchiveDefinitionPreference);
static void synthesizeWindowsThreadSafeStaticGuards(
    std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
    std::unordered_set<std::string> &undefined);
static bool materializeCommonSymbols(std::vector<ObjFile> &allObjects,
                                     std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                                     std::ostream &err);

struct ComdatDefinition {
    ComdatSelection selection = ComdatSelection::None;
    std::string key;
    size_t objIdx = 0;
    uint32_t secIdx = 0;
    size_t size = 0;
    uint64_t hash = 0;
};

static bool isSupportedCommonAlignment(size_t alignment) {
    return alignment != 0 && (alignment & (alignment - 1)) == 0 &&
           alignment <= std::numeric_limits<uint32_t>::max();
}

/// @brief Return an ASCII-lowercased copy of @p value for Windows archive-name checks.
static std::string asciiLower(std::string value) {
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

/// @brief Extract the archive path component from an object display name.
/// @details Archive members are named as "path/to/archive.lib(member.obj)".
///          Non-archive objects are returned unchanged, which lets the caller
///          reject them by basename policy.
static std::string archivePathFromObjectName(const std::string &objectName) {
    const size_t memberStart = objectName.find('(');
    return memberStart == std::string::npos ? objectName : objectName.substr(0, memberStart);
}

/// @brief Extract the final path component from @p path using both host separators.
static std::string basenameFromPath(const std::string &path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/// @brief Check whether an archive basename is one of Viper's runtime archives.
/// @details This intentionally does not search the whole object path. Only a
///          basename such as libviper_rt_base.a, viper_rt_core.lib, or
///          viper_runtime.lib is allowed to activate duplicate-runtime shim
///          preference.
static bool isViperRuntimeArchiveBasename(const std::string &basename) {
    std::string name = asciiLower(basename);
    if (name.size() > 4 && name.substr(name.size() - 4) == ".lib")
        name.resize(name.size() - 4);
    else if (name.size() > 2 && name.substr(name.size() - 2) == ".a")
        name.resize(name.size() - 2);
    if (name.rfind("lib", 0) == 0)
        name.erase(0, 3);

    return name == "viper_runtime" || name.rfind("viper_rt_", 0) == 0 ||
           name.rfind("viper-runtime", 0) == 0;
}

static void setEntryFromSymbol(GlobalSymEntry &entry,
                               const ObjSymbol &sym,
                               size_t objIdx,
                               GlobalSymEntry::Binding binding) {
    entry.binding = binding;
    entry.objIndex = objIdx;
    entry.secIndex = sym.sectionIndex;
    entry.offset = sym.offset;
    entry.resolvedAddr = sym.absolute ? static_cast<uint64_t>(sym.offset) : 0;
    entry.resolvedAddrValid = sym.absolute;
    entry.absolute = sym.absolute;
    entry.common = false;
    entry.commonSize = 0;
    entry.commonAlignment = 1;
}

static void setEntryFromCommon(GlobalSymEntry &entry,
                               const ObjSymbol &sym,
                               size_t objIdx,
                               GlobalSymEntry::Binding binding) {
    entry.binding = binding;
    entry.objIndex = objIdx;
    entry.secIndex = 0;
    entry.offset = 0;
    entry.resolvedAddr = 0;
    entry.resolvedAddrValid = false;
    entry.absolute = false;
    entry.common = true;
    entry.commonSize = std::max(entry.commonSize, sym.size);
    entry.commonAlignment = std::max(entry.commonAlignment, sym.commonAlignment);
}

static uint64_t hashBytes(const std::vector<uint8_t> &bytes) {
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t b : bytes) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}

static void hashU64(uint64_t value, uint64_t &h) {
    for (unsigned i = 0; i < 8; ++i) {
        h ^= static_cast<uint8_t>(value >> (i * 8));
        h *= 1099511628211ULL;
    }
}

static void hashString(const std::string &value, uint64_t &h) {
    hashU64(value.size(), h);
    for (unsigned char ch : value) {
        h ^= ch;
        h *= 1099511628211ULL;
    }
}

static void hashRelocTarget(const ObjFile &obj, const ObjReloc &rel, uint64_t &h) {
    if (rel.symIndex >= obj.symbols.size()) {
        hashU64(rel.symIndex, h);
        return;
    }

    const auto &sym = obj.symbols[rel.symIndex];
    hashString(sym.name, h);
    hashU64(sym.binding, h);
    hashU64(sym.offset, h);
    hashU64(sym.size, h);
    hashU64(sym.absolute ? 1 : 0, h);
    hashU64(sym.common ? 1 : 0, h);
    hashU64(sym.commonAlignment, h);

    if (sym.sectionIndex == 0 || sym.absolute || sym.common) {
        hashU64(sym.sectionIndex, h);
        return;
    }
    if (sym.sectionIndex >= obj.sections.size()) {
        hashU64(sym.sectionIndex, h);
        return;
    }

    const auto &targetSec = obj.sections[sym.sectionIndex];
    hashString(targetSec.name, h);
    hashString(targetSec.comdatKey, h);
    hashU64(static_cast<uint64_t>(targetSec.comdatSelection), h);
    hashU64(targetSec.associativeSection, h);
}

static uint64_t hashComdatSection(const ObjFile &obj, const ObjSection &sec) {
    uint64_t h = hashBytes(sec.data);
    hashU64(objSectionMemSize(sec), h);
    hashU64(sec.memSize, h);
    hashU64(sec.alignment, h);
    hashU64(sec.executable ? 1 : 0, h);
    hashU64(sec.writable ? 1 : 0, h);
    hashU64(sec.alloc ? 1 : 0, h);
    hashU64(sec.tls ? 1 : 0, h);
    hashU64(sec.zeroFill ? 1 : 0, h);
    hashU64(sec.dataSegment ? 1 : 0, h);
    hashU64(sec.associativeSection, h);
    hashU64(sec.relocs.size(), h);
    for (const auto &rel : sec.relocs) {
        hashU64(rel.offset, h);
        hashU64(rel.type, h);
        hashU64(static_cast<uint64_t>(rel.addend), h);
        hashU64(rel.pcrel ? 1 : 0, h);
        hashU64(rel.length, h);
        hashU64(rel.sectionRelative ? 1 : 0, h);
        hashRelocTarget(obj, rel, h);
    }
    return h;
}

static bool weakExternalSearchesFallbackLibrary(const ObjSymbol &sym) {
    // COFF values: 1=NOLIBRARY, 2=LIBRARY, 3=ALIAS. ALIAS must also seed
    // archive extraction; otherwise an archive-only fallback is never loaded.
    // Viper-created tests and older readers used 0; keep that legacy value as
    // "search fallback".
    if (!sym.weakExternal || sym.weakDefaultName.empty())
        return false;
    return sym.weakExternalCharacteristics == 0 || sym.weakExternalCharacteristics == 2 ||
           sym.weakExternalCharacteristics == 3;
}

static bool isMsvcStdComparisonCategoryInlineSymbol(const std::string &name) {
    const bool isComparisonConstant =
        name.rfind("?less@", 0) == 0 || name.rfind("?equal@", 0) == 0 ||
        name.rfind("?equivalent@", 0) == 0 || name.rfind("?greater@", 0) == 0 ||
        name.rfind("?unordered@", 0) == 0;
    if (!isComparisonConstant)
        return false;

    return name.find("@strong_ordering@std@@2U") != std::string::npos ||
           name.find("@weak_ordering@std@@2U") != std::string::npos ||
           name.find("@partial_ordering@std@@2U") != std::string::npos;
}

static bool isMsvcStdTypeInfoOrVftableSymbol(const std::string &name) {
    const bool isTypeInfoOrVftable = name.rfind("??_7", 0) == 0 || name.rfind("??_R", 0) == 0;
    return isTypeInfoOrVftable && name.find("@std@@") != std::string::npos;
}

static bool isMsvcDecoratedStringLiteral(const std::string &name) {
    return name.rfind("??_C@", 0) == 0;
}

static bool isMsvcStdExceptionMetadataSymbol(const std::string &name) {
    const bool isExceptionMetadata = name.rfind("_CT", 0) == 0 || name.rfind("_TI", 0) == 0;
    return isExceptionMetadata && name.find("@std@@") != std::string::npos;
}

static bool isMsvcStdInlineConstDataSymbol(const std::string &name) {
    return name.rfind("?", 0) == 0 && name.find("@std@@3") != std::string::npos &&
           name.size() >= 2 && name.compare(name.size() - 2, 2, "@B") == 0;
}

static bool isMsvcGeneratedConstantSymbol(const std::string &name) {
    return name.rfind("__real@", 0) == 0 || name.rfind("__xmm@", 0) == 0 ||
           name.rfind("__ymm@", 0) == 0;
}

static bool getComdatDefinition(const ObjFile &obj,
                                size_t objIdx,
                                const ObjSymbol &sym,
                                ComdatDefinition &out) {
    if (sym.sectionIndex == 0 || sym.sectionIndex >= obj.sections.size())
        return false;
    const auto &sec = obj.sections[sym.sectionIndex];
    if (sec.comdatSelection == ComdatSelection::None ||
        sec.comdatSelection == ComdatSelection::Associative || sec.comdatKey.empty())
        return false;
    out.selection = sec.comdatSelection;
    out.key = sec.comdatKey;
    out.objIdx = objIdx;
    out.secIdx = sym.sectionIndex;
    out.size = objSectionMemSize(sec);
    out.hash = sec.comdatSelection == ComdatSelection::ExactMatch ? hashComdatSection(obj, sec) : 0;
    return true;
}

static bool selectComdatDuplicate(const ObjFile &obj,
                                  const ObjSymbol &sym,
                                  size_t objIdx,
                                  const ComdatDefinition &existing,
                                  const ComdatDefinition &candidate,
                                  GlobalSymEntry &entry,
                                  std::ostream &err) {
    if (existing.key != candidate.key) {
        if (existing.selection == ComdatSelection::Any &&
            candidate.selection == ComdatSelection::Any)
            return true;
        err << "error: multiply defined symbol '" << sym.name << "' has mismatched COMDAT keys\n";
        return false;
    }

    const ComdatSelection selection =
        existing.selection != ComdatSelection::None ? existing.selection : candidate.selection;
    switch (selection) {
        case ComdatSelection::Any:
            return true;
        case ComdatSelection::SameSize:
            if (existing.size == candidate.size)
                return true;
            err << "error: COMDAT SAME_SIZE symbol '" << sym.name
                << "' has different section sizes\n";
            return false;
        case ComdatSelection::ExactMatch:
            if (existing.size == candidate.size && existing.hash == candidate.hash)
                return true;
            err << "error: COMDAT EXACT_MATCH symbol '" << sym.name << "' has different contents\n";
            return false;
        case ComdatSelection::Largest:
            if (candidate.size > existing.size)
                setEntryFromSymbol(entry, sym, objIdx, GlobalSymEntry::Global);
            return true;
        case ComdatSelection::NoDuplicates:
            err << "error: COMDAT NODUPLICATES symbol '" << sym.name
                << "' is defined more than once\n";
            return false;
        case ComdatSelection::None:
        case ComdatSelection::Associative:
            err << "error: multiply defined symbol '" << sym.name << "' in " << obj.name << "\n";
            return false;
    }
    return false;
}

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
                          std::unordered_map<std::string, ComdatDefinition> &comdatDefs,
                          std::unordered_set<std::string> &undefined,
                          LinkPlatform platform,
                          bool allowArchiveDefinitionPreference,
                          std::vector<std::string> *newUndefined,
                          std::ostream &err) {
    auto eraseUndefinedVariants = [&](const std::string &name) {
        undefined.erase(name);
        if (platform == LinkPlatform::macOS) {
            if (!name.empty() && name[0] == '_')
                undefined.erase(name.substr(1));
            if (!name.empty())
                undefined.erase("_" + name);
        }
    };

    for (size_t i = 1; i < obj.symbols.size(); ++i) {
        const auto &sym = obj.symbols[i];
        if (sym.name.empty())
            continue;

        if (sym.binding == ObjSymbol::Undefined) {
            auto addUndefined = [&](const std::string &undefName) {
                auto it = findWithPlatformFallback(globalSyms, undefName, platform);
                if (it == globalSyms.end() || it->second.binding == GlobalSymEntry::Undefined) {
                    const bool inserted = undefined.insert(undefName).second;
                    if (inserted && newUndefined)
                        newUndefined->push_back(undefName);
                }
                if (it == globalSyms.end()) {
                    GlobalSymEntry e;
                    e.name = undefName;
                    e.binding = GlobalSymEntry::Undefined;
                    globalSyms[undefName] = std::move(e);
                }
            };

            if (sym.weakExternal) {
                if (weakExternalSearchesFallbackLibrary(sym))
                    addUndefined(sym.weakDefaultName);
                continue;
            }

            addUndefined(sym.name);
            continue;
        }

        if (sym.binding == ObjSymbol::Local)
            continue; // Locals don't participate in global resolution.

        const bool isWeak = (sym.binding == ObjSymbol::Weak);
        if (sym.common && !isSupportedCommonAlignment(sym.commonAlignment)) {
            err << "error: common symbol '" << sym.name << "' has unsupported alignment\n";
            return false;
        }

        auto it = findWithPlatformFallback(globalSyms, sym.name, platform);
        ComdatDefinition candidateComdat;
        const bool hasCandidateComdat = getComdatDefinition(obj, objIdx, sym, candidateComdat);
        if (it == globalSyms.end()) {
            // New symbol.
            GlobalSymEntry e;
            e.name = sym.name;
            if (sym.common) {
                setEntryFromCommon(
                    e, sym, objIdx, isWeak ? GlobalSymEntry::Weak : GlobalSymEntry::Global);
            } else {
                setEntryFromSymbol(
                    e, sym, objIdx, isWeak ? GlobalSymEntry::Weak : GlobalSymEntry::Global);
            }
            globalSyms[sym.name] = std::move(e);
            if (hasCandidateComdat)
                comdatDefs[sym.name] = candidateComdat;
            else
                comdatDefs.erase(sym.name);
            eraseUndefinedVariants(sym.name);
        } else {
            auto &existing = it->second;
            if (existing.binding == GlobalSymEntry::Undefined) {
                // Was undefined, now defined.
                if (sym.common) {
                    setEntryFromCommon(existing,
                                       sym,
                                       objIdx,
                                       isWeak ? GlobalSymEntry::Weak : GlobalSymEntry::Global);
                } else {
                    setEntryFromSymbol(existing,
                                       sym,
                                       objIdx,
                                       isWeak ? GlobalSymEntry::Weak : GlobalSymEntry::Global);
                }
                if (hasCandidateComdat)
                    comdatDefs[sym.name] = candidateComdat;
                else
                    comdatDefs.erase(sym.name);
                eraseUndefinedVariants(sym.name);
            } else if (sym.common) {
                if (existing.common) {
                    existing.commonSize = std::max(existing.commonSize, sym.size);
                    existing.commonAlignment =
                        std::max(existing.commonAlignment, sym.commonAlignment);
                    if (!isWeak && existing.binding == GlobalSymEntry::Weak) {
                        existing.binding = GlobalSymEntry::Global;
                        existing.objIndex = objIdx;
                    }
                    comdatDefs.erase(sym.name);
                    eraseUndefinedVariants(sym.name);
                } else if (existing.binding == GlobalSymEntry::Weak && !isWeak) {
                    // A strong common definition overrides a weak real definition.
                    setEntryFromCommon(existing, sym, objIdx, GlobalSymEntry::Global);
                    comdatDefs.erase(sym.name);
                    eraseUndefinedVariants(sym.name);
                }
                // Existing strong real definitions override tentative definitions.
            } else if (existing.common) {
                if (!isWeak) {
                    // A strong real definition overrides any tentative definition.
                    setEntryFromSymbol(existing, sym, objIdx, GlobalSymEntry::Global);
                    if (hasCandidateComdat)
                        comdatDefs[sym.name] = candidateComdat;
                    else
                        comdatDefs.erase(sym.name);
                    eraseUndefinedVariants(sym.name);
                }
                // Weak real definitions do not override common definitions.
            } else if (existing.binding == GlobalSymEntry::Weak && !isWeak) {
                // Strong overrides weak.
                setEntryFromSymbol(existing, sym, objIdx, GlobalSymEntry::Global);
                if (hasCandidateComdat)
                    comdatDefs[sym.name] = candidateComdat;
                else
                    comdatDefs.erase(sym.name);
                eraseUndefinedVariants(sym.name);
            } else if (existing.binding == GlobalSymEntry::Global && !isWeak) {
                auto comdatIt = findWithPlatformFallback(comdatDefs, sym.name, platform);
                if (hasCandidateComdat && comdatIt != comdatDefs.end()) {
                    if (!selectComdatDuplicate(
                            obj, sym, objIdx, comdatIt->second, candidateComdat, existing, err))
                        return false;
                    if (candidateComdat.selection == ComdatSelection::Largest &&
                        candidateComdat.size > comdatIt->second.size)
                        comdatIt->second = candidateComdat;
                    eraseUndefinedVariants(sym.name);
                    continue;
                }
                if (allowDuplicateStrongDefinition(
                        sym.name, platform, allowArchiveDefinitionPreference))
                    continue;
                err << "error: multiply defined symbol '" << sym.name << "' in " << obj.name
                    << "\n";
                return false;
            } else {
                // Weak doesn't override anything.
            }
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

    // The Zia completion bridge (rt_zia_*) ships a non-weak stub in
    // viper_rt_base on MSVC (RT_WEAK expands to nothing). When the fe_zia
    // frontend is force-loaded its strong definitions are added first; treat a
    // later duplicate strong stub definition as the same "runtime archive
    // provides an overridable shim" case the CRT names below already use, so it
    // is preferred-away rather than reported as multiply defined.
    if (name.rfind("rt_zia_", 0) == 0)
        return true;

    if (name == "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9" ||
        name == "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9")
        return false;

    if (name == "fprintf" || name == "snprintf" || name == "vsnprintf" || name == "_vfprintf_l" ||
        name == "_vfscanf_l" || name == "_vsprintf_l" || name == "_vsnprintf_l" ||
        name == "_vswprintf_l" || name == "_vfwprintf_l" || name == "fstat" ||
        name == "_fstat64i32" || name == "stat" || name == "_stat64i32" ||
        name == "mainCRTStartup" || name == "WinMainCRTStartup" || name == "wmainCRTStartup" ||
        name == "wWinMainCRTStartup" || name == "__security_check_cookie" ||
        name == "__security_init_cookie" || name == "__GSHandlerCheck" ||
        name == "__GSHandlerCheck_EH4" || name == "__chkstk")
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
    return isViperRuntimeArchiveBasename(basenameFromPath(archivePathFromObjectName(obj.name)));
}

/// MSVC emits some CRT inline-function local statics in every object that uses
/// the inline helper. Link.exe picks one definition; keep normal user duplicate
/// strong definitions strict while modeling that pick-any behavior.
static bool allowDuplicateStrongDefinition(const std::string &name,
                                           LinkPlatform platform,
                                           bool allowArchiveDefinitionPreference) {
    if (platform == LinkPlatform::Windows) {
        if (isWindowsStdioOptionsStorageSymbol(name))
            return true;
        if (isMsvcStdComparisonCategoryInlineSymbol(name))
            return true;
        if (isMsvcStdTypeInfoOrVftableSymbol(name))
            return true;
        if (isMsvcDecoratedStringLiteral(name))
            return true;
        if (isMsvcStdExceptionMetadataSymbol(name))
            return true;
        if (isMsvcStdInlineConstDataSymbol(name))
            return true;
        if (isMsvcGeneratedConstantSymbol(name))
            return true;
    }
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
    std::unordered_map<std::string, ComdatDefinition> comdatDefs;
    std::vector<std::string> pendingUndefined;
    size_t pendingIndex = 0;

    for (size_t i = 0; i < allObjects.size(); ++i) {
        if (!addObjSymbols(allObjects[i],
                           i,
                           globalSyms,
                           comdatDefs,
                           undefined,
                           platform,
                           false,
                           &pendingUndefined,
                           err))
            return false;
    }

    // Iteratively resolve from archives until fixed point.
    std::unordered_set<InputSectionKey, InputSectionKeyHash> extractedMembers;
    size_t maxArchiveExtractions = 0;
    for (const auto &ar : archives) {
        if (ar.members.size() > std::numeric_limits<size_t>::max() - maxArchiveExtractions) {
            err << "error: archive member count exceeds addressable size\n";
            return false;
        }
        maxArchiveExtractions += ar.members.size();
    }
    size_t extractedCount = 0;
    while (pendingIndex < pendingUndefined.size()) {
        const std::string undef = pendingUndefined[pendingIndex++];
        if (undefined.find(undef) == undefined.end())
            continue;

        bool extractedForUndef = false;
        for (size_t ai = 0; ai < archives.size(); ++ai) {
            auto &ar = archives[ai];
            // Mach-O archives use underscore-prefixed symbol names.
            auto candIt = findWithPlatformFallback(ar.symbolCandidates, undef, platform);
            std::vector<size_t> legacyCandidate;
            const std::vector<size_t> *candidates = nullptr;
            if (candIt != ar.symbolCandidates.end()) {
                candidates = &candIt->second;
            } else {
                auto symIt = findWithPlatformFallback(ar.symbolIndex, undef, platform);
                if (symIt == ar.symbolIndex.end())
                    continue;
                legacyCandidate.push_back(symIt->second);
                candidates = &legacyCandidate;
            }
            if (candidates == nullptr || candidates->empty())
                continue;

            for (size_t memberIdx : *candidates) {
                if (memberIdx >= ar.members.size()) {
                    err << "error: archive symbol index references missing member " << memberIdx
                        << "\n";
                    return false;
                }

                InputSectionKey key{ai, memberIdx};
                if (extractedMembers.count(key))
                    continue;

                // Extract and parse this member. Only mark it extracted after
                // it has been successfully materialized; otherwise a malformed
                // archive member could be skipped permanently for later symbols.
                auto memberData = memberDataView(ar, ar.members[memberIdx]);
                if (memberData.data == nullptr || memberData.size == 0) {
                    err << "error: archive member '" << ar.members[memberIdx].name
                        << "' has no object data\n";
                    return false;
                }

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
                extractedMembers.insert(key);
                if (++extractedCount > maxArchiveExtractions) {
                    err << "error: symbol resolution exceeded archive extraction bound\n";
                    return false;
                }

                size_t newIdx = allObjects.size();
                allObjects.push_back(std::move(memberObj));
                const bool allowArchiveDefinitionPreference =
                    isPreferredArchiveDefinitionObject(allObjects[newIdx], platform);
                if (!addObjSymbols(allObjects[newIdx],
                                   newIdx,
                                   globalSyms,
                                   comdatDefs,
                                   undefined,
                                   platform,
                                   allowArchiveDefinitionPreference,
                                   &pendingUndefined,
                                   err))
                    return false;
                extractedForUndef = true;
                break;
            }
            if (extractedForUndef)
                break;
        }
        if (extractedForUndef && undefined.find(undef) != undefined.end())
            pendingUndefined.push_back(undef);
    }

    if (platform == LinkPlatform::Windows)
        synthesizeWindowsThreadSafeStaticGuards(globalSyms, undefined);

    if (!materializeCommonSymbols(allObjects, globalSyms, err))
        return false;

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
        const bool allowDynamic = allowSynthetic || (isKnownDynamicSymbol(undef, platform) &&
                                                     !preferArchiveDefinition(undef, platform));
        if (!allowDynamic) {
            unresolvedErrors.push_back(undef);
            continue;
        }

        dynamicSyms.insert(undef);
        if (it != globalSyms.end()) {
            it->second.binding = GlobalSymEntry::Dynamic;
            it->second.resolvedAddrValid = it->second.resolvedAddr != 0;
        }
    }

    if (!unresolvedErrors.empty()) {
        std::sort(unresolvedErrors.begin(), unresolvedErrors.end());
        unresolvedErrors.erase(std::unique(unresolvedErrors.begin(), unresolvedErrors.end()),
                               unresolvedErrors.end());
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

static void synthesizeWindowsThreadSafeStaticGuards(
    std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
    std::unordered_set<std::string> &undefined) {
    std::vector<std::string> guardNames;
    guardNames.reserve(undefined.size());
    for (const auto &name : undefined) {
        if (isMsvcThreadSafeStaticGuardSymbol(name))
            guardNames.push_back(name);
    }

    for (const auto &name : guardNames) {
        auto existing = globalSyms.find(name);
        if (existing != globalSyms.end() && existing->second.binding != GlobalSymEntry::Undefined) {
            undefined.erase(name);
            continue;
        }

        auto &entry = globalSyms[name];
        entry.name = name;
        entry.binding = GlobalSymEntry::Global;
        entry.objIndex = 0;
        entry.secIndex = 0;
        entry.offset = 0;
        entry.resolvedAddr = 0;
        entry.resolvedAddrValid = false;
        entry.absolute = false;
        entry.common = true;
        entry.commonSize = std::max<size_t>(entry.commonSize, 4);
        entry.commonAlignment = std::max<size_t>(entry.commonAlignment, 4);
        undefined.erase(name);
    }
}

static bool materializeCommonSymbols(std::vector<ObjFile> &allObjects,
                                     std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                                     std::ostream &err) {
    std::vector<std::string> names;
    names.reserve(globalSyms.size());
    for (const auto &kv : globalSyms) {
        const auto &entry = kv.second;
        if (entry.common && entry.binding != GlobalSymEntry::Undefined &&
            entry.binding != GlobalSymEntry::Dynamic)
            names.push_back(kv.first);
    }
    if (names.empty())
        return true;

    std::sort(names.begin(), names.end());

    ObjFile commonObj;
    commonObj.name = "common";
    if (!allObjects.empty()) {
        commonObj.format = allObjects.front().format;
        commonObj.is64bit = allObjects.front().is64bit;
        commonObj.isLittleEndian = allObjects.front().isLittleEndian;
        commonObj.machine = allObjects.front().machine;
    }

    commonObj.sections.push_back(ObjSection{});
    commonObj.symbols.push_back(ObjSymbol{});

    ObjSection commonSec;
    commonSec.name = ".common";
    commonSec.writable = true;
    commonSec.alloc = true;
    commonSec.zeroFill = true;
    commonSec.alignment = 1;

    for (const auto &name : names) {
        auto it = globalSyms.find(name);
        if (it == globalSyms.end())
            continue;
        auto &entry = it->second;
        if (!isSupportedCommonAlignment(entry.commonAlignment)) {
            err << "error: common symbol '" << entry.name << "' has unsupported alignment\n";
            return false;
        }
        if (entry.commonSize > kMaxObjSectionBytes) {
            err << "error: common symbol '" << entry.name << "' exceeds section size limit\n";
            return false;
        }

        const size_t rem = commonSec.memSize % entry.commonAlignment;
        const size_t padding = (rem == 0) ? 0 : entry.commonAlignment - rem;
        if (padding > kMaxObjSectionBytes - commonSec.memSize ||
            entry.commonSize > kMaxObjSectionBytes - commonSec.memSize - padding) {
            err << "error: materialized common section exceeds size limit\n";
            return false;
        }

        commonSec.memSize += padding;
        const size_t offset = commonSec.memSize;
        commonSec.memSize += entry.commonSize;
        if (entry.commonAlignment > commonSec.alignment)
            commonSec.alignment = static_cast<uint32_t>(entry.commonAlignment);

        ObjSymbol sym;
        sym.name = entry.name.empty() ? name : entry.name;
        sym.binding = entry.binding == GlobalSymEntry::Weak ? ObjSymbol::Weak : ObjSymbol::Global;
        sym.sectionIndex = 1;
        sym.offset = offset;
        sym.size = entry.commonSize;
        commonObj.symbols.push_back(std::move(sym));

        entry.objIndex = allObjects.size();
        entry.secIndex = 1;
        entry.offset = offset;
        entry.absolute = false;
        entry.resolvedAddr = 0;
        entry.resolvedAddrValid = false;
        entry.common = false;
    }

    commonObj.sections.push_back(std::move(commonSec));
    allObjects.push_back(std::move(commonObj));
    return true;
}

} // namespace viper::codegen::linker

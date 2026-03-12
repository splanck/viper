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

#include <sstream>

namespace viper::codegen::linker
{

/// Add symbols from a single object file into the global table.
/// @param obj        The object file.
/// @param objIdx     Its index in allObjects.
/// @param globalSyms The global symbol table to update.
/// @param undefined  Set of currently undefined symbol names.
/// @param err        Error stream.
/// @return false if multiply-defined symbol error.
static bool addObjSymbols(const ObjFile &obj, size_t objIdx,
                          std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                          std::unordered_set<std::string> &undefined, std::ostream &err)
{
    for (size_t i = 1; i < obj.symbols.size(); ++i)
    {
        const auto &sym = obj.symbols[i];
        if (sym.name.empty())
            continue;

        if (sym.binding == ObjSymbol::Undefined)
        {
            // Only add to undefined set if not already defined.
            auto it = globalSyms.find(sym.name);
            if (it == globalSyms.end() || it->second.binding == GlobalSymEntry::Undefined)
                undefined.insert(sym.name);
            if (it == globalSyms.end())
            {
                GlobalSymEntry e;
                e.name = sym.name;
                e.binding = GlobalSymEntry::Undefined;
                globalSyms[sym.name] = std::move(e);
            }
            continue;
        }

        if (sym.binding == ObjSymbol::Local)
            continue; // Locals don't participate in global resolution.

        const bool isWeak = (sym.binding == ObjSymbol::Weak);
        auto it = globalSyms.find(sym.name);
        if (it == globalSyms.end())
        {
            // New symbol.
            GlobalSymEntry e;
            e.name = sym.name;
            e.binding = isWeak ? GlobalSymEntry::Weak : GlobalSymEntry::Global;
            e.objIndex = objIdx;
            e.secIndex = sym.sectionIndex;
            e.offset = sym.offset;
            globalSyms[sym.name] = std::move(e);
            undefined.erase(sym.name);
        }
        else
        {
            auto &existing = it->second;
            if (existing.binding == GlobalSymEntry::Undefined)
            {
                // Was undefined, now defined.
                existing.binding = isWeak ? GlobalSymEntry::Weak : GlobalSymEntry::Global;
                existing.objIndex = objIdx;
                existing.secIndex = sym.sectionIndex;
                existing.offset = sym.offset;
                undefined.erase(sym.name);
            }
            else if (existing.binding == GlobalSymEntry::Weak && !isWeak)
            {
                // Strong overrides weak.
                existing.binding = GlobalSymEntry::Global;
                existing.objIndex = objIdx;
                existing.secIndex = sym.sectionIndex;
                existing.offset = sym.offset;
            }
            else if (existing.binding == GlobalSymEntry::Global && !isWeak)
            {
                err << "error: multiply defined symbol '" << sym.name << "' in " << obj.name
                    << "\n";
                return false;
            }
            // Weak doesn't override anything.
        }
    }
    return true;
}

/// Known system/dynamic library symbols that won't be in archives.
static bool isKnownDynamicSymbol(const std::string &name)
{
    // Common C library and platform functions.
    static const char *const kDynSymPrefixes[] = {
        // C library
        "printf", "fprintf", "sprintf", "snprintf", "puts", "fputs", "fopen", "fclose",
        "fread", "fwrite", "fseek", "ftell", "fflush", "fgets",
        "malloc", "calloc", "realloc", "free",
        "memcpy", "memmove", "memset", "memcmp", "strlen", "strcmp", "strcpy", "strncpy",
        "strdup", "strndup", "strcat", "strncat", "strstr", "strchr", "strrchr",
        "atoi", "atol", "atof", "strtol", "strtod", "strtoul",
        "exit", "_exit", "abort", "atexit",
        "getenv", "setenv", "system",
        "time", "clock", "gettimeofday", "nanosleep", "usleep",
        "open", "close", "read", "write", "lseek", "stat", "fstat", "lstat",
        "mkdir", "rmdir", "unlink", "rename", "getcwd", "chdir",
        "socket", "bind", "listen", "accept", "connect", "send", "recv",
        "select", "poll",
        "pthread_create", "pthread_join", "pthread_mutex_init", "pthread_mutex_lock",
        "pthread_mutex_unlock", "pthread_cond_init", "pthread_cond_wait",
        "pthread_cond_signal",
        "dlopen", "dlsym", "dlclose", "dlerror",
        // Math
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
        "sqrt", "pow", "exp", "log", "log2", "log10",
        "ceil", "floor", "round", "fmod", "fabs", "fmin", "fmax",
        // macOS specific
        "_NSGetExecutablePath", "dyld_stub_binder",
        // Windows CRT
        "ExitProcess", "GetModuleHandleA", "GetProcAddress",
        "VirtualAlloc", "VirtualFree", "GetLastError",
    };

    for (const char *prefix : kDynSymPrefixes)
    {
        if (name == prefix)
            return true;
    }

    // C library internal symbols.
    if (name.size() > 2 && name[0] == '_' && name[1] == '_')
        return true; // __libc_start_main, __stack_chk_fail, etc.

    return false;
}

bool resolveSymbols(const std::vector<ObjFile> &initialObjects,
                    std::vector<Archive> &archives,
                    std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                    std::vector<ObjFile> &allObjects,
                    std::unordered_set<std::string> &dynamicSyms,
                    std::ostream &err)
{
    // Start with initial objects.
    allObjects = initialObjects;
    std::unordered_set<std::string> undefined;

    for (size_t i = 0; i < allObjects.size(); ++i)
    {
        if (!addObjSymbols(allObjects[i], i, globalSyms, undefined, err))
            return false;
    }

    // Iteratively resolve from archives until fixed point.
    std::unordered_set<size_t> extractedMembers; // Track "archiveIdx * 100000 + memberIdx".
    bool changed = true;
    while (changed)
    {
        changed = false;
        // Snapshot undefined set — addObjSymbols modifies it, invalidating iterators.
        std::vector<std::string> undefSnapshot(undefined.begin(), undefined.end());
        for (size_t ai = 0; ai < archives.size(); ++ai)
        {
            auto &ar = archives[ai];
            for (const auto &undef : undefSnapshot)
            {
                auto symIt = ar.symbolIndex.find(undef);
                // Mach-O archives use underscore-prefixed symbol names.
                if (symIt == ar.symbolIndex.end())
                    symIt = ar.symbolIndex.find("_" + undef);
                if (symIt == ar.symbolIndex.end())
                    continue;

                size_t memberIdx = symIt->second;
                size_t key = ai * 100000 + memberIdx;
                if (extractedMembers.count(key))
                    continue;

                // Extract and parse this member.
                extractedMembers.insert(key);
                auto memberData = extractMember(ar, ar.members[memberIdx]);
                if (memberData.empty())
                    continue;

                ObjFile memberObj;
                std::ostringstream memberErr;
                if (!readObjFile(memberData.data(), memberData.size(),
                                 ar.path + "(" + ar.members[memberIdx].name + ")", memberObj,
                                 memberErr))
                {
                    err << memberErr.str();
                    continue;
                }

                size_t newIdx = allObjects.size();
                allObjects.push_back(std::move(memberObj));
                if (!addObjSymbols(allObjects[newIdx], newIdx, globalSyms, undefined, err))
                    return false;
                changed = true;
            }
        }
    }

    // Mark remaining undefined as dynamic or report errors.
    std::vector<std::string> unresolved;
    for (const auto &undef : undefined)
    {
        auto it = globalSyms.find(undef);
        if (it != globalSyms.end() && it->second.binding != GlobalSymEntry::Undefined)
            continue; // Was resolved during iteration.

        if (isKnownDynamicSymbol(undef))
        {
            dynamicSyms.insert(undef);
            if (it != globalSyms.end())
                it->second.binding = GlobalSymEntry::Dynamic;
        }
        else
        {
            // Mark as dynamic anyway — the system linker will handle it.
            // This is less strict than a traditional linker but matches our use case
            // where runtime symbols may not be in archives (e.g., libc).
            dynamicSyms.insert(undef);
            if (it != globalSyms.end())
                it->second.binding = GlobalSymEntry::Dynamic;
        }
    }

    return true;
}

} // namespace viper::codegen::linker

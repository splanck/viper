//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/link/ModuleLinker.cpp
// Purpose: Implements the IL module linker that merges multiple IL modules.
// Key invariants:
//   - All Import references resolved before returning.
//   - Name collisions among Internal functions are disambiguated.
//   - Extern signature mismatches are reported as errors.
// Ownership/Lifetime: Input modules are consumed by move.
// Links: docs/adr/0003-il-linkage-and-module-linking.md
//
//===----------------------------------------------------------------------===//

#include "il/link/ModuleLinker.hpp"

#include "il/core/Extern.hpp"
#include "il/core/Function.hpp"
#include "il/core/Global.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Linkage.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::link
{

using il::core::Extern;
using il::core::Function;
using il::core::Global;
using il::core::Linkage;
using il::core::Module;

namespace
{

/// @brief Identify which module index contains the "main" function.
/// @return Index of the entry module, or -1 if none found.
int findEntryModule(const std::vector<Module> &modules, std::vector<std::string> &errors)
{
    int entryIdx = -1;
    for (size_t i = 0; i < modules.size(); ++i)
    {
        for (const auto &fn : modules[i].functions)
        {
            if (fn.name == "main" && fn.linkage != Linkage::Import)
            {
                if (entryIdx >= 0)
                {
                    errors.push_back("multiple modules define 'main' (modules " +
                                     std::to_string(entryIdx) + " and " + std::to_string(i) + ")");
                    return -1;
                }
                entryIdx = static_cast<int>(i);
            }
        }
    }
    if (entryIdx < 0)
        errors.push_back("no module defines 'main'");
    return entryIdx;
}

/// @brief Build an index of all exported function names → module index.
std::unordered_map<std::string, size_t> buildExportIndex(const std::vector<Module> &modules)
{
    std::unordered_map<std::string, size_t> index;
    for (size_t i = 0; i < modules.size(); ++i)
    {
        for (const auto &fn : modules[i].functions)
        {
            if (fn.linkage == Linkage::Export)
                index[fn.name] = i;
        }
    }
    return index;
}

/// @brief Generate a module prefix for disambiguating Internal functions.
/// @details Uses "m<index>$" to prefix function names from non-entry modules.
std::string modulePrefix(size_t moduleIndex)
{
    return "m" + std::to_string(moduleIndex) + "$";
}

/// @brief Check if a function name looks like an init function that should be
///        called from main.
bool isInitFunction(const std::string &name)
{
    // Match patterns: __zia_iface_init, __mod_init$oop, *$init, etc.
    if (name.find("__zia_iface_init") != std::string::npos)
        return true;
    if (name.find("__mod_init$oop") != std::string::npos)
        return true;
    if (name.size() > 5 && name.substr(name.size() - 5) == "$init")
        return true;
    return false;
}

/// @brief Rewrite all call instructions in a function to use renamed targets.
void rewriteCalls(Function &fn, const std::unordered_map<std::string, std::string> &renameMap)
{
    for (auto &bb : fn.blocks)
    {
        for (auto &instr : bb.instructions)
        {
            if (!instr.callee.empty())
            {
                auto it = renameMap.find(instr.callee);
                if (it != renameMap.end())
                    instr.callee = it->second;
            }
        }
    }
}

} // namespace

LinkResult linkModules(std::vector<Module> modules)
{
    LinkResult result;

    if (modules.empty())
    {
        result.errors.push_back("no modules to link");
        return result;
    }
    if (modules.size() == 1)
    {
        result.module = std::move(modules[0]);
        return result;
    }

    // Step 1: Find the entry module.
    int entryIdx = findEntryModule(modules, result.errors);
    if (entryIdx < 0)
        return result;

    // Step 2: Build export index for import resolution.
    auto exportIndex = buildExportIndex(modules);

    // Step 3: Resolve imports.
    // For each Import function in any module, find the defining Export.
    // Also track which functions need renaming to avoid collisions.
    std::unordered_map<std::string, std::string> renameMap; // old name → new name
    std::unordered_set<std::string> usedNames;

    // First pass: collect all Export and entry-module function names.
    for (size_t i = 0; i < modules.size(); ++i)
    {
        for (const auto &fn : modules[i].functions)
        {
            if (fn.linkage == Linkage::Export || static_cast<int>(i) == entryIdx)
                usedNames.insert(fn.name);
        }
    }

    // Second pass: rename Internal functions from non-entry modules if they collide.
    for (size_t i = 0; i < modules.size(); ++i)
    {
        if (static_cast<int>(i) == entryIdx)
            continue;

        std::string prefix = modulePrefix(i);
        for (auto &fn : modules[i].functions)
        {
            if (fn.linkage == Linkage::Internal)
            {
                if (usedNames.count(fn.name))
                {
                    // Collision with an existing name — prefix it.
                    std::string newName = prefix + fn.name;
                    renameMap[fn.name] = newName;
                    fn.name = newName;
                }
                usedNames.insert(fn.name);
            }
        }
    }

    // Third pass: resolve Import declarations.
    for (size_t i = 0; i < modules.size(); ++i)
    {
        for (const auto &fn : modules[i].functions)
        {
            if (fn.linkage != Linkage::Import)
                continue;

            // Check export index.
            auto expIt = exportIndex.find(fn.name);
            if (expIt == exportIndex.end())
            {
                // Also allow resolving against Internal functions from the entry module.
                bool foundInternal = false;
                for (const auto &entryFn : modules[static_cast<size_t>(entryIdx)].functions)
                {
                    if (entryFn.name == fn.name && entryFn.linkage != Linkage::Import)
                    {
                        foundInternal = true;
                        break;
                    }
                }
                if (!foundInternal)
                {
                    result.errors.push_back("unresolved import: @" + fn.name);
                }
            }
        }
    }

    if (!result.errors.empty())
        return result;

    // Step 4: Merge externs.
    std::unordered_map<std::string, Extern> mergedExterns;
    for (auto &mod : modules)
    {
        for (auto &ext : mod.externs)
        {
            auto it = mergedExterns.find(ext.name);
            if (it != mergedExterns.end())
            {
                // Verify signature compatibility.
                const auto &existing = it->second;
                if (existing.retType.kind != ext.retType.kind ||
                    existing.params.size() != ext.params.size())
                {
                    result.errors.push_back("extern signature mismatch for @" + ext.name);
                    continue;
                }
                bool mismatch = false;
                for (size_t p = 0; p < ext.params.size(); ++p)
                {
                    if (existing.params[p].kind != ext.params[p].kind)
                    {
                        mismatch = true;
                        break;
                    }
                }
                if (mismatch)
                    result.errors.push_back("extern parameter type mismatch for @" + ext.name);
            }
            else
            {
                mergedExterns.emplace(ext.name, std::move(ext));
            }
        }
    }

    if (!result.errors.empty())
        return result;

    // Step 5: Merge globals.
    std::unordered_map<std::string, Global> mergedGlobals;
    for (size_t i = 0; i < modules.size(); ++i)
    {
        std::string prefix = (static_cast<int>(i) == entryIdx) ? "" : modulePrefix(i);
        for (auto &g : modules[i].globals)
        {
            std::string name = g.name;
            if (!prefix.empty() && mergedGlobals.count(name))
            {
                // Collision — prefix the non-entry module's global.
                name = prefix + g.name;
                g.name = name;
            }
            mergedGlobals.emplace(name, std::move(g));
        }
    }

    // Step 6: Collect init functions from non-entry modules.
    std::vector<std::string> initFunctions;
    for (size_t i = 0; i < modules.size(); ++i)
    {
        if (static_cast<int>(i) == entryIdx)
            continue;
        for (const auto &fn : modules[i].functions)
        {
            if (fn.linkage != Linkage::Import && isInitFunction(fn.name))
                initFunctions.push_back(fn.name);
        }
    }

    // Step 7: Build the merged module.
    Module &merged = result.module;

    // Copy externs.
    for (auto &[name, ext] : mergedExterns)
        merged.externs.push_back(std::move(ext));

    // Copy globals.
    for (auto &[name, g] : mergedGlobals)
        merged.globals.push_back(std::move(g));

    // Copy functions (skip Import stubs — they're resolved by the definitions).
    for (size_t i = 0; i < modules.size(); ++i)
    {
        // Apply call renaming for functions in non-entry modules.
        for (auto &fn : modules[i].functions)
        {
            if (fn.linkage == Linkage::Import)
                continue; // Skip import stubs.

            // Rewrite calls to renamed functions.
            if (!renameMap.empty())
                rewriteCalls(fn, renameMap);

            merged.functions.push_back(std::move(fn));
        }
    }

    // Step 8: Inject init calls into main.
    // Find main in the merged module and prepend calls to init functions.
    if (!initFunctions.empty())
    {
        for (auto &fn : merged.functions)
        {
            if (fn.name == "main" && !fn.blocks.empty())
            {
                auto &entry = fn.blocks.front();
                // Insert init calls at the beginning of the entry block.
                std::vector<il::core::Instr> initInstrs;
                for (const auto &initName : initFunctions)
                {
                    il::core::Instr callInstr;
                    callInstr.op = il::core::Opcode::Call;
                    callInstr.type = il::core::Type(il::core::Type::Kind::Void);
                    callInstr.callee = initName;
                    initInstrs.push_back(std::move(callInstr));
                }
                entry.instructions.insert(
                    entry.instructions.begin(), initInstrs.begin(), initInstrs.end());
                break;
            }
        }
    }

    return result;
}

} // namespace il::link

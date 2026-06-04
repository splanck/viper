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
#include "il/core/Value.hpp"
#include "il/link/InteropThunks.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::link {

using il::core::Extern;
using il::core::Function;
using il::core::Global;
using il::core::Linkage;
using il::core::Module;
using il::core::Type;

namespace {

/// @brief Identify which module index contains the "main" function.
/// @return Index of the entry module, or -1 if none found.
int findEntryModule(const std::vector<Module> &modules, std::vector<std::string> &errors) {
    int entryIdx = -1;
    for (size_t i = 0; i < modules.size(); ++i) {
        for (const auto &fn : modules[i].functions) {
            if (fn.name == "main" && fn.linkage != Linkage::Import) {
                if (entryIdx >= 0) {
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

struct FunctionRef {
    size_t moduleIndex = 0;
    size_t functionIndex = 0;
};

/// @brief Build an index of all exported function names → function location.
std::unordered_map<std::string, FunctionRef> buildExportIndex(const std::vector<Module> &modules,
                                                              std::vector<std::string> &errors) {
    std::unordered_map<std::string, FunctionRef> index;
    for (size_t i = 0; i < modules.size(); ++i) {
        for (size_t f = 0; f < modules[i].functions.size(); ++f) {
            const auto &fn = modules[i].functions[f];
            if (fn.linkage != Linkage::Export)
                continue;
            auto [it, inserted] = index.emplace(fn.name, FunctionRef{i, f});
            if (!inserted) {
                errors.push_back("duplicate export: @" + fn.name);
            }
        }
    }
    return index;
}

/// @brief Generate a module prefix for disambiguating Internal functions.
/// @details Uses "m<index>$" to prefix function names from non-entry modules.
std::string modulePrefix(size_t moduleIndex) {
    return "m" + std::to_string(moduleIndex) + "$";
}

/// @brief Check if a function name looks like an init function that should be
///        called from main.
bool isInitFunction(const std::string &name) {
    // Match patterns: __zia_iface_init, __mod_init$oop, *$init, etc.
    if (name.find("__zia_iface_init") != std::string::npos)
        return true;
    if (name.find("__mod_init$oop") != std::string::npos)
        return true;
    if (name.size() > 5 && name.substr(name.size() - 5) == "$init")
        return true;
    return false;
}

/// @brief Test whether two function declarations share an identical IL signature.
/// @details Compares return type, parameter count + types, and varargs flag. Used
///          when matching an Import stub to its Export definition before deciding
///          whether a thunk is needed.
bool sameSignature(const Function &a, const Function &b) {
    if (a.retType.kind != b.retType.kind || a.params.size() != b.params.size() ||
        a.isVarArg != b.isVarArg)
        return false;
    for (size_t i = 0; i < a.params.size(); ++i) {
        if (a.params[i].type.kind != b.params[i].type.kind)
            return false;
    }
    return true;
}

/// @brief Detect the I1 ↔ I64 narrow/widen pair that boolean-interop thunks bridge.
bool isBooleanMismatch(Type::Kind a, Type::Kind b) {
    return (a == Type::Kind::I1 && b == Type::Kind::I64) ||
           (a == Type::Kind::I64 && b == Type::Kind::I1);
}

/// @brief Check whether an Import/Export pair differs only by I1↔I64 boolean
///        coercion in the return type or any parameter slot.
/// @details When this returns true the linker can synthesise a boolean-coercion
///          thunk via il::link::generateBooleanThunks instead of erroring out.
bool booleanInteropCompatible(const Function &importDecl, const Function &definition) {
    if (importDecl.params.size() != definition.params.size() ||
        importDecl.isVarArg != definition.isVarArg)
        return false;
    if (importDecl.retType.kind != definition.retType.kind &&
        !isBooleanMismatch(importDecl.retType.kind, definition.retType.kind))
        return false;
    for (size_t i = 0; i < importDecl.params.size(); ++i) {
        const auto importKind = importDecl.params[i].type.kind;
        const auto defKind = definition.params[i].type.kind;
        if (importKind != defKind && !isBooleanMismatch(importKind, defKind))
            return false;
    }
    return true;
}

/// @brief Reserve @p base in @p usedNames, appending "$N" suffixes until unique.
/// @details Mutates @p usedNames so subsequent callers see the chosen name as taken.
std::string makeUniqueName(const std::string &base, std::unordered_set<std::string> &usedNames) {
    if (usedNames.insert(base).second)
        return base;
    for (size_t i = 1;; ++i) {
        std::string candidate = base + "$" + std::to_string(i);
        if (usedNames.insert(candidate).second)
            return candidate;
    }
}

/// @brief Apply the global-rename map to a single Value if it is a GlobalAddr.
void rewriteGlobalValue(il::core::Value &value,
                        const std::unordered_map<std::string, std::string> &globalRenameMap) {
    if (value.kind != il::core::Value::Kind::GlobalAddr)
        return;
    auto it = globalRenameMap.find(value.str);
    if (it != globalRenameMap.end())
        value.str = it->second;
}

/// @brief Rewrite a Value's symbol reference using the function- or global-rename map.
/// @details Function renames take precedence — if @p value points at a renamed
///          function, the global map is not consulted.
void rewriteSymbolValue(il::core::Value &value,
                        const std::unordered_map<std::string, std::string> &functionRenameMap,
                        const std::unordered_map<std::string, std::string> &globalRenameMap) {
    if (value.kind != il::core::Value::Kind::GlobalAddr)
        return;
    if (auto fnIt = functionRenameMap.find(value.str); fnIt != functionRenameMap.end()) {
        value.str = fnIt->second;
        return;
    }
    rewriteGlobalValue(value, globalRenameMap);
}

/// @brief Rewrite function and global references inside one function.
void rewriteFunctionRefs(Function &fn,
                         const std::unordered_map<std::string, std::string> &functionRenameMap,
                         const std::unordered_map<std::string, std::string> &globalRenameMap) {
    for (auto &bb : fn.blocks) {
        for (auto &instr : bb.instructions) {
            if (!instr.callee.empty()) {
                auto it = functionRenameMap.find(instr.callee);
                if (it != functionRenameMap.end())
                    instr.callee = it->second;
            }
            for (auto &operand : instr.operands)
                rewriteSymbolValue(operand, functionRenameMap, globalRenameMap);
            for (auto &args : instr.brArgs) {
                for (auto &arg : args)
                    rewriteSymbolValue(arg, functionRenameMap, globalRenameMap);
            }
        }
    }
}

} // namespace

LinkResult linkModules(std::vector<Module> modules) {
    LinkResult result;

    if (modules.empty()) {
        result.errors.push_back("no modules to link");
        return result;
    }

    // Step 1: Find the entry module.
    int entryIdx = findEntryModule(modules, result.errors);
    if (entryIdx < 0)
        return result;

    // Step 2: Build export index for import resolution.
    auto exportIndex = buildExportIndex(modules, result.errors);
    if (!result.errors.empty())
        return result;

    // Step 3: Resolve imports.
    // For each Import function in any module, find the defining Export.
    // Also track which functions need renaming to avoid collisions.
    std::vector<std::unordered_map<std::string, std::string>> functionRenameMaps(modules.size());
    std::vector<std::unordered_map<std::string, std::string>> globalRenameMaps(modules.size());
    std::vector<Function> generatedThunks;
    std::unordered_set<std::string> usedNames;

    // First pass: collect all Export and entry-module function names.
    for (size_t i = 0; i < modules.size(); ++i) {
        for (const auto &fn : modules[i].functions) {
            if (fn.linkage == Linkage::Export || static_cast<int>(i) == entryIdx)
                usedNames.insert(fn.name);
        }
    }

    // Second pass: rename Internal functions from non-entry modules if they collide.
    for (size_t i = 0; i < modules.size(); ++i) {
        if (static_cast<int>(i) == entryIdx)
            continue;

        std::string prefix = modulePrefix(i);
        for (auto &fn : modules[i].functions) {
            if (fn.linkage == Linkage::Internal) {
                if (usedNames.count(fn.name)) {
                    // Collision with an existing name — prefix it.
                    std::string newName = prefix + fn.name;
                    functionRenameMaps[i][fn.name] = newName;
                    fn.name = newName;
                }
                usedNames.insert(fn.name);
            }
        }
    }

    // Third pass: resolve Import declarations.
    for (size_t i = 0; i < modules.size(); ++i) {
        for (const auto &fn : modules[i].functions) {
            if (fn.linkage != Linkage::Import)
                continue;

            const Function *definition = nullptr;

            auto expIt = exportIndex.find(fn.name);
            if (expIt != exportIndex.end()) {
                definition =
                    &modules[expIt->second.moduleIndex].functions[expIt->second.functionIndex];
            } else {
                for (const auto &entryFn : modules[static_cast<size_t>(entryIdx)].functions) {
                    if (entryFn.name == fn.name && entryFn.linkage != Linkage::Import) {
                        definition = &entryFn;
                        break;
                    }
                }
            }

            if (!definition) {
                result.errors.push_back("unresolved import: @" + fn.name);
                continue;
            }

            if (sameSignature(fn, *definition))
                continue;

            if (booleanInteropCompatible(fn, *definition)) {
                Module importMod;
                importMod.functions.push_back(fn);
                Module exportMod;
                Function exported = *definition;
                exported.linkage = Linkage::Export;
                exportMod.functions.push_back(std::move(exported));

                auto thunks = generateBooleanThunks(importMod, exportMod);
                if (thunks.empty()) {
                    result.errors.push_back("function signature mismatch for @" + fn.name);
                    continue;
                }
                std::string thunkName = makeUniqueName(thunks.front().thunkName, usedNames);
                thunks.front().thunk.name = thunkName;
                functionRenameMaps[i][fn.name] = thunkName;
                generatedThunks.push_back(std::move(thunks.front().thunk));
                continue;
            }

            result.errors.push_back("function signature mismatch for @" + fn.name);
        }
    }

    if (!result.errors.empty())
        return result;

    // Step 4: Merge externs.
    std::unordered_map<std::string, Extern> mergedExterns;
    for (auto &mod : modules) {
        for (auto &ext : mod.externs) {
            auto it = mergedExterns.find(ext.name);
            if (it != mergedExterns.end()) {
                // Verify signature compatibility.
                const auto &existing = it->second;
                if (existing.retType.kind != ext.retType.kind ||
                    existing.params.size() != ext.params.size()) {
                    result.errors.push_back("extern signature mismatch for @" + ext.name);
                    continue;
                }
                bool mismatch = false;
                for (size_t p = 0; p < ext.params.size(); ++p) {
                    if (existing.params[p].kind != ext.params[p].kind) {
                        mismatch = true;
                        break;
                    }
                }
                if (mismatch)
                    result.errors.push_back("extern parameter type mismatch for @" + ext.name);
                const bool mergedPure = it->second.attrs().pure && ext.attrs().pure;
                const bool mergedReadonly =
                    (it->second.attrs().readonly || it->second.attrs().pure) &&
                    (ext.attrs().readonly || ext.attrs().pure);
                it->second.attrs().nothrow = it->second.attrs().nothrow && ext.attrs().nothrow;
                it->second.attrs().readonly = mergedReadonly;
                it->second.attrs().pure = mergedPure;
            } else {
                mergedExterns.emplace(ext.name, std::move(ext));
            }
        }
    }

    if (!result.errors.empty())
        return result;

    // Step 5: Merge globals.
    std::unordered_map<std::string, Global> mergedGlobals;
    for (size_t i = 0; i < modules.size(); ++i) {
        std::string prefix = (static_cast<int>(i) == entryIdx) ? "" : modulePrefix(i);
        for (auto &g : modules[i].globals) {
            std::string name = g.name;
            if (mergedGlobals.count(name)) {
                if (prefix.empty()) {
                    result.errors.push_back("duplicate global: @" + name);
                    continue;
                }
                // Collision — prefix the non-entry module's global.
                name = prefix + g.name;
                while (mergedGlobals.count(name))
                    name = prefix + name;
                globalRenameMaps[i][g.name] = name;
                g.name = name;
            }
            mergedGlobals.emplace(name, std::move(g));
        }
    }

    if (!result.errors.empty())
        return result;

    // Step 6: Collect init functions from non-entry modules.
    std::vector<std::string> initFunctions;
    for (size_t i = 0; i < modules.size(); ++i) {
        if (static_cast<int>(i) == entryIdx)
            continue;
        for (const auto &fn : modules[i].functions) {
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
    for (size_t i = 0; i < modules.size(); ++i) {
        // Apply call renaming for functions in non-entry modules.
        for (auto &fn : modules[i].functions) {
            if (fn.linkage == Linkage::Import)
                continue; // Skip import stubs.

            rewriteFunctionRefs(fn, functionRenameMaps[i], globalRenameMaps[i]);

            merged.functions.push_back(std::move(fn));
        }
    }

    for (auto &thunk : generatedThunks)
        merged.functions.push_back(std::move(thunk));

    {
        std::unordered_set<std::string> finalNames;
        for (const auto &fn : merged.functions) {
            if (!finalNames.insert(fn.name).second)
                result.errors.push_back("duplicate function after linking: @" + fn.name);
        }
        if (!result.errors.empty()) {
            result.module = Module{};
            return result;
        }
    }

    // Step 8: Inject init calls into main.
    // Find main in the merged module and prepend calls to init functions.
    if (!initFunctions.empty()) {
        for (auto &fn : merged.functions) {
            if (fn.name == "main" && !fn.blocks.empty()) {
                auto &entry = fn.blocks.front();
                // Insert init calls at the beginning of the entry block.
                std::vector<il::core::Instr> initInstrs;
                for (const auto &initName : initFunctions) {
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

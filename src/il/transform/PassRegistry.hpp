// File: src/il/transform/PassRegistry.hpp
// Purpose: Declare pass registration primitives and preservation tracking for IL transforms.
// Key invariants: Pass identifiers are unique within the registry; preservation queries are
// idempotent. Ownership/Lifetime: Factories and callbacks stored in PassRegistry outlive the pass
// manager. Links: docs/codemap.md
#pragma once

#include "il/core/fwd.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace il::transform
{

class AnalysisManager;

class PreservedAnalyses
{
  public:
    static PreservedAnalyses all();
    static PreservedAnalyses none();

    PreservedAnalyses &preserveModule(const std::string &id);
    PreservedAnalyses &preserveFunction(const std::string &id);
    PreservedAnalyses &preserveAllModules();
    PreservedAnalyses &preserveAllFunctions();

    bool preservesAllModuleAnalyses() const;
    bool preservesAllFunctionAnalyses() const;
    bool isModulePreserved(const std::string &id) const;
    bool isFunctionPreserved(const std::string &id) const;
    bool hasModulePreservations() const;
    bool hasFunctionPreservations() const;

  private:
    bool preserveAllModules_ = false;
    bool preserveAllFunctions_ = false;
    std::unordered_set<std::string> moduleAnalyses_;
    std::unordered_set<std::string> functionAnalyses_;
};

class ModulePass
{
  public:
    virtual ~ModulePass() = default;
    virtual std::string_view id() const = 0;
    virtual PreservedAnalyses run(core::Module &module, AnalysisManager &analysis) = 0;
};

class FunctionPass
{
  public:
    virtual ~FunctionPass() = default;
    virtual std::string_view id() const = 0;
    virtual PreservedAnalyses run(core::Function &function, AnalysisManager &analysis) = 0;
};

namespace detail
{
enum class PassKind
{
    Module,
    Function
};

struct PassFactory
{
    PassKind kind;
    std::function<std::unique_ptr<ModulePass>()> makeModule;
    std::function<std::unique_ptr<FunctionPass>()> makeFunction;
};
} // namespace detail

class PassRegistry
{
  public:
    using ModulePassFactory = std::function<std::unique_ptr<ModulePass>()>;
    using FunctionPassFactory = std::function<std::unique_ptr<FunctionPass>()>;
    using ModulePassCallback = std::function<PreservedAnalyses(core::Module &, AnalysisManager &)>;
    using FunctionPassCallback =
        std::function<PreservedAnalyses(core::Function &, AnalysisManager &)>;

    void registerModulePass(const std::string &id, ModulePassFactory factory);
    void registerModulePass(const std::string &id, ModulePassCallback callback);
    void registerModulePass(const std::string &id, const std::function<void(core::Module &)> &fn);

    void registerFunctionPass(const std::string &id, FunctionPassFactory factory);
    void registerFunctionPass(const std::string &id, FunctionPassCallback callback);
    void registerFunctionPass(const std::string &id,
                              const std::function<void(core::Function &)> &fn);

    const detail::PassFactory *lookup(std::string_view id) const;

  private:
    std::unordered_map<std::string, detail::PassFactory> registry_;
};

void registerLoopSimplifyPass(PassRegistry &registry);
void registerSCCPPass(PassRegistry &registry);

} // namespace il::transform

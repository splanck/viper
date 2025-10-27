// File: src/codegen/x86_64/passes/PassManager.hpp
// Purpose: Declare a lightweight pass manager orchestrating x86-64 codegen passes.
// Key invariants: Passes run sequentially, short-circuiting on failure while preserving
//                 diagnostics collected to that point.
// Ownership/Lifetime: PassManager owns registered passes via unique_ptr and operates on
//                     caller-owned Module state for the duration of run().
// Links: docs/codemap.md, src/codegen/x86_64/CodegenPipeline.hpp

#pragma once

#include "codegen/x86_64/Backend.hpp"
#include "il/core/Module.hpp"

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace viper::codegen::x64::passes
{

/// \brief Mutable state threaded through the code-generation passes.
struct Module
{
    il::core::Module il;                                ///< Original IL module loaded from disk.
    std::optional<ILModule> lowered;                    ///< Adapter module produced by lowering.
    bool legalised = false;                             ///< Flag toggled once legalisation completes.
    bool registersAllocated = false;                    ///< Flag toggled once register allocation runs.
    std::optional<CodegenResult> codegenResult;         ///< Backend assembly emission artefacts.
};

/// \brief Diagnostic sink used by passes to surface errors and warnings.
class Diagnostics
{
  public:
    /// \brief Record an error message and mark the diagnostic stream as failed.
    void error(std::string message);

    /// \brief Record a non-fatal warning message.
    void warning(std::string message);

    /// \brief Query whether any error has been recorded.
    [[nodiscard]] bool hasErrors() const noexcept;

    /// \brief Query whether any warnings were recorded.
    [[nodiscard]] bool hasWarnings() const noexcept;

    /// \brief Emit accumulated diagnostics to the provided streams.
    void flush(std::ostream &err, std::ostream *warn = nullptr) const;

    /// \brief Direct read-only access to stored error messages (for testing).
    [[nodiscard]] const std::vector<std::string> &errors() const noexcept;

    /// \brief Direct read-only access to stored warning messages (for testing).
    [[nodiscard]] const std::vector<std::string> &warnings() const noexcept;

  private:
    std::vector<std::string> errors_{};
    std::vector<std::string> warnings_{};
};

/// \brief Abstract interface implemented by individual pipeline passes.
class Pass
{
  public:
    virtual ~Pass() = default;
    /// \brief Execute the pass over @p module, emitting diagnostics to @p diags.
    virtual bool run(Module &module, Diagnostics &diags) = 0;
};

/// \brief Container sequencing registered passes for execution.
class PassManager
{
  public:
    /// \brief Add a pass to the manager; ownership is transferred to the manager.
    void addPass(std::unique_ptr<Pass> pass);

    /// \brief Execute all registered passes in order.
    /// \return False when a pass signals failure; true otherwise.
    bool run(Module &module, Diagnostics &diags) const;

  private:
    std::vector<std::unique_ptr<Pass>> passes_{};
};

} // namespace viper::codegen::x64::passes

// File: src/il/verify/FunctionVerifier.hpp
// Purpose: Declares the verifier responsible for validating module functions and their bodies.
// Key invariants: Function, block, and instruction checks follow IL specification invariants.
// Ownership/Lifetime: Holds pointers into the inspected module for the duration of verification
// only. Links: docs/il-guide.md#reference
#pragma once

#include "il/verify/DiagSink.hpp"
#include "il/verify/ExceptionHandlerAnalysis.hpp"

#include "support/diag_expected.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace il::core
{
struct Module;
struct Function;
struct BasicBlock;
struct Instr;
struct Extern;
struct Type;
} // namespace il::core

namespace il::verify
{

class TypeInference;

/// @brief Validates function declarations, bodies, and intra-function references.
class FunctionVerifier
{
  public:
    using ExternMap = std::unordered_map<std::string, const il::core::Extern *>;

    explicit FunctionVerifier(const ExternMap &externs);

    /// @brief Verify every function within @p module using registered instruction strategies.
    /// @param module Module whose functions should be validated.
    /// @param sink Diagnostic sink receiving warnings discovered during verification.
    /// @return Success on completion or a diagnostic describing the first failure.
    il::support::Expected<void> run(const il::core::Module &module, DiagSink &sink);

    class InstructionStrategy
    {
      public:
        virtual ~InstructionStrategy() = default;

        virtual bool matches(const il::core::Instr &instr) const = 0;
        virtual il::support::Expected<void> verify(
            const il::core::Function &fn,
            const il::core::BasicBlock &bb,
            const il::core::Instr &instr,
            const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
            const std::unordered_map<std::string, const il::core::Extern *> &externs,
            const std::unordered_map<std::string, const il::core::Function *> &funcs,
            TypeInference &types,
            DiagSink &sink) const = 0;
    };

  private:
    il::support::Expected<void> verifyFunction(const il::core::Function &fn, DiagSink &sink);
    il::support::Expected<void> verifyBlock(
        const il::core::Function &fn,
        const il::core::BasicBlock &bb,
        const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
        std::unordered_map<unsigned, il::core::Type> &temps,
        DiagSink &sink);
    il::support::Expected<void> verifyInstruction(
        const il::core::Function &fn,
        const il::core::BasicBlock &bb,
        const il::core::Instr &instr,
        const std::unordered_map<std::string, const il::core::BasicBlock *> &blockMap,
        TypeInference &types,
        DiagSink &sink);
    [[nodiscard]] std::string formatFunctionDiag(const il::core::Function &fn,
                                                 std::string_view message) const;

    const ExternMap &externs_;
    std::unordered_map<std::string, const il::core::Function *> functionMap_;
    std::unordered_map<std::string, HandlerSignature> handlerInfo_;
    std::vector<std::unique_ptr<InstructionStrategy>> strategies_;
};

} // namespace il::verify

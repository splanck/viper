//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the FunctionVerifier class, which orchestrates comprehensive
// validation of IL function definitions. The FunctionVerifier is the central
// coordinator for verifying function bodies, dispatching to specialized strategies
// for different instruction categories.
//
// Functions are the primary unit of code in IL modules. Each function contains
// zero or more basic blocks, each block contains zero or more instructions. The
// FunctionVerifier ensures that every function adheres to IL specification rules
// including proper control flow structure, type safety, and exception handling
// semantics.
//
// Key Responsibilities:
// - Validate function signature (name, parameters, return type)
// - Build basic block symbol tables for control flow edge validation
// - Verify basic block structure (parameters, terminator presence)
// - Dispatch instruction verification to registered strategies
// - Maintain type inference context across instruction verification
// - Validate exception handler blocks and their signatures
//
// Design Rationale:
// The strategy pattern enables extensible instruction verification. Each opcode
// category (arithmetic, memory, control flow, exceptions) has dedicated strategy
// implementations. The FunctionVerifier coordinates strategy execution while
// maintaining the shared verification context (type environment, block maps,
// extern/function tables) needed by all strategies.
//
// Ownership and Lifetime:
// The FunctionVerifier holds const pointers into the module being verified. These
// pointers are valid only during the run() method execution and must not be
// retained afterward. The verifier builds temporary maps and type environments
// that exist only for the duration of a single function's verification.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/verify/BlockMap.hpp"
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

/**
 * @brief Validates IL function semantics and type safety.
 *
 * Performs comprehensive verification of function bodies including:
 * - Type inference and checking
 * - Control flow validation
 * - SSA form verification
 * - External function resolution
 *
 * @invariant All functions in a module must pass verification before execution.
 */
class FunctionVerifier
{
  public:
    using ExternMap = std::unordered_map<std::string, const il::core::Extern *>;

    explicit FunctionVerifier(const ExternMap &externs);

    /**
     * @brief Verify all functions in the module for correctness.
     *
     * Applies instruction-specific verification strategies to validate:
     * - Type consistency across operations
     * - Proper SSA form with unique definitions
     * - Valid control flow targets
     * - Correct external function usage
     *
     * @param module Module containing functions to verify.
     * @param sink Collector for non-fatal warnings during verification.
     * @return Success if all functions pass, error with diagnostic on first failure.
     */
    [[nodiscard]] il::support::Expected<void> run(const il::core::Module &module, DiagSink &sink);

    class InstructionStrategy
    {
      public:
        virtual ~InstructionStrategy() = default;

        virtual bool matches(const il::core::Instr &instr) const = 0;
        [[nodiscard]] virtual il::support::Expected<void> verify(
            const il::core::Function &fn,
            const il::core::BasicBlock &bb,
            const il::core::Instr &instr,
            const BlockMap &blockMap,
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
        const BlockMap &blockMap,
        std::unordered_map<unsigned, il::core::Type> &temps,
        const std::unordered_map<unsigned, const il::core::BasicBlock *> &definingBlock,
        DiagSink &sink);
    il::support::Expected<void> verifyInstruction(const il::core::Function &fn,
                                                  const il::core::BasicBlock &bb,
                                                  const il::core::Instr &instr,
                                                  const BlockMap &blockMap,
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

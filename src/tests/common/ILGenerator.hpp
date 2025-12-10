//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/common/ILGenerator.hpp
// Purpose: Generate random but well-typed IL modules for property-based testing.
// Key invariants: Generated modules are always valid IL (no UB, valid SSA).
// Ownership/Lifetime: Generator is stateless except for RNG seed.
// Links: docs/testing.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Module.hpp"
#include "il/core/Opcode.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace viper::tests
{

/// @brief Configuration for IL program generation.
struct ILGeneratorConfig
{
    /// @brief Minimum number of instructions to generate.
    std::size_t minInstructions = 3;

    /// @brief Maximum number of instructions to generate.
    std::size_t maxInstructions = 20;

    /// @brief Minimum number of basic blocks.
    std::size_t minBlocks = 1;

    /// @brief Maximum number of basic blocks.
    std::size_t maxBlocks = 4;

    /// @brief Whether to include floating-point operations.
    bool includeFloats = false;

    /// @brief Whether to include control flow (branches).
    bool includeControlFlow = true;

    /// @brief Whether to include comparison operations.
    bool includeComparisons = true;

    /// @brief Range for generated integer constants.
    std::int64_t minConstant = -1000;
    std::int64_t maxConstant = 1000;
};

/// @brief Result of IL generation including the module and metadata.
struct ILGeneratorResult
{
    /// @brief The generated module.
    il::core::Module module;

    /// @brief Seed used for generation (for reproduction).
    std::uint64_t seed = 0;

    /// @brief Textual IL source (for debugging).
    std::string ilSource;

    /// @brief Number of instructions generated.
    std::size_t instructionCount = 0;

    /// @brief Number of basic blocks generated.
    std::size_t blockCount = 0;
};

/// @brief Generates random but well-typed IL modules for testing.
/// @details The generator creates valid IL programs with:
///          - Arithmetic operations (add, sub, mul, div)
///          - Comparison operations (scmp_eq, scmp_lt, etc.)
///          - Control flow (conditional/unconditional branches)
///          - Valid SSA form with proper def-use chains
///
/// @invariant All generated modules pass IL verification.
/// @invariant Generated programs are deterministic given the same seed.
class ILGenerator
{
  public:
    /// @brief Create a generator with a random seed.
    ILGenerator();

    /// @brief Create a generator with a specific seed for reproducibility.
    /// @param seed RNG seed for deterministic generation.
    explicit ILGenerator(std::uint64_t seed);

    /// @brief Generate a random IL module according to configuration.
    /// @param config Generation parameters.
    /// @return Generated module with metadata.
    [[nodiscard]] ILGeneratorResult generate(const ILGeneratorConfig &config = {});

    /// @brief Get the current seed.
    [[nodiscard]] std::uint64_t seed() const noexcept
    {
        return seed_;
    }

  private:
    /// @brief Available arithmetic opcodes for generation.
    /// @note Uses checked ops per IL spec: iadd.ovf, isub.ovf, imul.ovf, sdiv.chk0
    static constexpr il::core::Opcode kArithOps[] = {
        il::core::Opcode::IAddOvf,
        il::core::Opcode::ISubOvf,
        il::core::Opcode::IMulOvf,
        il::core::Opcode::SDivChk0,
    };

    /// @brief Available comparison opcodes for generation.
    static constexpr il::core::Opcode kCmpOps[] = {
        il::core::Opcode::ICmpEq,
        il::core::Opcode::ICmpNe,
        il::core::Opcode::SCmpLT,
        il::core::Opcode::SCmpLE,
        il::core::Opcode::SCmpGT,
        il::core::Opcode::SCmpGE,
    };

    /// @brief Generate a random integer constant.
    [[nodiscard]] std::int64_t randomConstant(std::int64_t min, std::int64_t max);

    /// @brief Generate a random value (constant or existing temp).
    [[nodiscard]] il::core::Value randomValue(const std::vector<unsigned> &availableTemps,
                                              std::int64_t minConst,
                                              std::int64_t maxConst);

    /// @brief Pick a random element from an array.
    template <typename T, std::size_t N> [[nodiscard]] const T &randomChoice(const T (&arr)[N])
    {
        std::uniform_int_distribution<std::size_t> dist(0, N - 1);
        return arr[dist(rng_)];
    }

    /// @brief Generate unique block label.
    [[nodiscard]] std::string generateBlockLabel(std::size_t index);

    std::uint64_t seed_;
    std::mt19937_64 rng_;
};

/// @brief Print IL module to string for debugging and reproduction.
/// @param module Module to print.
/// @return IL source text.
[[nodiscard]] std::string printILToString(const il::core::Module &module);

} // namespace viper::tests

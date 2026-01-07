//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/common/ILGenerator.cpp
// Purpose: Implementation of random IL module generation.
// Key invariants: Generated modules always pass IL verification.
// Ownership/Lifetime: Generator maintains RNG state across calls.
// Links: docs/testing.md
//
//===----------------------------------------------------------------------===//

#include "common/ILGenerator.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/io/Serializer.hpp"

#include <chrono>

namespace viper::tests
{

ILGenerator::ILGenerator()
    : seed_(
          static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())),
      rng_(seed_)
{
}

ILGenerator::ILGenerator(std::uint64_t seed) : seed_(seed), rng_(seed) {}

std::int64_t ILGenerator::randomConstant(std::int64_t min, std::int64_t max)
{
    std::uniform_int_distribution<std::int64_t> dist(min, max);
    return dist(rng_);
}

il::core::Value ILGenerator::randomValue(const std::vector<unsigned> &availableTemps,
                                         std::int64_t minConst,
                                         std::int64_t maxConst)
{
    // 50% chance to use existing temp if available
    if (!availableTemps.empty())
    {
        std::uniform_int_distribution<int> coinFlip(0, 1);
        if (coinFlip(rng_) == 0)
        {
            std::uniform_int_distribution<std::size_t> dist(0, availableTemps.size() - 1);
            return il::core::Value::temp(availableTemps[dist(rng_)]);
        }
    }

    // Otherwise generate a constant (avoid 0 for divisor safety)
    std::int64_t val = randomConstant(minConst, maxConst);
    // Avoid division by zero - ensure non-zero constants when used as divisor
    if (val == 0)
    {
        val = 1;
    }
    return il::core::Value::constInt(val);
}

std::string ILGenerator::generateBlockLabel(std::size_t index)
{
    if (index == 0)
    {
        return "entry";
    }
    return "bb" + std::to_string(index);
}

ILGeneratorResult ILGenerator::generate(const ILGeneratorConfig &config)
{
    using namespace il::core;

    ILGeneratorResult result;
    result.seed = seed_;

    Module &module = result.module;

    // Create main function
    Function fn;
    fn.name = "main";
    fn.retType = Type(Type::Kind::I64);

    // Determine number of blocks (only 1 block for now to avoid control flow issues)
    result.blockCount = 1;

    // Track available temps for SSA
    std::vector<unsigned> availableTemps;
    unsigned nextTemp = 0;

    // Determine number of instructions
    std::uniform_int_distribution<std::size_t> instrDist(config.minInstructions,
                                                         config.maxInstructions);
    const std::size_t numInstructions = instrDist(rng_);
    result.instructionCount = numInstructions;

    // Create the entry block
    BasicBlock entry;
    entry.label = "entry";

    // Build list of enabled operation categories
    std::vector<int> categories; // 0=arith, 1=cmp, 2=bitwise, 3=shift
    categories.push_back(0);     // arithmetic always enabled
    if (config.includeComparisons)
        categories.push_back(1);
    if (config.includeBitwise)
        categories.push_back(2);
    if (config.includeShifts)
        categories.push_back(3);

    // Generate instructions
    for (std::size_t i = 0; i < numInstructions; ++i)
    {
        Instr instr;
        instr.result = nextTemp++;
        instr.loc = {1, 1, 1};

        // Choose operation category
        std::uniform_int_distribution<std::size_t> catDist(0, categories.size() - 1);
        int category = categories[catDist(rng_)];

        bool producesI1 = false;

        switch (category)
        {
            case 0: // arithmetic
                instr.op = randomChoice(kArithOps);
                instr.type = Type(Type::Kind::I64);
                break;
            case 1: // comparison
                instr.op = randomChoice(kCmpOps);
                instr.type = Type(Type::Kind::I1);
                producesI1 = true;
                break;
            case 2: // bitwise
                instr.op = randomChoice(kBitwiseOps);
                instr.type = Type(Type::Kind::I64);
                break;
            case 3: // shift
                instr.op = randomChoice(kShiftOps);
                instr.type = Type(Type::Kind::I64);
                break;
        }

        // Generate operands
        Value lhs = randomValue(availableTemps, config.minConstant, config.maxConstant);
        Value rhs = randomValue(availableTemps, config.minConstant, config.maxConstant);

        // For overflow-checked arithmetic, use only constants to prevent chain overflow.
        // Temps can grow unboundedly, causing overflow when chained.
        if (instr.op == Opcode::IAddOvf || instr.op == Opcode::ISubOvf ||
            instr.op == Opcode::IMulOvf)
        {
            lhs = Value::constInt(randomConstant(config.minConstant, config.maxConstant));
            rhs = Value::constInt(randomConstant(config.minConstant, config.maxConstant));
        }

        // For division, ensure non-zero divisor (use constants only to avoid runtime div-by-zero)
        // Also avoid MIN_INT64 / -1 which overflows
        if (instr.op == Opcode::SDivChk0 || instr.op == Opcode::UDivChk0)
        {
            // Use small constants for both operands to avoid overflow
            lhs = Value::constInt(randomConstant(config.minConstant, config.maxConstant));
            std::int64_t divisor = randomConstant(1, 10); // positive only
            rhs = Value::constInt(divisor);
        }

        // For shifts, ensure shift amount is in valid range (0-63)
        // and use non-negative values for the shifted operand to avoid edge cases
        if (instr.op == Opcode::Shl || instr.op == Opcode::LShr || instr.op == Opcode::AShr)
        {
            std::int64_t shiftAmt = randomConstant(0, 63);
            rhs = Value::constInt(shiftAmt);
            // For LShr/AShr, use non-negative LHS to avoid edge cases with negative constants
            if ((instr.op == Opcode::LShr || instr.op == Opcode::AShr) &&
                lhs.kind == Value::Kind::ConstInt && lhs.i64 < 0)
            {
                lhs = Value::constInt(std::abs(lhs.i64) % 10000);
            }
        }

        instr.operands = {lhs, rhs};
        entry.instructions.push_back(instr);

        // Only add i64 results to available temps (not comparisons)
        if (!producesI1)
        {
            availableTemps.push_back(*instr.result);
        }
    }

    // Add return instruction
    Instr ret;
    ret.op = Opcode::Ret;
    ret.type = Type(Type::Kind::Void);
    ret.loc = {1, 1, 1};

    // Return the last computed i64 value or a constant
    if (!availableTemps.empty())
    {
        ret.operands = {Value::temp(availableTemps.back())};
    }
    else
    {
        ret.operands = {Value::constInt(42)};
    }
    entry.instructions.push_back(ret);
    entry.terminated = true;

    // Add block to function
    fn.blocks.push_back(std::move(entry));

    // Ensure valueNames is sized appropriately
    fn.valueNames.resize(nextTemp);

    // Add function to module
    module.functions.push_back(std::move(fn));

    // Generate IL source text
    result.ilSource = printILToString(module);

    return result;
}

std::string printILToString(const il::core::Module &module)
{
    return il::io::Serializer::toString(module, il::io::Serializer::Mode::Pretty);
}

} // namespace viper::tests

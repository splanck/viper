// File: tests/common/TestIRBuilder.hpp
// Purpose: Provide a lightweight IR construction helper tailored for tests.
// Key invariants: Maintains a single active insertion block for SSA emission.
// Ownership/Lifetime: Owns the underlying Module and VmFixture used by tests.
// Links: docs/codemap.md

#pragma once

#include "il/build/IRBuilder.hpp"
#include "il/core/Instr.hpp"
#include "support/source_location.hpp"
#include "common/VmFixture.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace viper::tests
{

/// @brief Helper that constructs small IL fragments and executes them via the VM.
/// @invariant Maintains a current insertion block within the synthetic function.
/// @ownership Owns the module under construction and the VmFixture executing it.
class TestIRBuilder
{
  public:
    /// @brief Create a builder targeting a default "main" function returning i64.
    /// @param retType Return type for the generated function (defaults to i64).
    /// @param functionName Synthetic function identifier (defaults to "main").
    /// @param entryLabel Label assigned to the entry block (defaults to "entry").
    explicit TestIRBuilder(il::core::Type retType = il::core::Type(il::core::Type::Kind::I64),
                           std::string functionName = "main",
                           std::string entryLabel = "entry");

    /// @brief Retrieve the default source location used by helper methods.
    [[nodiscard]] static il::support::SourceLoc defaultLoc() noexcept
    {
        return {1, 1, 1};
    }

    TestIRBuilder(const TestIRBuilder &) = delete;
    TestIRBuilder &operator=(const TestIRBuilder &) = delete;
    TestIRBuilder(TestIRBuilder &&) = delete;
    TestIRBuilder &operator=(TestIRBuilder &&) = delete;
    ~TestIRBuilder() = default;

    /// @brief Access the underlying module under construction.
    [[nodiscard]] il::core::Module &module() noexcept
    {
        return module_;
    }

    /// @brief Access the synthetic function used for the test program.
    [[nodiscard]] il::core::Function &function() noexcept
    {
        return *function_;
    }

    /// @brief Access the current insertion block.
    [[nodiscard]] il::core::BasicBlock &block() noexcept
    {
        return function_->blocks.at(currentBlockIndex_);
    }

    /// @brief Change the insertion point to @p bb.
    void setInsertPoint(il::core::BasicBlock &bb);

    /// @brief Reserve the next temporary identifier.
    [[nodiscard]] unsigned reserveTemp();

    /// @brief Convenience helper that returns an i64 constant operand.
    [[nodiscard]] il::core::Value const_i64(std::int64_t value) const;

    /// @brief Emit an add instruction and return the resulting SSA value.
    [[nodiscard]] il::core::Value add(il::core::Value lhs,
                                      il::core::Value rhs,
                                      il::support::SourceLoc loc = defaultLoc());

    /// @brief Emit an arbitrary binary instruction and return its SSA result.
    il::core::Value binary(il::core::Opcode op,
                           il::core::Type type,
                           il::core::Value lhs,
                           il::core::Value rhs,
                           il::support::SourceLoc loc = defaultLoc());

    /// @brief Emit a store instruction targeting the active block.
    void store(il::core::Value pointer,
               il::core::Value value,
               il::core::Type storedType = il::core::Type(il::core::Type::Kind::I64),
               il::support::SourceLoc loc = defaultLoc());

    /// @brief Emit a return instruction with optional value.
    void ret(const std::optional<il::core::Value> &value,
             il::support::SourceLoc loc = defaultLoc());

    /// @brief Emit a void return instruction.
    void retVoid(il::support::SourceLoc loc = defaultLoc());

    /// @brief Execute the constructed module, optionally adding a return first.
    [[nodiscard]] std::int64_t run(const std::optional<il::core::Value> &value = std::nullopt,
                                   il::support::SourceLoc loc = defaultLoc());

    /// @brief Execute the constructed module expecting a trap and capture stderr.
    [[nodiscard]] std::string captureTrap(
        const std::optional<il::core::Value> &value = std::nullopt,
        il::support::SourceLoc loc = defaultLoc());

    /// @brief Produce a source location anchored at @p line/@p column.
    [[nodiscard]] il::support::SourceLoc loc(uint32_t line = 1, uint32_t column = 1) const;

    /// @brief Update the default file identifier used for generated locations.
    void setFileId(uint32_t fileId)
    {
        defaultLoc_.file_id = fileId;
    }

  private:
    il::core::Module module_{};
    il::build::IRBuilder builder_;
    il::core::Function *function_{nullptr};
    std::size_t currentBlockIndex_{0};
    il::support::SourceLoc defaultLoc_{defaultLoc()};
    VmFixture fixture_{};
};

} // namespace viper::tests

/// @brief Macro wrapper that instantiates a TestIRBuilder and executes @p BODY.
#define TEST_WITH_IL(NAME, BODY)                                                                   \
    int main()                                                                                     \
    {                                                                                              \
        viper::tests::TestIRBuilder NAME;                                                          \
        BODY return 0;                                                                             \
    }

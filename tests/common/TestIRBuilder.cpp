// File: tests/common/TestIRBuilder.cpp
// Purpose: Implement the TestIRBuilder helper used by VM unit tests.
// Key invariants: Tracks the active insertion block within the synthetic function.
// Ownership/Lifetime: Owns the IL module and VmFixture executing the program.
// Links: docs/codemap.md

#include "tests/common/TestIRBuilder.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Module.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace viper::tests
{
namespace
{
[[nodiscard]] il::core::Value makeTempValue(unsigned id)
{
    return il::core::Value::temp(id);
}
} // namespace

TestIRBuilder::TestIRBuilder(il::core::Type retType, std::string functionName, std::string entryLabel)
    : builder_(module_)
{
    function_ = &builder_.startFunction(functionName, retType, {});
    il::core::BasicBlock &entry = builder_.addBlock(*function_, entryLabel);
    builder_.setInsertPoint(entry);
    currentBlockIndex_ = function_->blocks.size() - 1U;
}

void TestIRBuilder::setInsertPoint(il::core::BasicBlock &bb)
{
    builder_.setInsertPoint(bb);
    const auto it = std::find_if(function_->blocks.begin(), function_->blocks.end(), [&](auto &candidate) {
        return &candidate == &bb;
    });
    if (it != function_->blocks.end())
    {
        currentBlockIndex_ = static_cast<std::size_t>(std::distance(function_->blocks.begin(), it));
    }
}

unsigned TestIRBuilder::reserveTemp()
{
    return builder_.reserveTempId();
}

il::core::Value TestIRBuilder::const_i64(std::int64_t value) const
{
    return il::core::Value::constInt(value);
}

il::core::Value TestIRBuilder::add(il::core::Value lhs,
                                   il::core::Value rhs,
                                   il::support::SourceLoc loc)
{
    return binary(il::core::Opcode::Add, il::core::Type(il::core::Type::Kind::I64), lhs, rhs, loc);
}

il::core::Value TestIRBuilder::binary(il::core::Opcode op,
                                      il::core::Type type,
                                      il::core::Value lhs,
                                      il::core::Value rhs,
                                      il::support::SourceLoc loc)
{
    il::core::Instr instr;
    const unsigned id = reserveTemp();
    instr.result = id;
    instr.op = op;
    instr.type = type;
    instr.operands.push_back(lhs);
    instr.operands.push_back(rhs);
    instr.loc = loc;
    block().instructions.push_back(std::move(instr));
    return makeTempValue(id);
}

void TestIRBuilder::store(il::core::Value pointer,
                          il::core::Value value,
                          il::core::Type storedType,
                          il::support::SourceLoc loc)
{
    il::core::Instr instr;
    instr.op = il::core::Opcode::Store;
    instr.type = storedType;
    instr.operands.push_back(pointer);
    instr.operands.push_back(value);
    instr.loc = loc;
    block().instructions.push_back(std::move(instr));
}

void TestIRBuilder::ret(const std::optional<il::core::Value> &value, il::support::SourceLoc loc)
{
    if (!block().terminated)
    {
        builder_.emitRet(value, loc);
    }
}

void TestIRBuilder::retVoid(il::support::SourceLoc loc)
{
    ret(std::nullopt, loc);
}

std::int64_t TestIRBuilder::run(const std::optional<il::core::Value> &value, il::support::SourceLoc loc)
{
    ret(value, loc);
    return fixture_.run(module_);
}

std::string TestIRBuilder::captureTrap(const std::optional<il::core::Value> &value, il::support::SourceLoc loc)
{
    ret(value, loc);
    return fixture_.captureTrap(module_);
}

il::support::SourceLoc TestIRBuilder::loc(uint32_t line, uint32_t column) const
{
    return {defaultLoc_.file_id, line, column};
}

} // namespace viper::tests

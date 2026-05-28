//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/il/OpcodeInfoTests.cpp
// Purpose: Exercise opcode metadata enumeration helpers for stability.
// Key invariants: Enumeration covers every opcode exactly once in declaration order.
// Ownership/Lifetime: Uses read-only metadata from il::core.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/OpcodeInfo.hpp"
#include "il/verify/SpecTables.hpp"

#include "tests/TestHarness.hpp"

#include <sstream>

namespace {

std::string metadataShape(
    il::core::Opcode op,
    il::core::ResultArity resultArity,
    il::core::TypeCategory resultType,
    uint8_t numOperandsMin,
    uint8_t numOperandsMax,
    const std::array<il::core::TypeCategory, il::core::kMaxOperandCategories> &operandTypes,
    bool hasSideEffects,
    uint8_t numSuccessors,
    bool isTerminator) {
    std::ostringstream out;
    out << il::core::opcode_mnemonic(op) << ':' << static_cast<int>(resultArity) << ':'
        << static_cast<int>(resultType) << ':' << static_cast<unsigned>(numOperandsMin) << ':'
        << static_cast<unsigned>(numOperandsMax);
    for (const auto category : operandTypes)
        out << ':' << static_cast<int>(category);
    out << ':' << hasSideEffects << ':' << static_cast<unsigned>(numSuccessors) << ':'
        << isTerminator;
    return out.str();
}

} // namespace

TEST(IL, OpcodeInfoTests) {
    using namespace il::core;

    const auto ops = all_opcodes();
    ASSERT_FALSE(ops.empty());

    const auto again = all_opcodes();
    ASSERT_EQ(ops, again);

    for (size_t index = 0; index < ops.size(); ++index) {
        const Opcode op = ops[index];
        ASSERT_EQ(static_cast<size_t>(op), index);

        const auto mnemonic = opcode_mnemonic(op);
        ASSERT_FALSE(mnemonic.empty());
        ASSERT_EQ(mnemonic, getOpcodeInfo(op).name);

        const auto &info = getOpcodeInfo(op);
        const auto &spec = il::verify::getInstructionSpec(op);
        EXPECT_EQ(metadataShape(op,
                                spec.resultArity,
                                spec.resultType,
                                spec.numOperandsMin,
                                spec.numOperandsMax,
                                spec.operandTypes,
                                spec.hasSideEffects,
                                spec.numSuccessors,
                                spec.isTerminator),
                  metadataShape(op,
                                info.resultArity,
                                info.resultType,
                                info.numOperandsMin,
                                info.numOperandsMax,
                                info.operandTypes,
                                info.hasSideEffects,
                                info.numSuccessors,
                                info.isTerminator));
    }

    EXPECT_EQ(memoryEffects(Opcode::ConstStr), MemoryEffects::Read);
}

TEST(IL, OpcodeVerifierStrategiesStayAssigned) {
    using il::core::Opcode;
    using il::verify::VerifyStrategy;

    EXPECT_EQ(il::verify::getInstructionSpec(Opcode::SDiv).strategy, VerifyStrategy::Reject);
    ASSERT_NE(il::verify::getInstructionSpec(Opcode::SDiv).rejectMessage, nullptr);
    EXPECT_NE(
        std::string{il::verify::getInstructionSpec(Opcode::SDiv).rejectMessage}.find("sdiv.chk0"),
        std::string::npos);

    EXPECT_EQ(il::verify::getInstructionSpec(Opcode::Alloca).strategy, VerifyStrategy::Alloca);
    EXPECT_EQ(il::verify::getInstructionSpec(Opcode::GEP).strategy, VerifyStrategy::GEP);
    EXPECT_EQ(il::verify::getInstructionSpec(Opcode::Load).strategy, VerifyStrategy::Load);
    EXPECT_EQ(il::verify::getInstructionSpec(Opcode::Store).strategy, VerifyStrategy::Store);
    EXPECT_EQ(il::verify::getInstructionSpec(Opcode::Call).strategy, VerifyStrategy::Call);
    EXPECT_EQ(il::verify::getInstructionSpec(Opcode::CallIndirect).strategy, VerifyStrategy::Call);
    EXPECT_EQ(il::verify::getInstructionSpec(Opcode::ConstNull).strategy,
              VerifyStrategy::ConstNull);
    EXPECT_EQ(il::verify::getInstructionSpec(Opcode::TrapKind).strategy, VerifyStrategy::TrapKind);
    EXPECT_EQ(il::verify::getInstructionSpec(Opcode::TrapErr).strategy, VerifyStrategy::TrapErr);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}

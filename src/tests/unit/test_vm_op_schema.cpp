//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_op_schema.cpp
// Purpose: Validate generated VM opcode schema metadata matches IL opcode info.
// Key invariants: Generated schema mirrors il::core::Opcode metadata and ensures
// Ownership/Lifetime: Tests operate on static tables only.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/core/OpcodeInfo.hpp"
#include "vm/ops/generated/HandlerTable.hpp"
#include "vm/ops/generated/OpSchema.hpp"

#include <cassert>
#include <string_view>

int main()
{
    const auto &schema = il::vm::generated::kOpSchema;
    const auto &handlers = il::vm::generated::opcodeHandlers();

    for (size_t idx = 0; idx < il::core::kNumOpcodes; ++idx)
    {
        const auto &info = il::core::kOpcodeTable[idx];
        const auto &entry = schema[idx];
        std::string_view mnemonic(entry.mnemonic ? entry.mnemonic : "");

        assert(!mnemonic.empty() && "opcode schema missing mnemonic");
        assert(entry.resultArity == info.resultArity && "result arity mismatch");
        assert(entry.resultType == info.resultType && "result type mismatch");
        assert(entry.operandMin == info.numOperandsMin && "operand min mismatch");
        assert(entry.operandMax == info.numOperandsMax && "operand max mismatch");
        for (size_t typeIdx = 0; typeIdx < il::core::kMaxOperandCategories; ++typeIdx)
        {
            assert(entry.operandTypes[typeIdx] == info.operandTypes[typeIdx] &&
                   "operand type mismatch");
        }
        assert(entry.hasSideEffects == info.hasSideEffects && "side effect flag mismatch");
        assert(entry.successors == info.numSuccessors && "successor count mismatch");
        assert(entry.terminator == info.isTerminator && "terminator flag mismatch");
        assert(entry.dispatch == info.vmDispatch && "dispatch kind mismatch");

        const bool handlerPresent = handlers[idx] != nullptr;
        assert(entry.hasHandler == handlerPresent && "handler presence mismatch");
        if (info.vmDispatch != il::core::VMDispatch::None)
        {
            assert(handlerPresent && "dispatchable opcode missing handler");
        }
    }

    return 0;
}

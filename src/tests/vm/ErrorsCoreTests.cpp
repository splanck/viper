//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/ErrorsCoreTests.cpp
// Purpose: Verify trap.kind emits structured trap diagnostics with kind, IP, and line info. 
// Key invariants: Diagnostics must include the requested trap kind, instruction index, and source
// Ownership/Lifetime: To be documented.
// Links: docs/specs/errors.md
//
//===----------------------------------------------------------------------===//

#include "common/TestIRBuilder.hpp"
#include "vm/err_bridge.hpp"

#include <array>
#include <cassert>
#include <string>

using namespace il::core;

namespace
{
std::string captureTrap(il::vm::TrapKind kind, int line)
{
    viper::tests::TestIRBuilder il;
    const il::support::SourceLoc trapLoc = il.loc(static_cast<uint32_t>(line));

    switch (kind)
    {
        case il::vm::TrapKind::DivideByZero:
            il.binary(
                Opcode::SDivChk0, Type(Type::Kind::I64), il.const_i64(1), il.const_i64(0), trapLoc);
            break;
        case il::vm::TrapKind::Bounds:
        {
            il::core::Instr trap;
            trap.op = Opcode::TrapFromErr;
            trap.type = Type(Type::Kind::I32);
            trap.operands.push_back(
                il.const_i64(static_cast<long long>(il::vm::ErrCode::Err_Bounds)));
            trap.loc = trapLoc;
            il.block().instructions.push_back(trap);
            break;
        }
        case il::vm::TrapKind::RuntimeError:
        {
            il::core::Instr trap;
            trap.op = Opcode::TrapFromErr;
            trap.type = Type(Type::Kind::I32);
            trap.operands.push_back(
                il.const_i64(static_cast<long long>(il::vm::ErrCode::Err_RuntimeError)));
            trap.loc = trapLoc;
            il.block().instructions.push_back(trap);
            break;
        }
        default:
        {
            il::core::Instr trap;
            trap.op = Opcode::Trap;
            trap.type = Type(Type::Kind::Void);
            trap.loc = trapLoc;
            il.block().instructions.push_back(trap);
            break;
        }
    }

    il.retVoid(trapLoc);
    return il.captureTrap();
}
} // namespace

int main()
{
    struct Sample
    {
        il::vm::TrapKind kind;
        int line;
        const char *token;
        int code;
    };

    const std::array<Sample, 3> samples = {
        {{il::vm::TrapKind::DivideByZero, 5, "DivideByZero", 0},
         {il::vm::TrapKind::Bounds, 9, "Bounds", static_cast<int>(il::vm::ErrCode::Err_Bounds)},
         {il::vm::TrapKind::RuntimeError,
          13,
          "RuntimeError",
          static_cast<int>(il::vm::ErrCode::Err_RuntimeError)}}};

    for (const auto &sample : samples)
    {
        const std::string out = captureTrap(sample.kind, sample.line);
        const bool hasKind = out.find(sample.token) != std::string::npos;
        assert(hasKind && "trap.kind diagnostic must include the trap kind");

        const bool hasIp = out.find("#0") != std::string::npos;
        assert(hasIp && "trap.kind diagnostic must include instruction index");

        const std::string lineToken = "line " + std::to_string(sample.line);
        const bool hasLine = out.find(lineToken) != std::string::npos;
        assert(hasLine && "trap.kind diagnostic must include source line");

        const std::string codeToken = "code=" + std::to_string(sample.code);
        const bool hasCode = out.find(codeToken) != std::string::npos;
        assert(hasCode && "trap diagnostic must include the expected code");
    }

    return 0;
}

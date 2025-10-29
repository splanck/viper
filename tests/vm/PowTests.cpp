// File: tests/vm/PowTests.cpp
// Purpose: Validate VM integration for the BASIC power operator semantics.
// Key invariants: Negative integral exponents succeed; fractional exponents on negative bases trap.
// Ownership/Lifetime: Builds ephemeral IL modules inside each test case.
// Links: docs/specs/numerics.md

#include "il/build/IRBuilder.hpp"
#include "il/runtime/RuntimeSignatures.hpp"
#include "tests/common/VmFixture.hpp"
#include "vm/Marshal.hpp"
#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <optional>
#include <string>

using namespace il::core;

namespace
{
void addPowExtern(Module &module)
{
    il::build::IRBuilder builder(module);
    builder.addExtern(
        "rt_pow_f64_chkdom", Type(Type::Kind::F64), {Type(Type::Kind::F64), Type(Type::Kind::F64)});
}

void buildPowSuccess(Module &module)
{
    addPowExtern(module);
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    const il::support::SourceLoc loc{1, 1, 1};
    const unsigned powRes = builder.reserveTempId();
    builder.emitCall("rt_pow_f64_chkdom",
                     {Value::constFloat(-2.0), Value::constFloat(3.0)},
                     Value::temp(powRes),
                     loc);

    Instr convert;
    convert.result = builder.reserveTempId();
    convert.op = Opcode::Fptosi;
    convert.type = Type(Type::Kind::I64);
    convert.operands.push_back(Value::temp(powRes));
    convert.loc = loc;
    bb.instructions.push_back(convert);

    const Value convVal = Value::temp(*convert.result);
    builder.emitRet(std::optional<Value>{convVal}, loc);
}

void buildPowDomainError(Module &module)
{
    addPowExtern(module);
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    const il::support::SourceLoc loc{1, 1, 1};
    builder.emitCall(
        "rt_pow_f64_chkdom", {Value::constFloat(-2.0), Value::constFloat(0.5)}, std::nullopt, loc);
    builder.emitRet(std::optional<Value>{Value::constInt(0)}, loc);
}

void buildPowOverflow(Module &module)
{
    addPowExtern(module);
    il::build::IRBuilder builder(module);
    auto &fn = builder.startFunction("main", Type(Type::Kind::I64), {});
    auto &bb = builder.addBlock(fn, "entry");
    builder.setInsertPoint(bb);

    const il::support::SourceLoc loc{1, 1, 1};
    builder.emitCall("rt_pow_f64_chkdom",
                     {Value::constFloat(2.0), Value::constFloat(4096.0)},
                     std::nullopt,
                     loc);
    builder.emitRet(std::optional<Value>{Value::constInt(0)}, loc);
}

} // namespace

int main()
{
    using viper::tests::VmFixture;

    VmFixture fixture;

    {
        Module module;
        buildPowSuccess(module);
        const int64_t rv = fixture.run(module);
        assert(rv == -8);
    }

    {
        Module module;
        buildPowDomainError(module);
        const std::string out = fixture.captureTrap(module);
        const bool ok = out.find("Trap @main") != std::string::npos &&
                        out.find("DomainError (code=0)") != std::string::npos;
        assert(ok && "expected DomainError trap for negative base fractional exponent");
    }

    {
        Module module;
        buildPowOverflow(module);
        const std::string out = fixture.captureTrap(module);
        const bool ok = out.find("Trap @main") != std::string::npos &&
                        out.find("Overflow (code=0)") != std::string::npos;
        assert(ok && "expected Overflow trap for large exponent");
    }

    {
        const auto *desc = il::runtime::findRuntimeDescriptor("rt_pow_f64_chkdom");
        assert(desc && "pow runtime descriptor must exist");

        il::vm::PowStatus powStatus{};
        std::array<il::vm::Slot, 2> args{};
        args[0].f64 = -2.0;
        args[1].f64 = 0.5;

        auto rawArgs =
            il::vm::marshalArguments(desc->signature, std::span<il::vm::Slot>(args), powStatus);
        assert(powStatus.active);
        const size_t statusIndex = desc->signature.paramTypes.size();
        assert(statusIndex < rawArgs.size());

        bool runtimeOk = true;
        auto **statusPtrPtr = reinterpret_cast<bool **>(rawArgs[statusIndex]);
        *statusPtrPtr = &runtimeOk;
        runtimeOk = false;

        il::vm::ResultBuffers buffers{};
        auto trap =
            il::vm::classifyPowTrap(*desc, powStatus, std::span<const il::vm::Slot>(args), buffers);
        assert(trap.triggered);
        assert(trap.kind == il::vm::TrapKind::DomainError);
    }

    {
        const auto *desc = il::runtime::findRuntimeDescriptor("rt_pow_f64_chkdom");
        assert(desc && "pow runtime descriptor must exist");

        il::vm::PowStatus powStatus{};
        std::array<il::vm::Slot, 2> args{};
        args[0].f64 = 2.0;
        args[1].f64 = 2048.0;

        auto rawArgs =
            il::vm::marshalArguments(desc->signature, std::span<il::vm::Slot>(args), powStatus);
        assert(powStatus.active);
        const size_t statusIndex = desc->signature.paramTypes.size();
        assert(statusIndex < rawArgs.size());

        bool runtimeOk = true;
        auto **statusPtrPtr = reinterpret_cast<bool **>(rawArgs[statusIndex]);
        *statusPtrPtr = &runtimeOk;
        runtimeOk = false;

        il::vm::ResultBuffers buffers{};
        auto trap =
            il::vm::classifyPowTrap(*desc, powStatus, std::span<const il::vm::Slot>(args), buffers);
        assert(trap.triggered);
        assert(trap.kind == il::vm::TrapKind::Overflow);
    }

    return 0;
}

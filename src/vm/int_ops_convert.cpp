// MIT License. See LICENSE in the project root for full license information.
// File: src/vm/int_ops_convert.cpp
// Purpose: Implement integer conversion opcode handlers, including range-checked
//          casts and boolean normalisation.
// Key invariants: Conversions respect IL range checks, trap semantics, and
//                 canonical `i1` representations.
// Links: docs/il-guide.md#reference §Conversions

#include "vm/OpHandlers_Int.hpp"

#include "vm/IntOpSupport.hpp"

#include "il/core/Opcode.hpp"

namespace il::vm::detail::integer
{
namespace
{
struct SignedNarrowCastTraits
{
    using WideType = int64_t;
    static constexpr const char *kOutOfRangeMessage = "value out of range in cast.si_narrow.chk";
    static constexpr const char *kUnsupportedTypeMessage =
        "unsupported target type in cast.si_narrow.chk";

    static WideType toWide(int64_t raw)
    {
        return raw;
    }

    static int64_t toStorage(WideType value)
    {
        return value;
    }

    template <typename NarrowT> static bool fits(WideType value)
    {
        return fitsSignedRange<NarrowT>(value);
    }

    template <typename NarrowT> static int64_t narrow(WideType value)
    {
        return static_cast<int64_t>(static_cast<NarrowT>(value));
    }

    static bool checkBoolean(WideType value)
    {
        return (value == 0) || (value == 1);
    }

    static int64_t booleanValue(WideType value)
    {
        return value & 1;
    }
};

struct UnsignedNarrowCastTraits
{
    using WideType = uint64_t;
    static constexpr const char *kOutOfRangeMessage = "value out of range in cast.ui_narrow.chk";
    static constexpr const char *kUnsupportedTypeMessage =
        "unsupported target type in cast.ui_narrow.chk";

    static WideType toWide(int64_t raw)
    {
        return static_cast<WideType>(raw);
    }

    static int64_t toStorage(WideType value)
    {
        return std::bit_cast<int64_t>(value);
    }

    template <typename NarrowT> static bool fits(WideType value)
    {
        return fitsUnsignedRange<NarrowT>(value);
    }

    template <typename NarrowT> static int64_t narrow(WideType value)
    {
        return static_cast<int64_t>(static_cast<NarrowT>(value));
    }

    static bool checkBoolean(WideType value)
    {
        return value <= 1;
    }

    static int64_t booleanValue(WideType value)
    {
        return static_cast<int64_t>(value & 1);
    }
};

template <typename Traits>
VM::ExecResult handleCastNarrowChkImpl(const Slot &value,
                                       Frame &fr,
                                       const il::core::Instr &in,
                                       const il::core::BasicBlock *bb)
{
    using Wide = typename Traits::WideType;
    const Wide operand = Traits::toWide(value.i64);

    auto trapOutOfRange = [&]()
    { emitTrap(TrapKind::InvalidCast, Traits::kOutOfRangeMessage, in, fr, bb); };

    int64_t narrowed = Traits::toStorage(operand);
    bool inRange = true;
    switch (in.type.kind)
    {
        case il::core::Type::Kind::I16:
            inRange = Traits::template fits<int16_t>(operand);
            if (inRange)
            {
                narrowed = Traits::template narrow<int16_t>(operand);
            }
            break;
        case il::core::Type::Kind::I32:
            inRange = Traits::template fits<int32_t>(operand);
            if (inRange)
            {
                narrowed = Traits::template narrow<int32_t>(operand);
            }
            break;
        case il::core::Type::Kind::I1:
            inRange = Traits::checkBoolean(operand);
            if (inRange)
            {
                narrowed = Traits::booleanValue(operand);
            }
            break;
        case il::core::Type::Kind::I64:
            break;
        default:
            emitTrap(TrapKind::InvalidCast, Traits::kUnsupportedTypeMessage, in, fr, bb);
            return {};
    }

    if (!inRange)
    {
        trapOutOfRange();
        return {};
    }

    Slot out{};
    out.i64 = narrowed;
    ops::storeResult(fr, in, out);
    return {};
}
} // namespace

VM::ExecResult handleCastSiNarrowChk(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)ip;
    const Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    return handleCastNarrowChkImpl<SignedNarrowCastTraits>(value, fr, in, bb);
}

VM::ExecResult handleCastUiNarrowChk(VM &vm,
                                     Frame &fr,
                                     const il::core::Instr &in,
                                     const VM::BlockMap &blocks,
                                     const il::core::BasicBlock *&bb,
                                     size_t &ip)
{
    (void)blocks;
    (void)ip;
    const Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    return handleCastNarrowChkImpl<UnsignedNarrowCastTraits>(value, fr, in, bb);
}

VM::ExecResult handleCastSiToFp(VM &vm,
                                Frame &fr,
                                const il::core::Instr &in,
                                const VM::BlockMap &blocks,
                                const il::core::BasicBlock *&bb,
                                size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    const Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    Slot out{};
    out.f64 = static_cast<double>(value.i64);
    ops::storeResult(fr, in, out);
    return {};
}

VM::ExecResult handleCastUiToFp(VM &vm,
                                Frame &fr,
                                const il::core::Instr &in,
                                const VM::BlockMap &blocks,
                                const il::core::BasicBlock *&bb,
                                size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    const Slot value = VMAccess::eval(vm, fr, in.operands[0]);
    const uint64_t operand = static_cast<uint64_t>(value.i64);
    Slot out{};
    out.f64 = static_cast<double>(operand);
    ops::storeResult(fr, in, out);
    return {};
}

VM::ExecResult handleTruncOrZext1(VM &vm,
                                  Frame &fr,
                                  const il::core::Instr &in,
                                  const VM::BlockMap &blocks,
                                  const il::core::BasicBlock *&bb,
                                  size_t &ip)
{
    (void)blocks;
    (void)bb;
    (void)ip;
    Slot operand = VMAccess::eval(vm, fr, in.operands[0]);
    const bool truthy = operand.i64 != 0;

    Slot result{};
    switch (in.op)
    {
        case il::core::Opcode::Trunc1:
        case il::core::Opcode::Zext1:
            result.i64 = truthy ? 1 : 0;
            break;
        default:
            result = operand;
            break;
    }

    ops::storeResult(fr, in, result);
    return {};
}
} // namespace il::vm::detail::integer

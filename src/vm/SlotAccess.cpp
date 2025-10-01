// File: src/vm/SlotAccess.cpp
// Purpose: Define helpers mapping IL types to VM slot storage and runtime buffers.
// Key invariants: Access tables must be updated when il::core::Type::Kind gains members.
// Ownership/Lifetime: Helpers operate on caller-owned slots and buffers without allocation.
// Links: docs/architecture.md#cpp-overview

#include "vm/SlotAccess.hpp"

#include "vm/VM.hpp"

#include <array>
#include <cassert>
#include <cstdint>

using il::core::Type;

namespace il::vm::slot_access
{
namespace
{

struct KindAccessors
{
    using SlotAccessor = void *(*)(Slot &);
    using ResultAccessor = void *(*)(ResultBuffers &);
    using ResultAssigner = void (*)(Slot &, const ResultBuffers &);
    using LoadFn = void (*)(Slot &, const void *);
    using StoreFn = void (*)(const Slot &, void *);

    SlotAccessor slotAccessor = nullptr;
    ResultAccessor resultAccessor = nullptr;
    ResultAssigner assignResult = nullptr;
    LoadFn load = nullptr;
    StoreFn store = nullptr;
};

constexpr void *nullResultBuffer(ResultBuffers &)
{
    return nullptr;
}

constexpr void assignNoop(Slot &, const ResultBuffers &)
{
}

template <auto Member>
constexpr void *slotMember(Slot &slot)
{
    return static_cast<void *>(&(slot.*Member));
}

template <auto Member>
constexpr void *bufferMember(ResultBuffers &buffers)
{
    return static_cast<void *>(&(buffers.*Member));
}

template <auto SlotMember, auto BufferMember>
constexpr void assignFromBuffer(Slot &slot, const ResultBuffers &buffers)
{
    slot.*SlotMember = buffers.*BufferMember;
}

constexpr void loadVoid(Slot &slot, const void *)
{
    slot.i64 = 0;
}

constexpr void storeVoid(const Slot &, void *)
{
}

constexpr void loadI1(Slot &slot, const void *ptr)
{
    const auto raw = *reinterpret_cast<const uint8_t *>(ptr);
    slot.i64 = static_cast<int64_t>(raw & 1u);
}

constexpr void storeI1(const Slot &slot, void *ptr)
{
    *reinterpret_cast<uint8_t *>(ptr) = static_cast<uint8_t>(slot.i64 != 0);
}

constexpr void loadI16(Slot &slot, const void *ptr)
{
    slot.i64 = static_cast<int64_t>(*reinterpret_cast<const int16_t *>(ptr));
}

constexpr void storeI16(const Slot &slot, void *ptr)
{
    *reinterpret_cast<int16_t *>(ptr) = static_cast<int16_t>(slot.i64);
}

constexpr void loadI32(Slot &slot, const void *ptr)
{
    slot.i64 = static_cast<int64_t>(*reinterpret_cast<const int32_t *>(ptr));
}

constexpr void storeI32(const Slot &slot, void *ptr)
{
    *reinterpret_cast<int32_t *>(ptr) = static_cast<int32_t>(slot.i64);
}

constexpr void loadI64(Slot &slot, const void *ptr)
{
    slot.i64 = *reinterpret_cast<const int64_t *>(ptr);
}

constexpr void storeI64(const Slot &slot, void *ptr)
{
    *reinterpret_cast<int64_t *>(ptr) = slot.i64;
}

constexpr void loadF64(Slot &slot, const void *ptr)
{
    slot.f64 = *reinterpret_cast<const double *>(ptr);
}

constexpr void storeF64(const Slot &slot, void *ptr)
{
    *reinterpret_cast<double *>(ptr) = slot.f64;
}

constexpr void loadPtr(Slot &slot, const void *ptr)
{
    slot.ptr = *reinterpret_cast<void *const *>(ptr);
}

constexpr void storePtr(const Slot &slot, void *ptr)
{
    *reinterpret_cast<void **>(ptr) = slot.ptr;
}

constexpr void loadStr(Slot &slot, const void *ptr)
{
    slot.str = *reinterpret_cast<rt_string const *>(ptr);
}

constexpr void storeStr(const Slot &slot, void *ptr)
{
    *reinterpret_cast<rt_string *>(ptr) = slot.str;
}

constexpr std::array<Type::Kind, 10> kSupportedKinds = {
    Type::Kind::Void,
    Type::Kind::I1,
    Type::Kind::I16,
    Type::Kind::I32,
    Type::Kind::I64,
    Type::Kind::F64,
    Type::Kind::Ptr,
    Type::Kind::Str,
    Type::Kind::Error,
    Type::Kind::ResumeTok,
};

static_assert(kSupportedKinds.size() == 10, "update SlotAccess when Type::Kind changes");

constexpr std::array<KindAccessors, kSupportedKinds.size()> buildTable()
{
    std::array<KindAccessors, kSupportedKinds.size()> table{};

    table[static_cast<size_t>(Type::Kind::Void)] = {
        nullptr, &nullResultBuffer, &assignNoop, &loadVoid, &storeVoid};

    table[static_cast<size_t>(Type::Kind::I1)] = {
        &slotMember<&Slot::i64>,
        &bufferMember<&ResultBuffers::i64>,
        &assignFromBuffer<&Slot::i64, &ResultBuffers::i64>,
        &loadI1,
        &storeI1};

    table[static_cast<size_t>(Type::Kind::I16)] = table[static_cast<size_t>(Type::Kind::I1)];
    table[static_cast<size_t>(Type::Kind::I16)].load = &loadI16;
    table[static_cast<size_t>(Type::Kind::I16)].store = &storeI16;

    table[static_cast<size_t>(Type::Kind::I32)] = table[static_cast<size_t>(Type::Kind::I1)];
    table[static_cast<size_t>(Type::Kind::I32)].load = &loadI32;
    table[static_cast<size_t>(Type::Kind::I32)].store = &storeI32;

    table[static_cast<size_t>(Type::Kind::I64)] = table[static_cast<size_t>(Type::Kind::I1)];
    table[static_cast<size_t>(Type::Kind::I64)].load = &loadI64;
    table[static_cast<size_t>(Type::Kind::I64)].store = &storeI64;

    table[static_cast<size_t>(Type::Kind::F64)] = {
        &slotMember<&Slot::f64>,
        &bufferMember<&ResultBuffers::f64>,
        &assignFromBuffer<&Slot::f64, &ResultBuffers::f64>,
        &loadF64,
        &storeF64};

    table[static_cast<size_t>(Type::Kind::Ptr)] = {
        &slotMember<&Slot::ptr>,
        &bufferMember<&ResultBuffers::ptr>,
        &assignFromBuffer<&Slot::ptr, &ResultBuffers::ptr>,
        &loadPtr,
        &storePtr};

    table[static_cast<size_t>(Type::Kind::Str)] = {
        &slotMember<&Slot::str>,
        &bufferMember<&ResultBuffers::str>,
        &assignFromBuffer<&Slot::str, &ResultBuffers::str>,
        &loadStr,
        &storeStr};

    table[static_cast<size_t>(Type::Kind::Error)] = {
        nullptr, &nullResultBuffer, &assignNoop, &loadVoid, &storeVoid};

    table[static_cast<size_t>(Type::Kind::ResumeTok)] = table[static_cast<size_t>(Type::Kind::Error)];

    return table;
}

constexpr auto kKindAccessors = buildTable();

const KindAccessors &dispatch(Type::Kind kind)
{
    const auto index = static_cast<size_t>(kind);
    assert(index < kKindAccessors.size() && "invalid type kind");
    return kKindAccessors[index];
}

} // namespace

void *slotPointer(Slot &slot, Type::Kind kind)
{
    const auto &entry = dispatch(kind);
    return entry.slotAccessor ? entry.slotAccessor(slot) : nullptr;
}

void *resultBuffer(Type::Kind kind, ResultBuffers &buffers)
{
    const auto &entry = dispatch(kind);
    return entry.resultAccessor ? entry.resultAccessor(buffers) : nullptr;
}

void assignResult(Slot &slot, Type::Kind kind, const ResultBuffers &buffers)
{
    const auto &entry = dispatch(kind);
    if (entry.assignResult)
        entry.assignResult(slot, buffers);
}

void loadFromPointer(Type::Kind kind, const void *ptr, Slot &out)
{
    const auto &entry = dispatch(kind);
    if (entry.load)
        entry.load(out, ptr);
}

void storeToPointer(Type::Kind kind, void *ptr, const Slot &value)
{
    const auto &entry = dispatch(kind);
    if (entry.store)
        entry.store(value, ptr);
}

} // namespace il::vm::slot_access

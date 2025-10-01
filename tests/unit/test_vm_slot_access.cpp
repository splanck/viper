// File: tests/unit/test_vm_slot_access.cpp
// Purpose: Validate shared SlotAccess helpers across VM runtime and memory ops.
// Key invariants: Each Type::Kind maps to the expected Slot member and memory layout.
// Ownership: Tests allocate local storage; runtime handles own buffers.
// Links: docs/codemap.md

#include "vm/SlotAccess.hpp"
#include "vm/VM.hpp"
#include "il/core/Type.hpp"

#include <cassert>
#include <cstdint>

using il::core::Type;
using il::vm::Slot;

int main()
{
    using il::vm::slot_access::assignResult;
    using il::vm::slot_access::loadFromPointer;
    using il::vm::slot_access::resultBuffer;
    using il::vm::slot_access::ResultBuffers;
    using il::vm::slot_access::slotPointer;
    using il::vm::slot_access::storeToPointer;

    // Slot pointer mappings for scalar kinds.
    {
        Slot slot{};
        slot.i64 = 42;
        void *ptr = slotPointer(slot, Type::Kind::I64);
        assert(ptr == &slot.i64);
        *reinterpret_cast<int64_t *>(ptr) = 7;
        assert(slot.i64 == 7);
    }
    {
        Slot slot{};
        slot.f64 = 1.25;
        void *ptr = slotPointer(slot, Type::Kind::F64);
        assert(ptr == &slot.f64);
        *reinterpret_cast<double *>(ptr) = 3.5;
        assert(slot.f64 == 3.5);
    }
    {
        Slot slot{};
        slot.ptr = reinterpret_cast<void *>(0x1234);
        void *ptr = slotPointer(slot, Type::Kind::Ptr);
        assert(ptr == &slot.ptr);
        *reinterpret_cast<void **>(ptr) = nullptr;
        assert(slot.ptr == nullptr);
    }
    {
        Slot slot{};
        slot.str = reinterpret_cast<rt_string>(0x1);
        void *ptr = slotPointer(slot, Type::Kind::Str);
        assert(ptr == &slot.str);
    }
    {
        Slot slot{};
        void *ptr = slotPointer(slot, Type::Kind::Void);
        assert(ptr == nullptr);
    }

    // Result buffer accessors and assignment.
    {
        ResultBuffers buffers{};
        void *ptr = resultBuffer(Type::Kind::F64, buffers);
        assert(ptr == &buffers.f64);
        buffers.f64 = -8.5;
        Slot slot{};
        assignResult(slot, Type::Kind::F64, buffers);
        assert(slot.f64 == -8.5);
    }
    {
        ResultBuffers buffers{};
        void *ptr = resultBuffer(Type::Kind::Void, buffers);
        assert(ptr == nullptr);
        Slot slot{};
        slot.i64 = 99;
        assignResult(slot, Type::Kind::Void, buffers);
        assert(slot.i64 == 99);
    }

    // Memory load/store for integer widths.
    {
        int16_t value = -123;
        Slot slot{};
        loadFromPointer(Type::Kind::I16, &value, slot);
        assert(slot.i64 == -123);
        slot.i64 = 456;
        storeToPointer(Type::Kind::I16, &value, slot);
        assert(value == static_cast<int16_t>(456));
    }
    {
        int32_t value = -9999;
        Slot slot{};
        loadFromPointer(Type::Kind::I32, &value, slot);
        assert(slot.i64 == -9999);
        slot.i64 = 77777;
        storeToPointer(Type::Kind::I32, &value, slot);
        assert(value == static_cast<int32_t>(77777));
    }
    {
        int64_t value = 0x1122334455667788ll;
        Slot slot{};
        loadFromPointer(Type::Kind::I64, &value, slot);
        assert(slot.i64 == 0x1122334455667788ll);
        slot.i64 = -1;
        storeToPointer(Type::Kind::I64, &value, slot);
        assert(value == -1);
    }
    {
        uint8_t value = 0xfeu;
        Slot slot{};
        loadFromPointer(Type::Kind::I1, &value, slot);
        assert(slot.i64 == 0);
        slot.i64 = 1;
        storeToPointer(Type::Kind::I1, &value, slot);
        assert(value == 1u);
    }

    // Floating-point load/store.
    {
        double value = -3.25;
        Slot slot{};
        loadFromPointer(Type::Kind::F64, &value, slot);
        assert(slot.f64 == -3.25);
        slot.f64 = 9.75;
        storeToPointer(Type::Kind::F64, &value, slot);
        assert(value == 9.75);
    }

    // Pointer load/store.
    {
        int payload = 17;
        void *stored = &payload;
        Slot slot{};
        loadFromPointer(Type::Kind::Ptr, &stored, slot);
        assert(slot.ptr == &payload);
        slot.ptr = nullptr;
        storeToPointer(Type::Kind::Ptr, &stored, slot);
        assert(stored == nullptr);
    }

    // String load/store.
    {
        alignas(void *) unsigned char storage[sizeof(void *)] = {};
        rt_string fake = reinterpret_cast<rt_string>(storage);
        rt_string stored = fake;
        Slot slot{};
        loadFromPointer(Type::Kind::Str, &stored, slot);
        assert(slot.str == fake);
        slot.str = nullptr;
        storeToPointer(Type::Kind::Str, &stored, slot);
        assert(stored == nullptr);
    }

    // Error and resume token kinds behave like void for loads/stores.
    {
        uintptr_t guard = 0xfeedfaceu;
        Slot slot{};
        loadFromPointer(Type::Kind::Error, &guard, slot);
        assert(slot.i64 == 0);
        storeToPointer(Type::Kind::Error, &guard, slot);
        assert(guard == 0xfeedfaceu);
    }
    {
        uintptr_t guard = 0xabad1deau;
        Slot slot{};
        loadFromPointer(Type::Kind::ResumeTok, &guard, slot);
        assert(slot.i64 == 0);
        storeToPointer(Type::Kind::ResumeTok, &guard, slot);
        assert(guard == 0xabad1deau);
    }

    // Void load leaves slot cleared.
    {
        uintptr_t guard = 0u;
        Slot slot{};
        slot.i64 = 55;
        loadFromPointer(Type::Kind::Void, &guard, slot);
        assert(slot.i64 == 0);
        storeToPointer(Type::Kind::Void, &guard, slot);
        assert(guard == 0u);
    }

    return 0;
}

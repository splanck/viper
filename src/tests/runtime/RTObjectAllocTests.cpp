//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTObjectAllocTests.cpp
// Purpose: Ensure rt_obj_new_i64 returns usable zero-initialized payloads. 
// Key invariants: Newly allocated object memory must be zero and writable.
// Ownership/Lifetime: Releases the object via rt_obj_release_check0 after validation.
// Links: docs/runtime-vm.md#runtime-abi
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main()
{
    const size_t payload_size = 32;
    uint8_t *payload = (uint8_t *)rt_obj_new_i64(42, (int64_t)payload_size);
    assert(payload != NULL);

    for (size_t i = 0; i < payload_size; ++i)
        assert(payload[i] == 0);

    payload[0] = 0x12;
    payload[payload_size - 1] = 0x34;
    assert(payload[0] == 0x12);
    assert(payload[payload_size - 1] == 0x34);

    int32_t released = rt_obj_release_check0(payload);
    assert(released == 1);
    assert(payload[0] == 0x12);
    assert(payload[payload_size - 1] == 0x34);
    rt_obj_free(payload);
    return 0;
}

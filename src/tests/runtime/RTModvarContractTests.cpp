//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTModvarContractTests.cpp
// Purpose: Validate indexed module-variable contracts and size safety.
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_context.h"
#include "rt_error.h"
#include "rt_modvar.h"
#include "rt_string.h"

#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace {

static RtContext g_ctx{};

static void expect_trap(void (*fn)(), int64_t expected_kind, const char *snippet) {
    jmp_buf env;
    rt_trap_set_recovery(&env);
    if (setjmp(env) == 0) {
        fn();
        rt_trap_clear_recovery();
        assert(false && "expected trap");
    }

    const char *message = rt_trap_get_error();
    assert(rt_trap_get_kind() == expected_kind);
    assert(message != nullptr);
    assert(strstr(message, snippet) != nullptr);
    rt_trap_clear_recovery();
}

static void trap_block_size_mismatch() {
    rt_string name = rt_const_cstr("shared_block");
    (void)rt_modvar_addr_block(name, 16);
    (void)rt_modvar_addr_block(name, 32);
}

static void trap_negative_block_size() {
    rt_string name = rt_const_cstr("neg_block");
    (void)rt_modvar_addr_block(name, -1);
}

static void test_ptr_and_block_use_distinct_slots(void) {
    rt_string name = rt_const_cstr("mixed_slot");
    void **ptr_slot = (void **)rt_modvar_addr_ptr(name);
    void *block_slot = rt_modvar_addr_block(name, 16);
    assert(ptr_slot != nullptr);
    assert(block_slot != nullptr);
    assert((void *)ptr_slot != block_slot);
}

} // namespace

int main() {
    rt_context_init(&g_ctx);
    rt_set_current_context(&g_ctx);

    test_ptr_and_block_use_distinct_slots();
    expect_trap(trap_block_size_mismatch, RT_TRAP_KIND_DOMAIN_ERROR, "storage size mismatch");
    expect_trap(trap_negative_block_size, RT_TRAP_KIND_DOMAIN_ERROR, "negative block size");

    rt_set_current_context(nullptr);
    rt_context_cleanup(&g_ctx);
    printf("RTModvarContractTests passed.\n");
    return 0;
}

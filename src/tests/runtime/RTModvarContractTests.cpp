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
#include "rt_internal.h"
#include "rt_modvar.h"
#include "rt_string.h"

#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace {

static RtContext g_ctx{};
static int g_alloc_fail_after = -1;

static void *fail_countdown_hook(int64_t bytes, void *(*next)(int64_t)) {
    if (g_alloc_fail_after == 0) {
        g_alloc_fail_after = -1;
        (void)bytes;
        return NULL;
    }
    if (g_alloc_fail_after > 0)
        --g_alloc_fail_after;
    return next ? next(bytes) : NULL;
}

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

static void trap_null_name() {
    (void)rt_modvar_addr_i64(NULL);
}

static void test_ptr_and_block_use_distinct_slots(void) {
    rt_string name = rt_const_cstr("mixed_slot");
    void **ptr_slot = (void **)rt_modvar_addr_ptr(name);
    void *block_slot = rt_modvar_addr_block(name, 16);
    assert(ptr_slot != nullptr);
    assert(block_slot != nullptr);
    assert((void *)ptr_slot != block_slot);
}

static void test_failed_create_does_not_publish_entry(int fail_after_allocs) {
    const char *raw_name = fail_after_allocs == 0 ? "oom_storage_slot" : "oom_name_slot";
    rt_string name = rt_string_from_bytes(raw_name, strlen(raw_name));
    assert(name != nullptr);
    size_t before = g_ctx.modvar_count;

    g_alloc_fail_after = fail_after_allocs;
    rt_set_alloc_hook(fail_countdown_hook);

    jmp_buf env;
    rt_trap_set_recovery(&env);
    if (setjmp(env) == 0) {
        (void)rt_modvar_addr_i64(name);
        rt_trap_clear_recovery();
        assert(false && "expected allocation trap");
    }

    const char *message = rt_trap_get_error();
    assert(message != nullptr);
    assert(strstr(message, "alloc") != nullptr);
    rt_trap_clear_recovery();
    rt_set_alloc_hook(NULL);
    g_alloc_fail_after = -1;

    assert(g_ctx.modvar_count == before);
    int64_t *slot = (int64_t *)rt_modvar_addr_i64(name);
    assert(slot != nullptr);
    *slot = 42;
    assert(g_ctx.modvar_count == before + 1);
    rt_string_unref(name);
}

} // namespace

int main() {
    rt_context_init(&g_ctx);
    rt_set_current_context(&g_ctx);

    test_ptr_and_block_use_distinct_slots();
    expect_trap(trap_block_size_mismatch, RT_TRAP_KIND_DOMAIN_ERROR, "storage size mismatch");
    expect_trap(trap_negative_block_size, RT_TRAP_KIND_DOMAIN_ERROR, "negative block size");
    expect_trap(trap_null_name, RT_TRAP_KIND_DOMAIN_ERROR, "null name");
    test_failed_create_does_not_publish_entry(0);
    test_failed_create_does_not_publish_entry(1);

    rt_set_current_context(nullptr);
    rt_context_cleanup(&g_ctx);
    printf("RTModvarContractTests passed.\n");
    return 0;
}

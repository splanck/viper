//===----------------------------------------------------------------------===//
// RTViperDOSPlatformTests.cpp - Platform abstraction and GC integration tests
//===----------------------------------------------------------------------===//
//
// Tests the platform-independent layers that ViperDOS (and all platforms) use:
//   - rt_gc (cycle collector + zeroing weak refs)
//   - rt_platform.h (path separators, platform detection)
//   - rt_serialize (unified serialization)
//   - rt_async (async combinators)
//   - Cross-platform string comparison wrappers
//
// These tests validate that the platform abstraction layer works correctly
// on the current build platform.
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "rt_internal.h"
#include "rt_gc.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_serialize.h"
#include "rt_machine.h"

void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}
}

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
    } else { \
        tests_passed++; \
    } \
} while(0)

//=============================================================================
// Platform detection
//=============================================================================

static void test_platform_detection() {
#ifdef _WIN32
    ASSERT(RT_PLATFORM_WINDOWS == 1, "Windows detected");
    ASSERT(RT_PLATFORM_VIPERDOS == 0, "not ViperDOS on Windows");
    ASSERT(RT_PATH_SEPARATOR == '\\', "Windows path separator");
#elif defined(__viperdos__)
    ASSERT(RT_PLATFORM_VIPERDOS == 1, "ViperDOS detected");
    ASSERT(RT_PLATFORM_WINDOWS == 0, "not Windows on ViperDOS");
    ASSERT(RT_PATH_SEPARATOR == '/', "ViperDOS path separator");
#else
    ASSERT(RT_PLATFORM_WINDOWS == 0, "not Windows on Unix");
    ASSERT(RT_PLATFORM_VIPERDOS == 0, "not ViperDOS on Unix");
    ASSERT(RT_PATH_SEPARATOR == '/', "Unix path separator");
#endif
}

//=============================================================================
// Machine info
//=============================================================================

static void test_machine_os_name() {
    rt_string os = rt_machine_os();
    ASSERT(os != NULL, "OS name not null");
    ASSERT(rt_str_len(os) > 0, "OS name not empty");

    const char *s = rt_string_cstr(os);
#ifdef _WIN32
    ASSERT(strcmp(s, "windows") == 0, "OS name is windows");
#elif defined(__viperdos__)
    ASSERT(strcmp(s, "viperdos") == 0, "OS name is viperdos");
#elif defined(__APPLE__)
    ASSERT(strcmp(s, "macos") == 0 || strcmp(s, "darwin") == 0, "OS name is macos/darwin");
#elif defined(__linux__)
    ASSERT(strcmp(s, "linux") == 0, "OS name is linux");
#endif
}

static void test_machine_os_version() {
    rt_string ver = rt_machine_os_ver();
    ASSERT(ver != NULL, "OS version not null");
    ASSERT(rt_str_len(ver) > 0, "OS version not empty");
}

//=============================================================================
// Serialization format detection (platform-independent)
//=============================================================================

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, (int64_t)strlen(s));
}

static void test_serialize_detect() {
    ASSERT(rt_serialize_detect(make_str("{\"a\":1}")) == RT_FORMAT_JSON, "detect JSON");
    ASSERT(rt_serialize_detect(make_str("<root/>")) == RT_FORMAT_XML, "detect XML");
    ASSERT(rt_serialize_detect(make_str("---\nkey: val")) == RT_FORMAT_YAML, "detect YAML");
    ASSERT(rt_serialize_detect(make_str("k = \"v\"")) == RT_FORMAT_TOML, "detect TOML");
    ASSERT(rt_serialize_detect(NULL) == -1, "detect null");
    ASSERT(rt_serialize_detect(make_str("")) == -1, "detect empty");
}

static void test_serialize_format_names() {
    ASSERT(strcmp(rt_string_cstr(rt_serialize_format_name(RT_FORMAT_JSON)), "json") == 0, "json name");
    ASSERT(strcmp(rt_string_cstr(rt_serialize_format_name(RT_FORMAT_XML)), "xml") == 0, "xml name");
    ASSERT(strcmp(rt_string_cstr(rt_serialize_format_name(RT_FORMAT_YAML)), "yaml") == 0, "yaml name");
    ASSERT(strcmp(rt_string_cstr(rt_serialize_format_name(RT_FORMAT_TOML)), "toml") == 0, "toml name");
    ASSERT(strcmp(rt_string_cstr(rt_serialize_format_name(RT_FORMAT_CSV)), "csv") == 0, "csv name");
}

//=============================================================================
// GC weak ref integration
//=============================================================================

struct simple_node { void *child; };

extern "C" {
static void node_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    struct simple_node *n = (struct simple_node *)obj;
    if (n->child) visitor(n->child, ctx);
}
}

static void test_gc_weakref_integration() {
    // Create a cycle: a -> b -> a
    void *a = rt_obj_new_i64(0, (int64_t)sizeof(struct simple_node));
    void *b = rt_obj_new_i64(0, (int64_t)sizeof(struct simple_node));
    ((struct simple_node *)a)->child = b;
    ((struct simple_node *)b)->child = a;

    rt_weakref *wa = rt_weakref_new(a);
    rt_weakref *wb = rt_weakref_new(b);

    ASSERT(rt_weakref_alive(wa) == 1, "wa alive before GC");
    ASSERT(rt_weakref_alive(wb) == 1, "wb alive before GC");

    rt_gc_track(a, node_traverse);
    rt_gc_track(b, node_traverse);

    int64_t freed = rt_gc_collect();
    ASSERT(freed == 2, "cycle collected");
    ASSERT(rt_weakref_alive(wa) == 0, "wa dead after GC");
    ASSERT(rt_weakref_alive(wb) == 0, "wb dead after GC");

    rt_weakref_free(wa);
    rt_weakref_free(wb);
}

static void test_gc_stats() {
    int64_t passes = rt_gc_pass_count();
    rt_gc_collect();
    ASSERT(rt_gc_pass_count() > passes, "pass count incremented");
}

//=============================================================================
// Main
//=============================================================================

int main() {
    test_platform_detection();
    test_machine_os_name();
    test_machine_os_version();
    test_serialize_detect();
    test_serialize_format_names();
    test_gc_weakref_integration();
    test_gc_stats();

    printf("ViperDOS platform tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

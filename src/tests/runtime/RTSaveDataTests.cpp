//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTSaveDataTests.cpp
// Purpose: Tests for SaveData key-value persistence system. Validates in-memory
//   operations (set/get/remove/clear/count), JSON save/load round-trip,
//   type safety (int vs string getters with defaults), and null safety.
// Key invariants:
//   - Keys are unique, last-write wins.
//   - GetInt on a string key returns the default (and vice versa).
//   - Save/Load are whole-file operations producing valid JSON.
//   - All functions are null-safe.
// Ownership/Lifetime:
//   - SaveData objects are GC-managed; tests rely on the runtime allocator.
// Links: rt_savedata.c, rt_savedata.h, docs/viperlib/game/persistence.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_savedata.h"

#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <process.h>
#include <direct.h>
#define GETPID _getpid
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define GETPID getpid
#endif

namespace {
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            (expr);                                                                                \
            g_trap_expected = false;                                                               \
            assert(!"Expected trap did not fire");                                                 \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

/// @brief Helper: create rt_string from C literal.
static rt_string S(const char *s) {
    return rt_const_cstr(s);
}

static bool string_bytes_equal(rt_string s, const char *bytes, size_t len) {
    return s && (size_t)rt_str_len(s) == len && memcmp(rt_string_cstr(s), bytes, len) == 0;
}

static void write_file_exact(const char *path, const char *bytes, size_t len) {
    FILE *fp = fopen(path, "wb");
    assert(fp != nullptr);
    size_t written = fwrite(bytes, 1, len, fp);
    fclose(fp);
    assert(written == len);
}

static void configure_test_save_root() {
    char root[256];
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp)
        tmp = "C:\\Temp";
    snprintf(root, sizeof(root), "%s\\viper_savedata_home_%d", tmp, (int)GETPID());
    _mkdir(root);
    _putenv_s("APPDATA", root);
    _putenv_s("USERPROFILE", root);
#else
    snprintf(root, sizeof(root), "/tmp/viper_savedata_home_%d", (int)GETPID());
    mkdir(root, 0755);
    setenv("HOME", root, 1);
#endif
}

// ============================================================================
// Null Safety Tests
// ============================================================================

static void test_null_safety() {
    printf("--- Null Safety ---\n");

    // All operations on NULL should not crash
    rt_savedata_set_int(nullptr, S("key"), 42);
    rt_savedata_set_string(nullptr, S("key"), S("val"));
    assert(rt_savedata_get_int(nullptr, S("key"), -1) == -1);
    assert(rt_savedata_has_key(nullptr, S("key")) == 0);
    assert(rt_savedata_remove(nullptr, S("key")) == 0);
    assert(rt_savedata_count(nullptr) == 0);
    assert(rt_savedata_save(nullptr) == 0);
    assert(rt_savedata_load(nullptr) == 0);
    rt_savedata_clear(nullptr);

    printf("  test_null_safety: PASSED\n");
}

static void test_empty_name_traps() {
    printf("--- Constructor Validation ---\n");

    EXPECT_TRAP(rt_savedata_new(S("")));
    printf("  test_empty_name_traps: PASSED\n");
}

// ============================================================================
// In-Memory Key-Value Operations
// ============================================================================

static void test_set_get_int() {
    void *sd = rt_savedata_new(S("test-int"));
    assert(sd != nullptr);

    rt_savedata_set_int(sd, S("score"), 42000);
    assert(rt_savedata_get_int(sd, S("score"), 0) == 42000);

    // Default for missing key
    assert(rt_savedata_get_int(sd, S("missing"), -1) == -1);

    // Overwrite
    rt_savedata_set_int(sd, S("score"), 99999);
    assert(rt_savedata_get_int(sd, S("score"), 0) == 99999);

    // Negative value
    rt_savedata_set_int(sd, S("negative"), -500);
    assert(rt_savedata_get_int(sd, S("negative"), 0) == -500);

    // Zero
    rt_savedata_set_int(sd, S("zero"), 0);
    assert(rt_savedata_get_int(sd, S("zero"), -1) == 0);

    printf("  test_set_get_int: PASSED\n");
}

static void test_set_get_string() {
    void *sd = rt_savedata_new(S("test-str"));
    assert(sd != nullptr);

    rt_savedata_set_string(sd, S("name"), S("ACE"));
    rt_string val = rt_savedata_get_string(sd, S("name"), S("default"));
    assert(strcmp(rt_string_cstr(val), "ACE") == 0);

    // Default for missing key
    rt_string missing = rt_savedata_get_string(sd, S("missing"), S("fallback"));
    assert(strcmp(rt_string_cstr(missing), "fallback") == 0);

    // Overwrite
    rt_savedata_set_string(sd, S("name"), S("PLAYER1"));
    rt_string updated = rt_savedata_get_string(sd, S("name"), S(""));
    assert(strcmp(rt_string_cstr(updated), "PLAYER1") == 0);

    // Empty string value
    rt_savedata_set_string(sd, S("empty_val"), S(""));
    rt_string empty = rt_savedata_get_string(sd, S("empty_val"), S("notfound"));
    assert(strcmp(rt_string_cstr(empty), "") == 0);

    printf("  test_set_get_string: PASSED\n");
}

static void test_type_mismatch_defaults() {
    void *sd = rt_savedata_new(S("test-types"));
    assert(sd != nullptr);

    // Set as int, get as string → returns default
    rt_savedata_set_int(sd, S("score"), 42000);
    rt_string s = rt_savedata_get_string(sd, S("score"), S("nope"));
    assert(strcmp(rt_string_cstr(s), "nope") == 0);

    // Set as string, get as int → returns default
    rt_savedata_set_string(sd, S("name"), S("ACE"));
    assert(rt_savedata_get_int(sd, S("name"), -1) == -1);

    printf("  test_type_mismatch_defaults: PASSED\n");
}

static void test_type_overwrite() {
    void *sd = rt_savedata_new(S("test-overwrite"));
    assert(sd != nullptr);

    // Set as int, then overwrite with string
    rt_savedata_set_int(sd, S("val"), 100);
    assert(rt_savedata_get_int(sd, S("val"), 0) == 100);

    rt_savedata_set_string(sd, S("val"), S("hello"));
    assert(rt_savedata_get_int(sd, S("val"), -1) == -1); // Now a string
    rt_string sv = rt_savedata_get_string(sd, S("val"), S(""));
    assert(strcmp(rt_string_cstr(sv), "hello") == 0);

    printf("  test_type_overwrite: PASSED\n");
}

// ============================================================================
// HasKey, Remove, Clear, Count
// ============================================================================

static void test_has_key() {
    void *sd = rt_savedata_new(S("test-haskey"));
    assert(sd != nullptr);

    assert(rt_savedata_has_key(sd, S("x")) == 0);

    rt_savedata_set_int(sd, S("x"), 10);
    assert(rt_savedata_has_key(sd, S("x")) == 1);
    assert(rt_savedata_has_key(sd, S("y")) == 0);

    printf("  test_has_key: PASSED\n");
}

static void test_remove() {
    void *sd = rt_savedata_new(S("test-remove"));
    assert(sd != nullptr);

    rt_savedata_set_int(sd, S("a"), 1);
    rt_savedata_set_int(sd, S("b"), 2);
    assert(rt_savedata_count(sd) == 2);

    // Remove existing
    assert(rt_savedata_remove(sd, S("a")) == 1);
    assert(rt_savedata_count(sd) == 1);
    assert(rt_savedata_has_key(sd, S("a")) == 0);
    assert(rt_savedata_has_key(sd, S("b")) == 1);

    // Remove non-existent
    assert(rt_savedata_remove(sd, S("a")) == 0);

    printf("  test_remove: PASSED\n");
}

static void test_clear() {
    void *sd = rt_savedata_new(S("test-clear"));
    assert(sd != nullptr);

    rt_savedata_set_int(sd, S("a"), 1);
    rt_savedata_set_string(sd, S("b"), S("two"));
    rt_savedata_set_int(sd, S("c"), 3);
    assert(rt_savedata_count(sd) == 3);

    rt_savedata_clear(sd);
    assert(rt_savedata_count(sd) == 0);
    assert(rt_savedata_has_key(sd, S("a")) == 0);

    printf("  test_clear: PASSED\n");
}

static void test_count() {
    void *sd = rt_savedata_new(S("test-count"));
    assert(sd != nullptr);

    assert(rt_savedata_count(sd) == 0);

    rt_savedata_set_int(sd, S("a"), 1);
    assert(rt_savedata_count(sd) == 1);

    rt_savedata_set_string(sd, S("b"), S("two"));
    assert(rt_savedata_count(sd) == 2);

    // Overwrite doesn't increase count
    rt_savedata_set_int(sd, S("a"), 99);
    assert(rt_savedata_count(sd) == 2);

    printf("  test_count: PASSED\n");
}

// ============================================================================
// Path Computation
// ============================================================================

static void test_path_contains_game_name() {
    void *sd = rt_savedata_new(S("mygame"));
    assert(sd != nullptr);

    rt_string path = rt_savedata_get_path(sd);
    const char *pcstr = rt_string_cstr(path);
    assert(pcstr != nullptr);
    assert(strstr(pcstr, "mygame") != nullptr);
    assert(strstr(pcstr, "save.json") != nullptr);

    printf("  test_path_contains_game_name: PASSED\n");
}

// ============================================================================
// Save / Load Round-Trip
// ============================================================================

static void test_save_load_round_trip() {
    /* Use a PID-unique game name to avoid collisions in parallel test runs */
    char game[64];
    snprintf(game, sizeof(game), "viper-test-%d", (int)GETPID());

    void *sd1 = rt_savedata_new(S(game));
    assert(sd1 != nullptr);

    // Populate
    rt_savedata_set_int(sd1, S("high_score"), 42000);
    rt_savedata_set_int(sd1, S("level"), 5);
    rt_savedata_set_string(sd1, S("player"), S("ACE"));
    rt_savedata_set_string(sd1, S("guild"), S("Viper Knights"));
    rt_savedata_set_int(sd1, S("negative"), -100);

    // Save
    int8_t save_ok = rt_savedata_save(sd1);
    assert(save_ok == 1);

    // Load into a fresh instance
    void *sd2 = rt_savedata_new(S(game));
    assert(sd2 != nullptr);
    assert(rt_savedata_count(sd2) == 0); // Empty before load

    int8_t load_ok = rt_savedata_load(sd2);
    assert(load_ok == 1);

    // Verify all values survived the round-trip
    assert(rt_savedata_get_int(sd2, S("high_score"), 0) == 42000);
    assert(rt_savedata_get_int(sd2, S("level"), 0) == 5);
    assert(rt_savedata_get_int(sd2, S("negative"), 0) == -100);

    rt_string p = rt_savedata_get_string(sd2, S("player"), S(""));
    assert(strcmp(rt_string_cstr(p), "ACE") == 0);

    rt_string g = rt_savedata_get_string(sd2, S("guild"), S(""));
    assert(strcmp(rt_string_cstr(g), "Viper Knights") == 0);

    assert(rt_savedata_count(sd2) == 5);

    // Cleanup: remove the save file
    rt_string path = rt_savedata_get_path(sd1);
    remove(rt_string_cstr(path));

    printf("  test_save_load_round_trip: PASSED\n");
}

static void test_load_nonexistent_returns_zero() {
    void *sd = rt_savedata_new(S("no-such-game-ever-9999"));
    assert(sd != nullptr);

    int8_t result = rt_savedata_load(sd);
    assert(result == 0); // File doesn't exist

    printf("  test_load_nonexistent_returns_zero: PASSED\n");
}

static void test_save_overwrite() {
    char game[64];
    snprintf(game, sizeof(game), "viper-overwrite-%d", (int)GETPID());

    void *sd = rt_savedata_new(S(game));
    assert(sd != nullptr);

    // First save
    rt_savedata_set_int(sd, S("score"), 100);
    assert(rt_savedata_save(sd) == 1);

    // Modify and save again
    rt_savedata_set_int(sd, S("score"), 200);
    rt_savedata_set_string(sd, S("name"), S("BOB"));
    assert(rt_savedata_save(sd) == 1);

    // Load into fresh instance — should see latest data
    void *sd2 = rt_savedata_new(S(game));
    assert(rt_savedata_load(sd2) == 1);
    assert(rt_savedata_get_int(sd2, S("score"), 0) == 200);

    rt_string n = rt_savedata_get_string(sd2, S("name"), S(""));
    assert(strcmp(rt_string_cstr(n), "BOB") == 0);

    // Cleanup
    rt_string path = rt_savedata_get_path(sd);
    remove(rt_string_cstr(path));

    printf("  test_save_overwrite: PASSED\n");
}

// ============================================================================
// Edge Cases
// ============================================================================

static void test_empty_key_ignored() {
    void *sd = rt_savedata_new(S("test-emptykey"));
    assert(sd != nullptr);

    // Empty key should be silently ignored
    rt_savedata_set_int(sd, S(""), 42);
    assert(rt_savedata_count(sd) == 0);

    rt_savedata_set_string(sd, S(""), S("val"));
    assert(rt_savedata_count(sd) == 0);

    printf("  test_empty_key_ignored: PASSED\n");
}

static void test_special_chars_in_values() {
    char game[64];
    snprintf(game, sizeof(game), "viper-special-%d", (int)GETPID());

    void *sd = rt_savedata_new(S(game));
    assert(sd != nullptr);

    // Values with JSON-sensitive characters
    rt_savedata_set_string(sd, S("quote"), S("He said \"hello\""));
    rt_savedata_set_string(sd, S("backslash"), S("C:\\Users\\test"));
    rt_savedata_set_string(sd, S("newline"), S("line1\nline2"));

    assert(rt_savedata_save(sd) == 1);

    // Round-trip
    void *sd2 = rt_savedata_new(S(game));
    assert(rt_savedata_load(sd2) == 1);

    rt_string q = rt_savedata_get_string(sd2, S("quote"), S(""));
    assert(strcmp(rt_string_cstr(q), "He said \"hello\"") == 0);

    rt_string b = rt_savedata_get_string(sd2, S("backslash"), S(""));
    assert(strcmp(rt_string_cstr(b), "C:\\Users\\test") == 0);

    rt_string nl = rt_savedata_get_string(sd2, S("newline"), S(""));
    assert(strcmp(rt_string_cstr(nl), "line1\nline2") == 0);

    // Cleanup
    rt_string path = rt_savedata_get_path(sd);
    remove(rt_string_cstr(path));

    printf("  test_special_chars_in_values: PASSED\n");
}

static void test_large_int_values() {
    void *sd = rt_savedata_new(S("test-large"));
    assert(sd != nullptr);

    rt_savedata_set_int(sd, S("max"), INT64_MAX);
    rt_savedata_set_int(sd, S("min"), INT64_MIN);

    assert(rt_savedata_get_int(sd, S("max"), 0) == INT64_MAX);
    assert(rt_savedata_get_int(sd, S("min"), 0) == INT64_MIN);

    printf("  test_large_int_values: PASSED\n");
}

static void test_binary_safe_string_round_trip() {
    char game[64];
    snprintf(game, sizeof(game), "viper-binary-%d", (int)GETPID());

    char key_bytes[] = {'b', 'i', 'n', '\0', 'k', 'e', 'y'};
    char value_bytes[] = {'A', '\0', 'B', 0x01, 'C'};
    rt_string key = rt_string_from_bytes(key_bytes, sizeof(key_bytes));
    rt_string value = rt_string_from_bytes(value_bytes, sizeof(value_bytes));

    void *sd = rt_savedata_new(S(game));
    assert(sd != nullptr);
    rt_savedata_set_string(sd, key, value);
    assert(rt_savedata_save(sd) == 1);

    void *sd2 = rt_savedata_new(S(game));
    assert(rt_savedata_load(sd2) == 1);

    rt_string loaded = rt_savedata_get_string(sd2, key, S(""));
    assert(string_bytes_equal(loaded, value_bytes, sizeof(value_bytes)));

    rt_string path = rt_savedata_get_path(sd);
    remove(rt_string_cstr(path));

    printf("  test_binary_safe_string_round_trip: PASSED\n");
}

static void test_load_rejects_malformed_json() {
    char game[64];
    snprintf(game, sizeof(game), "viper-malformed-%d", (int)GETPID());

    void *sd = rt_savedata_new(S(game));
    assert(sd != nullptr);
    rt_savedata_set_int(sd, S("score"), 7);
    assert(rt_savedata_save(sd) == 1);

    rt_string path = rt_savedata_get_path(sd);
    write_file_exact(rt_string_cstr(path), "{\"score\": 1", strlen("{\"score\": 1"));
    assert(rt_savedata_load(sd) == 0);
    assert(rt_savedata_get_int(sd, S("score"), -1) == 7);

    write_file_exact(rt_string_cstr(path), "{} trailing", strlen("{} trailing"));
    assert(rt_savedata_load(sd) == 0);
    assert(rt_savedata_get_int(sd, S("score"), -1) == 7);

    remove(rt_string_cstr(path));
    printf("  test_load_rejects_malformed_json: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== RTSaveDataTests ===\n\n");
    configure_test_save_root();

    test_null_safety();
    test_empty_name_traps();

    printf("\n--- Key-Value Operations ---\n");
    test_set_get_int();
    test_set_get_string();
    test_type_mismatch_defaults();
    test_type_overwrite();

    printf("\n--- HasKey / Remove / Clear / Count ---\n");
    test_has_key();
    test_remove();
    test_clear();
    test_count();

    printf("\n--- Path ---\n");
    test_path_contains_game_name();

    printf("\n--- Save / Load ---\n");
    test_save_load_round_trip();
    test_load_nonexistent_returns_zero();
    test_save_overwrite();

    printf("\n--- Edge Cases ---\n");
    test_empty_key_ignored();
    test_special_chars_in_values();
    test_large_int_values();
    test_binary_safe_string_round_trip();
    test_load_rejects_malformed_json();

    printf("\n=== All RTSaveDataTests passed! ===\n");
    return 0;
}

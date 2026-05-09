//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTSeqBoxTests.cpp
// Purpose: Validate Seq.Find/Has content-aware equality for boxed values.
// Key invariants: Boxed values are compared by content, not pointer identity.

#include "rt_box.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

extern "C" void rt_trap_set_recovery(jmp_buf *buf);
extern "C" void rt_trap_clear_recovery(void);
extern "C" const char *rt_trap_get_error(void);

static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

struct managed_value_payload {
    void *obj;
    rt_string str;
};

static int g_managed_value_child_finalized = 0;

static void managed_value_child_finalizer(void *obj) {
    (void)obj;
    g_managed_value_child_finalized++;
}

static void release_object(void *obj) {
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void expect_trap(void (*fn)(), const char *message) {
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        fn();
        rt_trap_clear_recovery();
        assert(false && "expected trap");
    } else {
        std::string text = rt_trap_get_error();
        rt_trap_clear_recovery();
        assert(text.find(message) != std::string::npos);
    }
}

//=============================================================================
// Seq.Find / Seq.Has with boxed strings
//=============================================================================

static void test_seq_find_boxed_strings() {
    printf("Testing Seq.Find/Has with boxed strings:\n");

    void *seq = rt_seq_new();

    void *apple1 = rt_box_str(rt_const_cstr("apple"));
    void *banana = rt_box_str(rt_const_cstr("banana"));
    void *cherry = rt_box_str(rt_const_cstr("cherry"));

    rt_seq_push(seq, apple1);
    rt_seq_push(seq, banana);
    rt_seq_push(seq, cherry);

    // Create DIFFERENT boxed strings with same content
    void *apple2 = rt_box_str(rt_const_cstr("apple"));
    void *banana2 = rt_box_str(rt_const_cstr("banana"));

    test_result("apple1 != apple2 (different pointers)", apple1 != apple2);
    test_result("Find apple2 returns 0", rt_seq_find(seq, apple2) == 0);
    test_result("Find banana2 returns 1", rt_seq_find(seq, banana2) == 1);
    test_result("Has apple2", rt_seq_has(seq, apple2) == 1);
    test_result("Has banana2", rt_seq_has(seq, banana2) == 1);

    // Non-existent
    void *grape = rt_box_str(rt_const_cstr("grape"));
    test_result("Find grape returns -1", rt_seq_find(seq, grape) == -1);
    test_result("Has grape is false", rt_seq_has(seq, grape) == 0);

    printf("\n");
}

//=============================================================================
// Seq.Find / Seq.Has with boxed integers
//=============================================================================

static void test_seq_find_boxed_integers() {
    printf("Testing Seq.Find/Has with boxed integers:\n");

    void *seq = rt_seq_new();

    void *i42a = rt_box_i64(42);
    void *i99 = rt_box_i64(99);
    void *i0 = rt_box_i64(0);

    rt_seq_push(seq, i42a);
    rt_seq_push(seq, i99);
    rt_seq_push(seq, i0);

    void *i42b = rt_box_i64(42);
    void *i99b = rt_box_i64(99);
    void *i0b = rt_box_i64(0);

    test_result("i42a != i42b (different pointers)", i42a != i42b);
    test_result("Find i42b returns 0", rt_seq_find(seq, i42b) == 0);
    test_result("Find i99b returns 1", rt_seq_find(seq, i99b) == 1);
    test_result("Find i0b returns 2", rt_seq_find(seq, i0b) == 2);
    test_result("Has i42b", rt_seq_has(seq, i42b) == 1);

    void *i77 = rt_box_i64(77);
    test_result("Find i77 returns -1", rt_seq_find(seq, i77) == -1);
    test_result("Has i77 is false", rt_seq_has(seq, i77) == 0);

    printf("\n");
}

//=============================================================================
// Seq.Find / Seq.Has with boxed floats
//=============================================================================

static void test_seq_find_boxed_floats() {
    printf("Testing Seq.Find/Has with boxed floats:\n");

    void *seq = rt_seq_new();

    void *f1a = rt_box_f64(3.14);
    void *f2 = rt_box_f64(2.718);
    rt_seq_push(seq, f1a);
    rt_seq_push(seq, f2);

    void *f1b = rt_box_f64(3.14);
    test_result("f1a != f1b (different pointers)", f1a != f1b);
    test_result("Find f1b returns 0", rt_seq_find(seq, f1b) == 0);
    test_result("Has f1b", rt_seq_has(seq, f1b) == 1);

    void *f3 = rt_box_f64(1.0);
    test_result("Find f3 returns -1", rt_seq_find(seq, f3) == -1);

    printf("\n");
}

//=============================================================================
// Seq.Find / Seq.Has with boxed booleans
//=============================================================================

static void test_seq_find_boxed_booleans() {
    printf("Testing Seq.Find/Has with boxed booleans:\n");

    void *seq = rt_seq_new();

    void *btrue1 = rt_box_i1(1);
    rt_seq_push(seq, btrue1);

    void *btrue2 = rt_box_i1(1);
    void *bfalse = rt_box_i1(0);

    test_result("btrue1 != btrue2 (different pointers)", btrue1 != btrue2);
    test_result("Has btrue2", rt_seq_has(seq, btrue2) == 1);
    test_result("Has bfalse is false", rt_seq_has(seq, bfalse) == 0);

    printf("\n");
}

//=============================================================================
// Pointer identity still works for non-boxed objects
//=============================================================================

static void test_seq_pointer_identity() {
    printf("Testing Seq.Find/Has with pointer identity (non-boxed):\n");

    void *seq = rt_seq_new();

    // Use the seq itself as a non-boxed element
    rt_seq_push(seq, seq);
    test_result("Has self (same pointer)", rt_seq_has(seq, seq) == 1);
    test_result("Find self returns 0", rt_seq_find(seq, seq) == 0);

    printf("\n");
}

static void test_value_type_managed_fields() {
    printf("Testing Box.ValueType managed field registration:\n");

    g_managed_value_child_finalized = 0;
    void *child = rt_obj_new_i64(0xB0A, 8);
    rt_obj_set_finalizer(child, managed_value_child_finalizer);
    rt_string text = rt_string_from_bytes("managed", 7);

    managed_value_payload *boxed =
        (managed_value_payload *)rt_box_value_type((int64_t)sizeof(managed_value_payload));
    test_result("ValueType class id", rt_obj_class_id(boxed) == RT_VALUE_TYPE_CLASS_ID);
    boxed->obj = child;
    boxed->str = text;
    rt_box_value_type_add_field(
        boxed, (int64_t)offsetof(managed_value_payload, obj), RT_VALUE_FIELD_OBJ, 1);
    rt_box_value_type_add_field(
        boxed, (int64_t)offsetof(managed_value_payload, str), RT_VALUE_FIELD_STR, 1);

    release_object(child);
    test_result("Child retained by ValueType", g_managed_value_child_finalized == 0);
    rt_string_unref(text);

    release_object(boxed);
    test_result("ValueType finalizer releases object field", g_managed_value_child_finalized == 1);
    printf("\n");
}

static managed_value_payload *g_conflict_value = nullptr;
static managed_value_payload *g_invalid_field_value = nullptr;
static void *g_misaligned_field_value = nullptr;

static void call_value_type_conflicting_field() {
    rt_box_value_type_add_field(
        g_conflict_value, (int64_t)offsetof(managed_value_payload, obj), RT_VALUE_FIELD_STR, 0);
}

static void call_value_type_invalid_retain_field() {
    rt_box_value_type_add_field(
        g_invalid_field_value, (int64_t)offsetof(managed_value_payload, str), RT_VALUE_FIELD_STR, 1);
}

static void call_value_type_misaligned_field() {
    rt_box_value_type_add_field(g_misaligned_field_value, 1, RT_VALUE_FIELD_OBJ, 0);
}

static void test_value_type_zero_size_and_duplicate_fields() {
    printf("Testing Box.ValueType zero-size and duplicate field validation:\n");

    void *empty = rt_box_value_type(0);
    test_result("Zero-size ValueType allocated", empty != nullptr);
    test_result("Zero-size ValueType class id", rt_obj_class_id(empty) == RT_VALUE_TYPE_CLASS_ID);
    release_object(empty);

    managed_value_payload *boxed =
        (managed_value_payload *)rt_box_value_type((int64_t)sizeof(managed_value_payload));
    g_conflict_value = boxed;
    rt_box_value_type_add_field(
        boxed, (int64_t)offsetof(managed_value_payload, obj), RT_VALUE_FIELD_OBJ, 0);
    rt_box_value_type_add_field(
        boxed, (int64_t)offsetof(managed_value_payload, obj), RT_VALUE_FIELD_OBJ, 0);
    expect_trap(call_value_type_conflicting_field, "field offset already registered");
    g_conflict_value = nullptr;
    release_object(boxed);

    managed_value_payload *invalid =
        (managed_value_payload *)rt_box_value_type((int64_t)sizeof(managed_value_payload));
    invalid->str = (rt_string)(uintptr_t)0x1234;
    g_invalid_field_value = invalid;
    expect_trap(call_value_type_invalid_retain_field, "invalid managed field value");
    g_invalid_field_value = nullptr;
    release_object(invalid);

    void *misaligned = rt_box_value_type((int64_t)(sizeof(void *) + 1));
    g_misaligned_field_value = misaligned;
    expect_trap(call_value_type_misaligned_field, "field offset is not pointer-aligned");
    g_misaligned_field_value = nullptr;
    release_object(misaligned);
    printf("\n");
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== Seq Box Content Equality Tests ===\n\n");

    test_seq_find_boxed_strings();
    test_seq_find_boxed_integers();
    test_seq_find_boxed_floats();
    test_seq_find_boxed_booleans();
    test_seq_pointer_identity();
    test_value_type_managed_fields();
    test_value_type_zero_size_and_duplicate_fields();

    printf("All Seq box equality tests passed!\n");
    return 0;
}

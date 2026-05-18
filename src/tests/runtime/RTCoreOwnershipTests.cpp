//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTCoreOwnershipTests.cpp
// Purpose: Validate core runtime ownership rules for copied C strings,
//          borrowed native pointers, and Option/Result retention.
//
//===----------------------------------------------------------------------===//

#include "rt_object.h"
#include "rt_heap.h"
#include "rt_option.h"
#include "rt_result.h"
#include "rt_string.h"
#include "il/runtime/HelperEffects.hpp"
#include "il/runtime/RuntimeOwnership.hpp"
#include "il/runtime/RuntimeSignatures.hpp"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

static int g_finalizer_count = 0;

static void release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void count_finalizer(void *obj) {
    (void)obj;
    ++g_finalizer_count;
}

static void test_const_cstr_copies_transient_input(void) {
    char buffer[] = "alpha";
    rt_string s = rt_const_cstr(buffer);
    assert(s != nullptr);
    buffer[0] = 'o';
    assert(strcmp(rt_string_cstr(s), "alpha") == 0);
    rt_string_unref(s);
}

static void test_foreign_pointers_are_borrowed(void) {
    int local = 42;
    assert(rt_obj_class_id(&local) == 0);
    rt_obj_retain_maybe(&local);
    assert(rt_obj_release_check0(&local) == 0);
    rt_obj_free(&local);

    void *opt = rt_option_some(&local);
    assert(opt != nullptr);
    release_object(opt);

    void *res = rt_result_err(&local);
    assert(res != nullptr);
    release_object(res);
}

static void test_non_object_heap_payloads_have_no_type_id(void) {
    void *payload = rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_U8, 1, 1, 1);
    assert(payload != nullptr);
    rt_heap_hdr_t *hdr = rt_heap_hdr(payload);
    hdr->class_id = 0x1234;

    assert(rt_obj_class_id(payload) == 0);
    assert(rt_obj_type_id(payload) == 0);

    assert(rt_memory_release(payload) == 0);
}

static void test_option_retains_runtime_objects(void) {
    g_finalizer_count = 0;
    void *payload = rt_obj_new_i64(0xC0DE, (int64_t)sizeof(int64_t));
    assert(payload != nullptr);
    rt_obj_set_finalizer(payload, count_finalizer);

    void *opt = rt_option_some(payload);
    assert(opt != nullptr);
    assert(rt_obj_release_check0(payload) == 0);
    assert(g_finalizer_count == 0);

    release_object(opt);
    assert(g_finalizer_count == 1);
}

static void test_result_retains_runtime_objects(void) {
    g_finalizer_count = 0;
    void *payload = rt_obj_new_i64(0xC0DF, (int64_t)sizeof(int64_t));
    assert(payload != nullptr);
    rt_obj_set_finalizer(payload, count_finalizer);

    void *res = rt_result_ok(payload);
    assert(res != nullptr);
    assert(rt_obj_release_check0(payload) == 0);
    assert(g_finalizer_count == 0);

    release_object(res);
    assert(g_finalizer_count == 1);
}

static void test_runtime_metadata_matches_core_contracts(void) {
    const auto retain = il::runtime::classifyRuntimeOwnership("Viper.Memory.Retain");
    assert(retain.retainsArg(0));
    const auto releaseStr = il::runtime::classifyRuntimeOwnership("Viper.Memory.ReleaseStr");
    assert(releaseStr.consumesArg(0));
    const auto releaseStrSym = il::runtime::classifyRuntimeOwnership("rt_memory_release_str");
    assert(releaseStrSym.consumesArg(0));

    const auto boxStr = il::runtime::classifyRuntimeOwnership("Viper.Core.Box.Str");
    assert(boxStr.retainsArg(0));
    assert(boxStr.returnsOwned);
    assert(boxStr.mayAllocate);
    const auto boxAliasI64 = il::runtime::classifyRuntimeOwnership("Viper.Box.I64");
    assert(boxAliasI64.returnsOwned);
    assert(boxAliasI64.mayAllocate);
    const auto boxAliasStr = il::runtime::classifyRuntimeOwnership("Viper.Box.Str");
    assert(boxAliasStr.retainsArg(0));
    assert(boxAliasStr.returnsOwned);
    assert(boxAliasStr.mayAllocate);

    const auto unboxStr = il::runtime::classifyRuntimeOwnership("Viper.Core.Box.ToStr");
    assert(unboxStr.returnsOwned);
    const auto unboxStrAlias = il::runtime::classifyRuntimeOwnership("Viper.Box.ToStr");
    assert(unboxStrAlias.returnsOwned);
    const auto tryToStr = il::runtime::classifyRuntimeOwnership("Viper.Core.Box.TryToStr");
    assert(tryToStr.returnsOwned);
    assert(tryToStr.mayAllocate);
    assert(!tryToStr.writesOwnedOutArg(1));
    const auto rawTryToStr = il::runtime::classifyRuntimeOwnership("rt_box_try_to_str");
    assert(rawTryToStr.writesOwnedOutArg(1));
    const auto tryToI64 = il::runtime::classifyRuntimeOwnership("Viper.Core.Box.TryToI64");
    assert(tryToI64.returnsOwned);
    assert(tryToI64.mayAllocate);
    const auto toStrOpt = il::runtime::classifyRuntimeOwnership("Viper.Core.Box.ToStrOption");
    assert(toStrOpt.returnsOwned);
    assert(toStrOpt.mayAllocate);
    const auto boxOptSym = il::runtime::classifyRuntimeOwnership("rt_box_to_f64_option");
    assert(boxOptSym.returnsOwned);
    assert(boxOptSym.mayAllocate);
    const auto objTypeName = il::runtime::classifyRuntimeOwnership("Viper.Core.Object.TypeName");
    assert(objTypeName.returnsOwned);
    assert(objTypeName.mayAllocate);
    const auto parseDoubleOwn = il::runtime::classifyRuntimeOwnership("Viper.Core.Parse.Double");
    assert(parseDoubleOwn.returnsOwned);
    assert(parseDoubleOwn.mayAllocate);
    const auto parseTryBool = il::runtime::classifyRuntimeOwnership("Viper.Core.Parse.TryBool");
    assert(parseTryBool.returnsOwned);
    assert(parseTryBool.mayAllocate);
    const auto parseAlias = il::runtime::classifyRuntimeOwnership("Viper.Parse.TryNum");
    assert(parseAlias.returnsOwned);
    assert(parseAlias.mayAllocate);
    const auto parseBoolSym = il::runtime::classifyRuntimeOwnership("rt_parse_bool_option");
    assert(parseBoolSym.returnsOwned);
    assert(parseBoolSym.mayAllocate);
    const auto parseOpt = il::runtime::classifyRuntimeOwnership("Viper.Core.Parse.Int64Option");
    assert(parseOpt.returnsOwned);
    assert(parseOpt.mayAllocate);
    const auto parseOptSym = il::runtime::classifyRuntimeOwnership("rt_parse_int64_option");
    assert(parseOptSym.returnsOwned);
    assert(parseOptSym.mayAllocate);
    const auto msgSub = il::runtime::classifyRuntimeOwnership("Viper.Core.MessageBus.Subscribe");
    assert(msgSub.retainsArg(1));
    assert(msgSub.retainsArg(2));
    const auto weakNew = il::runtime::classifyRuntimeOwnership("Viper.Memory.WeakRef.New");
    assert(weakNew.returnsOwned);
    assert(weakNew.mayAllocate);
    const auto weakGet = il::runtime::classifyRuntimeOwnership("Viper.Memory.WeakRef.Get");
    assert(weakGet.returnsOwned);
    const auto weakFree = il::runtime::classifyRuntimeOwnership("Viper.Memory.WeakRef.Free");
    assert(weakFree.consumesArg(0));
    const auto weakReset = il::runtime::classifyRuntimeOwnership("Viper.Memory.WeakRef.Reset");
    assert(weakReset.mayAllocate);
    const auto weakMapGet = il::runtime::classifyRuntimeOwnership("Viper.Collections.WeakMap.Get");
    assert(weakMapGet.returnsOwned);
    const auto listGet = il::runtime::classifyRuntimeOwnership("Viper.Collections.List.Get");
    assert(listGet.returnsOwned);
    const auto dequePeek =
        il::runtime::classifyRuntimeOwnership("Viper.Collections.Deque.PeekFront");
    assert(dequePeek.returnsOwned);
    const auto seqRemove = il::runtime::classifyRuntimeOwnership("Viper.Collections.Seq.Remove");
    assert(seqRemove.returnsOwned);
    const auto heapPop = il::runtime::classifyRuntimeOwnership("Viper.Collections.Heap.Pop");
    assert(heapPop.returnsOwned);
    const auto heapItems = il::runtime::classifyRuntimeOwnership("Viper.Collections.Heap.Items");
    assert(heapItems.returnsOwned);
    assert(heapItems.mayAllocate);
    const auto sortedFirst =
        il::runtime::classifyRuntimeOwnership("Viper.Collections.SortedSet.First");
    assert(sortedFirst.returnsOwned);
    assert(sortedFirst.mayAllocate);
    const auto bytesHex = il::runtime::classifyRuntimeOwnership("Viper.Collections.Bytes.ToHex");
    assert(bytesHex.returnsOwned);
    assert(bytesHex.mayAllocate);
    const auto orderedKey =
        il::runtime::classifyRuntimeOwnership("Viper.Collections.OrderedMap.KeyAt");
    assert(orderedKey.returnsOwned);
    assert(orderedKey.mayAllocate);
    const auto iterNext = il::runtime::classifyRuntimeOwnership("Viper.Collections.Iterator.Next");
    assert(iterNext.returnsOwned);
    const auto heapToSeq = il::runtime::classifyRuntimeOwnership("rt_pqueue_to_seq");
    assert(heapToSeq.returnsOwned);
    assert(heapToSeq.mayAllocate);

    const auto absI64 = il::runtime::classifyHelperEffects("rt_abs_i64");
    assert(absI64.known);
    assert(absI64.pure);
    assert(!absI64.nothrow);
    const auto toInt = il::runtime::classifyHelperEffects("rt_to_int");
    assert(toInt.known);
    assert(!toInt.nothrow);
    assert(!toInt.pure);
    const auto toDouble = il::runtime::classifyHelperEffects("rt_to_double");
    assert(toDouble.known);
    assert(!toDouble.nothrow);
    assert(!toDouble.pure);
    const auto strLen = il::runtime::classifyHelperEffects("rt_str_len");
    assert(strLen.known);
    assert(strLen.readonly);
    assert(!strLen.nothrow);

    const auto *parseDouble = il::runtime::findRuntimeSignature("Viper.Core.Parse.Double");
    assert(parseDouble != nullptr);
    assert(parseDouble->paramTypes.size() == 1);
    assert(parseDouble->retType.kind == il::core::Type::Kind::Ptr);
    assert(parseDouble->paramTypes[0].kind == il::core::Type::Kind::Str);
    const auto *parseInt64 = il::runtime::findRuntimeSignature("Viper.Core.Parse.Int64");
    assert(parseInt64 != nullptr);
    assert(parseInt64->paramTypes.size() == 1);
    assert(parseInt64->retType.kind == il::core::Type::Kind::Ptr);
    assert(parseInt64->paramTypes[0].kind == il::core::Type::Kind::Str);
}

} // namespace

int main() {
    test_const_cstr_copies_transient_input();
    test_foreign_pointers_are_borrowed();
    test_non_object_heap_payloads_have_no_type_id();
    test_option_retains_runtime_objects();
    test_result_retains_runtime_objects();
    test_runtime_metadata_matches_core_contracts();
    printf("RTCoreOwnershipTests passed.\n");
    return 0;
}

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/il/runtime/RuntimeOwnership.hpp
// Purpose: Centralize runtime reference ownership metadata for optimizer queries.
// Key invariants: Unknown helpers are classified conservatively with no
//                 ownership facts. Bitmasks refer to explicit IL-visible
//                 arguments, not hidden bridge parameters.
// Ownership/Lifetime: Header-only table; no dynamic storage.
// Links: docs/il-passes.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

namespace il::runtime {

/// @brief Ownership effects attached to a runtime helper call.
struct RuntimeOwnershipEffects {
    std::uint64_t consumedArgMask{0}; ///< Arguments whose ownership is consumed.
    std::uint64_t retainedArgMask{0}; ///< Arguments whose reference count is retained.
    std::uint64_t ownedOutArgMask{0}; ///< Pointer args that receive an owned reference.
    bool returnsOwned{false};         ///< Result is an owned string/reference handle.
    bool mayAllocate{false};          ///< Helper may allocate runtime-managed storage.

    /// @brief Query whether the helper consumes argument @p index.
    [[nodiscard]] constexpr bool consumesArg(unsigned index) const noexcept {
        return index < 64 && (consumedArgMask & (std::uint64_t{1} << index)) != 0;
    }

    /// @brief Query whether the helper retains argument @p index.
    [[nodiscard]] constexpr bool retainsArg(unsigned index) const noexcept {
        return index < 64 && (retainedArgMask & (std::uint64_t{1} << index)) != 0;
    }

    /// @brief Query whether pointer argument @p index receives an owned reference.
    [[nodiscard]] constexpr bool writesOwnedOutArg(unsigned index) const noexcept {
        return index < 64 && (ownedOutArgMask & (std::uint64_t{1} << index)) != 0;
    }

    /// @brief True when any ownership fact is known.
    [[nodiscard]] constexpr bool hasAny() const noexcept {
        return consumedArgMask != 0 || retainedArgMask != 0 || ownedOutArgMask != 0 ||
               returnsOwned || mayAllocate;
    }
};

namespace detail {

[[nodiscard]] constexpr bool startsWith(std::string_view value,
                                        std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] constexpr bool endsWith(std::string_view value, std::string_view suffix) noexcept {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size(), suffix.size()) == suffix;
}

[[nodiscard]] constexpr bool contains(std::string_view value, std::string_view needle) noexcept {
    return value.find(needle) != std::string_view::npos;
}

} // namespace detail

/// @brief Classify string/reference ownership effects for known runtime helpers.
/// @details This metadata prevents optimizers from treating owned string,
///          object, array, and collection construction/consumption as ordinary
///          value operations. Names include both C runtime symbols and
///          high-level runtime namespace aliases produced by frontends.
[[nodiscard]] inline RuntimeOwnershipEffects classifyRuntimeOwnership(std::string_view name) {
    RuntimeOwnershipEffects effects{};

    if (name == "rt_str_concat" || name == "Viper.String.Concat") {
        effects.consumedArgMask = 0b11;
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_str_release" || name == "rt_str_release_maybe" ||
        name == "rt_memory_release_str" || name == "Viper.String.ReleaseMaybe" ||
        name == "Viper.Memory.ReleaseStr") {
        effects.consumedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_str_retain" || name == "rt_str_retain_maybe" ||
        name == "rt_memory_retain_str" || name == "Viper.String.RetainMaybe" ||
        name == "Viper.Memory.RetainStr") {
        effects.retainedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_memory_release" || name == "Viper.Memory.Release") {
        effects.consumedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_memory_retain" || name == "Viper.Memory.Retain") {
        effects.retainedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_str_empty") {
        effects.returnsOwned = true;
        return effects;
    }

    if (name == "rt_str_substr" || name == "rt_csv_quote_alloc" ||
        name == "rt_str_split_fields" || name == "rt_int_to_str" ||
        name == "rt_f64_to_str" || name == "rt_str_i16_alloc" ||
        name == "rt_str_i32_alloc" || name == "rt_str_f_alloc" ||
        name == "rt_const_cstr" || name == "rt_str_from_lit" ||
        name == "rt_str_left" || name == "rt_str_right" || name == "rt_str_mid" ||
        name == "rt_str_mid_len" || name == "rt_str_ltrim" ||
        name == "rt_str_rtrim" || name == "rt_str_trim" ||
        name == "rt_str_ucase" || name == "rt_str_lcase" || name == "rt_str_chr" ||
        name == "rt_args_get" || name == "rt_cmdline" || name == "rt_getkey_str" ||
        name == "rt_inkey_str" || name == "rt_term_read_line" ||
        name == "rt_term_ask" || name == "Viper.String.Left" ||
        name == "Viper.String.Right" || name == "Viper.String.Mid2" ||
        name == "Viper.String.Mid3" || name == "Viper.String.LTrim" ||
        name == "Viper.String.RTrim" || name == "Viper.String.Trim" ||
        name == "Viper.String.UCase" || name == "Viper.String.LCase" ||
        name == "Viper.String.Chr" || name == "Viper.String.FromI64" ||
        name == "Viper.String.FromF64") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_arr_i32_new" || name == "rt_arr_i64_new" || name == "rt_arr_f64_new" ||
        name == "rt_arr_str_alloc" || name == "rt_arr_obj_new") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_arr_i32_retain" || name == "rt_arr_i64_retain" ||
        name == "rt_arr_f64_retain") {
        effects.retainedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_arr_i32_release" || name == "rt_arr_i64_release" ||
        name == "rt_arr_f64_release" || name == "rt_arr_str_release" ||
        name == "rt_arr_obj_release") {
        effects.consumedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_arr_str_get" || name == "rt_arr_obj_get") {
        effects.returnsOwned = true;
        return effects;
    }

    if (name == "rt_list_get" || name == "rt_list_first" || name == "rt_list_last" ||
        name == "rt_list_pop" || name == "Viper.Collections.List.Get" ||
        name == "Viper.Collections.List.First" || name == "Viper.Collections.List.Last" ||
        name == "Viper.Collections.List.Pop" || name == "rt_deque_get" ||
        name == "rt_deque_peek_front" || name == "rt_deque_peek_back" ||
        name == "rt_deque_pop_front" || name == "rt_deque_pop_back" ||
        name == "rt_deque_try_pop_front" || name == "rt_deque_try_pop_back" ||
        name == "Viper.Collections.Deque.Get" ||
        name == "Viper.Collections.Deque.PeekFront" ||
        name == "Viper.Collections.Deque.PeekBack" ||
        name == "Viper.Collections.Deque.First" ||
        name == "Viper.Collections.Deque.Last" ||
        name == "Viper.Collections.Deque.PopFront" ||
        name == "Viper.Collections.Deque.PopBack" ||
        name == "Viper.Collections.Deque.TryPopFront" ||
        name == "Viper.Collections.Deque.TryPopBack" || name == "rt_stack_pop" ||
        name == "rt_stack_try_pop" || name == "Viper.Collections.Stack.Pop" ||
        name == "Viper.Collections.Stack.TryPop" || name == "rt_queue_pop" ||
        name == "rt_queue_try_pop" || name == "Viper.Collections.Queue.Pop" ||
        name == "Viper.Collections.Queue.TryPop" || name == "rt_seq_pop" ||
        name == "rt_seq_remove" || name == "Viper.Collections.Seq.Pop" ||
        name == "Viper.Collections.Seq.Remove" || name == "rt_multimap_get_first" ||
        name == "Viper.Collections.MultiMap.GetFirst" || name == "rt_pqueue_pop" ||
        name == "rt_pqueue_try_pop" || name == "rt_pqueue_peek" ||
        name == "rt_pqueue_try_peek" || name == "Viper.Collections.Heap.Pop" ||
        name == "Viper.Collections.Heap.TryPop" ||
        name == "Viper.Collections.Heap.Peek" ||
        name == "Viper.Collections.Heap.TryPeek") {
        effects.returnsOwned = true;
        return effects;
    }

    if (name == "rt_iter_next" || name == "rt_iter_peek" ||
        name == "Viper.Collections.Iterator.Next" ||
        name == "Viper.Collections.Iterator.Peek") {
        effects.returnsOwned = true;
        return effects;
    }

    if (name == "rt_weakmap_get" || name == "Viper.Collections.WeakMap.Get") {
        effects.returnsOwned = true;
        return effects;
    }

    if (name == "rt_arr_str_put" || name == "rt_arr_obj_put") {
        effects.retainedArgMask = 0b100;
        return effects;
    }

    if (name == "rt_obj_new_i64" || name == "rt_box_i64" || name == "rt_box_f64" ||
        name == "rt_box_i1" || name == "rt_box_i1_bool" || name == "rt_box_value_type" ||
        name == "Viper.Core.Box.I64" || name == "Viper.Core.Box.F64" ||
        name == "Viper.Core.Box.I1" || name == "Viper.Core.Box.ValueType") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_box_str" || name == "Viper.Core.Box.Str") {
        effects.retainedArgMask = 0b1;
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_box_try_to_str" || name == "Viper.Core.Box.TryToStr") {
        effects.ownedOutArgMask = 0b10;
        return effects;
    }

    if (name == "rt_bitset_to_string" || name == "rt_bytes_to_str" ||
        name == "rt_bytes_to_hex" || name == "rt_bytes_to_base64" ||
        name == "rt_orderedmap_key_at" || name == "rt_trie_longest_prefix" ||
        name == "rt_sortedset_first" || name == "rt_sortedset_last" ||
        name == "rt_sortedset_floor" || name == "rt_sortedset_ceil" ||
        name == "rt_sortedset_lower" || name == "rt_sortedset_higher" ||
        name == "rt_sortedset_at" || name == "Viper.Collections.BitSet.ToString" ||
        name == "Viper.Collections.Bytes.ToStr" ||
        name == "Viper.Collections.Bytes.ToHex" ||
        name == "Viper.Collections.Bytes.ToBase64" ||
        name == "Viper.Collections.OrderedMap.KeyAt" ||
        name == "Viper.Collections.Trie.LongestPrefix" ||
        name == "Viper.Collections.SortedSet.First" ||
        name == "Viper.Collections.SortedSet.Last" ||
        name == "Viper.Collections.SortedSet.Floor" ||
        name == "Viper.Collections.SortedSet.Ceil" ||
        name == "Viper.Collections.SortedSet.Lower" ||
        name == "Viper.Collections.SortedSet.Higher" ||
        name == "Viper.Collections.SortedSet.At") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_map_keys" || name == "rt_map_values" || name == "rt_orderedmap_keys" ||
        name == "rt_orderedmap_values" || name == "rt_frozenmap_keys" ||
        name == "rt_frozenmap_values" || name == "rt_frozenset_items" ||
        name == "rt_bag_items" || name == "rt_bag_union" || name == "rt_bag_intersect" ||
        name == "rt_bag_diff" || name == "rt_set_items" || name == "rt_set_union" ||
        name == "rt_set_intersect" || name == "rt_set_diff" || name == "rt_sparse_indices" ||
        name == "rt_sparse_values" || name == "rt_multimap_get" ||
        name == "rt_multimap_keys" || name == "rt_intmap_keys" ||
        name == "rt_intmap_values" || name == "rt_countmap_keys" ||
        name == "rt_countmap_most_common" || name == "rt_lrucache_keys" ||
        name == "rt_lrucache_values" || name == "rt_weakmap_keys" ||
        name == "rt_pqueue_to_seq" || name == "rt_ring_to_seq" || name == "rt_deque_to_seq" ||
        name == "rt_deque_to_list" || name == "rt_stack_to_seq" ||
        name == "rt_stack_to_list" || name == "rt_queue_to_seq" ||
        name == "rt_queue_to_list" || name == "rt_list_to_seq" ||
        name == "rt_list_to_set" || name == "rt_list_to_stack" ||
        name == "rt_list_to_queue" || name == "rt_seq_to_list" ||
        name == "rt_seq_to_set" || name == "rt_seq_to_stack" ||
        name == "rt_seq_to_queue" || name == "rt_seq_to_deque" ||
        name == "rt_seq_to_bag" || name == "rt_seq_slice" || name == "rt_seq_take" ||
        name == "rt_seq_drop" || name == "rt_seq_keep_wrapper" ||
        name == "rt_seq_reject_wrapper" || name == "rt_seq_apply_wrapper" ||
        name == "rt_seq_take_while_wrapper" || name == "rt_seq_drop_while_wrapper" ||
        name == "rt_trie_keys" || name == "rt_trie_with_prefix" ||
        name == "rt_sortedset_items" || name == "rt_sortedset_range" ||
        name == "rt_sortedset_take" || name == "rt_sortedset_skip" ||
        name == "rt_sortedset_union" || name == "rt_sortedset_intersect" ||
        name == "rt_sortedset_diff" || name == "rt_iter_to_seq") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_unbox_str" || name == "Viper.Core.Box.ToStr" ||
        name == "rt_obj_to_string" || name == "Viper.Core.Object.ToString" ||
        name == "rt_obj_type_name" || name == "Viper.Core.Object.TypeName" ||
        name == "Viper.Core.Object.get_TypeName" || name == "rt_parse_double_option" ||
        name == "rt_parse_int64_option" || name == "Viper.Core.Parse.DoubleOption" ||
        name == "Viper.Core.Parse.Int64Option" || name == "Viper.Parse.DoubleOption" ||
        name == "Viper.Parse.Int64Option" || name == "Viper.Core.Convert.ToString_Int" ||
        name == "Viper.Core.Convert.ToString_Double" || name == "Viper.Convert.ToString_Int" ||
        name == "Viper.Convert.ToString_Double") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_msgbus_new" || name == "Viper.Core.MessageBus.New" ||
        name == "rt_msgbus_callback_new" || name == "Viper.Core.MessageBus.Callback" ||
        name == "rt_msgbus_topics" || name == "Viper.Core.MessageBus.Topics") {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (detail::startsWith(name, "Viper.Collections.") &&
        (detail::endsWith(name, ".Items") || detail::endsWith(name, ".Keys") ||
         detail::endsWith(name, ".Values") || detail::endsWith(name, ".ToSeq") ||
         detail::endsWith(name, ".ToList") || detail::endsWith(name, ".ToSet") ||
         detail::endsWith(name, ".ToStack") || detail::endsWith(name, ".ToQueue") ||
         detail::endsWith(name, ".Union") || detail::endsWith(name, ".Intersect") ||
         detail::endsWith(name, ".Diff") || detail::endsWith(name, ".Merge") ||
         detail::endsWith(name, ".Range") || detail::endsWith(name, ".Take") ||
         detail::endsWith(name, ".Skip") || detail::endsWith(name, ".And") ||
         detail::endsWith(name, ".Or") || detail::endsWith(name, ".Xor") ||
         detail::endsWith(name, ".Not") || detail::endsWith(name, ".Empty") ||
         detail::endsWith(name, ".WithCapacity") || detail::endsWith(name, ".NewMax") ||
         detail::endsWith(name, ".NewDefault"))) {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_msgbus_subscribe" || name == "Viper.Core.MessageBus.Subscribe") {
        effects.retainedArgMask = 0b110;
        effects.mayAllocate = true;
        return effects;
    }

    if (name == "rt_obj_retain_maybe" || name == "rt_obj_retain_known") {
        effects.retainedArgMask = 0b1;
        return effects;
    }

    if (name == "rt_obj_release_check0" || name == "rt_obj_release_known_check0") {
        effects.consumedArgMask = 0b1;
        return effects;
    }

    if (detail::startsWith(name, "rt_") &&
        (detail::endsWith(name, "_new") || detail::endsWith(name, "_clone") ||
         detail::contains(name, "_from_"))) {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    if (detail::startsWith(name, "Viper.") &&
        (detail::endsWith(name, ".New") || detail::endsWith(name, ".Clone") ||
         detail::contains(name, ".From"))) {
        effects.returnsOwned = true;
        effects.mayAllocate = true;
        return effects;
    }

    return effects;
}

} // namespace il::runtime

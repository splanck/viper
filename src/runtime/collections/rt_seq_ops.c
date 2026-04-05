//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_seq_ops.c
// Purpose: Sorting and higher-order functional operations for Viper.Collections.Seq.
//   Contains the merge sort implementation and all functional-style operations
//   (keep/reject/apply/all/any/none/fold/take/drop) that operate on sequences
//   without mutating the underlying storage layout.
//
// Key invariants:
//   - Sorting is stable (merge sort) with O(n log n) time and O(n) extra space.
//   - Functional operations that return new sequences (keep, reject, apply, take,
//     drop, take_while, drop_while) allocate via rt_seq_new/rt_seq_with_capacity
//     and push via rt_seq_push — they never touch capacity management directly.
//   - Predicate and transform functions are caller-provided; NULL predicates
//     produce documented default behavior (clone, true, etc.).
//   - Not thread-safe; external synchronization required for concurrent access.
//
// Ownership/Lifetime:
//   - New sequences returned by functional operations are GC-managed.
//   - Elements are shallow-copied (pointer only); ownership stays with the source.
//
// Links: src/runtime/collections/rt_seq_internal.h (struct definition),
//        src/runtime/collections/rt_seq.c (core operations),
//        src/runtime/collections/rt_seq_functional.c (void* wrapper layer)
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_seq_internal.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Sorting Implementation
//=============================================================================

/// @brief Default comparison function for sorting.
/// @details Compares elements as strings if they appear to be strings,
///          otherwise compares by pointer value. String comparison is
///          case-sensitive and lexicographic.
/// @param a First element pointer.
/// @param b Second element pointer.
/// @return Negative if a < b, zero if equal, positive if a > b.
static int64_t seq_default_compare(void *a, void *b) {
    // If both are NULL, they're equal
    if (!a && !b)
        return 0;
    // NULL sorts before non-NULL
    if (!a)
        return -1;
    if (!b)
        return 1;

    // Check if elements are strings using the runtime string checker
    if (rt_string_is_handle(a) && rt_string_is_handle(b)) {
        return rt_str_cmp((rt_string)a, (rt_string)b);
    }

    // Fall back to pointer comparison for non-strings
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

/// @brief Merge two sorted halves of an array.
/// @details Used by merge sort. Merges items[left..mid] and items[mid+1..right]
///          into a sorted sequence.
/// @param items Array of element pointers.
/// @param temp Temporary buffer for merging.
/// @param left Start index of left half.
/// @param mid End index of left half.
/// @param right End index of right half.
/// @param cmp Comparison function.
static void seq_merge(void **items,
                      void **temp,
                      int64_t left,
                      int64_t mid,
                      int64_t right,
                      int64_t (*cmp)(void *, void *)) {
    int64_t i = left;
    int64_t j = mid + 1;
    int64_t k = left;

    // Merge the two halves
    while (i <= mid && j <= right) {
        if (cmp(items[i], items[j]) <= 0) {
            temp[k++] = items[i++];
        } else {
            temp[k++] = items[j++];
        }
    }

    // Copy remaining elements from left half
    while (i <= mid) {
        temp[k++] = items[i++];
    }

    // Copy remaining elements from right half
    while (j <= right) {
        temp[k++] = items[j++];
    }

    // Copy back to original array
    for (int64_t x = left; x <= right; x++) {
        items[x] = temp[x];
    }
}

/// @brief Recursive merge sort implementation.
/// @param items Array of element pointers.
/// @param temp Temporary buffer.
/// @param left Start index.
/// @param right End index (inclusive).
/// @param cmp Comparison function.
static void seq_merge_sort(
    void **items, void **temp, int64_t left, int64_t right, int64_t (*cmp)(void *, void *)) {
    if (left >= right)
        return;

    int64_t mid = left + (right - left) / 2;
    seq_merge_sort(items, temp, left, mid, cmp);
    seq_merge_sort(items, temp, mid + 1, right, cmp);
    seq_merge(items, temp, left, mid, right, cmp);
}

/// @brief Sorts the elements in the Seq in ascending order.
///
/// Rearranges elements into ascending order using a stable merge sort algorithm.
/// Strings are compared lexicographically (case-sensitive). Non-string objects
/// are compared by their memory address (pointer value).
///
/// **Sorting behavior:**
/// - Strings: Lexicographic comparison ("a" < "b" < "z")
/// - Other objects: Pointer comparison (for consistent ordering)
/// - NULL values sort before non-NULL values
///
/// **Stability:**
/// The sort is stable, meaning equal elements maintain their relative order.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("cherry")
/// seq.Push("apple")
/// seq.Push("banana")
/// seq.Sort()
/// ' seq is now: ["apple", "banana", "cherry"]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, this is a no-op.
///
/// @note O(n log n) time complexity.
/// @note O(n) additional space for the merge operation.
/// @note Modifies the Seq in place.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_sort_desc For descending order
/// @see rt_seq_sort_by For custom comparison
void rt_seq_sort(void *obj) {
    rt_seq_sort_by(obj, seq_default_compare);
}

/// @brief Sorts the elements using a custom comparison function.
///
/// Rearranges elements into order determined by the provided comparison function.
/// The comparison function receives two element pointers and should return:
/// - Negative value if the first element should come before the second
/// - Zero if the elements are equal (stable sort preserves order)
/// - Positive value if the first element should come after the second
///
/// **Example with numbers (boxed):**
/// ```
/// Function CompareNumbers(a, b) As I64
///     Dim na = Unbox.I64(a)
///     Dim nb = Unbox.I64(b)
///     Return na - nb
/// End Function
///
/// Dim seq = Seq.New()
/// seq.Push(Box.I64(42))
/// seq.Push(Box.I64(17))
/// seq.Push(Box.I64(99))
/// seq.SortBy(AddressOf CompareNumbers)
/// ' seq is now: [17, 42, 99]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, this is a no-op.
/// @param cmp Comparison function. If NULL, uses default string/pointer comparison.
///
/// @note O(n log n) time complexity.
/// @note O(n) additional space for the merge operation.
/// @note Modifies the Seq in place.
/// @note The comparison function must be consistent (transitive ordering).
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_sort For default ascending sort
void rt_seq_sort_by(void *obj, int64_t (*cmp)(void *, void *)) {
    if (!obj)
        return;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    // Nothing to sort
    if (seq->len <= 1)
        return;

    // Use default comparison if none provided
    if (!cmp)
        cmp = seq_default_compare;

    // Allocate temporary buffer for merge sort
    void **temp = (void **)malloc((size_t)seq->len * sizeof(void *));
    if (!temp) {
        rt_trap("Seq.Sort: memory allocation failed");
        return;
    }

    // Perform stable merge sort
    seq_merge_sort(seq->items, temp, 0, seq->len - 1, cmp);

    free(temp);
}

/// @brief Comparison function for descending sort.
static int64_t seq_compare_desc(void *a, void *b) {
    return -seq_default_compare(a, b);
}

/// @brief Sorts the elements in the Seq in descending order.
///
/// Rearranges elements into descending order using a stable merge sort algorithm.
/// This is equivalent to calling Sort() followed by Reverse(), but more efficient.
///
/// **Example:**
/// ```
/// Dim seq = Seq.New()
/// seq.Push("apple")
/// seq.Push("cherry")
/// seq.Push("banana")
/// seq.SortDesc()
/// ' seq is now: ["cherry", "banana", "apple"]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, this is a no-op.
///
/// @note O(n log n) time complexity.
/// @note O(n) additional space for the merge operation.
/// @note Modifies the Seq in place.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_sort For ascending order
void rt_seq_sort_desc(void *obj) {
    rt_seq_sort_by(obj, seq_compare_desc);
}

//=============================================================================
// Functional Operations
//=============================================================================

/// @brief Create a new Seq containing only elements matching a predicate.
///
/// Iterates through the Seq and includes elements for which the predicate
/// function returns non-zero (true). This is the primary filtering operation.
///
/// **Example:**
/// ```
/// Function IsEven(n) As Bool
///     Return Unbox.I64(n) Mod 2 = 0
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(2))
/// nums.Push(Box.I64(3))
/// nums.Push(Box.I64(4))
/// Dim evens = nums.Keep(AddressOf IsEven)
/// ' evens is: [2, 4]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param pred Predicate function returning non-zero to include element.
///             If NULL, returns a clone of the original Seq.
///
/// @return New Seq containing only matching elements.
///
/// @note O(n) time complexity.
/// @note Creates a new Seq; original is not modified.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_reject For the inverse operation
void *rt_seq_keep(void *obj, int8_t (*pred)(void *)) {
    if (!obj)
        return rt_seq_new();

    if (!pred)
        return rt_seq_clone(obj);

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    void *result = rt_seq_new();

    for (int64_t i = 0; i < seq->len; i++) {
        if (pred(seq->items[i])) {
            rt_seq_push(result, seq->items[i]);
        }
    }

    return result;
}

/// @brief Create a new Seq excluding elements matching a predicate.
///
/// Inverse of Keep. Includes elements for which the predicate returns zero (false).
///
/// **Example:**
/// ```
/// Function IsEmpty(s) As Bool
///     Return Len(s) = 0
/// End Function
///
/// Dim words = Seq.New()
/// words.Push("hello")
/// words.Push("")
/// words.Push("world")
/// words.Push("")
/// Dim nonEmpty = words.Reject(AddressOf IsEmpty)
/// ' nonEmpty is: ["hello", "world"]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param pred Predicate function. Elements where this returns non-zero are excluded.
///             If NULL, returns a clone of the original Seq.
///
/// @return New Seq containing only non-matching elements.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_keep For the inverse operation
void *rt_seq_reject(void *obj, int8_t (*pred)(void *)) {
    if (!obj)
        return rt_seq_new();

    if (!pred)
        return rt_seq_clone(obj);

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    void *result = rt_seq_new();

    for (int64_t i = 0; i < seq->len; i++) {
        if (!pred(seq->items[i])) {
            rt_seq_push(result, seq->items[i]);
        }
    }

    return result;
}

/// @brief Create a new Seq by transforming each element with a function.
///
/// Applies the transform function to each element and collects the results
/// into a new Seq. This is the primary mapping operation.
///
/// **Example:**
/// ```
/// Function Double(n) As Object
///     Return Box.I64(Unbox.I64(n) * 2)
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(2))
/// nums.Push(Box.I64(3))
/// Dim doubled = nums.Apply(AddressOf Double)
/// ' doubled is: [2, 4, 6]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param fn Transform function. If NULL, returns a clone of the original.
///
/// @return New Seq containing transformed elements.
///
/// @note O(n) time complexity.
/// @note The transform function must return a valid object pointer.
/// @note Thread safety: Not thread-safe.
void *rt_seq_apply(void *obj, void *(*fn)(void *)) {
    if (!obj)
        return rt_seq_new();

    if (!fn)
        return rt_seq_clone(obj);

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    void *result = rt_seq_with_capacity(seq->len);

    for (int64_t i = 0; i < seq->len; i++) {
        rt_seq_push(result, fn(seq->items[i]));
    }

    return result;
}

/// @brief Check if all elements satisfy a predicate.
///
/// Returns true if the predicate returns non-zero for every element.
/// Returns true for empty sequences (vacuous truth).
///
/// **Example:**
/// ```
/// Function IsPositive(n) As Bool
///     Return Unbox.I64(n) > 0
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(2))
/// nums.Push(Box.I64(3))
/// Print nums.All(AddressOf IsPositive)  ' True
///
/// nums.Push(Box.I64(-1))
/// Print nums.All(AddressOf IsPositive)  ' False
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns 1 (true).
/// @param pred Predicate function. If NULL, returns 1 (true).
///
/// @return 1 if all elements match, 0 otherwise.
///
/// @note O(n) worst case, but short-circuits on first non-match.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_any For checking if any element matches
/// @see rt_seq_none For checking if no elements match
int8_t rt_seq_all(void *obj, int8_t (*pred)(void *)) {
    if (!obj || !pred)
        return 1;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    for (int64_t i = 0; i < seq->len; i++) {
        if (!pred(seq->items[i])) {
            return 0;
        }
    }

    return 1;
}

/// @brief Check if any element satisfies a predicate.
///
/// Returns true if the predicate returns non-zero for at least one element.
/// Returns false for empty sequences.
///
/// **Example:**
/// ```
/// Function IsNegative(n) As Bool
///     Return Unbox.I64(n) < 0
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(2))
/// Print nums.Any(AddressOf IsNegative)  ' False
///
/// nums.Push(Box.I64(-1))
/// Print nums.Any(AddressOf IsNegative)  ' True
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns 0 (false).
/// @param pred Predicate function. If NULL, returns 0 (false).
///
/// @return 1 if any element matches, 0 otherwise.
///
/// @note O(n) worst case, but short-circuits on first match.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_all For checking if all elements match
/// @see rt_seq_none For checking if no elements match
int8_t rt_seq_any(void *obj, int8_t (*pred)(void *)) {
    if (!obj || !pred)
        return 0;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    for (int64_t i = 0; i < seq->len; i++) {
        if (pred(seq->items[i])) {
            return 1;
        }
    }

    return 0;
}

/// @brief Check if no elements satisfy a predicate.
///
/// Returns true if the predicate returns zero for every element.
/// Returns true for empty sequences.
///
/// **Example:**
/// ```
/// Function IsNull(obj) As Bool
///     Return obj = Nothing
/// End Function
///
/// Dim items = Seq.New()
/// items.Push("a")
/// items.Push("b")
/// Print items.None(AddressOf IsNull)  ' True (no nulls)
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns 1 (true).
/// @param pred Predicate function. If NULL, returns 1 (true).
///
/// @return 1 if no elements match, 0 otherwise.
///
/// @note O(n) worst case, but short-circuits on first match.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_all For checking if all elements match
/// @see rt_seq_any For checking if any element matches
int8_t rt_seq_none(void *obj, int8_t (*pred)(void *)) {
    return !rt_seq_any(obj, pred);
}

/// @brief Count elements that satisfy a predicate.
///
/// **Example:**
/// ```
/// Function StartsWithA(s) As Bool
///     Return Left(s, 1) = "A"
/// End Function
///
/// Dim words = Seq.New()
/// words.Push("Apple")
/// words.Push("Banana")
/// words.Push("Apricot")
/// words.Push("Cherry")
/// Print words.CountWhere(AddressOf StartsWithA)  ' 2
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns 0.
/// @param pred Predicate function. If NULL, returns total length.
///
/// @return Number of elements for which predicate returns non-zero.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
int64_t rt_seq_count_where(void *obj, int8_t (*pred)(void *)) {
    if (!obj)
        return 0;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (!pred)
        return seq->len;

    int64_t count = 0;
    for (int64_t i = 0; i < seq->len; i++) {
        if (pred(seq->items[i])) {
            count++;
        }
    }

    return count;
}

/// @brief Find the first element satisfying a predicate.
///
/// **Example:**
/// ```
/// Function IsLong(s) As Bool
///     Return Len(s) > 5
/// End Function
///
/// Dim words = Seq.New()
/// words.Push("hi")
/// words.Push("hello")
/// words.Push("wonderful")
/// words.Push("world")
/// Dim found = words.FindWhere(AddressOf IsLong)
/// Print found  ' "wonderful"
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns NULL.
/// @param pred Predicate function. If NULL, returns first element or NULL.
///
/// @return First matching element, or NULL if none found.
///
/// @note O(n) worst case, but short-circuits on first match.
/// @note Thread safety: Not thread-safe.
void *rt_seq_find_where(void *obj, int8_t (*pred)(void *)) {
    if (!obj)
        return NULL;

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (seq->len == 0)
        return NULL;

    if (!pred)
        return seq->items[0];

    for (int64_t i = 0; i < seq->len; i++) {
        if (pred(seq->items[i])) {
            return seq->items[i];
        }
    }

    return NULL;
}

/// @brief Create a new Seq with the first N elements.
///
/// **Example:**
/// ```
/// Dim nums = Seq.New()
/// For i = 1 To 10
///     nums.Push(Box.I64(i))
/// Next
/// Dim first3 = nums.Take(3)
/// ' first3 is: [1, 2, 3]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param n Number of elements to take. Clamped to [0, len].
///
/// @return New Seq containing at most n elements from the start.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_drop For skipping elements
/// @see rt_seq_slice For arbitrary ranges
void *rt_seq_take(void *obj, int64_t n) {
    if (!obj || n <= 0)
        return rt_seq_new();

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (n >= seq->len)
        return rt_seq_clone(obj);

    return rt_seq_slice(obj, 0, n);
}

/// @brief Create a new Seq skipping the first N elements.
///
/// **Example:**
/// ```
/// Dim nums = Seq.New()
/// For i = 1 To 5
///     nums.Push(Box.I64(i))
/// Next
/// Dim rest = nums.Drop(2)
/// ' rest is: [3, 4, 5]
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param n Number of elements to skip. Clamped to [0, len].
///
/// @return New Seq containing elements after the first n.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_take For taking elements
/// @see rt_seq_slice For arbitrary ranges
void *rt_seq_drop(void *obj, int64_t n) {
    if (!obj)
        return rt_seq_new();

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    if (n <= 0)
        return rt_seq_clone(obj);

    if (n >= seq->len)
        return rt_seq_new();

    return rt_seq_slice(obj, n, seq->len);
}

/// @brief Create a new Seq with elements taken while predicate is true.
///
/// Takes elements from the start while the predicate returns non-zero.
/// Stops at the first element where predicate is false.
///
/// **Example:**
/// ```
/// Function LessThan5(n) As Bool
///     Return Unbox.I64(n) < 5
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(3))
/// nums.Push(Box.I64(7))
/// nums.Push(Box.I64(2))
/// Dim taken = nums.TakeWhile(AddressOf LessThan5)
/// ' taken is: [1, 3] (stops at 7)
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param pred Predicate function. If NULL, returns clone.
///
/// @return New Seq with leading elements matching predicate.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_drop_while For the inverse operation
void *rt_seq_take_while(void *obj, int8_t (*pred)(void *)) {
    if (!obj)
        return rt_seq_new();

    if (!pred)
        return rt_seq_clone(obj);

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    void *result = rt_seq_new();

    for (int64_t i = 0; i < seq->len; i++) {
        if (!pred(seq->items[i])) {
            break;
        }
        rt_seq_push(result, seq->items[i]);
    }

    return result;
}

/// @brief Create a new Seq skipping elements while predicate is true.
///
/// Skips elements from the start while the predicate returns non-zero.
/// Includes all elements from the first non-match onwards.
///
/// **Example:**
/// ```
/// Function LessThan5(n) As Bool
///     Return Unbox.I64(n) < 5
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(3))
/// nums.Push(Box.I64(7))
/// nums.Push(Box.I64(2))
/// Dim rest = nums.DropWhile(AddressOf LessThan5)
/// ' rest is: [7, 2] (skipped 1, 3)
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns empty Seq.
/// @param pred Predicate function. If NULL, returns empty Seq.
///
/// @return New Seq with elements after the leading matching ones.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_seq_take_while For the inverse operation
void *rt_seq_drop_while(void *obj, int8_t (*pred)(void *)) {
    if (!obj)
        return rt_seq_new();

    if (!pred)
        return rt_seq_new();

    rt_seq_impl *seq = (rt_seq_impl *)obj;

    // Find the first non-matching element
    int64_t start = 0;
    while (start < seq->len && pred(seq->items[start])) {
        start++;
    }

    return rt_seq_slice(obj, start, seq->len);
}

/// @brief Reduce the sequence to a single value using an accumulator.
///
/// Applies the reducer function to each element and an accumulator,
/// threading the result through as the new accumulator.
///
/// **Example:**
/// ```
/// Function Sum(acc, n) As Object
///     Return Box.I64(Unbox.I64(acc) + Unbox.I64(n))
/// End Function
///
/// Dim nums = Seq.New()
/// nums.Push(Box.I64(1))
/// nums.Push(Box.I64(2))
/// nums.Push(Box.I64(3))
/// nums.Push(Box.I64(4))
/// Dim total = nums.Fold(Box.I64(0), AddressOf Sum)
/// Print Unbox.I64(total)  ' 10
/// ```
///
/// @param obj Pointer to a Seq object. If NULL, returns init.
/// @param init Initial accumulator value.
/// @param fn Reducer function: fn(accumulator, element) -> new accumulator.
///           If NULL, returns init.
///
/// @return Final accumulated value.
///
/// @note O(n) time complexity.
/// @note Thread safety: Not thread-safe.
void *rt_seq_fold(void *obj, void *init, void *(*fn)(void *, void *)) {
    if (!obj || !fn)
        return init;

    rt_seq_impl *seq = (rt_seq_impl *)obj;
    void *acc = init;

    for (int64_t i = 0; i < seq->len; i++) {
        acc = fn(acc, seq->items[i]);
    }

    return acc;
}

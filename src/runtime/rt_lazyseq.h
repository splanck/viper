//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_lazyseq.h
// Purpose: Lazy sequence type for on-demand element generation and transformation.
// Key invariants: Sequences are single-pass unless reset; collectors may not terminate on infinite
// sequences. Ownership/Lifetime: Caller owns the handle; destroy with rt_lazyseq_destroy(). Links:
// docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_LAZYSEQ_H
#define VIPER_RT_LAZYSEQ_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Generator function type for producing sequence elements.
    /// @param state User-provided state (mutable).
    /// @param index Current index being requested (0-based).
    /// @param has_more Output parameter: set to 0 to indicate sequence end.
    /// @return The generated element, or NULL if sequence ended.
    typedef void *(*rt_lazyseq_gen_fn)(void *state, int64_t index, int8_t *has_more);

    /// Opaque handle to a LazySeq instance.
    typedef struct rt_lazyseq_impl *rt_lazyseq;

    //=============================================================================
    // Creation
    //=============================================================================

    /// @brief Create a lazy sequence from a generator function.
    /// @param gen Generator function that produces elements.
    /// @param state User-provided state passed to generator.
    /// @return A new LazySeq instance.
    rt_lazyseq rt_lazyseq_new(rt_lazyseq_gen_fn gen, void *state);

    /// @brief Create a lazy sequence that generates a range of integers.
    /// @param start Starting value (inclusive).
    /// @param end Ending value (exclusive), or INT64_MAX for infinite.
    /// @param step Step between values (must be non-zero).
    /// @return A new LazySeq generating integer pointers.
    rt_lazyseq rt_lazyseq_range(int64_t start, int64_t end, int64_t step);

    /// @brief Create a lazy sequence that repeats a value.
    /// @param value The value to repeat.
    /// @param count Number of repetitions, or -1 for infinite.
    /// @return A new LazySeq.
    rt_lazyseq rt_lazyseq_repeat(void *value, int64_t count);

    /// @brief Create a lazy sequence that iteratively applies a function.
    /// @param seed Initial value.
    /// @param fn Function to apply: next = fn(current).
    /// @return A new infinite LazySeq.
    rt_lazyseq rt_lazyseq_iterate(void *seed, void *(*fn)(void *));

    /// @brief Destroy a lazy sequence.
    /// @param seq The sequence to destroy.
    void rt_lazyseq_destroy(rt_lazyseq seq);

    //=============================================================================
    // Element Access
    //=============================================================================

    /// @brief Get the next element from the sequence.
    /// @param seq The lazy sequence.
    /// @param has_more Output: 1 if element returned, 0 if exhausted.
    /// @return The next element, or NULL if exhausted.
    void *rt_lazyseq_next(rt_lazyseq seq, int8_t *has_more);

    /// @brief Peek at the next element without consuming it.
    /// @param seq The lazy sequence.
    /// @param has_more Output: 1 if element available, 0 if exhausted.
    /// @return The next element, or NULL if exhausted.
    void *rt_lazyseq_peek(rt_lazyseq seq, int8_t *has_more);

    /// @brief Reset the sequence to the beginning.
    /// @param seq The lazy sequence.
    void rt_lazyseq_reset(rt_lazyseq seq);

    /// @brief Get the current index (number of elements consumed).
    /// @param seq The lazy sequence.
    /// @return Current index.
    int64_t rt_lazyseq_index(rt_lazyseq seq);

    /// @brief Check if the sequence is exhausted.
    /// @param seq The lazy sequence.
    /// @return 1 if exhausted, 0 if more elements available.
    int8_t rt_lazyseq_is_exhausted(rt_lazyseq seq);

    //=============================================================================
    // Transformations (return new lazy sequences)
    //=============================================================================

    /// @brief Create a lazy sequence that transforms each element.
    /// @param seq Source sequence.
    /// @param fn Transformation function.
    /// @return New LazySeq applying fn to each element.
    rt_lazyseq rt_lazyseq_map(rt_lazyseq seq, void *(*fn)(void *));

    /// @brief Create a lazy sequence that filters elements.
    /// @param seq Source sequence.
    /// @param pred Predicate function; include element if non-zero.
    /// @return New LazySeq containing only matching elements.
    rt_lazyseq rt_lazyseq_filter(rt_lazyseq seq, int8_t (*pred)(void *));

    /// @brief Create a lazy sequence taking only the first n elements.
    /// @param seq Source sequence.
    /// @param n Maximum number of elements.
    /// @return New bounded LazySeq.
    rt_lazyseq rt_lazyseq_take(rt_lazyseq seq, int64_t n);

    /// @brief Create a lazy sequence skipping the first n elements.
    /// @param seq Source sequence.
    /// @param n Number of elements to skip.
    /// @return New LazySeq starting after n elements.
    rt_lazyseq rt_lazyseq_drop(rt_lazyseq seq, int64_t n);

    /// @brief Create a lazy sequence taking elements while predicate is true.
    /// @param seq Source sequence.
    /// @param pred Predicate function.
    /// @return New LazySeq stopping at first non-match.
    rt_lazyseq rt_lazyseq_take_while(rt_lazyseq seq, int8_t (*pred)(void *));

    /// @brief Create a lazy sequence skipping elements while predicate is true.
    /// @param seq Source sequence.
    /// @param pred Predicate function.
    /// @return New LazySeq starting at first non-match.
    rt_lazyseq rt_lazyseq_drop_while(rt_lazyseq seq, int8_t (*pred)(void *));

    /// @brief Concatenate two lazy sequences.
    /// @param first First sequence.
    /// @param second Second sequence.
    /// @return New LazySeq yielding all of first, then all of second.
    rt_lazyseq rt_lazyseq_concat(rt_lazyseq first, rt_lazyseq second);

    /// @brief Zip two lazy sequences together.
    /// @param seq1 First sequence.
    /// @param seq2 Second sequence.
    /// @param combine Function combining pairs: combine(elem1, elem2).
    /// @return New LazySeq of combined elements.
    rt_lazyseq rt_lazyseq_zip(rt_lazyseq seq1, rt_lazyseq seq2, void *(*combine)(void *, void *));

    //=============================================================================
    // Collectors (consume sequence)
    //=============================================================================

    /// @brief Collect all elements into a Seq (array).
    /// @param seq The lazy sequence to collect.
    /// @return New Seq containing all elements.
    /// @warning May not terminate for infinite sequences!
    void *rt_lazyseq_to_seq(rt_lazyseq seq);

    /// @brief Collect up to n elements into a Seq.
    /// @param seq The lazy sequence.
    /// @param n Maximum elements to collect.
    /// @return New Seq containing at most n elements.
    void *rt_lazyseq_to_seq_n(rt_lazyseq seq, int64_t n);

    /// @brief Fold/reduce the sequence to a single value.
    /// @param seq The lazy sequence.
    /// @param init Initial accumulator.
    /// @param fn Reducer: fn(accumulator, element) -> new accumulator.
    /// @return Final accumulated value.
    /// @warning May not terminate for infinite sequences!
    void *rt_lazyseq_fold(rt_lazyseq seq, void *init, void *(*fn)(void *, void *));

    /// @brief Count elements in the sequence.
    /// @param seq The lazy sequence.
    /// @return Number of elements.
    /// @warning May not terminate for infinite sequences!
    int64_t rt_lazyseq_count(rt_lazyseq seq);

    /// @brief Execute a function for each element (side effects).
    /// @param seq The lazy sequence.
    /// @param fn Function to execute for each element.
    /// @warning May not terminate for infinite sequences!
    void rt_lazyseq_foreach(rt_lazyseq seq, void (*fn)(void *));

    /// @brief Find first element matching predicate.
    /// @param seq The lazy sequence.
    /// @param pred Predicate function.
    /// @param found Output: 1 if found, 0 if not.
    /// @return Matching element or NULL.
    void *rt_lazyseq_find(rt_lazyseq seq, int8_t (*pred)(void *), int8_t *found);

    /// @brief Check if any element matches predicate.
    /// @param seq The lazy sequence.
    /// @param pred Predicate function.
    /// @return 1 if any match, 0 otherwise.
    int8_t rt_lazyseq_any(rt_lazyseq seq, int8_t (*pred)(void *));

    /// @brief Check if all elements match predicate.
    /// @param seq The lazy sequence.
    /// @param pred Predicate function.
    /// @return 1 if all match, 0 otherwise.
    /// @warning May not terminate for infinite sequences!
    int8_t rt_lazyseq_all(rt_lazyseq seq, int8_t (*pred)(void *));

    //=============================================================================
    // IL ABI wrappers (void* interface for runtime signature handlers)
    //=============================================================================

    void *rt_lazyseq_w_range(int64_t start, int64_t end, int64_t step);
    void *rt_lazyseq_w_repeat(void *value, int64_t count);
    void *rt_lazyseq_w_next(void *seq);
    void *rt_lazyseq_w_peek(void *seq);
    void  rt_lazyseq_w_reset(void *seq);
    int64_t rt_lazyseq_w_index(void *seq);
    int8_t  rt_lazyseq_w_is_exhausted(void *seq);
    void *rt_lazyseq_w_take(void *seq, int64_t n);
    void *rt_lazyseq_w_drop(void *seq, int64_t n);
    void *rt_lazyseq_w_concat(void *first, void *second);
    void *rt_lazyseq_w_to_seq(void *seq);
    void *rt_lazyseq_w_to_seq_n(void *seq, int64_t n);
    int64_t rt_lazyseq_w_count(void *seq);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_LAZYSEQ_H

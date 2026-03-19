# Viper Codebase Audit — 2026-03-05

Comprehensive review of `src/runtime/`, `src/vm/`, `src/il/`, `src/codegen/` for bugs,
inefficiencies, and code quality issues. Focus on deep, hard-to-discover bugs.

---

## Findings Summary

| ID | Severity | File | Category | Status |
|----|----------|------|----------|--------|
| OVF-001 | HIGH | `rt_seq.c` | Integer overflow in capacity growth | FIXED |
| OVF-002 | HIGH | `rt_deque.c` | Integer overflow in capacity growth | FIXED |
| OVF-003 | HIGH | `rt_defaultmap.c` | Integer overflow in capacity growth | FIXED |
| OVF-004 | HIGH | `rt_sortedset.c` | Integer overflow in capacity growth | FIXED |
| OVF-005 | HIGH | `rt_sparsearray.c` | Integer overflow in capacity growth | FIXED |
| OVF-006 | HIGH | `rt_weakmap.c` | Integer overflow in capacity growth | FIXED |
| OVF-007 | MEDIUM | `rt_frozenmap.c` | Integer overflow in capacity calc | FIXED |
| OVF-008 | MEDIUM | `rt_frozenset.c` | Integer overflow in capacity calc | FIXED |
| OVF-009 | HIGH | `rt_set.c` | Integer overflow in capacity growth | FIXED |
| OVF-010 | HIGH | `rt_map.c` | Integer overflow in capacity growth | FIXED |
| OVF-011 | HIGH | `rt_intmap.c` | Integer overflow in capacity growth | FIXED |
| OVF-012 | HIGH | `rt_bag.c` | Integer overflow in capacity growth | FIXED |
| OVF-013 | HIGH | `rt_multimap.c` | Integer overflow in capacity growth | FIXED |
| OVF-014 | HIGH | `rt_treemap.c` | Integer overflow in capacity growth | FIXED |
| OVF-015 | HIGH | `rt_countmap.c` | Integer overflow in capacity growth | FIXED |
| OVF-016 | HIGH | `rt_bimap.c` (×2) | Integer overflow in capacity growth | FIXED |
| TRUNC-001 | MEDIUM | `Mem2Reg.cpp` | Unchecked int64→unsigned truncation | FIXED |
| DOM-001 | LOW | `Dominators.cpp` | Missing defensive assert in intersect | FIXED |
| DOC-001 | LOW | `rt_deque.c` | Missing doc comments on public API | FIXED |
| DOC-002 | LOW | `Allocator.cpp` | Missing perf rationale comment | FIXED |

---

## Detailed Findings

### OVF-001 through OVF-016: Capacity Overflow in Collection Growth

**Category:** Integer overflow
**Severity:** HIGH
**Impact:** When capacity approaches `INT64_MAX/2` or `SIZE_MAX/2`, the `cap * 2`
doubling wraps to negative (int64) or zero (size_t). For int64 with a while loop
(e.g., rt_seq.c), this causes an infinite loop. For size_t, it produces a zero or
tiny allocation that subsequent writes overflow.

**Root cause:** Every collection doubles capacity on growth, but 16 of 19 files lack
an overflow guard before the multiplication. Three files already have the guard:
`rt_orderedmap.c`, `rt_binbuf.c`, `rt_string_builder.c`.

**Fix:** Add 2-line guard before each `* 2` operation:
- int64_t: `if (cap > INT64_MAX / 2) rt_trap("...: capacity overflow");`
- size_t: `if (cap > SIZE_MAX / 2) rt_trap("...: capacity overflow");`

**Files (16):**
1. `src/runtime/collections/rt_seq.c:152-156` — while loop multiply
2. `src/runtime/collections/rt_deque.c:71` — `d->cap * 2`
3. `src/runtime/collections/rt_defaultmap.c:89` — `m->capacity * 2`
4. `src/runtime/collections/rt_sortedset.c:118-120` — while loop multiply
5. `src/runtime/collections/rt_sparsearray.c:128` — `old_cap * 2`
6. `src/runtime/collections/rt_weakmap.c:101` — `old_cap * 2`
7. `src/runtime/collections/rt_frozenmap.c:122-124` — `next_pow2` helper
8. `src/runtime/collections/rt_frozenset.c:115-118` — `next_pow2` helper
9. `src/runtime/collections/rt_set.c:83` — `set->capacity * 2`
10. `src/runtime/collections/rt_map.c:237` — `map->capacity * 2`
11. `src/runtime/collections/rt_intmap.c:149` — `map->capacity * 2`
12. `src/runtime/collections/rt_bag.c:233` — `bag->capacity * 2`
13. `src/runtime/collections/rt_multimap.c:127` — `mm->capacity * 2`
14. `src/runtime/collections/rt_treemap.c:192` — `tm->capacity * 2`
15. `src/runtime/collections/rt_countmap.c:119` — `cm->capacity * 2`
16. `src/runtime/collections/rt_bimap.c:183,209` — fwd and inv capacity

### TRUNC-001: Unchecked int64→unsigned truncation in Mem2Reg

**File:** `src/il/transform/Mem2Reg.cpp:94-98`
**Category:** Silent data truncation
**Severity:** MEDIUM

`constOffset()` checks `v.i64 < 0` but not `v.i64 > UINT_MAX`. Values above 4GB
silently truncate via `static_cast<unsigned>`, potentially matching wrong SROA field
offsets.

**Fix:** Add upper bound check: `if (v.i64 > (int64_t)UINT_MAX) return nullopt;`

### DOM-001: Dominator intersect missing defensive assert

**File:** `src/il/analysis/Dominators.cpp:125-134`
**Category:** Defensive programming
**Severity:** LOW

The intersect lambda follows `DT.idom[]` chains without verifying entries exist.
Caller guards with `contains()`, but malformed CFGs could bypass the guard.

**Fix:** Add `assert(DT.idom.count(b))` inside the while loops.

### DOC-001: Deque public API missing doc comments

**File:** `src/runtime/collections/rt_deque.c`
**Category:** Documentation
**Severity:** LOW

15 public functions lack doc comments. All other collections (seq, map, set, etc.)
have comprehensive documentation.

### DOC-002: Allocator reserveForCall missing rationale

**File:** `src/codegen/x86_64/ra/Allocator.cpp:1185-1200`
**Category:** Documentation
**Severity:** LOW

Linear search on `reservedForCall_` looks like O(n²) but is fine because n ≤ 6
(x86-64 argument registers). Needs a comment explaining this.

---

## Fix Verification

All 20 findings fixed. Build and test results:

- **Build:** Clean (zero warnings from changes)
- **Tests:** 1259/1259 passed (100%)
- **Date:** 2026-03-05

### Fixes Applied

| IDs | Fix |
|-----|-----|
| OVF-001 through OVF-008 | Added `INT64_MAX / 2` overflow guard before each `cap * 2` |
| OVF-009 through OVF-016 | Added `SIZE_MAX / 2` overflow guard before each `cap * 2` |
| TRUNC-001 | Added `v.i64 > UINT_MAX` upper bound check in `constOffset()` |
| DOM-001 | Added `assert(DT.idom.count(...))` in intersect lambda |
| DOC-001 | Added `@brief`, `@param`, `@return`, `@note` to 15 deque functions |
| DOC-002 | Added comment explaining n≤6 linear search is intentional |

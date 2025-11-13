# VIPER BASIC — Resolved Bugs

Last Updated: 2025-11-13

This document records bugs from devdocs/basic_bugs.md that have been verified and resolved in code, with notes on scope, approach, and validation.

---

## BUG-007: Multi-dimensional arrays parsing and lowering
- Status: RESOLVED (Parser + Analyzer + Lowerer support multi-dim)
- Summary: DIM accepts comma-separated extents (e.g., `DIM A(2,3,4)`), analyzer records per-dimension extents and total, and the lowerer flattens multi-dimensional indexing to a single zero-based index using row-major order when metadata is available. REDIM is supported for single-dimension today; multi-dimension REDIM remains out-of-scope as initially documented.
- Key paths:
  - Parser: `Parser_Stmt_Runtime.cpp` (`parseDimStatement`) parses comma-separated dimension sizes and stores them in `DimStmt::dimensions`.
  - Analyzer: `SemanticAnalyzer.Stmts.Runtime.cpp::analyzeDim` computes and stores `ArrayMetadata{extents,totalSize}` and tracks `ArrayString`/`ArrayInt` based on name suffix.
  - Lowerer: `lower/Emit_Expr.cpp::lowerArrayAccess` computes flattened index using recorded extents and emits bounds checks via runtime helpers.
- Validation: DIM/UBOUND/array element access tests exercise arrays; added extents plumbing and flattened indexing already used by lowering. Existing golden tests for arrays remain green.

---

## BUG-014: String arrays not supported (duplicate of BUG-032)
- Status: RESOLVED
- Summary: String array allocation, element get/put, and length queries are implemented via `rt_arr_str_*` helpers. Lowering was updated to pass a pointer to a temporary holding the string handle for stores to match the runtime ABI, and the semantic analyzer now recognizes `ARRAY STRING` element types.
- Changes (previous prompt):
  - `LowerStmt_Runtime.cpp`: for `assignArrayElement` to string arrays, allocate a temp slot, store the string handle, pass the slot pointer to `rt_arr_str_put`.
  - Runtime signatures left pointer-based for handles; no whitelist drift.
- Validation: Runtime and golden tests for string operations pass; string-array element assignments no longer report call-argument type mismatches.

---

## BUG-036: String comparison in OR condition causes IL error
- Status: RESOLVED
- Summary: Short-circuit logical lowering (`ANDALSO`/`ORELSE`) and eager logical (`AND`/`OR`) use a store-to-slot + join-block pattern that does not involve branch argument bundles, preventing the earlier IL verification error. String comparisons (`rt_str_eq`, `rt_str_lt`, etc.) return `i1` and are coerced to boolean where needed for short-circuiting.
- Key paths:
  - `LowerExprLogical.cpp`: short-circuit lowering uses `lowerBoolBranchExpr` and stores `i1` results; eager logical ops coerce to `i64` masks as needed.
  - `LowerExpr.cpp::lowerBoolBranchExpr`: emits dedicated then/else/join blocks without branch arguments, avoiding the verifier complaint.
- Validation: Existing string comparison and boolean short-circuit goldens pass; no `cbr` argument-bundle mismatches observed in current test suite.

---

## BUG-016: Local string variables in methods cause compilation error
- Status: RESOLVED (verified 2025-11-13)
- Summary: Local string variable declarations inside class methods now work correctly. The previous "empty block" error has been fixed.
- Test case:
```basic
CLASS Test
    SUB ShowMessage()
        DIM msg$ AS STRING
        msg$ = "Hello"
        PRINT msg$
    END SUB
END CLASS
```
- Validation: Test runs successfully and prints "Hello". String variables can now be declared and used within class methods without compilation errors.

---

## BUG-025: EXP of large values causes overflow trap
- Status: RESOLVED (verified 2025-11-13)
- Summary: EXP function no longer traps on large values. Instead of crashing with "fp overflow in cast.fp_to_si.rte.chk", it now returns the correct floating-point result (including infinity representation for very large values).
- Test case:
```basic
x% = 100
result# = EXP(x%)
PRINT result#  ' Output: 2.68811714181614e+43
```
- Validation: EXP(100) runs successfully and returns approximately 2.688e43, which is the mathematically correct result. No more runtime traps.

---

## Notes
- Related resolved issues: BUG-032/033 (string arrays - NOTE: still failing verification as of 2025-11-13), BUG-034 (MID$ float indices + one-based start) are tracked in devdocs/basic_bugs2.md and reflected in goldens.

---

## BUG-026: DO WHILE with GOSUB causes "empty block" error
- Status: RESOLVED (loop lowering fix + golden)
- Summary: DO/WHILE/FOR lowering previously marked the loop done block as terminated without emitting a terminator instruction when the loop body ended in a terminator (e.g., a GOSUB that generates branches). This produced IL verifier failures: `error: main:do_done: empty block`. The fix leaves the done block unterminated and lets the statement sequencer emit a fallthrough branch to the next line block, ensuring every block contains a terminator only when an instruction is emitted.
- Key paths:
  - src/frontends/basic/lower/Lower_Loops.cpp: stop setting `done->terminated` in `emitDo`, `emitWhile`, and FOR lowering; keep the done block open for the sequencer to wire fallthrough.
  - tests/golden/basic/do_gosub_loop.bas: new golden reproducing DO + GOSUB body with no extra statements; previously failed with IL verifier, now runs cleanly (empty stdout).
- Validation: Golden test added; prior to fix it triggered `empty block` in verifier, after fix it lowers and runs with no diagnostics. Broader loops continue to function; fallthrough to subsequent statements is handled by the sequencer.

---

## BUG-032/033: String arrays end-to-end support
- Status: RESOLVED (verified 2025-11-13)
- Summary: BASIC string arrays now work through allocation, assignment, element load, and printing. The root cause was a mismatch between IL-visible signatures and VM marshalling for string array helpers: `rt_arr_str_get` returned `ptr` instead of `string`, and `rt_arr_str_put` value marshalling dereferenced the wrong level (passing the address of the temporary rather than the string handle).
- Key paths:
  - src/il/runtime/RuntimeSignatures.cpp: signatures updated (`rt_arr_str_get` returns `string`) and wrappers fixed to correctly dereference pointer-typed operands for `rt_arr_str_{get,put,len,release}`.
  - src/frontends/basic/LowerStmt_Runtime.cpp: store path already passed a pointer to a temporary slot for `rt_arr_str_put`; now matches the corrected marshalling.
  - tests/golden/arrays/string_array_store_and_print.bas: new golden validates allocate → store → get → print.
- Validation: Golden passes; existing runtime/string tests remain green. Ownership semantics: `rt_arr_str_get` retains on read; `rt_arr_str_put` retains the new value and releases the previous.

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

## Notes
- Related resolved issues: BUG-032/033 (string arrays), BUG-034 (MID$ float indices + one-based start) are tracked in devdocs/basic_bugs2.md and reflected in goldens.


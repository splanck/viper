# Platform Bugs Found During Adversarial Testing — 2026-02-28

Bugs in the Viper compiler, runtime, or VM discovered while adversarial-testing ViperSQL.
These are **platform bugs**, not SQL logic bugs.

---

## BUG-ADV-001: SIGABRT when accessing entity String field cross-module after certain executor states

- **Status**: FIXED
- **Severity**: P0 (crash)
- **Component**: Zia compiler — Lowerer_Expr_Complex.cpp (string field loads)
- **Symptom**: Accessing `QueryResult.message` (a String field) from a function defined in a different module (`test_common.zia`) causes SIGABRT (exit code 134). The string handle in the entity field is a borrowed reference (no retain on Load); when the string is consumed by concatenation or passed cross-module, the borrowed reference becomes dangling.
- **Root cause**: In `Lowerer_Expr_Complex.cpp`, loading a `Str`-typed entity/value-type field emits a bare `Load` IL instruction without a `rt_str_retain_maybe` call. The VM's `loadSlotFromPtr` returns a borrowed reference. When the loaded string is used in concatenation (which creates temporaries that may reuse freed memory), the borrowed reference becomes dangling.
- **Fix**:
  - Added `kStrRetainMaybe` constant to `src/frontends/zia/RuntimeNames.hpp`
  - In `Lowerer_Expr_Complex.cpp`, after Load instructions for Str-typed fields (both value type and entity type sections), emit `emitCall(runtime::kStrRetainMaybe, {Value::temp(loadId)})` to convert the borrowed reference to an owned reference
- **Regression test**: `src/tests/zia/test_zia_entity_string_field.cpp` — 5 tests verifying `rt_str_retain_maybe` is emitted after Str field loads
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-002: Unmatched closing parentheses not detected by SQL parser

- **Status**: FIXED
- **Severity**: P1 (wrong result)
- **Component**: ViperSQL executor — `executor.zia`
- **Symptom**: `SELECT 1)))` succeeds instead of returning an error. Trailing unmatched `)` tokens are silently ignored.
- **Root cause**: After `parseSelectStmt()`, only `parser.hasError` was checked — there was no check for unconsumed trailing tokens.
- **Fix**: In `executor.zia`, after parsing SELECT, INSERT, UPDATE, DELETE statements, added trailing-token checks that verify `parser.currentKind()` is `TK_EOF`, `TK_SEMICOLON`, or a valid chained keyword (UNION/EXCEPT/INTERSECT for SELECT). Returns error for any unexpected trailing tokens.
- **Regression test**: `demos/zia/sqldb/tests/test_adversarial_fixes.zia` — ADV-002a through ADV-002f
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-003: Unclosed string literal not detected by SQL lexer

- **Status**: FIXED
- **Severity**: P1 (wrong result)
- **Component**: ViperSQL lexer — `lexer.zia`
- **Symptom**: `SELECT 'hello` (no closing quote) succeeds instead of returning an error.
- **Root cause**: The lexer's `readString()` exits the while loop on EOF without setting an error. Returns a valid `TK_STRING` token for the unclosed literal.
- **Fix**:
  - In `lexer.zia:readString()`, after the while loop, check for EOF without closing quote. Return a `TK_ERROR` token instead of `TK_STRING`.
  - In `parser.zia:advance()`, check for `TK_ERROR` token kind and propagate to `parser.hasError`/`parser.error`.
- **Regression test**: `demos/zia/sqldb/tests/test_adversarial_fixes.zia` — ADV-003a through ADV-003d
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-004: SELECT/WHERE/GROUP BY/UPDATE on nonexistent columns succeeds

- **Status**: FIXED
- **Severity**: P1 (wrong result)
- **Component**: ViperSQL executor — column resolution
- **Symptom**: Queries referencing columns that don't exist in the table succeed instead of returning an error.
- **Root cause**: `evalColumn()` in `executor.zia` calls `table.findColumnIndex(expr.columnName)` — when it returns -1 (not found), the function silently returns `sqlNull()` with no error propagation.
- **Fix**:
  - Added `hasEvalError` (Integer flag) and `evalError` (String) fields to the Executor entity
  - `evalColumn()` sets `hasEvalError = 1` and `evalError = "Column '...' not found"` when column not found
  - Added `hasEvalError` checks after evalExpr calls in query.zia (WHERE, projection, GROUP BY sections)
  - Added direct `colIdx < 0` check in dml.zia for UPDATE SET on nonexistent columns
  - **Note**: Uses Integer flag (not String comparison) because reading String fields through chained entity references (`queryHandler.exec.evalError != ""`) has cross-module visibility issues — a secondary BUG-ADV-001-like symptom
- **Regression test**: `demos/zia/sqldb/tests/test_adversarial_fixes.zia` — ADV-004a through ADV-004f
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-005: FOREIGN KEY referencing nonexistent table succeeds

- **Status**: FIXED
- **Severity**: P1 (wrong result)
- **Component**: ViperSQL DDL — `ddl.zia`
- **Symptom**: `CREATE TABLE` with a `FOREIGN KEY` clause referencing a table that doesn't exist succeeds.
- **Root cause**: `executeCreateTable()` adds columns with FK constraints without validating that the referenced table or column exists. FK validation only happened at DML time (INSERT/UPDATE) in `validateConstraints()`.
- **Fix**: In `ddl.zia:executeCreateTable()`, after adding columns and before adding the table to the database, validate FK references:
  - Check that the referenced table exists via `exec.findTable(refTableName)`
  - Check that the referenced column exists via `refTable.findColumnIndex(refColumnName)`
  - Return error if either check fails
- **Regression test**: `demos/zia/sqldb/tests/test_adversarial_fixes.zia` — ADV-005a through ADV-005e
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-001a: Comprehensive string retain — emitFieldLoad helper

- **Status**: FIXED
- **Severity**: P0 (crash)
- **Component**: Zia compiler — `Lowerer_Emit.cpp` (emitFieldLoad)
- **Symptom**: The original BUG-ADV-001 fix only covered inline GEP+Load in `lowerField()`. Eight other code paths that load Str-typed fields via the `emitFieldLoad()` helper also produce borrowed references, leading to use-after-free.
- **Affected paths**: Implicit `self.field` in value/entity methods, optional chaining, boxing/copying value types, pattern matching.
- **Fix**: Added `rt_str_retain_maybe` call inside `emitFieldLoad()` itself (`Lowerer_Emit.cpp:464-466`) when `fieldType.kind == Type::Kind::Str`. Single change covers all 8 paths.
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-006: ORDER BY nonexistent column succeeds

- **Status**: FIXED
- **Severity**: P1 (wrong result)
- **Component**: ViperSQL query — `query.zia`
- **Symptom**: `SELECT * FROM t ORDER BY phantom_col` succeeds instead of returning an error. With a single row (or no comparisons), `sortMatchingRows` never calls `evalColumn`, so `hasEvalError` is never set.
- **Root cause**: ORDER BY column validation happened only during comparison, which doesn't execute when there are 0 or 1 rows to sort.
- **Fix**: Added pre-sort column validation in `query.zia` that checks `findColumnIndex` for each ORDER BY EXPR_COLUMN expression before calling `sortMatchingRows`.
- **Regression test**: `test_adversarial_fixes.zia` — ADV-006a, ADV-006b
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-007: INSERT with wrong value count silently succeeds

- **Status**: FIXED
- **Severity**: P1 (wrong result)
- **Component**: ViperSQL DML — `dml.zia`
- **Symptom**: `INSERT INTO t(a,b,c) VALUES (1,2)` succeeds, silently NULLing the missing column.
- **Root cause**: `executeInsert()` never validated that the number of VALUES in each row matched the expected column count.
- **Fix**: In `dml.zia`, after computing `stmtSavepoint`, added a validation loop that computes `expectedCount` (from column list or non-generated columns) and checks each value row.
- **Regression test**: `test_adversarial_fixes.zia` — ADV-007a through ADV-007c
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-008: Ambiguous column in JOINs silently resolves to first match

- **Status**: FIXED
- **Severity**: P1 (wrong result)
- **Component**: ViperSQL JOIN — `join.zia`
- **Symptom**: `SELECT id FROM t1 JOIN t2 ON ...` succeeds when `id` exists in both tables. Returns the first table's value.
- **Root cause**: `findJoinColumnValue()` returned the FIRST matching column when `tableName == ""` (unqualified). Never checked if the same column exists in multiple joined tables.
- **Fix**:
  - Modified `findJoinColumnValue()` to count matches for unqualified columns and set `hasEvalError` when `foundCount > 1`
  - Added `hasEvalError` checks after JOIN condition evaluation, WHERE filtering, projection, and ORDER BY in `executeCrossJoin()`
  - Modified `findResultColumnIndex()` to detect ambiguity in ORDER BY column resolution
  - Added pre-projection column validation to catch ambiguity even with 0 result rows (covers USING/NATURAL joins with no matching data)
- **Regression test**: `test_adversarial_fixes.zia` — ADV-008a through ADV-008d
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-009: Scalar subquery returning >1 row silently returns first

- **Status**: FIXED
- **Severity**: P1 (wrong result)
- **Component**: ViperSQL executor — `executor.zia`
- **Symptom**: `SELECT (SELECT n FROM nums)` where `nums` has 5 rows succeeds and returns first row value.
- **Root cause**: `evalSubquery()` took the first row from a subquery result without checking that there was exactly 1 row. SQL requires an error when a scalar subquery returns 2+ rows.
- **Fix**: Added `result.rowCount() > 1` check in `evalSubquery()` that sets `hasEvalError` and returns `sqlNull()`. Also added `hasEvalError` check in `executeExpressionSelect()` (SELECT without FROM).
- **Regression test**: `test_adversarial_fixes.zia` — ADV-009a through ADV-009c
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-010: Stale hasEvalError from prior statement causes cascading failures

- **Status**: FIXED
- **Severity**: P1 (wrong result)
- **Component**: ViperSQL executor — `executor.zia`
- **Symptom**: After any statement that sets `hasEvalError = 1` (e.g., SELECT on nonexistent column), all subsequent statements inherit the error flag and may fail silently or return incorrect results.
- **Root cause**: `executeSqlDispatch()` cleared the subquery cache but not `hasEvalError`/`evalError` between statements.
- **Fix**: Added `hasEvalError = 0; evalError = "";` at the top of `executeSqlDispatch()`.
- **Regression test**: `test_adversarial_fixes.zia` — ADV-010a through ADV-010c
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-011: Nested IN subquery returns 0 rows instead of expected results

- **Status**: FIXED
- **Severity**: P2 (wrong result)
- **Component**: ViperSQL executor — `executor.zia` (`flattenSubqueries`)
- **Symptom**: `SELECT n FROM nums WHERE n IN (SELECT n FROM nums WHERE n IN (SELECT n FROM nums WHERE n > 3))` returns 0 rows instead of {4, 5}. Two-level IN nesting works, three-level does not.
- **Root cause**: `flattenSubqueries()` pre-executes inner subqueries and substitutes results as literals. When an inner subquery returns multiple rows (e.g., {4, 5}), the function takes only the FIRST row via `innerResult.getRow(0)` and produces a scalar replacement like `4`. For an IN context (`WHERE n IN (SELECT ...)`), this creates invalid SQL like `WHERE n IN 4` instead of `WHERE n IN (4, 5)`.
- **Fix**: In `flattenSubqueries()`, detect IN context by checking if the text before `(SELECT` ends with `IN ` (case-insensitive). When in IN context with multiple rows, build a parenthesized value list `(val1, val2, ...)` instead of a scalar. Single-row and zero-row results use the existing scalar path.
- **Regression test**: `test_adversarial.zia` — test 7.4 (nested IN subqueries)
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## BUG-ADV-012: SIGABRT under memory pressure from BytecodeVM alloca buffer reallocation

- **Status**: FIXED
- **Severity**: P0 (crash)
- **Component**: BytecodeVM — `src/bytecode/BytecodeVM.cpp` (alloca buffer management)
- **Symptom**: Creating 50 tables + inserting 10,000 rows (100 batches of 100 rows) causes SIGABRT at `rt_heap.c:96` (`hdr->magic == RT_MAGIC` assertion). Crash occurs around batch 30 of 100.
- **Root cause**: The BytecodeVM's `allocaBuffer_` (`std::vector<uint8_t>`) is initially sized to 64KB. When alloca usage exceeds capacity, the ALLOCA opcode handler calls `allocaBuffer_.resize(newSize)`, which reallocates the buffer to a new memory location. All previously computed alloca pointers (stored in the operand stack and local variables) become dangling — they point to the freed old buffer. Subsequent Load/Store operations dereference these stale pointers, causing heap-use-after-free. This eventually corrupts heap headers, triggering the `RT_MAGIC` assertion.
- **Fix**: In the BytecodeVM constructor, call `allocaBuffer_.reserve(16 * 1024 * 1024)` before the initial `resize(64 * 1024)`. This pre-allocates virtual address space for the maximum 16MB limit, ensuring that subsequent `resize()` calls never trigger reallocation. All alloca pointers remain valid.
- **Confirmed via ASAN**: AddressSanitizer identified the exact error as `heap-use-after-free` in `BytecodeVM::runThreaded()`, with the freed region being exactly the 65536-byte initial buffer.
- **Regression test**: `demos/zia/sqldb/tests/test_scale_debug.zia` — creates 50 tables, inserts 10,000 rows, runs SELECT COUNT
- **Found**: 2026-02-28
- **Fixed**: 2026-02-28

---

## Known Remaining Issues

| Issue | Severity | Description |
|-------|----------|-------------|
| `PASSWORD` reserved keyword | P3 | `PASSWORD` is lexed as `TK_PASSWORD`, cannot be used as column name without quoting. |

---

## Summary

| Bug | Severity | Component | Status |
|-----|----------|-----------|--------|
| BUG-ADV-001 | P0 (crash) | Zia compiler string field loads | FIXED |
| BUG-ADV-001a | P0 (crash) | Zia compiler emitFieldLoad helper | FIXED |
| BUG-ADV-002 | P1 (wrong result) | SQL executor trailing tokens | FIXED |
| BUG-ADV-003 | P1 (wrong result) | SQL lexer unclosed strings | FIXED |
| BUG-ADV-004 | P1 (wrong result) | SQL executor column resolution | FIXED |
| BUG-ADV-005 | P1 (wrong result) | SQL DDL FK validation | FIXED |
| BUG-ADV-006 | P1 (wrong result) | SQL ORDER BY nonexistent column | FIXED |
| BUG-ADV-007 | P1 (wrong result) | SQL INSERT value count validation | FIXED |
| BUG-ADV-008 | P1 (wrong result) | SQL JOIN ambiguous columns | FIXED |
| BUG-ADV-009 | P1 (wrong result) | SQL scalar subquery validation | FIXED |
| BUG-ADV-010 | P1 (wrong result) | SQL stale eval error state | FIXED |
| BUG-ADV-011 | P2 (wrong result) | SQL nested IN subquery flattening | FIXED |
| BUG-ADV-012 | P0 (crash) | BytecodeVM alloca buffer reallocation | FIXED |

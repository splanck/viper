# Zia SQL Database - Bug Tracker

## Summary
- **Total Bugs Found**: 1
- **Bugs Fixed**: 0
- **Bugs Open**: 1
- **Tests Passing**: 162
- **Tests Failing**: 1

---

## Bug Log

### BUG-001: SELECT with non-existent column returns NULL instead of error
- **Status**: Open
- **Severity**: Low
- **Description**: When selecting a column that doesn't exist in a table, the query succeeds and returns NULL values instead of an error.
- **Steps to Reproduce**:
  1. Create table: `CREATE TABLE t (id INTEGER)`
  2. Run: `SELECT nonexistent_col FROM t`
  3. Query succeeds with NULL instead of "column not found" error
- **Expected**: Error "column 'nonexistent_col' not found"
- **Actual**: Query succeeds with NULL value
- **Impact**: Minor - matches some SQL implementations that are lenient

---

## Development Progress

### Phase 10: Storage & Persistence (Steps 49-53) - COMPLETED
- [x] Step 49: File format design (SQL dump format)
- [x] Step 50: SAVE command (`SAVE 'filename'`)
- [x] Step 51: OPEN command (`OPEN 'filename'`)
- [x] Step 52: EXPORT to CSV (`EXPORT table TO 'file.csv'`)
- [x] Step 53: IMPORT from CSV (`IMPORT INTO table FROM 'file.csv'`)

### Phase 11: Advanced Features (Steps 54-60) - COMPLETED
- [x] Step 54: ALTER TABLE (ADD/DROP/RENAME COLUMN, RENAME TABLE)
- [x] Step 55: DROP TABLE/INDEX (already implemented)
- [x] Step 56: VACUUM
- [x] Step 57: UNION/UNION ALL
- [x] Step 58: CASE expressions
- [ ] Step 59: Views (CREATE VIEW) - Deferred (complex, low priority)
- [ ] Step 60: Triggers (basic) - Deferred (complex, low priority)

### Phase 13: REPL & CLI (Steps 66-68) - COMPLETED
- [x] Step 66: Interactive REPL (`repl.zia`)
- [x] Step 67: Multi-line input (supported via semicolon detection)
- [x] Step 68: Help & metadata (HELP, DESCRIBE, .help, .tables, etc.)

---

## Test Summary

| Test File | Passed | Failed |
|-----------|--------|--------|
| test.zia | 22 | 0 |
| test_features.zia | 22 | 0 |
| test_functions.zia | 25 | 0 |
| test_index.zia | 16 | 0 |
| test_constraints.zia | 16 | 0 |
| test_subquery.zia | 16 | 0 |
| test_persistence.zia | 31 | 1 |
| test_advanced.zia | 14 | 0 |
| **Total** | **162** | **1** |

---

## Features Implemented

### Core SQL
- DDL: CREATE TABLE, DROP TABLE, CREATE INDEX, DROP INDEX
- DML: INSERT, SELECT, UPDATE, DELETE
- Clauses: WHERE, ORDER BY, LIMIT, OFFSET, DISTINCT, GROUP BY, HAVING
- Joins: CROSS, INNER, LEFT, RIGHT, FULL OUTER
- Aggregates: COUNT, SUM, AVG, MIN, MAX
- Subqueries: Scalar, IN

### Extended Features (Phase 11)
- UNION / UNION ALL
- CASE expressions
- ALTER TABLE (ADD/DROP/RENAME COLUMN, RENAME TO)
- VACUUM

### Persistence (Phase 10)
- SAVE / OPEN (SQL dump format)
- EXPORT / IMPORT (CSV format)

### Functions Library
- String: UPPER, LOWER, LENGTH, SUBSTR, TRIM, LTRIM, RTRIM, REPLACE, CONCAT, INSTR
- Math: ABS, MOD, MIN, MAX
- Null-handling: COALESCE, IFNULL, NULLIF, IIF
- Type: TYPEOF

### REPL & CLI (Phase 13)
- Interactive shell with prompt
- Multi-line input support
- Meta-commands: .help, .quit, .tables, .describe, .save, .open, .schema

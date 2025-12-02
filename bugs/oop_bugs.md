# Viper OOP Runtime Bugs

This file tracks bugs discovered while developing full-featured games using the Viper.* OOP runtime.

## Open Bugs

(None - all bugs have been resolved)

## Resolved Bugs

| Bug | Description | Status | Date |
|-----|-------------|--------|------|
| BUG-OOP-042 | Field/variable name 'base' conflicts with BASE keyword | Fixed | 2025-12-02 |
| BUG-OOP-041 | Parameter name 'floor' conflicts with FLOOR keyword | Fixed | 2025-12-02 |
| BUG-OOP-040 | Viper.Random namespace not accessible - RANDOM is reserved keyword | Fixed | 2025-12-02 |
| BUG-OOP-039 | Fully qualified type names not supported in DIM inside CLASS | Fixed | 2025-12-02 |
| BUG-OOP-038 | Runtime assertion on array element type for object array parameters | Fixed | 2025-12-02 |
| BUG-OOP-037 | Local variable names matching class member names return wrong values | Cannot Reproduce | 2025-12-02 |
| BUG-OOP-036 | Same-named local variables corrupt across consecutive function calls in a class | Fixed | 2025-12-02 |
| BUG-OOP-035 | FUNCTION returning object to local variable causes segfault | Fixed | 2025-12-01 |
| BUG-OOP-034 | FOR loop counter typed as i64 when bounds are INTEGER parameters | Cannot Reproduce | 2025-12-01 |
| BUG-OOP-033 | VM stack size limit causes stack overflow with moderate arrays | Fixed | 2025-12-01 |
| BUG-OOP-032 | Local variable same name as class causes false "unknown procedure" error | Fixed | 2025-12-01 |
| BUG-OOP-031 | Calling global SUB/FUNCTION from inside CLASS method fails | Fixed | 2025-11-30 |
| BUG-OOP-030 | Backslash in string literal causes parser error | Fixed | 2025-11-30 |
| BUG-OOP-029 | Pipe character in string literal causes parser error in IF/ELSEIF chain | Fixed | 2025-11-30 |
| BUG-OOP-028 | USING statement not propagated through ADDFILE | Fixed | 2025-11-30 |
| BUG-OOP-027 | Object array declarations cause stack overflow | Fixed | 2025-12-01 |
| BUG-OOP-026 | USING statement doesn't enable unqualified procedure calls | Fixed | 2025-11-29 |
| BUG-OOP-025 | Viper.Collections.List cannot store primitive values | Fixed | 2025-11-29 |
| BUG-OOP-024 | Viper.Terminal.* functions have type mismatch (i32 vs i64) | Fixed | 2025-11-29 |
| BUG-OOP-023 | Viper.Terminal.InKey() missing from runtime | Fixed | 2025-11-29 |
| BUG-OOP-022 | SELECT CASE cannot use string expressions for CASE labels | Fixed | 2025-11-28 |
| BUG-OOP-021 | Reserved keywords cannot be used as identifiers | Won't Fix | 2025-11-28 |
| BUG-OOP-020 | SUB calls require parentheses even with no arguments | Fixed | 2025-11-28 |
| BUG-OOP-019 | SELECT CASE cannot use CONST labels across ADDFILE boundaries | Fixed | 2025-11-30 |
| BUG-OOP-018 | Viper.* runtime classes not callable from BASIC | Fixed | 2025-11-29 |
| BUG-OOP-017 | Single-line IF with colon only applies condition to first statement | Fixed | 2025-11-28 |
| BUG-OOP-016 | INTEGER variable type mismatch when passed to functions | Fixed | 2025-11-27 |
| BUG-OOP-015 | Functions cannot be called as statements | Fixed | 2025-11-27 |
| BUG-OOP-014 | END statement inside SUB causes compiler error | Fixed | 2025-11-27 |
| BUG-OOP-013 | ELSE IF (two words) causes internal compiler error | Fixed | 2025-11-27 |
| BUG-OOP-012 | OR/AND expressions in IF statements cause internal compiler error | Fixed | 2025-11-27 |
| BUG-OOP-011 | String array element access causes internal compiler error | Fixed | 2025-11-27 |
| BUG-OOP-010 | DIM inside FOR loop causes internal compiler error | Fixed | 2025-11-27 |
| BUG-OOP-009 | Object parameters lose state inside procedures | Fixed | 2025-11-26 |
| BUG-OOP-008 | Multi-file class visibility issues | Fixed | 2025-11-26 |
| BUG-OOP-007 | Method calls on local object variables don't work | Fixed | 2025-11-26 |
| BUG-OOP-006 | PRINT # with semicolons in format | Fixed | 2025-11-26 |
| BUG-OOP-005 | Method name conflicts with field name (case-insensitive) | Fixed | 2025-11-26 |
| BUG-OOP-004 | INPUT # with multiple targets not supported | Fixed | 2025-11-26 |
| BUG-OOP-003 | Viper.Collections.List as function parameter type | Fixed | 2025-11-26 |
| BUG-OOP-002 | EXIT SUB/FUNCTION inside FOR/WHILE loop not allowed | Fixed | 2025-11-26 |
| BUG-OOP-001 | BYREF parameters not supported | Won't Fix | 2025-11-26 |

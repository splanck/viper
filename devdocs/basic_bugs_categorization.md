# VIPER BASIC Bug Categorization

## RESOLVED (Already Fixed)
- BUG-001: String concatenation $ suffix ✅
- BUG-002: & operator ✅
- BUG-003: FUNCTION name assignment ✅
- BUG-005: SGN function ✅
- BUG-006: TAN, ATN, EXP, LOG functions ✅
- BUG-008: REDIM PRESERVE ✅
- BUG-009: CONST keyword ✅
- BUG-011: SWAP statement ✅

## BUG FIXES (Straightforward fixes)
- **BUG-021**: SELECT CASE negative literals - Parser fix to accept unary minus
- **BUG-024**: CONST with type suffix assertion - Storage allocation fix
- **BUG-025**: EXP overflow trap - Better error handling (graceful NaN/Inf)

## REQUIRES SIGNIFICANT PLANNING (Architecture changes)
- **BUG-004**: Optional parentheses - Parser grammar change, ambiguity resolution
- **BUG-007**: Multi-dimensional arrays - Major runtime/semantic changes
- **BUG-010**: STATIC keyword - Storage model changes, lifetime management
- **BUG-012**: BOOLEAN type system - Type compatibility overhaul (Partial: IF/ELSEIF accept INT truthiness as of 2025-11-13; remaining work: TRUE/FALSE constants as BOOL, EOF() return type, logical operator coercions)
- **BUG-013**: SHARED keyword - Scope system redesign
- **BUG-014**: String arrays - Runtime array system extension
- **BUG-015**: String properties in classes - Runtime string lifecycle for OOP
- **BUG-016**: Local strings in methods - Code generation for method locals
- **BUG-017**: Global strings from methods - Scope resolution in OOP
- **BUG-018**: FUNCTION methods in classes - Label generation for class methods
- **BUG-019**: Float CONST truncation - Type inference from initializer
- **BUG-020**: String constants runtime error - String lifecycle for CONST
- **BUG-022**: Float literal type inference - Default type system policy
- **BUG-023**: DIM with initializer - Parser extension, combined statement

## PRIORITY ORDER FOR BUG FIXES
1. BUG-021 (SELECT CASE negative literals) - Parser fix
2. BUG-024 (CONST type suffix) - Storage allocation fix
3. BUG-025 (EXP overflow) - Error handling improvement

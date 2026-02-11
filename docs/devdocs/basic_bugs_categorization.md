# VIPER BASIC Bug Categorization

> **Note:** This file was originally created 2025-11-12. Many bugs listed below have since been resolved in later
> sessions. See `bugs/basic_resolved.md` for the authoritative list of resolved bugs with details.

## RESOLVED (Already Fixed)

- BUG-001: String concatenation $ suffix ✅
- BUG-002: & operator ✅
- BUG-003: FUNCTION name assignment ✅
- BUG-005: SGN function ✅
- BUG-006: TAN, ATN, EXP, LOG functions ✅
- BUG-008: REDIM PRESERVE ✅
- BUG-009: CONST keyword ✅
- BUG-011: SWAP statement ✅
- BUG-021: SELECT CASE negative literals ✅ (Fixed 2025-11-12)
- BUG-022: Float literal type inference ✅ (Fixed 2025-11-12)
- BUG-023: DIM with initializer ✅ (Fixed 2025-11-12)
- BUG-024: CONST with type suffix assertion ✅ (Fixed 2025-11-12)

## BUG FIXES (Straightforward fixes)

- **BUG-025**: EXP overflow trap - Better error handling (graceful NaN/Inf)

## REQUIRES SIGNIFICANT PLANNING (Architecture changes)

- **BUG-004**: Optional parentheses - Parser grammar change, ambiguity resolution
- **BUG-007**: Multi-dimensional arrays - Major runtime/semantic changes
- **BUG-010**: STATIC keyword - Storage model changes, lifetime management
- **BUG-012**: BOOLEAN type system - Type compatibility overhaul (Partial: IF/ELSEIF accept INT truthiness as of
  2025-11-13; remaining work: TRUE/FALSE constants as BOOL, EOF() return type, logical operator coercions)
- **BUG-013**: SHARED keyword - Scope system redesign
- **BUG-014**: String arrays - Runtime array system extension
- **BUG-015**: String properties in classes - Runtime string lifecycle for OOP
- **BUG-016**: Local strings in methods - Code generation for method locals
- **BUG-017**: Global strings from methods - Scope resolution in OOP
- **BUG-018**: FUNCTION methods in classes - Label generation for class methods
- **BUG-019**: Float CONST truncation - Type inference from initializer
- **BUG-020**: String constants runtime error - String lifecycle for CONST

## PRIORITY ORDER FOR REMAINING BUG FIXES

1. BUG-025 (EXP overflow) - Error handling improvement

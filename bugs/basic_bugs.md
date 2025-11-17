# VIPER BASIC Known Bugs and Issues

*Last Updated: 2025-11-17*

**Bug Statistics**: 73 resolved, 0 outstanding bugs, 1 design decision (74 total documented)

**Test Suite Status**: 642/642 tests passing (100%)

**STATUS**: ✅ All bugs resolved! BUG-072 (SELECT CASE block ordering) was the final outstanding bug and has been successfully fixed.

---

## OUTSTANDING BUGS

**0 bugs** - All issues resolved! 🎉

---

## DESIGN DECISIONS (Not Bugs)

- ℹ️ **BUG-049**: RND() function signature incompatible with standard BASIC - BY DESIGN (zero-argument form)
- ℹ️ **BUG-054**: STEP is reserved word - BY DESIGN (FOR loop keyword)
- ℹ️ **BUG-055**: Cannot assign to FOR loop variable - BY DESIGN (intentional semantic check)
- ℹ️ **BUG-069**: Objects not auto-initialized by DIM - BY DESIGN (explicit NEW required for reference semantics)

---

## RESOLVED BUGS (73 bugs)

### Recently Resolved (2025-11-17)
- ✅ **BUG-067**: Array fields - RESOLVED 2025-11-17
- ✅ **BUG-068**: Function name implicit returns - RESOLVED 2025-11-17
- ✅ **BUG-070**: Boolean parameters - RESOLVED 2025-11-17
- ✅ **BUG-071**: String arrays - RESOLVED 2025-11-17
- ✅ **BUG-072**: SELECT CASE blocks after exit - RESOLVED 2025-11-17
- ✅ **BUG-073**: Object parameter methods - RESOLVED 2025-11-17
- ✅ **BUG-074**: Constructor corruption - RESOLVED 2025-11-17

### All Resolved Bugs (BUG-001 through BUG-074)
- ✅ **BUG-001**: String concatenation requires $ suffix for type inference - RESOLVED 2025-11-12
- ✅ **BUG-002**: & operator for string concatenation not supported - RESOLVED 2025-11-12
- ✅ **BUG-003**: FUNCTION name assignment syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-004**: Procedure calls require parentheses even with no arguments - RESOLVED 2025-11-12
- ✅ **BUG-005**: SGN function not implemented - RESOLVED 2025-11-12
- ✅ **BUG-006**: Limited trigonometric/math functions - RESOLVED 2025-11-12
- ✅ **BUG-007**: Multi-dimensional arrays not supported - RESOLVED 2025-11-13
- ✅ **BUG-008**: REDIM PRESERVE syntax not supported - RESOLVED 2025-11-12
- ✅ **BUG-009**: CONST keyword not implemented - RESOLVED 2025-11-12
- ✅ **BUG-010**: STATIC keyword not implemented - RESOLVED 2025-11-14
- ✅ **BUG-011**: SWAP statement not implemented - RESOLVED 2025-11-12
- ✅ **BUG-012**: BOOLEAN type incompatibility with TRUE/FALSE constants - RESOLVED 2025-11-14
- ✅ **BUG-013**: SHARED keyword not supported - RESOLVED 2025-11-13
- ✅ **BUG-014**: String arrays not supported (duplicate of BUG-032) - RESOLVED 2025-11-13
- ✅ **BUG-015**: String properties in classes cause runtime error - RESOLVED 2025-11-13
- ✅ **BUG-016**: Local string variables in methods cause compilation error - RESOLVED 2025-11-13
- ✅ **BUG-017**: Accessing global strings from methods causes segfault - RESOLVED 2025-11-14
- ✅ **BUG-018**: FUNCTION methods in classes cause code generation error - RESOLVED 2025-11-12
- ✅ **BUG-019**: Float literals assigned to CONST are truncated to integers - RESOLVED 2025-11-14
- ✅ **BUG-020**: String constants cause runtime error - RESOLVED 2025-11-13
- ✅ **BUG-021**: SELECT CASE doesn't support negative integer literals - RESOLVED 2025-11-12
- ✅ **BUG-022**: Float literals without explicit type default to INTEGER - RESOLVED 2025-11-12
- ✅ **BUG-023**: DIM with initializer not supported - RESOLVED 2025-11-12
- ✅ **BUG-024**: CONST with type suffix causes assertion failure - RESOLVED 2025-11-12
- ✅ **BUG-025**: EXP of large values causes overflow trap - RESOLVED 2025-11-13
- ✅ **BUG-026**: DO WHILE loops with GOSUB cause "empty block" error - RESOLVED 2025-11-13
- ✅ **BUG-027**: MOD operator doesn't work with INTEGER type (%) - RESOLVED 2025-11-12
- ✅ **BUG-028**: Integer division operator (\\) doesn't work with INTEGER type (%) - RESOLVED 2025-11-12
- ✅ **BUG-029**: EXIT FUNCTION and EXIT SUB not supported - RESOLVED 2025-11-12
- ✅ **BUG-030**: SUBs and FUNCTIONs cannot access global variables - RESOLVED 2025-11-14
- ✅ **BUG-031**: String comparison operators (<, >, <=, >=) not supported - RESOLVED 2025-11-12
- ✅ **BUG-032**: String arrays not supported - RESOLVED 2025-11-13
- ✅ **BUG-033**: String array assignment causes type mismatch error (duplicate of BUG-032) - RESOLVED 2025-11-13
- ✅ **BUG-034**: MID$ does not convert float arguments to integer - RESOLVED 2025-11-13
- ✅ **BUG-035**: Global variables not accessible in SUB/FUNCTION (duplicate of BUG-030) - RESOLVED 2025-11-14
- ✅ **BUG-036**: String comparison in OR condition causes IL error - RESOLVED 2025-11-13
- ✅ **BUG-037**: SUB methods on class instances cannot be called - RESOLVED 2025-11-15
- ✅ **BUG-038**: String concatenation with method results fails in certain contexts - RESOLVED 2025-11-14
- ✅ **BUG-039**: Cannot assign method call results to variables - RESOLVED 2025-11-15
- ✅ **BUG-040**: Cannot use custom class types as function return types - RESOLVED 2025-11-15
- ✅ **BUG-041**: Cannot create arrays of custom class types - RESOLVED 2025-11-14
- ✅ **BUG-042**: Reserved keyword 'LINE' cannot be used as variable name - RESOLVED 2025-11-14
- ✅ **BUG-043**: String arrays reported not working (duplicate of BUG-032/033) - RESOLVED 2025-11-13
- ✅ **BUG-044**: CHR() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-045**: STRING arrays not working with AS STRING syntax - RESOLVED 2025-11-15
- ✅ **BUG-046**: Cannot call methods on array elements - RESOLVED 2025-11-15
- ✅ **BUG-047**: IF/THEN/END IF inside class methods causes crash - RESOLVED 2025-11-15
- ✅ **BUG-048**: Cannot call module-level SUB/FUNCTION from within class methods - RESOLVED 2025-11-15
- ✅ **BUG-050**: SELECT CASE with multiple values causes IL generation error - VERIFIED NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-051**: DO UNTIL loop causes IL generation error - VERIFIED NOT REPRODUCIBLE 2025-11-15
- ✅ **BUG-052**: ON ERROR GOTO handler blocks missing terminators - RESOLVED 2025-11-15
- ✅ **BUG-053**: Cannot access global arrays in SUB/FUNCTION - RESOLVED 2025-11-15
- ✅ **BUG-056**: Arrays not allowed as class fields - RESOLVED 2025-11-15
- ✅ **BUG-057**: BOOLEAN return type in class methods causes type mismatch - RESOLVED 2025-11-15
- ✅ **BUG-058**: String array fields don't retain values - RESOLVED 2025-11-15
- ✅ **BUG-059**: Cannot access array fields within class methods - RESOLVED 2025-11-15
- ✅ **BUG-060**: Cannot call methods on class objects passed as SUB/FUNCTION parameters - RESOLVED 2025-11-15
- ✅ **BUG-061**: Cannot assign class field value to local variable (regression) - RESOLVED 2025-11-15
- ✅ **BUG-062**: CONST with CHR$() not evaluated at compile time - RESOLVED 2025-11-15
- ✅ **BUG-063**: Module-level initialization cleanup code leaks into subsequent functions - RESOLVED 2025-11-15
- ✅ **BUG-064**: ASC() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-065**: Array field assignments silently dropped by compiler - RESOLVED 2025-11-15
- ✅ **BUG-066**: VAL() function not implemented - RESOLVED 2025-11-15
- ✅ **BUG-067**: Array fields - RESOLVED 2025-11-17
- ✅ **BUG-068**: Function name implicit returns - RESOLVED 2025-11-17
- ✅ **BUG-070**: Boolean parameters - RESOLVED 2025-11-17
- ✅ **BUG-071**: String arrays - RESOLVED 2025-11-17
- ✅ **BUG-072**: SELECT CASE blocks after exit - RESOLVED 2025-11-17
- ✅ **BUG-073**: Object parameter methods - RESOLVED 2025-11-17
- ✅ **BUG-074**: Constructor corruption - RESOLVED 2025-11-17

---

## ADDITIONAL INFORMATION

**For detailed bug descriptions, reproduction cases, root cause analyses, and implementation notes:**
- See `basic_resolved.md` - Complete documentation of all resolved bugs
- See `/bugs/bug_testing/` - Test cases and stress tests that discovered these bugs

**Testing Sources:**
- Language audit and systematic feature testing
- Stress tests: Dungeon, Frogger, Adventure, BasicDB, Othello, Vipergrep
- OOP stress tests with complex class hierarchies and interactions

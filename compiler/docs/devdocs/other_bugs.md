# OTHER BUGS (Non-BASIC Frontend Issues)

# Discovered during comprehensive BASIC testing session: 2025-11-12

## IL-BUG-001: IL verifier error with complex nested IF-ELSEIF structures

**Component**: IL Verifier / BASIC Frontend IL Generation
**Severity**: High
**Status**: Confirmed
**Test Case**: dungeon_quest_v3.bas (line 324)
**Discovered**: 2025-11-12

**Description**:
Complex nested IF statements inside ELSEIF blocks can cause the IL verifier to fail with "expected 2 branch argument
bundles, or none" error on cbr instruction. The BASIC frontend successfully generates IL code, but the VM rejects it
during verification.

**Error Message**:

```
error: main:if_test_6: cbr %t928 label if_test_06 label if_else2: expected 2 branch argument bundles, or none
```

**Context**:
This occurs in a large IF-ELSEIF chain (7+ branches) where multiple ELSEIF branches contain nested IF-ELSE statements.
Simpler nested IF structures work fine.

**Reproduction Pattern**:
The error appears with this structure:

- IF ... THEN (no nested IF)
- ELSEIF ... THEN (with nested IF)
- ELSEIF ... THEN (no nested IF)
- ELSEIF ... THEN (no nested IF)
- ELSEIF ... THEN (with nested IF-ELSE)
- ELSEIF ... THEN (with nested IF-ELSE) <- Error occurs here (6th branch)
- ELSE
- END IF

**Impact**:
Cannot create complex branching logic combining multiple ELSEIF branches with nested conditionals. This significantly
limits the complexity of programs that can be written.

**Analysis**:
The IL code generation succeeds (front basic -emit-il works), but VM execution fails. This suggests either:

1. The IL verifier has incorrect validation logic for complex branch structures
2. The BASIC frontend generates malformed IL for certain nesting patterns
3. There's a label management issue when many branches have nested conditionals

Since simpler nested structures work, it appears to be triggered by a combination of:

- Multiple ELSEIF branches (6+)
- Multiple branches containing nested conditionals
- Specific depth/complexity threshold

**Workaround**:
Restructure code to reduce nesting depth or split into separate IF statements.

---

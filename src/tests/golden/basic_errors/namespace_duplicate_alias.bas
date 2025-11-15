REM Test E_NS_004: duplicate alias
REM
REM NOTE: This error is difficult to demonstrate in a single-file golden test due to
REM       ordering constraints:
REM       1. USING must appear BEFORE any NAMESPACE declarations (E_NS_005)
REM       2. USING checks namespace existence BEFORE checking for duplicate alias (E_NS_001)
REM       3. Therefore, to test E_NS_004, we'd need namespaces to exist before USING them
REM       4. But declaring namespaces in the same file violates rule #1
REM
REM The E_NS_004 error is properly tested in unit tests (test_namespace_diagnostics.cpp)
REM where source can be constructed to simulate multi-file scenarios.
REM
REM This golden test demonstrates the E_NS_001 error that fires first:

USING Coll = NonExistent1
USING Coll = NonExistent2

END

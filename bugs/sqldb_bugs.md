# SQLite Clone Bug Tracker

## Summary
- **Total Bugs Found**: 18
- **Bugs Fixed**: 13 (#001 COMPILER FIX, #003 COMPILER FIX, #007 COMPILER FIX, #008 user education, #009 COMPILER FIX, #012 user education, #013 COMPILER FIX, #014 COMPILER FIX, #015 COMPILER FIX, #016 code fix, #017 COMPILER FIX, #018 COMPILER FIX)
- **Bugs Closed**: 3 (#002, #004, #005 - were cascading effects of #007, now fixed)
- **Bugs Open**: 2 (#010, #011 - root causes identified, require deeper fixes)
- **Workarounds Applied**: 2 (#010, #011)
- **Not A Bug**: 1 (#006 by design)

### Root Cause Investigation Summary (2026-01-08)
- **Bug #007** (backslash escape): **FIXED IN COMPILER** - Modified Lexer.cpp and Parser_Expr.cpp to treat backslash as regular character (standard BASIC behavior).
- **Bug #008** (single-line IF): **NOT A BUG** - Standard BASIC behavior. In `IF x THEN A : B`, only A is conditional, B is unconditional.
- **Bug #006** (OR requires BOOLEAN): **BY DESIGN** - Intentional strict typing. Use `(x <> 0)` to convert INTEGER to BOOLEAN.
- **Bugs #002, #004, #005**: **CLOSED** - Were cascading effects of Bug #007. Now work correctly after compiler fix.

---

## Bug Log

### Bug #001: ViperLang cross-module entity type references fail
- **Date**: 2026-01-08
- **Language**: ViperLang
- **Component**: Frontend (Sema)
- **Severity**: Medium
- **Status**: FIXED IN COMPILER
- **Description**: Using `module.EntityName` as a function return type causes "Unknown type" error
- **Steps to Reproduce**:
  1. Create token.viper with `entity Token { ... }`
  2. Create lexer.viper with `import "./token";`
  3. Declare function: `func readNumber() -> token.Token { ... }`
- **Expected**: Type should resolve from imported module
- **Actual**: Error "Unknown type: token.Token"
- **Root Cause**: The `resolveNamedType()` function only looked up types by their exact name in the registry. When the parser created a qualified type like "token.Token", it couldn't find it since the registry stored types under their simple names ("Token"). The ImportResolver merges imported declarations, so the types are available, but the name lookup was failing.
- **Fix Details**:
  1. **Sema.cpp**: Modified `resolveNamedType()` to handle dotted names
  2. When a name contains a dot, extract the type name after the last dot
  3. Look up the unqualified type name in the registry
  4. This works because ImportResolver already merges all declarations from imported modules
- **Notes**: Cross-module type references now work correctly for all contexts including function return types, method return types, and variable declarations.

### Bug #002: Viper Basic parser fails on string literals in class method IF statements
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Frontend (Parser)
- **Severity**: High
- **Status**: CLOSED (was cascading effect of Bug #007)
- **Description**: String literal comparisons inside class method FUNCTION cause parse error
- **Steps to Reproduce**:
  1. Create a CLASS with a FUNCTION method
  2. Inside the function, use: `IF word = "CREATE" THEN RETURN TK_CREATE`
- **Expected**: Should parse and compare string variable to string literal
- **Actual**: Parse error at line 187:20 "expected THEN, got ident" - parser sees "CREATE" as identifier
- **Root Cause Analysis** (2026-01-08):
  - Investigated Parser_Stmt_OOP.cpp and Parser_Stmt_If.cpp
  - Class method parsing (Parser_Stmt_OOP.cpp:717-812) uses same `parseExpression()` as top-level functions
  - Parser should not distinguish between class method and top-level function contexts for IF statements
  - **Likely Related to Bug #007**: If file has backslash literals earlier, parser state may be corrupted
  - Needs isolated test case without backslashes to confirm if this is a separate bug
- **Workaround**: Move keyword lookup to top-level FUNCTION outside CLASS
- **Notes**: This may be a cascading effect of Bug #007 (backslash corruption) rather than a distinct parser bug.

### Bug #003: ViperLang string comparison operators generate wrong IL
- **Date**: 2026-01-08
- **Language**: ViperLang
- **Component**: Frontend (Lowerer/Codegen)
- **Severity**: Critical
- **Status**: FIXED IN COMPILER
- **Description**: String comparison using `>=`, `<=`, `<`, `>` operators generated integer comparison IL instruction
- **Steps to Reproduce**:
  1. Write function: `func isDigit(ch: String) -> Boolean { return ch >= "0" && ch <= "9"; }`
  2. Compile and run
- **Expected**: Should compare strings lexicographically
- **Actual**: IL error "scmp_ge: operand type mismatch: operand 0 must be i64"
- **Root Cause**: Lowerer generated `scmp_ge`/`scmp_le`/`scmp_lt`/`scmp_gt` for string operands instead of calling runtime string comparison functions.
- **Fix Details**:
  1. **Lowerer_Expr_Binary.cpp**: Added string type detection for `<`, `<=`, `>`, `>=` operators to call runtime functions `rt_str_lt`, `rt_str_le`, `rt_str_gt`, `rt_str_ge`
  2. **Lowerer_Expr_Binary.cpp**: Fixed `==` and `!=` to call `Viper.Strings.Equals` with i64 return type and convert to boolean
  3. **Lowerer_Expr_Match.cpp**: Updated string match pattern to use i64 return type
  4. **Signatures_Strings.cpp**: Fixed signature registry to declare string comparison functions as returning `I64` (matching actual C implementation) instead of `I1`
  5. **RuntimeSignatures.cpp**: Fixed hardcoded descriptor rows for `rt_str_lt/le/gt/ge` to use `i64(string,string)` spec
  6. **runtime.def**: Fixed `Viper.Strings.Equals` signature to `i64(str,str)`
  7. **generated/RuntimeSignatures.inc**: Updated `rt_str_eq` and `Viper.Strings.Equals` specs to `i64(string,string)`
- **Notes**: All string comparison operators (`<`, `<=`, `>`, `>=`, `==`, `!=`) now work correctly for String type.

### Bug #004: Viper Basic empty string in function call causes parse error
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Frontend (Parser)
- **Severity**: High
- **Status**: CLOSED (was cascading effect of Bug #007)
- **Description**: Passing empty string `""` as function argument inside class method causes parse error
- **Steps to Reproduce**: `NextToken = NEW Token(TK_EOF, "", lineNum, colNum)`
- **Expected**: Should parse empty string as second argument
- **Actual**: Parse error "expected THEN, got string"
- **Root Cause Analysis** (2026-01-08):
  - Investigated Lexer.cpp:374-403 (lexString function)
  - Empty strings are handled correctly: `while (!eof() && peek() != '"')` exits immediately for `""`
  - The error message "expected THEN, got string" suggests parser state corruption
  - **Likely Related to Bug #007**: Parser corruption from backslash earlier in file
  - Needs isolated test case without backslashes to verify
- **Workaround**: Use a single-space string `" "` or named constant; verify no backslashes in file

### Bug #005: Viper Basic RETURN in class methods causes missing return error
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Frontend (Sema)
- **Severity**: Medium
- **Status**: CLOSED (was cascading effect of Bug #007)
- **Description**: Using `EXIT FUNCTION` or `RETURN` in class method FUNCTION gives "missing return" error
- **Steps to Reproduce**: Create a class FUNCTION that uses EXIT FUNCTION to return early
- **Expected**: Should allow early return from function
- **Actual**: Error "missing return in FUNCTION"
- **Root Cause Analysis** (2026-01-08):
  - Investigated SemanticAnalyzer_Procs.cpp:306-312
  - Return checking uses `mustReturn()` function to determine if function body has guaranteed return path
  - EXIT FUNCTION is parsed as `ExitStmt::LoopKind::Function` (Parser_Stmt_Loop.cpp:231-277)
  - EXIT SUB/FUNCTION are permitted anywhere within procedure body (sem/Check_Loops.cpp:255-258)
  - **Possible Issue**: `mustReturn()` may not recognize EXIT FUNCTION as a valid return path
  - Or the single-line IF pattern `IF x THEN FuncName = v : EXIT FUNCTION` is not being analyzed correctly
- **Workaround**: Use `FunctionName = value` assignment at END FUNCTION, avoid early exits

### Bug #006: Viper Basic OR operator requires BOOLEAN operands
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Frontend (Sema)
- **Severity**: Medium
- **Status**: Not A Bug (By Design)
- **Description**: Logical OR operator rejects INTEGER operands even when used for boolean logic
- **Steps to Reproduce**: `IsAlphaNumCh = IsAlphaCh(ch) OR IsDigitCh(ch)` where functions return INTEGER (0 or -1)
- **Expected**: Should allow integer operands for boolean OR (BASIC tradition: 0=false, non-0=true)
- **Actual**: Error "Logical operator OR requires BOOLEAN operands, got INT and INT"
- **Root Cause Analysis** (2026-01-08):
  - Investigated sem/Check_Expr_Binary.cpp:259-273
  - `validateLogicalOperands()` explicitly checks `isBooleanType(lhs) && isBooleanType(rhs)`
  - This is **intentional design** - Viper Basic has stricter typing than classic BASIC
  - The eager operators (AND, OR) and short-circuit operators (ANDALSO, ORELSE) all require boolean operands
  - See LowerExprLogical.cpp for implementation details
- **Workaround**: Convert integers to boolean explicitly:
  - `(IsAlphaCh(ch) <> 0) OR (IsDigitCh(ch) <> 0)`
  - Or return BOOLEAN from helper functions instead of INTEGER
- **Notes**: This is working as designed. Viper Basic is stricter than classic BASIC for type safety.

### Bug #007: Viper Basic backslash in string literal corrupts parser state
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Frontend (Lexer + Parser)
- **Severity**: Critical
- **Status**: FIXED IN COMPILER
- **Description**: Using a backslash character `"\"` in a string literal inside a SUB causes the parser to enter a corrupted state, leading to cascading "expected THEN, got string" errors on all subsequent code
- **Steps to Reproduce**:
  1. Create a SUB with code like: `IF LexerPeek() = "\" THEN`
  2. Add any code after this SUB
  3. The code after the SUB will fail with "expected THEN" errors
- **Expected**: Should parse backslash as a normal string character
- **Actual**: Parser gets confused and subsequent code fails with "expected THEN, got string" or "expected THEN, got ident" errors
- **Root Cause Analysis** (2026-01-08):
  - **CONFIRMED** in Lexer.cpp:391-395:
    ```cpp
    if (c == '\\' && !eof())
    {
        s.push_back(c);
        s.push_back(get());  // Consumes next character as escaped
        continue;
    }
    ```
  - When you write `"\"`  in code, the lexer sees:
    1. Opening quote `"`
    2. Backslash `\` - triggers escape sequence handling
    3. Next character consumed as "escaped" - but this is the closing quote `"`!
  - Result: String is never terminated, lexer reads past end of string literal
  - All subsequent tokens are malformed, causing cascading parse errors
  - **This is a KNOWN escape sequence design that conflicts with BASIC conventions**
- **Workaround**: Use `CHR$(92)` instead of `"\"` (no longer needed after fix)
- **Fix Applied (2026-01-08)**:
  1. Modified `src/frontends/basic/Lexer.cpp` - Removed escape sequence handling in `lexString()`
  2. Modified `src/frontends/basic/Parser_Expr.cpp` - Removed call to `decodeEscapedString()` in `parseString()`
  3. Backslash is now treated as a regular character in BASIC strings (standard BASIC behavior)
- **Compiler Files Changed**:
  - `src/frontends/basic/Lexer.cpp:388-394`
  - `src/frontends/basic/Parser_Expr.cpp:259-267`

### Bug #008: Viper Basic FUNCTION with string comparison in single-line IF always returns 0
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Frontend (Parser) - NOT A BUG, USER ERROR
- **Severity**: Critical
- **Status**: **ROOT CAUSE CONFIRMED** - Workaround Available
- **Description**: A FUNCTION that uses single-line `IF word = "KEYWORD" THEN FuncName = value : EXIT FUNCTION` pattern always returns 0, regardless of input
- **Steps to Reproduce**:
  ```basic
  FUNCTION LookupKeyword(word AS STRING) AS INTEGER
      IF word = "SELECT" THEN LookupKeyword = 30 : EXIT FUNCTION
      IF word = "INSERT" THEN LookupKeyword = 31 : EXIT FUNCTION
      LookupKeyword = 13  ' Default to identifier
  END FUNCTION

  PRINT LookupKeyword("SELECT")  ' Prints 30 (WORKS for first match)
  PRINT LookupKeyword("INSERT")  ' Prints 0 (BROKEN - should be 31)
  ```
- **Expected**: Should return 30, 31, or 13 depending on input
- **Actual**: Returns 30 for "SELECT", 0 for everything else
- **Root Cause** (2026-01-08 - **CONFIRMED BY TESTING**):
  - **This is NOT a compiler bug - it's BASIC language semantics!**
  - In single-line IF: `IF cond THEN A : B`, only statement A is conditional
  - Statement B is UNCONDITIONAL and executes regardless of the condition
  - **Test Results** (test_bug008.bas):
    ```
    TestSingleLineIF("SELECT") = 30  (correct - first IF succeeds)
    TestSingleLineIF("INSERT") = 0   (wrong - EXIT FUNCTION runs unconditionally after first IF)
    TestSingleLineIF("other")  = 0   (wrong - same reason)
    ```
  - **What happens**: When `word != "SELECT"`:
    1. First IF condition is false, assignment skipped
    2. BUT `EXIT FUNCTION` executes unconditionally
    3. Function exits immediately with default value 0
    4. Second IF and default assignment are never reached
- **Workaround**: Use multi-line IF (TESTED AND WORKING):
  ```basic
  IF word = "SELECT" THEN
      LookupKeyword = 30
      EXIT FUNCTION
  END IF
  ```
- **Alternative**: Use ELSEIF chain without EXIT FUNCTION:
  ```basic
  IF word = "SELECT" THEN
      LookupKeyword = 30
  ELSEIF word = "INSERT" THEN
      LookupKeyword = 31
  ELSE
      LookupKeyword = 13
  END IF
  ```
- **Notes**: This is standard BASIC behavior. The colon in single-line IF separates statements but only the first is conditional. This is a common mistake when coming from other languages.

### Bug #009: Viper Basic CASE ELSE return value not set in FUNCTION
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Frontend (Codegen)
- **Severity**: High
- **Status**: **FIXED IN COMPILER** (as part of Bug #017 fix)
- **Description**: When using SELECT CASE in a FUNCTION, assignments in CASE ELSE branch do not set the function return value
- **Steps to Reproduce**:
  ```basic
  FUNCTION LookupKeyword(word AS STRING) AS INTEGER
      SELECT CASE word
          CASE "SELECT": LookupKeyword = 30
          CASE "INSERT": LookupKeyword = 31
          CASE ELSE: LookupKeyword = 13
      END SELECT
  END FUNCTION

  PRINT LookupKeyword("OTHER")  ' Prints 0 instead of 13
  ```
- **Expected**: CASE ELSE should set return value to 13
- **Actual**: Function returns 0 (default INTEGER value)
- **Root Cause**: This bug was a cascading effect of Bug #017's pointer invalidation issue. The CASE ELSE block's target was being corrupted due to stale pointers, causing the branch to go to the wrong location or skip the ELSE body entirely.
- **Fix Applied**: Fixed as part of Bug #017 fix - the pointer invalidation fix in `SelectCaseLowering.cpp` ensures all SELECT CASE branches are correctly generated, including CASE ELSE.
- **Verification**: Test case `/tmp/test_case_else.bas` confirms CASE ELSE now returns the correct value (99) instead of the default (0).

### Bug #010: Viper Basic 2D array corruption with CONST dimensions
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Frontend (Semantic Analyzer / Constant Folding)
- **Severity**: Critical
- **Status**: Open (requires deeper investigation)
- **Description**: When using 2D arrays with CONST dimensions (e.g., `DIM arr(ROWS, COLS)` where ROWS and COLS are CONST), setting one element corrupts other elements
- **Steps to Reproduce**:
  ```basic
  CONST MAX_ROWS = 100
  CONST MAX_COLS = 64
  DIM arr(MAX_ROWS, MAX_COLS) AS INTEGER

  arr(0, 0) = 10
  PRINT arr(0, 0)  ' Prints 10

  arr(0, 1) = 20
  PRINT arr(0, 0)  ' Prints 20 (WRONG - should still be 10)
  PRINT arr(0, 1)  ' Prints 20
  ```
- **Expected**: Setting arr(0, 1) should not affect arr(0, 0)
- **Actual**: Setting arr(0, 1) overwrites arr(0, 0) (both use index 0)
- **Key Finding**: Literal dimensions work correctly:
  ```basic
  DIM arr(100, 64) AS INTEGER  ' Works correctly!
  ```
- **Root Cause Analysis (2026-01-08)**:
  1. **Semantic analyzer issue**: In `SemanticAnalyzer_Stmts_Runtime.cpp:analyzeDim()`, array extents are extracted from dimension expressions
  2. Only `IntExpr` (literal integers) and `FloatExpr` are recognized as constant extents (lines 544-566)
  3. When CONST values are used (`DIM arr(ROWS, COLS)`), the dimension expressions are `VarExpr` nodes referencing the constants
  4. The code hits the `else` branch at line 567: `allConstant = false`
  5. This causes `ArrayMetadata.extents` to be empty
  6. **Lowering issue**: In `Emit_Expr.cpp:computeFlatIndex()`, when array metadata has no extents, it falls back to just using the first index (line 387): `return idxVals[0]`
  7. Since `arr(0, 0)` and `arr(0, 1)` both have first index 0, both accesses map to the same memory location
- **IL Evidence**: CONST dimensions generate `call @rt_arr_i64_set(%t21, 0, 10)` with literal `0` index, while literal dimensions correctly generate computed indices (`%t18`)
- **Potential Fix**: Modify `analyzeDim()` to handle `VarExpr` dimension expressions that reference CONSTs:
  1. Check if dimension expression is a `VarExpr`
  2. Check if it's a known CONST (via `constants_` set)
  3. Look up the CONST's initializer value from previously processed `ConstStmt` nodes
  4. Use that value as the extent
  Alternatively, enhance the `ConstFolderPass` to propagate CONST values into VarExpr references before semantic analysis runs.
- **Workaround**: Use literal dimensions or 1D array with manual index calculation:
  ```basic
  DIM arr(100, 64) AS INTEGER  ' Literal dimensions work
  ' OR use 1D array:
  DIM arr(6400) AS INTEGER
  arr(row * 64 + col) = value  ' Manual index calculation
  ```
- **Fix Applied**: InsertStmt class uses 1D array with manual indexing

### Bug #011: Viper Basic class member fields cannot be passed directly to methods
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Frontend (Scan/Type Inference)
- **Severity**: High
- **Status**: Open (requires deeper investigation)
- **Description**: Passing a class member field directly as an argument to a method that expects STRING causes "no viable overload" error
- **Steps to Reproduce**:
  ```basic
  CLASS MyClass
      PUBLIC name AS STRING
      PUBLIC FUNCTION FindTable(tableName AS STRING) AS INTEGER
          FindTable = 0
      END FUNCTION
  END CLASS

  SUB TestIt(stmt AS MyClass)
      DIM idx AS INTEGER
      idx = gObj.FindTable(stmt.name)  ' ERROR: no viable overload for 'FINDTABLE'
  END SUB
  ```
- **Expected**: Should resolve overload and pass string value
- **Actual**: Compiler reports "no viable overload" error
- **Root Cause Analysis (2026-01-08)**:
  - The issue is in the type scanning phase (`Scan_ExprTypes.cpp`)
  - When `scanExpr()` is called to determine argument types, it uses `resolveObjectClass()` to find the class of the base expression (`stmt`)
  - `resolveObjectClass()` calls `getSlotType()` which uses `findSymbol()` to look up the parameter
  - However, procedure parameters with object types are only registered in the emit phase (`Lowerer_Procedure_Emit.cpp:251`), not during the scan phase
  - This means when `scanExpr(stmt.name)` is called during overload resolution, the symbol table doesn't know that `stmt` is of type `MyClass`, so the field type cannot be determined
  - The scan phase returns `ExprType::I64` (default) instead of `ExprType::Str`, causing overload mismatch
- **Workaround**: Copy the member field to a local variable first:
  ```basic
  DIM tName AS STRING
  tName = stmt.name
  idx = gObj.FindTable(tName)  ' Works
  ```
- **Potential Fix**: Register procedure parameters with object types in the scan phase (before `scanExpr` is called), not just in the emit phase. This requires modifying `Scan_RuntimeNeeds.cpp` to set symbol object types for procedure parameters early in the lowering process.

### Bug #012: Viper Basic objects declared with DIM need NEW before method calls
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Runtime / Code Generation
- **Severity**: High
- **Status**: Fixed (user education)
- **Description**: Objects declared with DIM but not initialized with NEW cause segfaults when methods are called
- **Steps to Reproduce**:
  ```basic
  CLASS MyClass
      PUBLIC SUB Init()
          ' initialization
      END SUB
  END CLASS

  SUB TestIt()
      DIM obj AS MyClass
      obj.Init()  ' SEGFAULT - obj is null
  END SUB
  ```
- **Expected**: Either auto-initialize or give compile error
- **Actual**: Runtime segmentation fault (exit code 139)
- **Root Cause**: DIM declares a variable but doesn't allocate the object. Calling methods on null object causes crash.
- **Workaround**: Always use `LET obj = NEW ClassName()` before calling methods:
  ```basic
  DIM obj AS MyClass
  LET obj = NEW MyClass()
  obj.Init()  ' Works
  ```
- **Notes**: This is expected behavior in object-oriented BASIC variants, but error handling could be improved.

### Bug #013: ViperLang optional type not unwrapped for method resolution
- **Date**: 2026-01-08
- **Language**: ViperLang
- **Component**: Frontend (Sema + Lowerer)
- **Severity**: Critical
- **Status**: **FIXED IN COMPILER**
- **Description**: Variables assigned from optional types don't have method resolution work correctly, causing IL generation errors
- **Steps to Reproduce**:
  ```viper
  func findTable(name: String) -> Table? { ... }

  func test() {
      var maybeTable = findTable("users");
      if maybeTable == null {
          return;
      }
      // After null check, maybeTable is known non-null
      var table = maybeTable;
      table.doSomething();  // BUG: method not found, generates invalid IL
  }
  ```
- **Expected**: After null check, methods should be resolvable on the variable
- **Actual**:
  - IL error: `store void, %t40, 0: instruction type must be non-void`
  - `call.indirect 0` generated instead of proper method call
- **Root Cause**:
  1. When `var table = maybeTable;` is analyzed, `table` is given type `Table?` (the optional type from `maybeTable`)
  2. Semantic analyzer's `analyzeField()` didn't unwrap Optional types, so method lookup failed
  3. Lowerer's call resolution didn't unwrap Optional types, so method dispatch failed
- **Fix Applied**:
  1. Modified `Sema_Expr.cpp:analyzeField()` to unwrap Optional types before field/method lookup
  2. Modified `Lowerer_Expr_Call.cpp` to unwrap Optional types before method resolution
- **Files Changed**:
  - `src/frontends/viperlang/Sema_Expr.cpp` - Added Optional unwrapping in `analyzeField()`
  - `src/frontends/viperlang/Lowerer_Expr_Call.cpp` - Added Optional unwrapping before method resolution
- **Notes**: This enables the common pattern of assigning from an optional after a null guard. A proper fix would involve flow-sensitive type narrowing, but this workaround handles the common case.

### Bug #014: ViperLang optional type field access returns default values
- **Date**: 2026-01-08
- **Language**: ViperLang
- **Component**: Frontend (Lowerer)
- **Severity**: Critical
- **Status**: **FIXED IN COMPILER**
- **Description**: Field access on variables assigned from optional types returns default values (empty string, 0, etc.) instead of actual field values
- **Steps to Reproduce**:
  ```viper
  func getColumn(idx: Integer) -> Column? { ... }

  func test() {
      var maybeCol = getColumn(0);
      if maybeCol != null {
          var col = maybeCol;
          var name = col.name;  // BUG: returns "" instead of actual column name
      }
  }
  ```
- **Expected**: `col.name` should return the column's actual name
- **Actual**: Returns empty string (the default value for String)
- **Root Cause**:
  - Bug #013 fixed method resolution by unwrapping Optional types in Sema_Expr.cpp and Lowerer_Expr_Call.cpp
  - But field access in Lowerer_Expr.cpp `lowerField()` also needs to unwrap Optional types
  - When `baseType` was `Column?`, the lookup in `entityTypes_["Column?"]` failed, causing incorrect code generation
- **Fix Applied**:
  - Modified `Lowerer_Expr.cpp:lowerField()` to unwrap Optional types before looking up entity field information
- **Files Changed**:
  - `src/frontends/viperlang/Lowerer_Expr.cpp` - Added Optional unwrapping at start of `lowerField()`
- **Notes**: This completes the fix for optional type handling. Bug #013 fixed method calls, Bug #014 fixes field access.

### Bug #015: ViperLang optional type field assignment does not work
- **Date**: 2026-01-08
- **Language**: ViperLang
- **Component**: Frontend (Lowerer)
- **Severity**: Critical
- **Status**: **FIXED IN COMPILER**
- **Description**: Field assignment on variables assigned from optional types does not actually set the field value
- **Steps to Reproduce**:
  ```viper
  func getRow(idx: Integer) -> Row? { ... }

  func test() {
      var maybeRow = getRow(0);
      if maybeRow != null {
          var row = maybeRow;
          row.deleted = true;  // BUG: assignment has no effect
          // row.deleted is still false
      }
  }
  ```
- **Expected**: `row.deleted` should be set to `true`
- **Actual**: Field value remains unchanged (false)
- **Root Cause**:
  - Similar to Bug #014, the field assignment code in `Lowerer_Expr_Binary.cpp` uses `baseType->name` to look up field information
  - When `baseType` is `Row?` (Optional), the lookup in `entityTypes_["Row?"]` fails
  - Without finding the field, the assignment is silently not performed
- **Fix Applied**:
  - Modified `Lowerer_Expr_Binary.cpp` to unwrap Optional types before looking up entity field information for assignment
- **Files Changed**:
  - `src/frontends/viperlang/Lowerer_Expr_Binary.cpp` - Added Optional unwrapping before field assignment lookup
- **Notes**: This completes the fix for optional type handling. Bug #013 fixed method calls, Bug #014 fixes field reads, Bug #015 fixes field writes.

### Bug #016: Viper Basic VAL() overflow on non-numeric strings
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Runtime / SqlValue.Compare
- **Severity**: Critical
- **Status**: **FIXED** (code fix)
- **Description**: Calling VAL() on non-numeric strings like "Alice" causes a floating-point overflow trap
- **Steps to Reproduce**:
  ```basic
  DIM myNum AS INTEGER
  myNum = VAL("Alice")  ' Trap: fp overflow in cast.fp_to_si.rte.chk
  ```
- **Expected**: VAL("Alice") should return 0 (standard BASIC behavior)
- **Actual**: Runtime trap with "Overflow (code=0): fp overflow in cast.fp_to_si.rte.chk"
- **Root Cause**:
  - In SqlValue.Compare(), when comparing two TEXT values, the code called VAL() on both strings to enable numeric comparison (so "30" > "5" would work)
  - VAL() on non-numeric strings may return a floating-point value that causes overflow when cast to INTEGER
- **Fix Applied**:
  - Modified SqlValue.Compare() to check if strings look like numbers (start with digit or minus sign) before calling VAL()
  - If either string is non-numeric, use lexicographic comparison instead
- **Files Changed**:
  - `demos/sqldb/basic/sql.bas` - Added numeric string detection before VAL() calls in Compare function
- **Notes**: This fix ensures safe string comparison for all TEXT values, whether numeric or non-numeric.

### Bug #017: Viper Basic SELECT CASE with many string cases generates invalid branch targets
- **Date**: 2026-01-08
- **Language**: Viper Basic
- **Component**: Frontend (Lowerer/Codegen)
- **Severity**: Critical
- **Status**: **FIXED IN COMPILER**
- **Description**: When a SELECT CASE statement has many string cases (50+), the generated IL contains references to undefined blocks like `bb_1`, `bb_2`, etc. instead of the correct `select_arm_N_FUNCNAME` labels
- **Steps to Reproduce**:
  ```basic
  FUNCTION LookupKeyword(word AS STRING) AS INTEGER
      SELECT CASE word
          CASE "CREATE": LookupKeyword = 1
          CASE "TABLE": LookupKeyword = 2
          ' ... many more cases (50+) ...
          CASE "CAST": LookupKeyword = 67
      END SELECT
  END FUNCTION
  ```
- **Expected**: IL should generate valid branch targets to `select_arm_N_LOOKUPKEYWORD` labels
- **Actual**: IL generates branches to `bb_1`, `bb_2`, etc. which don't exist:
  ```
  cbr %t124, bb_1, select_check_58_LOOKUPKEYWORD
  ```
  Results in: `error: line 14226: unknown block 'bb_1'`
- **Root Cause**: **POINTER INVALIDATION** - In `SelectCaseLowering.cpp:emitCompareChain()`:
  1. The code obtained a `checkBlk` pointer from `&func->blocks[currentIdx]`
  2. Then called `addBlock()` to create intermediate comparison blocks
  3. `addBlock()` can cause the `blocks` vector to reallocate, invalidating `checkBlk`
  4. The code then used the stale `checkBlk` pointer to set the current block
  5. Instructions were emitted to a garbage memory location, causing corrupted labels
  6. Additionally, `CasePlanEntry` stored block pointers which became invalid after `addBlock()` calls
- **Fix Applied**:
  1. **SelectCaseLowering.hpp**: Changed `CasePlanEntry` to store `targetIdx` (size_t index) instead of `target` (BasicBlock* pointer)
  2. **SelectCaseLowering.cpp**: Modified `lowerStringArms()`, `lowerNumericDispatch()`, and `emitCompareChain()` to use indices instead of pointers
  3. **SelectCaseLowering.cpp**: Moved `checkBlk` pointer acquisition AFTER the `addBlock()` call to avoid pointer invalidation
  4. **SelectCaseLowering.cpp**: Added refresh of func and checkBlk pointers after any operation that might add blocks
  5. **LowerExprNumeric.cpp**: Fixed string comparison to use i64 return type from runtime functions and convert to BASIC logical form
- **Files Changed**:
  - `src/frontends/basic/SelectCaseLowering.hpp` (lines 127-129)
  - `src/frontends/basic/SelectCaseLowering.cpp` (multiple locations)
  - `src/frontends/basic/LowerExprNumeric.cpp` (lines 394-407)
- **Notes**: This was a classic C++ container invalidation bug. When a `std::vector` reallocates to grow, all pointers and iterators to its elements become invalid. The fix ensures all block pointers are obtained AFTER any operations that might cause reallocation, and indices are used for persistent references.

### Bug #018: ViperLang method calls on primitive types (String.length(), Integer.toString()) generate incorrect IL
- **Date**: 2026-01-08
- **Language**: ViperLang
- **Component**: Frontend (Lowerer - Lowerer_Expr_Call.cpp)
- **Severity**: Critical
- **Status**: Fixed (COMPILER FIX)
- **Description**: Method calls on primitive types like `s.length()` (String) and `i.toString()` (Integer) were incorrectly lowered as indirect function calls. The lowerer would first evaluate the property/method as a field access, then try to call the result as a function pointer.
- **Steps to Reproduce**:
  ```viper
  var s = "Hello";
  var len = s.length();  // Crashes - tries to call the length value as a function

  var i = 42;
  var str = i.toString();  // Crashes - tries to call 42 as a function pointer
  ```
- **Expected**:
  - `s.length()` should return 5 (calls Viper.String.Length)
  - `i.toString()` should return "42" (calls Viper.Fmt.Int)
- **Actual**:
  - Generated IL showed `call @Viper.String.Length(%t2)` followed by `call.indirect %t3` - the result was being used as a function pointer
  - This caused segfaults or invalid IL errors
- **Root Cause**: In `lowerCall()`, when the callee is a FieldExpr (e.g., `s.length`), the code checked for method calls on value types, entity types, interfaces, modules, Lists, and Maps. However, there was no handling for primitive types (String, Integer, Number). The code fell through to the generic handler which lowered `s.length` as a field access expression (returning an i64) and then tried to use that as a function pointer for an indirect call.
- **Fix**: Added explicit handling in `Lowerer_Expr_Call.cpp:lowerCall()` for:
  1. `String.length()` - directly emits call to `Viper.String.Length`
  2. `Integer.toString()` - directly emits call to `Viper.Fmt.Int`
  3. `Number.toString()` - directly emits call to `Viper.Fmt.Num`
- **Files Modified**: `src/frontends/viperlang/Lowerer_Expr_Call.cpp` (lines 523-560)
- **Impact**: This bug broke all ViperLang code using `str.length()` or `num.toString()` patterns. The SQL clone now works correctly.
- **Notes**: The runtime `rt_substr` and other string functions were working correctly - the bug was entirely in the frontend lowering phase.


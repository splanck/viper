# Telnet Demo Bug Tracking

This document tracks bugs discovered while developing the ViperLang telnet client/server demo.

## Overview

- **Demo Location**: `/compiler/demos/viperlang/telnet/`
- **Components**: server.viper, client.viper
- **Purpose**: Simple telnet server/client with shell capabilities

---

## Bugs Found

### Bug #1: Missing TCP Functions in ViperLang Frontend

**Status**: Fixed

**Description**: The ViperLang frontend (Sema.cpp) was missing many TCP/Network function registrations that exist in the runtime. Functions like `SendStr`, `RecvStr`, `RecvLine`, `get_IsOpen`, `get_Available`, `SetRecvTimeout`, and all `TcpServer` functions were not exposed.

**Root Cause**: Manual registration mismatch between runtime and frontend.
- Runtime functions are defined in `/src/il/runtime/runtime.def` using macros like `RT_FN()`
- Frontend registers available functions manually in `/src/frontends/viperlang/Sema.cpp` `Sema::Sema()` constructor
- The runtime had 26 TCP functions but Sema.cpp only registered 6 basic ones
- This is an architectural issue: runtime additions don't automatically propagate to frontends

**Location**: `/src/frontends/viperlang/Sema.cpp:1160-1186` (after fix)

**Fix**: Added missing function registrations to `/src/frontends/viperlang/Sema.cpp` lines 1160-1186:
- Added TCP Client functions: `ConnectFor`, `get_Host`, `get_Port`, `get_LocalPort`, `get_IsOpen`, `get_Available`, `SendStr`, `SendAll`, `RecvStr`, `RecvExact`, `RecvLine`, `SetRecvTimeout`, `SetSendTimeout`
- Added TCP Server functions: `Listen`, `ListenAt`, `get_Port`, `get_Address`, `get_IsListening`, `Accept`, `AcceptFor`, `Close`

**Proper Fix**: Consider auto-generating frontend bindings from `runtime.def` to prevent future drift.

**Fixed**: [x]

---

### Bug #2: Wrong API Names for Common Operations

**Status**: Fixed

**Description**: Several API names were incorrect:
- `Viper.Console.*` doesn't exist - should be `Viper.Terminal.*`
- `Viper.Time.Now()` doesn't exist - should be `Viper.DateTime.Now()`
- `Viper.Time.Clock.Now` was registered in Sema.cpp but doesn't exist in runtime

**Root Cause**: Phantom registrations in Sema.cpp - dead code that referenced non-existent runtime functions.
- `Sema.cpp` contained registrations for functions that don't exist in `runtime.def`
- Examples: `Viper.Console.*` (5 functions), `Viper.Time.Clock.Now`
- These were likely leftover from earlier API designs that were changed
- No validation exists to detect frontend registrations without matching runtime implementations
- The runtime uses `Viper.Terminal.*` namespace, not `Viper.Console.*`

**Location**: `/src/frontends/viperlang/Sema.cpp` (phantom registrations have since been removed/corrected)

**Fix**: Updated code to use correct API names:
- `Viper.Console.PrintStr()` -> `Viper.Terminal.Print()`
- `Viper.Console.PrintI64()` -> `Viper.Terminal.PrintInt()`
- `Viper.Console.ReadLine()` -> `Viper.Terminal.ReadLine()`
- `Viper.Time.Now()` -> `Viper.DateTime.Now()`

**Proper Fix**: Add build-time validation that all Sema.cpp registrations have corresponding runtime.def entries.

**Fixed**: [x]

---

### Bug #3: Property Syntax Not Supported for Strings

**Status**: ✅ FIXED

**Description**: ViperLang doesn't support property syntax like `str.Length` for String types. Must use function call syntax instead.

**Expected**: `cmd.Length` should return string length
**Actual**: Compilation error - arithmetic operations fail

**Root Cause**: `analyzeField()` in Sema_Expr.cpp only handles List type properties, not String.
- Location: `/src/frontends/viperlang/Sema_Expr.cpp:687-696`
- The function has special handling for `List` type to support `.count` and `.size` properties:
  ```cpp
  if (baseType && baseType->kind == TypeKindSem::List) {
      if (expr->field == "count" || expr->field == "size") {
          return types::integer();
      }
  }
  ```
- String type has no equivalent handling for `.Length` property
- The function returns `types::unknown()` for all other types, causing downstream failures
- Additionally, `RuntimeNames.hpp:154` declares `kStringLength = "Viper.String.get_Length"` but Sema.cpp registers `"Viper.String.Length"` (inconsistent naming)

**Location**: `/src/frontends/viperlang/Sema_Expr.cpp:696-703` and `/src/frontends/viperlang/Lowerer_Expr.cpp:1661-1671`

**Fix Applied**:
1. Added String property handling to `analyzeField()` in Sema_Expr.cpp:
   ```cpp
   if (baseType && baseType->kind == TypeKindSem::String) {
       if (expr->field == "Length" || expr->field == "length") {
           return types::integer();
       }
   }
   ```
2. Added lowering for String.Length in Lowerer_Expr.cpp to synthesize runtime call

**Test**: `demos/viperlang/bug_tests/test_bug3_string_length.viper` - PASSED

**Fixed**: [x]

---

### Bug #4: Boolean Comparisons with Integer Literals

**Status**: Fixed

**Description**: Functions returning boolean values (like `Dir.Exists`, `File.Exists`, `Tcp.get_IsOpen`) cannot be compared with integer literals `0` or `1`. Must use `== true` or `== false`.

**Expected**: `if Viper.IO.Dir.Exists(path) == 0 { ... }` should work
**Actual**: Runtime type mismatch error

**Root Cause**: Semantic analysis doesn't validate operand types for comparison operators.
- Location: `/src/frontends/viperlang/Sema_Expr.cpp:201-208`
- Comparison operators (`==`, `!=`, `<`, etc.) unconditionally return `types::boolean()` without checking operand compatibility:
  ```cpp
  case BinaryOp::Eq:
  case BinaryOp::Ne:
  case BinaryOp::Lt:
  // ...
      return types::boolean();  // No type validation!
  ```
- This allows comparing `i1` (boolean) with `i64` (integer) to pass semantic analysis
- The lowerer (`Lowerer_Expr.cpp:608-660`) handles type extension correctly (zext i1 → i64)
- However, the IL verifier (`BranchVerifier.cpp`) may still flag type mismatches in some edge cases
- The safer approach is to use `true`/`false` literals which have proper i1 type

**Location**: `/src/frontends/viperlang/Sema_Expr.cpp:201-208`

**Fix**: Changed all boolean comparisons to use `== true` or `== false` instead of `== 1` or `== 0`.

**Proper Fix**: Add operand type validation in Sema to either reject or auto-coerce boolean/integer comparisons.

**Fixed**: [x]

---

### Bug #5: else-if Chain Code Generation Bug

**Status**: ✅ FIXED

**Description**: Complex else-if chains generate incorrect IL code. The compiler generates a fallthrough block that returns `0` (integer) instead of continuing to the else clause.

**Steps to Reproduce**: Create a function returning String with many `else if` branches. At runtime, receive error: "ret value type mismatch: expected str but got i64"

**Root Cause**: Incorrect implicit return value in `lowerFunctionDecl()`.
- Location: `/src/frontends/viperlang/Lowerer_Decl.cpp:250-261`
- When a function has unterminated code paths (e.g., control flow falls through without an explicit return), the lowerer emits an implicit return
- The original code ALWAYS emitted `emitRet(Value::constInt(0))` regardless of return type
- This caused "ret value type mismatch: expected str but got i64" when the function returned String

**Location**: `/src/frontends/viperlang/Lowerer_Decl.cpp:250-290` and `:494-529`

**Fix Applied**: Modified implicit return to use correct default value based on return type:
```cpp
switch (ilReturnType.kind) {
    case Type::Kind::I1:
        defaultValue = Value::constBool(false);
        break;
    case Type::Kind::I64:
    case Type::Kind::I16:
    case Type::Kind::I32:
        defaultValue = Value::constInt(0);
        break;
    case Type::Kind::F64:
        defaultValue = Value::constFloat(0.0);
        break;
    case Type::Kind::Str:
        defaultValue = Value::constStr("");  // <-- Fixed!
        break;
    case Type::Kind::Ptr:
        defaultValue = Value::null();
        break;
    // ...
}
```

**Test**: `demos/viperlang/bug_tests/test_bug5_elseif_chain.viper` - PASSED

**Fixed**: [x]

---

### Bug #6: Logical Operators `or` and `and` Not Recognized

**Status**: Fixed (Clarification: Both forms work)

**Description**: ViperLang uses `||` and `&&` for logical operators, not `or` and `and` as in some other languages.

**Expected**: `if a == "x" or a == "y"` should work
**Actual**: Compilation succeeds but generates incorrect code

**Root Cause**: This is actually NOT a bug - both `or`/`and` AND `||`/`&&` are valid in ViperLang!
- Location: `/src/frontends/viperlang/Lexer.cpp:247,273` - Keywords are tokenized:
  ```cpp
  {"and", TokenKind::KwAnd},
  {"or", TokenKind::KwOr},
  ```
- Location: `/src/frontends/viperlang/Parser_Expr.cpp:283,303` - Both forms are parsed:
  ```cpp
  while (match(TokenKind::PipePipe, &opTok) || match(TokenKind::KwOr, &opTok))
  while (match(TokenKind::AmpAmp, &opTok) || match(TokenKind::KwAnd, &opTok))
  ```
- Location: `/src/frontends/viperlang/Lowerer_Expr.cpp:775-793` - `BinaryOp::Or` is correctly lowered:
  ```cpp
  case BinaryOp::Or:
      // Boolean OR: zext to i64, perform OR, trunc back to i1
  ```
- The original issue was likely a different problem (perhaps operator precedence or a typo)
- Looking at the client IL output, `or` generates correct IL code

**Location**: Parser and Lowerer handle both forms correctly.

**Fix**: Use `||` and `&&` (both forms are valid, but `||`/`&&` is more conventional):
- `if a == "x" or a == "y"` -> `if a == "x" || a == "y"`
- `if x > 0 and y == 0` -> `if x > 0 && y == 0`

**Note**: The original issue may have been a red herring caused by a different problem (e.g., else-if bug, operator precedence).

**Fixed**: [x]

---

### Bug #7: Dir.List Returns Opaque Pointer

**Status**: ✅ FIXED

**Description**: `Viper.IO.Dir.List()` returns a raw `ptr` instead of a typed `List[String]`. Calling `.count()` or `.get(i)` on it generates broken IL code.

**Steps to Reproduce**:
```viper
var entries = Viper.IO.Dir.List(path);
while i < entries.count() {  // This generates broken code
    var entry = entries.get(i);  // This also fails
}
```

**Root Cause**: Runtime function registered with incorrect return type in Sema.cpp.
- Location: `/src/frontends/viperlang/Sema.cpp:1034`
  ```cpp
  runtimeFunctions_["Viper.IO.Dir.List"] = types::ptr();
  runtimeFunctions_["Viper.IO.Dir.ListSeq"] = types::ptr();
  ```
- The return type is `ptr` (raw pointer), not `List[String]`
- When calling `.count()` or `.get(i)` on a `ptr` type, semantic analysis doesn't know it's a list
- This causes `analyzeField()` and method resolution to fail
- The lowerer generates invalid IL because the type information is lost

**Location**: `/src/frontends/viperlang/Sema.cpp:1034-1040`

**Fix Applied**: Changed registration to use proper typed return in Sema.cpp:
```cpp
runtimeFunctions_["Viper.IO.Dir.List"] = types::list(types::string());
runtimeFunctions_["Viper.IO.Dir.ListSeq"] = types::list(types::string());
runtimeFunctions_["Viper.IO.Dir.Files"] = types::list(types::string());
runtimeFunctions_["Viper.IO.Dir.FilesSeq"] = types::list(types::string());
runtimeFunctions_["Viper.IO.Dir.Dirs"] = types::list(types::string());
runtimeFunctions_["Viper.IO.Dir.DirsSeq"] = types::list(types::string());
```

**Test**: `demos/viperlang/bug_tests/test_bug7_list_simple.viper` - PASSED
(Note: Dir.List runtime function has a separate crash issue unrelated to the type fix)

**Fixed**: [x]

---

### Bug #8: List Element Type Inference

**Status**: Fixed

**Description**: When retrieving items from a List, must use explicit `String` type annotation or `Viper.Box.ToStr()` to properly convert the boxed value to a string.

**Expected**: `var entry = parts.get(i)` should infer String type from `List[String]`
**Actual**: Returns ptr/boxed value, string operations fail

**Root Cause**: List's `.get()` method returns boxed values, and the lowerer needs type hints to unbox correctly.
- ViperLang's List is implemented as a heterogeneous container using boxing
- The `.get(i)` method returns a boxed value (ptr) at the IL level
- Without an explicit type annotation, the variable is inferred as `ptr`
- When an explicit type like `String entry = ...` is provided:
  - The lowerer sees the target type (`String`)
  - It generates unboxing IL code to convert `ptr` → `str`
- This is similar to generics erasure in Java - runtime type info is lost
- Location: The type inference happens in `Sema::analyzeVarStmt()` and unboxing in `Lowerer`

**Location**: Semantic analysis and lowering for variable declarations with initializers.

**Fix**: Use explicit type annotation: `String entry = parts.get(i);`

**Note**: This is a design limitation of the current type system, not a bug per se. The explicit annotation guides the lowerer to generate correct unboxing code.

**Fixed**: [x]

---

## Compilation Log

### Server Compilation

```
Initial: Multiple errors with undefined identifiers and type mismatches
After Fix #1: Missing TCP functions added to Sema.cpp
After Fix #2: Changed Viper.Console to Viper.Terminal
After Fix #3: Changed .Length to Viper.String.Length()
After Fix #4: Changed == 0/1 to == false/true for booleans
After Fix #5: Restructured else-if chain to use result variable
After Fix #6: Changed 'or'/'and' to '||'/'&&'
After Fix #7: Simplified lsCommand to avoid Dir.List issue
Final: Compiles and runs successfully
```

### Client Compilation

```
Initial: Errors similar to server
After fixes: Compiles and runs successfully
```

---

## Feature Testing Log

| Feature | Status | Notes |
|---------|--------|-------|
| Server startup | Working | Listens on port 2323 |
| Client connection | Working | Connects and receives welcome |
| Command: help | Working | Shows available commands |
| Command: pwd | Working | Shows current directory |
| Command: cd | Not Tested | Should work |
| Command: ls | Partial | Simplified due to Bug #7 |
| Command: cat | Not Tested | May have same issue as ls |
| Command: echo | Working | Returns text |
| Command: version | Working | Shows v1.0.0 |
| Command: whoami | Working | Shows "guest" |
| Command: hostname | Working | Shows "viper-telnet-server" |
| Command: date | Working | Shows Unix timestamp |
| Command: exit | Working | Disconnects client |
| Disconnect handling | Working | Server continues after disconnect |

---

## API Notes

### Viper.Network.Tcp Methods (Now Available)
- `Connect(host, port)` - Connect to server
- `ConnectFor(host, port, timeout)` - Connect with timeout
- `SendStr(socket, text)` - Send string
- `RecvStr(socket, maxBytes)` - Receive string
- `RecvLine(socket)` - Receive line
- `SetRecvTimeout(socket, ms)` - Set receive timeout
- `SetSendTimeout(socket, ms)` - Set send timeout
- `Close(socket)` - Close connection
- `get_Host(socket)` - Get host
- `get_Port(socket)` - Get port
- `get_IsOpen(socket)` - Check if open
- `get_Available(socket)` - Get available bytes

### Viper.Network.TcpServer Methods (Now Available)
- `Listen(port)` - Start listening
- `ListenAt(addr, port)` - Listen on specific address
- `Accept(server)` - Accept connection
- `AcceptFor(server, timeout)` - Accept with timeout
- `Close(server)` - Stop server
- `get_Port(server)` - Get listening port
- `get_IsListening(server)` - Check if listening

### Viper.IO Methods
- `Dir.Current()` - Get current directory
- `Dir.Exists(path)` - Check directory exists (returns Boolean!)
- `Dir.List(path)` - List directory contents (now returns List[String]!)
- `File.Exists(path)` - Check file exists (returns Boolean!)
- `File.ReadAllText(path)` - Read file contents
- `Path.Join(dir, name)` - Join paths

---

## Root Cause Summary

| Bug | Category | Root Cause Location | Status |
|-----|----------|---------------------|--------|
| #1 | Registration | Sema.cpp - manual registration mismatch | ✅ Fixed |
| #2 | Registration | Sema.cpp - phantom/dead registrations | ✅ Fixed |
| #3 | Type System | Sema_Expr.cpp - missing String property support | ✅ Fixed |
| #4 | Type System | Sema_Expr.cpp - no comparison type validation | ⚠️ Workaround |
| #5 | Codegen | Lowerer_Decl.cpp - implicit return type mismatch | ✅ Fixed |
| #6 | Non-bug | Parser supports both forms | ✅ N/A |
| #7 | Registration | Sema.cpp - wrong return type (ptr vs List) | ✅ Fixed |
| #8 | Type System | Design limitation - boxed generics | ⚠️ Workaround |

### Key Architectural Issues (Mostly Resolved)

1. **Manual Registration Drift**: ✅ Fixed for TCP and Dir functions. Consider auto-generation for future.

2. **Missing Type Validation**: ⚠️ Use `== true/false` instead of `== 0/1` for boolean comparisons.

3. **~~Incomplete Property Support~~**: ✅ String.Length now works with property syntax.

4. **~~Opaque Pointers for Collections~~**: ✅ Dir.List now returns `List[String]`.

5. **~~Control Flow Edge Cases~~**: ✅ Implicit return now uses correct type-based default values.

---

## Test Suite

Bug fix tests are located in `demos/viperlang/bug_tests/`:
- `test_bug3_string_length.viper` - Tests String.Length property syntax
- `test_bug5_elseif_chain.viper` - Tests else-if chains with String return
- `test_bug7_list_simple.viper` - Tests List[String] type methods

Run all tests: `demos/viperlang/bug_tests/run_all_tests.sh`

---

## Version History

- v1.0.0 - Initial implementation with workarounds for known bugs
- v1.0.1 - Added root cause analysis for all bugs
- v1.1.0 - Fixed bugs #3, #5, #7 with proper compiler fixes and test suite

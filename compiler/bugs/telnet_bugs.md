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

**Fix**: Added missing function registrations to `/src/frontends/viperlang/Sema.cpp` lines 1160-1186:
- Added TCP Client functions: `ConnectFor`, `get_Host`, `get_Port`, `get_LocalPort`, `get_IsOpen`, `get_Available`, `SendStr`, `SendAll`, `RecvStr`, `RecvExact`, `RecvLine`, `SetRecvTimeout`, `SetSendTimeout`
- Added TCP Server functions: `Listen`, `ListenAt`, `get_Port`, `get_Address`, `get_IsListening`, `Accept`, `AcceptFor`, `Close`

**Fixed**: [x]

---

### Bug #2: Wrong API Names for Common Operations

**Status**: Fixed

**Description**: Several API names were incorrect:
- `Viper.Console.*` doesn't exist - should be `Viper.Terminal.*`
- `Viper.Time.Now()` doesn't exist - should be `Viper.DateTime.Now()`
- `Viper.Time.Clock.Now` was registered in Sema.cpp but doesn't exist in runtime

**Fix**: Updated code to use correct API names:
- `Viper.Console.PrintStr()` -> `Viper.Terminal.Print()`
- `Viper.Console.PrintI64()` -> `Viper.Terminal.PrintInt()`
- `Viper.Console.ReadLine()` -> `Viper.Terminal.ReadLine()`
- `Viper.Time.Now()` -> `Viper.DateTime.Now()`

**Fixed**: [x]

---

### Bug #3: Property Syntax Not Supported for Strings

**Status**: Workaround Applied

**Description**: ViperLang doesn't support property syntax like `str.Length` for String types. Must use function call syntax instead.

**Expected**: `cmd.Length` should return string length
**Actual**: Compilation error - arithmetic operations fail

**Workaround**: Use `Viper.String.Length(cmd)` instead of `cmd.Length`

**Fixed**: [ ] (Workaround only)

---

### Bug #4: Boolean Comparisons with Integer Literals

**Status**: Fixed

**Description**: Functions returning boolean values (like `Dir.Exists`, `File.Exists`, `Tcp.get_IsOpen`) cannot be compared with integer literals `0` or `1`. Must use `== true` or `== false`.

**Expected**: `if Viper.IO.Dir.Exists(path) == 0 { ... }` should work
**Actual**: Runtime type mismatch error

**Fix**: Changed all boolean comparisons to use `== true` or `== false` instead of `== 1` or `== 0`.

**Fixed**: [x]

---

### Bug #5: else-if Chain Code Generation Bug

**Status**: Workaround Applied

**Description**: Complex else-if chains generate incorrect IL code. The compiler generates a fallthrough block that returns `0` (integer) instead of continuing to the else clause.

**Steps to Reproduce**: Create a function returning String with many `else if` branches. At runtime, receive error: "ret value type mismatch: expected str but got i64"

**Workaround**: Restructure code to use separate `if` statements with a result variable instead of else-if chains. Return once at the end of the function.

**Fixed**: [ ] (Compiler bug - workaround only)

---

### Bug #6: Logical Operators `or` and `and` Not Recognized

**Status**: Fixed

**Description**: ViperLang uses `||` and `&&` for logical operators, not `or` and `and` as in some other languages.

**Expected**: `if a == "x" or a == "y"` should work
**Actual**: Compilation succeeds but generates incorrect code

**Fix**: Use `||` and `&&`:
- `if a == "x" or a == "y"` -> `if a == "x" || a == "y"`
- `if x > 0 and y == 0` -> `if x > 0 && y == 0`

**Fixed**: [x]

---

### Bug #7: Dir.List Returns Opaque Pointer

**Status**: Workaround Applied

**Description**: `Viper.IO.Dir.List()` returns a raw `ptr` instead of a typed `List[String]`. Calling `.count()` or `.get(i)` on it generates broken IL code.

**Steps to Reproduce**:
```viper
var entries = Viper.IO.Dir.List(path);
while i < entries.count() {  // This generates broken code
    var entry = entries.get(i);  // This also fails
}
```

**Workaround**: Temporarily simplified `lsCommand` to not use directory listing. Need to use static function calls:
```viper
var count = Viper.Collections.List.get_Count(entries);
var item = Viper.Collections.List.get_Item(entries, i);
String entry = Viper.Box.ToStr(item);
```
But this also causes runtime issues. Requires further investigation.

**Fixed**: [ ] (Workaround only - ls command simplified)

---

### Bug #8: List Element Type Inference

**Status**: Fixed

**Description**: When retrieving items from a List, must use explicit `String` type annotation or `Viper.Box.ToStr()` to properly convert the boxed value to a string.

**Expected**: `var entry = parts.get(i)` should infer String type from `List[String]`
**Actual**: Returns ptr/boxed value, string operations fail

**Fix**: Use explicit type annotation: `String entry = parts.get(i);`

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
- `Dir.List(path)` - List directory contents (returns raw ptr - buggy)
- `File.Exists(path)` - Check file exists (returns Boolean!)
- `File.ReadAllText(path)` - Read file contents
- `Path.Join(dir, name)` - Join paths

---

## Version History

- v1.0.0 - Initial implementation with workarounds for known bugs

# ViperLang Bugs Found During Centipede Development

This document tracks bugs discovered while porting Centipede to ViperLang.

---

## Bug #45: Entity Methods Cannot Call Other Methods Without `self.`

**Severity:** High
**Status:** FIXED

**Description:**
When a method inside an entity calls another method of the same entity, the compiler reports "Undefined identifier". The method name is not automatically resolved within the entity scope.

**Root Cause:**
In `Sema_Decl.cpp::analyzeEntityDecl()`, entity fields were added to the scope via `defineSymbol()`, but methods were only registered in the `methodTypes_` map, not in the local scope. When calling `helper()`, `lookupSymbol("helper")` failed because methods weren't in scope.

Additionally, `Lowerer_Expr.cpp::lowerCall()` didn't check for implicit method calls when the callee was just an identifier matching a method of the current entity.

**Fix:**
1. Modified `Sema_Decl.cpp::analyzeEntityDecl()` to pre-define method symbols in scope before analyzing method bodies
2. Modified `Lowerer_Expr.cpp::lowerCall()` to check for implicit method calls when inside an entity

**Verification:**
```viper
entity Foo {
    expose func helper() -> Integer { return 42; }
    expose func doWork() -> Integer { return helper(); }  // Now works!
}
```

---

## Bug #46: Missing `Viper.Math.RandInt()` Function

**Severity:** Medium
**Status:** NOT A BUG - API Exists

**Description:**
There is no direct integer random function under `Viper.Math.RandInt()`.

**Root Cause:**
This was a documentation/API discovery issue. The correct API is:
- `Viper.Random.NextInt(max)` - returns Integer in range [0, max)
- `Viper.Random.Next()` - returns Number in range [0.0, 1.0)
- `Viper.Random.Seed(seed)` - seeds the RNG with an Integer value

**Correct Usage:**
```viper
var x = Viper.Random.NextInt(10);  // Returns 0-9
Viper.Terminal.SayInt(x);
```

---

## Bug #47: Missing `Viper.Math.Randomize()` Function

**Severity:** Low
**Status:** NOT A BUG - API Exists

**Description:**
The `Viper.Math.Randomize()` function to seed the RNG was reported as missing.

**Root Cause:**
The function exists and is registered in both `Sema.cpp` and `RuntimeSignatures.cpp`. The API is:
- `Viper.Math.Randomize(seed)` - takes an Integer seed
- `Viper.Random.Seed(seed)` - alternative name, same function

**Correct Usage:**
```viper
Viper.Math.Randomize(12345);  // Seeds the RNG
var x = Viper.Random.NextInt(100);
```

---

## Bug #48: `String.substring()` May Not Exist

**Severity:** Medium
**Status:** NOT A BUG - API Exists

**Description:**
The `String.substring(start, length)` method was reported as possibly missing.

**Root Cause:**
The function exists as `Viper.String.Substring(str, start, length)`. It is registered in both `Sema.cpp` and `runtime.def`.

**Correct Usage:**
```viper
var s = "Hello World";
var sub = Viper.String.Substring(s, 0, 5);  // Returns "Hello"
```

---

## Bug #49: Entity Field Access in Expressions Can Fail

**Severity:** Medium
**Status:** NOT A BUG - Works Correctly

**Description:**
Complex expressions involving entity field access were reported to sometimes fail with "Invalid operands for arithmetic operation".

**Root Cause:**
Testing confirms this works correctly. The original issue may have been caused by type mismatches (mixing Integer and Number types) or other syntax errors.

**Verification:**
```viper
entity Game {
    expose Integer x;
    expose Integer y;
    expose Integer width;

    expose func test() {
        var idx = y * width + x;  // Works correctly
        Viper.Terminal.SayInt(idx);
    }
}
```

---

## Bug #50: `List.set()` Method May Not Exist

**Severity:** High
**Status:** NOT A BUG - API Exists

**Description:**
The `List[T].set(index, value)` method was reported as possibly missing.

**Root Cause:**
The method exists as `set_Item(index, value)`. It is registered in both `Sema.cpp` as `Viper.Collections.List.set_Item` and in `runtime.def`.

**Correct Usage:**
```viper
var items: List[Integer] = [1, 2, 3];
items.set_Item(1, 99);  // Sets index 1 to 99
```

---

## Bug #51: Implicit `self` for Field Access Works, But Not For Methods

**Severity:** Medium
**Status:** FIXED (by Bug #45 fix)

**Description:**
Within an entity method, accessing `fieldName` implicitly uses `self.fieldName` and works correctly. However, calling `methodName()` did NOT implicitly use `self.methodName()`.

**Root Cause:**
Same as Bug #45. Methods weren't added to the entity scope.

**Fix:**
The Bug #45 fix also resolved this inconsistency. Now both fields and methods work implicitly within entity methods.

---

## Bug #52: Terminal Input Function Names

**Severity:** Low
**Status:** Documentation Issue - Resolved

**Description:**
Terminal input functions have different names than expected:
- `Viper.Terminal.ReadLine()` - reads a line of text (blocking)
- `Viper.Terminal.GetKey()` - reads a single key (blocking)
- `Viper.Terminal.InKey()` - reads a key if available (non-blocking)

**Resolution:**
Use the correct function names as listed above.

---

## Bug #53: No Number-to-Integer Conversion

**Severity:** High
**Status:** FIXED

**Description:**
There was no way to convert a Number (float) to an Integer. Functions like `Viper.Math.Trunc()`, `Viper.Math.Floor()`, etc. all return Number, not Integer.

**Root Cause:**
The `Viper.Convert.NumToInt` function was missing from both the semantic analyzer and the runtime.

**Fix:**
Added `Viper.Convert.NumToInt(num)` which truncates a Number toward zero and returns an Integer:
1. Added `rt_f64_to_i64()` function to `rt_numeric_conv.c`
2. Added declaration to `rt_numeric.h`
3. Added to `runtime.def`
4. Registered in `Sema.cpp`

**Correct Usage:**
```viper
var n: Number = 3.7;
var i: Integer = Viper.Convert.NumToInt(n);  // Returns 3

var n2: Number = -2.9;
var i2: Integer = Viper.Convert.NumToInt(n2);  // Returns -2
```

---

## Bug #54: List Uses `.size()` Not `.count()`

**Severity:** Low
**Status:** Documentation Issue

**Description:**
List collections use `.size()` to get the element count, not `.count()` as some documentation suggests.

**Example:**
```viper
var items: List[Integer] = [1, 2, 3];
var len = items.size();  // Correct
// var len = items.count();  // Wrong
```

---

## Summary of Fixes Applied

| Bug | Status | Fix Location |
|-----|--------|--------------|
| #45 | FIXED | `Sema_Decl.cpp`, `Lowerer_Expr.cpp` |
| #46 | Not a bug | Use `Viper.Random.NextInt(max)` |
| #47 | Not a bug | `Viper.Math.Randomize(seed)` exists |
| #48 | Not a bug | `Viper.String.Substring(str, start, len)` exists |
| #49 | Not a bug | Works correctly |
| #50 | Not a bug | Use `list.set_Item(index, value)` |
| #51 | FIXED | Fixed by Bug #45 fix |
| #52 | Docs only | Use correct API names |
| #53 | FIXED | Added `Viper.Convert.NumToInt(num)` |
| #54 | Docs only | Use `.size()` not `.count()` |

## Notes for Future Development

1. The ViperLang frontend now supports implicit method calls within entities (same as fields)
2. Runtime library documentation should list all available Viper.* functions
3. Consider adding `Viper.Random.Range(min, max)` for convenience (currently registered in Sema but not implemented in runtime)

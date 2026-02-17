# Functional Types

> Lazy evaluation, optional values, and result types for safe and expressive programming.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Lazy](#viperlazy)
- [Viper.Option](#viperoption)
- [Viper.Result](#viperresult)

---

## Viper.Lazy

Lazy evaluation wrapper that defers computation until the value is first accessed. Supports wrapping objects, strings, and integers.

**Type:** Static utility class (all methods called on `Lazy`)

### Factory Methods

| Method        | Signature          | Description                                |
|---------------|--------------------|--------------------------------------------|
| `Of(value)`   | `Object(Object)`   | Wrap an object value for lazy evaluation   |
| `OfStr(value)` | `Object(String)`  | Wrap a string value for lazy evaluation    |
| `OfI64(value)` | `Object(Integer)` | Wrap an integer value for lazy evaluation  |

### Retrieval Methods

| Method        | Signature          | Description                                |
|---------------|--------------------|--------------------------------------------|
| `Get(lazy)`   | `Object(Object)`   | Retrieve the wrapped object value          |
| `GetStr(lazy)` | `String(Object)`  | Retrieve the wrapped string value          |
| `GetI64(lazy)` | `Integer(Object)` | Retrieve the wrapped integer value         |

### Properties

| Property      | Type                  | Description                                     |
|---------------|-----------------------|-------------------------------------------------|
| `IsEvaluated` | `Boolean` (read-only) | True if the lazy value has been evaluated        |

### Transformation Methods

| Method              | Signature          | Description                                                       |
|---------------------|--------------------|-------------------------------------------------------------------|
| `FlatMap(lazy, fn)` | `Object(Object, Object)` | Chain Lazy operations — fn receives the value and returns a new Lazy; the result is unwrapped |
| `Map(lazy, fn)`     | `Object(Object, Object)` | Create a new Lazy by applying fn to the value when accessed       |

### Methods

| Method        | Signature      | Description                                           |
|---------------|----------------|-------------------------------------------------------|
| `Force(lazy)` | `Void(Object)` | Force evaluation of the lazy value without retrieving |

### Notes

- All methods are called statically: `Lazy.OfI64(42)`, `Lazy.GetI64(l)`
- Once evaluated, subsequent `Get` calls return the cached value without re-computation
- `IsEvaluated` returns true after the first access or `Force` call
- Use typed factory/retrieval pairs (`OfI64`/`GetI64`, `OfStr`/`GetStr`) for primitive types
- Use `Of`/`Get` for heap-allocated object values

### Zia Example

```zia
module LazyDemo;

bind Viper.Terminal;
bind Viper.Lazy as Lazy;
bind Viper.Fmt as Fmt;

func start() {
    var l = Lazy.OfI64(42);
    Say("IsEvaluated: " + Fmt.Bool(l.get_IsEvaluated())); // true
    Say("Value: " + Fmt.Int(Lazy.GetI64(l)));              // 42
}
```

### BASIC Example

```basic
' Create a lazy integer value
DIM l AS OBJECT = Viper.Lazy.OfI64(42)

' Check if evaluated
PRINT "IsEvaluated: "; l.IsEvaluated   ' Output: 1

' Retrieve the value
DIM val AS INTEGER = Viper.Lazy.GetI64(l)
PRINT "Value: "; val                    ' Output: 42

' Create a lazy string value
DIM ls AS OBJECT = Viper.Lazy.OfStr("hello")
PRINT "String: "; Viper.Lazy.GetStr(ls) ' Output: hello

' Force evaluation without retrieving
DIM l2 AS OBJECT = Viper.Lazy.OfI64(99)
Viper.Lazy.Force(l2)
PRINT "Forced IsEvaluated: "; l2.IsEvaluated  ' Output: 1
```

---

## Viper.Option

Optional value type representing a value that may or may not be present. Use `Some` variants to wrap a value and `None` to represent absence. Provides safe access methods that avoid null-related errors.

**Type:** Static utility class (all methods called on `Option`)

### Factory Methods

| Method          | Signature          | Description                          |
|-----------------|--------------------|--------------------------------------|
| `Some(value)`   | `Object(Object)`   | Wrap an object in Some               |
| `SomeStr(value)` | `Object(String)`  | Wrap a string in Some                |
| `SomeI64(value)` | `Object(Integer)` | Wrap an integer in Some              |
| `SomeF64(value)` | `Object(Double)`  | Wrap a double in Some                |
| `None()`        | `Object()`         | Create an empty Option (no value)    |

### Properties

| Property | Type                  | Description                            |
|----------|-----------------------|----------------------------------------|
| `IsSome` | `Boolean` (read-only) | True if the option contains a value    |
| `IsNone` | `Boolean` (read-only) | True if the option is empty            |

### Unwrap Methods

| Method            | Signature          | Description                                            |
|-------------------|--------------------|--------------------------------------------------------|
| `Unwrap()`        | `Object()`         | Returns the contained object; traps if None            |
| `UnwrapStr()`     | `String()`         | Returns the contained string; traps if None            |
| `UnwrapI64()`     | `Integer()`        | Returns the contained integer; traps if None           |
| `UnwrapF64()`     | `Double()`         | Returns the contained double; traps if None            |

### Safe Unwrap Methods

| Method                   | Signature              | Description                                       |
|--------------------------|------------------------|---------------------------------------------------|
| `UnwrapOr(default)`      | `Object(Object)`       | Returns value if Some, otherwise returns default  |
| `UnwrapOrStr(default)`   | `String(String)`       | Returns string if Some, otherwise returns default |
| `UnwrapOrI64(default)`   | `Integer(Integer)`     | Returns integer if Some, otherwise returns default|
| `UnwrapOrF64(default)`   | `Double(Double)`       | Returns double if Some, otherwise returns default |

### Additional Methods

| Method          | Signature          | Description                                              |
|-----------------|--------------------|----------------------------------------------------------|
| `Value()`       | `Object()`         | Returns the contained value (alias for Unwrap)           |
| `Expect(msg)`   | `Object(String)`   | Returns value if Some; traps with msg if None            |
| `OkOr(err)`     | `Object(Object)`   | Converts to Result: Ok if Some, Err(err) if None        |
| `OkOrStr(err)`  | `Object(String)`   | Converts to Result: Ok if Some, Err(err) if None        |
| `ToString()`    | `String()`         | Returns "Some(<value>)" or "None"                        |

### Notes

- `Unwrap` and typed variants trap at runtime if called on a None option
- Use `UnwrapOr` variants for safe access with a fallback default
- `IsSome` and `IsNone` are complementary: exactly one is always true
- `Expect` provides a custom error message when trapping on None
- `OkOr` bridges from Option to Result for error-handling pipelines
- `ToString` produces readable output: `Some(42)` or `None`

### Zia Example

```zia
module OptionDemo;

bind Viper.Terminal;
bind Viper.Option as Option;
bind Viper.Fmt as Fmt;

func start() {
    var some = Option.SomeI64(42);
    Say("IsSome: " + Fmt.Bool(some.get_IsSome()));        // true
    Say("Value: " + Fmt.Int(some.UnwrapI64()));            // 42
    Say("ToString: " + some.ToString());                   // Some(42)

    var none = Option.None();
    Say("IsNone: " + Fmt.Bool(none.get_IsNone()));         // true
    Say("OrDefault: " + Fmt.Int(none.UnwrapOrI64(99)));    // 99
    Say("ToString: " + none.ToString());                   // None
}
```

### BASIC Example

```basic
' Create a Some option with an integer
DIM some AS OBJECT = Viper.Option.SomeI64(42)

PRINT "IsSome: "; some.IsSome       ' Output: 1
PRINT "IsNone: "; some.IsNone       ' Output: 0
PRINT "Value: "; some.UnwrapI64()   ' Output: 42
PRINT "ToString: "; some.ToString() ' Output: Some(42)

' Create a None option
DIM none AS OBJECT = Viper.Option.None()

PRINT "IsSome: "; none.IsSome       ' Output: 0
PRINT "IsNone: "; none.IsNone       ' Output: 1

' Safe access with default value
PRINT "Default: "; none.UnwrapOrI64(99)  ' Output: 99

' String options
DIM sopt AS OBJECT = Viper.Option.SomeStr("hello")
PRINT "String: "; sopt.UnwrapStr()       ' Output: hello

' Using Expect for descriptive errors
DIM val AS OBJECT = some.Expect("expected a value")
' Calling none.Expect("msg") would trap with "msg"

' Convert to Result
DIM result AS OBJECT = none.OkOrStr("value was missing")
PRINT "IsErr: "; result.IsErr            ' Output: 1
```

---

## Viper.Result

Result type for operations that can succeed or fail. Wraps either an `Ok` value (success) or an `Err` value (failure), enabling explicit error handling without exceptions.

**Type:** Static utility class (all methods called on `Result`)

### Ok Factory Methods

| Method          | Signature          | Description                        |
|-----------------|--------------------|------------------------------------|
| `Ok(value)`     | `Object(Object)`   | Create a successful result (object)|
| `OkStr(value)`  | `Object(String)`   | Create a successful result (string)|
| `OkI64(value)`  | `Object(Integer)`  | Create a successful result (integer)|
| `OkF64(value)`  | `Object(Double)`   | Create a successful result (double)|

### Err Factory Methods

| Method          | Signature          | Description                        |
|-----------------|--------------------|------------------------------------|
| `Err(value)`    | `Object(Object)`   | Create an error result (object)    |
| `ErrStr(value)` | `Object(String)`   | Create an error result (string)    |

### Properties

| Property | Type                  | Description                               |
|----------|-----------------------|-------------------------------------------|
| `IsOk`   | `Boolean` (read-only) | True if the result is a success           |
| `IsErr`  | `Boolean` (read-only) | True if the result is an error            |

### Unwrap Methods

| Method            | Signature          | Description                                            |
|-------------------|--------------------|--------------------------------------------------------|
| `Unwrap()`        | `Object()`         | Returns the Ok value; traps if Err                     |
| `UnwrapStr()`     | `String()`         | Returns the Ok string; traps if Err                    |
| `UnwrapI64()`     | `Integer()`        | Returns the Ok integer; traps if Err                   |
| `UnwrapF64()`     | `Double()`         | Returns the Ok double; traps if Err                    |

### Safe Unwrap Methods

| Method                   | Signature              | Description                                       |
|--------------------------|------------------------|---------------------------------------------------|
| `UnwrapOr(default)`      | `Object(Object)`       | Returns Ok value, or default if Err               |
| `UnwrapOrStr(default)`   | `String(String)`       | Returns Ok string, or default if Err              |
| `UnwrapOrI64(default)`   | `Integer(Integer)`     | Returns Ok integer, or default if Err             |
| `UnwrapOrF64(default)`   | `Double(Double)`       | Returns Ok double, or default if Err              |

### Error Access Methods

| Method            | Signature      | Description                                            |
|-------------------|----------------|--------------------------------------------------------|
| `UnwrapErr()`     | `Object()`     | Returns the Err value; traps if Ok                     |
| `UnwrapErrStr()`  | `String()`     | Returns the Err string; traps if Ok                    |

### Value Access Methods

| Method        | Signature    | Description                                                 |
|---------------|-------------|-------------------------------------------------------------|
| `OkValue()`   | `Object()`  | Returns the Ok value (alias for Unwrap)                     |
| `ErrValue()`  | `Object()`  | Returns the Err value (alias for UnwrapErr)                 |

### Additional Methods

| Method           | Signature        | Description                                              |
|------------------|------------------|----------------------------------------------------------|
| `Expect(msg)`    | `Object(String)` | Returns Ok value; traps with msg if Err                  |
| `ExpectErr(msg)` | `Object(String)` | Returns Err value; traps with msg if Ok                  |
| `ToString()`     | `String()`       | Returns "Ok(<value>)" or "Err(\"<message>\")"            |

### Notes

- `Unwrap` and typed variants trap at runtime if called on an Err result
- `UnwrapErr` and `UnwrapErrStr` trap if called on an Ok result
- Use `UnwrapOr` variants for safe access with a fallback default
- `IsOk` and `IsErr` are complementary: exactly one is always true
- `Expect` provides a custom error message when trapping on Err
- `ToString` produces readable output: `Ok(42)` or `Err("oops")`
- Result provides an alternative to exception-based error handling

### Zia Example

```zia
module ResultDemo;

bind Viper.Terminal;
bind Viper.Result as Result;
bind Viper.Fmt as Fmt;

func start() {
    // Success case
    var ok = Result.OkI64(42);
    Say("IsOk: " + Fmt.Bool(ok.get_IsOk()));          // true
    Say("Value: " + Fmt.Int(ok.UnwrapI64()));          // 42
    Say("ToString: " + ok.ToString());                 // Ok(42)

    // Error case
    var err = Result.ErrStr("oops");
    Say("IsErr: " + Fmt.Bool(err.get_IsErr()));        // true
    Say("Error: " + err.UnwrapErrStr());               // oops
    Say("ToString: " + err.ToString());                // Err("oops")

    // Safe unwrap with default
    Say("Default: " + Fmt.Int(err.UnwrapOrI64(0)));    // 0
}
```

### BASIC Example

```basic
' Create a successful result
DIM ok AS OBJECT = Viper.Result.OkI64(42)

PRINT "IsOk: "; ok.IsOk           ' Output: 1
PRINT "IsErr: "; ok.IsErr         ' Output: 0
PRINT "Value: "; ok.UnwrapI64()   ' Output: 42
PRINT "ToString: "; ok.ToString() ' Output: Ok(42)

' Create an error result
DIM err AS OBJECT = Viper.Result.ErrStr("oops")

PRINT "IsOk: "; err.IsOk          ' Output: 0
PRINT "IsErr: "; err.IsErr        ' Output: 1
PRINT "Error: "; err.UnwrapErrStr() ' Output: oops
PRINT "ToString: "; err.ToString() ' Output: Err("oops")

' Safe unwrap with default
PRINT "Default: "; err.UnwrapOrI64(0)    ' Output: 0

' Using Expect for descriptive errors
DIM val AS INTEGER = ok.Expect("should have a value")
' Calling err.Expect("msg") would trap with "msg"

' String results
DIM okStr AS OBJECT = Viper.Result.OkStr("success")
PRINT "Ok string: "; okStr.UnwrapStr()   ' Output: success

' Double results
DIM okF64 AS OBJECT = Viper.Result.OkF64(3.14)
PRINT "Ok double: "; okF64.UnwrapF64()   ' Output: 3.14

' Error value access
DIM errMsg AS STRING = err.UnwrapErrStr()
PRINT "Error message: "; errMsg           ' Output: oops
```

### Error Handling Patterns

```basic
' Function returning a Result
' (pseudocode - actual implementation depends on the API)
DIM result AS OBJECT = SomeOperationThatMayFail()

IF result.IsOk THEN
    DIM value AS INTEGER = result.UnwrapI64()
    PRINT "Success: "; value
ELSE
    DIM errMsg AS STRING = result.UnwrapErrStr()
    PRINT "Error: "; errMsg
END IF

' Chain with defaults
DIM safeValue AS INTEGER = result.UnwrapOrI64(-1)
```

### Result vs Option

| Feature            | Result                    | Option               |
|--------------------|---------------------------|----------------------|
| Success value      | Ok(value)                 | Some(value)          |
| Failure value      | Err(error) with message   | None (no message)    |
| Error information  | Yes (carries error data)  | No                   |
| Use case           | Operations that can fail  | Values that may exist|
| Convert to other   | --                        | `OkOr` to Result     |

---

## See Also

- [Core Types](core.md) - `Object`, `Box`, `String` -- foundational types
- [Collections](collections.md) - `Seq`, `Map` for structured data
- [Diagnostics](diagnostics.md) - `Assert`, `Trap` for assertion checking

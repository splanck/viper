# Viper Pascal — Language Specification

## Version 0.1 Draft 6

<div align="center">

**Status: Release Candidate**

*Pascal clarity meets Viper simplicity*

</div>

---

## Philosophy

Viper Pascal is Pascal distilled to its essence:

1. **Readable** — Code reads like structured prose
2. **Simple** — Small language, easy to master
3. **Safe** — Strong types, automatic memory management
4. **Practical** — Full access to Viper runtime

---

## Deliberate Omissions

The following legacy Pascal features are intentionally omitted:

| Omitted           | Rationale                          |
|-------------------|------------------------------------|
| `with` statement  | Obscures scope, widely discouraged |
| `goto` / `label`  | Use structured control flow        |
| `packed`          | No manual memory layout            |
| `set` types       | Use enums and conditionals         |
| Variant records   | Use class inheritance              |
| Nested procedures | Simplifies scoping                 |
| Pointer types     | Use classes for references         |
| `new` / `dispose` | Automatic memory management        |
| File types        | Use Viper.IO library (future)      |
| Generics          | Deferred to v0.2                   |
| User overloading  | Deferred to v0.2                   |

---

## Lexical Structure

### Case Sensitivity

Viper Pascal is **case-insensitive** for keywords and identifiers:

```pascal
BEGIN = begin = Begin
MyVar = myvar = MYVAR
```

By convention: keywords lowercase, types TitleCase, variables camelCase.

### Comments

```pascal
// Line comment (to end of line)

{ Block comment
  can span multiple lines }

(* Alternative block comment
   also spans multiple lines *)
```

Block comments do not nest.

### Identifiers

```
identifier = letter { letter | digit | "_" }
letter     = "A".."Z" | "a".."z"
digit      = "0".."9"
```

### Reserved Words

**Keywords:**

```
and        array      begin      break      case
class      const      constructor continue   destructor
div        do         downto     else       end
except     finally    for        function   if
implementation in     interface  mod        nil
not        of         on         or         private
procedure  program    public     raise      record
repeat     then       to         try        type
unit       until      uses       var        virtual
override   weak
```

**Predefined identifiers** (not keywords, but reserved):

```
Self       Result     True       False
Integer    Real       Boolean    String
Exception
```

These cannot be redeclared as variable, type, or procedure names.

The lexer recognizes `True`, `False`, and `nil` as distinct token kinds. `True` and `False` also appear as predefined
identifiers and cannot be redefined.

### Numeric Literals

```pascal
42              // Integer
3.14            // Real
1.0e-5          // Real with exponent
$FF             // Hexadecimal integer (255)
$DEADBEEF       // Hexadecimal integer
```

### String Literals

```pascal
'Hello'         // String
'It''s'         // Embedded quote (apostrophe doubled)
''              // Empty string
```

No escape sequences. Use `Chr(10)` for newline, `Chr(9)` for tab.

---

## Program Structure

### Minimal Program

```pascal
program HelloWorld;

begin
    WriteLn('Hello, Viper!');
end.
```

### Program with Declarations

```pascal
program CircleArea;

uses Viper.Math;

type
    TCircle = class
    public
        X, Y, Radius: Real;
        constructor Create(ax, ay, ar: Real);
        function Area: Real;
    end;

constructor TCircle.Create(ax, ay, ar: Real);
begin
    X := ax;
    Y := ay;
    Radius := ar;
end;

function TCircle.Area: Real;
begin
    Result := Pi * Pow(Radius, 2);
end;

var
    c: TCircle;
    
begin
    c := TCircle.Create(0, 0, 5);
    WriteLn('Area: ', c.Area);
end.
```

---

## Types

### Memory Model

| Category            | Semantics                               | Examples                                             |
|---------------------|-----------------------------------------|------------------------------------------------------|
| **Value types**     | Copied on assignment                    | Integer, Real, Boolean, records, fixed arrays, enums |
| **Reference types** | Shared on assignment, reference counted | String, classes, interfaces, dynamic arrays          |

```pascal
var
    r1, r2: TPoint;    // Record: r2 := r1 copies all fields
    c1, c2: TCircle;   // Class: c2 := c1 shares same object
```

### Primitive Types

| Type      | Description          | IL Type | Default |
|-----------|----------------------|---------|---------|
| `Integer` | 64-bit signed        | I64     | 0       |
| `Real`    | 64-bit IEEE 754      | F64     | 0.0     |
| `Boolean` | True/False           | I1      | False   |
| `String`  | Managed text (UTF-8) | Str     | ''      |

### Strings and Unicode

Strings are immutable sequences of bytes encoded as UTF-8.

| Function    | Returns                             |
|-------------|-------------------------------------|
| `Length(s)` | Number of bytes                     |
| `Chr(n)`    | Single-byte string from code 0..255 |
| `Asc(s)`    | First byte as integer 0..255        |

**Note:** All string operations are byte-based. For Unicode-aware operations (code points, grapheme clusters), a future
`Viper.Unicode` module will be provided.

### Enums

```pascal
type
    Color = (Red, Green, Blue);
    Day = (Mon, Tue, Wed, Thu, Fri, Sat, Sun);
    
var
    favorite: Color;
    today: Day;
    
begin
    favorite := Blue;
    today := Fri;
    
    if favorite = Blue then
        WriteLn('Blue selected');
        
    if today > Wed then
        WriteLn('Late in the week');
end.
```

**Rules:**

- Enum values are integers starting at 0
- Comparisons (`=`, `<>`, `<`, `>`, `<=`, `>=`) are allowed
- Arithmetic on enums is **not** allowed (no `Color + 1`)
- Explicit ordinals are not supported in v0.1
- Use in `case` statements is allowed

### Optional Types

Any type can be made optional with `?`:

```pascal
var
    username: String?;
    score: Integer?;
    
begin
    username := nil;           // No value
    username := 'Alice';       // Has value
    
    if username <> nil then
        WriteLn(username);     // OK after nil check
        
    WriteLn(username ?? 'Guest');  // Coalesce: use 'Guest' if nil
    
    // Chaining allowed
    WriteLn(first ?? second ?? 'default');
end.
```

**Rules:**

- `nil` represents "no value"
- `T` implicitly converts to `T?`
- `T?` does **not** implicitly convert to `T`
- `??` is short-circuiting and left-associative: `a ?? b ?? c` = `(a ?? b) ?? c`
- `T??` (double optional) is a compile error

**For reference types (classes, interfaces):**

- `TClass` is non-nullable by default
- `TClass?` explicitly allows nil
- Assigning nil to a non-optional class variable is a compile error
- Reading a non-optional class or interface variable that has not been definitely assigned is a compile-time error.
  Valid programs never observe a nil value of a non-optional class or interface type, even though implementations may
  initialize the underlying storage to nil for safety.

**Flow-sensitive narrowing:**

After a nil check, the compiler treats the variable as non-optional within the guarded scope:

```pascal
var name: String?;

if name <> nil then
begin
    // Inside this block, 'name' is treated as String (not String?)
    WriteLn(name);
    WriteLn(Length(name));
end;

// After the block, 'name' is still String?
```

Narrowing applies to:

- `if x <> nil then` — narrowed in then-branch
- `if x = nil then ... else` — narrowed in else-branch
- `while x <> nil do` — narrowed in loop body

Narrowing does **not** propagate across procedure calls or assignments.

**Implementation note:** For value types, `T?` is represented as a `(hasValue: Boolean, value: T)` pair. For reference
types, `nil` is the null pointer.

### Arrays

**Fixed arrays** (value type, copied on assignment):

```pascal
var
    scores: array[10] of Integer;     // 10 elements, indexed 0..9
    grid: array[3, 4] of Real;        // 3x4 matrix
    
begin
    scores[0] := 95;
    grid[1, 2] := 3.14;
end.
```

**Dynamic arrays** (reference type, shared on assignment):

```pascal
var
    data: array of Integer;
    
begin
    SetLength(data, 100);      // Allocate 100 elements
    data[0] := 42;
    WriteLn(Length(data));     // 100
end.
```

**Rules:**

- All array indices are 0-based
- Out-of-bounds access causes a runtime panic (not catchable)
- Dynamic arrays default to `nil` (length 0)
- `SetLength` on a `nil` array allocates a new array
- `SetLength` preserves existing elements up to `min(oldLen, newLen)`
- New elements from growing are initialized to the element type's default

### Records

Value types with fields (copied on assignment):

```pascal
type
    Point = record
        x: Real;
        y: Real;
    end;
    
var
    p1, p2: Point;
    
begin
    p1.x := 10;
    p1.y := 20;
    p2 := p1;       // Copies all fields
    p2.x := 99;     // p1.x still 10
end.
```

---

## Variables and Constants

```pascal
const
    MaxSize = 100;
    Pi = 3.14159;           // Compile-time constant
    AppName = 'My App';

var
    count: Integer;
    x, y: Real;
    ready: Boolean = False; // With initializer
```

**Default initialization:**

- **Value types** (Integer, Real, Boolean, enums, records, fixed arrays) are initialized to their type's default value (
  0, 0.0, False, all fields defaulted)
- **String** variables are initialized to the empty string `''`
- **Dynamic arrays** are initialized to `nil` (length 0)
- **Optional types** (`T?`) are initialized to `nil`
- **Non-optional class and interface variables** (`TClass`, `IFoo`) are **not** implicitly initialized. They must be
  definitely assigned before first read; using such a variable before the compiler can prove it has been assigned is a
  compile-time error.

---

## Operators

### Arithmetic

| Operator | Description      | Operands      | Result      |
|----------|------------------|---------------|-------------|
| `+`      | Addition         | Integer, Real | Wider type  |
| `-`      | Subtraction      | Integer, Real | Wider type  |
| `*`      | Multiplication   | Integer, Real | Wider type  |
| `/`      | Division         | Integer, Real | Always Real |
| `div`    | Integer division | Integer only  | Integer     |
| `mod`    | Remainder        | Integer only  | Integer     |

**Type promotion:** If either operand is Real, result is Real.

**Division:** `/` always returns Real, even for integer operands. Use `div` for integer division.

**Overflow:** Integer overflow wraps (two's complement). No runtime check.

**div/mod semantics:** Truncated toward zero. `a = (a div b) * b + (a mod b)` always holds. Examples:

- `7 div 3` = 2, `7 mod 3` = 1
- `-7 div 3` = -2, `-7 mod 3` = -1
- `7 div -3` = -2, `7 mod -3` = 1

### Comparison

| Operator          | Description        |
|-------------------|--------------------|
| `=`               | Equal              |
| `<>`              | Not equal          |
| `<` `>` `<=` `>=` | Ordered comparison |

Works on Integer, Real, String (lexicographic byte comparison), Boolean, enums.

### Logical

| Operator | Description                 |
|----------|-----------------------------|
| `and`    | Logical AND (short-circuit) |
| `or`     | Logical OR (short-circuit)  |
| `not`    | Logical NOT                 |

Short-circuit: `a and b` does not evaluate `b` if `a` is False. `a or b` does not evaluate `b` if `a` is True.

### String

| Operator | Description   |
|----------|---------------|
| `+`      | Concatenation |

### Null Coalescing

| Operator | Description                                                   |
|----------|---------------------------------------------------------------|
| `??`     | Return LHS if not nil, else RHS (left-associative, chainable) |

### Precedence (Highest to Lowest)

1. `not`, unary `-`
2. `*`, `/`, `div`, `mod`, `and`
3. `+`, `-`, `or`
4. `=`, `<>`, `<`, `>`, `<=`, `>=`
5. `??`

---

## Control Flow

### If-Then-Else

```pascal
if x > 0 then
    WriteLn('Positive')
else if x < 0 then
    WriteLn('Negative')
else
    WriteLn('Zero');

// Multiple statements need begin/end
if ready then
begin
    Initialize;
    Start;
end;
```

### Case

```pascal
case choice of
    1: DoFirst;
    2: DoSecond;
    3, 4, 5: DoOther;
else
    WriteLn('Invalid');
end;
```

Case expressions must be Integer or enum. Ranges (`1..5`) are not supported in v0.1.

### For Loop

```pascal
// Count up (inclusive)
for i := 1 to 10 do
    WriteLn(i);

// Count down (inclusive)  
for i := 10 downto 1 do
    WriteLn(i);

// Iterate collection
for item in items do
    Process(item);

// Iterate string bytes
for b in message do
    WriteLn(Asc(b));
```

**Semantics:**

- Bounds `a` and `b` in `for i := a to b` are evaluated once at loop entry
- If `a > b` in `to`, loop executes 0 times
- If `a < b` in `downto`, loop executes 0 times
- Loop variable is read-only; assigning to it is a **compile error**
- After loop exits, loop variable is **undefined** — do not use it
- Loop variable must be Integer or enum (not Real)
- `for-in` works on: fixed arrays, dynamic arrays, strings (iterates bytes as single-character strings)

### While and Repeat

```pascal
while count < 10 do
begin
    WriteLn(count);
    count := count + 1;
end;

repeat
    WriteLn('Enter password:');
    password := ReadLn;
until password = secret;
```

### Break and Continue

```pascal
for i := 0 to Length(data) - 1 do
begin
    if data[i] < 0 then
        continue;           // Skip to next iteration
    if data[i] = target then
        break;              // Exit loop
end;
```

Only valid inside loops. Using `break` or `continue` outside a loop is a compile error.

---

## Procedures and Functions

```pascal
procedure Greet(name: String);
begin
    WriteLn('Hello, ', name);
end;

procedure Swap(var a, b: Integer);
var
    temp: Integer;
begin
    temp := a;
    a := b;
    b := temp;
end;

function Square(x: Real): Real;
begin
    Result := x * x;
end;

function Max(a, b: Integer): Integer;
begin
    if a > b then
        Result := a
    else
        Result := b;
end;
```

### The Result Variable

Functions have an implicit `Result` variable of the return type:

- `Result` is initialized to the type's default value (0, 0.0, False, '', nil) at function entry
- Assigning to `Result` sets the return value
- If `Result` is never assigned, the function returns the default value (no compile error)
- Classic Pascal `FunctionName := value` syntax is **not** supported; use `Result` only
- `Result` is a reserved identifier; you cannot declare a variable named `Result`

### The Self Identifier

Inside methods (including constructors and destructors), `Self` refers to the current instance:

- `Self` is implicitly available; you don't declare it
- `Self` is a reserved identifier; you cannot declare a variable named `Self`
- `Self.field` is equivalent to just `field` when unambiguous
- Use `Self` to disambiguate when a parameter shadows a field name

```pascal
constructor TPoint.Create(x, y: Real);
begin
    Self.X := x;   // 'x' parameter shadows 'X' field
    Self.Y := y;
end;
```

### Parameter Passing

| Syntax           | Semantics                                 |
|------------------|-------------------------------------------|
| `x: Integer`     | By value (copy)                           |
| `var x: Integer` | By reference (caller's variable modified) |
| `x: Integer = 0` | Default value (optional parameter)        |

### Overloading

User-defined procedure/function overloading is **not** supported in v0.1.

Built-in functions (`Abs`, `Length`, `Write`, `WriteLn`) have compiler-provided overloads that accept multiple types.

**Statement restrictions:**

A standalone `designator` used as a statement (call_stmt) must denote a procedure or function call. Using a variable,
field, or array element as a standalone statement (e.g., `x;` or `arr[0];`) is a compile-time error.

---

## Classes

Reference types with automatic memory management.

### Declaration and Implementation

```pascal
type
    TPoint = class
    public
        X: Real;
        Y: Real;
        
        constructor Create(ax, ay: Real);
        function DistanceTo(other: TPoint): Real;
        procedure Move(dx, dy: Real);
    end;

constructor TPoint.Create(ax, ay: Real);
begin
    X := ax;
    Y := ay;
end;

function TPoint.DistanceTo(other: TPoint): Real;
var
    dx, dy: Real;
begin
    dx := X - other.X;
    dy := Y - other.Y;
    Result := Sqrt(dx * dx + dy * dy);
end;

procedure TPoint.Move(dx, dy: Real);
begin
    X := X + dx;
    Y := Y + dy;
end;
```

### Visibility Sections

Visibility is section-based (Delphi-style). Each section applies to all following members until the next section:

```pascal
type
    TAccount = class
    private
        balance: Real;          // private
        id: String;             // private
        
    public
        constructor Create(initial: Real);    // public
        procedure Deposit(amount: Real);      // public
        function GetBalance: Real;            // public
    end;
```

**Default visibility:** If no section is specified, members are `public`.

**Visibility rules:**

- `private` — accessible only within the same unit
- `public` — accessible from anywhere

### Constructors and Destructors

```pascal
type
    TResource = class
    public
        constructor Create;
        destructor Destroy;
    end;

constructor TResource.Create;
begin
    // Initialize
end;

destructor TResource.Destroy;
begin
    // Cleanup
end;
```

**Rules:**

- Constructors initialize object state and return the new instance
- Destructors are called automatically when reference count reaches zero
- Destructor must be named `Destroy` (convention)
- Destructors are implicitly virtual; `virtual` and `override` are not needed on destructors
- Use `inherited` in destructors to call parent cleanup

**Additional rules:**

- It is a compile-time error to specify a return type on a constructor or destructor implementation
- It is a compile-time error for a destructor to declare any parameters; the implementation must match the parameterless
  `destructor Destroy;` declaration in the class

### Inheritance

```pascal
type
    TShape = class
    public
        X, Y: Real;
        
        constructor Create(x, y: Real);
        function Area: Real; virtual;
    end;
    
    TCircle = class(TShape)
    public
        Radius: Real;
        
        constructor Create(x, y, r: Real);
        function Area: Real; override;
    end;

constructor TShape.Create(x, y: Real);
begin
    Self.X := x;
    Self.Y := y;
end;

function TShape.Area: Real;
begin
    Result := 0;
end;

constructor TCircle.Create(x, y, r: Real);
begin
    inherited Create(x, y);
    Radius := r;
end;

function TCircle.Area: Real;
begin
    Result := Pi * Pow(Radius, 2);
end;
```

**Rules:**

- Single inheritance only
- `virtual` marks a method as overridable
- `override` is required when overriding a virtual method
- `inherited` calls the parent's implementation
- `inherited` with no method name calls the same-named parent method

### Interfaces

```pascal
type
    IDrawable = interface
        procedure Draw;
    end;
    
    ISerializable = interface
        function ToJson: String;
    end;
    
    TButton = class(IDrawable, ISerializable)
    public
        Label: String;
        procedure Draw;
        function ToJson: String;
    end;

procedure TButton.Draw;
begin
    WriteLn('Drawing button: ', Label);
end;

function TButton.ToJson: String;
begin
    Result := '{"label":"' + Label + '"}';
end;
```

**Rules:**

- Interfaces contain only method signatures (no fields, no constructors)
- A class can implement multiple interfaces (comma-separated)
- A class can extend one base class AND implement multiple interfaces:
  ```pascal
  TFancyButton = class(TButton, IClickable, IAnimatable)
  ```
- Interface references are reference-counted
- The first identifier after `class(` that names a class is the base class; all others must be interfaces
- If more than one class type appears in the parent list, it is a compile-time error. At most one base class is
  permitted.

### Weak References

Break reference cycles:

```pascal
type
    TNode = class
    public
        Value: Integer;
        Next: TNode;            // Strong reference
        weak Prev: TNode;       // Weak reference
    end;
```

**Rules:**

- `weak` only applies to class and interface fields
- Weak references do not prevent deallocation
- When the target is deallocated, the weak reference automatically becomes `nil`
- Reading a weak reference always returns either a valid object or `nil`
- Cannot use `weak` on value types, strings, or arrays

---

## Error Handling

### Exception Base Class

`Exception` is a built-in class available without imports:

```pascal
type
    Exception = class
    public
        Message: String;
        constructor Create(msg: String);
    end;
```

*This declaration is illustrative only. `Exception` is a built-in type provided by the implementation and cannot be
redeclared or redefined by user code.*

### Try-Except

```pascal
try
    result := StrToInt(input);
    WriteLn('Parsed: ', result);
except
    on E: Exception do
        WriteLn('Error: ', E.Message);
end;
```

Multiple handlers (checked in order):

```pascal
try
    ProcessData;
except
    on E: EInvalidInput do
        WriteLn('Bad input: ', E.Message);
    on E: EOverflow do
        WriteLn('Overflow occurred');
    on E: Exception do
        WriteLn('Unknown error: ', E.Message);
end;
```

The first matching handler runs; subsequent handlers are skipped.

**Note:** The `else` clause in `except` is **not supported** in v0.1. Use `on E: Exception do` to catch all exceptions.

### Try-Finally

```pascal
resource := TResource.Create;
try
    resource.DoWork;
finally
    resource.Free;    // Always executed
end;
```

The `finally` block runs whether or not an exception was raised. If an exception was raised, it propagates after the
`finally` block completes.

### Raising Exceptions

```pascal
procedure Validate(age: Integer);
begin
    if age < 0 then
        raise Exception.Create('Age cannot be negative');
end;
```

**Re-raising:**

```pascal
try
    DoSomething;
except
    on E: Exception do
    begin
        LogError(E.Message);
        raise;              // Re-raise same exception
    end;
end;
```

**Rules:**

- `raise;` (without expression) is only valid inside an `except` block
- `raise;` outside an `except` block is a compile error
- `raise Expr;` creates and raises a new exception

### What Is and Isn't Catchable

| Condition                     | Behavior                          |
|-------------------------------|-----------------------------------|
| `raise Exception.Create(...)` | Catchable with `try/except`       |
| Array out-of-bounds           | Runtime panic (not catchable)     |
| Integer division by zero      | Runtime panic (not catchable)     |
| Integer overflow              | Wraps silently (no error)         |
| Real division by zero         | Returns `Inf` or `NaN` (no error) |
| Nil dereference               | Runtime panic (not catchable)     |

### Uncaught Exceptions

If an exception propagates out of the main program:

1. Error message is printed to stderr
2. Program exits with non-zero exit code

---

## Units

### Unit Structure

```pascal
unit MyMath;

interface

const
    Tau = 6.28318;

function Square(x: Real): Real;
function Cube(x: Real): Real;

implementation

function Square(x: Real): Real;
begin
    Result := x * x;
end;

function Cube(x: Real): Real;
begin
    Result := x * x * x;
end;

end.
```

### Interface vs Implementation

| Section          | Contains                                   | Visible to importers |
|------------------|--------------------------------------------|----------------------|
| `interface`      | const, type, procedure/function signatures | Yes                  |
| `implementation` | Full implementations, private helpers      | No                   |

**No exported variables:** `var` declarations are not allowed in the `interface` section. This prevents global mutable
state from being shared across units.

### Using Units

```pascal
program Demo;

uses 
    MyMath,
    Viper.Math;

begin
    WriteLn(Square(5));     // From MyMath
    WriteLn(Sqrt(25));      // From Viper.Math
end.
```

**Note:** There are two uses of `interface` in the language: the unit `interface` section (public API) and `interface`
type definitions (contracts for classes). Context distinguishes them.

### Classes in Units

Classes declared in a unit's `interface` are public. Method implementations go in the `implementation` section:

```pascal
unit Shapes;

interface

type
    TCircle = class
    public
        Radius: Real;
        constructor Create(r: Real);
        function Area: Real;
    end;

implementation

uses Viper.Math;

constructor TCircle.Create(r: Real);
begin
    Radius := r;
end;

function TCircle.Area: Real;
begin
    Result := Pi * Pow(Radius, 2);
end;

end.
```

---

## Built-in Functions

Available without imports:

| Function            | Description            | Returns                   |
|---------------------|------------------------|---------------------------|
| `Write(...)`        | Print without newline  | (none)                    |
| `WriteLn(...)`      | Print with newline     | (none)                    |
| `ReadLn`            | Read line from input   | String                    |
| `ReadInteger`       | Read and parse integer | Integer (raises on error) |
| `ReadReal`          | Read and parse real    | Real (raises on error)    |
| `Length(arr)`       | Array length           | Integer                   |
| `Length(str)`       | String byte length     | Integer                   |
| `SetLength(arr, n)` | Resize dynamic array   | (none)                    |
| `IntToStr(i)`       | Integer to string      | String                    |
| `RealToStr(r)`      | Real to string         | String                    |
| `StrToInt(s)`       | String to integer      | Integer (raises on error) |
| `StrToReal(s)`      | String to real         | Real (raises on error)    |

**Write and WriteLn:**

- Accept any number of arguments
- Each argument must be Integer, Real, Boolean, or String
- Example: `WriteLn('Value: ', x, ', Ready: ', ready);`

**ReadLn:**

- Returns one line of input without the trailing newline
- Can be used as a statement (discards result): `ReadLn;`

---

## Viper.Strings

```pascal
uses Viper.Strings;

var s: String;
begin
    s := 'Hello, World!';
    
    WriteLn(Upper(s));         // 'HELLO, WORLD!'
    WriteLn(Lower(s));         // 'hello, world!'
    WriteLn(Left(s, 5));       // 'Hello'
    WriteLn(Right(s, 6));      // 'World!'
    WriteLn(Mid(s, 0, 5));     // 'Hello' (0-based start)
    WriteLn(Chr(65));          // 'A'
    WriteLn(Asc('A'));         // 65
end.
```

| Function             | Description                        | Maps to    |
|----------------------|------------------------------------|------------|
| `Upper(s)`           | Uppercase (ASCII only)             | `rt_ucase` |
| `Lower(s)`           | Lowercase (ASCII only)             | `rt_lcase` |
| `Left(s, n)`         | First n bytes                      | `rt_left`  |
| `Right(s, n)`        | Last n bytes                       | `rt_right` |
| `Mid(s, start, len)` | Substring (0-based start)          | `rt_mid3`  |
| `Chr(n)`             | Byte (0-255) to single-char string | `rt_chr`   |
| `Asc(s)`             | First byte as integer (0-255)      | `rt_asc`   |

**Note:** All operations are byte-based and ASCII-only. They do not respect multi-byte UTF-8 sequences. Use a future
`Viper.Unicode` module for proper Unicode handling.

---

## Viper.Math

```pascal
uses Viper.Math;

begin
    WriteLn(Sqrt(16.0));       // 4.0
    WriteLn(Abs(-5));          // 5
    WriteLn(Abs(-3.5));        // 3.5
    WriteLn(Floor(3.7));       // 3.0
    WriteLn(Ceil(3.2));        // 4.0
    WriteLn(Sin(Pi / 2));      // 1.0
    WriteLn(Cos(0.0));         // 1.0
    WriteLn(Tan(Pi / 4));      // ~1.0
    WriteLn(Atan(1.0));        // ~0.785
    WriteLn(Exp(1.0));         // ~2.718
    WriteLn(Ln(E));            // 1.0
    WriteLn(Pow(2.0, 10.0));   // 1024.0
end.
```

| Function         | Description                      | Maps to                     |
|------------------|----------------------------------|-----------------------------|
| `Sqrt(x)`        | Square root                      | `rt_sqrt`                   |
| `Abs(x)`         | Absolute value (Integer or Real) | `rt_abs_i64` / `rt_abs_f64` |
| `Floor(x)`       | Round toward -∞                  | `rt_floor`                  |
| `Ceil(x)`        | Round toward +∞                  | `rt_ceil`                   |
| `Sin(x)`         | Sine (radians)                   | `rt_sin`                    |
| `Cos(x)`         | Cosine (radians)                 | `rt_cos`                    |
| `Tan(x)`         | Tangent (radians)                | `rt_tan`                    |
| `Atan(x)`        | Arctangent                       | `rt_atan`                   |
| `Exp(x)`         | e^x                              | `rt_exp`                    |
| `Ln(x)`          | Natural logarithm                | `rt_log`                    |
| `Pow(base, exp)` | Power                            | `rt_pow`                    |

**Constants:**

| Constant | Value            |
|----------|------------------|
| `Pi`     | 3.14159265358979 |
| `E`      | 2.71828182845904 |

---

## Implementation Notes

These notes are non-normative guidance for frontend implementers.

### IL Mapping

| Pascal                      | IL Opcode / Runtime                                |
|-----------------------------|----------------------------------------------------|
| `+`, `-`, `*` on Integer    | `Add`, `Sub`, `Mul`                                |
| `+`, `-`, `*` on Real       | `Fadd`, `Fsub`, `Fmul`                             |
| `div`                       | `Sdiv`                                             |
| `mod`                       | `Srem`                                             |
| `/` (always Real result)    | `Fdiv` (after `Sitofp` if needed)                  |
| Integer overflow            | Wraps (use plain ops, not `IaddOvf` etc.)          |
| Integer division by zero    | Use `SdivChk0` or emit explicit check + `Trap`     |
| Array bounds                | Emit check + `rt_arr_oob_panic` or rely on runtime |
| `try/except`, `try/finally` | `EhPush`, `EhPop`, `ResumeSame`, `ResumeNext`      |

### Variable Initialization

All local variables must be initialized to their default values at scope entry. The lowerer should emit stores of 0,
0.0, False, '', or nil immediately after `Alloca` for each local slot.

### Optional Types for Value Types

`T?` where T is a value type (Integer, Real, Boolean, enum, record) should be represented as a pair:
`(hasValue: I1, value: T)`. This allows distinguishing between "no value" and "value is zero/false."

For reference types (String, class, interface, dynamic array), use the null pointer as `nil`.

### Dynamic Array Fields in Classes

Dynamic array fields in classes are stored as pointer-sized handles. Follow the BASIC frontend pattern:

- Constructor calls `rt_arr_*_new` to allocate
- Field access loads the handle, then calls `rt_arr_*_get`/`rt_arr_*_set`
- Use `rt_arr_*_len` for bounds checking

---

## Complete Grammar

```ebnf
(* === Program Structure === *)
program     = "program" ident ";" [uses] {decl} {method_impl} block "." .
unit        = "unit" ident ";" interface_part impl_part "end" "." .
uses        = "uses" ident_list ";" .

interface_part = "interface" {const_decl | type_decl | proc_sig | func_sig} .
impl_part      = "implementation" [uses] {decl} {method_impl} .

(* === Declarations === *)
decl        = const_decl | type_decl | var_decl | proc_decl | func_decl .
const_decl  = "const" {ident "=" expr ";"} .
type_decl   = "type" {ident "=" type_def ";"} .
var_decl    = "var" {ident_list ":" type ["=" expr] ";"} .

(* === Types === *)
type_def    = type | enum_def | record_def | class_def | interface_def .
type        = base_type ["?"] .
base_type   = ident | array_type .
enum_def    = "(" ident_list ")" .
array_type  = "array" ["[" expr {"," expr} "]"] "of" type .
record_def  = "record" {field_list} "end" .
class_def   = "class" ["(" ident_list ")"] {class_section} "end" .
interface_def = "interface" {proc_sig | func_sig} "end" .

(* === Class Members === *)
class_section = [visibility] {member} .
visibility  = "private" | "public" .
member      = field_decl | proc_sig | func_sig | ctor_sig | dtor_sig .
field_decl  = ["weak"] ident_list ":" type ";" .

(* === Procedures and Functions === *)
proc_sig    = "procedure" ident ["(" params ")"] ";" [directive] .
func_sig    = "function" ident ["(" params ")"] ":" type ";" [directive] .
ctor_sig    = "constructor" ident ["(" params ")"] ";" .
dtor_sig    = "destructor" ident ";" .
directive   = "virtual" | "override" .

proc_decl   = proc_sig block ";" .
func_decl   = func_sig block ";" .

method_impl = method_kind ident "." ident ["(" params ")"] [":" type] ";" block ";" .
method_kind = "procedure" | "function" | "constructor" | "destructor" .

params      = [param {";" param}] .
param       = ["var"] ident_list ":" type ["=" expr] .
field_list  = ident_list ":" type ";" .

(* === Statements === *)
block       = {var_decl} "begin" stmt_list "end" .
stmt_list   = [stmt {";" stmt}] [";"] .
stmt        = [simple_stmt | structured_stmt] .
simple_stmt = assign | call_stmt | "break" | "continue" | raise_stmt .
structured_stmt = if_stmt | case_stmt | for_stmt | while_stmt 
                | repeat_stmt | try_stmt | compound_stmt .

assign      = designator ":=" expr .
call_stmt   = designator .
compound_stmt = "begin" stmt_list "end" .

if_stmt     = "if" expr "then" stmt ["else" stmt] .
case_stmt   = "case" expr "of" {case_item} ["else" stmt_list] "end" .
case_item   = case_labels ":" stmt ";" .
case_labels = expr {"," expr} .

for_stmt    = "for" ident ":=" expr ("to" | "downto") expr "do" stmt
            | "for" ident "in" expr "do" stmt .
while_stmt  = "while" expr "do" stmt .
repeat_stmt = "repeat" stmt_list "until" expr .

try_stmt    = "try" stmt_list (except_part | finally_part) "end" .
except_part = "except" {except_handler} .
except_handler = "on" [ident ":"] ident "do" stmt ";" .
finally_part = "finally" stmt_list .

raise_stmt  = "raise" [expr] .

(* === Expressions === *)
expr        = coalesce .
coalesce    = relation {"??" relation} .
relation    = simple [relop simple] .
simple      = ["-"] term {addop term} .
term        = factor {mulop factor} .
factor      = literal | designator | "(" expr ")" | "not" factor .
literal     = number | string | "True" | "False" | "nil" .

designator  = ident {desg_part} .
desg_part   = "." ident | "[" expr {"," expr} "]" | "(" [args] ")" .
args        = expr {"," expr} .

relop       = "=" | "<>" | "<" | ">" | "<=" | ">=" .
addop       = "+" | "-" | "or" .
mulop       = "*" | "/" | "div" | "mod" | "and" .

(* === Lexical === *)
ident       = letter {letter | digit | "_"} .
ident_list  = ident {"," ident} .
number      = integer | real .
integer     = digits | "$" hex_digits .
real        = digits "." digits [exponent] .
exponent    = ("e" | "E") ["+" | "-"] digits .
string      = "'" {char | "''"} "'" .
digits      = digit {digit} .
hex_digits  = hex_digit {hex_digit} .
digit       = "0".."9" .
hex_digit   = digit | "A".."F" | "a".."f" .
letter      = "A".."Z" | "a".."z" .
char        = (* any character except "'" *) .

(* === Comments (lexer handles, not in grammar) === *)
(* // line comment *)
(* { block comment } *)
(* (* block comment *) *)
```

---

## Summary

| Category          | Contents                                                                                                   |
|-------------------|------------------------------------------------------------------------------------------------------------|
| **Types**         | Integer, Real, Boolean, String, enums, records, classes, interfaces, arrays, optionals                     |
| **Built-in**      | Write, WriteLn, ReadLn, ReadInteger, ReadReal, Length, SetLength, IntToStr, RealToStr, StrToInt, StrToReal |
| **Viper.Strings** | Upper, Lower, Left, Right, Mid, Chr, Asc                                                                   |
| **Viper.Math**    | Sqrt, Abs, Floor, Ceil, Sin, Cos, Tan, Atan, Exp, Ln, Pow, Pi, E                                           |

---

**Status:** Draft 6 — Release Candidate

**© 2024 Viper Project**

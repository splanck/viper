---
status: active
audience: public
last-updated: 2025-12-07
---

# Viper Pascal — Reference

Complete language reference for Viper Pascal. This document describes **program structure**, **statements**, *
*expressions & operators**, **built-in functions**, **units**, and **exception handling**. For a tutorial introduction,
see **[Pascal Tutorial](pascal-language.md)**.

---

## Key Language Features

- **Strong typing**: All variables must be declared with types
- **Structured programming**: Procedures, functions, units
- **Modern exceptions**: Try/except/finally, raise
- **Units**: Modular code with interface/implementation separation
- **Clean syntax**: Case-insensitive, semicolon-separated statements

---

## Table of Contents

- [Program Structure](#program-structure)
- [Lexical Elements](#lexical-elements)
- [Types](#types)
- [Object-Oriented Programming](#object-oriented-programming)
- [Declarations](#declarations)
- [Statements A-Z](#statements-a-z)
- [Expressions & Operators](#expressions--operators)
- [Built-in Functions](#built-in-functions)
- [Units](#units)
- [Exception Handling](#exception-handling)
- [Keyword Index](#keyword-index)

---

## Program Structure

### Program

A Pascal program has this structure:

```pascal
program ProgramName;

uses Unit1, Unit2;    { optional }

const
  { constant declarations }

type
  { type declarations }

var
  { variable declarations }

{ procedure and function declarations }

begin
  { main program statements }
end.
```

### Minimal Program

```pascal
program Minimal;
begin
end.
```

---

## Lexical Elements

### Identifiers

- Start with a letter or underscore
- Contain letters, digits, underscores
- Case-insensitive (`MyVar` = `myvar` = `MYVAR`)

### Comments

```pascal
{ Block comment }
(* Alternative block comment *)
// Single-line comment (if supported)
```

### Literals

| Type    | Examples                             |
|---------|--------------------------------------|
| Integer | `42`, `-17`, `0`                     |
| Real    | `3.14`, `-0.5`, `1.0e10`             |
| String  | `'Hello'`, `'It''s'` (escaped quote) |
| Char    | `'A'`, `'x'`                         |
| Boolean | `True`, `False`                      |

### Separators

- `;` — Statement separator
- `,` — List separator
- `.` — Program terminator, record field access
- `:` — Type annotation
- `:=` — Assignment

---

## Types

### Basic Types

| Type      | Description      | Range/Size      |
|-----------|------------------|-----------------|
| `Integer` | Signed integer   | 64-bit          |
| `Real`    | Floating point   | 64-bit IEEE 754 |
| `Boolean` | Logical value    | `True`, `False` |
| `Char`    | Single character | 8-bit           |
| `String`  | Text string      | Dynamic length  |

### Type Aliases

```pascal
type
  TCount = Integer;
  TName = String;
```

### Array Types

```pascal
type
  TNumbers = array[1..10] of Integer;
  TMatrix = array[1..3, 1..3] of Real;

var
  nums: TNumbers;
  grid: TMatrix;
```

### Record Types

```pascal
type
  TPoint = record
    x, y: Integer;
  end;

  TPerson = record
    name: String;
    age: Integer;
    active: Boolean;
  end;
```

### Pointer Types

```pascal
type
  PInteger = ^Integer;

var
  p: PInteger;
  n: Integer;
begin
  n := 42;
  p := @n;
  WriteLn(IntToStr(p^))  { 42 }
end.
```

---

## Object-Oriented Programming

Viper Pascal supports object-oriented programming with classes, interfaces, inheritance, and polymorphism.

### Class Declaration

```pascal
type
  TPoint = class
  private
    FX: Integer;
    FY: Integer;
  public
    constructor Create(x: Integer; y: Integer);
    function GetX: Integer;
    procedure SetX(value: Integer);
  end;

constructor TPoint.Create(x: Integer; y: Integer);
begin
  FX := x;
  FY := y
end;

function TPoint.GetX: Integer;
begin
  Result := FX
end;

procedure TPoint.SetX(value: Integer);
begin
  FX := value
end;
```

### Visibility Sections

| Section   | Access                           |
|-----------|----------------------------------|
| `public`  | Accessible from anywhere         |
| `private` | Only accessible within the class |

```pascal
type
  TCounter = class
  private
    FValue: Integer;    { Only accessible within TCounter }
  public
    constructor Create;
    procedure Increment; { Accessible from anywhere }
    function GetValue: Integer;
  end;
```

### Constructors and Destructors

```pascal
type
  TResource = class
  public
    constructor Create;
    destructor Destroy; virtual;
  end;

constructor TResource.Create;
begin
  { Initialize resources }
end;

destructor TResource.Destroy;
begin
  { Clean up resources }
  inherited Destroy  { Call base destructor }
end;
```

**Notes:**

- Constructors are named `Create` by convention but can have any name
- Destructors must be named `Destroy`
- Use `inherited` to call base class destructor

### Inheritance

Single inheritance is supported via the `class(BaseClass)` syntax:

```pascal
type
  TShape = class
  public
    X, Y: Integer;
    constructor Create(x: Integer; y: Integer);
  end;

  TRectangle = class(TShape)
  public
    Width, Height: Integer;
    constructor Create(x: Integer; y: Integer; w: Integer; h: Integer);
  end;

constructor TRectangle.Create(x: Integer; y: Integer; w: Integer; h: Integer);
begin
  inherited Create(x, y);  { Call base constructor }
  Width := w;
  Height := h
end;
```

### Virtual Methods and Override

```pascal
type
  TAnimal = class
  public
    procedure Speak; virtual;  { Can be overridden }
  end;

  TDog = class(TAnimal)
  public
    procedure Speak; override;  { Overrides base implementation }
  end;

procedure TAnimal.Speak;
begin
  WriteLn('...')
end;

procedure TDog.Speak;
begin
  WriteLn('Woof!')
end;
```

### Abstract Methods

Abstract methods have no implementation and must be overridden:

```pascal
type
  TShape = class
  public
    procedure Draw; virtual; abstract;  { Must be implemented by subclasses }
  end;

  TCircle = class(TShape)
  public
    procedure Draw; override;
  end;

procedure TCircle.Draw;
begin
  WriteLn('Drawing circle')
end;
```

**Note:** Classes with abstract methods cannot be instantiated directly.

### Interfaces

Interfaces define contracts that classes can implement:

```pascal
type
  IDrawable = interface
    procedure Draw;
  end;

  IResizable = interface
    procedure Resize(w: Integer; h: Integer);
  end;

  TWidget = class(IDrawable, IResizable)
  public
    procedure Draw;
    procedure Resize(w: Integer; h: Integer);
  end;
```

### Class with Base Class and Interfaces

```pascal
type
  TButton = class(TControl, IClickable, IDrawable)
  public
    procedure Draw;
    procedure OnClick;
  end;
```

### Properties

Properties provide controlled access to fields:

```pascal
type
  TCounter = class
  private
    FValue: Integer;
    function GetValue: Integer;
    procedure SetValue(v: Integer);
  public
    property Value: Integer read FValue write FValue;        { Direct field access }
    property Count: Integer read GetValue write SetValue;    { Via methods }
    property ReadOnly: Integer read FValue;                  { Read-only property }
  end;
```

### Self Identifier

Inside methods, `Self` refers to the current instance:

```pascal
procedure TPoint.MoveBy(dx: Integer; dy: Integer);
begin
  Self.X := Self.X + dx;
  Self.Y := Self.Y + dy
end;
```

### Result Variable

In functions, assign to `Result` to set the return value:

```pascal
function TCalc.Add(a: Integer; b: Integer): Integer;
begin
  Result := a + b
end;
```

### Inherited Statement

Call base class implementation:

```pascal
procedure TChild.DoWork;
begin
  inherited;           { Call base DoWork with same arguments }
  inherited DoWork;    { Explicit form }
  { Additional work }
end;
```

### RTTI: IS and AS Operators

**IS operator** — Runtime type check:

```pascal
var
  shape: TShape;
begin
  shape := TCircle.Create;
  if shape is TCircle then
    WriteLn('It is a circle');
  if shape is IDrawable then
    WriteLn('It is drawable')
end.
```

**AS operator** — Safe downcast:

```pascal
var
  shape: TShape;
  circle: TCircle;
begin
  shape := TCircle.Create;
  circle := shape as TCircle;  { Safe cast }
  circle.Radius := 10
end.
```

### Weak References

Prevent circular reference memory leaks:

```pascal
type
  TNode = class
  public
    Next: TNode;           { Strong reference }
    weak Prev: TNode;      { Weak reference - doesn't prevent deallocation }
  end;
```

**Note:** `weak` can only be applied to class or interface types, not value types.

### Object Creation

Create objects with the `Create` constructor:

```pascal
var
  p: TPoint;
begin
  p := TPoint.Create(10, 20);
  { Use p }
end.
```

### With Statement and Objects

The `with` statement works with objects:

```pascal
with myPoint do
begin
  X := 10;
  Y := 20
end;
```

---

## Declarations

### Constant Declarations

```pascal
const
  MaxSize = 100;
  Pi = 3.14159;
  AppName = 'MyApp';
  Debug = True;
```

### Type Declarations

```pascal
type
  TStatus = Integer;
  TPoint = record
    x, y: Real;
  end;
  TScores = array[1..5] of Integer;
```

### Variable Declarations

```pascal
var
  count: Integer;
  name, title: String;
  values: array[1..10] of Real;
  point: TPoint;
```

### Procedure Declarations

```pascal
procedure PrintLine;
begin
  WriteLn('---')
end;

procedure Greet(name: String);
begin
  WriteLn('Hello, ' + name)
end;

procedure Swap(var a, b: Integer);
var temp: Integer;
begin
  temp := a;
  a := b;
  b := temp
end;
```

### Function Declarations

```pascal
function Square(x: Integer): Integer;
begin
  Square := x * x
end;

function Max(a, b: Integer): Integer;
begin
  if a > b then
    Max := a
  else
    Max := b
end;
```

### Parameter Passing

| Mode      | Syntax           | Semantics                       |
|-----------|------------------|---------------------------------|
| Value     | `x: Integer`     | Copy passed, original unchanged |
| Reference | `var x: Integer` | Original variable modified      |

```pascal
procedure Increment(var n: Integer);
begin
  n := n + 1
end;
```

---

## Statements A-Z

### Assignment

```pascal
x := 10;
name := 'Alice';
point.x := 5.0;
arr[i] := value;
```

### Begin ... End

Groups multiple statements into a compound statement:

```pascal
begin
  statement1;
  statement2;
  statement3
end
```

### Case

Multi-way branch based on an ordinal value:

```pascal
case expression of
  value1: statement1;
  value2, value3: statement2;
  value4..value5: statement3;
else
  defaultStatement
end
```

Example:

```pascal
case grade of
  'A': WriteLn('Excellent');
  'B', 'C': WriteLn('Good');
  'D'..'F': WriteLn('Needs work')
else
  WriteLn('Unknown')
end
```

### For

Counted loop:

```pascal
for variable := startValue to endValue do
  statement;

for variable := startValue downto endValue do
  statement;
```

Example:

```pascal
for i := 1 to 10 do
  WriteLn(IntToStr(i));

for i := 10 downto 1 do
  WriteLn(IntToStr(i));
```

### If ... Then ... Else

Conditional execution:

```pascal
if condition then
  statement;

if condition then
  statement1
else
  statement2;

if condition1 then
  statement1
else if condition2 then
  statement2
else
  statement3;
```

### Repeat ... Until

Post-test loop (executes at least once):

```pascal
repeat
  statement1;
  statement2
until condition;
```

### While ... Do

Pre-test loop:

```pascal
while condition do
  statement;

while condition do
begin
  statement1;
  statement2
end;
```

### With

Simplifies access to record fields:

```pascal
with recordVariable do
begin
  field1 := value1;
  field2 := value2
end;
```

Example:

```pascal
with person do
begin
  name := 'Alice';
  age := 30
end;
```

### Procedure Call

```pascal
WriteLn('Hello');
Greet('World');
Process(a, b, c);
```

---

## Expressions & Operators

### Arithmetic Operators

| Operator | Description      | Example   |
|----------|------------------|-----------|
| `+`      | Addition         | `a + b`   |
| `-`      | Subtraction      | `a - b`   |
| `*`      | Multiplication   | `a * b`   |
| `/`      | Real division    | `a / b`   |
| `div`    | Integer division | `a div b` |
| `mod`    | Modulus          | `a mod b` |
| `-`      | Unary minus      | `-x`      |

### Comparison Operators

| Operator | Description      | Example  |
|----------|------------------|----------|
| `=`      | Equal            | `a = b`  |
| `<>`     | Not equal        | `a <> b` |
| `<`      | Less than        | `a < b`  |
| `<=`     | Less or equal    | `a <= b` |
| `>`      | Greater than     | `a > b`  |
| `>=`     | Greater or equal | `a >= b` |

### Boolean Operators

| Operator | Description | Example   |
|----------|-------------|-----------|
| `and`    | Logical AND | `a and b` |
| `or`     | Logical OR  | `a or b`  |
| `not`    | Logical NOT | `not a`   |
| `xor`    | Logical XOR | `a xor b` |

### String Operators

| Operator | Description   | Example              |
|----------|---------------|----------------------|
| `+`      | Concatenation | `'Hello' + ' World'` |

### Operator Precedence (High to Low)

1. `not`, unary `-`
2. `*`, `/`, `div`, `mod`, `and`
3. `+`, `-`, `or`, `xor`
4. `=`, `<>`, `<`, `<=`, `>`, `>=`

---

## Built-in Functions

### I/O Procedures

| Name           | Description                  |
|----------------|------------------------------|
| `Write(...)`   | Write values without newline |
| `WriteLn(...)` | Write values with newline    |
| `Read(var)`    | Read value from input        |
| `ReadLn(var)`  | Read line from input         |

### String Functions

| Name        | Arguments                          | Returns   | Description                    |
|-------------|------------------------------------|-----------|--------------------------------|
| `Length`    | `(s: String)`                      | `Integer` | String length                  |
| `Copy`      | `(s: String; start, len: Integer)` | `String`  | Substring (1-based)            |
| `Pos`       | `(substr, s: String)`              | `Integer` | Find position (0 if not found) |
| `Concat`    | `(s1, s2: String)`                 | `String`  | Concatenate strings            |
| `UpCase`    | `(c: Char)`                        | `Char`    | Uppercase character            |
| `LowerCase` | `(s: String)`                      | `String`  | Lowercase string               |
| `UpperCase` | `(s: String)`                      | `String`  | Uppercase string               |
| `Trim`      | `(s: String)`                      | `String`  | Remove whitespace              |

### Conversion Functions

| Name         | Arguments      | Returns   | Description        |
|--------------|----------------|-----------|--------------------|
| `IntToStr`   | `(n: Integer)` | `String`  | Integer to string  |
| `FloatToStr` | `(x: Real)`    | `String`  | Float to string    |
| `StrToInt`   | `(s: String)`  | `Integer` | String to integer  |
| `StrToFloat` | `(s: String)`  | `Real`    | String to float    |
| `Ord`        | `(c: Char)`    | `Integer` | Character to ASCII |
| `Chr`        | `(n: Integer)` | `Char`    | ASCII to character |

### Math Functions

| Name    | Arguments   | Returns   | Description          |
|---------|-------------|-----------|----------------------|
| `Abs`   | `(x)`       | Same      | Absolute value       |
| `Sqr`   | `(x)`       | Same      | Square (x * x)       |
| `Sqrt`  | `(x: Real)` | `Real`    | Square root          |
| `Sin`   | `(x: Real)` | `Real`    | Sine                 |
| `Cos`   | `(x: Real)` | `Real`    | Cosine               |
| `Tan`   | `(x: Real)` | `Real`    | Tangent              |
| `Exp`   | `(x: Real)` | `Real`    | e^x                  |
| `Ln`    | `(x: Real)` | `Real`    | Natural logarithm    |
| `Round` | `(x: Real)` | `Integer` | Round to nearest     |
| `Trunc` | `(x: Real)` | `Integer` | Truncate toward zero |

### Ordinal Functions

| Name   | Arguments | Returns | Description        |
|--------|-----------|---------|--------------------|
| `Succ` | `(x)`     | Same    | Successor value    |
| `Pred` | `(x)`     | Same    | Predecessor value  |
| `Inc`  | `(var x)` | —       | Increment variable |
| `Dec`  | `(var x)` | —       | Decrement variable |

### Array Functions

| Name        | Arguments        | Returns   | Description          |
|-------------|------------------|-----------|----------------------|
| `Low`       | `(arr)`          | `Integer` | Lowest index         |
| `High`      | `(arr)`          | `Integer` | Highest index        |
| `Length`    | `(arr)`          | `Integer` | Array length         |
| `SetLength` | `(var arr; len)` | —         | Resize dynamic array |

---

## Units

Units provide modular code organization with separate interface and implementation sections.

### Unit Structure

```pascal
unit UnitName;

interface
  { Public declarations }
  const
    PublicConst = 100;

  type
    TPublicType = Integer;

  function PublicFunc(x: Integer): Integer;
  procedure PublicProc;

implementation

{ Uses clause for implementation-only dependencies }
uses OtherUnit;

{ Private declarations }
var
  privateVar: Integer;

{ Public implementations }
function PublicFunc(x: Integer): Integer;
begin
  PublicFunc := x * 2
end;

procedure PublicProc;
begin
  WriteLn('Hello')
end;

{ Initialization section (optional) }
begin
  privateVar := 0
end.
```

### Interface Section Rules

The interface section may contain:

- Constant declarations (`const`)
- Type declarations (`type`)
- Function and procedure signatures

**Variables cannot be declared in the interface section.** This ensures units don't expose mutable global state.

### Using Units

```pascal
program Main;
uses Unit1, Unit2, Unit3;

begin
  { Symbols from all used units are available }
  PublicProc;
  WriteLn(IntToStr(PublicFunc(5)))
end.
```

### Unit Dependencies

Units can use other units in their interface or implementation sections:

```pascal
unit HighLevel;

interface
  uses LowLevel;  { Dependency visible to users }

  function Process(x: Integer): Integer;

implementation

function Process(x: Integer): Integer;
begin
  Process := LowLevelFunc(x)
end;

end.
```

---

## Exception Handling

### Try ... Except

Catches and handles exceptions:

```pascal
try
  { Protected statements }
  riskyOperation;
except
  { Handler for any exception }
  WriteLn('An error occurred')
end;
```

With exception variable:

```pascal
try
  riskyOperation
except
  on E: Exception do
    WriteLn('Error: ' + E.Message)
end;
```

### Try ... Finally

Ensures cleanup code runs regardless of exceptions:

```pascal
try
  { Statements }
  allocateResource;
  useResource
finally
  { Always executes }
  freeResource
end;
```

### Raise

Raises an exception:

```pascal
raise Exception.Create('Something went wrong');

{ Or re-raise current exception }
try
  something
except
  WriteLn('Logging error');
  raise  { Re-raise }
end;
```

### Combining Try/Except and Try/Finally

```pascal
try
  try
    riskyOperation
  except
    handleError
  end
finally
  cleanup
end;
```

---

## Keyword Index

### A

- `abstract` — Abstract method modifier (no implementation, must be overridden)
- `and` — Boolean AND operator
- `array` — Array type declaration
- `as` — Safe type cast operator (RTTI)

### B

- `begin` — Start compound statement
- `boolean` — Boolean type

### C

- `case` — Multi-way branch statement
- `char` — Character type
- `class` — Class type declaration
- `const` — Constant declaration section
- `constructor` — Constructor method declaration

### D

- `destructor` — Destructor method declaration
- `div` — Integer division
- `do` — Loop body introducer
- `downto` — Decreasing for loop

### E

- `else` — Alternative branch
- `end` — End compound statement/program/unit
- `except` — Exception handler block

### F

- `false` — Boolean false value
- `finally` — Cleanup block
- `for` — Counted loop
- `function` — Function declaration

### I

- `if` — Conditional statement
- `implementation` — Unit implementation section
- `inherited` — Call base class method
- `integer` — Integer type
- `interface` — Unit interface section / Interface type declaration
- `is` — Runtime type check operator (RTTI)

### M

- `mod` — Modulus operator

### N

- `not` — Boolean NOT operator

### O

- `of` — Type specifier (array of, case of)
- `or` — Boolean OR operator
- `override` — Override virtual method from base class

### P

- `private` — Private visibility section in class
- `procedure` — Procedure declaration
- `program` — Program declaration
- `property` — Property declaration in class
- `public` — Public visibility section in class

### R

- `raise` — Raise exception
- `read` — Property read accessor specifier
- `real` — Real number type
- `record` — Record type declaration
- `repeat` — Post-test loop
- `Result` — Function return value variable

### S

- `Self` — Reference to current object instance
- `string` — String type

### T

- `then` — If statement body introducer
- `to` — Increasing for loop
- `true` — Boolean true value
- `try` — Exception handling block
- `type` — Type declaration section

### U

- `unit` — Unit declaration
- `until` — Repeat loop terminator
- `uses` — Unit import clause

### V

- `var` — Variable declaration section
- `virtual` — Virtual method modifier (can be overridden)

### W

- `weak` — Weak reference field modifier (prevents retain cycles)
- `while` — Pre-test loop
- `with` — Record/object field shortcut
- `write` — Property write accessor specifier

### X

- `xor` — Boolean XOR operator

---

## Implementation Notes

### Current Limitations

- Some standard library functions may not be available
- File I/O is limited
- Generic types are not yet supported
- Multiple inheritance is not supported (single class inheritance + multiple interfaces)

### VM vs Native Execution

Programs can run in two modes:

1. **VM Execution** (default): `vpascal program.pas`
    - Immediate execution via interpreter
    - Full debugging support
    - Overflow-checked arithmetic

2. **Native Compilation**: Via `ilc codegen arm64`
    - Compile to native ARM64 binary
    - Better performance
    - Requires explicit compilation step

### Compatibility Notes

Viper Pascal is inspired by standard Pascal but includes modernizations:

- Exception handling (try/except/finally)
- Dynamic strings
- 64-bit integers by default

For the most current information, see the source code and test files in `src/tests/data/pascal/`.

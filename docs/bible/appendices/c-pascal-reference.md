# Appendix C: Viper Pascal Reference

A comprehensive reference for Viper Pascal syntax and features. This appendix serves as a quick lookup for all Pascal language constructs, with examples, explanations, and cross-references to the chapters where each concept is taught in depth.

---

## Table of Contents

1. [Comments](#comments)
2. [Program Structure](#program-structure)
3. [Variables and Constants](#variables-and-constants)
4. [Data Types](#data-types)
5. [Operators](#operators)
6. [Control Flow](#control-flow)
7. [Procedures and Functions](#procedures-and-functions)
8. [Arrays](#arrays)
9. [Strings](#strings)
10. [Records](#records)
11. [Classes and Objects](#classes-and-objects)
12. [Interfaces](#interfaces)
13. [Inheritance](#inheritance)
14. [Exception Handling](#exception-handling)
15. [Units](#units)
16. [Built-in Functions](#built-in-functions)
17. [Input/Output](#inputoutput)
18. [Keywords Reference](#keywords-reference)

---

## Comments

Comments document your code and are ignored by the compiler.

> **See also:** [Chapter 2: Your First Program](../part1-foundations/02-first-program.md) for introduction to comments.

### Block Comments

```pascal
{ This is a block comment }
(* This is an alternative block comment syntax *)

{ Block comments can span
  multiple lines }
```

### Single-Line Comments

```pascal
// This is a single-line comment (modern Pascal syntax)
x := 42;  // Comments can appear at end of lines
```

### When to Use Each Style

| Style | Best For |
|-------|----------|
| `{ }` | Standard block comments, most common |
| `(* *)` | When you need to nest comments |
| `//` | Quick inline comments, modern code |

**Zia equivalent:**
```rust
// Single-line comment
/* Multi-line comment */
/// Documentation comment
```

**BASIC equivalent:**
```basic
' Single-line comment
REM Traditional comment
```

---

## Program Structure

Every Pascal program follows a specific structure.

### Complete Program

```pascal
program ProgramName;

uses Unit1, Unit2;    { Optional: import units }

const
  MaxValue = 100;     { Constant declarations }

type
  TCount = Integer;   { Type declarations }

var
  count: Integer;     { Variable declarations }

{ Procedure and function declarations }
procedure SayHello;
begin
  WriteLn('Hello!')
end;

{ Main program block }
begin
  SayHello
end.
```

### Minimal Program

```pascal
program Minimal;
begin
end.
```

### Program Sections (in order)

1. `program` header (required)
2. `uses` clause (optional)
3. `const` section (optional)
4. `type` section (optional)
5. `var` section (optional)
6. Procedure/function declarations (optional)
7. `begin`...`end.` main block (required)

> **See also:** [Chapter 2: Your First Program](../part1-foundations/02-first-program.md)

---

## Variables and Constants

Variables store values that can change; constants store fixed values.

> **See also:** [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md)

### Variable Declarations

```pascal
var
  count: Integer;           { Single variable }
  x, y, z: Integer;         { Multiple variables of same type }
  name: String;
  isActive: Boolean;

{ With initialization }
var
  score: Integer = 0;
  greeting: String = 'Hello';
```

### Constants

```pascal
const
  MaxPlayers = 4;
  Pi = 3.14159265359;
  GameTitle = 'Space Adventure';
  DebugMode = True;
```

### Typed Constants

```pascal
const
  DefaultWidth: Integer = 800;
  DefaultHeight: Integer = 600;
```

### Naming Conventions

| Convention | Example | Use For |
|------------|---------|---------|
| PascalCase | `PlayerScore` | Variables, procedures, functions |
| TPascalCase | `TPlayer` | Types (prefix T) |
| UPPER_CASE | `MAX_HEALTH` | Constants |
| FPascalCase | `FCount` | Private fields (prefix F) |

**Zia equivalent:**
```rust
var count = 0;        // Mutable variable
final PI = 3.14159;   // Immutable constant
```

**BASIC equivalent:**
```basic
DIM count AS INTEGER = 0
CONST PI = 3.14159
```

---

## Data Types

Every value in Pascal has a type.

> **See also:** [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md)

### Basic Types

| Type | Description | Range/Size |
|------|-------------|------------|
| `Integer` | Signed integer | 64-bit |
| `Real` | Floating point | 64-bit IEEE 754 |
| `Boolean` | Logical value | `True`, `False` |
| `Char` | Single character | 8-bit |
| `String` | Text string | Dynamic length |

### Integer Types

```pascal
var
  count: Integer;      { 64-bit signed, default choice }
  small: ShortInt;     { 8-bit signed }
  medium: SmallInt;    { 16-bit signed }
  large: LongInt;      { 32-bit signed }
  huge: Int64;         { 64-bit signed }
  positive: Cardinal;  { Unsigned integer }
  b: Byte;             { 0..255 }
  w: Word;             { 0..65535 }
```

### Floating-Point Types

```pascal
var
  price: Real;         { 64-bit, default choice }
  fast: Single;        { 32-bit, for graphics }
  precise: Double;     { 64-bit, same as Real }
  extended: Extended;  { Maximum precision }
```

### Boolean Type

```pascal
var
  isActive: Boolean;
  hasKey: Boolean;
begin
  isActive := True;
  hasKey := False;

  if isActive and not hasKey then
    WriteLn('Active but no key')
end.
```

### Character and String Types

```pascal
var
  letter: Char;
  name: String;
  shortName: String[50];  { Fixed-length string }
begin
  letter := 'A';
  name := 'Alice';
  shortName := 'Bob'
end.
```

### Type Aliases

```pascal
type
  TCount = Integer;
  TName = String;
  TScore = Integer;

var
  playerCount: TCount;
  playerName: TName;
```

**Zia equivalent:**
```rust
var count: Integer = 42;
var price: Number = 19.99;
var name: String = "Alice";
var active: Boolean = true;
```

---

## Operators

### Arithmetic Operators

```pascal
var a, b, result: Integer;
begin
  a := 10;
  b := 3;

  result := a + b;    { Addition: 13 }
  result := a - b;    { Subtraction: 7 }
  result := a * b;    { Multiplication: 30 }
  result := a div b;  { Integer division: 3 }
  result := a mod b;  { Modulo (remainder): 1 }

  { Real division }
  var r: Real;
  r := a / b;         { Division: 3.333... }
end.
```

### Comparison Operators

```pascal
var
  x, y: Integer;
  result: Boolean;
begin
  x := 5;
  y := 10;

  result := x = y;    { Equal: False }
  result := x <> y;   { Not equal: True }
  result := x < y;    { Less than: True }
  result := x <= y;   { Less or equal: True }
  result := x > y;    { Greater than: False }
  result := x >= y;   { Greater or equal: False }
end.
```

### Logical Operators

```pascal
var
  a, b, result: Boolean;
begin
  a := True;
  b := False;

  result := a and b;  { Logical AND: False }
  result := a or b;   { Logical OR: True }
  result := not a;    { Logical NOT: False }
  result := a xor b;  { Exclusive OR: True }
end.
```

### String Operators

```pascal
var
  s1, s2, combined: String;
begin
  s1 := 'Hello';
  s2 := 'World';
  combined := s1 + ', ' + s2 + '!';  { "Hello, World!" }
end.
```

### Operator Precedence (Highest to Lowest)

| Precedence | Operators |
|------------|-----------|
| 1 | `not`, `-` (unary) |
| 2 | `*`, `/`, `div`, `mod`, `and` |
| 3 | `+`, `-`, `or`, `xor` |
| 4 | `=`, `<>`, `<`, `<=`, `>`, `>=`, `in` |

> **See also:** [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md)

---

## Control Flow

### If/Then/Else

```pascal
{ Simple if }
if score > 100 then
  WriteLn('High score!');

{ If with else }
if temperature > 30 then
  WriteLn('Hot')
else
  WriteLn('Comfortable');

{ Multiple conditions }
if grade >= 90 then
  WriteLn('A')
else if grade >= 80 then
  WriteLn('B')
else if grade >= 70 then
  WriteLn('C')
else
  WriteLn('Below C');

{ Compound statements need begin/end }
if condition then
begin
  statement1;
  statement2
end
else
begin
  statement3;
  statement4
end;
```

### Case Statement

```pascal
case dayNumber of
  1: WriteLn('Monday');
  2: WriteLn('Tuesday');
  3: WriteLn('Wednesday');
  4: WriteLn('Thursday');
  5: WriteLn('Friday');
  6, 7: WriteLn('Weekend!');
else
  WriteLn('Invalid day')
end;

{ Range matching }
case score of
  0..10: WriteLn('Getting started');
  11..50: WriteLn('Making progress');
  51..100: WriteLn('Doing well!')
else
  WriteLn('Invalid score')
end;
```

### For Loop

```pascal
{ Count up }
for i := 1 to 10 do
  WriteLn(IntToStr(i));

{ Count down }
for i := 10 downto 1 do
  WriteLn(IntToStr(i));

{ With compound statement }
for i := 1 to 5 do
begin
  WriteLn('Iteration: ' + IntToStr(i));
  ProcessItem(i)
end;
```

### While Loop

```pascal
var count: Integer;
begin
  count := 0;
  while count < 10 do
  begin
    WriteLn(IntToStr(count));
    count := count + 1
  end
end.
```

### Repeat/Until Loop

```pascal
{ Always executes at least once }
var input: String;
begin
  repeat
    Write('Enter password: ');
    ReadLn(input)
  until Length(input) >= 8;

  WriteLn('Password accepted')
end.
```

### Break and Continue

```pascal
{ Break exits the loop }
for i := 1 to 100 do
begin
  if data[i] = target then
  begin
    WriteLn('Found at index ' + IntToStr(i));
    Break
  end
end;

{ Continue skips to next iteration }
for i := 1 to 100 do
begin
  if i mod 2 = 0 then
    Continue;  { Skip even numbers }
  WriteLn(IntToStr(i))  { Only prints odd numbers }
end;
```

> **See also:** [Chapter 4: Making Decisions](../part1-foundations/04-decisions.md), [Chapter 5: Repetition](../part1-foundations/05-repetition.md)

**Zia equivalent:**
```rust
if condition { ... } else { ... }
for i in 1..=10 { ... }
while condition { ... }
match value { 1 => ..., _ => ... }
```

---

## Procedures and Functions

### Procedures (No Return Value)

```pascal
procedure Greet(name: String);
begin
  WriteLn('Hello, ' + name + '!')
end;

{ Calling a procedure }
Greet('Alice');
```

### Functions (Return a Value)

```pascal
function Add(a: Integer; b: Integer): Integer;
begin
  Result := a + b
end;

{ Alternative: assign to function name }
function Multiply(a: Integer; b: Integer): Integer;
begin
  Multiply := a * b
end;

{ Using functions }
var sum: Integer;
begin
  sum := Add(3, 4);  { sum = 7 }
  WriteLn(IntToStr(Multiply(5, 6)))  { prints 30 }
end.
```

### Parameter Passing

```pascal
{ Value parameter (copy, default) }
procedure DoubleValue(x: Integer);
begin
  x := x * 2;  { Original unchanged }
  WriteLn(IntToStr(x))
end;

{ Reference parameter (modifies original) }
procedure DoubleRef(var x: Integer);
begin
  x := x * 2  { Original is modified }
end;

{ Constant parameter (read-only, efficient) }
procedure PrintName(const name: String);
begin
  WriteLn(name)
end;

{ Output parameter }
procedure Divide(a, b: Integer; out quotient, remainder: Integer);
begin
  quotient := a div b;
  remainder := a mod b
end;
```

### Default Parameters

```pascal
procedure Greet(name: String; greeting: String = 'Hello');
begin
  WriteLn(greeting + ', ' + name + '!')
end;

{ Calling }
Greet('Alice');           { "Hello, Alice!" }
Greet('Bob', 'Hi');       { "Hi, Bob!" }
```

### Local Variables

```pascal
function Calculate(x: Integer): Integer;
var
  temp: Integer;  { Local variable }
begin
  temp := x * 2;
  Result := temp + 1
end;
```

### Forward Declarations

```pascal
{ Forward declaration }
procedure ProcessB(x: Integer); forward;

procedure ProcessA(x: Integer);
begin
  if x > 0 then
    ProcessB(x - 1)
end;

procedure ProcessB(x: Integer);
begin
  if x > 0 then
    ProcessA(x - 1)
end;
```

> **See also:** [Chapter 7: Breaking It Down](../part1-foundations/07-functions.md)

**Zia equivalent:**
```rust
func greet(name: String) {
    Viper.Terminal.Say("Hello, " + name);
}

func add(a: Integer, b: Integer) -> Integer {
    return a + b;
}
```

---

## Arrays

Arrays store multiple values of the same type.

> **See also:** [Chapter 6: Collections](../part1-foundations/06-collections.md)

### Static Arrays

```pascal
var
  { Array with explicit bounds }
  scores: array[1..10] of Integer;

  { Zero-based array }
  items: array[0..99] of String;

  { Multi-dimensional array }
  grid: array[1..3, 1..3] of Integer;
```

### Array Operations

```pascal
var
  numbers: array[1..5] of Integer;
  i: Integer;
begin
  { Assign values }
  numbers[1] := 10;
  numbers[2] := 20;
  numbers[3] := 30;

  { Read values }
  WriteLn(IntToStr(numbers[1]));

  { Iterate }
  for i := 1 to 5 do
    WriteLn(IntToStr(numbers[i]));

  { Get bounds }
  WriteLn('Low: ' + IntToStr(Low(numbers)));   { 1 }
  WriteLn('High: ' + IntToStr(High(numbers))); { 5 }
  WriteLn('Length: ' + IntToStr(Length(numbers))) { 5 }
end.
```

### Dynamic Arrays

```pascal
var
  items: array of Integer;
begin
  { Set length }
  SetLength(items, 10);

  { Use like static array (0-based) }
  items[0] := 100;
  items[9] := 999;

  { Resize }
  SetLength(items, 20);

  { Get length }
  WriteLn(IntToStr(Length(items)))
end.
```

### Multi-Dimensional Arrays

```pascal
var
  matrix: array[1..3, 1..3] of Integer;
  row, col: Integer;
begin
  { Initialize }
  for row := 1 to 3 do
    for col := 1 to 3 do
      matrix[row, col] := row * col;

  { Access }
  WriteLn(IntToStr(matrix[2, 3]))  { 6 }
end.
```

### Array Type Declarations

```pascal
type
  TScores = array[1..100] of Integer;
  TMatrix = array[1..10, 1..10] of Real;
  TNames = array of String;  { Dynamic }

var
  scores: TScores;
  grid: TMatrix;
  names: TNames;
```

**Zia equivalent:**
```rust
var numbers = [1, 2, 3, 4, 5];
var first = numbers[0];
numbers.push(6);
numbers.length;
```

---

## Strings

Strings are sequences of characters.

> **See also:** [Chapter 8: Text and Strings](../part2-building-blocks/08-strings.md)

### String Basics

```pascal
var
  greeting: String;
  letter: Char;
begin
  greeting := 'Hello, World!';
  letter := greeting[1];  { 'H' - strings are 1-indexed }

  WriteLn(greeting);
  WriteLn('Length: ' + IntToStr(Length(greeting)))
end.
```

### String Operations

```pascal
var
  s: String;
begin
  s := 'Hello World';

  { Length }
  WriteLn(IntToStr(Length(s)));  { 11 }

  { Case conversion }
  WriteLn(UpperCase(s));  { HELLO WORLD }
  WriteLn(LowerCase(s));  { hello world }

  { Trimming }
  s := '  hello  ';
  WriteLn('[' + Trim(s) + ']');     { [hello] }
  WriteLn('[' + TrimLeft(s) + ']'); { [hello  ] }
  WriteLn('[' + TrimRight(s) + ']') { [  hello] }
end.
```

### Substring Operations

```pascal
var
  text: String;
begin
  text := 'Hello, World!';

  { Extract substring: Copy(str, start, count) }
  WriteLn(Copy(text, 1, 5));   { Hello }
  WriteLn(Copy(text, 8, 5));   { World }

  { Find position: Pos(substr, str) }
  WriteLn(IntToStr(Pos('World', text)));  { 8 }
  WriteLn(IntToStr(Pos('xyz', text)))     { 0 = not found }
end.
```

### String Modification

```pascal
var
  s: String;
begin
  s := 'Hello World';

  { Delete: Delete(str, start, count) }
  Delete(s, 6, 6);  { "Hello" }

  { Insert: Insert(substr, str, pos) }
  Insert(' there', s, 6);  { "Hello there" }

  { String replacement }
  s := StringReplace(s, 'there', 'World', [rfReplaceAll])
end.
```

### String Conversion

```pascal
var
  n: Integer;
  r: Real;
  s: String;
begin
  { Number to string }
  n := 42;
  s := IntToStr(n);       { "42" }

  r := 3.14159;
  s := FloatToStr(r);     { "3.14159" }

  { String to number }
  n := StrToInt('42');
  r := StrToFloat('3.14');

  { Safe conversion with default }
  n := StrToIntDef('abc', 0);  { Returns 0 on error }
end.
```

### String Building

```pascal
var
  parts: array[1..3] of String;
  result: String;
begin
  parts[1] := 'Hello';
  parts[2] := 'World';
  parts[3] := '!';

  { Concatenation }
  result := parts[1] + ', ' + parts[2] + parts[3];

  { Using Concat }
  result := Concat(parts[1], ', ', parts[2], parts[3])
end.
```

**Zia equivalent:**
```rust
var s = "Hello, World!";
s.toUpperCase();
s.substring(0, 5);
s.contains("World");
"Hello, ${name}!";  // String interpolation
```

---

## Records

Records group related data together.

> **See also:** [Chapter 11: Structures](../part2-building-blocks/11-structures.md)

### Record Definition

```pascal
type
  TPoint = record
    x: Integer;
    y: Integer
  end;

  TPerson = record
    name: String;
    age: Integer;
    active: Boolean
  end;
```

### Using Records

```pascal
var
  point: TPoint;
  person: TPerson;
begin
  { Assign fields }
  point.x := 10;
  point.y := 20;

  person.name := 'Alice';
  person.age := 30;
  person.active := True;

  { Read fields }
  WriteLn(IntToStr(point.x) + ', ' + IntToStr(point.y));
  WriteLn(person.name + ' is ' + IntToStr(person.age))
end.
```

### With Statement

```pascal
var
  player: TPlayer;
begin
  with player do
  begin
    name := 'Hero';
    health := 100;
    x := 50;
    y := 100
  end
end.
```

### Nested Records

```pascal
type
  TRectangle = record
    topLeft: TPoint;
    bottomRight: TPoint
  end;

var
  rect: TRectangle;
begin
  rect.topLeft.x := 0;
  rect.topLeft.y := 0;
  rect.bottomRight.x := 100;
  rect.bottomRight.y := 100
end.
```

### Arrays of Records

```pascal
type
  TPlayer = record
    name: String;
    score: Integer
  end;

var
  players: array[1..4] of TPlayer;
  i: Integer;
begin
  for i := 1 to 4 do
  begin
    players[i].name := 'Player ' + IntToStr(i);
    players[i].score := 0
  end
end.
```

**Zia equivalent:**
```rust
value Point {
    x: Integer;
    y: Integer;
}

var p = Point { x: 10, y: 20 };
```

---

## Classes and Objects

Classes combine data and behavior with reference semantics.

> **See also:** [Chapter 14: Objects and Classes](../part3-objects/14-objects.md)

### Class Definition

```pascal
type
  TCounter = class
  private
    FCount: Integer;
  public
    constructor Create;
    constructor Create(initial: Integer);
    procedure Increment;
    procedure Decrement;
    function GetCount: Integer;
  end;

constructor TCounter.Create;
begin
  FCount := 0
end;

constructor TCounter.Create(initial: Integer);
begin
  FCount := initial
end;

procedure TCounter.Increment;
begin
  FCount := FCount + 1
end;

procedure TCounter.Decrement;
begin
  if FCount > 0 then
    FCount := FCount - 1
end;

function TCounter.GetCount: Integer;
begin
  Result := FCount
end;
```

### Creating Objects

```pascal
var
  counter1, counter2: TCounter;
begin
  counter1 := TCounter.Create;        { Calls first constructor }
  counter2 := TCounter.Create(100);   { Calls second constructor }

  counter1.Increment;
  counter1.Increment;

  WriteLn(IntToStr(counter1.GetCount));  { 2 }
  WriteLn(IntToStr(counter2.GetCount))   { 100 }
end.
```

### Visibility Sections

| Section | Access |
|---------|--------|
| `public` | Accessible from anywhere |
| `private` | Only within the class |
| `protected` | Within class and descendants |
| `published` | Like public, with RTTI |

```pascal
type
  TBankAccount = class
  private
    FBalance: Real;
    FAccountNumber: String;
  protected
    FTransactionLog: String;
  public
    constructor Create(initialBalance: Real);
    procedure Deposit(amount: Real);
    function GetBalance: Real;
  end;
```

### Properties

```pascal
type
  TTemperature = class
  private
    FCelsius: Real;
    function GetFahrenheit: Real;
    procedure SetFahrenheit(value: Real);
  public
    property Celsius: Real read FCelsius write FCelsius;
    property Fahrenheit: Real read GetFahrenheit write SetFahrenheit;
  end;

function TTemperature.GetFahrenheit: Real;
begin
  Result := FCelsius * 9 / 5 + 32
end;

procedure TTemperature.SetFahrenheit(value: Real);
begin
  FCelsius := (value - 32) * 5 / 9
end;
```

### Self Reference

```pascal
procedure TPoint.MoveBy(dx: Integer; dy: Integer);
begin
  Self.x := Self.x + dx;
  Self.y := Self.y + dy
end;
```

### Result Variable

```pascal
function TCalc.Add(a: Integer; b: Integer): Integer;
begin
  Result := a + b
end;
```

**Zia equivalent:**
```rust
entity Counter {
    hide count: Integer;

    expose func init() {
        self.count = 0;
    }

    expose func increment() {
        self.count += 1;
    }

    expose func getCount() -> Integer {
        return self.count;
    }
}
```

---

## Interfaces

Interfaces define contracts that classes must implement.

> **See also:** [Chapter 16: Interfaces](../part3-objects/16-interfaces.md)

### Interface Definition

```pascal
type
  IDrawable = interface
    procedure Draw;
    function GetBounds: TRect;
  end;

  IClickable = interface
    procedure OnClick(x: Integer; y: Integer);
    function Contains(x: Integer; y: Integer): Boolean;
  end;
```

### Implementing Interfaces

```pascal
type
  TButton = class(IDrawable, IClickable)
  private
    FX, FY, FWidth, FHeight: Integer;
    FLabel: String;
  public
    constructor Create(x, y, w, h: Integer; lbl: String);

    { IDrawable }
    procedure Draw;
    function GetBounds: TRect;

    { IClickable }
    procedure OnClick(x: Integer; y: Integer);
    function Contains(x: Integer; y: Integer): Boolean;
  end;
```

### Using Interfaces

```pascal
var
  drawable: IDrawable;
  button: TButton;
begin
  button := TButton.Create(10, 10, 100, 30, 'OK');
  drawable := button;  { Assign to interface type }
  drawable.Draw        { Polymorphic call }
end.
```

**Zia equivalent:**
```rust
interface Drawable {
    func draw();
    func getBounds() -> Rect;
}

entity Button implements Drawable, Clickable {
    func draw() { ... }
    func getBounds() -> Rect { ... }
}
```

---

## Inheritance

Classes can extend other classes to inherit and specialize behavior.

> **See also:** [Chapter 15: Inheritance](../part3-objects/15-inheritance.md)

### Basic Inheritance

```pascal
type
  TAnimal = class
  protected
    FName: String;
  public
    constructor Create(name: String);
    procedure Speak; virtual;
    function GetName: String;
  end;

  TDog = class(TAnimal)
  private
    FBreed: String;
  public
    constructor Create(name: String; breed: String);
    procedure Speak; override;
    procedure Fetch;
  end;

constructor TAnimal.Create(name: String);
begin
  FName := name
end;

procedure TAnimal.Speak;
begin
  WriteLn(FName + ' makes a sound')
end;

function TAnimal.GetName: String;
begin
  Result := FName
end;

constructor TDog.Create(name: String; breed: String);
begin
  inherited Create(name);  { Call base constructor }
  FBreed := breed
end;

procedure TDog.Speak;
begin
  WriteLn(FName + ' barks: Woof!')
end;

procedure TDog.Fetch;
begin
  WriteLn(FName + ' fetches the ball')
end;
```

### Virtual and Override

```pascal
type
  TShape = class
  public
    procedure Draw; virtual;  { Can be overridden }
  end;

  TCircle = class(TShape)
  public
    procedure Draw; override;  { Overrides base }
  end;
```

### Abstract Methods

```pascal
type
  TShape = class
  public
    procedure Draw; virtual; abstract;  { Must be overridden }
    function Area: Real; virtual; abstract;
  end;
```

### Calling Inherited Methods

```pascal
procedure TDog.Speak;
begin
  inherited;           { Call base Speak }
  WriteLn('(wags tail)')
end;

{ Or explicitly }
procedure TDog.Speak;
begin
  inherited Speak;
  WriteLn('(wags tail)')
end;
```

### Type Checking: IS and AS

```pascal
var
  animal: TAnimal;
  dog: TDog;
begin
  animal := TDog.Create('Rex', 'German Shepherd');

  { IS operator - type check }
  if animal is TDog then
    WriteLn('It is a dog');

  { AS operator - type cast }
  dog := animal as TDog;
  dog.Fetch
end.
```

**Zia equivalent:**
```rust
entity Dog extends Animal {
    override expose func speak() {
        Viper.Terminal.Say(self.name + " barks: Woof!");
    }
}
```

---

## Exception Handling

Handle runtime errors gracefully.

> **See also:** [Chapter 10: Errors and Recovery](../part2-building-blocks/10-errors.md)

### Try/Except

```pascal
try
  x := StrToInt(input);
  result := 100 div x
except
  on E: EConvertError do
    WriteLn('Invalid number: ' + E.Message);
  on E: EDivByZero do
    WriteLn('Cannot divide by zero');
  on E: Exception do
    WriteLn('Error: ' + E.Message)
end;
```

### Try/Finally

```pascal
var
  f: TextFile;
begin
  AssignFile(f, 'data.txt');
  Reset(f);
  try
    { Read file }
    while not Eof(f) do
      ProcessLine(f)
  finally
    CloseFile(f)  { Always executed }
  end
end.
```

### Combined Try/Except/Finally

```pascal
try
  try
    { Risky operations }
  except
    on E: Exception do
      HandleError(E)
  end
finally
  Cleanup
end;
```

### Raising Exceptions

```pascal
procedure Withdraw(amount: Real);
begin
  if amount <= 0 then
    raise Exception.Create('Amount must be positive');
  if amount > FBalance then
    raise Exception.Create('Insufficient funds');
  FBalance := FBalance - amount
end;
```

### Custom Exception Classes

```pascal
type
  EValidationError = class(Exception)
  private
    FFieldName: String;
  public
    constructor Create(const msg: String; const field: String);
    property FieldName: String read FFieldName;
  end;

constructor EValidationError.Create(const msg: String; const field: String);
begin
  inherited Create(msg);
  FFieldName := field
end;
```

**Zia equivalent:**
```rust
try {
    var result = divide(a, b);
} catch DivisionByZeroError {
    Viper.Terminal.Say("Cannot divide by zero");
} finally {
    cleanup();
}

throw ValidationError("Invalid input");
```

---

## Units

Units organize code into reusable modules.

> **See also:** [Chapter 12: Modules](../part2-building-blocks/12-modules.md)

### Unit Structure

```pascal
unit MathUtils;

interface

{ Public declarations }
function Square(x: Integer): Integer;
function Cube(x: Integer): Integer;

implementation

{ Private declarations and implementations }

function Square(x: Integer): Integer;
begin
  Result := x * x
end;

function Cube(x: Integer): Integer;
begin
  Result := x * x * x
end;

{ Optional initialization section }
initialization
  WriteLn('MathUtils initialized');

{ Optional finalization section }
finalization
  WriteLn('MathUtils finalized');

end.
```

### Using Units

```pascal
program MyProgram;

uses MathUtils, StringUtils;

begin
  WriteLn(IntToStr(Square(5)));   { 25 }
  WriteLn(IntToStr(Cube(3)))      { 27 }
end.
```

### Unit Sections

| Section | Purpose |
|---------|---------|
| `interface` | Public declarations (types, procedures, functions) |
| `implementation` | Private code and public implementations |
| `initialization` | Code run when unit is loaded (optional) |
| `finalization` | Code run when program exits (optional) |

**Zia equivalent:**
```rust
module MathUtils;

export func square(x: Integer) -> Integer {
    return x * x;
}

// In another file:
bind MathUtils;
var result = MathUtils.square(5);
```

---

## Built-in Functions

### Math Functions

```pascal
{ Absolute value }
Abs(-5)           { 5 }

{ Sign }
Sign(-5)          { -1 }
Sign(0)           { 0 }
Sign(5)           { 1 }

{ Rounding }
Round(3.5)        { 4 }
Trunc(3.7)        { 3 }
Int(3.7)          { 3.0 }
Frac(3.7)         { 0.7 }
Ceil(3.2)         { 4 }
Floor(3.8)        { 3 }

{ Powers and roots }
Sqr(5)            { 25 - square }
Sqrt(16)          { 4.0 - square root }
Power(2, 10)      { 1024 }
Exp(1)            { 2.718... - e^x }
Ln(2.718)         { ~1 - natural log }
Log10(100)        { 2 }

{ Trigonometry (radians) }
Sin(Pi / 2)       { 1.0 }
Cos(0)            { 1.0 }
Tan(Pi / 4)       { 1.0 }
ArcSin(1)         { Pi/2 }
ArcCos(0)         { Pi/2 }
ArcTan(1)         { Pi/4 }

{ Min/Max }
Min(5, 10)        { 5 }
Max(5, 10)        { 10 }

{ Random }
Randomize;        { Initialize generator }
Random            { 0.0 to 1.0 }
Random(100)       { 0 to 99 }
```

### String Functions

```pascal
{ Length }
Length('hello')           { 5 }

{ Case }
UpperCase('hello')        { HELLO }
LowerCase('HELLO')        { hello }

{ Trimming }
Trim('  hi  ')            { 'hi' }
TrimLeft('  hi')          { 'hi' }
TrimRight('hi  ')         { 'hi' }

{ Substrings }
Copy('hello', 2, 3)       { 'ell' }
Pos('ll', 'hello')        { 3 }

{ Conversion }
IntToStr(42)              { '42' }
StrToInt('42')            { 42 }
FloatToStr(3.14)          { '3.14' }
StrToFloat('3.14')        { 3.14 }

{ Character }
Chr(65)                   { 'A' }
Ord('A')                  { 65 }
```

### Ordinal Functions

```pascal
{ Successor and predecessor }
Succ(5)           { 6 }
Pred(5)           { 4 }
Succ('A')         { 'B' }

{ Increment and decrement (modify in place) }
Inc(x)            { x := x + 1 }
Dec(x)            { x := x - 1 }
Inc(x, 5)         { x := x + 5 }
```

### Type Functions

```pascal
{ Size }
SizeOf(Integer)   { Size in bytes }

{ Type info }
TypeInfo(Integer) { Type information }

{ Range }
Low(array)        { Lowest index }
High(array)       { Highest index }
Length(array)     { Number of elements }
```

---

## Input/Output

### Console I/O

```pascal
{ Output }
Write('Hello');           { No newline }
WriteLn('World');         { With newline }
WriteLn('Value: ', x);    { Multiple values }

{ Input }
ReadLn(name);             { Read line into variable }
Read(x);                  { Read value }

{ Formatted output }
WriteLn(x:10);            { Right-aligned in 10 chars }
WriteLn(r:10:2);          { 10 chars, 2 decimal places }
```

### File I/O

```pascal
var
  f: TextFile;
  line: String;
begin
  { Writing to file }
  AssignFile(f, 'output.txt');
  Rewrite(f);
  WriteLn(f, 'Line 1');
  WriteLn(f, 'Line 2');
  CloseFile(f);

  { Reading from file }
  AssignFile(f, 'input.txt');
  Reset(f);
  while not Eof(f) do
  begin
    ReadLn(f, line);
    WriteLn(line)
  end;
  CloseFile(f);

  { Appending to file }
  AssignFile(f, 'log.txt');
  Append(f);
  WriteLn(f, 'New entry');
  CloseFile(f)
end.
```

> **See also:** [Chapter 9: Files and Persistence](../part2-building-blocks/09-files.md)

**Zia equivalent:**
```rust
Viper.Terminal.Say("Hello");
var input = Viper.Terminal.Ask("Name: ");
var file = Viper.File.Open("data.txt");
var content = file.readAll();
```

---

## Keywords Reference

### Reserved Words

```
and         array       as          begin       case
class       const       constructor destructor  div
do          downto      else        end         except
exports     file        finalization finally    for
forward     function    goto        if          implementation
in          inherited   initialization interface is
mod         nil         not         object      of
or          packed      procedure   program     property
raise       record      repeat      set         shl
shr         string      then        to          try
type        unit        until       uses        var
virtual     while       with        xor
```

### Keywords by Category

| Category | Keywords |
|----------|----------|
| **Program Structure** | `program`, `unit`, `uses`, `begin`, `end`, `interface`, `implementation`, `initialization`, `finalization` |
| **Declarations** | `const`, `type`, `var`, `procedure`, `function`, `constructor`, `destructor`, `property`, `forward` |
| **Control Flow** | `if`, `then`, `else`, `case`, `of`, `for`, `to`, `downto`, `do`, `while`, `repeat`, `until`, `goto` |
| **OOP** | `class`, `object`, `interface`, `inherited`, `virtual`, `override`, `abstract`, `is`, `as` |
| **Operators** | `and`, `or`, `not`, `xor`, `div`, `mod`, `shl`, `shr`, `in` |
| **Exception Handling** | `try`, `except`, `finally`, `raise`, `on` |
| **Types** | `array`, `record`, `set`, `file`, `string`, `packed` |
| **Other** | `nil`, `with`, `exports` |

---

## Quick Reference Card

### Variable Declaration
```pascal
var name: String;
var count: Integer = 0;
const PI = 3.14159;
```

### Conditionals
```pascal
if condition then ... else ...;
case value of 1: ...; 2: ...; else ... end;
```

### Loops
```pascal
for i := 1 to 10 do ...;
while condition do ...;
repeat ... until condition;
```

### Functions
```pascal
function Name(param: Type): ReturnType;
begin Result := value end;

procedure Name(param: Type);
begin ... end;
```

### Classes
```pascal
type TName = class ... end;
obj := TName.Create;
```

### Error Handling
```pascal
try ... except on E: Exception do ... end;
try ... finally ... end;
raise Exception.Create('message');
```

---

*[Back to Table of Contents](../README.md) | [Prev: Appendix B: BASIC Reference](b-basic-reference.md) | [Next: Appendix D: Runtime Library](d-runtime-reference.md)*

---
status: active
audience: public
last-updated: 2025-12-07
---

# Viper Pascal — Tutorial

Learn Viper Pascal by example. For a complete reference, see **[Pascal Reference](pascal-reference.md)**.

> **What is Viper Pascal?**
> A clean, modern Pascal dialect designed for clarity: structured programming, strong typing, units for modular code, and straightforward I/O. It runs on Viper's VM and can be compiled to native code.

---

## Table of Contents

1. [First Steps: Hello World](#1-first-steps-hello-world)
2. [Variables and Types](#2-variables-and-types)
3. [Control Flow](#3-control-flow)
4. [Procedures and Functions](#4-procedures-and-functions)
5. [Units and Modular Code](#5-units-and-modular-code)
6. [Arrays and Records](#6-arrays-and-records)
7. [String Operations](#7-string-operations)
8. [Exception Handling](#8-exception-handling)
9. [Example Programs](#9-example-programs)
10. [Where to Go Next](#10-where-to-go-next)

---

## 1. First Steps: Hello World

```pascal
program Hello;
begin
  WriteLn('Hello, World!')
end.
```

**Key points:**
- Programs start with `program Name;` and end with `end.`
- Statements are separated by semicolons
- `{ comments }` or `(* comments *)` for multi-line, `//` for single-line
- `WriteLn` prints with a newline; `Write` prints without

Run it:

```bash
vpascal hello.pas
```

---

## 2. Variables and Types

### Variable Declarations

Variables must be declared in a `var` section before use:

```pascal
program Variables;
var
  x, y: Integer;
  name: String;
  pi: Real;
  flag: Boolean;
begin
  x := 10;
  y := x + 5;
  name := 'Alice';
  pi := 3.14159;
  flag := True;

  WriteLn(IntToStr(y));    { 15 }
  WriteLn(name);           { Alice }
end.
```

### Basic Types

| Type | Description | Example |
|------|-------------|---------|
| `Integer` | 64-bit signed integer | `42`, `-17` |
| `Real` | 64-bit floating point | `3.14`, `-0.5` |
| `Boolean` | True or False | `True`, `False` |
| `Char` | Single character | `'A'`, `'x'` |
| `String` | Text string | `'Hello'` |

### Constants

```pascal
program Constants;
const
  MaxSize = 100;
  Pi = 3.14159;
  Greeting = 'Hello';
begin
  WriteLn(IntToStr(MaxSize));
  WriteLn(Greeting)
end.
```

---

## 3. Control Flow

### If ... Then ... Else

```pascal
program IfExample;
var n: Integer;
begin
  n := 5;

  if n = 0 then
    WriteLn('zero')
  else if n < 0 then
    WriteLn('negative')
  else
    WriteLn('positive')
end.
```

For multiple statements, use `begin`...`end`:

```pascal
if n > 0 then
begin
  WriteLn('n is positive');
  WriteLn(IntToStr(n))
end;
```

### For Loop

```pascal
program ForExample;
var i: Integer;
begin
  for i := 1 to 5 do
    WriteLn(IntToStr(i));

  { Count down }
  for i := 5 downto 1 do
    WriteLn(IntToStr(i))
end.
```

### While Loop

```pascal
program WhileExample;
var x: Integer;
begin
  x := 0;
  while x < 3 do
  begin
    WriteLn(IntToStr(x));
    x := x + 1
  end
end.
```

### Repeat ... Until

```pascal
program RepeatExample;
var i: Integer;
begin
  i := 3;
  repeat
    WriteLn(IntToStr(i));
    i := i - 1
  until i = 0
end.
```

### Case Statement

```pascal
program CaseExample;
var grade: Char;
begin
  grade := 'B';

  case grade of
    'A': WriteLn('Excellent');
    'B': WriteLn('Good');
    'C': WriteLn('Average');
    'D', 'F': WriteLn('Needs improvement')
  else
    WriteLn('Unknown grade')
  end
end.
```

---

## 4. Procedures and Functions

### Procedures

Procedures perform actions but don't return values:

```pascal
program ProcedureExample;

procedure Greet(name: String);
begin
  WriteLn('Hello, ' + name + '!')
end;

begin
  Greet('Alice');
  Greet('Bob')
end.
```

### Functions

Functions return values. Assign to the function name to return:

```pascal
program FunctionExample;

function Square(x: Integer): Integer;
begin
  Square := x * x
end;

function Factorial(n: Integer): Integer;
begin
  if n <= 1 then
    Factorial := 1
  else
    Factorial := n * Factorial(n - 1)
end;

var result: Integer;
begin
  result := Square(5);
  WriteLn(IntToStr(result));     { 25 }
  WriteLn(IntToStr(Factorial(5))) { 120 }
end.
```

### Local Variables

```pascal
function Sum(a, b: Integer): Integer;
var temp: Integer;
begin
  temp := a + b;
  Sum := temp
end;
```

---

## 5. Units and Modular Code

Units let you organize code into reusable modules.

### Creating a Unit

```pascal
{ MyMath.pas }
unit MyMath;

interface
  const Pi = 3.14159;
  function Square(x: Integer): Integer;
  function Cube(x: Integer): Integer;

implementation

function Square(x: Integer): Integer;
begin
  Square := x * x
end;

function Cube(x: Integer): Integer;
begin
  Cube := x * x * x
end;

end.
```

### Using a Unit

```pascal
{ Main.pas }
program Main;
uses MyMath;

var n: Integer;
begin
  n := 4;
  WriteLn(IntToStr(Square(n)));  { 16 }
  WriteLn(IntToStr(Cube(n)))     { 64 }
end.
```

**Key points:**
- `interface` section declares public symbols (visible to users)
- `implementation` section contains the actual code
- Variables cannot be exported from the interface section
- Use `uses` to import a unit's public symbols

---

## 6. Arrays and Records

### Arrays

```pascal
program ArrayExample;
var
  numbers: array[1..5] of Integer;
  i: Integer;
begin
  { Initialize }
  for i := 1 to 5 do
    numbers[i] := i * 10;

  { Print }
  for i := 1 to 5 do
    WriteLn(IntToStr(numbers[i]))
end.
```

### Records

```pascal
program RecordExample;
type
  Person = record
    name: String;
    age: Integer;
  end;

var p: Person;
begin
  p.name := 'Alice';
  p.age := 30;

  WriteLn(p.name);
  WriteLn(IntToStr(p.age))
end.
```

---

## 7. String Operations

```pascal
program StringExample;
var
  s: String;
  len: Integer;
begin
  s := 'Hello, World!';

  { Length }
  len := Length(s);
  WriteLn(IntToStr(len));     { 13 }

  { Substring (1-based) }
  WriteLn(Copy(s, 1, 5));     { Hello }

  { Concatenation }
  WriteLn('Say: ' + s);

  { Character access }
  WriteLn(s[1]);              { H }

  { Conversion }
  WriteLn(IntToStr(42));      { 42 }
  WriteLn(FloatToStr(3.14))   { 3.14 }
end.
```

---

## 8. Exception Handling

### Try ... Except

```pascal
program ExceptionExample;
var x, y: Integer;
begin
  x := 10;
  y := 0;

  try
    WriteLn(IntToStr(x div y))
  except
    WriteLn('Error: Division by zero!')
  end;

  WriteLn('Program continues...')
end.
```

### Try ... Finally

```pascal
program FinallyExample;
begin
  try
    WriteLn('Doing work...');
    { ... operations ... }
  finally
    WriteLn('Cleanup always runs')
  end
end.
```

### Raise

```pascal
program RaiseExample;

procedure CheckPositive(n: Integer);
begin
  if n < 0 then
    raise Exception.Create('Value must be positive')
end;

begin
  try
    CheckPositive(-5)
  except
    WriteLn('Caught an exception')
  end
end.
```

---

## 9. Example Programs

### Example A: Number Guessing Game

```pascal
program GuessingGame;
var
  secret, guess, attempts: Integer;
begin
  { Simple pseudo-random for demo }
  secret := 42;
  attempts := 0;

  WriteLn('Guess the number (1-100)!');

  repeat
    Write('Your guess: ');
    ReadLn(guess);
    attempts := attempts + 1;

    if guess < secret then
      WriteLn('Too low!')
    else if guess > secret then
      WriteLn('Too high!')
    else
      WriteLn('Correct! You got it in ' + IntToStr(attempts) + ' tries!')
  until guess = secret
end.
```

### Example B: Factorial Calculator

```pascal
program FactorialCalc;

function Factorial(n: Integer): Integer;
begin
  if n <= 1 then
    Factorial := 1
  else
    Factorial := n * Factorial(n - 1)
end;

var i: Integer;
begin
  WriteLn('Factorials 1-10:');
  for i := 1 to 10 do
    WriteLn(IntToStr(i) + '! = ' + IntToStr(Factorial(i)))
end.
```

### Example C: Fibonacci Sequence

```pascal
program Fibonacci;

function Fib(n: Integer): Integer;
begin
  if n <= 1 then
    Fib := n
  else
    Fib := Fib(n - 1) + Fib(n - 2)
end;

var i: Integer;
begin
  WriteLn('Fibonacci sequence:');
  for i := 0 to 10 do
    WriteLn(IntToStr(Fib(i)))
end.
```

---

## 10. Where to Go Next

**Learn More:**
- **[Pascal Reference](pascal-reference.md)** — Complete language reference
- **[IL Guide](il-guide.md)** — Understanding the intermediate language

**Explore Examples:**
- Browse `examples/pascal/` for more sample programs
- Check `src/tests/data/pascal/` for test cases

**Tools:**
- `vpascal program.pas` — Run a Pascal program
- `vpascal program.pas --emit-il` — See the generated IL
- `vpascal program.pas -o output.il` — Save IL to file

**Advanced Topics:**
- Multi-file compilation with units
- Exception handling patterns
- Native code compilation with `ilc codegen arm64`

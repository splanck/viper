# Appendix C: Pascal Reference

A quick reference for Viper Pascal syntax and features.

---

## Comments

```pascal
// Single-line comment

{ Multi-line
  comment }

(* Alternative
   multi-line comment *)
```

---

## Program Structure

```pascal
program HelloWorld;

uses SysUtils;  { Import modules }

const
    PI = 3.14159;

type
    TPoint = record
        X, Y: Double;
    end;

var
    greeting: string;

procedure SayHello;
begin
    WriteLn('Hello!');
end;

begin  { Main program }
    greeting := 'Hello, World!';
    WriteLn(greeting);
end.
```

---

## Variables

```pascal
var
    x: Integer;           { Declaration }
    y: Integer = 42;      { With initialization }
    a, b, c: Double;      { Multiple variables }

const
    PI = 3.14159;         { Compile-time constant }

type
    TMyInt = Integer;     { Type alias }
```

---

## Data Types

| Type | Description | Example |
|------|-------------|---------|
| `Integer` | 64-bit signed integer | `42`, `-7` |
| `Int64` | 64-bit signed integer | `42` |
| `Int32` | 32-bit signed integer | `42` |
| `Byte` | 8-bit unsigned | `255` |
| `Word` | 16-bit unsigned | `65535` |
| `Single` | 32-bit float | `3.14` |
| `Double` | 64-bit float | `3.14159265` |
| `Real` | Alias for Double | `3.14` |
| `Boolean` | True/False | `True`, `False` |
| `Char` | Single character | `'A'` |
| `String` | Text | `'hello'` |

---

## Operators

### Arithmetic
```pascal
a + b    { Addition }
a - b    { Subtraction }
a * b    { Multiplication }
a / b    { Real division }
a div b  { Integer division }
a mod b  { Modulo }
-a       { Negation }
```

### Comparison
```pascal
a = b    { Equal }
a <> b   { Not equal }
a < b    { Less than }
a <= b   { Less than or equal }
a > b    { Greater than }
a >= b   { Greater than or equal }
```

### Logical
```pascal
a and b  { Logical and }
a or b   { Logical or }
not a    { Logical not }
a xor b  { Exclusive or }
```

### Bitwise
```pascal
a and b  { Bitwise and }
a or b   { Bitwise or }
a xor b  { Bitwise xor }
not a    { Bitwise not }
a shl n  { Shift left }
a shr n  { Shift right }
```

### String
```pascal
a + b    { Concatenation }
```

---

## Control Flow

### If/Then/Else
```pascal
if condition then
begin
    { ... }
end
else if otherCondition then
begin
    { ... }
end
else
begin
    { ... }
end;

{ Single statement - no begin/end needed }
if x > 0 then
    WriteLn('positive');
```

### Case
```pascal
case value of
    1: WriteLn('one');
    2, 3: WriteLn('two or three');
    4..10: WriteLn('four to ten');
else
    WriteLn('other');
end;
```

### For Loop
```pascal
for i := 1 to 10 do
begin
    WriteLn(i);
end;

for i := 10 downto 1 do
begin
    WriteLn(i);
end;

{ For-in loop }
for item in collection do
begin
    WriteLn(item);
end;
```

### While Loop
```pascal
while condition do
begin
    { ... }
end;
```

### Repeat/Until
```pascal
repeat
    { ... }
until condition;  { Loops until condition is true }
```

### Break and Continue
```pascal
for i := 1 to 100 do
begin
    if i = 50 then Break;
    if i mod 2 = 0 then Continue;
    WriteLn(i);
end;
```

---

## Procedures and Functions

### Procedures
```pascal
procedure Greet(name: string);
begin
    WriteLn('Hello, ', name, '!');
end;

{ Call }
Greet('Alice');
```

### Functions
```pascal
function Add(a, b: Integer): Integer;
begin
    Result := a + b;  { Or: Add := a + b; }
end;

{ Call }
x := Add(3, 4);
```

### Parameter Passing
```pascal
{ By value (default) }
procedure ByValue(x: Integer);

{ By reference }
procedure ByRef(var x: Integer);
begin
    x := x + 1;  { Modifies original }
end;

{ Constant (read-only reference) }
procedure ByConst(const x: string);

{ Output parameter }
procedure GetValues(out a, b: Integer);
begin
    a := 1;
    b := 2;
end;
```

### Default Parameters
```pascal
procedure Greet(name: string; greeting: string = 'Hello');
begin
    WriteLn(greeting, ', ', name, '!');
end;

Greet('Alice');           { Uses default }
Greet('Bob', 'Hi');       { Uses provided }
```

### Overloading
```pascal
function Add(a, b: Integer): Integer; overload;
begin
    Result := a + b;
end;

function Add(a, b: Double): Double; overload;
begin
    Result := a + b;
end;
```

---

## Arrays

```pascal
var
    numbers: array[1..10] of Integer;       { Static array }
    matrix: array[1..5, 1..5] of Integer;   { 2D array }
    dynamic: array of Integer;               { Dynamic array }

{ Access }
numbers[1] := 42;
x := numbers[1];

{ Dynamic array operations }
SetLength(dynamic, 10);
SetLength(dynamic, Length(dynamic) + 1);

{ Array functions }
len := Length(numbers);
lo := Low(numbers);
hi := High(numbers);
```

### Open Arrays
```pascal
procedure ProcessArray(arr: array of Integer);
var
    i: Integer;
begin
    for i := Low(arr) to High(arr) do
        WriteLn(arr[i]);
end;
```

---

## Strings

```pascal
var
    s: string;
    c: Char;

s := 'Hello, World!';

{ String operations }
len := Length(s);
s := UpperCase(s);
s := LowerCase(s);
s := Trim(s);
s := Copy(s, 1, 5);     { Substring }
pos := Pos('World', s); { Find position }
s := StringReplace(s, 'World', 'Pascal', [rfReplaceAll]);

{ Concatenation }
s := 'Hello' + ', ' + 'World';

{ Character access (1-based!) }
c := s[1];  { First character }
```

---

## Records

```pascal
type
    TPoint = record
        X, Y: Double;
    end;

    TPerson = record
        Name: string;
        Age: Integer;

        procedure Birthday;
    end;

procedure TPerson.Birthday;
begin
    Inc(Age);
end;

var
    p: TPoint;
    person: TPerson;
begin
    p.X := 10.0;
    p.Y := 20.0;

    person.Name := 'Alice';
    person.Age := 30;
    person.Birthday;
end.
```

### With Statement
```pascal
with p do
begin
    X := 10.0;
    Y := 20.0;
end;
```

---

## Classes

```pascal
type
    TCounter = class
    private
        FCount: Integer;
    public
        constructor Create;
        constructor Create(initial: Integer);
        destructor Destroy; override;

        procedure Increment;
        function GetCount: Integer;

        property Count: Integer read FCount write FCount;
    end;

constructor TCounter.Create;
begin
    inherited Create;
    FCount := 0;
end;

constructor TCounter.Create(initial: Integer);
begin
    inherited Create;
    FCount := initial;
end;

destructor TCounter.Destroy;
begin
    { Cleanup }
    inherited Destroy;
end;

procedure TCounter.Increment;
begin
    Inc(FCount);
end;

function TCounter.GetCount: Integer;
begin
    Result := FCount;
end;

{ Usage }
var
    counter: TCounter;
begin
    counter := TCounter.Create;
    counter.Increment;
    WriteLn(counter.Count);
    counter.Free;
end.
```

### Inheritance
```pascal
type
    TAnimal = class
    protected
        FName: string;
    public
        constructor Create(name: string);
        procedure Speak; virtual;
    end;

    TDog = class(TAnimal)
    public
        procedure Speak; override;
    end;

procedure TAnimal.Speak;
begin
    WriteLn('...');
end;

procedure TDog.Speak;
begin
    WriteLn(FName, ' says Woof!');
end;
```

### Visibility
```pascal
private     { Only within class }
protected   { Class and descendants }
public      { Accessible everywhere }
published   { Public + runtime info }
```

---

## Interfaces

```pascal
type
    IDrawable = interface
        procedure Draw;
        function GetBounds: TRect;
    end;

    TCircle = class(TInterfacedObject, IDrawable)
    public
        procedure Draw;
        function GetBounds: TRect;
    end;
```

---

## Enumerations

```pascal
type
    TColor = (clRed, clGreen, clBlue);
    TDay = (Monday, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday);

var
    color: TColor;
    day: TDay;
begin
    color := clRed;
    day := Monday;

    { Ord gives numeric value }
    WriteLn(Ord(clGreen));  { 1 }
end.
```

---

## Sets

```pascal
type
    TDigit = 0..9;
    TDigitSet = set of TDigit;
    TCharSet = set of Char;

var
    digits: TDigitSet;
    vowels: TCharSet;
begin
    digits := [1, 2, 3];
    vowels := ['a', 'e', 'i', 'o', 'u'];

    if 5 in digits then
        WriteLn('5 is in set');

    digits := digits + [4, 5];  { Add }
    digits := digits - [1];      { Remove }
    digits := digits * [2, 3];   { Intersection }
end.
```

---

## Pointers

```pascal
type
    PInteger = ^Integer;

var
    x: Integer;
    p: PInteger;
begin
    x := 42;
    p := @x;       { Address of x }
    WriteLn(p^);   { Dereference: 42 }
    p^ := 100;     { Modify through pointer }
end.
```

---

## Units (Modules)

```pascal
unit MyUnit;

interface

{ Public declarations }
procedure PublicProc;
function PublicFunc: Integer;

type
    TPublicType = class
        { ... }
    end;

implementation

{ Private declarations }
procedure PrivateProc;
begin
    { ... }
end;

{ Implementation of public items }
procedure PublicProc;
begin
    { ... }
end;

function PublicFunc: Integer;
begin
    Result := 42;
end;

initialization
    { Code run when unit is loaded }

finalization
    { Code run when program ends }

end.
```

```pascal
{ Using the unit }
program Main;

uses MyUnit;

begin
    PublicProc;
end.
```

---

## Exception Handling

```pascal
try
    { Risky code }
    x := StrToInt(s);
except
    on E: EConvertError do
        WriteLn('Conversion error: ', E.Message);
    on E: Exception do
        WriteLn('Error: ', E.Message);
end;

{ With finally }
try
    f := TFileStream.Create('data.txt', fmOpenRead);
    try
        { Use file }
    finally
        f.Free;  { Always executed }
    end;
except
    on E: Exception do
        WriteLn('Error: ', E.Message);
end;

{ Raising exceptions }
raise Exception.Create('Something went wrong');
raise EInvalidArgument.CreateFmt('Invalid value: %d', [x]);
```

---

## Input/Output

### Console
```pascal
Write('No newline');
WriteLn('With newline');
WriteLn('Value: ', x);
WriteLn('Formatted: ', x:10:2);  { Width 10, 2 decimals }

ReadLn(name);
Read(x);
```

### Files
```pascal
var
    f: TextFile;
    line: string;
begin
    { Writing }
    AssignFile(f, 'data.txt');
    Rewrite(f);
    WriteLn(f, 'Line 1');
    WriteLn(f, 'Line 2');
    CloseFile(f);

    { Reading }
    AssignFile(f, 'data.txt');
    Reset(f);
    while not Eof(f) do
    begin
        ReadLn(f, line);
        WriteLn(line);
    end;
    CloseFile(f);
end.
```

---

## Generics

```pascal
type
    TBox<T> = class
    private
        FValue: T;
    public
        constructor Create(value: T);
        function GetValue: T;
        procedure SetValue(value: T);
        property Value: T read GetValue write SetValue;
    end;

constructor TBox<T>.Create(value: T);
begin
    FValue := value;
end;

function TBox<T>.GetValue: T;
begin
    Result := FValue;
end;

{ Usage }
var
    intBox: TBox<Integer>;
    strBox: TBox<string>;
begin
    intBox := TBox<Integer>.Create(42);
    strBox := TBox<string>.Create('hello');
end.
```

---

## Built-in Functions

### Math
```pascal
Abs(x)          { Absolute value }
Sqr(x)          { Square }
Sqrt(x)         { Square root }
Sin(x), Cos(x), Tan(x)  { Trigonometry }
ArcTan(x)       { Arctangent }
Ln(x)           { Natural logarithm }
Exp(x)          { e^x }
Power(base, exp) { Power }
Round(x)        { Round to nearest }
Trunc(x)        { Truncate }
Frac(x)         { Fractional part }
Int(x)          { Integer part }
Random          { Random 0..1 }
Random(n)       { Random 0..n-1 }
Randomize       { Seed random }
Min(a, b)       { Minimum }
Max(a, b)       { Maximum }
```

### String
```pascal
Length(s)       { String length }
UpperCase(s)    { To uppercase }
LowerCase(s)    { To lowercase }
Trim(s)         { Trim whitespace }
TrimLeft(s)     { Trim left }
TrimRight(s)    { Trim right }
Copy(s, start, len)   { Substring }
Pos(sub, s)     { Find position }
Insert(sub, s, pos)   { Insert substring }
Delete(s, pos, len)   { Delete substring }
StringReplace(s, old, new, flags)
IntToStr(n)     { Integer to string }
StrToInt(s)     { String to integer }
FloatToStr(f)   { Float to string }
StrToFloat(s)   { String to float }
Format(fmt, args)     { Formatted string }
Chr(n)          { ASCII code to char }
Ord(c)          { Char to ASCII code }
```

### Ordinal
```pascal
Inc(x)          { Increment }
Dec(x)          { Decrement }
Succ(x)         { Successor }
Pred(x)         { Predecessor }
Low(type)       { Lowest value }
High(type)      { Highest value }
```

---

## Keywords

```
and         array       as          asm         begin
case        class       const       constructor destructor
dispinterface           div         do          downto
else        end         except      exports     file
finalization            finally     for         function
goto        if          implementation          in
inherited   initialization          inline      interface
is          label       library     mod         nil
not         object      of          on          or
out         packed      procedure   program     property
raise       record      repeat      resourcestring
set         shl         shr         string      then
threadvar   to          try         type        unit
until       uses        var         while       with
xor
```

---

*[Back to Table of Contents](../README.md) | [Prev: Appendix B](b-basic-reference.md) | [Next: Appendix D: Runtime Library →](d-runtime-reference.md)*

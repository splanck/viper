# Mathematics

> Mathematical functions, vectors, matrices, bit operations, and random numbers.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Math](#vipermath)
- [Viper.Math.BigInt](#vipermathbigint)
- [Viper.Math.Bits](#vipermathbits)
- [Viper.Math.Easing](#vipermatheasing)
- [Viper.Math.Mat3](#vipermathmat3)
- [Viper.Math.Mat4](#vipermathmat4)
- [Viper.Math.PerlinNoise](#vipermathperlinnoise)
- [Viper.Math.Quaternion](#vipermathquaternion)
- [Viper.Math.Random](#vipermathrandom)
- [Viper.Math.Spline](#vipermathspline)
- [Viper.Math.Vec2](#vipermathvec2)
- [Viper.Math.Vec3](#vipermathvec3)

---

## Viper.Math.Bits

Bit manipulation utilities for working with 64-bit integers at the bit level.

**Type:** Static (no instantiation required)

### Methods

| Method             | Signature       | Description                            |
|--------------------|-----------------|----------------------------------------|
| `And(a, b)`        | `i64(i64, i64)` | Bitwise AND                            |
| `Or(a, b)`         | `i64(i64, i64)` | Bitwise OR                             |
| `Xor(a, b)`        | `i64(i64, i64)` | Bitwise XOR                            |
| `Not(val)`         | `i64(i64)`      | Bitwise NOT (complement)               |
| `Shl(val, count)`  | `i64(i64, i64)` | Logical shift left                     |
| `Shr(val, count)`  | `i64(i64, i64)` | Arithmetic shift right (sign-extended) |
| `Ushr(val, count)` | `i64(i64, i64)` | Logical shift right (zero-fill)        |
| `Rotl(val, count)` | `i64(i64, i64)` | Rotate left                            |
| `Rotr(val, count)` | `i64(i64, i64)` | Rotate right                           |
| `Count(val)`       | `i64(i64)`      | Population count (number of 1 bits)    |
| `LeadZ(val)`       | `i64(i64)`      | Count leading zeros                    |
| `TrailZ(val)`      | `i64(i64)`      | Count trailing zeros                   |
| `Flip(val)`        | `i64(i64)`      | Reverse all 64 bits                    |
| `Swap(val)`        | `i64(i64)`      | Byte swap (endian swap)                |
| `Get(val, bit)`    | `i1(i64, i64)`  | Get bit at position (0-63)             |
| `Set(val, bit)`    | `i64(i64, i64)` | Set bit at position                    |
| `Clear(val, bit)`  | `i64(i64, i64)` | Clear bit at position                  |
| `Toggle(val, bit)` | `i64(i64, i64)` | Toggle bit at position                 |

### Method Details

#### Shift Operations

- **Shl** — Logical shift left. Shifts bits left, filling with zeros on the right.
- **Shr** — Arithmetic shift right. Shifts bits right, preserving the sign bit (sign-extended).
- **Ushr** — Logical shift right. Shifts bits right, filling with zeros on the left.

Shift counts are clamped: negative counts or counts >= 64 return 0 (for Shl/Ushr) or the sign bit extended (for Shr with
negative values).

#### Rotate Operations

- **Rotl** — Rotate left. Bits shifted out on the left wrap around to the right.
- **Rotr** — Rotate right. Bits shifted out on the right wrap around to the left.

Rotate counts are normalized to 0-63 (count MOD 64).

#### Bit Counting

- **Count** — Population count (popcount). Returns the number of 1 bits.
- **LeadZ** — Count leading zeros. Returns 64 for zero, 0 for negative values.
- **TrailZ** — Count trailing zeros. Returns 64 for zero.

#### Single Bit Operations

All single-bit operations accept bit positions 0-63. Out-of-range positions return the input unchanged (for
Set/Clear/Toggle) or false (for Get).

### Zia Example

```zia
module BitsDemo;

bind Viper.Terminal;
bind Viper.Math.Bits as Bits;
bind Viper.Fmt as Fmt;

func start() {
    Say("And: " + Fmt.Int(Bits.And(12, 10)));     // 8
    Say("Or: " + Fmt.Int(Bits.Or(12, 10)));        // 14
    Say("Xor: " + Fmt.Int(Bits.Xor(12, 10)));     // 6
    Say("Shl: " + Fmt.Int(Bits.Shl(1, 4)));       // 16
    Say("Count: " + Fmt.Int(Bits.Count(255)));     // 8
    Say("LeadZ: " + Fmt.Int(Bits.LeadZ(1)));       // 63
}
```

### BASIC Example

```basic
' Basic bitwise operations
DIM a AS INTEGER = &HFF
DIM b AS INTEGER = &H0F
PRINT Viper.Math.Bits.And(a, b)  ' 15 (&H0F)
PRINT Viper.Math.Bits.Or(a, b)   ' 255 (&HFF)
PRINT Viper.Math.Bits.Xor(a, b)  ' 240 (&HF0)

' Shift operations
DIM val AS INTEGER = 1
PRINT Viper.Math.Bits.Shl(val, 4)   ' 16
PRINT Viper.Math.Bits.Shr(16, 2)    ' 4

' Count set bits
DIM mask AS INTEGER = &HFF
PRINT Viper.Math.Bits.Count(mask)   ' 8

' Work with individual bits
DIM flags AS INTEGER = 0
flags = Viper.Math.Bits.Set(flags, 0)    ' Set bit 0
flags = Viper.Math.Bits.Set(flags, 3)    ' Set bit 3
PRINT Viper.Math.Bits.Get(flags, 0)      ' True
PRINT Viper.Math.Bits.Get(flags, 1)      ' False
flags = Viper.Math.Bits.Toggle(flags, 3) ' Toggle bit 3 off
PRINT flags                          ' 1

' Endian conversion
DIM big AS INTEGER = &H0102030405060708
DIM little AS INTEGER = Viper.Math.Bits.Swap(big)
' little = &H0807060504030201
```

---

## Viper.Math

Mathematical functions and constants.

**Type:** Static utility class

### Constants

| Property | Type     | Description                          |
|----------|----------|--------------------------------------|
| `Pi`     | `Double` | π (3.14159265358979...)              |
| `E`      | `Double` | Euler's number (2.71828182845904...) |
| `Tau`    | `Double` | τ = 2π (6.28318530717958...)         |

### Basic Functions

| Method           | Signature                | Description                               |
|------------------|--------------------------|-------------------------------------------|
| `Abs(x)`         | `Double(Double)`         | Absolute value of a floating-point number |
| `AbsInt(x)`      | `Integer(Integer)`       | Absolute value of an integer              |
| `Sqrt(x)`        | `Double(Double)`         | Square root                               |
| `Pow(base, exp)` | `Double(Double, Double)` | Raises base to the power of exp           |
| `Exp(x)`         | `Double(Double)`         | e raised to the power x                   |
| `Sgn(x)`         | `Double(Double)`         | Sign of x: -1, 0, or 1                    |
| `SgnInt(x)`      | `Integer(Integer)`       | Sign of integer x: -1, 0, or 1            |

### Trigonometric Functions

| Method        | Signature                | Description                                            |
|---------------|--------------------------|--------------------------------------------------------|
| `Sin(x)`      | `Double(Double)`         | Sine (radians)                                         |
| `Cos(x)`      | `Double(Double)`         | Cosine (radians)                                       |
| `Tan(x)`      | `Double(Double)`         | Tangent (radians)                                      |
| `Atan(x)`     | `Double(Double)`         | Arctangent (returns radians)                           |
| `Atan2(y, x)` | `Double(Double, Double)` | Arctangent of y/x (returns radians, respects quadrant) |
| `Asin(x)`     | `Double(Double)`         | Arc sine (returns radians)                             |
| `Acos(x)`     | `Double(Double)`         | Arc cosine (returns radians)                           |

### Hyperbolic Functions

| Method    | Signature        | Description        |
|-----------|------------------|--------------------|
| `Sinh(x)` | `Double(Double)` | Hyperbolic sine    |
| `Cosh(x)` | `Double(Double)` | Hyperbolic cosine  |
| `Tanh(x)` | `Double(Double)` | Hyperbolic tangent |

### Logarithmic Functions

| Method     | Signature        | Description                |
|------------|------------------|----------------------------|
| `Log(x)`   | `Double(Double)` | Natural logarithm (base e) |
| `Log10(x)` | `Double(Double)` | Base-10 logarithm          |
| `Log2(x)`  | `Double(Double)` | Base-2 logarithm           |

### Rounding Functions

| Method     | Signature        | Description                                 |
|------------|------------------|---------------------------------------------|
| `Floor(x)` | `Double(Double)` | Largest integer less than or equal to x     |
| `Ceil(x)`  | `Double(Double)` | Smallest integer greater than or equal to x |
| `Round(x)` | `Double(Double)` | Round to nearest integer                    |
| `Trunc(x)` | `Double(Double)` | Truncate toward zero                        |

### Min/Max Functions

| Method                  | Signature                            | Description                          |
|-------------------------|--------------------------------------|--------------------------------------|
| `Min(a, b)`             | `Double(Double, Double)`             | Smaller of two floating-point values |
| `Max(a, b)`             | `Double(Double, Double)`             | Larger of two floating-point values  |
| `MinInt(a, b)`          | `Integer(Integer, Integer)`          | Smaller of two integers              |
| `MaxInt(a, b)`          | `Integer(Integer, Integer)`          | Larger of two integers               |
| `Clamp(val, lo, hi)`    | `Double(Double, Double, Double)`     | Constrain value to range [lo, hi]    |
| `ClampInt(val, lo, hi)` | `Integer(Integer, Integer, Integer)` | Constrain integer to range [lo, hi]  |

### Utility Functions

| Method                 | Signature                            | Description                       |
|------------------------|--------------------------------------|-----------------------------------|
| `FMod(x, y)`           | `Double(Double, Double)`             | Floating-point remainder of x/y   |
| `Lerp(a, b, t)`        | `Double(Double, Double, Double)`     | Linear interpolation: a + t*(b-a) |
| `Wrap(val, lo, hi)`    | `Double(Double, Double, Double)`     | Wrap value to range [lo, hi)      |
| `WrapInt(val, lo, hi)` | `Integer(Integer, Integer, Integer)` | Wrap integer to range [lo, hi)    |
| `Hypot(x, y)`          | `Double(Double, Double)`             | Hypotenuse: sqrt(x² + y²)         |

### Angle Conversion

| Method         | Signature        | Description                |
|----------------|------------------|----------------------------|
| `Deg(radians)` | `Double(Double)` | Convert radians to degrees |
| `Rad(degrees)` | `Double(Double)` | Convert degrees to radians |

### Zia Example

```zia
module MathDemo;

bind Viper.Terminal;
bind Viper.Math as Math;
bind Viper.Fmt as Fmt;

func start() {
    Say("Sqrt(144): " + Fmt.NumFixed(Math.Sqrt(144.0), 1));          // 12.0
    Say("Pow(2,10): " + Fmt.NumFixed(Math.Pow(2.0, 10.0), 0));      // 1024
    Say("AbsInt(-42): " + Fmt.Int(Math.AbsInt(-42)));                 // 42
    Say("MinInt(3,7): " + Fmt.Int(Math.MinInt(3, 7)));                // 3
    Say("MaxInt(3,7): " + Fmt.Int(Math.MaxInt(3, 7)));                // 7
    Say("ClampInt(15,0,10): " + Fmt.Int(Math.ClampInt(15, 0, 10)));   // 10
    Say("Floor(3.7): " + Fmt.NumFixed(Math.Floor(3.7), 0));           // 3
    Say("Ceil(3.2): " + Fmt.NumFixed(Math.Ceil(3.2), 0));             // 4
}
```

### BASIC Example

```basic
' Using constants
PRINT Viper.Math.Pi              ' Output: 3.14159265358979
PRINT Viper.Math.E               ' Output: 2.71828182845905

' Basic math
PRINT Viper.Math.Sqrt(16)        ' Output: 4.0
PRINT Viper.Math.Pow(2, 10)      ' Output: 1024.0
PRINT Viper.Math.Abs(-42.5)      ' Output: 42.5

' Rounding
PRINT Viper.Math.Floor(2.7)      ' Output: 2.0
PRINT Viper.Math.Ceil(2.1)       ' Output: 3.0
PRINT Viper.Math.Round(2.5)      ' Output: 3.0
PRINT Viper.Math.Trunc(-2.7)     ' Output: -2.0

' Trigonometry (radians)
PRINT Viper.Math.Sin(Viper.Math.Pi / 2)  ' Output: 1.0
PRINT Viper.Math.Cos(0)                   ' Output: 1.0

' Angle conversion
PRINT Viper.Math.Deg(Viper.Math.Pi)      ' Output: 180.0
PRINT Viper.Math.Rad(90)                  ' Output: 1.5707963...

' Min/Max/Clamp
PRINT Viper.Math.MaxInt(10, 20)          ' Output: 20
PRINT Viper.Math.Clamp(15, 0, 10)        ' Output: 10.0

' Lerp and Wrap
PRINT Viper.Math.Lerp(0, 100, 0.5)       ' Output: 50.0
PRINT Viper.Math.Wrap(370, 0, 360)       ' Output: 10.0

' Geometry
PRINT Viper.Math.Hypot(3, 4)             ' Output: 5.0
```

---

## Viper.Math.Random

Random number generation with uniform and distribution-based functions.

**Type:** Static utility class

**Constructor:** `Viper.Math.Random.New(seed)` - Create a seeded random generator instance

### Core Methods

| Method         | Signature          | Description                                     |
|----------------|--------------------|-------------------------------------------------|
| `Seed(value)`  | `Void(Integer)`    | Seeds the random number generator               |
| `Next()`       | `Double()`         | Returns a random double in the range [0.0, 1.0) |
| `NextInt(max)` | `Integer(Integer)` | Returns a random integer in the range [0, max)  |

### Distribution Methods

| Method                     | Signature              | Description                                           |
|----------------------------|------------------------|-------------------------------------------------------|
| `Range(min, max)`          | `Integer(Integer, Integer)` | Returns a random integer in [min, max] inclusive |
| `Gaussian(mean, stddev)`   | `Double(Double, Double)` | Returns a normally distributed random value         |
| `Exponential(lambda)`      | `Double(Double)`       | Returns an exponentially distributed random value     |
| `Dice(sides)`              | `Integer(Integer)`     | Simulates a dice roll, returns [1, sides]             |
| `Chance(probability)`      | `Integer(Double)`      | Returns 1 with probability p, otherwise 0             |
| `Shuffle(seq)`             | `Void(Seq)`            | Randomly shuffles a sequence in place                 |

### Zia Example

```zia
module RandomDemo;

bind Viper.Terminal;
bind Viper.Math.Random as Random;
bind Viper.Fmt as Fmt;

func start() {
    Say("Range(1,100): " + Fmt.Int(Random.Range(1, 100)));
    Say("Float: " + Fmt.NumFixed(Random.Next(), 4));
    Say("Dice d6: " + Fmt.Int(Random.Dice(6)));
}
```

### BASIC Example

```basic
' Seed for reproducible sequences
Viper.Math.Random.Seed(12345)

' Random float between 0 and 1
DIM r AS DOUBLE
r = Viper.Math.Random.Next()
PRINT r  ' Output: 0.123... (varies)

' Random integer 0-99
DIM n AS INTEGER
n = Viper.Math.Random.NextInt(100)
PRINT n  ' Output: 0-99 (varies)

' Random integer in range [1, 10] inclusive
DIM x AS INTEGER
x = Viper.Math.Random.Range(1, 10)
PRINT x  ' Output: 1-10 (varies)

' Simulate dice roll (1-6)
DIM die AS INTEGER
die = Viper.Math.Random.Dice(6)
PRINT "You rolled: "; die

' Gaussian distribution (bell curve) with mean=100, stddev=15
DIM iq AS DOUBLE
iq = Viper.Math.Random.Gaussian(100.0, 15.0)
PRINT "IQ Score: "; INT(iq)

' Exponential distribution (for waiting times, etc.)
DIM waitTime AS DOUBLE
waitTime = Viper.Math.Random.Exponential(0.5)  ' mean = 2.0
PRINT "Wait: "; waitTime

' 70% chance of success
IF Viper.Math.Random.Chance(0.7) = 1 THEN
    PRINT "Success!"
ELSE
    PRINT "Failed."
END IF

' Shuffle a sequence
DIM seq AS Viper.Collections.Seq
seq = Viper.Collections.Seq.New()
FOR i = 1 TO 5
    seq.Push(Viper.Core.Box.I64(i))
NEXT i
Viper.Math.Random.Shuffle(seq)  ' Now shuffled: e.g., [3, 1, 5, 2, 4]
```

### Notes

- All random functions use the same LCG (Linear Congruential Generator)
- Sequences are deterministic for a given seed
- `Gaussian` uses the Box-Muller transform for accurate normal distribution
- `Exponential(lambda)` produces values with mean = 1/lambda
- `Range(a, b)` automatically swaps bounds if min > max
- `Shuffle` performs a Fisher-Yates shuffle (O(n) complexity)

---

## Viper.Math.Vec2

2D vector math for positions, directions, velocities, and physics calculations.

**Type:** Instance (obj)
**Constructor:** `Viper.Math.Vec2.New(x, y)` or `Viper.Math.Vec2.Zero()` or `Viper.Math.Vec2.One()`

### Static Constructors

| Method      | Signature       | Description                               |
|-------------|-----------------|-------------------------------------------|
| `New(x, y)` | `obj(f64, f64)` | Create a new vector with given components |
| `Zero()`    | `obj()`         | Create a vector at origin (0, 0)          |
| `One()`     | `obj()`         | Create a vector (1, 1)                    |

### Properties

| Property | Type  | Description             |
|----------|-------|-------------------------|
| `X`      | `f64` | X component (read-only) |
| `Y`      | `f64` | Y component (read-only) |

### Methods

| Method           | Signature       | Description                                                |
|------------------|-----------------|------------------------------------------------------------|
| `Add(other)`     | `obj(obj)`      | Add two vectors: self + other                              |
| `Sub(other)`     | `obj(obj)`      | Subtract vectors: self - other                             |
| `Mul(scalar)`    | `obj(f64)`      | Multiply by scalar: self * s                               |
| `Div(scalar)`    | `obj(f64)`      | Divide by scalar: self / s                                 |
| `Neg()`          | `obj()`         | Negate vector: -self                                       |
| `Dot(other)`     | `f64(obj)`      | Dot product of two vectors                                 |
| `Cross(other)`   | `f64(obj)`      | 2D cross product (scalar z-component)                      |
| `Len()`          | `f64()`         | Length (magnitude) of vector                               |
| `LenSq()`        | `f64()`         | Squared length (avoids sqrt)                               |
| `Norm()`         | `obj()`         | Normalize to unit length                                   |
| `Dist(other)`    | `f64(obj)`      | Distance to another point                                  |
| `Lerp(other, t)` | `obj(obj, f64)` | Linear interpolation (t=0 returns self, t=1 returns other) |
| `Angle()`        | `f64()`         | Angle in radians (atan2(y, x))                             |
| `Rotate(angle)`  | `obj(f64)`      | Rotate by angle in radians                                 |

### Notes

- Vectors are immutable - all operations return new vectors
- `Norm()` returns zero vector if input has zero length
- `Div()` traps on division by zero
- `Cross()` returns the scalar z-component of the 3D cross product (treating 2D vectors as 3D with z=0)
- Angles are in radians; use `Viper.Math.Rad()` and `Viper.Math.Deg()` for conversion

### Zia Example

```zia
module Vec2Demo;

bind Viper.Terminal;
bind Viper.Math.Vec2 as V2;
bind Viper.Fmt as Fmt;

func start() {
    var a = V2.New(3.0, 4.0);
    Say("Length: " + Fmt.NumFixed(V2.Len(a), 2));            // 5.00

    var b = V2.New(1.0, 0.0);
    var c = V2.Add(a, b);
    Say("Add length: " + Fmt.NumFixed(V2.Len(c), 4));        // 5.6569

    var n = V2.Norm(a);
    Say("Normalized: " + Fmt.NumFixed(V2.Len(n), 2));         // 1.00
}
```

### BASIC Example

```basic
' Create vectors
DIM pos AS OBJECT = Viper.Math.Vec2.New(100.0, 200.0)
DIM vel AS OBJECT = Viper.Math.Vec2.New(5.0, -3.0)

' Move position by velocity
pos = pos.Add(vel)
PRINT "Position: ("; pos.X; ", "; pos.Y; ")"

' Calculate distance
DIM target AS OBJECT = Viper.Math.Vec2.New(150.0, 180.0)
DIM dist AS DOUBLE = pos.Dist(target)
PRINT "Distance to target: "; dist

' Normalize to get direction
DIM dir AS OBJECT = vel.Norm()
PRINT "Direction: ("; dir.X; ", "; dir.Y; ")"
PRINT "Direction length: "; dir.Len()  ' Should be 1.0

' Rotate a vector 90 degrees
DIM right AS OBJECT = Viper.Math.Vec2.New(1.0, 0.0)
DIM up AS OBJECT = right.Rotate(3.14159265 / 2.0)
PRINT "Rotated: ("; up.X; ", "; up.Y; ")"  ' (0, 1)

' Linear interpolation for smooth movement
DIM start AS OBJECT = Viper.Math.Vec2.Zero()
DIM endpoint AS OBJECT = Viper.Math.Vec2.New(100.0, 100.0)
DIM midpoint AS OBJECT = start.Lerp(endpoint, 0.5)
PRINT "Midpoint: ("; midpoint.X; ", "; midpoint.Y; ")"  ' (50, 50)

' Dot product to check perpendicularity
DIM a AS OBJECT = Viper.Math.Vec2.New(1.0, 0.0)
DIM b AS OBJECT = Viper.Math.Vec2.New(0.0, 1.0)
IF a.Dot(b) = 0.0 THEN
    PRINT "Vectors are perpendicular"
END IF
```

---

## Viper.Math.Vec3

3D vector math for positions, directions, velocities, and physics calculations in 3D space.

**Type:** Instance (obj)
**Constructor:** `Viper.Math.Vec3.New(x, y, z)` or `Viper.Math.Vec3.Zero()` or `Viper.Math.Vec3.One()`

### Static Constructors

| Method         | Signature            | Description                               |
|----------------|----------------------|-------------------------------------------|
| `New(x, y, z)` | `obj(f64, f64, f64)` | Create a new vector with given components |
| `Zero()`       | `obj()`              | Create a vector at origin (0, 0, 0)       |
| `One()`        | `obj()`              | Create a vector (1, 1, 1)                 |

### Properties

| Property | Type  | Description             |
|----------|-------|-------------------------|
| `X`      | `f64` | X component (read-only) |
| `Y`      | `f64` | Y component (read-only) |
| `Z`      | `f64` | Z component (read-only) |

### Methods

| Method           | Signature       | Description                                                |
|------------------|-----------------|------------------------------------------------------------|
| `Add(other)`     | `obj(obj)`      | Add two vectors: self + other                              |
| `Sub(other)`     | `obj(obj)`      | Subtract vectors: self - other                             |
| `Mul(scalar)`    | `obj(f64)`      | Multiply by scalar: self * s                               |
| `Div(scalar)`    | `obj(f64)`      | Divide by scalar: self / s                                 |
| `Neg()`          | `obj()`         | Negate vector: -self                                       |
| `Dot(other)`     | `f64(obj)`      | Dot product of two vectors                                 |
| `Cross(other)`   | `obj(obj)`      | Cross product (returns Vec3)                               |
| `Len()`          | `f64()`         | Length (magnitude) of vector                               |
| `LenSq()`        | `f64()`         | Squared length (avoids sqrt)                               |
| `Norm()`         | `obj()`         | Normalize to unit length                                   |
| `Dist(other)`    | `f64(obj)`      | Distance to another point                                  |
| `Lerp(other, t)` | `obj(obj, f64)` | Linear interpolation (t=0 returns self, t=1 returns other) |

### Notes

- Vectors are immutable - all operations return new vectors
- `Norm()` returns zero vector if input has zero length
- `Div()` traps on division by zero
- `Cross()` returns a Vec3 perpendicular to both input vectors (right-hand rule)
- The cross product formula: a × b = (ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx)

### Zia Example

```zia
module Vec3Demo;

bind Viper.Terminal;
bind Viper.Math.Vec3 as V3;
bind Viper.Fmt as Fmt;

func start() {
    var v = V3.New(1.0, 2.0, 3.0);
    Say("Length: " + Fmt.NumFixed(V3.Len(v), 4));             // 3.7417

    var n = V3.Norm(v);
    Say("Normalized: " + Fmt.NumFixed(V3.Len(n), 2));         // 1.00

    var a = V3.New(1.0, 0.0, 0.0);
    var b = V3.New(0.0, 1.0, 0.0);
    var cross = V3.Cross(a, b);
    Say("Cross len: " + Fmt.NumFixed(V3.Len(cross), 2));      // 1.00
}
```

### BASIC Example

```basic
' Create 3D vectors
DIM pos AS OBJECT = Viper.Math.Vec3.New(100.0, 200.0, 50.0)
DIM vel AS OBJECT = Viper.Math.Vec3.New(5.0, -3.0, 2.0)

' Move position by velocity
pos = pos.Add(vel)
PRINT "Position: ("; pos.X; ", "; pos.Y; ", "; pos.Z; ")"

' Calculate distance in 3D
DIM target AS OBJECT = Viper.Math.Vec3.New(150.0, 180.0, 60.0)
DIM dist AS DOUBLE = pos.Dist(target)
PRINT "Distance to target: "; dist

' Normalize to get direction
DIM dir AS OBJECT = vel.Norm()
PRINT "Direction length: "; dir.Len()  ' Should be 1.0

' Cross product for surface normals
DIM edge1 AS OBJECT = Viper.Math.Vec3.New(1.0, 0.0, 0.0)
DIM edge2 AS OBJECT = Viper.Math.Vec3.New(0.0, 1.0, 0.0)
DIM normal AS OBJECT = edge1.Cross(edge2)
PRINT "Normal: ("; normal.X; ", "; normal.Y; ", "; normal.Z; ")"  ' (0, 0, 1)

' Verify cross product is perpendicular
PRINT "Dot with edge1: "; normal.Dot(edge1)  ' 0
PRINT "Dot with edge2: "; normal.Dot(edge2)  ' 0

' Linear interpolation for smooth 3D movement
DIM start AS OBJECT = Viper.Math.Vec3.Zero()
DIM endpoint AS OBJECT = Viper.Math.Vec3.New(100.0, 100.0, 100.0)
DIM midpoint AS OBJECT = start.Lerp(endpoint, 0.5)
PRINT "Midpoint: ("; midpoint.X; ", "; midpoint.Y; ", "; midpoint.Z; ")"  ' (50, 50, 50)
```

---

## Viper.Math.Quaternion

> **Note:** The runtime class name is `Viper.Math.Quat`. Use `Quat.Identity()`, `Quat.FromAxisAngle()`, etc. The name `Quaternion` is used in this documentation as an alias for readability.

Quaternion math for 3D rotations, avoiding gimbal lock. Quaternions represent orientations in 3D space and support
smooth interpolation via SLERP.

**Type:** Instance (obj)
**Constructor:** `Quat.New(w, x, y, z)` or `Quat.Identity()`

### Static Constructors

| Method                        | Signature             | Description                                              |
|-------------------------------|-----------------------|----------------------------------------------------------|
| `New(w, x, y, z)`             | `obj(f64,f64,f64,f64)` | Create quaternion from components                       |
| `Identity()`                  | `obj()`               | Create identity quaternion (1, 0, 0, 0)                 |
| `FromAxisAngle(axis, angle)`  | `obj(obj, f64)`       | Create from a Vec3 axis and angle in radians            |
| `FromEuler(pitch, yaw, roll)` | `obj(f64, f64, f64)`  | Create from Euler angles in radians                     |

### Properties

| Property | Type  | Description              |
|----------|-------|--------------------------|
| `W`      | `f64` | W (scalar) component     |
| `X`      | `f64` | X (vector i) component   |
| `Y`      | `f64` | Y (vector j) component   |
| `Z`      | `f64` | Z (vector k) component   |

### Methods

| Method            | Signature       | Description                                                    |
|-------------------|-----------------|----------------------------------------------------------------|
| `Angle()`         | `f64()`         | Return the rotation angle in radians                           |
| `Axis()`          | `Vec3()`        | Return the normalized rotation axis as a Vec3                  |
| `Conjugate()`     | `obj()`         | Return conjugate (inverse for unit quaternions)                |
| `Dot(other)`      | `f64(obj)`      | Dot product with another quaternion                            |
| `Inverse()`       | `obj()`         | Return the inverse quaternion                                  |
| `Len()`           | `f64()`         | Magnitude of the quaternion                                    |
| `LenSq()`         | `f64()`         | Squared magnitude (avoids sqrt)                                |
| `Lerp(other, t)`  | `obj(obj, f64)` | Linear interpolation between two quaternions                   |
| `Mul(other)`      | `obj(obj)`      | Multiply (compose) two quaternion rotations                    |
| `Norm()`          | `obj()`         | Normalize to unit length                                       |
| `RotateVec3(v)`   | `obj(obj)`      | Rotate a Vec3 by this quaternion, returns Vec3                 |
| `Slerp(other, t)` | `obj(obj, f64)` | Spherical linear interpolation (t=0 returns self)              |
| `ToMat4()`        | `obj()`         | Convert to a 4x4 rotation matrix                               |

### Notes

- Quaternions are immutable — all operations return new quaternions.
- `Mul` composes rotations: `a.Mul(b)` applies rotation `b` then rotation `a`.
- `Slerp` performs smooth interpolation along the shortest arc on the unit sphere.
- `Norm()` returns identity for zero-length quaternions.
- `FromAxisAngle` takes a `Vec3` axis object and a scalar angle in radians.

### Zia Example

```zia
module QuaternionDemo;

bind Viper.Math;
bind Viper.Terminal;

func start() {
    // Identity quaternion
    var id = Quat.Identity();
    SayNum(id.W);  // 1.0

    // From axis-angle (90° around Y)
    var yAxis = Vec3.New(0.0, 1.0, 0.0);
    var q90 = Quat.FromAxisAngle(yAxis, 1.5707963);

    // Rotate a vector
    var v = Vec3.New(1.0, 0.0, 0.0);
    var rotated = q90.RotateVec3(v);
    SayNum(rotated.Z);  // ~-1.0

    // Compose rotations (90° + 90° = 180°)
    var combined = q90.Mul(q90);

    // Interpolate (slerp)
    var halfway = id.Slerp(q90, 0.5);
    SayNum(halfway.Len());  // 1.0

    // Inverse (q * q^-1 = identity)
    var inv = q90.Inverse();
    var check = q90.Mul(inv);
    SayNum(check.W);  // ~1.0

    // Euler angles
    var qe = Quat.FromEuler(0.0, 1.5707963, 0.0);
    SayNum(qe.Angle());  // ~1.5707963
}
```

### BASIC Example

```basic
' Create quaternion from axis-angle (90 degrees around Y axis)
DIM axis AS OBJECT = Viper.Math.Vec3.New(0.0, 1.0, 0.0)
DIM q AS OBJECT = Viper.Math.Quat.FromAxisAngle(axis, Viper.Math.Rad(90.0))

' Rotate a vector
DIM v AS OBJECT = Viper.Math.Vec3.New(1.0, 0.0, 0.0)
DIM rotated AS OBJECT = q.RotateVec3(v)
PRINT "Rotated: ("; rotated.X; ", "; rotated.Y; ", "; rotated.Z; ")"
' Approximately (0, 0, -1) for 90-degree Y rotation

' Compose rotations
DIM axis2 AS OBJECT = Viper.Math.Vec3.New(1.0, 0.0, 0.0)
DIM q2 AS OBJECT = Viper.Math.Quat.FromAxisAngle(axis2, Viper.Math.Rad(45.0))
DIM combined AS OBJECT = q.Mul(q2)

' Smooth interpolation between orientations
DIM halfway AS OBJECT = Viper.Math.Quat.Identity().Slerp(q, 0.5)
```

---

## Viper.Math.Easing

Standard easing functions for smooth animation and interpolation. Each function takes a normalized time value `t` in
[0.0, 1.0] and returns a transformed value.

**Type:** Static utility class

### Methods

| Method           | Signature    | Description                                  |
|------------------|-------------|----------------------------------------------|
| `Linear(t)`      | `Double(Double)` | Linear (no easing)                       |
| `InQuad(t)`      | `Double(Double)` | Quadratic ease in (accelerate)           |
| `OutQuad(t)`     | `Double(Double)` | Quadratic ease out (decelerate)          |
| `InOutQuad(t)`   | `Double(Double)` | Quadratic ease in-out                    |
| `InCubic(t)`     | `Double(Double)` | Cubic ease in                            |
| `OutCubic(t)`    | `Double(Double)` | Cubic ease out                           |
| `InOutCubic(t)`  | `Double(Double)` | Cubic ease in-out                        |
| `InQuart(t)`     | `Double(Double)` | Quartic ease in                          |
| `OutQuart(t)`    | `Double(Double)` | Quartic ease out                         |
| `InOutQuart(t)`  | `Double(Double)` | Quartic ease in-out                      |
| `InSine(t)`      | `Double(Double)` | Sinusoidal ease in                       |
| `OutSine(t)`     | `Double(Double)` | Sinusoidal ease out                      |
| `InOutSine(t)`   | `Double(Double)` | Sinusoidal ease in-out                   |
| `InExpo(t)`      | `Double(Double)` | Exponential ease in                      |
| `OutExpo(t)`     | `Double(Double)` | Exponential ease out                     |
| `InOutExpo(t)`   | `Double(Double)` | Exponential ease in-out                  |
| `InCirc(t)`      | `Double(Double)` | Circular ease in                         |
| `OutCirc(t)`     | `Double(Double)` | Circular ease out                        |
| `InOutCirc(t)`   | `Double(Double)` | Circular ease in-out                     |
| `InBack(t)`      | `Double(Double)` | Back ease in (overshoots start)          |
| `OutBack(t)`     | `Double(Double)` | Back ease out (overshoots end)           |
| `InOutBack(t)`   | `Double(Double)` | Back ease in-out                         |
| `InElastic(t)`   | `Double(Double)` | Elastic ease in (spring-like)            |
| `OutElastic(t)`  | `Double(Double)` | Elastic ease out                         |
| `InOutElastic(t)`| `Double(Double)` | Elastic ease in-out                      |
| `InBounce(t)`    | `Double(Double)` | Bounce ease in                           |
| `OutBounce(t)`   | `Double(Double)` | Bounce ease out                          |
| `InOutBounce(t)` | `Double(Double)` | Bounce ease in-out                       |

### Zia Example

```zia
module EasingDemo;

bind Viper.Math;
bind Viper.Terminal;

func start() {
    // Linear
    SayNum(Easing.Linear(0.0));   // 0.0
    SayNum(Easing.Linear(0.5));   // 0.5
    SayNum(Easing.Linear(1.0));   // 1.0

    // Quadratic
    SayNum(Easing.InQuad(0.5));      // 0.25
    SayNum(Easing.OutQuad(0.5));     // 0.75
    SayNum(Easing.InOutQuad(0.5));   // 0.5

    // Cubic
    SayNum(Easing.InCubic(0.5));     // 0.125
    SayNum(Easing.OutCubic(0.5));    // 0.875

    // Sine
    SayNum(Easing.InSine(0.0));      // 0.0
    SayNum(Easing.OutSine(1.0));     // 1.0

    // Elastic and Bounce
    SayNum(Easing.InElastic(0.0));   // 0.0
    SayNum(Easing.OutBounce(1.0));   // 1.0

    // Back (overshoots slightly)
    SayNum(Easing.InBack(0.0));      // 0.0
    SayNum(Easing.OutBack(1.0));     // 1.0

    // Quart
    SayNum(Easing.InQuart(0.5));     // 0.0625
    SayNum(Easing.OutQuart(0.5));    // 0.9375
}
```

### BASIC Example

```basic
' Smooth animation using easing functions
DIM t AS DOUBLE
FOR i = 0 TO 10
    t = i / 10.0
    DIM eased AS DOUBLE = Viper.Math.Easing.OutCubic(t)
    PRINT "t="; t; " eased="; eased
NEXT

' Use with Lerp for smooth movement
DIM startX AS DOUBLE = 0.0
DIM endX AS DOUBLE = 100.0
DIM progress AS DOUBLE = 0.5
DIM smoothX AS DOUBLE = Viper.Math.Lerp(startX, endX, Viper.Math.Easing.InOutQuad(progress))
```

---

## Viper.Math.Spline

Curve interpolation for smooth paths. Supports Catmull-Rom, Bezier, and linear splines.

**Type:** Instance (obj)

### Static Constructors

| Method                              | Signature                              | Description                                         |
|-------------------------------------|----------------------------------------|-----------------------------------------------------|
| `CatmullRom(points)`               | `Spline(Seq)`                          | Create Catmull-Rom spline from a sequence of Vec2   |
| `Bezier(p0, p1, p2, p3)`           | `Spline(Vec2, Vec2, Vec2, Vec2)`       | Create cubic Bezier curve from 4 control points     |
| `Linear(points)`                    | `Spline(Seq)`                          | Create linear interpolation between points          |

### Properties

| Property     | Type    | Description                     |
|--------------|---------|---------------------------------|
| `PointCount` | Integer | Number of control points        |

### Methods

| Method                           | Signature                        | Description                                              |
|----------------------------------|----------------------------------|----------------------------------------------------------|
| `Eval(t)`                        | `Vec2(Double)`                   | Evaluate position at parameter t (0.0 to 1.0)           |
| `Tangent(t)`                     | `Vec2(Double)`                   | Evaluate tangent vector at parameter t                   |
| `PointAt(index)`                 | `Vec2(Integer)`                  | Get control point at index                               |
| `ArcLength(t0, t1, segments)`    | `Double(Double, Double, Integer)`| Approximate arc length between t0 and t1                 |
| `Sample(count)`                  | `Seq(Integer)`                   | Sample count evenly-spaced points along the spline       |

### Zia Example

```zia
module SplineDemo;

bind Viper.Math;
bind Viper.Terminal;
bind Viper.Collections;

func start() {
    // Linear spline from points
    var pts = Seq.New();
    pts.Push(Vec2.New(0.0, 0.0));
    pts.Push(Vec2.New(100.0, 0.0));
    pts.Push(Vec2.New(100.0, 100.0));

    var lin = Spline.Linear(pts);
    SayInt(Spline.get_PointCount(lin));  // 3

    // Evaluate at t=0, 0.5, 1.0
    var p0 = Spline.Eval(lin, 0.0);
    SayNum(Vec2.get_X(p0));  // 0.0

    var pm = Spline.Eval(lin, 0.5);
    SayNum(Vec2.get_X(pm));  // 100.0

    // Arc length and sampling
    var len = Spline.ArcLength(lin, 0.0, 1.0, 100);
    SayNum(len);  // 200.0

    var samples = Spline.Sample(lin, 5);
    SayInt(Seq.get_Len(samples));  // 5

    // Bezier curve
    var bez = Spline.Bezier(
        Vec2.New(0.0, 0.0),
        Vec2.New(33.0, 100.0),
        Vec2.New(66.0, 100.0),
        Vec2.New(100.0, 0.0)
    );
    var bm = Spline.Eval(bez, 0.5);
    SayNum(Vec2.get_Y(bm));  // 75.0

    // Catmull-Rom
    var cpts = Seq.New();
    cpts.Push(Vec2.New(0.0, 0.0));
    cpts.Push(Vec2.New(50.0, 100.0));
    cpts.Push(Vec2.New(100.0, 50.0));
    cpts.Push(Vec2.New(150.0, 100.0));
    var cr = Spline.CatmullRom(cpts);
    SayInt(Spline.get_PointCount(cr));  // 4
}
```

### BASIC Example

```basic
' Create a Catmull-Rom spline
DIM points AS OBJECT = NEW Viper.Collections.Seq()
points.Push(Viper.Math.Vec2.New(0.0, 0.0))
points.Push(Viper.Math.Vec2.New(50.0, 100.0))
points.Push(Viper.Math.Vec2.New(100.0, 50.0))
points.Push(Viper.Math.Vec2.New(150.0, 100.0))

DIM spline AS OBJECT = Viper.Math.Spline.CatmullRom(points)

' Sample points along the spline
DIM samples AS OBJECT = spline.Sample(20)
FOR i = 0 TO samples.Len - 1
    DIM p AS OBJECT = samples.Get(i)
    PRINT "x="; p.X; " y="; p.Y
NEXT

' Evaluate at specific parameter
DIM midpoint AS OBJECT = spline.Eval(0.5)
PRINT "Mid: ("; midpoint.X; ", "; midpoint.Y; ")"
```

---

## Viper.Math.PerlinNoise

Perlin noise generator for procedural content generation. Produces smooth, continuous pseudo-random values suitable for terrain generation, texture synthesis, and organic-looking randomness.

**Type:** Instance class (requires `New(seed)`)

### Constructor

| Method       | Signature            | Description                                        |
|--------------|----------------------|----------------------------------------------------|
| `New(seed)`  | `PerlinNoise(Integer)` | Create a new Perlin noise generator with a seed  |

### Methods

| Method                               | Signature                               | Description                                                  |
|--------------------------------------|-----------------------------------------|--------------------------------------------------------------|
| `Noise2D(noise, x, y)`              | `Double(Object, Double, Double)`        | Sample 2D Perlin noise at coordinates (x, y)                |
| `Noise3D(noise, x, y, z)`           | `Double(Object, Double, Double, Double)` | Sample 3D Perlin noise at coordinates (x, y, z)            |
| `Octave2D(noise, x, y, oct, pers)`  | `Double(Object, Double, Double, Integer, Double)` | Sample 2D fractal noise with octaves and persistence |
| `Octave3D(noise, x, y, z, oct, pers)` | `Double(Object, Double, Double, Double, Integer, Double)` | Sample 3D fractal noise with octaves and persistence |

### Parameters

| Parameter     | Type    | Description                                                |
|---------------|---------|------------------------------------------------------------|
| `noise`       | Object  | The PerlinNoise instance (passed explicitly)               |
| `x`, `y`, `z` | Double  | Sampling coordinates                                       |
| `oct`         | Integer | Number of octaves (layers of detail)                       |
| `pers`        | Double  | Persistence (amplitude reduction per octave, typically 0.5)|

### Notes

- All methods are called in a static style, passing the noise object as the first parameter
- Output values are in the range [-1.0, 1.0] for single-octave noise
- The same seed always produces the same noise field (deterministic)
- `Octave2D`/`Octave3D` layer multiple frequencies for more natural-looking noise (fractal Brownian motion)
- Higher octave counts add finer detail but increase computation cost
- Persistence controls how much each octave contributes; lower values produce smoother output

### Zia Example

```zia
module PerlinDemo;

bind Viper.Terminal;
bind Viper.Math.PerlinNoise as PerlinNoise;
bind Viper.Fmt as Fmt;

func start() {
    var p = PerlinNoise.New(42);

    var n2d = PerlinNoise.Noise2D(p, 0.5, 0.5);
    Say("Noise2D(0.5, 0.5): " + Fmt.NumFixed(n2d, 4));   // -0.5000

    var n3d = PerlinNoise.Noise3D(p, 1.0, 2.0, 3.0);
    Say("Noise3D(1.0, 2.0, 3.0): " + Fmt.NumFixed(n3d, 4)); // 0.0000

    // Fractal noise with 4 octaves
    var oct = PerlinNoise.Octave2D(p, 0.5, 0.5, 4, 0.5);
    Say("Octave2D: " + Fmt.NumFixed(oct, 4));
}
```

### BASIC Example

```basic
' Create a Perlin noise generator with seed 42
DIM p AS OBJECT = Viper.Math.PerlinNoise.New(42)

' Sample 2D noise
DIM n2d AS DOUBLE = Viper.Math.PerlinNoise.Noise2D(p, 0.5, 0.5)
PRINT "Noise2D(0.5, 0.5): "; n2d   ' Output: -0.5

' Sample 3D noise
DIM n3d AS DOUBLE = Viper.Math.PerlinNoise.Noise3D(p, 1.0, 2.0, 3.0)
PRINT "Noise3D(1.0, 2.0, 3.0): "; n3d   ' Output: 0

' Fractal noise with octaves for terrain-like output
DIM oct AS DOUBLE = Viper.Math.PerlinNoise.Octave2D(p, 0.5, 0.5, 4, 0.5)
PRINT "Octave2D: "; oct

' Generate a simple height map
FOR y = 0 TO 9
    FOR x = 0 TO 9
        DIM h AS DOUBLE = Viper.Math.PerlinNoise.Noise2D(p, x * 0.1, y * 0.1)
        ' Map noise from [-1,1] to [0,255]
        DIM height AS INTEGER = INT((h + 1.0) * 127.5)
    NEXT x
NEXT y
```

### Use Cases

- **Terrain generation:** Generate height maps for landscapes
- **Texture synthesis:** Create natural-looking procedural textures (clouds, marble, wood)
- **Animation:** Add organic movement to objects
- **Game worlds:** Procedurally generate caves, forests, and biomes
- **Particle effects:** Add natural variation to particle systems

---

## Viper.Math.BigInt

Arbitrary-precision integer arithmetic. All methods are static; instance values are opaque objects returned by the
constructors. Pass the bigint object explicitly as the first argument to instance-style methods.

**Type:** Static utility class

### Constructors and Constants

| Method / Property  | Signature         | Description                                    |
|--------------------|-------------------|------------------------------------------------|
| `FromInt(n)`       | `Object(Integer)` | Create a BigInt from a 64-bit integer          |
| `FromStr(s)`       | `Object(String)`  | Create a BigInt from a decimal string          |
| `FromBytes(b)`     | `Object(Bytes)`   | Create a BigInt from little-endian Bytes       |
| `Zero`             | `Object`          | The constant 0                                 |
| `One`              | `Object`          | The constant 1                                 |

### Conversion Methods

| Method             | Signature                | Description                                      |
|--------------------|--------------------------|--------------------------------------------------|
| `ToInt(n)`         | `Integer(Object)`        | Convert to 64-bit integer (traps if out of range)|
| `ToString(n)`      | `String(Object)`         | Convert to decimal string                        |
| `ToStrBase(n, base)` | `String(Object, Integer)` | Convert to string in given base (2–36)        |
| `ToBytes(n)`       | `Bytes(Object)`          | Convert to little-endian Bytes                   |
| `FitsInt(n)`       | `Boolean(Object)`        | True if value fits in a 64-bit signed integer    |

### Arithmetic Methods

| Method           | Signature                    | Description              |
|------------------|------------------------------|--------------------------|
| `Add(a, b)`      | `Object(Object, Object)`     | a + b                    |
| `Sub(a, b)`      | `Object(Object, Object)`     | a − b                    |
| `Mul(a, b)`      | `Object(Object, Object)`     | a × b                    |
| `Div(a, b)`      | `Object(Object, Object)`     | Truncated division a ÷ b |
| `Mod(a, b)`      | `Object(Object, Object)`     | Remainder of a ÷ b       |
| `Neg(n)`         | `Object(Object)`             | Negate: −n               |
| `Abs(n)`         | `Object(Object)`             | Absolute value           |
| `Sqrt(n)`        | `Object(Object)`             | Integer square root (floor) |
| `Pow(n, exp)`    | `Object(Object, Integer)`    | n raised to exp          |
| `PowMod(n, exp, mod)` | `Object(Object, Object, Object)` | Modular exponentiation |
| `Gcd(a, b)`      | `Object(Object, Object)`     | Greatest common divisor  |
| `Lcm(a, b)`      | `Object(Object, Object)`     | Least common multiple    |

### Comparison and Inspection

| Method           | Signature                    | Description                           |
|------------------|------------------------------|---------------------------------------|
| `Cmp(a, b)`      | `Integer(Object, Object)`    | -1 if a < b, 0 if equal, 1 if a > b  |
| `Eq(a, b)`       | `Boolean(Object, Object)`    | True if a equals b                    |
| `IsZero(n)`      | `Boolean(Object)`            | True if n == 0                        |
| `IsNegative(n)`  | `Boolean(Object)`            | True if n < 0                         |
| `Sign(n)`        | `Integer(Object)`            | -1, 0, or 1                           |

### Bitwise Methods

| Method              | Signature                    | Description                           |
|---------------------|------------------------------|---------------------------------------|
| `And(a, b)`         | `Object(Object, Object)`     | Bitwise AND                           |
| `Or(a, b)`          | `Object(Object, Object)`     | Bitwise OR                            |
| `Xor(a, b)`         | `Object(Object, Object)`     | Bitwise XOR                           |
| `Not(n)`            | `Object(Object)`             | Bitwise NOT (one's complement)        |
| `Shl(n, bits)`      | `Object(Object, Integer)`    | Shift left by bits                    |
| `Shr(n, bits)`      | `Object(Object, Integer)`    | Arithmetic shift right by bits        |
| `BitLength(n)`      | `Integer(Object)`            | Number of bits in binary representation |
| `TestBit(n, i)`     | `Boolean(Object, Integer)`   | True if bit i is set                  |
| `SetBit(n, i)`      | `Object(Object, Integer)`    | Return n with bit i set               |
| `ClearBit(n, i)`    | `Object(Object, Integer)`    | Return n with bit i cleared           |

### Notes

- BigInt values are immutable — all operations return new objects.
- `Div(a, b)` traps on division by zero.
- `Sqrt(n)` traps if n is negative.
- `Pow(n, exp)` requires a non-negative integer exponent.
- `ToStrBase` supports bases 2 through 36; digits above 9 use lowercase letters.

### Zia Example

```zia
module BigIntDemo;

bind Viper.Terminal;
bind Viper.Math.BigInt as BigInt;
bind Viper.Fmt as Fmt;

func start() {
    var a = BigInt.FromStr("123456789012345678901234567890");
    var b = BigInt.FromInt(999999999);

    var sum  = BigInt.Add(a, b);
    var prod = BigInt.Mul(b, b);

    Say("Sum: " + BigInt.ToString(sum));
    Say("Product: " + BigInt.ToString(prod));
    Say("FitsInt(b): " + Fmt.Bool(BigInt.FitsInt(b)));  // true
    Say("FitsInt(a): " + Fmt.Bool(BigInt.FitsInt(a)));  // false

    var g = BigInt.Gcd(BigInt.FromInt(48), BigInt.FromInt(36));
    Say("Gcd(48,36): " + BigInt.ToString(g));           // 12

    var pm = BigInt.PowMod(BigInt.FromInt(2), BigInt.FromInt(10), BigInt.FromInt(1000));
    Say("2^10 mod 1000: " + BigInt.ToString(pm));       // 24

    Say("BitLength: " + Fmt.Int(BigInt.BitLength(BigInt.FromInt(255))));  // 8
}
```

### BASIC Example

```basic
' Arbitrary-precision arithmetic
DIM a AS OBJECT = Viper.Math.BigInt.FromStr("999999999999999999999999")
DIM b AS OBJECT = Viper.Math.BigInt.FromInt(2)

DIM doubled AS OBJECT = Viper.Math.BigInt.Mul(a, b)
PRINT Viper.Math.BigInt.ToString(doubled)
' Output: 1999999999999999999999998

' Modular exponentiation (useful in cryptography)
DIM base AS OBJECT = Viper.Math.BigInt.FromInt(65537)
DIM exp  AS OBJECT = Viper.Math.BigInt.FromInt(65537)
DIM mod  AS OBJECT = Viper.Math.BigInt.FromStr("340282366920938463463374607431768211457")
DIM result AS OBJECT = Viper.Math.BigInt.PowMod(base, exp, mod)
PRINT Viper.Math.BigInt.ToString(result)

' GCD and LCM
DIM g AS OBJECT = Viper.Math.BigInt.Gcd(Viper.Math.BigInt.FromInt(48), Viper.Math.BigInt.FromInt(36))
PRINT Viper.Math.BigInt.ToString(g)   ' 12

' Comparison
DIM x AS OBJECT = Viper.Math.BigInt.FromStr("100000000000000000000")
DIM y AS OBJECT = Viper.Math.BigInt.FromStr("99999999999999999999")
PRINT Viper.Math.BigInt.Cmp(x, y)    ' 1 (x > y)

' Bitwise ops
DIM n AS OBJECT = Viper.Math.BigInt.FromInt(255)
PRINT Viper.Math.BigInt.BitLength(n)  ' 8
PRINT Viper.Math.BigInt.TestBit(n, 7) ' True
DIM shifted AS OBJECT = Viper.Math.BigInt.Shl(n, 8)
PRINT Viper.Math.BigInt.ToString(shifted)  ' 65280

' Check if big number fits in 64-bit integer
DIM big AS OBJECT = Viper.Math.BigInt.FromStr("99999999999999999999")
IF NOT Viper.Math.BigInt.FitsInt(big) THEN
    PRINT "Too large for native integer"
END IF
```

---

## Viper.Math.Mat3

3×3 matrix for 2D affine transformations (translation, rotation, scale, shear). All methods are static; matrix
values are opaque objects. Pass the matrix as the first argument to instance-style methods.

**Type:** Static utility class

### Constructors

| Method                        | Signature                                    | Description                                    |
|-------------------------------|----------------------------------------------|------------------------------------------------|
| `New(m00..m22)`               | `Object(f64×9)`                              | Create from 9 row-major floats                 |
| `Identity()`                  | `Object()`                                   | 3×3 identity matrix                            |
| `Zero()`                      | `Object()`                                   | 3×3 zero matrix                               |

### 2D Transform Factories

| Method                | Signature                 | Description                                     |
|-----------------------|---------------------------|-------------------------------------------------|
| `Translate(x, y)`     | `Object(f64, f64)`        | Translation matrix                              |
| `Scale(x, y)`         | `Object(f64, f64)`        | Non-uniform scale matrix                        |
| `ScaleUniform(s)`     | `Object(f64)`             | Uniform scale matrix                            |
| `Rotate(angle)`       | `Object(f64)`             | Counter-clockwise rotation matrix (radians)     |
| `Shear(x, y)`         | `Object(f64, f64)`        | Shear matrix (x shear in X direction, etc.)     |

### Element Access

| Method           | Signature                    | Description                         |
|------------------|------------------------------|-------------------------------------|
| `Get(m, row, col)` | `f64(Object, Integer, Integer)` | Get element at (row, col) 0-indexed |
| `Row(m, i)`      | `Object(Object, Integer)`    | Return row i as a Vec3              |
| `Col(m, i)`      | `Object(Object, Integer)`    | Return column i as a Vec3           |

### Math Operations

| Method              | Signature                    | Description                           |
|---------------------|------------------------------|---------------------------------------|
| `Add(a, b)`         | `Object(Object, Object)`     | Component-wise addition               |
| `Sub(a, b)`         | `Object(Object, Object)`     | Component-wise subtraction            |
| `Mul(a, b)`         | `Object(Object, Object)`     | Matrix multiplication                 |
| `MulScalar(m, s)`   | `Object(Object, f64)`        | Multiply every element by scalar      |
| `Neg(m)`            | `Object(Object)`             | Negate every element                  |
| `Transpose(m)`      | `Object(Object)`             | Transpose rows and columns            |
| `Inverse(m)`        | `Object(Object)`             | Matrix inverse (traps if singular)    |
| `Det(m)`            | `f64(Object)`                | Determinant                           |

### Transform Application

| Method                 | Signature                | Description                                      |
|------------------------|--------------------------|--------------------------------------------------|
| `TransformPoint(m, v)` | `Object(Object, Object)` | Transform a Vec2 point (applies translation)     |
| `TransformVec(m, v)`   | `Object(Object, Object)` | Transform a Vec2 direction (ignores translation) |
| `Eq(a, b, eps)`        | `Boolean(Object, Object, f64)` | True if all elements differ by less than eps |

### Notes

- Matrices are stored in row-major order.
- `TransformPoint` treats the Vec2 as a homogeneous point (w=1); translation is applied.
- `TransformVec` treats the Vec2 as a direction (w=0); translation is not applied.
- `Inverse` traps when the determinant is zero.
- All factory methods return a new matrix; matrices are immutable.

### Zia Example

```zia
module Mat3Demo;

bind Viper.Terminal;
bind Viper.Math.Mat3 as Mat3;
bind Viper.Math.Vec2 as Vec2;
bind Viper.Fmt as Fmt;

func start() {
    // Build a combined 2D transform: scale then translate
    var s  = Mat3.Scale(2.0, 2.0);
    var t  = Mat3.Translate(10.0, 5.0);
    var m  = Mat3.Mul(t, s);   // apply scale first, then translate

    // Transform a point
    var p      = Vec2.New(1.0, 1.0);
    var result = Mat3.TransformPoint(m, p);
    // Scaled: (2, 2) then translated: (12, 7)
    Say("X: " + Fmt.NumFixed(Vec2.get_X(result), 1));  // 12.0
    Say("Y: " + Fmt.NumFixed(Vec2.get_Y(result), 1));  // 7.0

    // Rotation by 90 degrees
    var r   = Mat3.Rotate(1.5707963);
    var dir = Vec2.New(1.0, 0.0);
    var rd  = Mat3.TransformVec(r, dir);
    Say("Rotated X: " + Fmt.NumFixed(Vec2.get_X(rd), 1));  // ~0.0
    Say("Rotated Y: " + Fmt.NumFixed(Vec2.get_Y(rd), 1));  // ~1.0

    // Determinant and inverse
    Say("Det: " + Fmt.NumFixed(Mat3.Det(m), 1));   // 4.0 (scale factor²)
    var inv = Mat3.Inverse(m);
    var chk = Mat3.Mul(m, inv);
    Say("[0,0] should be 1: " + Fmt.NumFixed(Mat3.Get(chk, 0, 0), 4));
}
```

### BASIC Example

```basic
' 2D transform pipeline
DIM scale AS OBJECT = Viper.Math.Mat3.Scale(3.0, 3.0)
DIM rot   AS OBJECT = Viper.Math.Mat3.Rotate(Viper.Math.Rad(45.0))
DIM trans AS OBJECT = Viper.Math.Mat3.Translate(100.0, 50.0)

' Compose: scale → rotate → translate (right to left application order)
DIM m AS OBJECT = Viper.Math.Mat3.Mul(trans, Viper.Math.Mat3.Mul(rot, scale))

' Transform a point
DIM pt AS OBJECT = Viper.Math.Vec2.New(1.0, 0.0)
DIM out AS OBJECT = Viper.Math.Mat3.TransformPoint(m, pt)
PRINT "x="; Viper.Math.Vec2.get_X(out); " y="; Viper.Math.Vec2.get_Y(out)

' Identity check
DIM id AS OBJECT = Viper.Math.Mat3.Identity()
PRINT "Det(I): "; Viper.Math.Mat3.Det(id)   ' 1.0

' Epsilon comparison
DIM a AS OBJECT = Viper.Math.Mat3.Identity()
DIM b AS OBJECT = Viper.Math.Mat3.Mul(a, Viper.Math.Mat3.Identity())
PRINT Viper.Math.Mat3.Eq(a, b, 0.0001)   ' True

' Row and column extraction
DIM row0 AS OBJECT = Viper.Math.Mat3.Row(id, 0)
PRINT Viper.Math.Vec3.get_X(row0)   ' 1.0 (first row of identity)
```

---

## Viper.Math.Mat4

4×4 matrix for 3D transformations, including translation, rotation, scale, and projection. All methods are static;
matrix values are opaque objects. Pass the matrix as the first argument to instance-style methods.

**Type:** Static utility class

### Constructors

| Method         | Signature    | Description                              |
|----------------|--------------|------------------------------------------|
| `New(m00..m33)` | `Object(f64×16)` | Create from 16 row-major floats      |
| `Identity()`   | `Object()`   | 4×4 identity matrix                      |
| `Zero()`       | `Object()`   | 4×4 zero matrix                          |

### 3D Transform Factories

| Method                      | Signature                        | Description                                    |
|-----------------------------|----------------------------------|------------------------------------------------|
| `Translate(x, y, z)`        | `Object(f64, f64, f64)`          | Translation matrix                             |
| `Scale(x, y, z)`            | `Object(f64, f64, f64)`          | Non-uniform scale matrix                       |
| `ScaleUniform(s)`           | `Object(f64)`                    | Uniform scale matrix                           |
| `RotateX(angle)`            | `Object(f64)`                    | Rotation around X axis (radians)               |
| `RotateY(angle)`            | `Object(f64)`                    | Rotation around Y axis (radians)               |
| `RotateZ(angle)`            | `Object(f64)`                    | Rotation around Z axis (radians)               |
| `RotateAxis(axis, angle)`   | `Object(Object, f64)`            | Rotation around an arbitrary Vec3 axis         |

### Projection Factories

| Method                              | Signature                               | Description                              |
|-------------------------------------|-----------------------------------------|------------------------------------------|
| `Perspective(fov, aspect, near, far)` | `Object(f64, f64, f64, f64)`          | Perspective projection matrix (fov in radians, column-major convention) |
| `Ortho(l, r, b, t, near, far)`      | `Object(f64, f64, f64, f64, f64, f64)` | Orthographic projection matrix           |
| `LookAt(eye, center, up)`           | `Object(Object, Object, Object)`        | View matrix from eye/center/up (Vec3)    |

### Element Access and Math

| Method              | Signature                          | Description                           |
|---------------------|------------------------------------|---------------------------------------|
| `Get(m, row, col)`  | `f64(Object, Integer, Integer)`    | Get element at (row, col) 0-indexed   |
| `Add(a, b)`         | `Object(Object, Object)`           | Component-wise addition               |
| `Sub(a, b)`         | `Object(Object, Object)`           | Component-wise subtraction            |
| `Mul(a, b)`         | `Object(Object, Object)`           | Matrix multiplication                 |
| `MulScalar(m, s)`   | `Object(Object, f64)`              | Multiply every element by scalar      |
| `Neg(m)`            | `Object(Object)`                   | Negate every element                  |
| `Transpose(m)`      | `Object(Object)`                   | Transpose rows and columns            |
| `Inverse(m)`        | `Object(Object)`                   | Matrix inverse (traps if singular)    |
| `Det(m)`            | `f64(Object)`                      | Determinant                           |
| `Eq(a, b, eps)`     | `Boolean(Object, Object, f64)`     | True if all elements differ by < eps  |

### Transform Application

| Method                 | Signature                | Description                                       |
|------------------------|--------------------------|---------------------------------------------------|
| `TransformPoint(m, v)` | `Object(Object, Object)` | Transform a Vec3 point (applies translation)      |
| `TransformVec(m, v)`   | `Object(Object, Object)` | Transform a Vec3 direction (ignores translation)  |

### Notes

- Matrices are stored in row-major order.
- `LookAt` expects right-handed coordinate system (OpenGL convention).
- `Perspective` uses the standard OpenGL depth range [-1, 1].
- `RotateAxis` normalizes the axis internally; passing a zero-length axis traps.
- Composing transforms: `Mul(B, A)` applies A first, then B (right-to-left).

### Zia Example

```zia
module Mat4Demo;

bind Viper.Terminal;
bind Viper.Math.Mat4 as Mat4;
bind Viper.Math.Vec3 as Vec3;
bind Viper.Math as Math;
bind Viper.Fmt as Fmt;

func start() {
    // Simple model matrix: scale → rotateY → translate
    var s = Mat4.Scale(2.0, 2.0, 2.0);
    var r = Mat4.RotateY(Math.Rad(45.0));
    var t = Mat4.Translate(0.0, 0.0, -5.0);
    var model = Mat4.Mul(t, Mat4.Mul(r, s));

    // Camera view matrix
    var eye    = Vec3.New(0.0, 3.0, 8.0);
    var center = Vec3.New(0.0, 0.0, 0.0);
    var up     = Vec3.New(0.0, 1.0, 0.0);
    var view   = Mat4.LookAt(eye, center, up);

    // Perspective projection
    var proj = Mat4.Perspective(Math.Rad(60.0), 16.0 / 9.0, 0.1, 1000.0);

    // MVP matrix
    var mvp = Mat4.Mul(proj, Mat4.Mul(view, model));

    // Transform a point
    var pt  = Vec3.New(1.0, 0.0, 0.0);
    var out = Mat4.TransformPoint(mvp, pt);
    Say("Transformed X: " + Fmt.NumFixed(Vec3.get_X(out), 4));

    // Verify identity round-trip
    var id  = Mat4.Identity();
    var inv = Mat4.Inverse(id);
    var chk = Mat4.Mul(id, inv);
    Say("[0,0]: " + Fmt.NumFixed(Mat4.Get(chk, 0, 0), 2));  // 1.00
}
```

### BASIC Example

```basic
' Build a 3D model-view-projection matrix
DIM scale AS OBJECT = Viper.Math.Mat4.Scale(1.0, 1.0, 1.0)
DIM rotY  AS OBJECT = Viper.Math.Mat4.RotateY(Viper.Math.Rad(30.0))
DIM trans AS OBJECT = Viper.Math.Mat4.Translate(0.0, 0.0, -10.0)

DIM model AS OBJECT = Viper.Math.Mat4.Mul(trans, Viper.Math.Mat4.Mul(rotY, scale))

' Camera
DIM eye    AS OBJECT = Viper.Math.Vec3.New(0.0, 5.0, 10.0)
DIM center AS OBJECT = Viper.Math.Vec3.New(0.0, 0.0, 0.0)
DIM up     AS OBJECT = Viper.Math.Vec3.New(0.0, 1.0, 0.0)
DIM view   AS OBJECT = Viper.Math.Mat4.LookAt(eye, center, up)

' Projection
DIM proj AS OBJECT = Viper.Math.Mat4.Perspective(Viper.Math.Rad(60.0), 1.777, 0.1, 1000.0)

' Combined MVP
DIM mvp AS OBJECT = Viper.Math.Mat4.Mul(proj, Viper.Math.Mat4.Mul(view, model))

' Transform a world-space point into clip space
DIM pt  AS OBJECT = Viper.Math.Vec3.New(0.0, 0.0, 0.0)
DIM out AS OBJECT = Viper.Math.Mat4.TransformPoint(mvp, pt)
PRINT "Clip X: "; Viper.Math.Vec3.get_X(out)
PRINT "Clip Y: "; Viper.Math.Vec3.get_Y(out)

' Orthographic projection for 2D/UI overlay
DIM ortho AS OBJECT = Viper.Math.Mat4.Ortho(0.0, 1920.0, 0.0, 1080.0, -1.0, 1.0)
PRINT "Det(ortho): "; Viper.Math.Mat4.Det(ortho)

' Rotate around arbitrary axis
DIM axis    AS OBJECT = Viper.Math.Vec3.New(1.0, 1.0, 0.0)
DIM rotAxis AS OBJECT = Viper.Math.Mat4.RotateAxis(axis, Viper.Math.Rad(45.0))
```

---

## See Also

- [Graphics](graphics.md) - Use `Vec2`, `Vec3`, `Mat3`, `Mat4`, and `Quaternion` with `Canvas` and `Pixels` for 2D/3D graphics
- [Cryptography](crypto.md) - `Rand` for cryptographically secure randomness


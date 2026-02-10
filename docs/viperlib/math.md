# Mathematics

> Mathematical functions, vectors, matrices, bit operations, and random numbers.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Bits](#viperbits)
- [Viper.Math](#vipermath)
- [Viper.Quaternion](#viperquaternion)
- [Viper.Random](#viperrandom)
- [Viper.Vec2](#vipervec2)
- [Viper.Vec3](#vipervec3)

---

## Viper.Bits

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
bind Viper.Bits as Bits;
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
PRINT Viper.Bits.And(a, b)  ' 15 (&H0F)
PRINT Viper.Bits.Or(a, b)   ' 255 (&HFF)
PRINT Viper.Bits.Xor(a, b)  ' 240 (&HF0)

' Shift operations
DIM val AS INTEGER = 1
PRINT Viper.Bits.Shl(val, 4)   ' 16
PRINT Viper.Bits.Shr(16, 2)    ' 4

' Count set bits
DIM mask AS INTEGER = &HFF
PRINT Viper.Bits.Count(mask)   ' 8

' Work with individual bits
DIM flags AS INTEGER = 0
flags = Viper.Bits.Set(flags, 0)    ' Set bit 0
flags = Viper.Bits.Set(flags, 3)    ' Set bit 3
PRINT Viper.Bits.Get(flags, 0)      ' True
PRINT Viper.Bits.Get(flags, 1)      ' False
flags = Viper.Bits.Toggle(flags, 3) ' Toggle bit 3 off
PRINT flags                          ' 1

' Endian conversion
DIM big AS INTEGER = &H0102030405060708
DIM little AS INTEGER = Viper.Bits.Swap(big)
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

## Viper.Random

Random number generation with uniform and distribution-based functions.

**Type:** Static utility class

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
bind Viper.Random as Random;
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
Viper.Random.Seed(12345)

' Random float between 0 and 1
DIM r AS DOUBLE
r = Viper.Random.Next()
PRINT r  ' Output: 0.123... (varies)

' Random integer 0-99
DIM n AS INTEGER
n = Viper.Random.NextInt(100)
PRINT n  ' Output: 0-99 (varies)

' Random integer in range [1, 10] inclusive
DIM x AS INTEGER
x = Viper.Random.Range(1, 10)
PRINT x  ' Output: 1-10 (varies)

' Simulate dice roll (1-6)
DIM die AS INTEGER
die = Viper.Random.Dice(6)
PRINT "You rolled: "; die

' Gaussian distribution (bell curve) with mean=100, stddev=15
DIM iq AS DOUBLE
iq = Viper.Random.Gaussian(100.0, 15.0)
PRINT "IQ Score: "; INT(iq)

' Exponential distribution (for waiting times, etc.)
DIM waitTime AS DOUBLE
waitTime = Viper.Random.Exponential(0.5)  ' mean = 2.0
PRINT "Wait: "; waitTime

' 70% chance of success
IF Viper.Random.Chance(0.7) = 1 THEN
    PRINT "Success!"
ELSE
    PRINT "Failed."
END IF

' Shuffle a sequence
DIM seq AS Viper.Collections.Seq
seq = Viper.Collections.Seq.New()
FOR i = 1 TO 5
    seq.Push(Viper.Box.I64(i))
NEXT i
Viper.Random.Shuffle(seq)  ' Now shuffled: e.g., [3, 1, 5, 2, 4]
```

### Notes

- All random functions use the same LCG (Linear Congruential Generator)
- Sequences are deterministic for a given seed
- `Gaussian` uses the Box-Muller transform for accurate normal distribution
- `Exponential(lambda)` produces values with mean = 1/lambda
- `Range(a, b)` automatically swaps bounds if min > max
- `Shuffle` performs a Fisher-Yates shuffle (O(n) complexity)

---

## Viper.Vec2

2D vector math for positions, directions, velocities, and physics calculations.

**Type:** Instance (obj)
**Constructor:** `Viper.Vec2.New(x, y)` or `Viper.Vec2.Zero()` or `Viper.Vec2.One()`

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
bind Viper.Vec2 as V2;
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
DIM pos AS OBJECT = Viper.Vec2.New(100.0, 200.0)
DIM vel AS OBJECT = Viper.Vec2.New(5.0, -3.0)

' Move position by velocity
pos = pos.Add(vel)
PRINT "Position: ("; pos.X; ", "; pos.Y; ")"

' Calculate distance
DIM target AS OBJECT = Viper.Vec2.New(150.0, 180.0)
DIM dist AS DOUBLE = pos.Dist(target)
PRINT "Distance to target: "; dist

' Normalize to get direction
DIM dir AS OBJECT = vel.Norm()
PRINT "Direction: ("; dir.X; ", "; dir.Y; ")"
PRINT "Direction length: "; dir.Len()  ' Should be 1.0

' Rotate a vector 90 degrees
DIM right AS OBJECT = Viper.Vec2.New(1.0, 0.0)
DIM up AS OBJECT = right.Rotate(3.14159265 / 2.0)
PRINT "Rotated: ("; up.X; ", "; up.Y; ")"  ' (0, 1)

' Linear interpolation for smooth movement
DIM start AS OBJECT = Viper.Vec2.Zero()
DIM endpoint AS OBJECT = Viper.Vec2.New(100.0, 100.0)
DIM midpoint AS OBJECT = start.Lerp(endpoint, 0.5)
PRINT "Midpoint: ("; midpoint.X; ", "; midpoint.Y; ")"  ' (50, 50)

' Dot product to check perpendicularity
DIM a AS OBJECT = Viper.Vec2.New(1.0, 0.0)
DIM b AS OBJECT = Viper.Vec2.New(0.0, 1.0)
IF a.Dot(b) = 0.0 THEN
    PRINT "Vectors are perpendicular"
END IF
```

---

## Viper.Vec3

3D vector math for positions, directions, velocities, and physics calculations in 3D space.

**Type:** Instance (obj)
**Constructor:** `Viper.Vec3.New(x, y, z)` or `Viper.Vec3.Zero()` or `Viper.Vec3.One()`

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
bind Viper.Vec3 as V3;
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
DIM pos AS OBJECT = Viper.Vec3.New(100.0, 200.0, 50.0)
DIM vel AS OBJECT = Viper.Vec3.New(5.0, -3.0, 2.0)

' Move position by velocity
pos = pos.Add(vel)
PRINT "Position: ("; pos.X; ", "; pos.Y; ", "; pos.Z; ")"

' Calculate distance in 3D
DIM target AS OBJECT = Viper.Vec3.New(150.0, 180.0, 60.0)
DIM dist AS DOUBLE = pos.Dist(target)
PRINT "Distance to target: "; dist

' Normalize to get direction
DIM dir AS OBJECT = vel.Norm()
PRINT "Direction length: "; dir.Len()  ' Should be 1.0

' Cross product for surface normals
DIM edge1 AS OBJECT = Viper.Vec3.New(1.0, 0.0, 0.0)
DIM edge2 AS OBJECT = Viper.Vec3.New(0.0, 1.0, 0.0)
DIM normal AS OBJECT = edge1.Cross(edge2)
PRINT "Normal: ("; normal.X; ", "; normal.Y; ", "; normal.Z; ")"  ' (0, 0, 1)

' Verify cross product is perpendicular
PRINT "Dot with edge1: "; normal.Dot(edge1)  ' 0
PRINT "Dot with edge2: "; normal.Dot(edge2)  ' 0

' Linear interpolation for smooth 3D movement
DIM start AS OBJECT = Viper.Vec3.Zero()
DIM endpoint AS OBJECT = Viper.Vec3.New(100.0, 100.0, 100.0)
DIM midpoint AS OBJECT = start.Lerp(endpoint, 0.5)
PRINT "Midpoint: ("; midpoint.X; ", "; midpoint.Y; ", "; midpoint.Z; ")"  ' (50, 50, 50)
```

---

## Viper.Quaternion

Quaternion math for 3D rotations, avoiding gimbal lock. Quaternions represent orientations in 3D space and support
smooth interpolation via SLERP.

**Type:** Instance (obj)
**Constructor:** `Viper.Quaternion.New(w, x, y, z)` or `Viper.Quaternion.Identity()`

### Static Constructors

| Method                  | Signature                   | Description                                              |
|-------------------------|-----------------------------|----------------------------------------------------------|
| `New(w, x, y, z)`      | `obj(f64, f64, f64, f64)`   | Create quaternion from components                        |
| `Identity()`            | `obj()`                     | Create identity quaternion (1, 0, 0, 0)                  |
| `FromAxisAngle(ax, ay, az, angle)` | `obj(f64, f64, f64, f64)` | Create from axis-angle (angle in radians)     |
| `FromEuler(pitch, yaw, roll)` | `obj(f64, f64, f64)`   | Create from Euler angles (radians)                       |

### Properties

| Property | Type  | Description              |
|----------|-------|--------------------------|
| `W`      | `f64` | W (scalar) component     |
| `X`      | `f64` | X (vector i) component   |
| `Y`      | `f64` | Y (vector j) component   |
| `Z`      | `f64` | Z (vector k) component   |

### Methods

| Method                  | Signature            | Description                                                |
|-------------------------|----------------------|------------------------------------------------------------|
| `Mul(other)`            | `obj(obj)`           | Multiply (compose) two quaternion rotations                |
| `Conjugate()`           | `obj()`              | Return conjugate (inverse for unit quaternions)            |
| `Norm()`                | `obj()`              | Normalize to unit length                                   |
| `Len()`                 | `f64()`              | Magnitude of the quaternion                                |
| `LenSq()`              | `f64()`              | Squared magnitude (avoids sqrt)                            |
| `Dot(other)`            | `f64(obj)`           | Dot product with another quaternion                        |
| `Slerp(other, t)`       | `obj(obj, f64)`      | Spherical linear interpolation (t=0 returns self)          |
| `RotateVec3(v)`          | `Vec3(obj)`          | Rotate a Vec3 by this quaternion                           |
| `ToEuler()`             | `Vec3()`             | Convert to Euler angles (pitch, yaw, roll) in radians      |
| `ToAxisAngle()`          | `obj()`              | Convert to (ax, ay, az, angle) representation              |

### Notes

- Quaternions are immutable — all operations return new quaternions.
- `Mul` composes rotations: `a.Mul(b)` applies rotation `b` then rotation `a`.
- `Slerp` performs smooth interpolation along the shortest arc on the unit sphere.
- `Norm()` returns identity for zero-length quaternions.
- Use `FromAxisAngle` for intuitive rotation specification.

### BASIC Example

```basic
' Create quaternion from axis-angle (90 degrees around Y axis)
DIM q AS OBJECT = Viper.Quaternion.FromAxisAngle(0.0, 1.0, 0.0, Viper.Math.Rad(90.0))

' Rotate a vector
DIM v AS OBJECT = Viper.Vec3.New(1.0, 0.0, 0.0)
DIM rotated AS OBJECT = q.RotateVec3(v)
PRINT "Rotated: ("; rotated.X; ", "; rotated.Y; ", "; rotated.Z; ")"
' Approximately (0, 0, -1) for 90-degree Y rotation

' Compose rotations
DIM q2 AS OBJECT = Viper.Quaternion.FromAxisAngle(1.0, 0.0, 0.0, Viper.Math.Rad(45.0))
DIM combined AS OBJECT = q.Mul(q2)

' Smooth interpolation between orientations
DIM halfway AS OBJECT = Viper.Quaternion.Identity().Slerp(q, 0.5)
```

---

## See Also

- [Graphics](graphics.md) - Use `Vec2`, `Vec3`, `Mat3`, `Mat4`, and `Quaternion` with `Canvas` and `Pixels` for 2D/3D graphics
- [Cryptography](crypto.md) - `Rand` for cryptographically secure randomness


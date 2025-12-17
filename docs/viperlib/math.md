# Mathematics

> Mathematical functions, vectors, bit operations, and random numbers.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Math](#vipermath)
- [Viper.Bits](#viperbits)
- [Viper.Random](#viperrandom)
- [Viper.Vec2](#vipervec2)
- [Viper.Vec3](#vipervec3)

---

## Viper.Math

Mathematical functions and constants.

**Type:** Static utility class

### Constants

| Property | Type | Description |
|----------|------|-------------|
| `Pi` | `Double` | π (3.14159265358979...) |
| `E` | `Double` | Euler's number (2.71828182845904...) |
| `Tau` | `Double` | τ = 2π (6.28318530717958...) |

### Basic Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Abs(x)` | `Double(Double)` | Absolute value of a floating-point number |
| `AbsInt(x)` | `Integer(Integer)` | Absolute value of an integer |
| `Sqrt(x)` | `Double(Double)` | Square root |
| `Pow(base, exp)` | `Double(Double, Double)` | Raises base to the power of exp |
| `Exp(x)` | `Double(Double)` | e raised to the power x |
| `Sgn(x)` | `Double(Double)` | Sign of x: -1, 0, or 1 |
| `SgnInt(x)` | `Integer(Integer)` | Sign of integer x: -1, 0, or 1 |

### Trigonometric Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sin(x)` | `Double(Double)` | Sine (radians) |
| `Cos(x)` | `Double(Double)` | Cosine (radians) |
| `Tan(x)` | `Double(Double)` | Tangent (radians) |
| `Atan(x)` | `Double(Double)` | Arctangent (returns radians) |
| `Atan2(y, x)` | `Double(Double, Double)` | Arctangent of y/x (returns radians, respects quadrant) |
| `Asin(x)` | `Double(Double)` | Arc sine (returns radians) |
| `Acos(x)` | `Double(Double)` | Arc cosine (returns radians) |

### Hyperbolic Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sinh(x)` | `Double(Double)` | Hyperbolic sine |
| `Cosh(x)` | `Double(Double)` | Hyperbolic cosine |
| `Tanh(x)` | `Double(Double)` | Hyperbolic tangent |

### Logarithmic Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Log(x)` | `Double(Double)` | Natural logarithm (base e) |
| `Log10(x)` | `Double(Double)` | Base-10 logarithm |
| `Log2(x)` | `Double(Double)` | Base-2 logarithm |

### Rounding Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Floor(x)` | `Double(Double)` | Largest integer less than or equal to x |
| `Ceil(x)` | `Double(Double)` | Smallest integer greater than or equal to x |
| `Round(x)` | `Double(Double)` | Round to nearest integer |
| `Trunc(x)` | `Double(Double)` | Truncate toward zero |

### Min/Max Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Min(a, b)` | `Double(Double, Double)` | Smaller of two floating-point values |
| `Max(a, b)` | `Double(Double, Double)` | Larger of two floating-point values |
| `MinInt(a, b)` | `Integer(Integer, Integer)` | Smaller of two integers |
| `MaxInt(a, b)` | `Integer(Integer, Integer)` | Larger of two integers |
| `Clamp(val, lo, hi)` | `Double(Double, Double, Double)` | Constrain value to range [lo, hi] |
| `ClampInt(val, lo, hi)` | `Integer(Integer, Integer, Integer)` | Constrain integer to range [lo, hi] |

### Utility Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `FMod(x, y)` | `Double(Double, Double)` | Floating-point remainder of x/y |
| `Lerp(a, b, t)` | `Double(Double, Double, Double)` | Linear interpolation: a + t*(b-a) |
| `Wrap(val, lo, hi)` | `Double(Double, Double, Double)` | Wrap value to range [lo, hi) |
| `WrapInt(val, lo, hi)` | `Integer(Integer, Integer, Integer)` | Wrap integer to range [lo, hi) |
| `Hypot(x, y)` | `Double(Double, Double)` | Hypotenuse: sqrt(x² + y²) |

### Angle Conversion

| Method | Signature | Description |
|--------|-----------|-------------|
| `Deg(radians)` | `Double(Double)` | Convert radians to degrees |
| `Rad(degrees)` | `Double(Double)` | Convert degrees to radians |

### Example

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

## Viper.Terminal

---

## Viper.Bits

Bit manipulation utilities for working with 64-bit integers at the bit level.

**Type:** Static (no instantiation required)

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `And(a, b)` | `i64(i64, i64)` | Bitwise AND |
| `Or(a, b)` | `i64(i64, i64)` | Bitwise OR |
| `Xor(a, b)` | `i64(i64, i64)` | Bitwise XOR |
| `Not(val)` | `i64(i64)` | Bitwise NOT (complement) |
| `Shl(val, count)` | `i64(i64, i64)` | Logical shift left |
| `Shr(val, count)` | `i64(i64, i64)` | Arithmetic shift right (sign-extended) |
| `Ushr(val, count)` | `i64(i64, i64)` | Logical shift right (zero-fill) |
| `Rotl(val, count)` | `i64(i64, i64)` | Rotate left |
| `Rotr(val, count)` | `i64(i64, i64)` | Rotate right |
| `Count(val)` | `i64(i64)` | Population count (number of 1 bits) |
| `LeadZ(val)` | `i64(i64)` | Count leading zeros |
| `TrailZ(val)` | `i64(i64)` | Count trailing zeros |
| `Flip(val)` | `i64(i64)` | Reverse all 64 bits |
| `Swap(val)` | `i64(i64)` | Byte swap (endian swap) |
| `Get(val, bit)` | `i1(i64, i64)` | Get bit at position (0-63) |
| `Set(val, bit)` | `i64(i64, i64)` | Set bit at position |
| `Clear(val, bit)` | `i64(i64, i64)` | Clear bit at position |
| `Toggle(val, bit)` | `i64(i64, i64)` | Toggle bit at position |

### Method Details

#### Shift Operations

- **Shl** — Logical shift left. Shifts bits left, filling with zeros on the right.
- **Shr** — Arithmetic shift right. Shifts bits right, preserving the sign bit (sign-extended).
- **Ushr** — Logical shift right. Shifts bits right, filling with zeros on the left.

Shift counts are clamped: negative counts or counts >= 64 return 0 (for Shl/Ushr) or the sign bit extended (for Shr with negative values).

#### Rotate Operations

- **Rotl** — Rotate left. Bits shifted out on the left wrap around to the right.
- **Rotr** — Rotate right. Bits shifted out on the right wrap around to the left.

Rotate counts are normalized to 0-63 (count MOD 64).

#### Bit Counting

- **Count** — Population count (popcount). Returns the number of 1 bits.
- **LeadZ** — Count leading zeros. Returns 64 for zero, 0 for negative values.
- **TrailZ** — Count trailing zeros. Returns 64 for zero.

#### Single Bit Operations

All single-bit operations accept bit positions 0-63. Out-of-range positions return the input unchanged (for Set/Clear/Toggle) or false (for Get).

### Example

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

## Viper.Collections.List

---

## Viper.Random

Random number generation.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Seed(value)` | `Void(Integer)` | Seeds the random number generator |
| `Next()` | `Double()` | Returns a random double in the range [0.0, 1.0) |
| `NextInt(max)` | `Integer(Integer)` | Returns a random integer in the range [0, max) |

### Example

```basic
' Seed with current time for different sequences each run
Viper.Random.Seed(12345)

' Random float between 0 and 1
DIM r AS DOUBLE
r = Viper.Random.Next()
PRINT r  ' Output: 0.123... (varies)

' Random integer 0-99
DIM n AS INTEGER
n = Viper.Random.NextInt(100)
PRINT n  ' Output: 0-99 (varies)

' Simulate dice roll (1-6)
DIM die AS INTEGER
die = Viper.Random.NextInt(6) + 1
PRINT "You rolled: "; die
```

---

## Viper.Environment

---

## Viper.Vec2

2D vector math for positions, directions, velocities, and physics calculations.

**Type:** Instance (obj)
**Constructor:** `Viper.Vec2.New(x, y)` or `Viper.Vec2.Zero()` or `Viper.Vec2.One()`

### Static Constructors

| Method | Signature | Description |
|--------|-----------|-------------|
| `New(x, y)` | `obj(f64, f64)` | Create a new vector with given components |
| `Zero()` | `obj()` | Create a vector at origin (0, 0) |
| `One()` | `obj()` | Create a vector (1, 1) |

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `X` | `f64` | X component (read-only) |
| `Y` | `f64` | Y component (read-only) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(other)` | `obj(obj)` | Add two vectors: self + other |
| `Sub(other)` | `obj(obj)` | Subtract vectors: self - other |
| `Mul(scalar)` | `obj(f64)` | Multiply by scalar: self * s |
| `Div(scalar)` | `obj(f64)` | Divide by scalar: self / s |
| `Neg()` | `obj()` | Negate vector: -self |
| `Dot(other)` | `f64(obj)` | Dot product of two vectors |
| `Cross(other)` | `f64(obj)` | 2D cross product (scalar z-component) |
| `Len()` | `f64()` | Length (magnitude) of vector |
| `LenSq()` | `f64()` | Squared length (avoids sqrt) |
| `Norm()` | `obj()` | Normalize to unit length |
| `Dist(other)` | `f64(obj)` | Distance to another point |
| `Lerp(other, t)` | `obj(obj, f64)` | Linear interpolation (t=0 returns self, t=1 returns other) |
| `Angle()` | `f64()` | Angle in radians (atan2(y, x)) |
| `Rotate(angle)` | `obj(f64)` | Rotate by angle in radians |

### Notes

- Vectors are immutable - all operations return new vectors
- `Norm()` returns zero vector if input has zero length
- `Div()` traps on division by zero
- `Cross()` returns the scalar z-component of the 3D cross product (treating 2D vectors as 3D with z=0)
- Angles are in radians; use `Viper.Math.Rad()` and `Viper.Math.Deg()` for conversion

### Example

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

---

## Viper.Vec3

3D vector math for positions, directions, velocities, and physics calculations in 3D space.

**Type:** Instance (obj)
**Constructor:** `Viper.Vec3.New(x, y, z)` or `Viper.Vec3.Zero()` or `Viper.Vec3.One()`

### Static Constructors

| Method | Signature | Description |
|--------|-----------|-------------|
| `New(x, y, z)` | `obj(f64, f64, f64)` | Create a new vector with given components |
| `Zero()` | `obj()` | Create a vector at origin (0, 0, 0) |
| `One()` | `obj()` | Create a vector (1, 1, 1) |

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `X` | `f64` | X component (read-only) |
| `Y` | `f64` | Y component (read-only) |
| `Z` | `f64` | Z component (read-only) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(other)` | `obj(obj)` | Add two vectors: self + other |
| `Sub(other)` | `obj(obj)` | Subtract vectors: self - other |
| `Mul(scalar)` | `obj(f64)` | Multiply by scalar: self * s |
| `Div(scalar)` | `obj(f64)` | Divide by scalar: self / s |
| `Neg()` | `obj()` | Negate vector: -self |
| `Dot(other)` | `f64(obj)` | Dot product of two vectors |
| `Cross(other)` | `obj(obj)` | Cross product (returns Vec3) |
| `Len()` | `f64()` | Length (magnitude) of vector |
| `LenSq()` | `f64()` | Squared length (avoids sqrt) |
| `Norm()` | `obj()` | Normalize to unit length |
| `Dist(other)` | `f64(obj)` | Distance to another point |
| `Lerp(other, t)` | `obj(obj, f64)` | Linear interpolation (t=0 returns self, t=1 returns other) |

### Notes

- Vectors are immutable - all operations return new vectors
- `Norm()` returns zero vector if input has zero length
- `Div()` traps on division by zero
- `Cross()` returns a Vec3 perpendicular to both input vectors (right-hand rule)
- The cross product formula: a × b = (ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx)

### Example

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

## Viper.Diagnostics.Assert


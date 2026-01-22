# Mathematics

> Mathematical functions, vectors, matrices, bit operations, and random numbers.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Math.BigInt](#vipermathbigint)
- [Viper.Bits](#viperbits)
- [Viper.Math](#vipermath)
- [Viper.Math.Mat3](#vipermathmat3)
- [Viper.Math.Mat4](#vipermathmat4)
- [Viper.Random](#viperrandom)
- [Viper.Vec2](#vipervec2)
- [Viper.Vec3](#vipervec3)

---

## Viper.Math.BigInt

Arbitrary precision integers for calculations that exceed 64-bit limits.

**Type:** Instance (obj)
**Constructor:** `Viper.Math.BigInt.FromInt(value)` or `Viper.Math.BigInt.FromStr(str)`

### Static Constructors

| Method           | Signature   | Description                            |
|------------------|-------------|----------------------------------------|
| `FromInt(value)` | `obj(i64)`  | Create BigInt from 64-bit integer      |
| `FromStr(str)`   | `obj(str)`  | Parse from string (decimal, hex 0x, binary 0b, octal 0o) |
| `FromBytes(b)`   | `obj(obj)`  | Create from big-endian two's complement bytes |
| `get_Zero`       | `obj()`     | Get BigInt representing zero           |
| `get_One`        | `obj()`     | Get BigInt representing one            |

### Conversion Methods

| Method            | Signature       | Description                                |
|-------------------|-----------------|--------------------------------------------|
| `ToInt()`         | `i64(obj)`      | Convert to 64-bit integer (truncates)      |
| `ToString()`      | `str(obj)`      | Convert to decimal string                  |
| `ToStrBase(base)` | `str(obj,i64)`  | Convert to string in base 2-36             |
| `ToBytes()`       | `obj(obj)`      | Convert to big-endian two's complement     |
| `FitsInt()`       | `i1(obj)`       | True if value fits in 64-bit integer       |

### Arithmetic Methods

| Method        | Signature       | Description                    |
|---------------|-----------------|--------------------------------|
| `Add(other)`  | `obj(obj,obj)`  | Addition: self + other         |
| `Sub(other)`  | `obj(obj,obj)`  | Subtraction: self - other      |
| `Mul(other)`  | `obj(obj,obj)`  | Multiplication: self * other   |
| `Div(other)`  | `obj(obj,obj)`  | Division: self / other         |
| `Mod(other)`  | `obj(obj,obj)`  | Remainder: self % other        |
| `Neg()`       | `obj(obj)`      | Negation: -self                |
| `Abs()`       | `obj(obj)`      | Absolute value                 |

### Comparison Methods

| Method         | Signature       | Description                           |
|----------------|-----------------|---------------------------------------|
| `Cmp(other)`   | `i64(obj,obj)`  | Compare: -1 if <, 0 if ==, 1 if >     |
| `Eq(other)`    | `i1(obj,obj)`   | True if equal                         |
| `IsZero()`     | `i1(obj)`       | True if value is zero                 |
| `IsNegative()` | `i1(obj)`       | True if value is negative             |
| `Sign()`       | `i64(obj)`      | Sign: -1, 0, or 1                     |

### Bitwise Methods

| Method        | Signature       | Description                    |
|---------------|-----------------|--------------------------------|
| `And(other)`  | `obj(obj,obj)`  | Bitwise AND                    |
| `Or(other)`   | `obj(obj,obj)`  | Bitwise OR                     |
| `Xor(other)`  | `obj(obj,obj)`  | Bitwise XOR                    |
| `Not()`       | `obj(obj)`      | Bitwise NOT (two's complement) |
| `Shl(n)`      | `obj(obj,i64)`  | Left shift by n bits           |
| `Shr(n)`      | `obj(obj,i64)`  | Arithmetic right shift         |

### Advanced Methods

| Method            | Signature           | Description                        |
|-------------------|---------------------|------------------------------------|
| `Pow(n)`          | `obj(obj,i64)`      | Raise to power n                   |
| `PowMod(n,m)`     | `obj(obj,obj,obj)`  | Modular exponentiation: self^n mod m |
| `Gcd(other)`      | `obj(obj,obj)`      | Greatest common divisor            |
| `Lcm(other)`      | `obj(obj,obj)`      | Least common multiple              |
| `BitLength()`     | `i64(obj)`          | Number of bits needed              |
| `TestBit(n)`      | `i1(obj,i64)`       | Test bit at position n             |
| `SetBit(n)`       | `obj(obj,i64)`      | Set bit at position n              |
| `ClearBit(n)`     | `obj(obj,i64)`      | Clear bit at position n            |
| `Sqrt()`          | `obj(obj)`          | Integer square root (floor)        |

### Example

```basic
' Create BigInts
DIM a AS OBJECT = Viper.Math.BigInt.FromStr("123456789012345678901234567890")
DIM b AS OBJECT = Viper.Math.BigInt.FromInt(12345)

' Arithmetic
DIM sum AS OBJECT = Viper.Math.BigInt.Add(a, b)
DIM product AS OBJECT = Viper.Math.BigInt.Mul(a, b)
PRINT Viper.Math.BigInt.ToString(sum)

' Check if result fits in regular integer
IF Viper.Math.BigInt.FitsInt(b) THEN
    DIM n AS INTEGER = Viper.Math.BigInt.ToInt(b)
    PRINT n  ' 12345
END IF

' Compute factorial of 100
DIM factorial AS OBJECT = Viper.Math.BigInt.get_One()
FOR i = 2 TO 100
    DIM bi AS OBJECT = Viper.Math.BigInt.FromInt(i)
    factorial = Viper.Math.BigInt.Mul(factorial, bi)
NEXT i
PRINT Viper.Math.BigInt.ToString(factorial)

' RSA-style modular exponentiation
DIM base AS OBJECT = Viper.Math.BigInt.FromInt(2)
DIM exp AS OBJECT = Viper.Math.BigInt.FromInt(1000)
DIM modulus AS OBJECT = Viper.Math.BigInt.FromStr("1000000007")
DIM result AS OBJECT = Viper.Math.BigInt.PowMod(base, exp, modulus)
```

### Use Cases

- **Cryptography:** RSA, Diffie-Hellman, large prime operations
- **Exact arithmetic:** Financial calculations without rounding errors
- **Number theory:** Factorials, Fibonacci, combinatorics
- **Scientific computing:** Large integer sequences

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

### Example

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

## Viper.Math.Mat3

3×3 matrix for 2D transformations including translation, rotation, scaling, and shearing.

**Type:** Instance (obj)
**Constructor:** `Viper.Mat3.New(m00, m01, m02, m10, m11, m12, m20, m21, m22)` or `Viper.Mat3.Identity()`

### Static Constructors

| Method              | Signature                        | Description                              |
|---------------------|----------------------------------|------------------------------------------|
| `New(...)`          | `obj(f64×9)`                     | Create matrix from 9 elements (row-major)|
| `Identity()`        | `obj()`                          | Create identity matrix                   |
| `Zero()`            | `obj()`                          | Create zero matrix                       |
| `Translate(tx, ty)` | `obj(f64, f64)`                  | Create translation matrix                |
| `Scale(sx, sy)`     | `obj(f64, f64)`                  | Create scaling matrix                    |
| `ScaleUniform(s)`   | `obj(f64)`                       | Create uniform scaling matrix            |
| `Rotate(angle)`     | `obj(f64)`                       | Create rotation matrix (radians)         |
| `Shear(sx, sy)`     | `obj(f64, f64)`                  | Create shear matrix                      |

### Element Access

| Method        | Signature           | Description                           |
|---------------|---------------------|---------------------------------------|
| `Get(r, c)`   | `f64(obj, i64, i64)`| Get element at row r, column c (0-2)  |
| `Row(r)`      | `obj(obj, i64)`     | Get row r as Vec3 (0-2)               |
| `Col(c)`      | `obj(obj, i64)`     | Get column c as Vec3 (0-2)            |

### Arithmetic Methods

| Method          | Signature           | Description                        |
|-----------------|---------------------|------------------------------------|
| `Add(other)`    | `obj(obj, obj)`     | Matrix addition                    |
| `Sub(other)`    | `obj(obj, obj)`     | Matrix subtraction                 |
| `Mul(other)`    | `obj(obj, obj)`     | Matrix multiplication              |
| `MulScalar(s)`  | `obj(obj, f64)`     | Scalar multiplication              |
| `Neg()`         | `obj(obj)`          | Negate all elements                |

### Transform Methods

| Method              | Signature       | Description                                 |
|---------------------|-----------------|---------------------------------------------|
| `TransformPoint(v)` | `obj(obj, obj)` | Transform a Vec2 point (with translation)   |
| `TransformVec(v)`   | `obj(obj, obj)` | Transform a Vec2 vector (without translation)|

### Matrix Operations

| Method        | Signature           | Description                                   |
|---------------|---------------------|-----------------------------------------------|
| `Transpose()` | `obj(obj)`          | Return transposed matrix                      |
| `Det()`       | `f64(obj)`          | Calculate determinant                         |
| `Inverse()`   | `obj(obj)`          | Return inverse matrix (traps if singular)     |
| `Eq(other,e)` | `i1(obj, obj, f64)` | Compare with epsilon tolerance                |

### Example

```basic
' Create transformation matrices
DIM translate AS OBJECT = Viper.Mat3.Translate(100.0, 50.0)
DIM rotate AS OBJECT = Viper.Mat3.Rotate(Viper.Math.Rad(45))
DIM scale AS OBJECT = Viper.Mat3.Scale(2.0, 2.0)

' Combine transformations (order matters!)
DIM transform AS OBJECT = Viper.Mat3.Mul(translate, Viper.Mat3.Mul(rotate, scale))

' Transform a point
DIM point AS OBJECT = Viper.Vec2.New(10.0, 20.0)
DIM transformed AS OBJECT = Viper.Mat3.TransformPoint(transform, point)
PRINT "Transformed: ("; transformed.X; ", "; transformed.Y; ")"

' Get matrix element
PRINT Viper.Mat3.Get(transform, 0, 0)  ' Element at row 0, col 0

' Invert the transformation
DIM inverse AS OBJECT = Viper.Mat3.Inverse(transform)
DIM original AS OBJECT = Viper.Mat3.TransformPoint(inverse, transformed)
```

### Use Cases

- **2D graphics:** Transforming sprites, shapes, and textures
- **UI layout:** Positioning and scaling UI elements
- **Animation:** Interpolating between transformation states
- **Physics:** Coordinate system conversions

---

## Viper.Math.Mat4

4×4 matrix for 3D transformations including translation, rotation, scaling, and projection.

**Type:** Instance (obj)
**Constructor:** `Viper.Mat4.New(m00...m33)` or `Viper.Mat4.Identity()`

### Static Constructors

| Method                 | Signature                        | Description                               |
|------------------------|----------------------------------|-------------------------------------------|
| `New(...)`             | `obj(f64×16)`                    | Create matrix from 16 elements (row-major)|
| `Identity()`           | `obj()`                          | Create identity matrix                    |
| `Zero()`               | `obj()`                          | Create zero matrix                        |
| `Translate(x, y, z)`   | `obj(f64, f64, f64)`             | Create translation matrix                 |
| `Scale(sx, sy, sz)`    | `obj(f64, f64, f64)`             | Create scaling matrix                     |
| `ScaleUniform(s)`      | `obj(f64)`                       | Create uniform scaling matrix             |
| `RotateX(angle)`       | `obj(f64)`                       | Create X-axis rotation matrix (radians)   |
| `RotateY(angle)`       | `obj(f64)`                       | Create Y-axis rotation matrix (radians)   |
| `RotateZ(angle)`       | `obj(f64)`                       | Create Z-axis rotation matrix (radians)   |
| `RotateAxis(axis, a)`  | `obj(obj, f64)`                  | Create rotation around Vec3 axis          |

### Projection Constructors

| Method                              | Signature                              | Description                               |
|-------------------------------------|----------------------------------------|-------------------------------------------|
| `Perspective(fov, aspect, near, far)` | `obj(f64, f64, f64, f64)`            | Create perspective projection             |
| `Ortho(l, r, b, t, near, far)`      | `obj(f64, f64, f64, f64, f64, f64)`   | Create orthographic projection            |
| `LookAt(eye, target, up)`           | `obj(obj, obj, obj)`                  | Create view matrix from eye position      |

### Element Access

| Method      | Signature             | Description                          |
|-------------|-----------------------|--------------------------------------|
| `Get(r, c)` | `f64(obj, i64, i64)`  | Get element at row r, column c (0-3) |

### Arithmetic Methods

| Method          | Signature           | Description             |
|-----------------|---------------------|-------------------------|
| `Add(other)`    | `obj(obj, obj)`     | Matrix addition         |
| `Sub(other)`    | `obj(obj, obj)`     | Matrix subtraction      |
| `Mul(other)`    | `obj(obj, obj)`     | Matrix multiplication   |
| `MulScalar(s)`  | `obj(obj, f64)`     | Scalar multiplication   |
| `Neg()`         | `obj(obj)`          | Negate all elements     |

### Transform Methods

| Method              | Signature       | Description                                 |
|---------------------|-----------------|---------------------------------------------|
| `TransformPoint(v)` | `obj(obj, obj)` | Transform a Vec3 point (with translation)   |
| `TransformVec(v)`   | `obj(obj, obj)` | Transform a Vec3 vector (without translation)|

### Matrix Operations

| Method        | Signature           | Description                               |
|---------------|---------------------|-------------------------------------------|
| `Transpose()` | `obj(obj)`          | Return transposed matrix                  |
| `Det()`       | `f64(obj)`          | Calculate determinant                     |
| `Inverse()`   | `obj(obj)`          | Return inverse matrix (traps if singular) |
| `Eq(other,e)` | `i1(obj, obj, f64)` | Compare with epsilon tolerance            |

### Example

```basic
' Set up a 3D camera
DIM eye AS OBJECT = Viper.Vec3.New(0.0, 5.0, 10.0)
DIM target AS OBJECT = Viper.Vec3.Zero()
DIM up AS OBJECT = Viper.Vec3.New(0.0, 1.0, 0.0)
DIM view AS OBJECT = Viper.Mat4.LookAt(eye, target, up)

' Create perspective projection (45 degree FOV, 16:9 aspect)
DIM projection AS OBJECT = Viper.Mat4.Perspective(Viper.Math.Rad(45), 16.0/9.0, 0.1, 100.0)

' Create model transformation
DIM model AS OBJECT = Viper.Mat4.Mul(
    Viper.Mat4.Translate(0.0, 1.0, 0.0),
    Viper.Mat4.RotateY(Viper.Math.Rad(30))
)

' Combine: MVP = projection * view * model
DIM mvp AS OBJECT = Viper.Mat4.Mul(projection, Viper.Mat4.Mul(view, model))

' Transform a vertex
DIM vertex AS OBJECT = Viper.Vec3.New(1.0, 0.0, 0.0)
DIM transformed AS OBJECT = Viper.Mat4.TransformPoint(mvp, vertex)

' Orthographic projection for 2D/UI
DIM ortho AS OBJECT = Viper.Mat4.Ortho(0, 800, 600, 0, -1, 1)
```

### Projection Parameters

**Perspective:**
- `fov` — Field of view in radians (vertical)
- `aspect` — Width/height aspect ratio
- `near` — Near clipping plane distance (must be > 0)
- `far` — Far clipping plane distance (must be > near)

**Orthographic:**
- `left`, `right` — Left and right clipping planes
- `bottom`, `top` — Bottom and top clipping planes
- `near`, `far` — Near and far clipping planes

**LookAt:**
- `eye` — Camera position (Vec3)
- `target` — Point to look at (Vec3)
- `up` — Up direction (Vec3, typically (0,1,0))

### Use Cases

- **3D graphics:** Model-view-projection transformations
- **Game engines:** Camera and object positioning
- **Physics simulation:** Coordinate transformations
- **CAD/modeling:** 3D object manipulation

---

## See Also

- [Graphics](graphics.md) - Use `Vec2`, `Vec3`, `Mat3`, and `Mat4` with `Canvas` and `Pixels` for 2D/3D graphics
- [Cryptography](crypto.md) - `Rand` for cryptographically secure randomness


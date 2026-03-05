' =============================================================================
' API Audit: Viper.Math.BigInt - Arbitrary Precision Integers (BASIC)
' =============================================================================
' Tests: FromInt, FromStr, Zero, One, Add, Sub, Mul, Div, Mod, Neg, Abs,
'        Cmp, Eq, IsZero, IsNegative, Sign, And, Or, Xor, Not, Shl, Shr,
'        Pow, Gcd, Lcm, BitLength, TestBit, SetBit, ClearBit, Sqrt,
'        ToInt, ToString, ToStrBase, FitsInt
' =============================================================================

PRINT "=== API Audit: Viper.Math.BigInt ==="

' --- Factory: FromInt ---
PRINT "--- FromInt ---"
DIM a AS OBJECT
a = Viper.Math.BigInt.FromInt(42)
PRINT "BigInt.FromInt(42) ToString: "; Viper.Math.BigInt.ToString(a)
DIM b AS OBJECT
b = Viper.Math.BigInt.FromInt(-100)
PRINT "BigInt.FromInt(-100) ToString: "; Viper.Math.BigInt.ToString(b)
DIM c AS OBJECT
c = Viper.Math.BigInt.FromInt(0)
PRINT "BigInt.FromInt(0) ToString: "; Viper.Math.BigInt.ToString(c)

' --- Factory: FromStr ---
PRINT "--- FromStr ---"
DIM d AS OBJECT
d = Viper.Math.BigInt.FromStr("123456789012345678901234567890")
PRINT "BigInt.FromStr(large) ToString: "; Viper.Math.BigInt.ToString(d)
DIM e AS OBJECT
e = Viper.Math.BigInt.FromStr("-999")
PRINT "BigInt.FromStr(-999) ToString: "; Viper.Math.BigInt.ToString(e)

' --- Factory: Zero / One ---
PRINT "--- Zero / One ---"
DIM z AS OBJECT
z = Viper.Math.BigInt.Zero
PRINT "BigInt.Zero ToString: "; Viper.Math.BigInt.ToString(z)
DIM bi_one AS OBJECT
bi_one = Viper.Math.BigInt.One
PRINT "BigInt.One ToString: "; Viper.Math.BigInt.ToString(bi_one)

' --- Add ---
PRINT "--- Add ---"
DIM sum1 AS OBJECT
sum1 = Viper.Math.BigInt.Add(a, b)
PRINT "Add(42, -100) ToString: "; Viper.Math.BigInt.ToString(sum1)
DIM sum2 AS OBJECT
sum2 = Viper.Math.BigInt.Add(a, bi_one)
PRINT "Add(42, 1) ToString: "; Viper.Math.BigInt.ToString(sum2)

' --- Sub ---
PRINT "--- Sub ---"
DIM diff AS OBJECT
diff = Viper.Math.BigInt.Sub(a, b)
PRINT "Sub(42, -100) ToString: "; Viper.Math.BigInt.ToString(diff)
DIM diff2 AS OBJECT
diff2 = Viper.Math.BigInt.Sub(a, a)
PRINT "Sub(42, 42) ToString: "; Viper.Math.BigInt.ToString(diff2)

' --- Mul ---
PRINT "--- Mul ---"
DIM prod AS OBJECT
prod = Viper.Math.BigInt.Mul(a, b)
PRINT "Mul(42, -100) ToString: "; Viper.Math.BigInt.ToString(prod)
DIM prod2 AS OBJECT
prod2 = Viper.Math.BigInt.Mul(a, z)
PRINT "Mul(42, 0) ToString: "; Viper.Math.BigInt.ToString(prod2)

' --- Div ---
PRINT "--- Div ---"
DIM ten AS OBJECT
ten = Viper.Math.BigInt.FromInt(10)
DIM quot AS OBJECT
quot = Viper.Math.BigInt.Div(a, ten)
PRINT "Div(42, 10) ToString: "; Viper.Math.BigInt.ToString(quot)
DIM quot2 AS OBJECT
quot2 = Viper.Math.BigInt.Div(b, ten)
PRINT "Div(-100, 10) ToString: "; Viper.Math.BigInt.ToString(quot2)

' --- Mod ---
PRINT "--- Mod ---"
DIM rem1 AS OBJECT
rem1 = Viper.Math.BigInt.Mod(a, ten)
PRINT "Mod(42, 10) ToString: "; Viper.Math.BigInt.ToString(rem1)
DIM seven AS OBJECT
seven = Viper.Math.BigInt.FromInt(7)
DIM rem2 AS OBJECT
rem2 = Viper.Math.BigInt.Mod(a, seven)
PRINT "Mod(42, 7) ToString: "; Viper.Math.BigInt.ToString(rem2)

' --- Neg ---
PRINT "--- Neg ---"
DIM neg1 AS OBJECT
neg1 = Viper.Math.BigInt.Neg(a)
PRINT "Neg(42) ToString: "; Viper.Math.BigInt.ToString(neg1)
DIM neg2 AS OBJECT
neg2 = Viper.Math.BigInt.Neg(b)
PRINT "Neg(-100) ToString: "; Viper.Math.BigInt.ToString(neg2)

' --- Abs ---
PRINT "--- Abs ---"
DIM abs1 AS OBJECT
abs1 = Viper.Math.BigInt.Abs(b)
PRINT "Abs(-100) ToString: "; Viper.Math.BigInt.ToString(abs1)
DIM abs2 AS OBJECT
abs2 = Viper.Math.BigInt.Abs(a)
PRINT "Abs(42) ToString: "; Viper.Math.BigInt.ToString(abs2)

' --- Cmp ---
PRINT "--- Cmp ---"
PRINT "Cmp(42, -100): "; Viper.Math.BigInt.Cmp(a, b)
PRINT "Cmp(-100, 42): "; Viper.Math.BigInt.Cmp(b, a)
PRINT "Cmp(42, 42): "; Viper.Math.BigInt.Cmp(a, a)

' --- Eq ---
PRINT "--- Eq ---"
PRINT "Eq(42, 42): "; Viper.Math.BigInt.Eq(a, a)
PRINT "Eq(42, -100): "; Viper.Math.BigInt.Eq(a, b)

' --- IsZero ---
PRINT "--- IsZero ---"
PRINT "IsZero(0): "; Viper.Math.BigInt.IsZero(z)
PRINT "IsZero(42): "; Viper.Math.BigInt.IsZero(a)

' --- IsNegative ---
PRINT "--- IsNegative ---"
PRINT "IsNegative(-100): "; Viper.Math.BigInt.IsNegative(b)
PRINT "IsNegative(42): "; Viper.Math.BigInt.IsNegative(a)
PRINT "IsNegative(0): "; Viper.Math.BigInt.IsNegative(z)

' --- Sign ---
PRINT "--- Sign ---"
PRINT "Sign(42): "; Viper.Math.BigInt.Sign(a)
PRINT "Sign(-100): "; Viper.Math.BigInt.Sign(b)
PRINT "Sign(0): "; Viper.Math.BigInt.Sign(z)

' --- And ---
PRINT "--- And ---"
DIM x12 AS OBJECT
x12 = Viper.Math.BigInt.FromInt(12)
DIM x10 AS OBJECT
x10 = Viper.Math.BigInt.FromInt(10)
DIM band AS OBJECT
band = Viper.Math.BigInt.And(x12, x10)
PRINT "And(12, 10) ToString: "; Viper.Math.BigInt.ToString(band)

' --- Or ---
PRINT "--- Or ---"
DIM bor AS OBJECT
bor = Viper.Math.BigInt.Or(x12, x10)
PRINT "Or(12, 10) ToString: "; Viper.Math.BigInt.ToString(bor)

' --- Xor ---
PRINT "--- Xor ---"
DIM bxor AS OBJECT
bxor = Viper.Math.BigInt.Xor(x12, x10)
PRINT "Xor(12, 10) ToString: "; Viper.Math.BigInt.ToString(bxor)

' --- Not ---
PRINT "--- Not ---"
DIM bnot AS OBJECT
bnot = Viper.Math.BigInt.Not(z)
PRINT "Not(0) ToString: "; Viper.Math.BigInt.ToString(bnot)

' --- Shl ---
PRINT "--- Shl ---"
DIM shl1 AS OBJECT
shl1 = Viper.Math.BigInt.Shl(bi_one, 10)
PRINT "Shl(1, 10) ToString: "; Viper.Math.BigInt.ToString(shl1)

' --- Shr ---
PRINT "--- Shr ---"
DIM big1024 AS OBJECT
big1024 = Viper.Math.BigInt.FromInt(1024)
DIM shr1 AS OBJECT
shr1 = Viper.Math.BigInt.Shr(big1024, 5)
PRINT "Shr(1024, 5) ToString: "; Viper.Math.BigInt.ToString(shr1)

' --- Pow ---
PRINT "--- Pow ---"
DIM two AS OBJECT
two = Viper.Math.BigInt.FromInt(2)
DIM pow1 AS OBJECT
pow1 = Viper.Math.BigInt.Pow(two, 10)
PRINT "Pow(2, 10) ToString: "; Viper.Math.BigInt.ToString(pow1)
DIM pow2 AS OBJECT
pow2 = Viper.Math.BigInt.Pow(ten, 5)
PRINT "Pow(10, 5) ToString: "; Viper.Math.BigInt.ToString(pow2)

' --- Gcd ---
PRINT "--- Gcd ---"
DIM x48 AS OBJECT
x48 = Viper.Math.BigInt.FromInt(48)
DIM x18 AS OBJECT
x18 = Viper.Math.BigInt.FromInt(18)
DIM gcd1 AS OBJECT
gcd1 = Viper.Math.BigInt.Gcd(x48, x18)
PRINT "Gcd(48, 18) ToString: "; Viper.Math.BigInt.ToString(gcd1)

' --- Lcm ---
PRINT "--- Lcm ---"
DIM x4 AS OBJECT
x4 = Viper.Math.BigInt.FromInt(4)
DIM x6 AS OBJECT
x6 = Viper.Math.BigInt.FromInt(6)
DIM lcm1 AS OBJECT
lcm1 = Viper.Math.BigInt.Lcm(x4, x6)
PRINT "Lcm(4, 6) ToString: "; Viper.Math.BigInt.ToString(lcm1)

' --- BitLength ---
PRINT "--- BitLength ---"
PRINT "BitLength(1): "; Viper.Math.BigInt.BitLength(bi_one)
DIM x255 AS OBJECT
x255 = Viper.Math.BigInt.FromInt(255)
PRINT "BitLength(255): "; Viper.Math.BigInt.BitLength(x255)
DIM x256 AS OBJECT
x256 = Viper.Math.BigInt.FromInt(256)
PRINT "BitLength(256): "; Viper.Math.BigInt.BitLength(x256)

' --- TestBit ---
PRINT "--- TestBit ---"
DIM x5 AS OBJECT
x5 = Viper.Math.BigInt.FromInt(5)
PRINT "TestBit(5, 0): "; Viper.Math.BigInt.TestBit(x5, 0)
PRINT "TestBit(5, 1): "; Viper.Math.BigInt.TestBit(x5, 1)
PRINT "TestBit(5, 2): "; Viper.Math.BigInt.TestBit(x5, 2)

' --- SetBit ---
PRINT "--- SetBit ---"
DIM sb AS OBJECT
sb = Viper.Math.BigInt.SetBit(z, 3)
PRINT "SetBit(0, 3) ToString: "; Viper.Math.BigInt.ToString(sb)

' --- ClearBit ---
PRINT "--- ClearBit ---"
DIM cb AS OBJECT
cb = Viper.Math.BigInt.ClearBit(x5, 2)
PRINT "ClearBit(5, 2) ToString: "; Viper.Math.BigInt.ToString(cb)

' --- Sqrt ---
PRINT "--- Sqrt ---"
DIM x144 AS OBJECT
x144 = Viper.Math.BigInt.FromInt(144)
DIM sq AS OBJECT
sq = Viper.Math.BigInt.Sqrt(x144)
PRINT "Sqrt(144) ToString: "; Viper.Math.BigInt.ToString(sq)
DIM x100 AS OBJECT
x100 = Viper.Math.BigInt.FromInt(100)
DIM sq2 AS OBJECT
sq2 = Viper.Math.BigInt.Sqrt(x100)
PRINT "Sqrt(100) ToString: "; Viper.Math.BigInt.ToString(sq2)

' --- ToInt ---
PRINT "--- ToInt ---"
PRINT "ToInt(42): "; Viper.Math.BigInt.ToInt(a)
PRINT "ToInt(-100): "; Viper.Math.BigInt.ToInt(b)

' --- ToStrBase ---
PRINT "--- ToStrBase ---"
PRINT "ToStrBase(255, 16): "; Viper.Math.BigInt.ToStrBase(x255, 16)
PRINT "ToStrBase(255, 2): "; Viper.Math.BigInt.ToStrBase(x255, 2)
PRINT "ToStrBase(42, 8): "; Viper.Math.BigInt.ToStrBase(a, 8)

' --- FitsInt ---
PRINT "--- FitsInt ---"
PRINT "FitsInt(42): "; Viper.Math.BigInt.FitsInt(a)
PRINT "FitsInt(large): "; Viper.Math.BigInt.FitsInt(d)

PRINT "=== BigInt Audit Complete ==="
END

REM Scientific Calculator - tests all new math functions
PRINT "=== SCIENTIFIC CALCULATOR ==="
PRINT "Testing all math functions in realistic scenario"
PRINT ""

REM Constants using CONST
CONST E = 2.71828
CONST PI = 3.14159

PRINT "Mathematical Constants:"
PRINT "E = "; E
PRINT "PI = "; PI
PRINT ""

REM Trigonometric calculations
PRINT "=== Trigonometry ==="
angle = 45
PRINT "Angle (degrees): "; angle

REM Convert to radians: radians = degrees * PI / 180
radians = angle * PI / 180
PRINT "Angle (radians): "; radians

sinVal = SIN(radians)
cosVal = COS(radians)
tanVal = TAN(radians)

PRINT "SIN(45°) = "; sinVal
PRINT "COS(45°) = "; cosVal
PRINT "TAN(45°) = "; tanVal

REM Inverse trig
atanResult = ATN(1)
PRINT "ATN(1) = "; atanResult
PRINT "ATN(1) * 4 = PI? "; atanResult * 4
PRINT ""

REM Exponential and logarithms
PRINT "=== Exponential and Logarithms ==="
x = 2
expResult = EXP(x)
PRINT "EXP(2) = "; expResult

logResult = LOG(expResult)
PRINT "LOG(EXP(2)) = "; logResult

REM Natural log of E should be 1
logE = LOG(E)
PRINT "LOG(E) = "; logE
PRINT ""

REM Sign function applications
PRINT "=== Sign Analysis ==="
DIM values(5)
values(0) = -100
values(1) = -5
values(2) = 0
values(3) = 5
values(4) = 100

PRINT "Value", "Sign"
FOR i = 0 TO 4
    PRINT values(i), SGN(values(i))
NEXT i
PRINT ""

REM Complex calculation
PRINT "=== Complex Calculation ==="
REM Calculate: sqrt(sin²(x) + cos²(x)) should equal 1
testAngle = 1
s = SIN(testAngle)
c = COS(testAngle)
result = SQR(s * s + c * c)
PRINT "SQRT(SIN²(1) + COS²(1)) = "; result
PRINT "(Should be 1 - Pythagorean identity)"
PRINT ""

REM Swap variables in calculation
PRINT "=== Variable Swapping ==="
a = 10
b = 20
PRINT "Before SWAP: a="; a; " b="; b
PRINT "Sum: "; a + b

SWAP a, b
PRINT "After SWAP: a="; a; " b="; b
PRINT "Sum: "; a + b
PRINT "(Sum unchanged after swap)"
PRINT ""

PRINT "=== All calculations complete ==="

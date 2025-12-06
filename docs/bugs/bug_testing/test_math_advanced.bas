REM Test advanced math functions
DIM angle AS SINGLE
DIM result AS SINGLE
DIM num AS SINGLE

PRINT "=== MATH FUNCTION TEST ==="
PRINT ""

REM Trigonometry
angle = 1.5708  REM ~90 degrees in radians
PRINT "Testing trig functions with angle = "; angle
result = SIN(angle)
PRINT "SIN: "; result
result = COS(angle)
PRINT "COS: "; result
result = TAN(angle)
PRINT "TAN: "; result
PRINT ""

REM Inverse trig
num = 0.5
PRINT "Testing inverse trig with "; num
result = ATN(num)
PRINT "ATN: "; result
PRINT ""

REM Exponential and logarithm
num = 2.0
PRINT "Testing exp/log with "; num
result = EXP(num)
PRINT "EXP: "; result
result = LOG(num)
PRINT "LOG: "; result
PRINT ""

REM Power and roots
num = 9.0
PRINT "Testing sqrt with "; num
result = SQR(num)
PRINT "SQR: "; result
PRINT ""

REM Absolute value
num = -42.5
PRINT "Testing ABS with "; num
result = ABS(num)
PRINT "ABS: "; result
PRINT ""

REM Sign function
num = -15.0
PRINT "Testing SGN with "; num
DIM sign AS INTEGER
sign = SGN(num)
PRINT "SGN: "; sign
PRINT ""

REM Integer functions
num = 3.7
PRINT "Testing INT/FIX with "; num
DIM intVal AS INTEGER
intVal = INT(num)
PRINT "INT: "; intVal
intVal = FIX(num)
PRINT "FIX: "; intVal
PRINT ""

PRINT "Math test complete!"

' Test 15: Math functions
DIM x AS FLOAT
DIM y AS FLOAT
DIM result AS FLOAT

x = 9.0
y = 2.0

' Basic operators
PRINT "9.0 + 2.0 = "; x + y
PRINT "9.0 - 2.0 = "; x - y
PRINT "9.0 * 2.0 = "; x * y
PRINT "9.0 / 2.0 = "; x / y

' Math functions
result = SQR(x)
PRINT "SQR(9.0) = "; result

result = ABS(-5.5)
PRINT "ABS(-5.5) = "; result

result = INT(5.7)
PRINT "INT(5.7) = "; result

result = RND()
PRINT "RND() = "; result

result = SIN(1.0)
PRINT "SIN(1.0) = "; result

result = COS(1.0)
PRINT "COS(1.0) = "; result

result = TAN(1.0)
PRINT "TAN(1.0) = "; result

result = EXP(2.0)
PRINT "EXP(2.0) = "; result

result = LOG(10.0)
PRINT "LOG(10.0) = "; result
END

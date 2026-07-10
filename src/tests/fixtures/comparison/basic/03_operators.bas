' Test: Operators
' Tests: arithmetic, comparison, logical, string

' Arithmetic operators
DIM a AS INTEGER, b AS INTEGER
a = 10 : b = 3

PRINT "Arithmetic:"
PRINT "  10 + 3 = "; a + b
PRINT "  10 - 3 = "; a - b
PRINT "  10 * 3 = "; a * b
PRINT "  10 / 3 = "; a / b
' Integer division (backslash) - testing separately
DIM intDiv AS INTEGER
intDiv = 10 \ 3
PRINT "  10 intdiv 3 = "; intDiv
PRINT "  10 MOD 3 = "; a MOD b
PRINT "  2 ^ 8 = "; 2 ^ 8           ' Power

' Comparison operators
PRINT ""
PRINT "Comparison (5 vs 3):"
PRINT "  5 = 3: "; 5 = 3
PRINT "  5 <> 3: "; 5 <> 3
PRINT "  5 < 3: "; 5 < 3
PRINT "  5 > 3: "; 5 > 3
PRINT "  5 <= 3: "; 5 <= 3
PRINT "  5 >= 3: "; 5 >= 3
PRINT "  5 = 5: "; 5 = 5

' Logical operators
PRINT ""
PRINT "Logical:"
PRINT "  TRUE AND FALSE: "; TRUE AND FALSE
PRINT "  TRUE OR FALSE: "; TRUE OR FALSE
PRINT "  NOT TRUE: "; NOT TRUE
PRINT "  NOT FALSE: "; NOT FALSE

' String concatenation
DIM s1$, s2$
s1$ = "Hello"
s2$ = "World"
PRINT ""
PRINT "String concat:"
PRINT "  Hello + World = "; s1$ + " " + s2$

' Operator precedence
PRINT ""
PRINT "Precedence:"
PRINT "  2 + 3 * 4 = "; 2 + 3 * 4
PRINT "  (2 + 3) * 4 = "; (2 + 3) * 4
PRINT "  2 ^ 3 ^ 2 = "; 2 ^ 3 ^ 2

' Unary operators
PRINT ""
PRINT "Unary:"
DIM x AS INTEGER
x = 5
PRINT "  -5 = "; -x
PRINT "  +5 = "; +x

PRINT "=== Operators test complete ==="

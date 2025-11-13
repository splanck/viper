REM ============================================================================
REM Comprehensive String Operations Test
REM Testing all string functions and operations in VIPER BASIC
REM ============================================================================

PRINT "=== STRING OPERATIONS TEST ==="
PRINT ""

REM Basic string assignment and literals
DIM s1$, s2$, s3$, result$ AS STRING
s1$ = "Hello"
s2$ = "World"
s3$ = "VIPER BASIC"

PRINT "Basic Strings:"
PRINT "s1$ = "; s1$
PRINT "s2$ = "; s2$
PRINT "s3$ = "; s3$
PRINT ""

REM String concatenation with +
result$ = s1$ + " " + s2$
PRINT "Concatenation (+):"
PRINT "s1$ + "" "" + s2$ = "; result$
PRINT ""

REM LEN function
DIM len1%, len2%, len3% AS INTEGER
len1% = LEN(s1$)
len2% = LEN(s2$)
len3% = LEN(result$)

PRINT "LEN() function:"
PRINT "LEN("""; s1$; """) = "; len1%
PRINT "LEN("""; s2$; """) = "; len2%
PRINT "LEN("""; result$; """) = "; len3%
PRINT ""

REM LEFT$ function
DIM left1$, left2$, left3$ AS STRING
left1$ = LEFT$(s1$, 3)
left2$ = LEFT$(s2$, 4)
left3$ = LEFT$(result$, 5)

PRINT "LEFT$() function:"
PRINT "LEFT$("""; s1$; """, 3) = "; left1$
PRINT "LEFT$("""; s2$; """, 4) = "; left2$
PRINT "LEFT$("""; result$; """, 5) = "; left3$
PRINT ""

REM RIGHT$ function
DIM right1$, right2$, right3$ AS STRING
right1$ = RIGHT$(s1$, 3)
right2$ = RIGHT$(s2$, 4)
right3$ = RIGHT$(result$, 5)

PRINT "RIGHT$() function:"
PRINT "RIGHT$("""; s1$; """, 3) = "; right1$
PRINT "RIGHT$("""; s2$; """, 4) = "; right2$
PRINT "RIGHT$("""; result$; """, 5) = "; right3$
PRINT ""

REM MID$ function (3 parameters)
DIM mid1$, mid2$, mid3$ AS STRING
mid1$ = MID$(s1$, 2, 3)
mid2$ = MID$(s2$, 2, 3)
mid3$ = MID$(result$, 7, 5)

PRINT "MID$() function:"
PRINT "MID$("""; s1$; """, 2, 3) = "; mid1$
PRINT "MID$("""; s2$; """, 2, 3) = "; mid2$
PRINT "MID$("""; result$; """, 7, 5) = "; mid3$
PRINT ""

REM UCASE$ function
DIM upper1$, upper2$ AS STRING
upper1$ = UCASE$(s1$)
upper2$ = UCASE$(result$)

PRINT "UCASE$() function:"
PRINT "UCASE$("""; s1$; """) = "; upper1$
PRINT "UCASE$("""; result$; """) = "; upper2$
PRINT ""

REM LCASE$ function
DIM lower1$, lower2$ AS STRING
lower1$ = LCASE$(s3$)
lower2$ = LCASE$(upper1$)

PRINT "LCASE$() function:"
PRINT "LCASE$("""; s3$; """) = "; lower1$
PRINT "LCASE$("""; upper1$; """) = "; lower2$
PRINT ""

REM INSTR function
DIM pos1%, pos2%, pos3% AS INTEGER
pos1% = INSTR(result$, "W")
pos2% = INSTR(result$, "World")
pos3% = INSTR(s3$, "BASIC")

PRINT "INSTR() function:"
PRINT "INSTR("""; result$; """, ""W"") = "; pos1%
PRINT "INSTR("""; result$; """, ""World"") = "; pos2%
PRINT "INSTR("""; s3$; """, ""BASIC"") = "; pos3%
PRINT ""

REM LTRIM$, RTRIM$, TRIM$ functions
DIM padded$, trimmed1$, trimmed2$, trimmed3$ AS STRING
padded$ = "   spaced   "

trimmed1$ = LTRIM$(padded$)
trimmed2$ = RTRIM$(padded$)
trimmed3$ = TRIM$(padded$)

PRINT "Trim functions:"
PRINT "Original: |"; padded$; "|"
PRINT "LTRIM$:   |"; trimmed1$; "|"
PRINT "RTRIM$:   |"; trimmed2$; "|"
PRINT "TRIM$:    |"; trimmed3$; "|"
PRINT ""

REM CHR$ and ASC functions
DIM char1$, char2$ AS STRING
DIM code1%, code2% AS INTEGER

char1$ = CHR$(65)
char2$ = CHR$(97)
code1% = ASC("A")
code2% = ASC("Z")

PRINT "CHR$() and ASC() functions:"
PRINT "CHR$(65) = "; char1$
PRINT "CHR$(97) = "; char2$
PRINT "ASC(""A"") = "; code1%
PRINT "ASC(""Z"") = "; code2%
PRINT ""

REM VAL and STR$ functions
DIM numStr$, strNum$ AS STRING
DIM val1%, val2% AS INTEGER
DIM val3!, val4! AS SINGLE

numStr$ = "12345"
val1% = VAL(numStr$)

numStr$ = "3.14159"
val3! = VAL(numStr$)

strNum$ = STR$(42)
PRINT "VAL() and STR$() functions:"
PRINT "VAL(""12345"") = "; val1%
PRINT "VAL(""3.14159"") = "; val3!
PRINT "STR$(42) = "; strNum$
PRINT ""

REM String comparison
DIM cmp1$, cmp2$, cmp3$ AS STRING
cmp1$ = "apple"
cmp2$ = "banana"
cmp3$ = "apple"

PRINT "String comparison:"
PRINT ""; cmp1$; " = "; cmp3$; " is ";
IF cmp1$ = cmp3$ THEN
    PRINT "TRUE"
ELSE
    PRINT "FALSE"
END IF

REM BUG-031: String comparison operators (<, >, <=, >=) don't work
REM PRINT ""; cmp1$; " < "; cmp2$; " is ";
REM IF cmp1$ < cmp2$ THEN
REM     PRINT "TRUE"
REM ELSE
REM     PRINT "FALSE"
REM END IF

PRINT ""

REM BUG-032: String arrays don't work
REM DIM names$(5)
REM names$(0) = "Alice"
REM names$(1) = "Bob"
REM names$(2) = "Charlie"
REM names$(3) = "David"
REM names$(4) = "Eve"

REM PRINT "String arrays:"
REM FOR i% = 0 TO 4
REM     PRINT "names$("; i%; ") = "; names$(i%)
REM NEXT i%
REM PRINT ""

REM Empty string handling
DIM empty$ AS STRING
empty$ = ""
PRINT "Empty string length: "; LEN(empty$)
PRINT ""

REM Complex string expression
DIM firstname$, lastname$, fullname$ AS STRING
firstname$ = "John"
lastname$ = "Doe"
fullname$ = lastname$ + ", " + firstname$

PRINT "Complex expression:"
PRINT "Full name: "; fullname$
PRINT ""

REM String in loops
PRINT "Building string in loop:"
DIM builder$ AS STRING
builder$ = ""
FOR i% = 1 TO 5
    builder$ = builder$ + STR$(i%) + " "
NEXT i%
PRINT "Result: "; builder$
PRINT ""

PRINT "=== ALL STRING TESTS COMPLETE ==="
END

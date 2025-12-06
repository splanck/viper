REM Test advanced string functions
DIM name AS STRING
DIM upper AS STRING
DIM lower AS STRING
DIM full AS STRING

PRINT "Testing string functions..."
PRINT ""

name = "Mike Trout"
PRINT "Original: "; name

REM Test UCASE$
upper = UCASE$(name)
PRINT "UCASE$: "; upper

REM Test LCASE$
lower = LCASE$(name)
PRINT "LCASE$: "; lower

REM Test LEFT$
DIM first AS STRING
first = LEFT$(name, 4)
PRINT "LEFT$(4): "; first

REM Test RIGHT$
DIM last AS STRING
last = RIGHT$(name, 5)
PRINT "RIGHT$(5): "; last

REM Test MID$
DIM mid AS STRING
mid = MID$(name, 6, 5)
PRINT "MID$(6,5): "; mid

REM Test LEN
DIM length AS INTEGER
length = LEN(name)
PRINT "LEN: "; length

REM Test LTRIM$/RTRIM$/TRIM$
DIM padded AS STRING
padded = "  hello  "
PRINT "Padded: ["; padded; "]"
PRINT "LTRIM$: ["; LTRIM$(padded); "]"
PRINT "RTRIM$: ["; RTRIM$(padded); "]"
PRINT "TRIM$: ["; TRIM$(padded); "]"

REM Test INSTR
DIM pos AS INTEGER
pos = INSTR(name, "Trout")
PRINT "INSTR 'Trout': "; pos

PRINT ""
PRINT "String functions test complete!"

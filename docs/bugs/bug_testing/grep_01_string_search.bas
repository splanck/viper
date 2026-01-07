' Grep Clone Test 01: Basic string search
' Test if we can find a substring in a string

DIM text AS STRING
DIM pattern AS STRING
DIM found AS INTEGER

text = "Hello World"
pattern = "World"

' Manual substring search (no INSTR function yet)
DIM i AS INTEGER
DIM patLen AS INTEGER
DIM textLen AS INTEGER
DIM match AS INTEGER

patLen = LEN(pattern)
textLen = LEN(text)
found = 0

FOR i = 1 TO textLen - patLen + 1
    DIM substr AS STRING
    substr = MID$(text, i, patLen)

    ' Compare strings
    match = 0
    IF substr = pattern THEN
        found = 1
    END IF
NEXT i

PRINT "Text: "; text
PRINT "Pattern: "; pattern
PRINT "Found: "; found

END

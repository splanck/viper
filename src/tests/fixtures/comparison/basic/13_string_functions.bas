' Test: String Functions
' Tests: LEN, MID$, LEFT$, RIGHT$, UCASE$, LCASE$, TRIM$, INSTR, CHR$, ASC

PRINT "=== String Functions Test ==="
DIM s AS STRING
s = "Hello, World!"

' Length
PRINT ""
PRINT "--- LEN ---"
PRINT "LEN('Hello, World!'): "; LEN(s)

' Substring
PRINT ""
PRINT "--- MID$ ---"
PRINT "MID$(s, 1, 5): "; MID$(s, 1, 5)

' LEFT$/RIGHT$
PRINT ""
PRINT "--- LEFT$/RIGHT$ ---"
PRINT "LEFT$(s, 5): "; LEFT$(s, 5)
PRINT "RIGHT$(s, 6): "; RIGHT$(s, 6)

' Case conversion
PRINT ""
PRINT "--- UCASE$/LCASE$ ---"
PRINT "UCASE$: "; UCASE$(s)
PRINT "LCASE$: "; LCASE$(s)

' Trim
PRINT ""
PRINT "--- TRIM$ ---"
DIM padded AS STRING
padded = "  spaces  "
PRINT "TRIM$('  spaces  '): '"; TRIM$(padded); "'"
PRINT "LTRIM$: '"; LTRIM$(padded); "'"
PRINT "RTRIM$: '"; RTRIM$(padded); "'"

' INSTR
PRINT ""
PRINT "--- INSTR ---"
PRINT "INSTR(s, 'World'): "; INSTR(s, "World")

' CHR$/ASC
PRINT ""
PRINT "--- CHR$/ASC ---"
PRINT "CHR$(65): "; CHR$(65)
PRINT "ASC('A'): "; ASC("A")

' VAL/STR$
PRINT ""
PRINT "--- VAL/STR$ ---"
PRINT "VAL('123'): "; VAL("123")
PRINT "STR$(456): '"; STR$(456); "'"

PRINT ""
PRINT "=== String Functions test complete ==="

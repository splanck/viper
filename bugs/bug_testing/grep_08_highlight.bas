' Grep Clone Test 08: Highlight matches with brackets
' Since we can't use CHR() for ANSI codes, use [[ ]] to highlight

FUNCTION HighlightMatch(line AS STRING, pattern AS STRING) AS STRING
    DIM pos AS INTEGER
    DIM before AS STRING
    DIM match AS STRING
    DIM after AS STRING
    DIM result AS STRING
    DIM patLen AS INTEGER

    pos = INSTR(line, pattern)

    IF pos = 0 THEN
        RETURN line
    END IF

    patLen = LEN(pattern)

    ' Extract parts
    IF pos > 1 THEN
        before = LEFT$(line, pos - 1)
    ELSE
        before = ""
    END IF

    match = MID$(line, pos, patLen)

    IF pos + patLen <= LEN(line) THEN
        after = RIGHT$(line, LEN(line) - pos - patLen + 1)
    ELSE
        after = ""
    END IF

    ' Build highlighted result
    result = before + "[[" + match + "]]" + after

    RETURN result
END FUNCTION

' Test
DIM text AS STRING
DIM highlighted AS STRING

text = "This line contains pattern in it"
highlighted = HighlightMatch(text, "pattern")

PRINT "Original: "; text
PRINT "Highlighted: "; highlighted

END

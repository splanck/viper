' ================================================================
'                     VIPERGREP SIMPLE
'           Simplified Grep Clone in Viper BASIC
' ================================================================
' Avoiding complex object passing to work around BUG-048

' ===== CLASS DEFINITIONS =====

CLASS FileStats
    DIM filename AS STRING
    DIM totalLines AS INTEGER
    DIM matchedLines AS INTEGER
END CLASS

' ===== MAIN PROGRAM =====

DIM pattern AS STRING
DIM caseSensitive AS INTEGER
DIM showLineNumbers AS INTEGER

pattern = "pattern"
caseSensitive = 0  ' Case-insensitive (use LCASE$)
showLineNumbers = 1

PRINT "================================================================"
PRINT "                      VIPERGREP SIMPLE"
PRINT "================================================================"
PRINT ""
PRINT "Pattern: '"; pattern; "'"
PRINT "Case-sensitive: ";

IF caseSensitive = 1 THEN
    PRINT "YES"
ELSE
    PRINT "NO"
END IF

PRINT ""
PRINT "----------------------------------------------------------------"
PRINT ""

' Process files manually
DIM file1 AS STRING
DIM file2 AS STRING
DIM file3 AS STRING

file1 = "/tmp/grep_test_data.txt"
file2 = "/tmp/grep_test_data2.txt"
file3 = "/tmp/grep_test_data3.txt"

DIM fileNum AS INTEGER
DIM line AS STRING
DIM lineNum AS INTEGER
DIM matchCount AS INTEGER
DIM totalMatches AS INTEGER
DIM totalFiles AS INTEGER
DIM searchLine AS STRING
DIM searchPattern AS STRING
DIM pos AS INTEGER

totalMatches = 0
totalFiles = 0

' ===== Process File 1 =====
PRINT "File: "; file1
fileNum = 1
lineNum = 0
matchCount = 0

OPEN file1 FOR INPUT AS #fileNum

WHILE NOT EOF(#fileNum)
    LINE INPUT #fileNum, line
    lineNum = lineNum + 1

    ' Search (case-insensitive)
    searchLine = LCASE$(line)
    searchPattern = LCASE$(pattern)
    pos = INSTR(searchLine, searchPattern)

    IF pos > 0 THEN
        matchCount = matchCount + 1
        PRINT "  ";
        IF showLineNumbers = 1 THEN
            PRINT lineNum; ":";
        END IF

        ' Highlight match with brackets
        DIM before AS STRING
        DIM match AS STRING
        DIM after AS STRING
        DIM patLen AS INTEGER

        patLen = LEN(pattern)

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

        PRINT before; "[["; match; "]]"; after
    END IF
WEND

CLOSE #fileNum

PRINT "  Matches: "; matchCount
PRINT ""
totalMatches = totalMatches + matchCount
totalFiles = totalFiles + 1

' ===== Process File 2 =====
PRINT "File: "; file2
fileNum = 1
lineNum = 0
matchCount = 0

OPEN file2 FOR INPUT AS #fileNum

WHILE NOT EOF(#fileNum)
    LINE INPUT #fileNum, line
    lineNum = lineNum + 1

    searchLine = LCASE$(line)
    searchPattern = LCASE$(pattern)
    pos = INSTR(searchLine, searchPattern)

    IF pos > 0 THEN
        matchCount = matchCount + 1
        PRINT "  ";
        IF showLineNumbers = 1 THEN
            PRINT lineNum; ":";
        END IF
        PRINT line
    END IF
WEND

CLOSE #fileNum

PRINT "  Matches: "; matchCount
PRINT ""
totalMatches = totalMatches + matchCount
totalFiles = totalFiles + 1

' ===== Process File 3 =====
PRINT "File: "; file3
fileNum = 1
lineNum = 0
matchCount = 0

OPEN file3 FOR INPUT AS #fileNum

WHILE NOT EOF(#fileNum)
    LINE INPUT #fileNum, line
    lineNum = lineNum + 1

    searchLine = LCASE$(line)
    searchPattern = LCASE$(pattern)
    pos = INSTR(searchLine, searchPattern)

    IF pos > 0 THEN
        matchCount = matchCount + 1
        PRINT "  ";
        IF showLineNumbers = 1 THEN
            PRINT lineNum; ":";
        END IF
        PRINT line
    END IF
WEND

CLOSE #fileNum

PRINT "  Matches: "; matchCount
PRINT ""
totalMatches = totalMatches + matchCount
totalFiles = totalFiles + 1

' ===== Summary =====
PRINT "----------------------------------------------------------------"
PRINT "SUMMARY:"
PRINT "  Files searched: "; totalFiles
PRINT "  Total matches: "; totalMatches
PRINT "================================================================"

END

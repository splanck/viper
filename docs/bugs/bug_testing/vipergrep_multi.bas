' VIPERGREP MULTI - Grep clone with multiple file support
' Demonstrates: arrays, file I/O, OOP, string functions, loops

' ===== CLASSES =====

CLASS GrepOptions
    DIM pattern AS STRING
    DIM ignoreCase AS INTEGER
    DIM showLineNumbers AS INTEGER
    DIM countOnly AS INTEGER
END CLASS

CLASS FileStats
    DIM filename AS STRING
    DIM totalLines AS INTEGER
    DIM matchingLines AS INTEGER
END CLASS

' ===== HELPER FUNCTIONS =====

FUNCTION SearchInLine(line AS STRING, pattern AS STRING, ignoreCase AS INTEGER) AS INTEGER
    DIM searchLine AS STRING
    DIM searchPattern AS STRING

    searchLine = line
    searchPattern = pattern

    IF ignoreCase = 1 THEN
        searchLine = LCASE$(line)
        searchPattern = LCASE$(pattern)
    END IF

    RETURN INSTR(searchLine, searchPattern)
END FUNCTION

SUB ProcessFile(filename AS STRING, pattern AS STRING, ignoreCase AS INTEGER, showLineNum AS INTEGER)
    DIM fileNum AS INTEGER
    DIM line AS STRING
    DIM lineNum AS INTEGER
    DIM matchCount AS INTEGER
    DIM matchPos AS INTEGER

    fileNum = 1
    lineNum = 0
    matchCount = 0

    PRINT ""
    PRINT "=== File: "; filename; " ==="

    OPEN filename FOR INPUT AS #fileNum

    WHILE NOT EOF(#fileNum)
        LINE INPUT #fileNum, line
        lineNum = lineNum + 1

        matchPos = SearchInLine(line, pattern, ignoreCase)

        IF matchPos > 0 THEN
            matchCount = matchCount + 1

            IF showLineNum = 1 THEN
                PRINT filename; ":"; lineNum; ":";
            ELSE
                PRINT filename; ":";
            END IF
            PRINT line
        END IF
    WEND

    CLOSE #fileNum

    PRINT "Matches in this file: "; matchCount
END SUB

' ===== MAIN PROGRAM =====

DIM options AS GrepOptions

options = NEW GrepOptions()
options.pattern = "pattern"
options.ignoreCase = 0
options.showLineNumbers = 1
options.countOnly = 0

' Create a "manual array" of filenames (can't use STRING arrays due to BUG-045)
' Using multiple variables instead
DIM file1 AS STRING
DIM file2 AS STRING
DIM file3 AS STRING
DIM fileCount AS INTEGER

file1 = "/tmp/grep_test_data.txt"
file2 = "/tmp/grep_test_data2.txt"
file3 = "/tmp/grep_test_data3.txt"
fileCount = 3

PRINT "================================================"
PRINT "             VIPERGREP MULTI-FILE"
PRINT "================================================"
PRINT ""
PRINT "Pattern: '"; options.pattern; "'"
PRINT "Files to search: "; fileCount
PRINT "Case-insensitive: ";
IF options.ignoreCase = 1 THEN
    PRINT "YES"
ELSE
    PRINT "NO"
END IF

' Process each file (manual loop instead of array iteration)
ProcessFile(file1, options.pattern, options.ignoreCase, options.showLineNumbers)
ProcessFile(file2, options.pattern, options.ignoreCase, options.showLineNumbers)
ProcessFile(file3, options.pattern, options.ignoreCase, options.showLineNumbers)

PRINT ""
PRINT "================================================"
PRINT "Search complete. Processed "; fileCount; " files."
PRINT "================================================"

END

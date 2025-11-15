' Grep Clone Test 05: OOP version with classes
' Test classes for grep functionality

CLASS GrepOptions
    DIM ignoreCase AS INTEGER
    DIM showLineNumbers AS INTEGER
    DIM countOnly AS INTEGER
    DIM invertMatch AS INTEGER
END CLASS

CLASS GrepResult
    DIM lineNumber AS INTEGER
    DIM lineText AS STRING
    DIM matchPosition AS INTEGER
END CLASS

CLASS GrepStats
    DIM totalLines AS INTEGER
    DIM matchingLines AS INTEGER
    DIM filesSearched AS INTEGER
END CLASS

' Main program
DIM options AS GrepOptions
DIM stats AS GrepStats

options = NEW GrepOptions()
options.ignoreCase = 0
options.showLineNumbers = 1
options.countOnly = 0
options.invertMatch = 0

stats = NEW GrepStats()
stats.totalLines = 0
stats.matchingLines = 0
stats.filesSearched = 1

' Search parameters
DIM filename AS STRING
DIM pattern AS STRING
DIM fileNum AS INTEGER
DIM line AS STRING
DIM pos AS INTEGER

filename = "/tmp/grep_test_data.txt"
pattern = "pattern"
fileNum = 1

PRINT "=== OOP Grep Test ==="
PRINT "File: "; filename
PRINT "Pattern: "; pattern
PRINT "Show line numbers: "; options.showLineNumbers
PRINT ""

OPEN filename FOR INPUT AS #fileNum

WHILE NOT EOF(#fileNum)
    LINE INPUT #fileNum, line
    stats.totalLines = stats.totalLines + 1

    pos = INSTR(line, pattern)

    IF pos > 0 THEN
        stats.matchingLines = stats.matchingLines + 1

        ' Print result
        IF options.showLineNumbers = 1 THEN
            PRINT stats.totalLines; ": ";
        END IF
        PRINT line
    END IF
WEND

CLOSE #fileNum

' Print statistics
PRINT ""
PRINT "=== Statistics ==="
PRINT "Total lines: "; stats.totalLines
PRINT "Matching lines: "; stats.matchingLines
PRINT "Files searched: "; stats.filesSearched

END

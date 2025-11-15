' ================================================================
'                          VIPERGREP
'              Complete Grep Clone in Viper BASIC
' ================================================================
'
' Features demonstrated:
' - File I/O with OPEN/LINE INPUT/CLOSE
' - String searching with INSTR
' - Case-insensitive search with LCASE$
' - Pattern matching and highlighting
' - Line number tracking
' - Multiple file processing
' - Statistics collection with OOP classes
' - Modular design with FUNCTIONs and SUBs
'
' Limitations worked around:
' - No STRING arrays (BUG-045) - using separate variables
' - No IF in class methods (BUG-047) - logic in module level
' - No CHR() (BUG-044) - using brackets for highlighting
' - No ON ERROR (BUG-052) - errors trap immediately
'
' ================================================================

' ===== CLASS DEFINITIONS =====

CLASS GrepConfig
    DIM pattern AS STRING
    DIM caseSensitive AS INTEGER
    DIM showLineNumbers AS INTEGER
    DIM highlightMatches AS INTEGER
    DIM countOnly AS INTEGER
    DIM showFilenames AS INTEGER
END CLASS

CLASS FileResult
    DIM filename AS STRING
    DIM totalLines AS INTEGER
    DIM matchedLines AS INTEGER
    DIM firstMatch AS INTEGER
    DIM lastMatch AS INTEGER
END CLASS

CLASS GlobalStats
    DIM filesProcessed AS INTEGER
    DIM totalLinesScanned AS INTEGER
    DIM totalMatches AS INTEGER
    DIM filesWithMatches AS INTEGER
    DIM startTime AS INTEGER
END CLASS

' ===== UTILITY FUNCTIONS =====

SUB PrintBanner()
    PRINT "================================================================"
    PRINT "                          VIPERGREP"
    PRINT "              Complete Grep Clone in Viper BASIC"
    PRINT "================================================================"
END SUB

SUB PrintSeparator()
    PRINT "----------------------------------------------------------------"
END SUB

FUNCTION StringContains(text AS STRING, pattern AS STRING, caseSensitive AS INTEGER) AS INTEGER
    DIM searchText AS STRING
    DIM searchPattern AS STRING

    searchText = text
    searchPattern = pattern

    IF caseSensitive = 0 THEN
        searchText = LCASE$(text)
        searchPattern = LCASE$(pattern)
    END IF

    RETURN INSTR(searchText, searchPattern)
END FUNCTION

FUNCTION HighlightPattern(line AS STRING, pattern AS STRING, caseSensitive AS INTEGER) AS STRING
    DIM pos AS INTEGER
    DIM patLen AS INTEGER
    DIM before AS STRING
    DIM match AS STRING
    DIM after AS STRING

    ' Find pattern position (case-insensitive if needed)
    IF caseSensitive = 0 THEN
        pos = INSTR(LCASE$(line), LCASE$(pattern))
    ELSE
        pos = INSTR(line, pattern)
    END IF

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

    ' Return with highlighting brackets
    RETURN before + "[[" + match + "]]" + after
END FUNCTION

SUB ProcessSingleFile(fname AS STRING, cfg AS GrepConfig, result AS FileResult, grandTotal AS INTEGER)
    DIM fileNum AS INTEGER
    DIM line AS STRING
    DIM matchPos AS INTEGER
    DIM displayLine AS STRING

    fileNum = 1
    result.filename = fname
    result.totalLines = 0
    result.matchedLines = 0
    result.firstMatch = 0
    result.lastMatch = 0

    ' Open file
    OPEN fname FOR INPUT AS #fileNum

    ' Process each line
    WHILE NOT EOF(#fileNum)
        LINE INPUT #fileNum, line
        result.totalLines = result.totalLines + 1

        ' Check for match
        matchPos = StringContains(line, cfg.pattern, cfg.caseSensitive)

        IF matchPos > 0 THEN
            result.matchedLines = result.matchedLines + 1
            grandTotal = grandTotal + 1

            ' Track first and last match line numbers
            IF result.firstMatch = 0 THEN
                result.firstMatch = result.totalLines
            END IF
            result.lastMatch = result.totalLines

            ' Print match (unless count-only mode)
            IF cfg.countOnly = 0 THEN
                ' Filename prefix
                IF cfg.showFilenames = 1 THEN
                    PRINT fname; ":";
                END IF

                ' Line number
                IF cfg.showLineNumbers = 1 THEN
                    PRINT result.totalLines; ":";
                END IF

                ' The line itself (highlighted if requested)
                IF cfg.highlightMatches = 1 THEN
                    displayLine = HighlightPattern(line, cfg.pattern, cfg.caseSensitive)
                    PRINT displayLine
                ELSE
                    PRINT line
                END IF
            END IF
        END IF
    WEND

    CLOSE #fileNum
END SUB

SUB PrintFileStats(result AS FileResult)
    PRINT "  Lines scanned: "; result.totalLines
    PRINT "  Matches found: "; result.matchedLines

    IF result.matchedLines > 0 THEN
        PRINT "  First match at line: "; result.firstMatch
        PRINT "  Last match at line: "; result.lastMatch
    END IF
END SUB

' ===== MAIN PROGRAM =====

DIM config AS GrepConfig
DIM stats AS GlobalStats
DIM fileResults(3) AS FileResult
DIM tempResult AS FileResult

' Initialize configuration
config = NEW GrepConfig()
config.pattern = "pattern"
config.caseSensitive = 0  ' Case-insensitive
config.showLineNumbers = 1
config.highlightMatches = 1
config.countOnly = 0
config.showFilenames = 1

' Initialize statistics
stats = NEW GlobalStats()
stats.filesProcessed = 0
stats.totalLinesScanned = 0
stats.totalMatches = 0
stats.filesWithMatches = 0

' File list (manual since STRING arrays don't work)
DIM file1 AS STRING
DIM file2 AS STRING
DIM file3 AS STRING

file1 = "/tmp/grep_test_data.txt"
file2 = "/tmp/grep_test_data2.txt"
file3 = "/tmp/grep_test_data3.txt"

' Banner and configuration
PrintBanner()
PRINT ""
PRINT "Search Configuration:"
PRINT "  Pattern: '"; config.pattern; "'"
PRINT "  Case-sensitive: ";

IF config.caseSensitive = 1 THEN
    PRINT "YES"
ELSE
    PRINT "NO"
END IF

PRINT "  Show line numbers: ";
IF config.showLineNumbers = 1 THEN
    PRINT "YES"
ELSE
    PRINT "NO"
END IF

PRINT "  Highlight matches: ";
IF config.highlightMatches = 1 THEN
    PRINT "YES"
ELSE
    PRINT "NO"
END IF

PRINT ""
PrintSeparator()
PRINT ""

' Process each file
DIM runningTotal AS INTEGER
runningTotal = 0

' File 1
PRINT "Searching: "; file1
tempResult = NEW FileResult()
ProcessSingleFile(file1, config, tempResult, runningTotal)
fileResults(0) = tempResult
stats.filesProcessed = stats.filesProcessed + 1
stats.totalLinesScanned = stats.totalLinesScanned + tempResult.totalLines
stats.totalMatches = stats.totalMatches + tempResult.matchedLines
IF tempResult.matchedLines > 0 THEN
    stats.filesWithMatches = stats.filesWithMatches + 1
END IF
PrintFileStats(tempResult)
PRINT ""

' File 2
PRINT "Searching: "; file2
tempResult = NEW FileResult()
ProcessSingleFile(file2, config, tempResult, runningTotal)
fileResults(1) = tempResult
stats.filesProcessed = stats.filesProcessed + 1
stats.totalLinesScanned = stats.totalLinesScanned + tempResult.totalLines
stats.totalMatches = stats.totalMatches + tempResult.matchedLines
IF tempResult.matchedLines > 0 THEN
    stats.filesWithMatches = stats.filesWithMatches + 1
END IF
PrintFileStats(tempResult)
PRINT ""

' File 3
PRINT "Searching: "; file3
tempResult = NEW FileResult()
ProcessSingleFile(file3, config, tempResult, runningTotal)
fileResults(2) = tempResult
stats.filesProcessed = stats.filesProcessed + 1
stats.totalLinesScanned = stats.totalLinesScanned + tempResult.totalLines
stats.totalMatches = stats.totalMatches + tempResult.matchedLines
IF tempResult.matchedLines > 0 THEN
    stats.filesWithMatches = stats.filesWithMatches + 1
END IF
PrintFileStats(tempResult)
PRINT ""

' Final statistics
PrintSeparator()
PRINT ""
PRINT "SUMMARY STATISTICS:"
PRINT "  Files processed: "; stats.filesProcessed
PRINT "  Files with matches: "; stats.filesWithMatches
PRINT "  Total lines scanned: "; stats.totalLinesScanned
PRINT "  Total matches found: "; stats.totalMatches
PRINT ""

' Average statistics
DIM avgLinesPerFile AS INTEGER
DIM avgMatchesPerFile AS INTEGER

avgLinesPerFile = stats.totalLinesScanned / stats.filesProcessed
avgMatchesPerFile = stats.totalMatches / stats.filesProcessed

PRINT "AVERAGES:"
PRINT "  Lines per file: "; avgLinesPerFile
PRINT "  Matches per file: "; avgMatchesPerFile
PRINT ""

PrintBanner()
PRINT ""
PRINT "Search complete!"
PRINT ""

END

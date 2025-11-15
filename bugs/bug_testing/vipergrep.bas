' VIPERGREP - A grep clone written in Viper BASIC
' Usage: vipergrep [options] pattern filename
'
' Features:
' - Basic pattern matching with INSTR
' - Line number display
' - Case-insensitive search (-i)
' - Count matches only (-c)
' - Invert match (-v)
' - Multiple file support
' - Statistics

' ===== CLASSES =====

CLASS GrepOptions
    DIM pattern AS STRING
    DIM ignoreCase AS INTEGER
    DIM showLineNumbers AS INTEGER
    DIM countOnly AS INTEGER
    DIM invertMatch AS INTEGER
    DIM showFilename AS INTEGER
END CLASS

CLASS SearchResult
    DIM filename AS STRING
    DIM lineNumber AS INTEGER
    DIM lineText AS STRING
    DIM matchCount AS INTEGER
END CLASS

CLASS FileStats
    DIM filename AS STRING
    DIM totalLines AS INTEGER
    DIM matchingLines AS INTEGER
    DIM processed AS INTEGER
END CLASS

' ===== HELPER FUNCTIONS =====

SUB PrintUsage()
    PRINT "ViperGrep - Pattern search utility"
    PRINT ""
    PRINT "Usage: vipergrep [pattern] [filename]"
    PRINT ""
    PRINT "Options (hardcoded in this version):"
    PRINT "  ignoreCase: 0=case-sensitive, 1=ignore case"
    PRINT "  showLineNumbers: 0=no, 1=yes"
    PRINT "  countOnly: 0=show matches, 1=count only"
    PRINT ""
END SUB

FUNCTION SearchInLine(line AS STRING, pattern AS STRING, ignoreCase AS INTEGER) AS INTEGER
    DIM searchLine AS STRING
    DIM searchPattern AS STRING
    DIM pos AS INTEGER

    searchLine = line
    searchPattern = pattern

    ' Convert to lowercase if case-insensitive
    IF ignoreCase = 1 THEN
        searchLine = LCASE$(line)
        searchPattern = LCASE$(pattern)
    END IF

    pos = INSTR(searchLine, searchPattern)
    RETURN pos
END FUNCTION

SUB PrintMatch(lineNum AS INTEGER, line AS STRING, showLineNum AS INTEGER, filename AS STRING, showFilename AS INTEGER)
    IF showFilename = 1 THEN
        PRINT filename; ":";
    END IF

    IF showLineNum = 1 THEN
        PRINT lineNum; ":";
    END IF

    PRINT line
END SUB

' ===== MAIN PROGRAM =====

DIM options AS GrepOptions
DIM stats AS FileStats
DIM totalMatches AS INTEGER
DIM totalFiles AS INTEGER

options = NEW GrepOptions()
stats = NEW FileStats()

' Configuration (hardcoded for this demo)
options.pattern = "pattern"
options.ignoreCase = 0
options.showLineNumbers = 1
options.countOnly = 0
options.invertMatch = 0
options.showFilename = 0

' File to search
DIM filename AS STRING
filename = "/tmp/grep_test_data.txt"

' Initialize stats
stats.filename = filename
stats.totalLines = 0
stats.matchingLines = 0
stats.processed = 0

totalMatches = 0
totalFiles = 0

' Banner
PRINT "================================================"
PRINT "                  VIPERGREP"
PRINT "         Grep Clone in Viper BASIC"
PRINT "================================================"
PRINT ""
PRINT "Searching for: '"; options.pattern; "'"
PRINT "File: "; filename
PRINT "Case-sensitive: ";
IF options.ignoreCase = 0 THEN
    PRINT "YES"
ELSE
    PRINT "NO"
END IF
PRINT ""

' Process file
DIM fileNum AS INTEGER
DIM line AS STRING
DIM matchPos AS INTEGER

fileNum = 1

OPEN filename FOR INPUT AS #fileNum

WHILE NOT EOF(#fileNum)
    LINE INPUT #fileNum, line
    stats.totalLines = stats.totalLines + 1

    ' Search for pattern
    matchPos = SearchInLine(line, options.pattern, options.ignoreCase)

    ' Handle match/no-match based on invertMatch option
    DIM isMatch AS INTEGER
    isMatch = 0

    IF options.invertMatch = 0 THEN
        ' Normal mode: show lines that match
        IF matchPos > 0 THEN
            isMatch = 1
        END IF
    ELSE
        ' Invert mode: show lines that DON'T match
        IF matchPos = 0 THEN
            isMatch = 1
        END IF
    END IF

    ' Process match
    IF isMatch = 1 THEN
        stats.matchingLines = stats.matchingLines + 1
        totalMatches = totalMatches + 1

        ' Print match (unless count-only mode)
        IF options.countOnly = 0 THEN
            PrintMatch(stats.totalLines, line, options.showLineNumbers, filename, options.showFilename)
        END IF
    END IF
WEND

CLOSE #fileNum

stats.processed = 1
totalFiles = totalFiles + 1

' Print statistics
PRINT ""
PRINT "================================================"
PRINT "                 STATISTICS"
PRINT "================================================"
PRINT "File: "; stats.filename
PRINT "  Total lines: "; stats.totalLines
PRINT "  Matching lines: "; stats.matchingLines
PRINT ""
PRINT "Grand Total:"
PRINT "  Files searched: "; totalFiles
PRINT "  Total matches: "; totalMatches
PRINT ""

END

REM ===================================================================
REM ADVENTURE ENGINE - Core game engine and utilities
REM Testing: ANSI colors, string utilities, basic framework
REM ===================================================================

REM ANSI Color codes - initializing at module level due to bugs
REM BUG: CONST with CHR$() not evaluated at compile time
REM BUG: Global assignments in SUBs cause cleanup code to call rt_str_release_maybe on pointer types
DIM COLOR_RESET$ AS STRING
DIM COLOR_RED$ AS STRING
DIM COLOR_GREEN$ AS STRING
DIM COLOR_YELLOW$ AS STRING
DIM COLOR_BLUE$ AS STRING
DIM COLOR_MAGENTA$ AS STRING
DIM COLOR_CYAN$ AS STRING
DIM COLOR_WHITE$ AS STRING
DIM COLOR_BOLD$ AS STRING

REM Initialize colors at module level
COLOR_RESET$ = CHR$(27) + "[0m"
COLOR_RED$ = CHR$(27) + "[31m"
COLOR_GREEN$ = CHR$(27) + "[32m"
COLOR_YELLOW$ = CHR$(27) + "[33m"
COLOR_BLUE$ = CHR$(27) + "[34m"
COLOR_MAGENTA$ = CHR$(27) + "[35m"
COLOR_CYAN$ = CHR$(27) + "[36m"
COLOR_WHITE$ = CHR$(27) + "[37m"
COLOR_BOLD$ = CHR$(27) + "[1m"

REM Clear screen
SUB ClearScreen()
    CLS
END SUB

REM Print with color
SUB PrintColor(msg AS STRING, clr AS STRING)
    PRINT clr + msg + COLOR_RESET$
END SUB

REM Print a separator line
SUB PrintSeparator()
    PRINT COLOR_CYAN$ + "========================================" + COLOR_RESET$
END SUB

REM Test the basic utilities
SUB TestEngine()
    ClearScreen()
    PRINT COLOR_BOLD$ + "Adventure Engine Test" + COLOR_RESET$
    PrintSeparator()
    PrintColor("Red text test", COLOR_RED$)
    PrintColor("Green text test", COLOR_GREEN$)
    PrintColor("Yellow text test", COLOR_YELLOW$)
    PrintColor("Blue text test", COLOR_BLUE$)
    PrintSeparator()
    PRINT "Engine initialized successfully!"
END SUB

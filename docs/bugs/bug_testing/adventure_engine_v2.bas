REM ===================================================================
REM ADVENTURE ENGINE V2 - Core game engine and utilities
REM Testing: ANSI colors, string utilities, basic framework
REM Workaround: No module-level init to avoid cleanup bug
REM ===================================================================

REM Clear screen
SUB ClearScreen()
    CLS
END SUB

REM Print with color
SUB PrintColor(msg AS STRING, clr AS STRING, reset AS STRING)
    PRINT clr + msg + reset
END SUB

REM Print a separator line
SUB PrintSeparator(cyan AS STRING, reset AS STRING)
    PRINT cyan + "========================================" + reset
END SUB

REM Test the basic utilities
SUB TestEngine()
    DIM COLOR_RESET AS STRING
    DIM COLOR_RED AS STRING
    DIM COLOR_GREEN AS STRING
    DIM COLOR_YELLOW AS STRING
    DIM COLOR_BLUE AS STRING
    DIM COLOR_CYAN AS STRING
    DIM COLOR_BOLD AS STRING

    COLOR_RESET = CHR$(27) + "[0m"
    COLOR_RED = CHR$(27) + "[31m"
    COLOR_GREEN = CHR$(27) + "[32m"
    COLOR_YELLOW = CHR$(27) + "[33m"
    COLOR_BLUE = CHR$(27) + "[34m"
    COLOR_CYAN = CHR$(27) + "[36m"
    COLOR_BOLD = CHR$(27) + "[1m"

    ClearScreen()
    PRINT COLOR_BOLD + "Adventure Engine Test" + COLOR_RESET
    PrintSeparator(COLOR_CYAN, COLOR_RESET)
    PrintColor("Red text test", COLOR_RED, COLOR_RESET)
    PrintColor("Green text test", COLOR_GREEN, COLOR_RESET)
    PrintColor("Yellow text test", COLOR_YELLOW, COLOR_RESET)
    PrintColor("Blue text test", COLOR_BLUE, COLOR_RESET)
    PrintSeparator(COLOR_CYAN, COLOR_RESET)
    PRINT "Engine initialized successfully!"
END SUB

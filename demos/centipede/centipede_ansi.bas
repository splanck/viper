REM ====================================================================
REM ANSI Terminal Control Module for Centipede
REM Provides color and cursor control using ANSI escape codes
REM ====================================================================

REM ANSI escape sequence prefix
DIM ESC AS STRING
ESC = CHR(27)

REM ANSI Color Codes
DIM RESET AS STRING
DIM COLOR_BLACK AS STRING
DIM COLOR_RED AS STRING
DIM COLOR_GREEN AS STRING
DIM COLOR_YELLOW AS STRING
DIM COLOR_BLUE AS STRING
DIM COLOR_MAGENTA AS STRING
DIM COLOR_CYAN AS STRING
DIM COLOR_WHITE AS STRING
DIM COLOR_BRIGHT_RED AS STRING
DIM COLOR_BRIGHT_GREEN AS STRING
DIM COLOR_BRIGHT_YELLOW AS STRING
DIM COLOR_BRIGHT_BLUE AS STRING
DIM COLOR_BRIGHT_MAGENTA AS STRING
DIM COLOR_BRIGHT_CYAN AS STRING
DIM COLOR_BRIGHT_WHITE AS STRING

RESET = "[0m"
COLOR_BLACK = "[30m"
COLOR_RED = "[31m"
COLOR_GREEN = "[32m"
COLOR_YELLOW = "[33m"
COLOR_BLUE = "[34m"
COLOR_MAGENTA = "[35m"
COLOR_CYAN = "[36m"
COLOR_WHITE = "[37m"
COLOR_BRIGHT_RED = "[91m"
COLOR_BRIGHT_GREEN = "[92m"
COLOR_BRIGHT_YELLOW = "[93m"
COLOR_BRIGHT_BLUE = "[94m"
COLOR_BRIGHT_MAGENTA = "[95m"
COLOR_BRIGHT_CYAN = "[96m"
COLOR_BRIGHT_WHITE = "[97m"

REM Background colors
DIM BG_BLACK AS STRING
DIM BG_RED AS STRING
DIM BG_GREEN AS STRING
DIM BG_YELLOW AS STRING
DIM BG_BLUE AS STRING
DIM BG_MAGENTA AS STRING

BG_BLACK = "[40m"
BG_RED = "[41m"
BG_GREEN = "[42m"
BG_YELLOW = "[43m"
BG_BLUE = "[44m"
BG_MAGENTA = "[45m"

REM Clear the entire screen
SUB ClearScreen()
    PRINT ESC; "[2J"; ESC; "[H";
END SUB

REM Hide the cursor
SUB HideCursor()
    PRINT ESC; "[?25l";
END SUB

REM Show the cursor
SUB ShowCursor()
    PRINT ESC; "[?25h";
END SUB

REM Move cursor to specific row and column
SUB GotoXY(row AS INTEGER, col AS INTEGER)
    PRINT ESC; "["; row; ";"; col; "H";
END SUB

REM Print text at specific position
SUB PrintAt(row AS INTEGER, col AS INTEGER, text AS STRING)
    GotoXY(row, col)
    PRINT text;
END SUB

REM Print colored text at specific position
SUB PrintColorAt(row AS INTEGER, col AS INTEGER, clr AS STRING, text AS STRING)
    GotoXY(row, col)
    PRINT ESC; clr; text; ESC; RESET;
END SUB

REM Start a new frame - reposition cursor
SUB BeginFrame()
    PRINT ESC; "[H";
END SUB

REM Draw colored text at position (direct output)
SUB BufferColorAt(row AS INTEGER, col AS INTEGER, clr AS STRING, text AS STRING)
    PRINT ESC; "["; row; ";"; col; "H"; ESC; clr; text; ESC; RESET;
END SUB

REM Draw text at position (direct output)
SUB BufferAt(row AS INTEGER, col AS INTEGER, text AS STRING)
    PRINT ESC; "["; row; ";"; col; "H"; text;
END SUB

REM Flush frame
SUB FlushFrame()
    PRINT "";
END SUB

REM Draw a box at position
SUB DrawBox(row AS INTEGER, col AS INTEGER, width AS INTEGER, height AS INTEGER, clr AS STRING)
    DIM i AS INTEGER
    DIM topLine AS STRING
    DIM midLine AS STRING
    DIM botLine AS STRING

    topLine = "+"
    FOR i = 1 TO width - 2
        topLine = topLine + "-"
    NEXT i
    topLine = topLine + "+"

    botLine = topLine

    PrintColorAt(row, col, clr, topLine)
    FOR i = 1 TO height - 2
        PrintColorAt(row + i, col, clr, "|")
        PrintColorAt(row + i, col + width - 1, clr, "|")
    NEXT i
    PrintColorAt(row + height - 1, col, clr, botLine)
END SUB

REM Flash screen effect (for player death)
SUB FlashScreen()
    PRINT ESC; "[7m";
    SLEEP 100
    PRINT ESC; "[0m";
END SUB

REM Screen dimensions
DIM SCREEN_WIDTH AS INTEGER
DIM SCREEN_HEIGHT AS INTEGER
SCREEN_WIDTH = 80
SCREEN_HEIGHT = 24

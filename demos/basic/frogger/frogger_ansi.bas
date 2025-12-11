REM ====================================================================
REM ANSI Terminal Control Module
REM Provides color and cursor control functions using ANSI escape codes
REM ====================================================================

REM ANSI escape sequence prefix
DIM ESC AS STRING
ESC = CHR(27)

REM ANSI Color Codes
REM Usage: PRINT ESC; COLOR_GREEN; "text"; ESC; RESET
DIM RESET AS STRING
DIM COLOR_BLACK AS STRING
DIM COLOR_RED AS STRING
DIM COLOR_GREEN AS STRING
DIM COLOR_YELLOW AS STRING
DIM COLOR_BLUE AS STRING
DIM COLOR_MAGENTA AS STRING
DIM COLOR_CYAN AS STRING
DIM COLOR_WHITE AS STRING

RESET = "[0m"
COLOR_BLACK = "[30m"
COLOR_RED = "[31m"
COLOR_GREEN = "[32m"
COLOR_YELLOW = "[33m"
COLOR_BLUE = "[34m"
COLOR_MAGENTA = "[35m"
COLOR_CYAN = "[36m"
COLOR_WHITE = "[37m"

REM Screen buffer for double buffering (reduces flicker)
DIM SCREEN_BUFFER AS STRING

REM Clear the entire screen
SUB ClearScreen()
    PRINT ESC; "[2J"
END SUB

REM Use alternate screen buffer (prevents flicker)
SUB UseAltScreen()
    PRINT ESC; "[?1049h"
END SUB

REM Restore normal screen buffer
SUB UseNormalScreen()
    PRINT ESC; "[?1049l"
END SUB

REM Hide the cursor
SUB HideCursor()
    PRINT ESC; "[?25l"
END SUB

REM Show the cursor
SUB ShowCursor()
    PRINT ESC; "[?25h"
END SUB

REM Move cursor to specific row and column
SUB GotoXY(row AS INTEGER, col AS INTEGER)
    PRINT ESC; "["; STR$(row); ";"; STR$(col); "H";
END SUB

REM Print text at specific position
SUB PrintAt(row AS INTEGER, col AS INTEGER, text AS STRING)
    GotoXY(row, col)
    PRINT text
END SUB

REM Print colored text at specific position
SUB PrintColorAt(row AS INTEGER, col AS INTEGER, clr AS STRING, text AS STRING)
    GotoXY(row, col)
    PRINT ESC; clr; text; ESC; RESET;
END SUB

REM ====================================================================
REM Optimized Rendering Functions
REM ====================================================================

REM Start a new frame - just reposition cursor (no clear to avoid flicker)
SUB BeginFrame()
    PRINT ESC; "[H";
END SUB

REM Draw colored text at position (direct output)
SUB BufferColorAt(row AS INTEGER, col AS INTEGER, clr AS STRING, text AS STRING)
    PRINT ESC; "["; STR$(row); ";"; STR$(col); "H"; ESC; clr; text; ESC; RESET;
END SUB

REM Draw text at position (direct output)
SUB BufferAt(row AS INTEGER, col AS INTEGER, text AS STRING)
    PRINT ESC; "["; STR$(row); ";"; STR$(col); "H"; text;
END SUB

REM Flush frame (flush output buffer)
SUB FlushFrame()
    PRINT ""
END SUB

REM Helper: Build a string of repeated characters
FUNCTION RepeatChar(ch AS STRING, count AS INTEGER) AS STRING
    DIM result AS STRING
    DIM i AS INTEGER
    result = ""
    FOR i = 1 TO count
        result = result + ch
    NEXT i
    RepeatChar = result
END FUNCTION

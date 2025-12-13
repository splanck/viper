' ====================================================================
' ANSI Terminal Control Module for Monopoly
' Leverages Viper.Terminal for color and cursor control
' ====================================================================

' ANSI escape sequence prefix
DIM ESC AS STRING
ESC = CHR(27)

' ANSI Color Codes (foreground)
DIM RESET AS STRING
DIM FG_BLACK AS STRING
DIM FG_RED AS STRING
DIM FG_GREEN AS STRING
DIM FG_YELLOW AS STRING
DIM FG_BLUE AS STRING
DIM FG_MAGENTA AS STRING
DIM FG_CYAN AS STRING
DIM FG_WHITE AS STRING

' Bright colors
DIM FG_BRIGHT_RED AS STRING
DIM FG_BRIGHT_GREEN AS STRING
DIM FG_BRIGHT_YELLOW AS STRING
DIM FG_BRIGHT_BLUE AS STRING
DIM FG_BRIGHT_MAGENTA AS STRING
DIM FG_BRIGHT_CYAN AS STRING
DIM FG_BRIGHT_WHITE AS STRING

' Background colors
DIM BG_BLACK AS STRING
DIM BG_RED AS STRING
DIM BG_GREEN AS STRING
DIM BG_YELLOW AS STRING
DIM BG_BLUE AS STRING
DIM BG_MAGENTA AS STRING
DIM BG_CYAN AS STRING
DIM BG_WHITE AS STRING
DIM BG_BROWN AS STRING

RESET = "[0m"
FG_BLACK = "[30m"
FG_RED = "[31m"
FG_GREEN = "[32m"
FG_YELLOW = "[33m"
FG_BLUE = "[34m"
FG_MAGENTA = "[35m"
FG_CYAN = "[36m"
FG_WHITE = "[37m"

FG_BRIGHT_RED = "[91m"
FG_BRIGHT_GREEN = "[92m"
FG_BRIGHT_YELLOW = "[93m"
FG_BRIGHT_BLUE = "[94m"
FG_BRIGHT_MAGENTA = "[95m"
FG_BRIGHT_CYAN = "[96m"
FG_BRIGHT_WHITE = "[97m"

BG_BLACK = "[40m"
BG_RED = "[41m"
BG_GREEN = "[42m"
BG_YELLOW = "[43m"
BG_BLUE = "[44m"
BG_MAGENTA = "[45m"
BG_CYAN = "[46m"
BG_WHITE = "[47m"
BG_BROWN = "[43m"

' Bold and styles
DIM BOLD AS STRING
DIM DIM_TEXT AS STRING
DIM UNDERLINE AS STRING
DIM REVERSE_VID AS STRING

BOLD = "[1m"
DIM_TEXT = "[2m"
UNDERLINE = "[4m"
REVERSE_VID = "[7m"

' ====================================================================
' Terminal Control Functions
' ====================================================================

SUB ClearScreen()
    CLS
END SUB

SUB HideCursor()
    CURSOR OFF
END SUB

SUB ShowCursor()
    CURSOR ON
END SUB

SUB GotoXY(row AS INTEGER, col AS INTEGER)
    LOCATE row, col
END SUB

' Print text at specific position
SUB PrintAt(row AS INTEGER, col AS INTEGER, text AS STRING)
    LOCATE row, col
    PRINT text;
END SUB

' Print colored text
SUB PrintColor(clr AS STRING, text AS STRING)
    PRINT ESC; clr; text; ESC; RESET;
END SUB

SUB PrintColorAt(row AS INTEGER, col AS INTEGER, clr AS STRING, text AS STRING)
    LOCATE row, col
    PRINT ESC; clr; text; ESC; RESET;
END SUB

' Print with foreground and background colors
SUB PrintColorBgAt(row AS INTEGER, col AS INTEGER, fg AS STRING, bg AS STRING, text AS STRING)
    LOCATE row, col
    PRINT ESC; fg; ESC; bg; text; ESC; RESET;
END SUB

' Build repeated character string using Viper.String.Repeat
FUNCTION RepeatStr(ch AS STRING, count AS INTEGER) AS STRING
    IF count <= 0 THEN
        RepeatStr = ""
    ELSE
        RepeatStr = ch.Repeat(count)
    END IF
END FUNCTION

' Draw a horizontal line
SUB DrawHLine(row AS INTEGER, col AS INTEGER, length AS INTEGER, ch AS STRING)
    LOCATE row, col
    PRINT RepeatStr(ch, length);
END SUB

' Draw a vertical line
SUB DrawVLine(row AS INTEGER, col AS INTEGER, length AS INTEGER, ch AS STRING)
    DIM i AS INTEGER
    FOR i = 0 TO length - 1
        LOCATE row + i, col
        PRINT ch;
    NEXT i
END SUB

' Draw a box
SUB DrawBox(row AS INTEGER, col AS INTEGER, width AS INTEGER, height AS INTEGER)
    DIM i AS INTEGER

    ' Top border
    LOCATE row, col
    PRINT "+"; RepeatStr("-", width - 2); "+";

    ' Sides
    FOR i = 1 TO height - 2
        LOCATE row + i, col
        PRINT "|";
        LOCATE row + i, col + width - 1
        PRINT "|";
    NEXT i

    ' Bottom border
    LOCATE row + height - 1, col
    PRINT "+"; RepeatStr("-", width - 2); "+";
END SUB

' Draw a colored box
SUB DrawColorBox(row AS INTEGER, col AS INTEGER, width AS INTEGER, height AS INTEGER, fg AS STRING, bg AS STRING)
    DIM i AS INTEGER
    DIM spaces AS STRING
    spaces = RepeatStr(" ", width)

    FOR i = 0 TO height - 1
        LOCATE row + i, col
        PRINT ESC; fg; ESC; bg; spaces; ESC; RESET;
    NEXT i
END SUB

' Center text within a given width
FUNCTION CenterText(text AS STRING, width AS INTEGER) AS STRING
    DIM textLen AS INTEGER
    DIM leftPad AS INTEGER
    DIM rightPad AS INTEGER

    textLen = LEN(text)
    IF textLen >= width THEN
        CenterText = text.Left(width)
    ELSE
        leftPad = (width - textLen) / 2
        rightPad = width - textLen - leftPad
        CenterText = RepeatStr(" ", leftPad) + text + RepeatStr(" ", rightPad)
    END IF
END FUNCTION

' Right-align text
FUNCTION RightAlign(text AS STRING, width AS INTEGER) AS STRING
    DIM textLen AS INTEGER
    textLen = LEN(text)
    IF textLen >= width THEN
        RightAlign = text.Right(width)
    ELSE
        RightAlign = RepeatStr(" ", width - textLen) + text
    END IF
END FUNCTION

' Left-align text (pad right)
FUNCTION LeftAlign(text AS STRING, width AS INTEGER) AS STRING
    DIM textLen AS INTEGER
    textLen = LEN(text)
    IF textLen >= width THEN
        LeftAlign = text.Left(width)
    ELSE
        LeftAlign = text + RepeatStr(" ", width - textLen)
    END IF
END FUNCTION

' Wait for keypress and return the key
FUNCTION WaitKey() AS STRING
    DIM k AS STRING
    k = ""
    WHILE k = ""
        k = INKEY$()
        IF k = "" THEN
            SLEEP 20
        END IF
    WEND
    WaitKey = k
END FUNCTION

' Non-blocking key check
FUNCTION CheckKey() AS STRING
    CheckKey = INKEY$()
END FUNCTION

' Sleep for milliseconds
SUB WaitMs(ms AS INTEGER)
    SLEEP ms
END SUB

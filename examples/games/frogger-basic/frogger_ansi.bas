' ============================================================================
' MODULE: frogger_ansi.bas
' PURPOSE: Thin terminal-control layer. Wraps the raw BASIC primitives
'          (LOCATE, CLS, CURSOR, ALTSCREEN) in named helpers and provides a
'          set of string constants for the eight ANSI foreground colours.
'
' WHERE-THIS-FITS: Foundation module — every other frogger file includes
'          this first. The rendering code in frogger.bas and
'          frogger_scores.bas uses these helpers rather than emitting raw
'          escape sequences, which means you can change the rendering
'          backend (swap to a double-buffered strategy, add Windows
'          console support, etc.) by editing this one file.
'
' KEY-DESIGN-CHOICES:
'   * ANSI ESCAPE CODES AS STRING CONSTANTS. Each colour is a string like
'     "[32m" that callers concatenate with the ESC prefix (CHR(27)). The
'     caller-facing idiom is:
'         PRINT ESC; COLOR_GREEN; "text"; ESC; RESET
'     The `Buffer*` helpers fold the escape-sequence concatenation into a
'     single call so game code never handles the raw bytes.
'   * ALT-SCREEN + BATCH MODE. `UseAltScreen()` enables the VT100 alternate
'     screen buffer. Output in that mode is batched and flushed at the end
'     of each frame, which is what eliminates flicker during per-cell
'     redraws. `BeginFrame()` resets the cursor to (1,1) without clearing,
'     so the previous frame's buffer still provides a baseline to paint
'     over — this is the poor-man's equivalent of a double buffer.
'   * HIDE CURSOR. The default terminal cursor would appear at whatever
'     cell was last drawn, which in this game is constantly moving.
'     `HideCursor()` is called once in `GameLoop` to suppress it for the
'     entire session.
'   * NO FLUSH IN FlushFrame(). When ALTSCREEN batch mode is active the
'     runtime flushes at frame boundaries automatically. The function is
'     kept as a no-op so game code does not have to special-case the two
'     modes.
'
' HOW-TO-READ: Colour constants -> terminal controls -> positioned
'   printing -> batched rendering (Begin/Buffer/Flush). `RepeatChar` is a
'   small string utility that's convenient when drawing repeating
'   backgrounds.
' ============================================================================

' ANSI escape sequence prefix. Every escape code is written as
' "ESC + payload" because CHR(27) cannot be embedded in a string literal
' portably across BASIC dialects.
DIM ESC AS STRING
ESC = CHR(27)

' ANSI foreground colour codes. Each is the full sequence *after* ESC —
' callers prepend ESC themselves. The "[0m" RESET returns to the default
' colour and must be emitted at the end of each coloured segment to
' prevent colour bleed.
' Usage: PRINT ESC; COLOR_GREEN; "text"; ESC; RESET
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

' Reserved for a future double-buffering strategy. Currently unused.
DIM SCREEN_BUFFER AS STRING

' Clear the entire terminal and home the cursor. Called at scene
' transitions (e.g., menu <-> game <-> high scores).
SUB ClearScreen()
    CLS
END SUB

' Switch to the alternate screen buffer. This also enables the runtime's
' batch mode so per-character writes are coalesced into full-frame
' flushes, giving smooth scrolling instead of visible painting.
SUB UseAltScreen()
    ALTSCREEN ON
END SUB

' Return to the normal terminal buffer. Called before a "thanks for
' playing" message so it remains visible after the game exits.
SUB UseNormalScreen()
    ALTSCREEN OFF
END SUB

' Hide the text cursor so it doesn't flicker across the playfield as
' cells are repainted.
SUB HideCursor()
    CURSOR OFF
END SUB

' Restore the text cursor. Paired with HideCursor at game-over.
SUB ShowCursor()
    CURSOR ON
END SUB

' Move the cursor to (row, col). 1-based — (1, 1) is the top-left cell.
SUB GotoXY(row AS INTEGER, col AS INTEGER)
    LOCATE row, col
END SUB

' Place uncoloured text at (row, col). Thin wrapper around LOCATE + PRINT
' for callers who only need positioned output.
SUB PrintAt(row AS INTEGER, col AS INTEGER, text AS STRING)
    GotoXY(row, col)
    PRINT text
END SUB

' Place coloured text at (row, col). Equivalent to:
'   LOCATE row, col : PRINT ESC; clr; text; ESC; RESET
' Note the trailing semicolon after RESET — prevents an automatic newline
' so the cursor stays on the same line, allowing follow-up writes on the
' same row.
SUB PrintColorAt(row AS INTEGER, col AS INTEGER, clr AS STRING, text AS STRING)
    GotoXY(row, col)
    PRINT ESC; clr; text; ESC; RESET;
END SUB

' ============================================================================
' Buffered Rendering
' ----------------------------------------------------------------------------
' The game loop paints each frame in three phases:
'   1. BeginFrame()      - reset cursor, batch-mode is already active
'   2. BufferColorAt()/BufferAt() - accumulate all frame writes
'   3. FlushFrame()      - (no-op under batch mode) hand the frame to the
'                          runtime, which flushes atomically
' Under the hood this maps onto VT100 alt-screen batching. The old
' implementation built its own string buffer; that is now unnecessary
' because the runtime handles coalescing.
' ============================================================================

' Start a new frame. Moves the cursor to (1,1) without clearing so the
' previous frame's pixels provide a baseline — callers only have to
' repaint cells that are actually changing, which keeps per-frame work
' low.
SUB BeginFrame()
    LOCATE 1, 1
END SUB

' Paint a coloured cell at (row, col). Identical to PrintColorAt but
' named to match the buffered-rendering vocabulary so game code reads
' as a sequence of buffer ops between BeginFrame and FlushFrame.
SUB BufferColorAt(row AS INTEGER, col AS INTEGER, clr AS STRING, text AS STRING)
    LOCATE row, col
    PRINT ESC; clr; text; ESC; RESET;
END SUB

' Paint an uncoloured cell at (row, col).
SUB BufferAt(row AS INTEGER, col AS INTEGER, text AS STRING)
    LOCATE row, col
    PRINT text;
END SUB

' End-of-frame hook. Historically issued an explicit flush; under
' ALTSCREEN batch mode the runtime flushes at the end of each top-level
' I/O burst so no action is required. Left as a named no-op so callers
' don't have to change if batch-mode semantics ever change back.
SUB FlushFrame()
    ' No-op: the runtime flushes ALTSCREEN output at frame boundaries.
END SUB

' Build a string by repeating `ch` `count` times. Used for drawing
' full-width water / road rows in one shot. BASIC has no built-in STRING$
' in this dialect, so we write the helper ourselves.
FUNCTION RepeatChar(ch AS STRING, count AS INTEGER) AS STRING
    DIM result AS STRING
    DIM i AS INTEGER
    result = ""
    FOR i = 1 TO count
        result = result + ch
    NEXT i
    RepeatChar = result
END FUNCTION

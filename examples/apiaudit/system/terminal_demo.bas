' =============================================================================
' API Audit: Zanna.Terminal - Terminal I/O
' =============================================================================
' Tests: Say, SayInt, SayNum, SayBool, Print, PrintInt, PrintNum,
'        Bell, Clear, BeginBatch, EndBatch, Flush
' NOTE: Skip interactive input functions (TryAsk, AskResult, GetKey,
'       TryReadLine, ReadLineResult, ReadLine)
' =============================================================================

PRINT "=== API Audit: Zanna.Terminal ==="

' --- Say ---
PRINT "--- Say ---"
Zanna.Terminal.Say("Say prints a string with newline")
Zanna.Terminal.Say("Multiple Say calls")

' --- SayInt ---
PRINT "--- SayInt ---"
PRINT "SayInt(42):"
Zanna.Terminal.SayInt(42)
PRINT "SayInt(0):"
Zanna.Terminal.SayInt(0)
PRINT "SayInt(-100):"
Zanna.Terminal.SayInt(-100)

' --- SayNum ---
PRINT "--- SayNum ---"
PRINT "SayNum(3.14):"
Zanna.Terminal.SayNum(3.14)
PRINT "SayNum(0.0):"
Zanna.Terminal.SayNum(0.0)

' --- SayBool ---
PRINT "--- SayBool ---"
PRINT "SayBool(true):"
Zanna.Terminal.SayBool(TRUE)
PRINT "SayBool(false):"
Zanna.Terminal.SayBool(FALSE)

' --- Print (no newline) ---
PRINT "--- Print ---"
Zanna.Terminal.Print("Hello ")
Zanna.Terminal.Print("World")
PRINT

' --- PrintInt ---
PRINT "--- PrintInt ---"
Zanna.Terminal.PrintInt(123)
Zanna.Terminal.Print(" ")
Zanna.Terminal.PrintInt(456)
PRINT

' --- PrintNum ---
PRINT "--- PrintNum ---"
Zanna.Terminal.PrintNum(1.23)
Zanna.Terminal.Print(" ")
Zanna.Terminal.PrintNum(4.56)
PRINT

' --- BeginBatch / EndBatch ---
PRINT "--- BeginBatch / EndBatch ---"
Zanna.Terminal.BeginBatch()
PRINT "Inside batch 1"
PRINT "Inside batch 2"
Zanna.Terminal.EndBatch()
PRINT "Batch complete"

' --- Flush ---
PRINT "--- Flush ---"
Zanna.Terminal.Print("Before flush")
Zanna.Terminal.Flush()
PRINT
PRINT "Flush complete"

' NOTE: Skipping Bell and Clear to preserve output
' NOTE: Skipping TryAsk/AskResult/TryReadLine/ReadLineResult because this
'       audit demo must run unattended.
PRINT "(Skipping Bell, Clear, and interactive input to preserve output)"

PRINT "=== Terminal Demo Complete ==="
END

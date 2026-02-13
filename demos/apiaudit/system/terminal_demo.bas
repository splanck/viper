' =============================================================================
' API Audit: Viper.Terminal - Terminal I/O
' =============================================================================
' Tests: Say, SayInt, SayNum, SayBool, Print, PrintInt, PrintNum,
'        Bell, Clear, BeginBatch, EndBatch, Flush
' NOTE: Skip interactive input functions (Ask, GetKey, ReadLine)
' =============================================================================

PRINT "=== API Audit: Viper.Terminal ==="

' --- Say ---
PRINT "--- Say ---"
Viper.Terminal.Say("Say prints a string with newline")
Viper.Terminal.Say("Multiple Say calls")

' --- SayInt ---
PRINT "--- SayInt ---"
PRINT "SayInt(42):"
Viper.Terminal.SayInt(42)
PRINT "SayInt(0):"
Viper.Terminal.SayInt(0)
PRINT "SayInt(-100):"
Viper.Terminal.SayInt(-100)

' --- SayNum ---
PRINT "--- SayNum ---"
PRINT "SayNum(3.14):"
Viper.Terminal.SayNum(3.14)
PRINT "SayNum(0.0):"
Viper.Terminal.SayNum(0.0)

' --- SayBool ---
PRINT "--- SayBool ---"
PRINT "SayBool(true):"
Viper.Terminal.SayBool(TRUE)
PRINT "SayBool(false):"
Viper.Terminal.SayBool(FALSE)

' --- Print (no newline) ---
PRINT "--- Print ---"
Viper.Terminal.Print("Hello ")
Viper.Terminal.Print("World")
PRINT

' --- PrintInt ---
PRINT "--- PrintInt ---"
Viper.Terminal.PrintInt(123)
Viper.Terminal.Print(" ")
Viper.Terminal.PrintInt(456)
PRINT

' --- PrintNum ---
PRINT "--- PrintNum ---"
Viper.Terminal.PrintNum(1.23)
Viper.Terminal.Print(" ")
Viper.Terminal.PrintNum(4.56)
PRINT

' --- BeginBatch / EndBatch ---
PRINT "--- BeginBatch / EndBatch ---"
Viper.Terminal.BeginBatch()
PRINT "Inside batch 1"
PRINT "Inside batch 2"
Viper.Terminal.EndBatch()
PRINT "Batch complete"

' --- Flush ---
PRINT "--- Flush ---"
Viper.Terminal.Print("Before flush")
Viper.Terminal.Flush()
PRINT
PRINT "Flush complete"

' NOTE: Skipping Bell and Clear to preserve output
PRINT "(Skipping Bell and Clear to preserve output)"

PRINT "=== Terminal Demo Complete ==="
END

' Viper.Text.StringBuilder API Audit
' Tests all StringBuilder functions
' Note: Method-style calls (sb.Append) corrupt heap after multiple uses.
'       Use function-style calls (Viper.Text.StringBuilder.Append) instead.

PRINT "=== Viper.Text.StringBuilder API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM sb AS OBJECT
sb = Viper.Text.StringBuilder.New()

' --- Append ---
PRINT "--- Append ---"
Viper.Text.StringBuilder.Append(sb, "Hello")
Viper.Text.StringBuilder.Append(sb, " ")
Viper.Text.StringBuilder.Append(sb, "World")
PRINT Viper.Text.StringBuilder.ToString(sb)

' --- AppendLine ---
PRINT "--- AppendLine ---"
Viper.Text.StringBuilder.Clear(sb)
Viper.Text.StringBuilder.Append(sb, "Line 1")
Viper.Text.StringBuilder.AppendLine(sb, "")
Viper.Text.StringBuilder.Append(sb, "Line 2")
PRINT Viper.Text.StringBuilder.ToString(sb)

' --- get_Length ---
PRINT "--- get_Length ---"
Viper.Text.StringBuilder.Clear(sb)
Viper.Text.StringBuilder.Append(sb, "12345")
PRINT "Length: "; Viper.Text.StringBuilder.get_Length(sb)

' --- get_Capacity ---
PRINT "--- get_Capacity ---"
PRINT "Capacity: "; Viper.Text.StringBuilder.get_Capacity(sb)

' --- ToString ---
PRINT "--- ToString ---"
PRINT "ToString: "; Viper.Text.StringBuilder.ToString(sb)

' --- Clear ---
PRINT "--- Clear ---"
Viper.Text.StringBuilder.Clear(sb)
PRINT "Length after clear: "; Viper.Text.StringBuilder.get_Length(sb)
PRINT "ToString after clear: ["; Viper.Text.StringBuilder.ToString(sb); "]"

' Build a longer string
PRINT "--- Longer Build ---"
DIM sb2 AS OBJECT
sb2 = Viper.Text.StringBuilder.New()
Viper.Text.StringBuilder.Append(sb2, "The ")
Viper.Text.StringBuilder.Append(sb2, "quick ")
Viper.Text.StringBuilder.Append(sb2, "brown ")
Viper.Text.StringBuilder.Append(sb2, "fox ")
Viper.Text.StringBuilder.Append(sb2, "jumps.")
PRINT Viper.Text.StringBuilder.ToString(sb2)
PRINT "Length: "; Viper.Text.StringBuilder.get_Length(sb2)

PRINT "=== StringBuilder Demo Complete ==="
END

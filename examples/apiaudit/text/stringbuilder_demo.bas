' Zanna.Text.StringBuilder API Audit
' Tests all StringBuilder functions
' Note: Method-style calls (sb.Append) corrupt heap after multiple uses.
'       Use function-style calls (Zanna.Text.StringBuilder.Append) instead.

PRINT "=== Zanna.Text.StringBuilder API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM sb AS OBJECT
sb = Zanna.Text.StringBuilder.New()

' --- Append ---
PRINT "--- Append ---"
Zanna.Text.StringBuilder.Append(sb, "Hello")
Zanna.Text.StringBuilder.Append(sb, " ")
Zanna.Text.StringBuilder.Append(sb, "World")
PRINT Zanna.Text.StringBuilder.ToString(sb)

' --- AppendLine ---
PRINT "--- AppendLine ---"
Zanna.Text.StringBuilder.Clear(sb)
Zanna.Text.StringBuilder.Append(sb, "Line 1")
Zanna.Text.StringBuilder.AppendLine(sb, "")
Zanna.Text.StringBuilder.Append(sb, "Line 2")
PRINT Zanna.Text.StringBuilder.ToString(sb)

' --- get_Length ---
PRINT "--- get_Length ---"
Zanna.Text.StringBuilder.Clear(sb)
Zanna.Text.StringBuilder.Append(sb, "12345")
PRINT "Length: "; Zanna.Text.StringBuilder.get_Length(sb)

' --- get_Capacity ---
PRINT "--- get_Capacity ---"
PRINT "Capacity: "; Zanna.Text.StringBuilder.get_Capacity(sb)

' --- ToString ---
PRINT "--- ToString ---"
PRINT "ToString: "; Zanna.Text.StringBuilder.ToString(sb)

' --- Clear ---
PRINT "--- Clear ---"
Zanna.Text.StringBuilder.Clear(sb)
PRINT "Length after clear: "; Zanna.Text.StringBuilder.get_Length(sb)
PRINT "ToString after clear: ["; Zanna.Text.StringBuilder.ToString(sb); "]"

' Build a longer string
PRINT "--- Longer Build ---"
DIM sb2 AS OBJECT
sb2 = Zanna.Text.StringBuilder.New()
Zanna.Text.StringBuilder.Append(sb2, "The ")
Zanna.Text.StringBuilder.Append(sb2, "quick ")
Zanna.Text.StringBuilder.Append(sb2, "brown ")
Zanna.Text.StringBuilder.Append(sb2, "fox ")
Zanna.Text.StringBuilder.Append(sb2, "jumps.")
PRINT Zanna.Text.StringBuilder.ToString(sb2)
PRINT "Length: "; Zanna.Text.StringBuilder.get_Length(sb2)

PRINT "=== StringBuilder Demo Complete ==="
END

' Zanna.Text.Diff API Audit - Text Diffing
' Tests all Diff functions

PRINT "=== Zanna.Text.Diff API Audit ==="

DIM text1 AS STRING
DIM text2 AS STRING
text1 = "line1" + CHR(10) + "line2" + CHR(10) + "line3" + CHR(10) + "line4"
text2 = "line1" + CHR(10) + "modified" + CHR(10) + "line3" + CHR(10) + "line5"

' --- Lines ---
PRINT "--- Lines ---"
DIM changes AS OBJECT
changes = Zanna.Text.Diff.Lines(text1, text2)
PRINT Zanna.Data.Json.Format(changes)

' --- Unified ---
PRINT "--- Unified ---"
DIM unified AS STRING
unified = Zanna.Text.Diff.Unified(text1, text2, 3)
PRINT unified

' --- CountChanges ---
PRINT "--- CountChanges ---"
PRINT "Changes: "; Zanna.Text.Diff.CountChanges(text1, text2)

' Identical texts
PRINT "Identical: "; Zanna.Text.Diff.CountChanges("same", "same")

' Completely different
PRINT "Different: "; Zanna.Text.Diff.CountChanges("aaa", "bbb")

' --- Patch ---
PRINT "--- Patch ---"
DIM changes2 AS OBJECT
changes2 = Zanna.Text.Diff.Lines(text1, text2)
DIM patched AS STRING
patched = Zanna.Text.Diff.Patch(text1, changes2)
PRINT patched

' Edge cases
PRINT "--- Edge Cases ---"
PRINT "Empty to something: "; Zanna.Text.Diff.CountChanges("", "hello")
PRINT "Something to empty: "; Zanna.Text.Diff.CountChanges("hello", "")
PRINT "Both empty: "; Zanna.Text.Diff.CountChanges("", "")

PRINT "=== Diff Demo Complete ==="
END

' Viper.Text.Diff API Audit - Text Diffing
' Tests all Diff functions

PRINT "=== Viper.Text.Diff API Audit ==="

DIM text1 AS STRING
DIM text2 AS STRING
text1 = "line1" + CHR(10) + "line2" + CHR(10) + "line3" + CHR(10) + "line4"
text2 = "line1" + CHR(10) + "modified" + CHR(10) + "line3" + CHR(10) + "line5"

' --- Lines ---
PRINT "--- Lines ---"
DIM changes AS OBJECT
changes = Viper.Text.Diff.Lines(text1, text2)
PRINT Viper.Text.Json.Format(changes)

' --- Unified ---
PRINT "--- Unified ---"
DIM unified AS STRING
unified = Viper.Text.Diff.Unified(text1, text2, 3)
PRINT unified

' --- CountChanges ---
PRINT "--- CountChanges ---"
PRINT "Changes: "; Viper.Text.Diff.CountChanges(text1, text2)

' Identical texts
PRINT "Identical: "; Viper.Text.Diff.CountChanges("same", "same")

' Completely different
PRINT "Different: "; Viper.Text.Diff.CountChanges("aaa", "bbb")

' --- Patch ---
PRINT "--- Patch ---"
DIM changes2 AS OBJECT
changes2 = Viper.Text.Diff.Lines(text1, text2)
DIM patched AS STRING
patched = Viper.Text.Diff.Patch(text1, changes2)
PRINT patched

' Edge cases
PRINT "--- Edge Cases ---"
PRINT "Empty to something: "; Viper.Text.Diff.CountChanges("", "hello")
PRINT "Something to empty: "; Viper.Text.Diff.CountChanges("hello", "")
PRINT "Both empty: "; Viper.Text.Diff.CountChanges("", "")

PRINT "=== Diff Demo Complete ==="
END

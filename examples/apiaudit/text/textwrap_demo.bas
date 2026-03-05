' Viper.Text.TextWrapper API Audit - Text Wrapping and Formatting
' Tests all TextWrapper functions

PRINT "=== Viper.Text.TextWrapper API Audit ==="

DIM longText AS STRING
longText = "The quick brown fox jumps over the lazy dog. This is a longer sentence that should be wrapped at the specified width."

' --- Wrap ---
PRINT "--- Wrap ---"
PRINT Viper.Text.TextWrapper.Wrap(longText, 40)

' --- WrapLines ---
PRINT "--- WrapLines ---"
DIM lines AS Viper.Collections.Seq
lines = Viper.Text.TextWrapper.WrapLines(longText, 40)
PRINT "Line count: "; lines.Len
' Note: Accessing lines.Get(N) crashes due to known heap corruption bug
' with interim objects after many function calls.

' --- Fill ---
PRINT "--- Fill ---"
PRINT Viper.Text.TextWrapper.Fill(longText, 40)

' --- Indent ---
PRINT "--- Indent ---"
DIM txt AS STRING
txt = "line1" + CHR(10) + "line2" + CHR(10) + "line3"
PRINT Viper.Text.TextWrapper.Indent(txt, "  ")
PRINT Viper.Text.TextWrapper.Indent(txt, ">>> ")

' --- Dedent ---
PRINT "--- Dedent ---"
DIM indented AS STRING
indented = "    line1" + CHR(10) + "    line2" + CHR(10) + "    line3"
PRINT Viper.Text.TextWrapper.Dedent(indented)

' --- Hang ---
PRINT "--- Hang ---"
DIM paragraph AS STRING
paragraph = "This is the first line of a paragraph that will have a hanging indent applied to it for demonstration."
PRINT Viper.Text.TextWrapper.Hang(paragraph, "    ")

' --- Truncate ---
PRINT "--- Truncate ---"
PRINT Viper.Text.TextWrapper.Truncate("Hello, World! This is a long string.", 20)
PRINT Viper.Text.TextWrapper.Truncate("Short", 20)

' --- TruncateWith ---
PRINT "--- TruncateWith ---"
PRINT Viper.Text.TextWrapper.TruncateWith("Hello, World! This is a long string.", 20, " [...]")
PRINT Viper.Text.TextWrapper.TruncateWith("Short", 20, " [...]")

' --- Shorten ---
PRINT "--- Shorten ---"
PRINT Viper.Text.TextWrapper.Shorten("Hello, World! This is a long string.", 20)

' --- Left / Right / Center ---
PRINT "--- Left / Right / Center ---"
PRINT "["; Viper.Text.TextWrapper.Left("hello", 20); "]"
PRINT "["; Viper.Text.TextWrapper.Right("hello", 20); "]"
PRINT "["; Viper.Text.TextWrapper.Center("hello", 20); "]"

' --- LineCount ---
PRINT "--- LineCount ---"
PRINT "3 lines: "; Viper.Text.TextWrapper.LineCount("one" + CHR(10) + "two" + CHR(10) + "three")
PRINT "1 line: "; Viper.Text.TextWrapper.LineCount("single")
PRINT "empty: "; Viper.Text.TextWrapper.LineCount("")

' --- MaxLineLen ---
PRINT "--- MaxLineLen ---"
PRINT "Max len: "; Viper.Text.TextWrapper.MaxLineLen("short" + CHR(10) + "a much longer line" + CHR(10) + "med")
PRINT "Single: "; Viper.Text.TextWrapper.MaxLineLen("single")

PRINT "=== TextWrapper Demo Complete ==="
END

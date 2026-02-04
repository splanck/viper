' Edge case testing for String operations
' Looking for crashes, incorrect results, or unexpected behavior

DIM result AS STRING
DIM num AS INTEGER
DIM flag AS INTEGER

' === Empty string tests ===
PRINT "=== Empty String Tests ==="

' String.Length on empty
num = Viper.String.Length("")
PRINT "Length(''): "; num
IF num <> 0 THEN PRINT "BUG: Length('') should be 0"

' String.ToUpper/ToLower on empty
result = Viper.String.ToUpper("")
PRINT "ToUpper(''): '"; result; "'"

result = Viper.String.ToLower("")
PRINT "ToLower(''): '"; result; "'"

' String.Trim on empty
result = Viper.String.Trim("")
PRINT "Trim(''): '"; result; "'"

' String.Flip on empty
result = Viper.String.Flip("")
PRINT "Flip(''): '"; result; "'"

' String.Left/Right on empty
result = Viper.String.Left("", 5)
PRINT "Left('', 5): '"; result; "'"

result = Viper.String.Right("", 5)
PRINT "Right('', 5): '"; result; "'"

' String.Substring on empty
result = Viper.String.Substring("", 0, 5)
PRINT "Substring('', 0, 5): '"; result; "'"

' String.Split on empty - POTENTIAL BUG: returns 1 element, should be 0?
PRINT "Split('', ','): testing..."
DIM parts AS Viper.Collections.Seq
parts = Viper.String.Split("", ",")
PRINT "Split('', ',') length: "; parts.Len
IF parts.Len = 1 THEN PRINT "NOTE: Split('', ',') returns 1 element (empty string) - is this correct?"

' Viper.String.Join with empty seq
DIM emptySeq AS Viper.Collections.Seq
emptySeq = Viper.Collections.Seq.New()
result = Viper.String.Join(",", emptySeq)
PRINT "Join(',', emptySeq): '"; result; "'"

' === Zero index tests (valid) ===
PRINT ""
PRINT "=== Zero/Valid Index Tests ==="

result = Viper.String.Left("hello", 0)
PRINT "Left('hello', 0): '"; result; "'"

result = Viper.String.Right("hello", 0)
PRINT "Right('hello', 0): '"; result; "'"

result = Viper.String.Substring("hello", 0, 0)
PRINT "Substring('hello', 0, 0): '"; result; "'"

' === Out of bounds tests ===
PRINT ""
PRINT "=== Out of Bounds Tests ==="

result = Viper.String.Left("hi", 100)
PRINT "Left('hi', 100): '"; result; "'"

result = Viper.String.Right("hi", 100)
PRINT "Right('hi', 100): '"; result; "'"

result = Viper.String.Substring("hi", 0, 100)
PRINT "Substring('hi', 0, 100): '"; result; "'"

result = Viper.String.Substring("hi", 50, 10)
PRINT "Substring('hi', 50, 10): '"; result; "'"

' === IndexOf edge cases ===
PRINT ""
PRINT "=== IndexOf Tests ==="

num = Viper.String.IndexOf("hello", "")
PRINT "IndexOf('hello', ''): "; num

num = Viper.String.IndexOf("", "x")
PRINT "IndexOf('', 'x'): "; num

num = Viper.String.IndexOf("", "")
PRINT "IndexOf('', ''): "; num

' === Has (Contains) edge cases ===
PRINT ""
PRINT "=== Has Tests ==="

flag = Viper.String.Has("hello", "")
PRINT "Has('hello', ''): "; flag

flag = Viper.String.Has("", "x")
PRINT "Has('', 'x'): "; flag

flag = Viper.String.Has("", "")
PRINT "Has('', ''): "; flag

' === Replace edge cases ===
PRINT ""
PRINT "=== Replace Tests ==="

result = Viper.String.Replace("hello", "", "x")
PRINT "Replace('hello', '', 'x'): '"; result; "'"

result = Viper.String.Replace("", "x", "y")
PRINT "Replace('', 'x', 'y'): '"; result; "'"

result = Viper.String.Replace("hello", "l", "")
PRINT "Replace('hello', 'l', ''): '"; result; "'"

' === Repeat edge cases ===
PRINT ""
PRINT "=== Repeat Tests ==="

result = Viper.String.Repeat("x", 0)
PRINT "Repeat('x', 0): '"; result; "'"

result = Viper.String.Repeat("", 5)
PRINT "Repeat('', 5): '"; result; "'"

' === PadLeft/PadRight edge cases ===
PRINT ""
PRINT "=== Pad Tests ==="

result = Viper.String.PadLeft("hi", 1, "x")
PRINT "PadLeft('hi', 1, 'x'): '"; result; "'"

result = Viper.String.PadRight("hi", 1, "x")
PRINT "PadRight('hi', 1, 'x'): '"; result; "'"

result = Viper.String.PadLeft("hi", 5, "")
PRINT "PadLeft('hi', 5, ''): '"; result; "'"

' === Count edge cases ===
PRINT ""
PRINT "=== Count Tests ==="

num = Viper.String.Count("hello", "l")
PRINT "Count('hello', 'l'): "; num

num = Viper.String.Count("hello", "")
PRINT "Count('hello', ''): "; num

num = Viper.String.Count("", "x")
PRINT "Count('', 'x'): "; num

' === StartsWith/EndsWith edge cases ===
PRINT ""
PRINT "=== StartsWith/EndsWith Tests ==="

flag = Viper.String.StartsWith("hello", "")
PRINT "StartsWith('hello', ''): "; flag

flag = Viper.String.EndsWith("hello", "")
PRINT "EndsWith('hello', ''): "; flag

flag = Viper.String.StartsWith("", "x")
PRINT "StartsWith('', 'x'): "; flag

flag = Viper.String.EndsWith("", "x")
PRINT "EndsWith('', 'x'): "; flag

flag = Viper.String.StartsWith("", "")
PRINT "StartsWith('', ''): "; flag

flag = Viper.String.EndsWith("", "")
PRINT "EndsWith('', ''): "; flag

PRINT ""
PRINT "=== String Edge Case Tests Complete ==="
END

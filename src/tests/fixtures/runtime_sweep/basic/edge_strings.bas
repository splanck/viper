' Edge case testing for String operations
' Looking for crashes, incorrect results, or unexpected behavior

DIM result AS STRING
DIM num AS INTEGER
DIM flag AS INTEGER

' === Empty string tests ===
PRINT "=== Empty String Tests ==="

' String.Length on empty
num = Zanna.String.get_Length("")
PRINT "Length(''): "; num
IF num <> 0 THEN PRINT "BUG: Length('') should be 0"

' String.ToUpper/ToLower on empty
result = Zanna.String.ToUpper("")
PRINT "ToUpper(''): '"; result; "'"

result = Zanna.String.ToLower("")
PRINT "ToLower(''): '"; result; "'"

' String.Trim on empty
result = Zanna.String.Trim("")
PRINT "Trim(''): '"; result; "'"

' String.Flip on empty
result = Zanna.String.Reverse("")
PRINT "Flip(''): '"; result; "'"

' String.Left/Right on empty
result = Zanna.String.Left("", 5)
PRINT "Left('', 5): '"; result; "'"

result = Zanna.String.Right("", 5)
PRINT "Right('', 5): '"; result; "'"

' String.Substring on empty
result = Zanna.String.Substring("", 0, 5)
PRINT "Substring('', 0, 5): '"; result; "'"

' String.Split on empty - POTENTIAL BUG: returns 1 element, should be 0?
PRINT "Split('', ','): testing..."
DIM parts AS Zanna.Collections.Seq
parts = Zanna.String.Split("", ",")
PRINT "Split('', ',') length: "; parts.Count
IF parts.Count = 1 THEN PRINT "NOTE: Split('', ',') returns 1 element (empty string) - is this correct?"

' Zanna.String.Join with empty seq
DIM emptySeq AS Zanna.Collections.Seq
emptySeq = Zanna.Collections.Seq.New()
result = Zanna.String.Join(",", emptySeq)
PRINT "Join(',', emptySeq): '"; result; "'"

' === Zero index tests (valid) ===
PRINT ""
PRINT "=== Zero/Valid Index Tests ==="

result = Zanna.String.Left("hello", 0)
PRINT "Left('hello', 0): '"; result; "'"

result = Zanna.String.Right("hello", 0)
PRINT "Right('hello', 0): '"; result; "'"

result = Zanna.String.Substring("hello", 0, 0)
PRINT "Substring('hello', 0, 0): '"; result; "'"

' === Out of bounds tests ===
PRINT ""
PRINT "=== Out of Bounds Tests ==="

result = Zanna.String.Left("hi", 100)
PRINT "Left('hi', 100): '"; result; "'"

result = Zanna.String.Right("hi", 100)
PRINT "Right('hi', 100): '"; result; "'"

result = Zanna.String.Substring("hi", 0, 100)
PRINT "Substring('hi', 0, 100): '"; result; "'"

result = Zanna.String.Substring("hi", 50, 10)
PRINT "Substring('hi', 50, 10): '"; result; "'"

' === IndexOf edge cases ===
PRINT ""
PRINT "=== IndexOf Tests ==="

num = Zanna.String.IndexOf("hello", "")
PRINT "IndexOf('hello', ''): "; num

num = Zanna.String.IndexOf("", "x")
PRINT "IndexOf('', 'x'): "; num

num = Zanna.String.IndexOf("", "")
PRINT "IndexOf('', ''): "; num

' === Has (Contains) edge cases ===
PRINT ""
PRINT "=== Has Tests ==="

flag = Zanna.String.Contains("hello", "")
PRINT "Has('hello', ''): "; flag

flag = Zanna.String.Contains("", "x")
PRINT "Has('', 'x'): "; flag

flag = Zanna.String.Contains("", "")
PRINT "Has('', ''): "; flag

' === Replace edge cases ===
PRINT ""
PRINT "=== Replace Tests ==="

result = Zanna.String.Replace("hello", "", "x")
PRINT "Replace('hello', '', 'x'): '"; result; "'"

result = Zanna.String.Replace("", "x", "y")
PRINT "Replace('', 'x', 'y'): '"; result; "'"

result = Zanna.String.Replace("hello", "l", "")
PRINT "Replace('hello', 'l', ''): '"; result; "'"

' === Repeat edge cases ===
PRINT ""
PRINT "=== Repeat Tests ==="

result = Zanna.String.Repeat("x", 0)
PRINT "Repeat('x', 0): '"; result; "'"

result = Zanna.String.Repeat("", 5)
PRINT "Repeat('', 5): '"; result; "'"

' === PadLeft/PadRight edge cases ===
PRINT ""
PRINT "=== Pad Tests ==="

result = Zanna.String.PadLeft("hi", 1, "x")
PRINT "PadLeft('hi', 1, 'x'): '"; result; "'"

result = Zanna.String.PadRight("hi", 1, "x")
PRINT "PadRight('hi', 1, 'x'): '"; result; "'"

result = Zanna.String.PadLeft("hi", 5, "")
PRINT "PadLeft('hi', 5, ''): '"; result; "'"

' === Count edge cases ===
PRINT ""
PRINT "=== Count Tests ==="

num = Zanna.String.Count("hello", "l")
PRINT "Count('hello', 'l'): "; num

num = Zanna.String.Count("hello", "")
PRINT "Count('hello', ''): "; num

num = Zanna.String.Count("", "x")
PRINT "Count('', 'x'): "; num

' === StartsWith/EndsWith edge cases ===
PRINT ""
PRINT "=== StartsWith/EndsWith Tests ==="

flag = Zanna.String.StartsWith("hello", "")
PRINT "StartsWith('hello', ''): "; flag

flag = Zanna.String.EndsWith("hello", "")
PRINT "EndsWith('hello', ''): "; flag

flag = Zanna.String.StartsWith("", "x")
PRINT "StartsWith('', 'x'): "; flag

flag = Zanna.String.EndsWith("", "x")
PRINT "EndsWith('', 'x'): "; flag

flag = Zanna.String.StartsWith("", "")
PRINT "StartsWith('', ''): "; flag

flag = Zanna.String.EndsWith("", "")
PRINT "EndsWith('', ''): "; flag

PRINT ""
PRINT "=== String Edge Case Tests Complete ==="
END

' Edge case testing for String operations with unicode and long strings

DIM result AS STRING
DIM num AS INTEGER

' === Unicode handling ===
PRINT "=== Unicode Handling ==="

DIM unicodeStr AS STRING
unicodeStr = "Hello, 世界! 🌍 Привет мир"
PRINT "Unicode string: "; unicodeStr
PRINT "Length: "; Zanna.String.get_Length(unicodeStr)

result = Zanna.String.ToUpper(unicodeStr)
PRINT "ToUpper: "; result

result = Zanna.String.ToLower(unicodeStr)
PRINT "ToLower: "; result

result = Zanna.String.Reverse(unicodeStr)
PRINT "Flip: "; result

num = Zanna.String.IndexOf(unicodeStr, "世界")
PRINT "IndexOf('世界'): "; num

num = Zanna.String.Contains(unicodeStr, "🌍")
PRINT "Has('🌍'): "; num

result = Zanna.String.Replace(unicodeStr, "世界", "Earth")
PRINT "Replace('世界','Earth'): "; result
PRINT ""

' === Very long strings ===
PRINT "=== Very Long Strings ==="

DIM longStr AS STRING
longStr = Zanna.String.Repeat("x", 1000000)
PRINT "Created 1MB string of 'x'"
PRINT "Length: "; Zanna.String.get_Length(longStr)

result = Zanna.String.Left(longStr, 10)
PRINT "Left(10): "; result

result = Zanna.String.Right(longStr, 10)
PRINT "Right(10): "; result

result = Zanna.String.Substring(longStr, 500000, 10)
PRINT "Substring(500000, 10): "; result

num = Zanna.String.IndexOf(longStr, "x")
PRINT "IndexOf('x'): "; num

' Search for something not there
num = Zanna.String.IndexOf(longStr, "y")
PRINT "IndexOf('y'): "; num
PRINT ""

' === String concatenation stress ===
PRINT "=== Concatenation Stress ==="
DIM sb AS Zanna.Text.StringBuilder
sb = Zanna.Text.StringBuilder.New()
DIM i AS INTEGER
FOR i = 1 TO 10000
    sb.Append("x")
NEXT i
PRINT "Built 10000-char string via StringBuilder"
PRINT "Length: "; sb.Length
PRINT ""

' === Newlines and special chars ===
PRINT "=== Special Characters ==="
DIM specialStr AS STRING
specialStr = "Line1" + CHR$(10) + "Line2" + CHR$(13) + "Line3" + CHR$(9) + "Tab"
PRINT "String with newlines/tabs:"
PRINT specialStr
PRINT ""
PRINT "Length: "; Zanna.String.get_Length(specialStr)

result = Zanna.String.Trim(specialStr)
PRINT "After Trim, Length: "; Zanna.String.get_Length(result)
PRINT ""

' === Split with unicode delimiter ===
PRINT "=== Split with Unicode ==="
DIM csvUnicode AS STRING
csvUnicode = "one→two→three"
DIM parts AS Zanna.Collections.Seq
parts = Zanna.String.Split(csvUnicode, "→")
PRINT "Split 'one→two→three' by '→'"
PRINT "Part count: "; parts.Count

PRINT ""
PRINT "=== Unicode/Long String Tests Complete ==="
END

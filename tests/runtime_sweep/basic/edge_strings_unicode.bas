' Edge case testing for String operations with unicode and long strings

DIM result AS STRING
DIM num AS INTEGER

' === Unicode handling ===
PRINT "=== Unicode Handling ==="

DIM unicodeStr AS STRING
unicodeStr = "Hello, 世界! 🌍 Привет мир"
PRINT "Unicode string: "; unicodeStr
PRINT "Length: "; Viper.String.Length(unicodeStr)

result = Viper.String.ToUpper(unicodeStr)
PRINT "ToUpper: "; result

result = Viper.String.ToLower(unicodeStr)
PRINT "ToLower: "; result

result = Viper.String.Flip(unicodeStr)
PRINT "Flip: "; result

num = Viper.String.IndexOf(unicodeStr, "世界")
PRINT "IndexOf('世界'): "; num

num = Viper.String.Has(unicodeStr, "🌍")
PRINT "Has('🌍'): "; num

result = Viper.String.Replace(unicodeStr, "世界", "Earth")
PRINT "Replace('世界','Earth'): "; result
PRINT ""

' === Very long strings ===
PRINT "=== Very Long Strings ==="

DIM longStr AS STRING
longStr = Viper.String.Repeat("x", 1000000)
PRINT "Created 1MB string of 'x'"
PRINT "Length: "; Viper.String.Length(longStr)

result = Viper.String.Left(longStr, 10)
PRINT "Left(10): "; result

result = Viper.String.Right(longStr, 10)
PRINT "Right(10): "; result

result = Viper.String.Substring(longStr, 500000, 10)
PRINT "Substring(500000, 10): "; result

num = Viper.String.IndexOf(longStr, "x")
PRINT "IndexOf('x'): "; num

' Search for something not there
num = Viper.String.IndexOf(longStr, "y")
PRINT "IndexOf('y'): "; num
PRINT ""

' === String concatenation stress ===
PRINT "=== Concatenation Stress ==="
DIM sb AS Viper.Text.StringBuilder
sb = Viper.Text.StringBuilder.New()
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
PRINT "Length: "; Viper.String.Length(specialStr)

result = Viper.String.Trim(specialStr)
PRINT "After Trim, Length: "; Viper.String.Length(result)
PRINT ""

' === Split with unicode delimiter ===
PRINT "=== Split with Unicode ==="
DIM csvUnicode AS STRING
csvUnicode = "one→two→three"
DIM parts AS Viper.Collections.Seq
parts = Viper.String.Split(csvUnicode, "→")
PRINT "Split 'one→two→three' by '→'"
PRINT "Part count: "; parts.Length

PRINT ""
PRINT "=== Unicode/Long String Tests Complete ==="
END

' Testing String.Length behavior with unicode

DIM s AS STRING
DIM len AS INTEGER

PRINT "=== String.Length Unicode Test ==="

' ASCII only
s = "Hello"
len = Viper.String.Length(s)
PRINT "Length('Hello'): "; len; " (expected: 5)"

' Cyrillic (2 bytes per char)
s = "Привет"
len = Viper.String.Length(s)
PRINT "Length('Привет'): "; len; " (expected: 6 chars, actual bytes: 12)"

' Chinese (3 bytes per char)
s = "世界"
len = Viper.String.Length(s)
PRINT "Length('世界'): "; len; " (expected: 2 chars, actual bytes: 6)"

' Emoji (4 bytes)
s = "🌍"
len = Viper.String.Length(s)
PRINT "Length('🌍'): "; len; " (expected: 1 char, actual bytes: 4)"

' Mixed
s = "A世🌍"
len = Viper.String.Length(s)
PRINT "Length('A世🌍'): "; len; " (expected: 3 chars, actual bytes: 8)"

PRINT ""
PRINT "=== String.Length Test Complete ==="
END

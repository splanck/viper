' bytes_demo.bas - Comprehensive API audit for Viper.Collections.Bytes
' Tests: New, Get, Set, Len, Fill, Find, Clone, Slice, Copy,
'        ToHex, FromHex, ToBase64, FromBase64, ToStr, FromStr

PRINT "=== Bytes API Audit ==="

' --- New ---
PRINT "--- New ---"
DIM b AS Viper.Collections.Bytes
b = Viper.Collections.Bytes.New(4)
PRINT b.Len           ' 4

' --- Set / Get ---
PRINT "--- Set / Get ---"
b.Set(0, 72)   ' H
b.Set(1, 101)  ' e
b.Set(2, 108)  ' l
b.Set(3, 108)  ' l
PRINT b.Get(0)        ' 72
PRINT b.Get(1)        ' 101
PRINT b.Get(2)        ' 108
PRINT b.Get(3)        ' 108

' --- FromStr / ToStr ---
PRINT "--- FromStr / ToStr ---"
DIM hello AS Viper.Collections.Bytes
hello = Viper.Collections.Bytes.FromStr("Hello")
PRINT hello.Len       ' 5
PRINT hello.ToStr()   ' Hello
PRINT hello.Get(0)    ' 72

' --- ToHex ---
PRINT "--- ToHex ---"
PRINT hello.ToHex()   ' 48656c6c6f

' --- FromHex ---
PRINT "--- FromHex ---"
DIM hex AS Viper.Collections.Bytes
hex = Viper.Collections.Bytes.FromHex("deadbeef")
PRINT hex.Len         ' 4
PRINT hex.Get(0)      ' 222
PRINT hex.Get(3)      ' 239

' --- ToBase64 ---
PRINT "--- ToBase64 ---"
PRINT hello.ToBase64()   ' SGVsbG8=

' --- FromBase64 ---
PRINT "--- FromBase64 ---"
DIM decoded AS Viper.Collections.Bytes
decoded = Viper.Collections.Bytes.FromBase64("SGVsbG8=")
PRINT decoded.ToStr()    ' Hello
PRINT decoded.Len        ' 5

' --- Fill ---
PRINT "--- Fill ---"
DIM fill AS Viper.Collections.Bytes
fill = Viper.Collections.Bytes.New(4)
fill.Fill(255)
PRINT fill.Get(0)        ' 255
PRINT fill.Get(3)        ' 255
PRINT fill.ToHex()       ' ffffffff

' --- Find ---
PRINT "--- Find ---"
PRINT hello.Find(108)    ' 2
PRINT hello.Find(111)    ' 4
PRINT hello.Find(99)     ' -1

' --- Clone ---
PRINT "--- Clone ---"
DIM clone AS Viper.Collections.Bytes
clone = hello.Clone()
PRINT clone.Len          ' 5
PRINT clone.ToStr()      ' Hello
clone.Set(0, 74)
PRINT clone.ToStr()      ' Jello
PRINT hello.ToStr()      ' Hello (unchanged)

' --- Slice ---
PRINT "--- Slice ---"
DIM sl AS Viper.Collections.Bytes
sl = hello.Slice(1, 4)
PRINT sl.Len             ' 3
PRINT sl.ToStr()         ' ell

' --- Copy ---
PRINT "--- Copy ---"
DIM dst AS Viper.Collections.Bytes
dst = Viper.Collections.Bytes.New(10)
dst.Fill(0)
dst.Copy(2, hello, 0, 5)
PRINT dst.Get(0)         ' 0
PRINT dst.Get(1)         ' 0
PRINT dst.Get(2)         ' 72
PRINT dst.Get(6)         ' 111

PRINT "=== Bytes audit complete ==="
END

' stream_demo.bas
PRINT "=== Viper.IO.Stream Demo ==="
DIM s AS OBJECT
s = Viper.IO.Stream.OpenMemory()
PRINT s.Pos
PRINT s.Len
s.WriteByte(65)
s.WriteByte(66)
PRINT s.Len
s.Close()
PRINT "done"
END

' API Audit: Viper.IO.Stream (BASIC)
PRINT "=== API Audit: Viper.IO.Stream ==="

' --- OpenMemory ---
PRINT "--- OpenMemory ---"
DIM s1 AS OBJECT = Viper.IO.Stream.OpenMemory()
PRINT s1.Type
PRINT s1.Pos
PRINT s1.Len

' --- WriteByte / ReadByte ---
PRINT "--- WriteByte / ReadByte ---"
s1.WriteByte(65)
s1.WriteByte(66)
s1.WriteByte(67)
PRINT s1.Len
s1.Pos = 0
PRINT s1.ReadByte()
PRINT s1.ReadByte()
PRINT s1.ReadByte()

' --- Write / Read ---
PRINT "--- Write / Read ---"
DIM s2 AS OBJECT = Viper.IO.Stream.OpenMemory()
DIM data AS OBJECT = Viper.Collections.Bytes.New(4)
data.Set(0, 72)
data.Set(1, 73)
data.Set(2, 33)
data.Set(3, 10)
s2.Write(data)
PRINT s2.Len
s2.Pos = 0
DIM rd AS OBJECT = s2.Read(4)
PRINT Viper.Collections.Bytes.Get(rd, 0)
PRINT Viper.Collections.Bytes.Get(rd, 1)

' --- Eof ---
PRINT "--- Eof ---"
PRINT s2.Eof
s2.Pos = 0
PRINT s2.Eof

' --- ReadAll ---
PRINT "--- ReadAll ---"
s2.Pos = 0
DIM all_data AS OBJECT = s2.ReadAll()
PRINT all_data.Len

' --- ToBytes ---
PRINT "--- ToBytes ---"
DIM b AS OBJECT = s1.ToBytes()
PRINT b.Len

' --- Flush / Close ---
PRINT "--- Flush / Close ---"
s1.Flush()
s1.Close()
s2.Close()
PRINT "Closed all streams"

PRINT "=== Stream Audit Complete ==="
END

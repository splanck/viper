' =============================================================================
' API Audit: Viper.IO.MemStream - In-Memory Stream
' =============================================================================
' Tests: New, NewCapacity, WriteI8, ReadI8, WriteI32, ReadI32, WriteI64, ReadI64,
'        WriteStr, ReadStr, WriteBytes, ReadBytes, ToBytes, Pos, Len, Capacity,
'        Seek, Skip, Clear
' =============================================================================

PRINT "=== API Audit: Viper.IO.MemStream ==="

' --- New ---
PRINT "--- New ---"
DIM ms AS OBJECT
ms = Viper.IO.MemStream.New()
PRINT "New() created"

' --- Initial state ---
PRINT "--- Initial state ---"
PRINT "Pos: "; Viper.IO.MemStream.get_Pos(ms)
PRINT "Len: "; Viper.IO.MemStream.get_Len(ms)
PRINT "Capacity: "; Viper.IO.MemStream.get_Capacity(ms)

' --- WriteI8 / ReadI8 ---
PRINT "--- WriteI8 / ReadI8 ---"
Viper.IO.MemStream.WriteI8(ms, 42)
Viper.IO.MemStream.WriteI8(ms, -7)
PRINT "Wrote 2 I8 values"
Viper.IO.MemStream.Seek(ms, 0)
PRINT "ReadI8: "; Viper.IO.MemStream.ReadI8(ms)
PRINT "ReadI8: "; Viper.IO.MemStream.ReadI8(ms)

' --- WriteI32 / ReadI32 ---
PRINT "--- WriteI32 / ReadI32 ---"
Viper.IO.MemStream.Clear(ms)
Viper.IO.MemStream.WriteI32(ms, 100000)
Viper.IO.MemStream.WriteI32(ms, -99999)
Viper.IO.MemStream.Seek(ms, 0)
PRINT "ReadI32: "; Viper.IO.MemStream.ReadI32(ms)
PRINT "ReadI32: "; Viper.IO.MemStream.ReadI32(ms)

' --- WriteI64 / ReadI64 ---
PRINT "--- WriteI64 / ReadI64 ---"
Viper.IO.MemStream.Clear(ms)
Viper.IO.MemStream.WriteI64(ms, 123456789012)
Viper.IO.MemStream.Seek(ms, 0)
PRINT "ReadI64: "; Viper.IO.MemStream.ReadI64(ms)

' --- WriteStr / ReadStr ---
PRINT "--- WriteStr / ReadStr ---"
Viper.IO.MemStream.Clear(ms)
Viper.IO.MemStream.WriteStr(ms, "Hello Viper")
PRINT "Len after WriteStr: "; Viper.IO.MemStream.get_Len(ms)
Viper.IO.MemStream.Seek(ms, 0)
PRINT "ReadStr(11): "; Viper.IO.MemStream.ReadStr(ms, 11)

' --- ToBytes ---
PRINT "--- ToBytes ---"
DIM allBytes AS OBJECT
allBytes = Viper.IO.MemStream.ToBytes(ms)
PRINT "ToBytes returned"

' --- NewCapacity ---
PRINT "--- NewCapacity ---"
DIM ms2 AS OBJECT
ms2 = Viper.IO.MemStream.NewCapacity(1024)
PRINT "NewCapacity(1024) created"
PRINT "Capacity: "; Viper.IO.MemStream.get_Capacity(ms2)

' --- Skip ---
PRINT "--- Skip ---"
Viper.IO.MemStream.Clear(ms)
Viper.IO.MemStream.WriteI8(ms, 1)
Viper.IO.MemStream.WriteI8(ms, 2)
Viper.IO.MemStream.WriteI8(ms, 3)
Viper.IO.MemStream.Seek(ms, 0)
Viper.IO.MemStream.Skip(ms, 2)
PRINT "Skip(2), ReadI8: "; Viper.IO.MemStream.ReadI8(ms)

' --- Clear ---
PRINT "--- Clear ---"
Viper.IO.MemStream.Clear(ms)
PRINT "Len after clear: "; Viper.IO.MemStream.get_Len(ms)
PRINT "Pos after clear: "; Viper.IO.MemStream.get_Pos(ms)

PRINT "=== MemStream Demo Complete ==="
END

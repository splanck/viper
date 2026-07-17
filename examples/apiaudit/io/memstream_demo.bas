' =============================================================================
' API Audit: Zanna.IO.MemStream - In-Memory Stream
' =============================================================================
' Tests: New, NewCapacity, WriteI8, ReadI8, WriteI32, ReadI32, WriteI64, ReadI64,
'        WriteStr, ReadStr, WriteBytes, ReadBytes, ToBytes, Pos, Len, Capacity,
'        Seek, Skip, Clear
' =============================================================================

PRINT "=== API Audit: Zanna.IO.MemStream ==="

' --- New ---
PRINT "--- New ---"
DIM ms AS OBJECT
ms = Zanna.IO.MemStream.New()
PRINT "New() created"

' --- Initial state ---
PRINT "--- Initial state ---"
PRINT "Pos: "; Zanna.IO.MemStream.get_Position(ms)
PRINT "Len: "; Zanna.IO.MemStream.get_Length(ms)
PRINT "Capacity: "; Zanna.IO.MemStream.get_Capacity(ms)

' --- WriteI8 / ReadI8 ---
PRINT "--- WriteI8 / ReadI8 ---"
Zanna.IO.MemStream.WriteI8(ms, 42)
Zanna.IO.MemStream.WriteI8(ms, -7)
PRINT "Wrote 2 I8 values"
Zanna.IO.MemStream.Seek(ms, 0)
PRINT "ReadI8: "; Zanna.IO.MemStream.ReadI8(ms)
PRINT "ReadI8: "; Zanna.IO.MemStream.ReadI8(ms)

' --- WriteI32 / ReadI32 ---
PRINT "--- WriteI32 / ReadI32 ---"
Zanna.IO.MemStream.Clear(ms)
Zanna.IO.MemStream.WriteI32(ms, 100000)
Zanna.IO.MemStream.WriteI32(ms, -99999)
Zanna.IO.MemStream.Seek(ms, 0)
PRINT "ReadI32: "; Zanna.IO.MemStream.ReadI32(ms)
PRINT "ReadI32: "; Zanna.IO.MemStream.ReadI32(ms)

' --- WriteI64 / ReadI64 ---
PRINT "--- WriteI64 / ReadI64 ---"
Zanna.IO.MemStream.Clear(ms)
Zanna.IO.MemStream.WriteI64(ms, 123456789012)
Zanna.IO.MemStream.Seek(ms, 0)
PRINT "ReadI64: "; Zanna.IO.MemStream.ReadI64(ms)

' --- WriteStr / ReadStr ---
PRINT "--- WriteStr / ReadStr ---"
Zanna.IO.MemStream.Clear(ms)
Zanna.IO.MemStream.WriteStr(ms, "Hello Zanna")
PRINT "Len after WriteStr: "; Zanna.IO.MemStream.get_Length(ms)
Zanna.IO.MemStream.Seek(ms, 0)
PRINT "ReadStr(11): "; Zanna.IO.MemStream.ReadStr(ms, 11)

' --- ToBytes ---
PRINT "--- ToBytes ---"
DIM allBytes AS OBJECT
allBytes = Zanna.IO.MemStream.ToBytes(ms)
PRINT "ToBytes returned"

' --- NewCapacity ---
PRINT "--- NewCapacity ---"
DIM ms2 AS OBJECT
ms2 = Zanna.IO.MemStream.NewCapacity(1024)
PRINT "NewCapacity(1024) created"
PRINT "Capacity: "; Zanna.IO.MemStream.get_Capacity(ms2)

' --- Skip ---
PRINT "--- Skip ---"
Zanna.IO.MemStream.Clear(ms)
Zanna.IO.MemStream.WriteI8(ms, 1)
Zanna.IO.MemStream.WriteI8(ms, 2)
Zanna.IO.MemStream.WriteI8(ms, 3)
Zanna.IO.MemStream.Seek(ms, 0)
Zanna.IO.MemStream.Skip(ms, 2)
PRINT "Skip(2), ReadI8: "; Zanna.IO.MemStream.ReadI8(ms)

' --- Clear ---
PRINT "--- Clear ---"
Zanna.IO.MemStream.Clear(ms)
PRINT "Len after clear: "; Zanna.IO.MemStream.get_Length(ms)
PRINT "Pos after clear: "; Zanna.IO.MemStream.get_Position(ms)

PRINT "=== MemStream Demo Complete ==="
END

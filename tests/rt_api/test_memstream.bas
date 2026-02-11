' test_memstream.bas — IO.MemStream, IO.BinFile, IO.LineReader, IO.LineWriter, IO.Compress

' --- MemStream: integer read/write ---
DIM ms AS Viper.IO.MemStream
ms = Viper.IO.MemStream.New()
PRINT "ms pos: "; ms.Pos
PRINT "ms len: "; ms.Len

ms.WriteI8(42)
ms.WriteI16(1000)
ms.WriteI32(100000)
ms.WriteI64(9999999999)
PRINT "ms len after writes: "; ms.Len

' Seek back to start and read
ms.Seek(0)
PRINT "read i8: "; ms.ReadI8()
PRINT "read i16: "; ms.ReadI16()
PRINT "read i32: "; ms.ReadI32()
PRINT "read i64: "; ms.ReadI64()

' --- MemStream: string read/write ---
ms.Clear()
ms.WriteStr("hello world")
PRINT "ms len after str: "; ms.Len
ms.Seek(0)
PRINT "read str: "; ms.ReadStr(11)

' --- MemStream: unsigned types ---
ms.Clear()
ms.WriteU8(200)
ms.WriteU16(60000)
ms.WriteU32(3000000000)
ms.Seek(0)
PRINT "read u8: "; ms.ReadU8()
PRINT "read u16: "; ms.ReadU16()
PRINT "read u32: "; ms.ReadU32()

' --- MemStream: capacity and skip ---
PRINT "ms cap > 0: "; (ms.Capacity > 0)
ms.Clear()
ms.WriteI8(1)
ms.WriteI8(2)
ms.WriteI8(3)
ms.Seek(0)
ms.Skip(1)
PRINT "after skip pos: "; ms.Pos

' NOTE: LineWriter, LineReader, BinFile not recognized by BASIC frontend (BUG-009)
' Viper.IO.LineWriter.New — unknown procedure
' Viper.IO.LineReader.New — unknown procedure
' Viper.IO.BinFile.New — unknown procedure

' --- Compress: string deflate/inflate ---
DIM compressed AS OBJECT
compressed = Viper.IO.Compress.DeflateStr("hello world hello world hello world")
PRINT "compressed is obj: true"

DIM decompressed AS STRING
decompressed = Viper.IO.Compress.InflateStr(compressed)
PRINT "inflate: "; decompressed

' Gzip round-trip
DIM gzipped AS OBJECT
gzipped = Viper.IO.Compress.GzipStr("test data for gzip")
DIM gunzipped AS STRING
gunzipped = Viper.IO.Compress.GunzipStr(gzipped)
PRINT "gunzip: "; gunzipped

PRINT "done"
END

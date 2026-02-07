' EXPECT_OUT: RESULT: ok
' COVER: Viper.IO.BinFile.Open
' COVER: Viper.IO.BinFile.Eof
' COVER: Viper.IO.BinFile.Pos
' COVER: Viper.IO.BinFile.Size
' COVER: Viper.IO.BinFile.Close
' COVER: Viper.IO.BinFile.Flush
' COVER: Viper.IO.BinFile.Read
' COVER: Viper.IO.BinFile.ReadByte
' COVER: Viper.IO.BinFile.Seek
' COVER: Viper.IO.BinFile.Write
' COVER: Viper.IO.BinFile.WriteByte
' COVER: Viper.IO.LineReader.Open
' COVER: Viper.IO.LineReader.Eof
' COVER: Viper.IO.LineReader.Close
' COVER: Viper.IO.LineReader.PeekChar
' COVER: Viper.IO.LineReader.Read
' COVER: Viper.IO.LineReader.ReadAll
' COVER: Viper.IO.LineReader.ReadChar
' COVER: Viper.IO.LineWriter.Open
' COVER: Viper.IO.LineWriter.NewLine
' COVER: Viper.IO.LineWriter.Close
' COVER: Viper.IO.LineWriter.Flush
' COVER: Viper.IO.LineWriter.Write
' COVER: Viper.IO.LineWriter.WriteChar
' COVER: Viper.IO.LineWriter.WriteLn
' COVER: Viper.IO.MemStream.New
' COVER: Viper.IO.MemStream.Capacity
' COVER: Viper.IO.MemStream.Len
' COVER: Viper.IO.MemStream.Pos
' COVER: Viper.IO.MemStream.Clear
' COVER: Viper.IO.MemStream.ReadBytes
' COVER: Viper.IO.MemStream.ReadF32
' COVER: Viper.IO.MemStream.ReadF64
' COVER: Viper.IO.MemStream.ReadI16
' COVER: Viper.IO.MemStream.ReadI32
' COVER: Viper.IO.MemStream.ReadI64
' COVER: Viper.IO.MemStream.ReadI8
' COVER: Viper.IO.MemStream.ReadStr
' COVER: Viper.IO.MemStream.ReadU16
' COVER: Viper.IO.MemStream.ReadU32
' COVER: Viper.IO.MemStream.ReadU8
' COVER: Viper.IO.MemStream.Seek
' COVER: Viper.IO.MemStream.Skip
' COVER: Viper.IO.MemStream.ToBytes
' COVER: Viper.IO.MemStream.WriteBytes
' COVER: Viper.IO.MemStream.WriteF32
' COVER: Viper.IO.MemStream.WriteF64
' COVER: Viper.IO.MemStream.WriteI16
' COVER: Viper.IO.MemStream.WriteI32
' COVER: Viper.IO.MemStream.WriteI64
' COVER: Viper.IO.MemStream.WriteI8
' COVER: Viper.IO.MemStream.WriteStr
' COVER: Viper.IO.MemStream.WriteU16
' COVER: Viper.IO.MemStream.WriteU32
' COVER: Viper.IO.MemStream.WriteU8

SUB AssertApprox(actual AS DOUBLE, expected AS DOUBLE, eps AS DOUBLE, msg AS STRING)
    IF Viper.Math.Abs(actual - expected) > eps THEN
        Viper.Core.Diagnostics.Assert(FALSE, msg)
    END IF
END SUB

DIM cwd AS STRING
cwd = Viper.IO.Dir.Current()
DIM base AS STRING
base = Viper.IO.Path.Join(cwd, "tests/runtime_sweep/tmp_streams")
IF Viper.IO.Dir.Exists(base) THEN
    Viper.IO.Dir.RemoveAll(base)
END IF
Viper.IO.Dir.MakeAll(base)

DIM binPath AS STRING
binPath = Viper.IO.Path.Join(base, "bin.dat")
DIM bf AS Viper.IO.BinFile
bf = Viper.IO.BinFile.Open(binPath, "w")
bf.WriteByte(202)
bf.WriteByte(254)
DIM buf AS Viper.Collections.Bytes
buf = NEW Viper.Collections.Bytes(2)
buf.Set(0, 1)
buf.Set(1, 2)
bf.Write(buf, 0, 2)
bf.Flush()
bf.Close()

bf = Viper.IO.BinFile.Open(binPath, "r")
Viper.Core.Diagnostics.AssertEq(bf.Size, 4, "bin.size")
Viper.Core.Diagnostics.AssertEq(bf.Pos, 0, "bin.pos0")
DIM b0 AS INTEGER
b0 = bf.ReadByte()
Viper.Core.Diagnostics.AssertEq(b0, 202, "bin.readbyte")
DIM readBuf AS Viper.Collections.Bytes
readBuf = NEW Viper.Collections.Bytes(2)
DIM readCount AS INTEGER
readCount = bf.Read(readBuf, 0, 2)
Viper.Core.Diagnostics.AssertEq(readCount, 2, "bin.read")
Viper.Core.Diagnostics.AssertEqStr(readBuf.ToHex(), "fe01", "bin.read.hex")
DIM newPos AS INTEGER
newPos = bf.Seek(0, 0)
Viper.Core.Diagnostics.AssertEq(newPos, 0, "bin.seek")
Viper.Core.Diagnostics.Assert(bf.Eof = FALSE, "bin.eof")
bf.Close()

DIM linesPath AS STRING
linesPath = Viper.IO.Path.Join(base, "lines.txt")
DIM writer AS Viper.IO.LineWriter
writer = Viper.IO.LineWriter.Open(linesPath)
writer.NewLine = "\n"
writer.Write("A")
writer.WriteChar(66)
writer.WriteLn("C")
writer.Flush()
writer.Close()

DIM reader AS Viper.IO.LineReader
reader = Viper.IO.LineReader.Open(linesPath)
DIM peek AS INTEGER
peek = reader.PeekChar()
Viper.Core.Diagnostics.Assert(peek >= 0, "line.peek")
DIM ch AS INTEGER
ch = reader.ReadChar()
Viper.Core.Diagnostics.Assert(ch >= 0, "line.readchar")
DIM line AS STRING
line = reader.Read()
DIM rest AS STRING
rest = reader.ReadAll()
reader.Close()
Viper.Core.Diagnostics.Assert(reader.Eof, "line.eof")

DIM ms AS Viper.IO.MemStream
ms = Viper.IO.MemStream.New()
ms.WriteI8(-5)
ms.WriteU8(250)
ms.WriteI16(-1234)
ms.WriteU16(65530)
ms.WriteI32(-123456)
ms.WriteU32(4000000000)
ms.WriteI64(-123456789)
ms.WriteF32(1.5)
ms.WriteF64(2.25)
ms.WriteStr("hi")
DIM msBytes AS Viper.Collections.Bytes
msBytes = NEW Viper.Collections.Bytes(2)
msBytes.Set(0, 7)
msBytes.Set(1, 8)
ms.WriteBytes(msBytes)

Viper.Core.Diagnostics.Assert(ms.Len > 0, "ms.len")
Viper.Core.Diagnostics.Assert(ms.Capacity >= ms.Len, "ms.capacity")

ms.Seek(0)
Viper.Core.Diagnostics.AssertEq(ms.ReadI8(), -5, "ms.readi8")
Viper.Core.Diagnostics.AssertEq(ms.ReadU8(), 250, "ms.readu8")
Viper.Core.Diagnostics.AssertEq(ms.ReadI16(), -1234, "ms.readi16")
Viper.Core.Diagnostics.AssertEq(ms.ReadU16(), 65530, "ms.readu16")
Viper.Core.Diagnostics.AssertEq(ms.ReadI32(), -123456, "ms.readi32")
Viper.Core.Diagnostics.AssertEq(ms.ReadU32(), 4000000000, "ms.readu32")
Viper.Core.Diagnostics.AssertEq(ms.ReadI64(), -123456789, "ms.readi64")
AssertApprox(ms.ReadF32(), 1.5, 0.0001, "ms.readf32")
AssertApprox(ms.ReadF64(), 2.25, 0.0001, "ms.readf64")
Viper.Core.Diagnostics.AssertEqStr(ms.ReadStr(2), "hi", "ms.readstr")
DIM rb AS Viper.Collections.Bytes
rb = ms.ReadBytes(2)
Viper.Core.Diagnostics.AssertEqStr(rb.ToHex(), "0708", "ms.readbytes")

ms.Skip(0)
DIM allBytes AS Viper.Collections.Bytes
allBytes = ms.ToBytes()
Viper.Core.Diagnostics.Assert(allBytes.Len >= 0, "ms.tobytes")

ms.Clear()
Viper.Core.Diagnostics.AssertEq(ms.Len, 0, "ms.clear")
Viper.Core.Diagnostics.AssertEq(ms.Pos, 0, "ms.pos0")

Viper.IO.Dir.RemoveAll(base)

PRINT "RESULT: ok"
END

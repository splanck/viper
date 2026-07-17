' EXPECT_OUT: RESULT: ok
' COVER: Zanna.IO.Archive.Count
' COVER: Zanna.IO.Archive.Names
' COVER: Zanna.IO.Archive.Path
' COVER: Zanna.IO.Archive.Add
' COVER: Zanna.IO.Archive.AddDir
' COVER: Zanna.IO.Archive.AddFile
' COVER: Zanna.IO.Archive.AddStr
' COVER: Zanna.IO.Archive.Create
' COVER: Zanna.IO.Archive.Extract
' COVER: Zanna.IO.Archive.ExtractAll
' COVER: Zanna.IO.Archive.Finish
' COVER: Zanna.IO.Archive.FromBytes
' COVER: Zanna.IO.Archive.Has
' COVER: Zanna.IO.Archive.Info
' COVER: Zanna.IO.Archive.IsZip
' COVER: Zanna.IO.Archive.IsZipBytes
' COVER: Zanna.IO.Archive.Open
' COVER: Zanna.IO.Archive.Read
' COVER: Zanna.IO.Archive.ReadStr
' COVER: Zanna.IO.Compress.Deflate
' COVER: Zanna.IO.Compress.DeflateLvl
' COVER: Zanna.IO.Compress.DeflateStr
' COVER: Zanna.IO.Compress.Gunzip
' COVER: Zanna.IO.Compress.GunzipStr
' COVER: Zanna.IO.Compress.Gzip
' COVER: Zanna.IO.Compress.GzipLvl
' COVER: Zanna.IO.Compress.GzipStr
' COVER: Zanna.IO.Compress.Inflate
' COVER: Zanna.IO.Compress.InflateStr

SUB FillBytes(s AS STRING, b AS Zanna.IO.BinaryBuffer)
    DIM i AS INTEGER
    FOR i = 0 TO s.Length - 1
        b.WriteByte(Zanna.String.Asc(Zanna.String.MidLen(s, i + 1, 1)))
    NEXT i
END SUB

DIM cwd AS STRING
cwd = Zanna.IO.Dir.Current()
DIM base AS STRING
base = Zanna.IO.Path.Join(cwd, "tests/runtime_sweep/tmp_archive")
IF Zanna.IO.Dir.Exists(base) THEN
    Zanna.IO.Dir.RemoveAll(base)
END IF
Zanna.IO.Dir.MakeAll(base)

DIM notePath AS STRING
notePath = Zanna.IO.Path.Join(base, "note.txt")
Zanna.IO.File.WriteAllText(notePath, "note")

DIM dataBytes AS Zanna.IO.BinaryBuffer
dataBytes = Zanna.IO.BinaryBuffer.NewCapacity(4)
dataBytes.WriteByte(1)
dataBytes.WriteByte(2)
dataBytes.WriteByte(3)
dataBytes.WriteByte(4)

DIM archivePath AS STRING
archivePath = Zanna.IO.Path.Join(base, "test.zip")

DIM arc AS Zanna.IO.Archive
arc = Zanna.IO.Archive.Create(archivePath)
arc.AddStr("hello.txt", "hi")
arc.Add("data.bin", dataBytes)
arc.AddFile("note.txt", notePath)
arc.AddDir("logs/")
arc.Finish()

Zanna.Core.Diagnostics.Assert(Zanna.IO.Archive.IsZip(archivePath), "zip.iszip")

DIM arc2 AS Zanna.IO.Archive
arc2 = Zanna.IO.Archive.Open(archivePath)
Zanna.Core.Diagnostics.Assert(LEN(arc2.Path) > 0, "zip.path")
Zanna.Core.Diagnostics.Assert(arc2.Count >= 3, "zip.count")
DIM names AS Zanna.Collections.Seq
names = arc2.Names
Zanna.Core.Diagnostics.Assert(names.Count >= 3, "zip.names")
Zanna.Core.Diagnostics.Assert(arc2.Has("hello.txt"), "zip.has")
Zanna.Core.Diagnostics.AssertEqStr(arc2.ReadStr("hello.txt"), "hi", "zip.readstr")
DIM bin AS Zanna.IO.BinaryBuffer
bin = arc2.Read("data.bin")
Zanna.Core.Diagnostics.AssertEq(bin.Length, 4, "zip.read")
DIM info AS Zanna.Collections.Map
info = arc2.Info("hello.txt")
Zanna.Core.Diagnostics.Assert(info.Has("size"), "zip.info")

DIM outDir AS STRING
outDir = Zanna.IO.Path.Join(base, "out")
Zanna.IO.Dir.MakeAll(outDir)
arc2.ExtractAll(outDir)
DIM extracted AS STRING
extracted = Zanna.IO.Path.Join(outDir, "hello.txt")
Zanna.Core.Diagnostics.Assert(Zanna.IO.File.Exists(extracted), "zip.extractall")
DIM extractedNote AS STRING
extractedNote = Zanna.IO.Path.Join(base, "note_extracted.txt")
arc2.Extract("note.txt", extractedNote)
Zanna.Core.Diagnostics.Assert(Zanna.IO.File.Exists(extractedNote), "zip.extract")

DIM arcBytes AS Zanna.IO.BinaryBuffer
arcBytes = Zanna.IO.File.ReadAllBytes(archivePath)
Zanna.Core.Diagnostics.Assert(Zanna.IO.Archive.IsZipBytes(arcBytes), "zip.iszipbytes")
DIM arc3 AS Zanna.IO.Archive
arc3 = Zanna.IO.Archive.FromBytes(arcBytes)
Zanna.Core.Diagnostics.AssertEqStr(arc3.ReadStr("hello.txt"), "hi", "zip.frombytes")

DIM text AS STRING
text = "The quick brown fox jumps over the lazy dog."
DIM textBytes AS Zanna.IO.BinaryBuffer
textBytes = Zanna.IO.BinaryBuffer.NewCapacity(text.Length)
FillBytes(text, textBytes)

DIM gz AS Zanna.IO.BinaryBuffer
gz = Zanna.IO.Compress.Gzip(textBytes)
DIM gun AS Zanna.IO.BinaryBuffer
gun = Zanna.IO.Compress.Gunzip(gz)
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Collections.Bytes.ToHex(gun.ToBytes()), Zanna.Collections.Bytes.ToHex(textBytes.ToBytes()), "gzip")

DIM gz2 AS Zanna.IO.BinaryBuffer
gz2 = Zanna.IO.Compress.GzipLvl(textBytes, 9)
DIM gun2 AS Zanna.IO.BinaryBuffer
gun2 = Zanna.IO.Compress.Gunzip(gz2)
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Collections.Bytes.ToHex(gun2.ToBytes()), Zanna.Collections.Bytes.ToHex(textBytes.ToBytes()), "gzip.lvl")

DIM def AS Zanna.IO.BinaryBuffer
def = Zanna.IO.Compress.Deflate(textBytes)
DIM inf AS Zanna.IO.BinaryBuffer
inf = Zanna.IO.Compress.Inflate(def)
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Collections.Bytes.ToHex(inf.ToBytes()), Zanna.Collections.Bytes.ToHex(textBytes.ToBytes()), "deflate")

DIM def2 AS Zanna.IO.BinaryBuffer
def2 = Zanna.IO.Compress.DeflateLvl(textBytes, 9)
DIM inf2 AS Zanna.IO.BinaryBuffer
inf2 = Zanna.IO.Compress.Inflate(def2)
Zanna.Core.Diagnostics.AssertEqStr(Zanna.Collections.Bytes.ToHex(inf2.ToBytes()), Zanna.Collections.Bytes.ToHex(textBytes.ToBytes()), "deflate.lvl")

DIM gzStr AS Zanna.IO.BinaryBuffer
gzStr = Zanna.IO.Compress.GzipStr(text)
DIM gunStr AS STRING
gunStr = Zanna.IO.Compress.GunzipStr(gzStr)
Zanna.Core.Diagnostics.AssertEqStr(gunStr, text, "gzipstr")

DIM defStr AS Zanna.IO.BinaryBuffer
defStr = Zanna.IO.Compress.DeflateStr(text)
DIM infStr AS STRING
infStr = Zanna.IO.Compress.InflateStr(defStr)
Zanna.Core.Diagnostics.AssertEqStr(infStr, text, "deflatestr")

Zanna.IO.Dir.RemoveAll(base)

PRINT "RESULT: ok"
END

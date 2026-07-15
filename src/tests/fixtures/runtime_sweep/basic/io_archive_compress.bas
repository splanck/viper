' EXPECT_OUT: RESULT: ok
' COVER: Viper.IO.Archive.Count
' COVER: Viper.IO.Archive.Names
' COVER: Viper.IO.Archive.Path
' COVER: Viper.IO.Archive.Add
' COVER: Viper.IO.Archive.AddDir
' COVER: Viper.IO.Archive.AddFile
' COVER: Viper.IO.Archive.AddStr
' COVER: Viper.IO.Archive.Create
' COVER: Viper.IO.Archive.Extract
' COVER: Viper.IO.Archive.ExtractAll
' COVER: Viper.IO.Archive.Finish
' COVER: Viper.IO.Archive.FromBytes
' COVER: Viper.IO.Archive.Has
' COVER: Viper.IO.Archive.Info
' COVER: Viper.IO.Archive.IsZip
' COVER: Viper.IO.Archive.IsZipBytes
' COVER: Viper.IO.Archive.Open
' COVER: Viper.IO.Archive.Read
' COVER: Viper.IO.Archive.ReadStr
' COVER: Viper.IO.Compress.Deflate
' COVER: Viper.IO.Compress.DeflateLvl
' COVER: Viper.IO.Compress.DeflateStr
' COVER: Viper.IO.Compress.Gunzip
' COVER: Viper.IO.Compress.GunzipStr
' COVER: Viper.IO.Compress.Gzip
' COVER: Viper.IO.Compress.GzipLvl
' COVER: Viper.IO.Compress.GzipStr
' COVER: Viper.IO.Compress.Inflate
' COVER: Viper.IO.Compress.InflateStr

SUB FillBytes(s AS STRING, b AS Viper.IO.BinaryBuffer)
    DIM i AS INTEGER
    FOR i = 0 TO s.Length - 1
        b.WriteByte(Viper.String.Asc(Viper.String.MidLen(s, i + 1, 1)))
    NEXT i
END SUB

DIM cwd AS STRING
cwd = Viper.IO.Dir.Current()
DIM base AS STRING
base = Viper.IO.Path.Join(cwd, "tests/runtime_sweep/tmp_archive")
IF Viper.IO.Dir.Exists(base) THEN
    Viper.IO.Dir.RemoveAll(base)
END IF
Viper.IO.Dir.MakeAll(base)

DIM notePath AS STRING
notePath = Viper.IO.Path.Join(base, "note.txt")
Viper.IO.File.WriteAllText(notePath, "note")

DIM dataBytes AS Viper.IO.BinaryBuffer
dataBytes = Viper.IO.BinaryBuffer.NewCapacity(4)
dataBytes.WriteByte(1)
dataBytes.WriteByte(2)
dataBytes.WriteByte(3)
dataBytes.WriteByte(4)

DIM archivePath AS STRING
archivePath = Viper.IO.Path.Join(base, "test.zip")

DIM arc AS Viper.IO.Archive
arc = Viper.IO.Archive.Create(archivePath)
arc.AddStr("hello.txt", "hi")
arc.Add("data.bin", dataBytes)
arc.AddFile("note.txt", notePath)
arc.AddDir("logs/")
arc.Finish()

Viper.Core.Diagnostics.Assert(Viper.IO.Archive.IsZip(archivePath), "zip.iszip")

DIM arc2 AS Viper.IO.Archive
arc2 = Viper.IO.Archive.Open(archivePath)
Viper.Core.Diagnostics.Assert(LEN(arc2.Path) > 0, "zip.path")
Viper.Core.Diagnostics.Assert(arc2.Count >= 3, "zip.count")
DIM names AS Viper.Collections.Seq
names = arc2.Names
Viper.Core.Diagnostics.Assert(names.Count >= 3, "zip.names")
Viper.Core.Diagnostics.Assert(arc2.Has("hello.txt"), "zip.has")
Viper.Core.Diagnostics.AssertEqStr(arc2.ReadStr("hello.txt"), "hi", "zip.readstr")
DIM bin AS Viper.IO.BinaryBuffer
bin = arc2.Read("data.bin")
Viper.Core.Diagnostics.AssertEq(bin.Length, 4, "zip.read")
DIM info AS Viper.Collections.Map
info = arc2.Info("hello.txt")
Viper.Core.Diagnostics.Assert(info.Has("size"), "zip.info")

DIM outDir AS STRING
outDir = Viper.IO.Path.Join(base, "out")
Viper.IO.Dir.MakeAll(outDir)
arc2.ExtractAll(outDir)
DIM extracted AS STRING
extracted = Viper.IO.Path.Join(outDir, "hello.txt")
Viper.Core.Diagnostics.Assert(Viper.IO.File.Exists(extracted), "zip.extractall")
DIM extractedNote AS STRING
extractedNote = Viper.IO.Path.Join(base, "note_extracted.txt")
arc2.Extract("note.txt", extractedNote)
Viper.Core.Diagnostics.Assert(Viper.IO.File.Exists(extractedNote), "zip.extract")

DIM arcBytes AS Viper.IO.BinaryBuffer
arcBytes = Viper.IO.File.ReadAllBytes(archivePath)
Viper.Core.Diagnostics.Assert(Viper.IO.Archive.IsZipBytes(arcBytes), "zip.iszipbytes")
DIM arc3 AS Viper.IO.Archive
arc3 = Viper.IO.Archive.FromBytes(arcBytes)
Viper.Core.Diagnostics.AssertEqStr(arc3.ReadStr("hello.txt"), "hi", "zip.frombytes")

DIM text AS STRING
text = "The quick brown fox jumps over the lazy dog."
DIM textBytes AS Viper.IO.BinaryBuffer
textBytes = Viper.IO.BinaryBuffer.NewCapacity(text.Length)
FillBytes(text, textBytes)

DIM gz AS Viper.IO.BinaryBuffer
gz = Viper.IO.Compress.Gzip(textBytes)
DIM gun AS Viper.IO.BinaryBuffer
gun = Viper.IO.Compress.Gunzip(gz)
Viper.Core.Diagnostics.AssertEqStr(Viper.Collections.Bytes.ToHex(gun.ToBytes()), Viper.Collections.Bytes.ToHex(textBytes.ToBytes()), "gzip")

DIM gz2 AS Viper.IO.BinaryBuffer
gz2 = Viper.IO.Compress.GzipLvl(textBytes, 9)
DIM gun2 AS Viper.IO.BinaryBuffer
gun2 = Viper.IO.Compress.Gunzip(gz2)
Viper.Core.Diagnostics.AssertEqStr(Viper.Collections.Bytes.ToHex(gun2.ToBytes()), Viper.Collections.Bytes.ToHex(textBytes.ToBytes()), "gzip.lvl")

DIM def AS Viper.IO.BinaryBuffer
def = Viper.IO.Compress.Deflate(textBytes)
DIM inf AS Viper.IO.BinaryBuffer
inf = Viper.IO.Compress.Inflate(def)
Viper.Core.Diagnostics.AssertEqStr(Viper.Collections.Bytes.ToHex(inf.ToBytes()), Viper.Collections.Bytes.ToHex(textBytes.ToBytes()), "deflate")

DIM def2 AS Viper.IO.BinaryBuffer
def2 = Viper.IO.Compress.DeflateLvl(textBytes, 9)
DIM inf2 AS Viper.IO.BinaryBuffer
inf2 = Viper.IO.Compress.Inflate(def2)
Viper.Core.Diagnostics.AssertEqStr(Viper.Collections.Bytes.ToHex(inf2.ToBytes()), Viper.Collections.Bytes.ToHex(textBytes.ToBytes()), "deflate.lvl")

DIM gzStr AS Viper.IO.BinaryBuffer
gzStr = Viper.IO.Compress.GzipStr(text)
DIM gunStr AS STRING
gunStr = Viper.IO.Compress.GunzipStr(gzStr)
Viper.Core.Diagnostics.AssertEqStr(gunStr, text, "gzipstr")

DIM defStr AS Viper.IO.BinaryBuffer
defStr = Viper.IO.Compress.DeflateStr(text)
DIM infStr AS STRING
infStr = Viper.IO.Compress.InflateStr(defStr)
Viper.Core.Diagnostics.AssertEqStr(infStr, text, "deflatestr")

Viper.IO.Dir.RemoveAll(base)

PRINT "RESULT: ok"
END

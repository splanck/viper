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

SUB FillBytes(s AS STRING, b AS Viper.Collections.Bytes)
    DIM i AS INTEGER
    FOR i = 0 TO s.Length - 1
        b.Set(i, Viper.String.Asc(Viper.String.MidLen(s, i + 1, 1)))
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

DIM dataBytes AS Viper.Collections.Bytes
dataBytes = NEW Viper.Collections.Bytes(4)
dataBytes.Set(0, 1)
dataBytes.Set(1, 2)
dataBytes.Set(2, 3)
dataBytes.Set(3, 4)

DIM archivePath AS STRING
archivePath = Viper.IO.Path.Join(base, "test.zip")

DIM arc AS Viper.IO.Archive
arc = Viper.IO.Archive.Create(archivePath)
arc.AddStr("hello.txt", "hi")
arc.Add("data.bin", dataBytes)
arc.AddFile("note.txt", notePath)
arc.AddDir("logs/")
arc.Finish()

Viper.Diagnostics.Assert(Viper.IO.Archive.IsZip(archivePath), "zip.iszip")

DIM arc2 AS Viper.IO.Archive
arc2 = Viper.IO.Archive.Open(archivePath)
Viper.Diagnostics.Assert(arc2.Path <> "", "zip.path")
Viper.Diagnostics.Assert(arc2.Count >= 3, "zip.count")
DIM names AS Viper.Collections.Seq
names = arc2.Names
Viper.Diagnostics.Assert(names.Len >= 3, "zip.names")
Viper.Diagnostics.Assert(arc2.Has("hello.txt"), "zip.has")
Viper.Diagnostics.AssertEqStr(arc2.ReadStr("hello.txt"), "hi", "zip.readstr")
DIM bin AS Viper.Collections.Bytes
bin = arc2.Read("data.bin")
Viper.Diagnostics.AssertEq(bin.Len, 4, "zip.read")
DIM info AS Viper.Collections.Map
info = arc2.Info("hello.txt")
Viper.Diagnostics.Assert(info.Has("size"), "zip.info")

DIM outDir AS STRING
outDir = Viper.IO.Path.Join(base, "out")
Viper.IO.Dir.MakeAll(outDir)
arc2.ExtractAll(outDir)
DIM extracted AS STRING
extracted = Viper.IO.Path.Join(outDir, "hello.txt")
Viper.Diagnostics.Assert(Viper.IO.File.Exists(extracted), "zip.extractall")
DIM extractedNote AS STRING
extractedNote = Viper.IO.Path.Join(base, "note_extracted.txt")
arc2.Extract("note.txt", extractedNote)
Viper.Diagnostics.Assert(Viper.IO.File.Exists(extractedNote), "zip.extract")

DIM arcBytes AS Viper.Collections.Bytes
arcBytes = Viper.IO.File.ReadAllBytes(archivePath)
Viper.Diagnostics.Assert(Viper.IO.Archive.IsZipBytes(arcBytes), "zip.iszipbytes")
DIM arc3 AS Viper.IO.Archive
arc3 = Viper.IO.Archive.FromBytes(arcBytes)
Viper.Diagnostics.AssertEqStr(arc3.ReadStr("hello.txt"), "hi", "zip.frombytes")

DIM text AS STRING
text = "The quick brown fox jumps over the lazy dog."
DIM textBytes AS Viper.Collections.Bytes
textBytes = NEW Viper.Collections.Bytes(text.Length)
FillBytes(text, textBytes)

DIM gz AS Viper.Collections.Bytes
gz = Viper.IO.Compress.Gzip(textBytes)
DIM gun AS Viper.Collections.Bytes
gun = Viper.IO.Compress.Gunzip(gz)
Viper.Diagnostics.AssertEqStr(gun.ToHex(), textBytes.ToHex(), "gzip")

DIM gz2 AS Viper.Collections.Bytes
gz2 = Viper.IO.Compress.GzipLvl(textBytes, 9)
DIM gun2 AS Viper.Collections.Bytes
gun2 = Viper.IO.Compress.Gunzip(gz2)
Viper.Diagnostics.AssertEqStr(gun2.ToHex(), textBytes.ToHex(), "gzip.lvl")

DIM def AS Viper.Collections.Bytes
def = Viper.IO.Compress.Deflate(textBytes)
DIM inf AS Viper.Collections.Bytes
inf = Viper.IO.Compress.Inflate(def)
Viper.Diagnostics.AssertEqStr(inf.ToHex(), textBytes.ToHex(), "deflate")

DIM def2 AS Viper.Collections.Bytes
def2 = Viper.IO.Compress.DeflateLvl(textBytes, 9)
DIM inf2 AS Viper.Collections.Bytes
inf2 = Viper.IO.Compress.Inflate(def2)
Viper.Diagnostics.AssertEqStr(inf2.ToHex(), textBytes.ToHex(), "deflate.lvl")

DIM gzStr AS Viper.Collections.Bytes
gzStr = Viper.IO.Compress.GzipStr(text)
DIM gunStr AS STRING
gunStr = Viper.IO.Compress.GunzipStr(gzStr)
Viper.Diagnostics.AssertEqStr(gunStr, text, "gzipstr")

DIM defStr AS Viper.Collections.Bytes
defStr = Viper.IO.Compress.DeflateStr(text)
DIM infStr AS STRING
infStr = Viper.IO.Compress.InflateStr(defStr)
Viper.Diagnostics.AssertEqStr(infStr, text, "deflatestr")

Viper.IO.Dir.RemoveAll(base)

PRINT "RESULT: ok"
END

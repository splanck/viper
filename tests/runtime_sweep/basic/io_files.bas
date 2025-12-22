' EXPECT_OUT: RESULT: ok
' COVER: Viper.IO.Dir.Current
' COVER: Viper.IO.Dir.Dirs
' COVER: Viper.IO.Dir.DirsSeq
' COVER: Viper.IO.Dir.Entries
' COVER: Viper.IO.Dir.Exists
' COVER: Viper.IO.Dir.Files
' COVER: Viper.IO.Dir.FilesSeq
' COVER: Viper.IO.Dir.List
' COVER: Viper.IO.Dir.ListSeq
' COVER: Viper.IO.Dir.Make
' COVER: Viper.IO.Dir.MakeAll
' COVER: Viper.IO.Dir.Move
' COVER: Viper.IO.Dir.Remove
' COVER: Viper.IO.Dir.RemoveAll
' COVER: Viper.IO.Dir.SetCurrent
' COVER: Viper.IO.File.Append
' COVER: Viper.IO.File.AppendLine
' COVER: Viper.IO.File.Copy
' COVER: Viper.IO.File.Delete
' COVER: Viper.IO.File.Exists
' COVER: Viper.IO.File.Modified
' COVER: Viper.IO.File.Move
' COVER: Viper.IO.File.ReadAllBytes
' COVER: Viper.IO.File.ReadAllLines
' COVER: Viper.IO.File.ReadAllText
' COVER: Viper.IO.File.ReadBytes
' COVER: Viper.IO.File.ReadLines
' COVER: Viper.IO.File.Size
' COVER: Viper.IO.File.Touch
' COVER: Viper.IO.File.WriteAllBytes
' COVER: Viper.IO.File.WriteAllText
' COVER: Viper.IO.File.WriteBytes
' COVER: Viper.IO.File.WriteLines
' COVER: Viper.IO.Path.Abs
' COVER: Viper.IO.Path.Dir
' COVER: Viper.IO.Path.Ext
' COVER: Viper.IO.Path.IsAbs
' COVER: Viper.IO.Path.Join
' COVER: Viper.IO.Path.Name
' COVER: Viper.IO.Path.Norm
' COVER: Viper.IO.Path.Sep
' COVER: Viper.IO.Path.Stem
' COVER: Viper.IO.Path.WithExt

DIM cwd AS STRING
cwd = Viper.IO.Dir.Current()
DIM base AS STRING
base = Viper.IO.Path.Join(cwd, "tests/runtime_sweep/tmp_io")
IF Viper.IO.Dir.Exists(base) THEN
    Viper.IO.Dir.RemoveAll(base)
END IF
Viper.IO.Dir.MakeAll(base)

DIM subdir AS STRING
subdir = Viper.IO.Path.Join(base, "sub")
Viper.IO.Dir.Make(subdir)
Viper.Diagnostics.Assert(Viper.IO.Dir.Exists(subdir), "dir.exists")

DIM fileA AS STRING
fileA = Viper.IO.Path.Join(base, "a.txt")
Viper.IO.File.WriteAllText(fileA, "one")
Viper.IO.File.Append(fileA, " two")
Viper.IO.File.AppendLine(fileA, "three")
DIM text AS STRING
text = Viper.IO.File.ReadAllText(fileA)
Viper.Diagnostics.Assert(Viper.String.Has(text, "one"), "file.readalltext")

DIM lines AS Viper.Collections.Seq
lines = Viper.Collections.Seq.New()
lines.Push("L1")
lines.Push("L2")
DIM fileB AS STRING
fileB = Viper.IO.Path.Join(base, "b.txt")
Viper.IO.File.WriteLines(fileB, lines)
DIM linesAll AS Viper.Collections.Seq
linesAll = Viper.IO.File.ReadAllLines(fileB)
Viper.Diagnostics.AssertEq(linesAll.Len, 2, "file.readalllines")
DIM linesSeq AS Viper.Collections.Seq
linesSeq = Viper.IO.File.ReadLines(fileB)
Viper.Diagnostics.AssertEq(linesSeq.Len, 2, "file.readlines")

DIM bytes AS Viper.Collections.Bytes
bytes = NEW Viper.Collections.Bytes(3)
bytes.Set(0, 1)
bytes.Set(1, 2)
bytes.Set(2, 3)
DIM fileBin AS STRING
fileBin = Viper.IO.Path.Join(base, "c.bin")
Viper.IO.File.WriteAllBytes(fileBin, bytes)
DIM readAll AS Viper.Collections.Bytes
readAll = Viper.IO.File.ReadAllBytes(fileBin)
Viper.Diagnostics.AssertEqStr(readAll.ToHex(), "010203", "file.readallbytes")
Viper.IO.File.WriteBytes(fileBin, bytes)
DIM readBytes AS Viper.Collections.Bytes
readBytes = Viper.IO.File.ReadBytes(fileBin)
Viper.Diagnostics.AssertEqStr(readBytes.ToHex(), "010203", "file.readbytes")

Viper.Diagnostics.AssertEq(Viper.IO.File.Size(fileBin), 3, "file.size")
DIM mod1 AS INTEGER
mod1 = Viper.IO.File.Modified(fileBin)
Viper.IO.File.Touch(fileBin)
DIM mod2 AS INTEGER
mod2 = Viper.IO.File.Modified(fileBin)
Viper.Diagnostics.Assert(mod2 >= mod1, "file.modified")

DIM copyPath AS STRING
copyPath = Viper.IO.Path.Join(base, "copy.bin")
Viper.IO.File.Copy(fileBin, copyPath)
Viper.Diagnostics.Assert(Viper.IO.File.Exists(copyPath), "file.copy")
DIM movePath AS STRING
movePath = Viper.IO.Path.Join(base, "moved.bin")
Viper.IO.File.Move(copyPath, movePath)
Viper.Diagnostics.Assert(Viper.IO.File.Exists(movePath), "file.move")
Viper.IO.File.Delete(movePath)
Viper.Diagnostics.Assert(Viper.IO.File.Exists(movePath) = FALSE, "file.delete")

DIM entries AS Viper.Collections.Seq
entries = Viper.IO.Dir.Entries(base)
DIM list1 AS Viper.Collections.Seq
list1 = Viper.IO.Dir.List(base)
DIM list2 AS Viper.Collections.Seq
list2 = Viper.IO.Dir.ListSeq(base)
DIM files1 AS Viper.Collections.Seq
files1 = Viper.IO.Dir.Files(base)
DIM files2 AS Viper.Collections.Seq
files2 = Viper.IO.Dir.FilesSeq(base)
DIM dirs1 AS Viper.Collections.Seq
dirs1 = Viper.IO.Dir.Dirs(base)
DIM dirs2 AS Viper.Collections.Seq
dirs2 = Viper.IO.Dir.DirsSeq(base)
Viper.Diagnostics.Assert(entries.Len >= 2, "dir.entries")
Viper.Diagnostics.Assert(list1.Len = list2.Len, "dir.list")
Viper.Diagnostics.Assert(files1.Len = files2.Len, "dir.files")
Viper.Diagnostics.Assert(dirs1.Len = dirs2.Len, "dir.dirs")

DIM baseMoved AS STRING
baseMoved = Viper.IO.Path.Join(cwd, "tests/runtime_sweep/tmp_io_moved")
IF Viper.IO.Dir.Exists(baseMoved) THEN
    Viper.IO.Dir.RemoveAll(baseMoved)
END IF
Viper.IO.Dir.Move(base, baseMoved)
Viper.Diagnostics.Assert(Viper.IO.Dir.Exists(baseMoved), "dir.move")
Viper.IO.Dir.RemoveAll(baseMoved)
Viper.Diagnostics.Assert(Viper.IO.Dir.Exists(baseMoved) = FALSE, "dir.removeall")

DIM emptyDir AS STRING
emptyDir = Viper.IO.Path.Join(cwd, "tests/runtime_sweep/tmp_empty")
IF Viper.IO.Dir.Exists(emptyDir) THEN
    Viper.IO.Dir.RemoveAll(emptyDir)
END IF
Viper.IO.Dir.Make(emptyDir)
Viper.IO.Dir.Remove(emptyDir)
Viper.Diagnostics.Assert(Viper.IO.Dir.Exists(emptyDir) = FALSE, "dir.remove")

Viper.IO.Dir.MakeAll(base)
DIM cur AS STRING
cur = Viper.IO.Dir.Current()
Viper.IO.Dir.SetCurrent(base)
Viper.Diagnostics.AssertEqStr(Viper.IO.Dir.Current(), base, "dir.setcurrent")
Viper.IO.Dir.SetCurrent(cur)

DIM absPath AS STRING
absPath = Viper.IO.Path.Abs("tests")
Viper.Diagnostics.Assert(Viper.IO.Path.IsAbs(absPath), "path.abs")
Viper.Diagnostics.AssertEqStr(Viper.IO.Path.Dir(fileA), base, "path.dir")
Viper.Diagnostics.AssertEqStr(Viper.IO.Path.Name(fileA), "a.txt", "path.name")
Viper.Diagnostics.AssertEqStr(Viper.IO.Path.Stem(fileA), "a", "path.stem")
Viper.Diagnostics.AssertEqStr(Viper.IO.Path.Ext(fileA), ".txt", "path.ext")
DIM withExt AS STRING
withExt = Viper.IO.Path.WithExt(fileA, ".md")
Viper.Diagnostics.AssertEqStr(withExt, Viper.IO.Path.Join(base, "a.md"), "path.withext")
Viper.Diagnostics.AssertEqStr(Viper.IO.Path.Norm("a/b/../c"), "a/c", "path.norm")
DIM sep AS STRING
sep = Viper.IO.Path.Sep()
Viper.Diagnostics.Assert(sep <> "", "path.sep")

IF Viper.IO.Dir.Exists(base) THEN
    Viper.IO.Dir.RemoveAll(base)
END IF

PRINT "RESULT: ok"
END

' test_io_file.bas — IO.File, IO.TempFile, IO.Dir, IO.Glob
' Tests actual file operations: create, read, write, copy, move, delete

' --- TempFile: get temp directory and create temp paths ---
DIM tmpdir AS STRING
tmpdir = Viper.IO.TempFile.Dir()
PRINT "tempdir nonempty: "; (LEN(tmpdir) > 0)

DIM tmppath AS STRING
tmppath = Viper.IO.TempFile.Path()
PRINT "tmppath nonempty: "; (LEN(tmppath) > 0)

DIM tmppfx AS STRING
tmppfx = Viper.IO.TempFile.PathWithPrefix("vtest")
PRINT "tmppfx has prefix: "; Viper.String.Has(tmppfx, "vtest")

DIM tmpext AS STRING
tmpext = Viper.IO.TempFile.PathWithExt("vtest", ".txt")
PRINT "tmpext has ext: "; Viper.String.EndsWith(tmpext, ".txt")

' --- File: write, read, exists, size ---
DIM testfile AS STRING
testfile = Viper.IO.TempFile.PathWithExt("vtest_io", ".txt")

Viper.IO.File.WriteAllText(testfile, "hello world")
PRINT "file exists: "; Viper.IO.File.Exists(testfile)
PRINT "file size: "; Viper.IO.File.Size(testfile)

DIM content AS STRING
content = Viper.IO.File.ReadAllText(testfile)
PRINT "file content: "; content

' --- File: append ---
Viper.IO.File.Append(testfile, " appended")
content = Viper.IO.File.ReadAllText(testfile)
PRINT "after append: "; content

' --- File: appendline ---
Viper.IO.File.AppendLine(testfile, "line2")
content = Viper.IO.File.ReadAllText(testfile)
PRINT "content has line2: "; Viper.String.Has(content, "line2")

' --- File: copy ---
DIM copyfile AS STRING
copyfile = Viper.IO.TempFile.PathWithExt("vtest_copy", ".txt")
Viper.IO.File.Copy(testfile, copyfile)
PRINT "copy exists: "; Viper.IO.File.Exists(copyfile)

DIM copycontent AS STRING
copycontent = Viper.IO.File.ReadAllText(copyfile)
PRINT "copy matches: "; (copycontent = content)

' --- File: move ---
DIM movefile AS STRING
movefile = Viper.IO.TempFile.PathWithExt("vtest_moved", ".txt")
Viper.IO.File.Move(copyfile, movefile)
PRINT "moved exists: "; Viper.IO.File.Exists(movefile)
PRINT "old gone: "; (NOT Viper.IO.File.Exists(copyfile))

' --- File: delete ---
Viper.IO.File.Delete(movefile)
PRINT "deleted: "; (NOT Viper.IO.File.Exists(movefile))

' --- File: touch ---
DIM touchfile AS STRING
touchfile = Viper.IO.TempFile.PathWithExt("vtest_touch", ".txt")
Viper.IO.File.Touch(touchfile)
PRINT "touched exists: "; Viper.IO.File.Exists(touchfile)
Viper.IO.File.Delete(touchfile)

' --- File: modified ---
DIM modtime AS LONG
modtime = Viper.IO.File.Modified(testfile)
PRINT "modified > 0: "; (modtime > 0)

' --- Dir: make, exists, remove ---
DIM testdir AS STRING
testdir = Viper.IO.TempFile.PathWithPrefix("vtest_dir")
Viper.IO.Dir.Make(testdir)
PRINT "dir exists: "; Viper.IO.Dir.Exists(testdir)
Viper.IO.Dir.Remove(testdir)
PRINT "dir removed: "; (NOT Viper.IO.Dir.Exists(testdir))

' --- Dir: makeall ---
DIM deepdir AS STRING
deepdir = Viper.IO.TempFile.PathWithPrefix("vtest_deep")
DIM subdir AS STRING
subdir = Viper.IO.Path.Join(deepdir, "a")
subdir = Viper.IO.Path.Join(subdir, "b")
Viper.IO.Dir.MakeAll(subdir)
PRINT "deep dir exists: "; Viper.IO.Dir.Exists(subdir)
Viper.IO.Dir.RemoveAll(deepdir)
PRINT "deep dir removed: "; (NOT Viper.IO.Dir.Exists(deepdir))

' --- Dir: current ---
DIM curdir AS STRING
curdir = Viper.IO.Dir.Current()
PRINT "curdir nonempty: "; (LEN(curdir) > 0)

' --- IO.Glob: match ---
PRINT "glob match: "; Viper.IO.Glob.Match("*.txt", "hello.txt")
PRINT "glob nomatch: "; Viper.IO.Glob.Match("*.bas", "hello.txt")

' --- Cleanup ---
Viper.IO.File.Delete(testfile)

PRINT "done"
END

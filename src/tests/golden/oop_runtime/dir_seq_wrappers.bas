REM BASIC: Verify Zanna.IO.Dir listing helpers

DIM base AS STRING
base = "tmp_dir_seq_wrappers"

Zanna.IO.Dir.RemoveAll(base)
Zanna.IO.Dir.Make(base)

DIM subdir AS STRING
subdir = Zanna.IO.Path.Join(base, "subdir")
Zanna.IO.Dir.Make(subdir)

DIM file1 AS STRING
file1 = Zanna.IO.Path.Join(base, "file1.txt")
DIM file2 AS STRING
file2 = Zanna.IO.Path.Join(base, "file2.txt")

Zanna.IO.File.WriteAllText(file1, "one")
Zanna.IO.File.WriteAllText(file2, "two")

DIM list AS Zanna.Collections.Seq
list = Zanna.IO.Dir.List(base)

DIM list_join AS STRING
list_join = "|" + Zanna.String.Join("|", list) + "|"

PRINT list.Count
PRINT list_join.Contains("|subdir|")
PRINT list_join.Contains("|file1.txt|")
PRINT list_join.Contains("|file2.txt|")

DIM files AS Zanna.Collections.Seq
files = Zanna.IO.Dir.Files(base)

DIM files_join AS STRING
files_join = "|" + Zanna.String.Join("|", files) + "|"

PRINT files.Count
PRINT files_join.Contains("|file1.txt|")
PRINT files_join.Contains("|file2.txt|")

DIM dirs AS Zanna.Collections.Seq
dirs = Zanna.IO.Dir.Dirs(base)

DIM dirs_join AS STRING
dirs_join = "|" + Zanna.String.Join("|", dirs) + "|"

PRINT dirs.Count
PRINT dirs_join.Contains("|subdir|")

Zanna.IO.Dir.RemoveAll(base)
END

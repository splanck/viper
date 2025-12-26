REM BASIC: Verify Viper.IO.Dir *Seq listing wrappers

DIM base AS STRING
base = "tmp_dir_seq_wrappers"

Viper.IO.Dir.RemoveAll(base)
Viper.IO.Dir.Make(base)

DIM subdir AS STRING
subdir = Viper.IO.Path.Join(base, "subdir")
Viper.IO.Dir.Make(subdir)

DIM file1 AS STRING
file1 = Viper.IO.Path.Join(base, "file1.txt")
DIM file2 AS STRING
file2 = Viper.IO.Path.Join(base, "file2.txt")

Viper.IO.File.WriteAllText(file1, "one")
Viper.IO.File.WriteAllText(file2, "two")

DIM list AS Viper.Collections.Seq
list = Viper.IO.Dir.ListSeq(base)

DIM list_join AS STRING
list_join = "|" + Viper.Strings.Join("|", list) + "|"

PRINT list.Len
PRINT list_join.Has("|subdir|")
PRINT list_join.Has("|file1.txt|")
PRINT list_join.Has("|file2.txt|")

DIM files AS Viper.Collections.Seq
files = Viper.IO.Dir.FilesSeq(base)

DIM files_join AS STRING
files_join = "|" + Viper.Strings.Join("|", files) + "|"

PRINT files.Len
PRINT files_join.Has("|file1.txt|")
PRINT files_join.Has("|file2.txt|")

DIM dirs AS Viper.Collections.Seq
dirs = Viper.IO.Dir.DirsSeq(base)

DIM dirs_join AS STRING
dirs_join = "|" + Viper.Strings.Join("|", dirs) + "|"

PRINT dirs.Len
PRINT dirs_join.Has("|subdir|")

Viper.IO.Dir.RemoveAll(base)
END

' =============================================================================
' API Audit: Viper.IO.Dir - Directory Operations
' =============================================================================
' Tests: Current, Exists, Make, MakeAll, FilesSeq, DirsSeq, ListSeq, Entries,
'        Remove, RemoveAll, SetCurrent
' =============================================================================

PRINT "=== API Audit: Viper.IO.Dir ==="

DIM cwd AS STRING
cwd = Viper.IO.Dir.Current()

' --- Current ---
PRINT "--- Current ---"
PRINT "Current: "; cwd

DIM tmpDir AS STRING
tmpDir = Viper.IO.TempFile.Dir()
DIM testDir AS STRING
testDir = Viper.IO.Path.Join(tmpDir, "viper_dir_audit_bas")
DIM subDir AS STRING
subDir = Viper.IO.Path.Join(testDir, "sub1")
DIM deepDir AS STRING
deepDir = Viper.IO.Path.Join(testDir, "a/b/c")

' --- Make ---
PRINT "--- Make ---"
Viper.IO.Dir.Make(testDir)
PRINT "Make done"

' --- Exists ---
PRINT "--- Exists ---"
PRINT "Exists(testDir): "; Viper.IO.Dir.Exists(testDir)
PRINT "Exists('/nonexistent'): "; Viper.IO.Dir.Exists("/nonexistent_dir_xyz")

Viper.IO.Dir.Make(subDir)
PRINT "Made sub1"

' --- MakeAll ---
PRINT "--- MakeAll ---"
Viper.IO.Dir.MakeAll(deepDir)
PRINT "MakeAll(a/b/c) done"
PRINT "Exists(deepDir): "; Viper.IO.Dir.Exists(deepDir)

' Create test files
Viper.IO.File.WriteAllText(Viper.IO.Path.Join(testDir, "file1.txt"), "one")
Viper.IO.File.WriteAllText(Viper.IO.Path.Join(testDir, "file2.txt"), "two")
PRINT "Created test files"

' --- FilesSeq ---
PRINT "--- FilesSeq ---"
DIM files AS OBJECT
files = Viper.IO.Dir.FilesSeq(testDir)
PRINT "FilesSeq returned"

' --- DirsSeq ---
PRINT "--- DirsSeq ---"
DIM dirs AS OBJECT
dirs = Viper.IO.Dir.DirsSeq(testDir)
PRINT "DirsSeq returned"

' --- ListSeq ---
PRINT "--- ListSeq ---"
DIM lst AS OBJECT
lst = Viper.IO.Dir.ListSeq(testDir)
PRINT "ListSeq returned"

' --- Entries ---
PRINT "--- Entries ---"
DIM entries AS OBJECT
entries = Viper.IO.Dir.Entries(testDir)
PRINT "Entries returned"

' --- SetCurrent ---
PRINT "--- SetCurrent ---"
Viper.IO.Dir.SetCurrent(testDir)
PRINT "SetCurrent to testDir"
PRINT "Current now: "; Viper.IO.Dir.Current()
Viper.IO.Dir.SetCurrent(cwd)
PRINT "Restored original cwd"

' --- Remove ---
PRINT "--- Remove ---"
Viper.IO.Dir.Remove(subDir)
PRINT "Remove(sub1) done"
PRINT "sub1 exists: "; Viper.IO.Dir.Exists(subDir)

' --- RemoveAll (cleanup) ---
PRINT "--- RemoveAll ---"
Viper.IO.Dir.RemoveAll(testDir)
PRINT "RemoveAll done"
PRINT "testDir exists: "; Viper.IO.Dir.Exists(testDir)

PRINT "=== Dir Demo Complete ==="
END

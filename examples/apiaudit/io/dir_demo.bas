' =============================================================================
' API Audit: Zanna.IO.Dir - Directory Operations
' =============================================================================
' Tests: Current, Exists, Make, MakeAll, FilesSeq, DirsSeq, ListSeq, Entries,
'        Remove, RemoveAll, SetCurrent
' =============================================================================

PRINT "=== API Audit: Zanna.IO.Dir ==="

DIM cwd AS STRING
cwd = Zanna.IO.Dir.Current()

' --- Current ---
PRINT "--- Current ---"
PRINT "Current: "; cwd

DIM tmpDir AS STRING
tmpDir = Zanna.IO.TempFile.Dir()
DIM testDir AS STRING
testDir = Zanna.IO.Path.Join(tmpDir, "zanna_dir_audit_bas")
DIM subDir AS STRING
subDir = Zanna.IO.Path.Join(testDir, "sub1")
DIM deepDir AS STRING
deepDir = Zanna.IO.Path.Join(testDir, "a/b/c")

' --- Make ---
PRINT "--- Make ---"
Zanna.IO.Dir.Make(testDir)
PRINT "Make done"

' --- Exists ---
PRINT "--- Exists ---"
PRINT "Exists(testDir): "; Zanna.IO.Dir.Exists(testDir)
PRINT "Exists('/nonexistent'): "; Zanna.IO.Dir.Exists("/nonexistent_dir_xyz")

Zanna.IO.Dir.Make(subDir)
PRINT "Made sub1"

' --- MakeAll ---
PRINT "--- MakeAll ---"
Zanna.IO.Dir.MakeAll(deepDir)
PRINT "MakeAll(a/b/c) done"
PRINT "Exists(deepDir): "; Zanna.IO.Dir.Exists(deepDir)

' Create test files
Zanna.IO.File.WriteAllText(Zanna.IO.Path.Join(testDir, "file1.txt"), "one")
Zanna.IO.File.WriteAllText(Zanna.IO.Path.Join(testDir, "file2.txt"), "two")
PRINT "Created test files"

' --- FilesSeq ---
PRINT "--- FilesSeq ---"
DIM files AS OBJECT
files = Zanna.IO.Dir.Files(testDir)
PRINT "FilesSeq returned"

' --- DirsSeq ---
PRINT "--- DirsSeq ---"
DIM dirs AS OBJECT
dirs = Zanna.IO.Dir.Dirs(testDir)
PRINT "DirsSeq returned"

' --- ListSeq ---
PRINT "--- ListSeq ---"
DIM lst AS OBJECT
lst = Zanna.IO.Dir.List(testDir)
PRINT "ListSeq returned"

' --- Entries ---
PRINT "--- Entries ---"
DIM entries AS OBJECT
entries = Zanna.IO.Dir.Entries(testDir)
PRINT "Entries returned"

' --- SetCurrent ---
PRINT "--- SetCurrent ---"
Zanna.IO.Dir.SetCurrent(testDir)
PRINT "SetCurrent to testDir"
PRINT "Current now: "; Zanna.IO.Dir.Current()
Zanna.IO.Dir.SetCurrent(cwd)
PRINT "Restored original cwd"

' --- Remove ---
PRINT "--- Remove ---"
Zanna.IO.Dir.Remove(subDir)
PRINT "Remove(sub1) done"
PRINT "sub1 exists: "; Zanna.IO.Dir.Exists(subDir)

' --- RemoveAll (cleanup) ---
PRINT "--- RemoveAll ---"
Zanna.IO.Dir.RemoveAll(testDir)
PRINT "RemoveAll done"
PRINT "testDir exists: "; Zanna.IO.Dir.Exists(testDir)

PRINT "=== Dir Demo Complete ==="
END

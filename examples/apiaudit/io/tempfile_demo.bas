' =============================================================================
' API Audit: Zanna.IO.TempFile - Temporary File/Directory Creation
' =============================================================================
' Tests: Dir, Path, PathWithPrefix, PathWithExt, Create, CreateWithPrefix,
'        CreateDir, CreateDirWithPrefix
' =============================================================================

PRINT "=== API Audit: Zanna.IO.TempFile ==="

' --- Dir ---
PRINT "--- Dir ---"
PRINT "Dir: "; Zanna.IO.TempFile.Dir()

' --- Path ---
PRINT "--- Path ---"
PRINT "Path: "; Zanna.IO.TempFile.Path()

' --- PathWithPrefix ---
PRINT "--- PathWithPrefix ---"
PRINT "PathWithPrefix: "; Zanna.IO.TempFile.PathWithPrefix("zanna_audit_")

' --- PathWithExt ---
PRINT "--- PathWithExt ---"
PRINT "PathWithExt: "; Zanna.IO.TempFile.PathWithExt("zanna_audit_", ".txt")

' --- Create ---
PRINT "--- Create ---"
DIM created AS STRING
created = Zanna.IO.TempFile.Create()
PRINT "Create(): "; created
PRINT "File exists: "; Zanna.IO.File.Exists(created)
Zanna.IO.File.Delete(created)
PRINT "Cleaned up"

' --- CreateWithPrefix ---
PRINT "--- CreateWithPrefix ---"
DIM cwp AS STRING
cwp = Zanna.IO.TempFile.CreateWithPrefix("zanna_audit_")
PRINT "CreateWithPrefix: "; cwp
PRINT "File exists: "; Zanna.IO.File.Exists(cwp)
Zanna.IO.File.Delete(cwp)
PRINT "Cleaned up"

' --- CreateDir ---
PRINT "--- CreateDir ---"
DIM cd AS STRING
cd = Zanna.IO.TempFile.CreateDir()
PRINT "CreateDir(): "; cd
PRINT "Dir exists: "; Zanna.IO.Dir.Exists(cd)
Zanna.IO.Dir.Remove(cd)
PRINT "Cleaned up"

' --- CreateDirWithPrefix ---
PRINT "--- CreateDirWithPrefix ---"
DIM cdp AS STRING
cdp = Zanna.IO.TempFile.CreateDirWithPrefix("zanna_audit_")
PRINT "CreateDirWithPrefix: "; cdp
PRINT "Dir exists: "; Zanna.IO.Dir.Exists(cdp)
Zanna.IO.Dir.Remove(cdp)
PRINT "Cleaned up"

PRINT "=== TempFile Demo Complete ==="
END

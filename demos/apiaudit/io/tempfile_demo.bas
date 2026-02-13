' =============================================================================
' API Audit: Viper.IO.TempFile - Temporary File/Directory Creation
' =============================================================================
' Tests: Dir, Path, PathWithPrefix, PathWithExt, Create, CreateWithPrefix,
'        CreateDir, CreateDirWithPrefix
' =============================================================================

PRINT "=== API Audit: Viper.IO.TempFile ==="

' --- Dir ---
PRINT "--- Dir ---"
PRINT "Dir: "; Viper.IO.TempFile.Dir()

' --- Path ---
PRINT "--- Path ---"
PRINT "Path: "; Viper.IO.TempFile.Path()

' --- PathWithPrefix ---
PRINT "--- PathWithPrefix ---"
PRINT "PathWithPrefix: "; Viper.IO.TempFile.PathWithPrefix("viper_audit_")

' --- PathWithExt ---
PRINT "--- PathWithExt ---"
PRINT "PathWithExt: "; Viper.IO.TempFile.PathWithExt("viper_audit_", ".txt")

' --- Create ---
PRINT "--- Create ---"
DIM created AS STRING
created = Viper.IO.TempFile.Create()
PRINT "Create(): "; created
PRINT "File exists: "; Viper.IO.File.Exists(created)
Viper.IO.File.Delete(created)
PRINT "Cleaned up"

' --- CreateWithPrefix ---
PRINT "--- CreateWithPrefix ---"
DIM cwp AS STRING
cwp = Viper.IO.TempFile.CreateWithPrefix("viper_audit_")
PRINT "CreateWithPrefix: "; cwp
PRINT "File exists: "; Viper.IO.File.Exists(cwp)
Viper.IO.File.Delete(cwp)
PRINT "Cleaned up"

' --- CreateDir ---
PRINT "--- CreateDir ---"
DIM cd AS STRING
cd = Viper.IO.TempFile.CreateDir()
PRINT "CreateDir(): "; cd
PRINT "Dir exists: "; Viper.IO.Dir.Exists(cd)
Viper.IO.Dir.Remove(cd)
PRINT "Cleaned up"

' --- CreateDirWithPrefix ---
PRINT "--- CreateDirWithPrefix ---"
DIM cdp AS STRING
cdp = Viper.IO.TempFile.CreateDirWithPrefix("viper_audit_")
PRINT "CreateDirWithPrefix: "; cdp
PRINT "Dir exists: "; Viper.IO.Dir.Exists(cdp)
Viper.IO.Dir.Remove(cdp)
PRINT "Cleaned up"

PRINT "=== TempFile Demo Complete ==="
END

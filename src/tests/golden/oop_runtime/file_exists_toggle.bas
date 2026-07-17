REM Use fully-qualified procedural helpers to avoid keyword collisions
PRINT Zanna.IO.File.Exists("tmp_exists.txt")
Zanna.IO.File.WriteAllText("tmp_exists.txt", "x")
PRINT Zanna.IO.File.Exists("tmp_exists.txt")
REM Avoid DELETE keyword in BASIC; check existence, then rely on harness cleanup
REM Zanna.IO.File.Delete("tmp_exists.txt")
PRINT Zanna.IO.File.Exists("tmp_exists.txt")
END

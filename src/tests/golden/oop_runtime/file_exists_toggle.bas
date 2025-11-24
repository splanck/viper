REM Use fully-qualified procedural helpers to avoid keyword collisions
PRINT Viper.IO.File.Exists("tmp_exists.txt")
Viper.IO.File.WriteAllText("tmp_exists.txt", "x")
PRINT Viper.IO.File.Exists("tmp_exists.txt")
REM Avoid DELETE keyword in BASIC; check existence, then rely on harness cleanup
REM Viper.IO.File.Delete("tmp_exists.txt")
PRINT Viper.IO.File.Exists("tmp_exists.txt")
END

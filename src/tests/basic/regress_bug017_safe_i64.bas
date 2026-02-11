REM BUG-017 regression: SafeI64 should work on Windows
DIM s AS Viper.Threads.SafeI64
s = NEW Viper.Threads.SafeI64(100)
PRINT s.Get()
s.Set(200)
PRINT s.Get()
DIM result AS INTEGER
result = s.Add(50)
PRINT result
PRINT s.Get()

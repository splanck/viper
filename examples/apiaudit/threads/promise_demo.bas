' =============================================================================
' API Audit: Viper.Threads.Promise / Viper.Threads.Future (BASIC)
' =============================================================================
' Tests: Promise.New, Promise.Set, Promise.SetError, Promise.GetFuture,
'        Promise.IsDone, Future.Get, Future.Wait, Future.WaitFor,
'        Future.IsDone, Future.IsError, Future.Error
' =============================================================================

PRINT "=== API Audit: Viper.Threads.Promise / Future ==="

' --- Promise.New ---
PRINT "--- Promise.New ---"
DIM p AS OBJECT = Viper.Threads.Promise.New()
PRINT "Created promise"

' --- IsDone (before set) ---
PRINT "--- IsDone (before set) ---"
PRINT p.IsDone

' --- GetFuture ---
PRINT "--- GetFuture ---"
DIM f AS OBJECT = p.GetFuture()
PRINT "Got future from promise"

' --- Future.IsDone (before set) ---
PRINT "--- Future.IsDone (before set) ---"
PRINT f.IsDone

' --- Future.IsError (before set) ---
PRINT "--- Future.IsError (before set) ---"
PRINT f.IsError

' --- Promise.Set ---
PRINT "--- Promise.Set ---"
DIM boxed AS OBJECT = Viper.Core.Box.I64(42)
p.Set(boxed)
PRINT "Set promise with boxed 42"

' --- IsDone (after set) ---
PRINT "--- IsDone (after set) ---"
PRINT p.IsDone

' --- Future.IsDone (after set) ---
PRINT "--- Future.IsDone (after set) ---"
PRINT f.IsDone

' --- Future.IsError (after set) ---
PRINT "--- Future.IsError (after set) ---"
PRINT f.IsError

' --- Future.Get ---
PRINT "--- Future.Get ---"
DIM result AS OBJECT = f.Get()
PRINT "Future.Get returned: "; Viper.Core.Box.ToI64(result)

' --- Future.Wait ---
PRINT "--- Future.Wait ---"
f.Wait()
PRINT "Wait returned (already done)"

' --- Future.WaitFor ---
PRINT "--- Future.WaitFor ---"
PRINT f.WaitFor(1000)

' --- Promise with error ---
PRINT "--- Promise with error ---"
DIM p2 AS OBJECT = Viper.Threads.Promise.New()
DIM f2 AS OBJECT = p2.GetFuture()
p2.SetError("something went wrong")
PRINT "Set error on promise"

' --- Future.IsDone (error) ---
PRINT "--- Future.IsDone (error) ---"
PRINT f2.IsDone

' --- Future.IsError (error) ---
PRINT "--- Future.IsError (error) ---"
PRINT f2.IsError

' --- Future.Error ---
PRINT "--- Future.Error ---"
PRINT f2.Error

PRINT "=== Promise / Future Audit Complete ==="
END

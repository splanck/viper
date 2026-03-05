' =============================================================================
' API Audit: Viper.Threads.Monitor (BASIC)
' =============================================================================
' Tests: Enter, TryEnter, Exit (basic single-thread usage)
' Note: Monitor operates on any object. Multi-thread Wait/Pause/PauseAll
'       require actual threading and are not testable in single-thread context.
' =============================================================================

PRINT "=== API Audit: Viper.Threads.Monitor ==="

' Create a simple object to use as monitor target
DIM obj AS OBJECT = Viper.Collections.Seq.New()
PRINT "Created object for monitor"

' --- Enter ---
PRINT "--- Enter ---"
Viper.Threads.Monitor.Enter(obj)
PRINT "Monitor acquired via Enter"

' --- TryEnter (re-entrant, same thread) ---
PRINT "--- TryEnter (re-entrant) ---"
DIM ok AS INTEGER = Viper.Threads.Monitor.TryEnter(obj)
PRINT "TryEnter (re-entrant): "; ok

' --- Exit (balance re-entrant enter) ---
PRINT "--- Exit (balance re-entrant) ---"
Viper.Threads.Monitor.Exit(obj)
PRINT "First Exit done"

' --- Exit (balance original enter) ---
PRINT "--- Exit (original) ---"
Viper.Threads.Monitor.Exit(obj)
PRINT "Second Exit done"

' --- TryEnter on free monitor ---
PRINT "--- TryEnter (free) ---"
DIM ok2 AS INTEGER = Viper.Threads.Monitor.TryEnter(obj)
PRINT "TryEnter (free): "; ok2
Viper.Threads.Monitor.Exit(obj)

' --- Enter/Exit cycle ---
PRINT "--- Enter/Exit cycle ---"
Viper.Threads.Monitor.Enter(obj)
Viper.Threads.Monitor.Exit(obj)
PRINT "Enter/Exit cycle complete"

' --- Deep re-entrancy ---
PRINT "--- Deep re-entrancy ---"
Viper.Threads.Monitor.Enter(obj)
Viper.Threads.Monitor.Enter(obj)
Viper.Threads.Monitor.Enter(obj)
PRINT "3 levels deep"
Viper.Threads.Monitor.Exit(obj)
Viper.Threads.Monitor.Exit(obj)
Viper.Threads.Monitor.Exit(obj)
PRINT "All 3 exits done"

' --- TryEnterFor ---
PRINT "--- TryEnterFor ---"
Viper.Threads.Monitor.Enter(obj)
DIM ok3 AS INTEGER = Viper.Threads.Monitor.TryEnterFor(obj, 10)
PRINT "TryEnterFor(10ms, re-entrant): "; ok3
Viper.Threads.Monitor.Exit(obj)
Viper.Threads.Monitor.Exit(obj)

' --- Different object ---
PRINT "--- Different object ---"
DIM obj2 AS OBJECT = Viper.Collections.Seq.New()
Viper.Threads.Monitor.Enter(obj2)
PRINT "Monitor on second object acquired"
Viper.Threads.Monitor.Exit(obj2)
PRINT "Monitor on second object released"

PRINT "=== Monitor Audit Complete ==="
END
